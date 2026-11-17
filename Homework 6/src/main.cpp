#include <cstdio>
#include <cstdint>
#include <array>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_err.h"

static constexpr const char* TAG = "ELEVATOR";

// --- REQUEST INDICATOR LED PINS ---
static constexpr gpio_num_t LED_FLOOR_1   = GPIO_NUM_25;
static constexpr gpio_num_t LED_FLOOR_2   = GPIO_NUM_26;
static constexpr gpio_num_t LED_FLOOR_3   = GPIO_NUM_27;
static constexpr gpio_num_t LED_INDICATOR = GPIO_NUM_19;

// --- BUTTONS (Internal Pull-Up) ---
static constexpr gpio_num_t BTN_FLOOR_1   = GPIO_NUM_33;
static constexpr gpio_num_t BTN_FLOOR_2   = GPIO_NUM_16;
static constexpr gpio_num_t BTN_FLOOR_3   = GPIO_NUM_17;

// --- PASSIVE BUZZER (LEDC PWM) ---
static constexpr gpio_num_t BUZZER_PIN         = GPIO_NUM_32;
static constexpr ledc_timer_t BUZZER_TIMER     = LEDC_TIMER_0;
static constexpr ledc_mode_t BUZZER_MODE       = LEDC_LOW_SPEED_MODE;
static constexpr ledc_channel_t BUZZER_CHANNEL = LEDC_CHANNEL_0;
static constexpr ledc_timer_bit_t BUZZER_RES   = LEDC_TIMER_10_BIT;

// --- 74HC595 SHIFT REGISTER CONNECTIONS ---
static constexpr gpio_num_t DATA_PIN  = GPIO_NUM_23; // DS
static constexpr gpio_num_t CLOCK_PIN = GPIO_NUM_18; // SHCP
static constexpr gpio_num_t LATCH_PIN = GPIO_NUM_22; // STCP

// --- 7-SEGMENT DISPLAY CONFIG ---
static constexpr bool COMMON_ANODE = false;

// --- TIMING CONSTANTS (Microseconds) ---
static constexpr int64_t DEBOUNCE_TIME_US      = 25000;   // 25 ms button debounce
static constexpr int64_t DOORS_CLOSING_TIME_US = 1500000; // 1.5 s door closing interval
static constexpr int64_t TRAVEL_TIME_US        = 2000000; // 2.0 s per floor travel time
static constexpr int64_t DOORS_OPEN_MIN_US     = 1500000; // 1.5 s minimum doors open after arrival

// Segment order: A B C D E F G DP
static constexpr std::array<uint8_t, 16> BYTE_ENCODINGS = {
    0b11111100, // 0
    0b01100000, // 1
    0b11011010, // 2
    0b11110010, // 3
    0b01100110, // 4
    0b10110110, // 5
    0b10111110, // 6
    0b11100000, // 7
    0b11111110, // 8
    0b11110110, // 9
    0b11101110, // A
    0b00111110, // b
    0b10011100, // C
    0b01111010, // d
    0b10011110, // E
    0b10001110  // F
};

enum class ElevatorState {
    DOORS_OPEN,
    DOORS_CLOSING,
    MOVING,
    ARRIVING
};

struct ButtonState {
    gpio_num_t pin;
    int floor_number;
    int last_raw_state;
    int stable_state;
    int64_t last_change_us;
};

// --- SIMPLE FIFO QUEUE (Array-based, max 3 floors) ---
static int request_queue[3] = {0, 0, 0};
static int queue_size       = 0;

static bool is_floor_in_queue(int floor) {
    for (int i = 0; i < queue_size; ++i) {
        if (request_queue[i] == floor) {
            return true;
        }
    }
    return false;
}

static bool enqueue_request(int floor) {
    if (is_floor_in_queue(floor) || queue_size >= 3) {
        return false; // Ignore duplicate or full queue
    }
    request_queue[queue_size] = floor;
    queue_size++;
    return true;
}

static void dequeue_front_request() {
    if (queue_size == 0) {
        return;
    }
    for (int i = 0; i < queue_size - 1; ++i) {
        request_queue[i] = request_queue[i + 1];
    }
    queue_size--;
}

