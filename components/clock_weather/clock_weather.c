#include "clock_weather.h"
#include "ui.h"

#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_http_client.h"
#include "esp_lvgl_port.h"

static const char *TAG = "clock_weather";

#define WEATHER_BUF_SIZE 1024
#define TZ_TR             "<+03>-3"

static char s_weather_buf[WEATHER_BUF_SIZE];
static int  s_weather_len = 0;

static const char *weather_code_to_str(int code)
{
    if (code == 0)                    return "Clear";
    if (code >= 1 && code <= 3)       return "Partly Cloudy";
    if (code >= 45 && code <= 48)     return "Fog";
    if (code >= 51 && code <= 55)     return "Drizzle";
    if (code >= 56 && code <= 57)     return "Freezing Drizzle";
    if (code >= 61 && code <= 65)     return "Rain";
    if (code >= 66 && code <= 67)     return "Freezing Rain";
    if (code >= 71 && code <= 77)     return "Snow";
    if (code >= 80 && code <= 82)     return "Rain Showers";
    if (code >= 85 && code <= 86)     return "Snow Showers";
    if (code >= 95)                   return "Thunderstorm";
    return "Unknown";
}

static const char *day_name_tr(int wday)
{
    static const char *names[] = {
        "Pazar", "Pazartesi", "Sali", "Carsamba",
        "Persembe", "Cuma", "Cumartesi"
    };
    if (wday >= 0 && wday <= 6) return names[wday];
    return "?";
}

/* ── SNTP ─────────────────────────────────────────────────────────── */

static void sntp_sync_cb(struct timeval *tv)
{
    (void)tv;
    ESP_LOGI(TAG, "SNTP sync completed");
}

static void init_sntp(void)
{
    ESP_LOGI(TAG, "SNTP init");

    setenv("TZ", TZ_TR, 1);
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_set_time_sync_notification_cb(sntp_sync_cb);
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP started, waiting for sync...");
}

static void clock_task(void *pv)
{
    (void)pv;

    /* SNTP sync bekle */
    int retry = 0;
    while (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && retry < 30) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
    }

    if (retry >= 30) {
        ESP_LOGW(TAG, "SNTP sync timeout, using default time");
    } else {
        ESP_LOGI(TAG, "Time synced");
    }

    /* Sürekli güncelle */
    while (1) {
        time_t now;
        struct tm t;
        time(&now);
        localtime_r(&now, &t);

        if (lvgl_port_lock(pdMS_TO_TICKS(100))) {
            ui_set_time(t.tm_hour, t.tm_min, t.tm_sec,
                        t.tm_mday, t.tm_mon + 1, t.tm_year + 1900,
                        day_name_tr(t.tm_wday));
            lvgl_port_unlock();
        } else {
            ESP_LOGW(TAG, "LVGL lock failed for time update");
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ── Weather HTTP ─────────────────────────────────────────────────── */

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (s_weather_len + evt->data_len < WEATHER_BUF_SIZE - 1) {
                memcpy(s_weather_buf + s_weather_len, evt->data, evt->data_len);
                s_weather_len += evt->data_len;
            }
            break;
        default:
            break;
        }
    return ESP_OK;
}

static void fetch_weather(void)
{
    s_weather_len = 0;
    memset(s_weather_buf, 0, sizeof(s_weather_buf));

    const char *url = "http://api.open-meteo.com/v1/forecast?"
                      "latitude=41.01&longitude=28.98&"
                      "current=temperature_2m,relative_humidity_2m,weather_code&"
                      "timezone=auto";

    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = http_event_handler,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "HTTP client init failed");
        return;
    }

    ESP_LOGI(TAG, "Fetching weather...");
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP status: %d, received %d bytes", status, s_weather_len);

        s_weather_buf[s_weather_len] = '\0';
        ESP_LOGI(TAG, "Response (%d bytes): %s", s_weather_len, s_weather_buf);

        if (status != 200) {
            ESP_LOGW(TAG, "Bad HTTP status: %d", status);
            esp_http_client_cleanup(client);
            return;
        }

        /* Basit JSON parse - "current":{ bloğundan sonra ara */
        float temp = 0;
        int   hum  = 0;
        int   code = 0;

        char *p = strstr(s_weather_buf, "\"current\":{");
        if (!p) {
            ESP_LOGW(TAG, "\"current\":{ not found");
            esp_http_client_cleanup(client);
            return;
        }

        char *q;
        q = strstr(p, "\"temperature_2m\":");
        if (q) {
            ESP_LOGI(TAG, "Found temperature_2m at offset, raw: %.30s", q);
            temp = strtof(q + 17, NULL);
        }
        else { ESP_LOGW(TAG, "temperature_2m not found in current"); }

        q = strstr(p, "\"relative_humidity_2m\":");
        if (q) {
            ESP_LOGI(TAG, "Found relative_humidity_2m at offset, raw: %.30s", q);
            /* Find the colon and skip past it */
            char *colon = strchr(q, ':');
            if (colon) {
                hum = (int)strtol(colon + 1, NULL, 10);
                ESP_LOGI(TAG, "  humidity parsed: %d from: %.10s", hum, colon + 1);
            } else {
                ESP_LOGW(TAG, "  colon not found after relative_humidity_2m");
            }
        }
        else { ESP_LOGW(TAG, "relative_humidity_2m not found in current"); }

        q = strstr(p, "\"weather_code\":");
        if (q) {
            ESP_LOGI(TAG, "Found weather_code at offset, raw: %.30s", q);
            char *colon = strchr(q, ':');
            if (colon) {
                code = (int)strtol(colon + 1, NULL, 10);
                ESP_LOGI(TAG, "  weather_code parsed: %d from: %.10s", code, colon + 1);
            } else {
                ESP_LOGW(TAG, "  colon not found after weather_code");
            }
        }
        else { ESP_LOGW(TAG, "weather_code not found in current"); }

        char temp_str[16];
        char hum_str[16];
        snprintf(temp_str, sizeof(temp_str), "%.1f", temp);
        snprintf(hum_str, sizeof(hum_str), "%d", hum);

        if (lvgl_port_lock(pdMS_TO_TICKS(100))) {
            ui_set_weather(temp_str, weather_code_to_str(code), hum_str);
            ui_set_weather_icon(code);
            lvgl_port_unlock();
            ESP_LOGI(TAG, "Weather: %.0fC, %d%%, %s (code %d)",
                     temp, hum, weather_code_to_str(code), code);
        } else {
            ESP_LOGW(TAG, "LVGL lock failed for weather update");
        }
    } else {
        ESP_LOGW(TAG, "Weather fetch failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

static void weather_task(void *pv)
{
    (void)pv;

    /* WiFi bağlanana kadar bekle */
    vTaskDelay(pdMS_TO_TICKS(5000));

    while (1) {
        fetch_weather();
        vTaskDelay(pdMS_TO_TICKS(600000)); /* 10 dakikada bir güncelle */
    }
}

/* ── Init ─────────────────────────────────────────────────────────── */

esp_err_t clock_weather_init(void)
{
    ESP_LOGI(TAG, "Clock + Weather init");

    init_sntp();

    xTaskCreatePinnedToCore(clock_task, "clock_task", 4096, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(weather_task, "weather_task", 8192, NULL, 3, NULL, 1);

    ESP_LOGI(TAG, "Clock + Weather tasks started");
    return ESP_OK;
}
