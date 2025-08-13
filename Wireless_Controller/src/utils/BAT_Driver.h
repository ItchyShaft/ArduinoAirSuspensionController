#pragma once

#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

/* -------- Board specifics (Waveshare ESP32-S3 Touch LCD 2.8") -------- */
#define EXAMPLE_ADC1_CHAN   ADC_CHANNEL_7       /* GPIO8 */
#define EXAMPLE_ADC_ATTEN   ADC_ATTEN_DB_12     /* ~0..3.3V at pin */

/* Battery sense math:
 *  Vbat = Vpin * VBAT_DIVIDER_K, then trimmed by MEASUREMENT_OFFSET.
 *  Old code multiplied by 3.0 → keep that as default.
 */
#define VBAT_DIVIDER_K      3.051f
#define MEASUREMENT_OFFSET  1.0f

#ifdef __cplusplus
extern "C" {
#endif

extern float BAT_analogVolts;          /* latest computed battery voltage (V) */

void  BAT_Init(void);
float BAT_Get_Volts(void);

/* Optional: expose last raw/mV values for debug prints */
int   BAT_Get_Raw(void);
int   BAT_Get_mVPin(void);

#ifdef __cplusplus
} // extern "C"
#endif
