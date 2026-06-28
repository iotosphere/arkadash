#include "battery_monitor.h"

#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "app_config.h"

static const char *TAG = "battery";

/* ------------------------------------------------------------------ *
 *  Hardware assumptions (from schematic):
 *    - 18650 Li-Ion, 3.0V (empty) – 4.2V (full)
 *    - 100k+100k resistor divider → ADC sees Vbat / 2
 *    - 3.3V ADC reference, 12-bit (0..4095)
 *    - BATTERY_ADC_PIN is GPIO 48 (defined in app_config.h)
 * ------------------------------------------------------------------ */
#define ADC_ATTEN           ADC_ATTEN_DB_12   /* 0..3.3V full range */
#define ADC_BITWIDTH        ADC_BITWIDTH_12

/* Endpoints (in raw ADC counts, after calibration) */
#define RAW_EMPTY           1862   /* 3.0V / 2 = 1.5V */
#define RAW_FULL            2605   /* 4.2V / 2 = 2.1V */

/* Smoothing window (moving average, samples) */
#define SMOOTH_N            8

/* Charging detection: if raw rose by more than this in one cycle,
 * assume we are in the CC phase of a TP4056 charge cycle. */
#define CHARGING_DV_RAW     4

static adc_oneshot_unit_handle_t s_adc = NULL;
static adc_cali_handle_t s_cali   = NULL;
static adc_unit_t s_unit          = ADC_UNIT_1;
static adc_channel_t s_channel    = ADC_CHANNEL_0;

static uint16_t s_buf[SMOOTH_N]   = {0};
static int      s_buf_idx         = 0;
static int      s_buf_filled      = 0;
static uint16_t s_last_smoothed   = 0;
static bool     s_charging        = false;

static int raw_to_mv(int raw) {
    if (s_cali) {
        int mv = 0;
        if (adc_cali_raw_to_voltage(s_cali, raw, &mv) == ESP_OK) {
            return mv;
        }
    }
    /* Fallback: assume 3.3V ref, 12-bit */
    return (raw * 3300) / 4095;
}

static uint16_t smoothed_add(uint16_t raw) {
    s_buf[s_buf_idx] = raw;
    s_buf_idx = (s_buf_idx + 1) % SMOOTH_N;
    if (s_buf_filled < SMOOTH_N) s_buf_filled++;

    uint32_t sum = 0;
    for (int i = 0; i < s_buf_filled; i++) sum += s_buf[i];
    return (uint16_t)(sum / s_buf_filled);
}

void battery_monitor_init(void) {
    /* --- Resolve which ADC unit owns the requested GPIO --- */
    esp_err_t io_ret = adc_oneshot_io_to_channel(BATTERY_ADC_PIN, &s_unit, &s_channel);
    if (io_ret != ESP_OK) {
        ESP_LOGW(TAG, "GPIO %d has no ADC channel (err=0x%x) — battery monitor disabled",
                 BATTERY_ADC_PIN, io_ret);
        return;   /* s_adc stays NULL; battery_monitor_read() will return defaults */
    }

    /* --- Init the matching oneshot unit (ADC1 or ADC2) --- */
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = s_unit,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, s_channel, &chan_cfg));

    /* --- eFuse calibration (curve fitting) for accurate mV --- */
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = s_unit,
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) != ESP_OK) {
        ESP_LOGW(TAG, "calibration scheme not available, using linear fallback");
        s_cali = NULL;
    }

    ESP_LOGI(TAG, "init ok: pin=%d unit=%d channel=%d",
             BATTERY_ADC_PIN, s_unit, s_channel);
}

void battery_monitor_read(uint8_t *percent_out, bool *charging_out) {
    int raw = 0;
    if (s_adc == NULL || adc_oneshot_read(s_adc, s_channel, &raw) != ESP_OK) {
        if (percent_out)  *percent_out  = 100;
        if (charging_out) *charging_out = false;
        return;
    }

    uint16_t sm = smoothed_add((uint16_t)raw);

    /* --- charging: rising-edge detection on smoothed value --- */
    if (s_last_smoothed != 0 && sm > s_last_smoothed + CHARGING_DV_RAW) {
        s_charging = true;
    } else if (sm < s_last_smoothed) {
        s_charging = false; /* discharging */
    }
    s_last_smoothed = sm;

    /* --- percent mapping (clamped) --- */
    int pct;
    if (sm <= RAW_EMPTY)       pct = 0;
    else if (sm >= RAW_FULL)   pct = 100;
    else                       pct = ((int)sm - RAW_EMPTY) * 100 / (RAW_FULL - RAW_EMPTY);

    if (percent_out)  *percent_out  = (uint8_t)pct;
    if (charging_out) *charging_out = s_charging;

    int mv = raw_to_mv(raw);
    int mv_at_bat = mv * 2;  /* un-divide */
    ESP_LOGD(TAG, "raw=%d sm=%d mv_adc=%d vbat=%d.%03dV pct=%d charging=%d",
             raw, sm, mv, mv_at_bat / 1000, mv_at_bat % 1000, pct, s_charging);
}
