#include "BAT_Driver.h"

static const char *ADC_TAG = "ADC";

float BAT_analogVolts = 0.0f;

static adc_oneshot_unit_handle_t adc1_handle;
static bool          have_cali = false;
static adc_cali_handle_t cali_handle = NULL;

/* keep the last converted values for optional logging */
static int s_raw_avg = 0;
static int s_mv_pin  = 0;

static bool adc_calibration_init(adc_unit_t unit,
                                 adc_channel_t channel,
                                 adc_atten_t atten,
                                 adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_curve_fitting_config_t cfg = {
            .unit_id  = unit,
            .chan     = channel,
            .atten    = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cfg, &handle);
        if (ret == ESP_OK) calibrated = true;
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        adc_cali_line_fitting_config_t cfg = {
            .unit_id  = unit,
            .atten    = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cfg, &handle);
        if (ret == ESP_OK) calibrated = true;
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(ADC_TAG, "ADC calibration: OK");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(ADC_TAG, "ADC eFuse not burnt or scheme not supported; using uncalibrated fallback");
    } else {
        ESP_LOGE(ADC_TAG, "ADC calibration init failed");
    }
    return calibrated;
}

void BAT_Init(void)
{
    /* ADC1 init */
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc1_handle));

    /* Channel config */
    adc_oneshot_chan_cfg_t ch_cfg = {
        .atten    = EXAMPLE_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, EXAMPLE_ADC1_CHAN, &ch_cfg));

    /* Calibration (optional but preferred) */
    have_cali = adc_calibration_init(ADC_UNIT_1, EXAMPLE_ADC1_CHAN, EXAMPLE_ADC_ATTEN, &cali_handle);
}

float BAT_Get_Volts(void)
{
    /* average a few samples to reduce jitter */
    const int N = 8;
    int raw_sum = 0;
    int raw = 0;
    for (int i = 0; i < N; ++i) {
        if (adc_oneshot_read(adc1_handle, EXAMPLE_ADC1_CHAN, &raw) == ESP_OK) raw_sum += raw;
    }
    s_raw_avg = raw_sum / N;

    if (have_cali) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, s_raw_avg, &s_mv_pin));  /* mV at ADC pin */
    } else {
        /* crude fallback assuming ~3300 mV full-scale at 11 dB */
        s_mv_pin = (int)((s_raw_avg * 3300.0f) / 4095.0f + 0.5f);
    }

    const float v_pin = s_mv_pin / 1000.0f;                   /* V at ADC pin   */
    BAT_analogVolts = (v_pin * VBAT_DIVIDER_K) / MEASUREMENT_OFFSET;

    return BAT_analogVolts;
}

/* ---- optional debug accessors ---- */
int BAT_Get_Raw(void)   { return s_raw_avg; }
int BAT_Get_mVPin(void) { return s_mv_pin;  }
