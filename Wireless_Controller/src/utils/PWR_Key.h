#pragma once
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stdint.h>

#define PWR_KEY_Input_PIN    6   // Key_BAT
#define PWR_Control_PIN      7   // BAT_Control
#define PWR_KEY_ACTIVE_LOW   1   // button pulls low when pressed
#define PWR_LATCH_ACTIVE_HIGH 1  // drive HIGH to hold power

// Long-press thresholds are in loop "ticks" (calls to PWR_Loop)
#define Device_Sleep_Time     10  // e.g., 10 ticks
#define Device_Restart_Time   15
#define Device_Shutdown_Time  20

#ifdef __cplusplus
extern "C" {
#endif

void PWR_Init(void);
void PWR_Loop(void);

// Optional lifecycle hooks (override in your app if you want custom behavior)
void Fall_Asleep(void);
void Restart(void);
void Shutdown(void);

// Optional: expose battery/power presence
void PWR_SetBatteryPresent(bool present);

// Optional backlight hook implemented in your project (defaults to no-op)
void set_backlight(uint8_t level); // 0..100

#ifdef __cplusplus
}
#endif
