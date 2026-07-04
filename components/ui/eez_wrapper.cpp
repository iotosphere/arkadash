#include <stdint.h>
#include "vars.h"
#include "eez-flow.h"
#include "screens.h"
#include "images.h"
#include <stdio.h>
#include "esp_lvgl_port.h"
#include "esp_log.h"

static const char *TAG = "ui";

using namespace eez;

static const lv_img_dsc_t* weather_code_to_icon(int code)
{
    if (code == 0)                    return &img_sun;
    if (code >= 1 && code <= 3)       return &img_cloudy;
    if (code >= 45 && code <= 48)     return &img_foog;
    if (code >= 51 && code <= 57)     return &img_rainy;
    if (code >= 61 && code <= 67)     return &img_rainy__1_;
    if (code >= 71 && code <= 77)     return &img_snowy;
    if (code >= 80 && code <= 82)     return &img_rainy;
    if (code >= 85 && code <= 86)     return &img_snowy;
    if (code >= 95)                   return &img_storm;
    return &img_sun;
}

extern "C" {

void ui_set_time(int hour, int minute, int second, int date, const char *month_str, int year, const char *day_name)
{
    if (!lvgl_port_lock(500)) {
        ESP_LOGW(TAG, "ui_set_time: LVGL lock timeout, skipping");
        return;
    }

    static char hour_str[3];
    static char minute_str[3];
    static char second_str[3];
    static char date_str[3];

    snprintf(hour_str, sizeof(hour_str), "%02d", hour);
    snprintf(minute_str, sizeof(minute_str), "%02d", minute);
    snprintf(second_str, sizeof(second_str), "%02d", second);
    snprintf(date_str, sizeof(date_str), "%02d", date);

    flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TIME_HOUR, Value(hour_str));
    flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TIME_MINUTE, Value(minute_str));
    flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TIME_SECOND, Value(second_str));
    flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TIME_DATE, Value(date_str));
    if (month_str) {
        flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TIME_MONTH, Value(month_str));
    }
    flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TIME_YEAR, Value(year));
    if (day_name) {
        flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_TIME_DAY_NAME, Value(day_name));
    }

    lvgl_port_unlock();
}

void ui_set_weather(const char *temp, const char *condition, const char *humidity)
{
    if (!lvgl_port_lock(500)) {
        ESP_LOGW(TAG, "ui_set_weather: LVGL lock timeout, skipping");
        return;
    }

    printf("ui_set_weather: temp=%s hum=%s cond=%s\n",
           temp ? temp : "(null)",
           humidity ? humidity : "(null)",
           condition ? condition : "(null)");
    printf("  objects.weather_temp_label=%p\n", (void*)objects.weather_temp_label);
    printf("  objects.weather_hum_label=%p\n", (void*)objects.weather_hum_label);
    printf("  objects.weather_state=%p\n", (void*)objects.weather_state);
    printf("  objects.weather_icon=%p\n", (void*)objects.weather_icon);

    if (temp) {
        flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_WEATHER_TEMP, Value(temp));
    }
    if (condition) {
        flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_WEATHER_CONDITION, Value(condition));
    }
    if (humidity) {
        flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_WEATHER_HUM, Value(humidity));
    }

    if (objects.weather_temp_label && temp) {
        lv_label_set_text(objects.weather_temp_label, temp);
        printf("  -> Set temp label OK\n");
    }
    if (objects.weather_hum_label && humidity) {
        lv_label_set_text(objects.weather_hum_label, humidity);
        printf("  -> Set hum label OK\n");
    }
    if (objects.weather_state && condition) {
        lv_label_set_text(objects.weather_state, condition);
        printf("  -> Set state label OK\n");
    }

    lvgl_port_unlock();
}

void ui_set_weather_icon(int weather_code)
{
    if (!lvgl_port_lock(500)) {
        ESP_LOGW(TAG, "ui_set_weather_icon: LVGL lock timeout, skipping");
        return;
    }

    printf("ui_set_weather_icon: code=%d, weather_icon=%p\n", weather_code, (void*)objects.weather_icon);
    if (objects.weather_icon) {
        const lv_img_dsc_t *icon = weather_code_to_icon(weather_code);
        lv_image_set_src(objects.weather_icon, icon);
        printf("  -> Set icon OK\n");
    }

    lvgl_port_unlock();
}

/* -----------------------------------------------------------------------
 * Battery status → icon mapping.
 *
 * Image set (per asset names in EEZ):
 *   img_charging        → %20  red  (mis-named; intended as low-battery)
 *   img_50              → %50  yellow
 *   img_80              → %80  green
 *   img_full_battery    → %100 green
 *   img_high_battery    → %80  green (alias)
 *   img_plus            → charging overlay (bolt + plus)
 * ----------------------------------------------------------------------- */
static const lv_img_dsc_t* pick_battery_icon(int pct, bool charging)
{
    if (charging) return &img_plus;          /* charging takes priority */
    if (pct < 20)  return &img_charging;     /* red low */
    if (pct < 50)  return &img_50;           /* yellow */
    if (pct < 80)  return &img_80;           /* green mid */
    return &img_full_battery;                /* green full */
}

