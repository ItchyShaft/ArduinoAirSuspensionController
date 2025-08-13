#include "tasks.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

bool ps3ServiceStarted = false;

static void task_bluetooth(void *parameters)
{
    vTaskDelay(pdMS_TO_TICKS(200));          // use FreeRTOS delay
    Serial.println(F("Bluetooth Rest Service Beginning"));

    ble_setup();
    vTaskDelay(pdMS_TO_TICKS(10));
    for (;;)
    {
        ble_loop();
        vTaskDelay(pdMS_TO_TICKS(10));       // yield so other tasks run
    }
}

void setup_tasks()
{
    // Choose a sane priority; 2–5 is typical. Higher than loopTask if you really need it.
    constexpr UBaseType_t BLUETOOTH_PRIO = tskIDLE_PRIORITY + 3;  // 3

    // Stack depth is in *words* (4 bytes each). 3072 words = 12 KB.
    constexpr uint32_t BLUETOOTH_STACK_WORDS = 3072;

    xTaskCreate(
        task_bluetooth,
        "Bluetooth",
        BLUETOOTH_STACK_WORDS,
        nullptr,
        BLUETOOTH_PRIO,
        nullptr);
    // If you want to pin to a core on S3:
    // xTaskCreatePinnedToCore(task_bluetooth, "Bluetooth", BLUETOOTH_STACK_WORDS, nullptr, BLUETOOTH_PRIO, nullptr, APP_CPU_NUM);
}