// --- HELPER: SHIFT OUT TO 74HC595 (MSBFIRST) ---
static void shift_out_595(uint8_t data) {
    const uint8_t out_byte = COMMON_ANODE ? static_cast<uint8_t>(~data) : data;

    gpio_set_level(LATCH_PIN, 0);
    for (int i = 7; i >= 0; --i) {
        gpio_set_level(CLOCK_PIN, 0);
        gpio_set_level(DATA_PIN, (out_byte >> i) & 0x01);
        gpio_set_level(CLOCK_PIN, 1);
    }
    gpio_set_level(LATCH_PIN, 1);
}

// --- HELPER: PASSIVE BUZZER & SOUND PATTERNS ---
static void buzzer_init() {
    ledc_timer_config_t timer_conf{};
    timer_conf.speed_mode      = BUZZER_MODE;
    timer_conf.duty_resolution = BUZZER_RES;
    timer_conf.timer_num       = BUZZER_TIMER;
    timer_conf.freq_hz         = 2000;
    timer_conf.clk_cfg         = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t channel_conf{};
    channel_conf.gpio_num   = BUZZER_PIN;
    channel_conf.speed_mode = BUZZER_MODE;
    channel_conf.channel    = BUZZER_CHANNEL;
    channel_conf.intr_type  = LEDC_INTR_DISABLE;
    channel_conf.timer_sel  = BUZZER_TIMER;
    channel_conf.duty       = 0;
    channel_conf.hpoint     = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&channel_conf));
}

static void buzzer_beep(uint32_t freq_hz, uint32_t duration_ms) {
    ledc_set_freq(BUZZER_MODE, BUZZER_TIMER, freq_hz);
    ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 512);
    ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL);

    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    ledc_set_duty(BUZZER_MODE, BUZZER_CHANNEL, 0);
    ledc_update_duty(BUZZER_MODE, BUZZER_CHANNEL);
}

// Short low warning tone when doors start closing
static void play_doors_closing_sound() {
    buzzer_beep(800, 120);
}

// Distinct two-tone ascending chime upon arrival
static void play_arrival_sound() {
    buzzer_beep(1200, 120);
    vTaskDelay(pdMS_TO_TICKS(40));
    buzzer_beep(1800, 200);
}

// --- HELPER: GPIO INITIALIZATION ---
static void gpio_init_all() {
    gpio_config_t out_conf{};
    out_conf.pin_bit_mask = (1ULL << LED_FLOOR_1) |
                            (1ULL << LED_FLOOR_2) |
                            (1ULL << LED_FLOOR_3) |
                            (1ULL << LED_INDICATOR) |
                            (1ULL << DATA_PIN) |
                            (1ULL << CLOCK_PIN) |
                            (1ULL << LATCH_PIN);
    out_conf.mode         = GPIO_MODE_OUTPUT;
    out_conf.pull_up_en   = GPIO_PULLUP_DISABLE;
    out_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    out_conf.intr_type    = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&out_conf));

    gpio_config_t in_conf{};
    in_conf.pin_bit_mask = (1ULL << BTN_FLOOR_1) |
                           (1ULL << BTN_FLOOR_2) |
                           (1ULL << BTN_FLOOR_3);
    in_conf.mode         = GPIO_MODE_INPUT;
    in_conf.pull_up_en   = GPIO_PULLUP_ENABLE;
    in_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    in_conf.intr_type    = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&in_conf));
}

// --- UPDATE 7-SEGMENT DISPLAY (CURRENT POSITION) ---
static void update_7seg_display(int floor) {
    if (floor >= 0 && static_cast<size_t>(floor) < BYTE_ENCODINGS.size()) {
        shift_out_595(BYTE_ENCODINGS[floor]);
    }
}

// --- UPDATE REQUEST INDICATOR LEDS (PENDING REQUESTS) ---
static void update_request_leds() {
    gpio_set_level(LED_FLOOR_1, is_floor_in_queue(1) ? 1 : 0);
    gpio_set_level(LED_FLOOR_2, is_floor_in_queue(2) ? 1 : 0);
    gpio_set_level(LED_FLOOR_3, is_floor_in_queue(3) ? 1 : 0);
}

