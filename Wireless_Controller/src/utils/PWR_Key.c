// PWR_Key.c
#include "PWR_Key.h"
#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_sleep.h"

// -----------------------------------------------------------------------------
// Defaults (you can override these in PWR_Key.h before including this file)
#ifndef PWR_KEY_ACTIVE_LOW
#define PWR_KEY_ACTIVE_LOW        1   // Button pulls the line LOW when pressed
#endif

#ifndef PWR_LATCH_ACTIVE_HIGH
#define PWR_LATCH_ACTIVE_HIGH     1   // Drive HIGH on PWR_Control_PIN to hold power
#endif
// -----------------------------------------------------------------------------

// Weak backlight hook: implement this in your app if you want dimming
__attribute__((weak)) void set_backlight(uint8_t level) { (void)level; }

// Internal state
static uint8_t  BAT_State    = 0;   // 0:no power, 1:key held at boot, 2:running
static uint8_t  Device_State = 0;   // 0:none, 1:sleep, 2:restart
static uint16_t Long_Press   = 0;

static inline bool key_pressed(void) {
    int lvl = gpio_get_level(PWR_KEY_Input_PIN);
    return PWR_KEY_ACTIVE_LOW ? (lvl == 0) : (lvl != 0);
}

static inline void latch_on(void)  {
    gpio_set_level(PWR_Control_PIN, PWR_LATCH_ACTIVE_HIGH ? 1 : 0);
}

static inline void latch_off(void) {
    gpio_set_level(PWR_Control_PIN, PWR_LATCH_ACTIVE_HIGH ? 0 : 1);
}

static inline void configure_GPIO(gpio_num_t pin, gpio_mode_t mode) {
    gpio_reset_pin(pin);
    gpio_set_direction(pin, mode);
}

void PWR_Init(void)
{
    // Key pin (input with appropriate pull)
    configure_GPIO((gpio_num_t)PWR_KEY_Input_PIN, GPIO_MODE_INPUT);
#if PWR_KEY_ACTIVE_LOW
    gpio_pullup_en((gpio_num_t)PWR_KEY_Input_PIN);
    gpio_pulldown_dis((gpio_num_t)PWR_KEY_Input_PIN);
#else
    gpio_pulldown_en((gpio_num_t)PWR_KEY_Input_PIN);
    gpio_pullup_dis((gpio_num_t)PWR_KEY_Input_PIN);
#endif

    // Latch pin (output). Start OFF, then take over if key is held.
    configure_GPIO((gpio_num_t)PWR_Control_PIN, GPIO_MODE_OUTPUT);
    latch_off();
    vTaskDelay(pdMS_TO_TICKS(10));

    if (key_pressed()) {
        BAT_State = 1;          // booted by holding the key
        latch_on();             // take control immediately
    } else {
        BAT_State = 2;          // booted without key (e.g., USB)
        latch_on();             // <-- NEW: keep rail latched so battery takes over when USB is removed
    }
}

void PWR_Loop(void)
{
    if (!BAT_State) return;

    if (key_pressed()) {
        if (BAT_State == 2) {
            if (Long_Press < 0xFFFF) Long_Press++;

            if (Long_Press >= Device_Sleep_Time) {
                if (Long_Press < Device_Restart_Time) {
                    Device_State = 1;   // sleep window
                } else if (Long_Press < Device_Shutdown_Time) {
                    Device_State = 2;   // restart window
                } else {
                    // Held long enough: immediate shutdown while still pressed
                    Shutdown();
                }
            }
        }
    } else {
        // Released
        if (BAT_State == 1) BAT_State = 2;

        if (Device_State == 1)       Fall_Asleep();
        else if (Device_State == 2)  Restart();

        Device_State = 0;
        Long_Press = 0;
    }
}

// ---- Actions you can customize ------------------------------------------------

void Fall_Asleep(void)
{
    // Example: dim backlight and enter light sleep; wake when key pressed again
    set_backlight(5);

    // Configure wake on key (active level depends on your wiring)
#if PWR_KEY_ACTIVE_LOW
    gpio_wakeup_enable((gpio_num_t)PWR_KEY_Input_PIN, GPIO_INTR_LOW_LEVEL);
#else
    gpio_wakeup_enable((gpio_num_t)PWR_KEY_Input_PIN, GPIO_INTR_HIGH_LEVEL);
#endif
    esp_sleep_enable_gpio_wakeup();

    // Enter light sleep (returns on wake)
    esp_light_sleep_start();

    // Woke up
    set_backlight(80);
}

void Restart(void)
{
    // Do any state save if needed, then restart MCU
    esp_restart();
}

void Shutdown(void)
{
    // Turn off UI/backlight, drop the power latch
    set_backlight(0);
    latch_off();
    vTaskDelay(pdMS_TO_TICKS(50));

    // If USB is still powering the board, cut consumption anyway
    esp_deep_sleep_start();
}