void ui_set_battery_state(int percent, bool charging)
{
    if (!lvgl_port_lock(500)) {
        ESP_LOGW(TAG, "ui_set_battery_state: LVGL lock timeout, skipping");
        return;
    }

    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;

    printf("ui_set_battery_state: pct=%d charging=%d\n", percent, charging);

    const lv_img_dsc_t *icon = pick_battery_icon(percent, charging);

    /* main screen battery_1 is the only image object that carries the
     * battery glyph across the top-bar variants; mirror it everywhere we
     * can find one so the icon stays consistent regardless of screen. */
    if (objects.battery_1) {
        lv_image_set_src(objects.battery_1, icon);
        printf("  -> Set battery_1 OK (%s)\n", charging ? "plus" :
               percent < 20 ? "charging-as-low" :
               percent < 50 ? "50" :
               percent < 80 ? "80" : "full");
    }
    if (objects.battery) {
        lv_image_set_src(objects.battery, icon);
    }

    lvgl_port_unlock();
}

void ui_set_footer(const char *text)
{
    if (!lvgl_port_lock(500)) {
        ESP_LOGW(TAG, "ui_set_footer: LVGL lock timeout, skipping");
        return;
    }

    // Debounce: ayni text 100ms icinde tekrar set edilirse log'lama.
    // Encoder bounce spam'ini engeller.
    static const char *last_text = nullptr;
    static uint32_t last_ms = 0;
    uint32_t now = lv_tick_get();
    if (!(text && last_text == text && (now - last_ms) < 100)) {
        printf("ui_set_footer: text=%s\n", text ? text : "(null)");
        last_text = text;
        last_ms = now;
    }

    // Update header label on assistant screen
    if (objects.obj4) {
        lv_label_set_text(objects.obj4, text);
    }

    // Update all author (footer) labels
    if (objects.author && text) {
        lv_label_set_text(objects.author, text);
    }
    if (objects.author_1 && text) {
        lv_label_set_text(objects.author_1, text);
    }
    if (objects.author_2 && text) {
        lv_label_set_text(objects.author_2, text);
    }
    if (objects.author_3 && text) {
        lv_label_set_text(objects.author_3, text);
    }
    if (objects.author_4 && text) {
        lv_label_set_text(objects.author_4, text);
    }
    if (objects.author_5 && text) {
        lv_label_set_text(objects.author_5, text);
    }
    if (objects.author_6 && text) {
        lv_label_set_text(objects.author_6, text);
    }
    if (objects.author_7 && text) {
        lv_label_set_text(objects.author_7, text);
    }

    lvgl_port_unlock();
}

/* Settings page sliders — called from main.c when the user moves
 * volume / brightness. They in turn route into the existing audio
 * and display backlight APIs. */
void ui_set_volume(int pct)
{
    if (!lvgl_port_lock(500)) {
        ESP_LOGW(TAG, "ui_set_volume: LVGL lock timeout, skipping");
        return;
    }

    if (objects.volume) {
        /* Clamp to slider's configured range (0..100). */
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        lv_slider_set_value(objects.volume, pct, LV_ANIM_OFF);
    }

    /* Forward to audio_set_volume() even if slider object is missing —
     * audio path is independent of LVGL widget state. */
    extern void audio_set_volume(int pct);
    audio_set_volume(pct);

    lvgl_port_unlock();
}

void ui_set_brightness(int pct)
{
    if (!lvgl_port_lock(500)) {
        ESP_LOGW(TAG, "ui_set_brightness: LVGL lock timeout, skipping");
        return;
    }

    if (objects.brightness) {
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        lv_slider_set_value(objects.brightness, pct, LV_ANIM_OFF);
    }

    /* Forward to display backlight PWM even if slider object is missing. */
    extern void display_backlight_set(uint8_t pct);
    display_backlight_set((uint8_t)pct);

    lvgl_port_unlock();
}

/* LVGL slider drag callbacks — wired in screens.c create_screen_settings.
 * Kullanıcı slider'ı sürüklediğinde bu callback'ler tetiklenir; ilgili
 * audio/display API'sini çağırır. agent tarafı (services/agent.py) ileride
 * set_volume/set_brightness tool'ları ekleyebilir — aynı audio_set_volume
 * ve display_backlight_set fonksiyonlarını çağırarak P4 davranışını
 * sesli komutla da kontrol edebilir. */
void ui_slider_volume_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int pct = (int)lv_slider_get_value(slider);
    extern void audio_set_volume(int pct);
    audio_set_volume(pct);
    ESP_LOGI(TAG, "ui_slider_volume_cb: %d%%", pct);
}

void ui_slider_brightness_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int pct = (int)lv_slider_get_value(slider);
    extern void display_backlight_set(uint8_t pct);
    display_backlight_set((uint8_t)pct);
    ESP_LOGI(TAG, "ui_slider_brightness_cb: %d%%", pct);
}

}