// --- DEBOUNCING + EDGE DETECTION ---
static bool is_button_pressed_edge(ButtonState& btn, int64_t now_us) {
    const int current_raw = gpio_get_level(btn.pin);

    if (current_raw != btn.last_raw_state) {
        btn.last_raw_state = current_raw;
        btn.last_change_us = now_us;
    }

    if ((now_us - btn.last_change_us) >= DEBOUNCE_TIME_US) {
        if (current_raw != btn.stable_state) {
            btn.stable_state = current_raw;
            if (btn.stable_state == 0) {
                return true;
            }
        }
    }

    return false;
}

extern "C" void app_main(void) {
    gpio_init_all();
    buzzer_init();

    std::array<ButtonState, 3> buttons = {{
        {BTN_FLOOR_1, 1, 1, 1, 0},
        {BTN_FLOOR_2, 2, 1, 1, 0},
        {BTN_FLOOR_3, 3, 1, 1, 0}
    }};

    ElevatorState state    = ElevatorState::DOORS_OPEN;
    int current_floor      = 1;
    int active_target      = 1;
    int indicatorState = 0;
    int64_t state_timer_us = 0;
    int64_t indicator_timer = 0;
    const int64_t blink_interval = 500000; 

    gpio_set_level(LED_INDICATOR, 0);
    update_7seg_display(current_floor);
    update_request_leds();
    ESP_LOGI(TAG, "State: DOORS_OPEN | Ready at floor %d", current_floor);

    while (true) {
        const int64_t now_us = esp_timer_get_time();

        // 1. Scan buttons and add unique requests to the FIFO array
        for (auto& btn : buttons) {
            if (is_button_pressed_edge(btn, now_us)) {
                if (btn.floor_number == current_floor &&
                    (state == ElevatorState::DOORS_OPEN || state == ElevatorState::ARRIVING)) {
                    ESP_LOGI(TAG, "Already at floor %d with doors open", current_floor);
                    continue;
                }

                if (enqueue_request(btn.floor_number)) {
                    update_request_leds();
                    ESP_LOGI(TAG, "Request accepted: Floor %d (LED ON)", btn.floor_number);
                }
            }
        }

        // 2. Elevator Finite State Machine
        switch (state) {
            case ElevatorState::DOORS_OPEN: {
                if (queue_size > 0 && (now_us - state_timer_us) >= DOORS_OPEN_MIN_US) {
                    active_target  = request_queue[0];
                    state          = ElevatorState::DOORS_CLOSING;
                    state_timer_us = now_us;

                    ESP_LOGI(TAG, "State: DOORS_CLOSING | Preparing to move to floor %d", active_target);
                    play_doors_closing_sound();
                }
                break;
            }

            case ElevatorState::DOORS_CLOSING: {
                if ((now_us - state_timer_us) >= DOORS_CLOSING_TIME_US) {
                    state          = ElevatorState::MOVING;
                    state_timer_us = now_us;
                    gpio_set_level(LED_INDICATOR, 1);
                    indicatorState = 1;
                    indicator_timer = now_us;
                    ESP_LOGI(TAG, "State: MOVING | From floor %d toward floor %d", current_floor, active_target);
                }
                break;
            }

            case ElevatorState::MOVING: {
                if((now_us - indicator_timer) >= blink_interval){
                    indicator_timer = now_us;
                    gpio_set_level(LED_INDICATOR, 1 - indicatorState);
                    indicatorState = 1 - indicatorState;
                }
                if ((now_us - state_timer_us) >= TRAVEL_TIME_US) {
                    state_timer_us = now_us;

                    if (active_target > current_floor) {
                        current_floor++;
                    } else if (active_target < current_floor) {
                        current_floor--;
                    }

                    update_7seg_display(current_floor);
                    ESP_LOGI(TAG, "Passing floor %d", current_floor);

                    if (current_floor == active_target) {
                        state = ElevatorState::ARRIVING;
                    }
                }
                break;
            }

            case ElevatorState::ARRIVING: {
                gpio_set_level(LED_INDICATOR, 0);
                indicatorState = 0;
                dequeue_front_request();
                update_request_leds();

                ESP_LOGI(TAG, "State: ARRIVING / DOORS_OPENING | Reached floor %d (Request LED OFF)", current_floor);
                play_arrival_sound();

                state          = ElevatorState::DOORS_OPEN;
                state_timer_us = esp_timer_get_time();
                ESP_LOGI(TAG, "State: DOORS_OPEN | Waiting at floor %d", current_floor);
                break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}