#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "driver/i2s_std.h"
#include "wifi_station.h"
#include "display.h"
#include "encoder.h"
#include "ui.h"
#include "clock_weather.h"
#include "app_config.h"
#include "screens.h"
#include "bricks_breaker.h"
#include "audio.h"
#include "images.h"
#include "websocket_client.h"

static const char *TAG = "app_main";
#define WIFI_SSID "SUPERONLINE_WiFi_5292"
#define WIFI_PASS "EJPus6hfjy7z"
#define LONG_PRESS_MS 800u
#define SLEEP_TIMEOUT_MS 30000u
#define REC_BUF_SIZE (16000*2*5)

#define VOICE_SERVER_URI "ws://192.168.1.18:8765"

static bool is_recording = false;
static bool is_ai_speaking = false;
static uint32_t kitt_anim_tick = 0;
static uint8_t *s_rec_buf = NULL;
static size_t s_rec_bytes = 0;

static bool push_handled = false;
static bool menu_just_loaded = false;
static bool long_press_triggered = false;
static bool pending_menu_load = false;
static uint32_t push_start_ms = 0;
static uint32_t game_just_stopped_ms = 0;
static bool display_sleeping = false;
static uint32_t last_activity_tick = 0;
static bool wake_up_just_happened = false;
static lv_group_t *group_menu = NULL;
static lv_indev_t *encoder_indev = NULL;

static void kitt_anim(void)
{
    if (!is_ai_speaking && !is_recording) {
        if (lvgl_port_lock(0)) {
            if (objects.kitt_bar_left) lv_bar_set_value(objects.kitt_bar_left, 0, LV_ANIM_OFF);
            if (objects.kitt_bar_center) lv_bar_set_value(objects.kitt_bar_center, 0, LV_ANIM_OFF);
            if (objects.kitt_bar_right) lv_bar_set_value(objects.kitt_bar_right, 0, LV_ANIM_OFF);
            lvgl_port_unlock();
        }
        return;
    }
    kitt_anim_tick++;
    int p = (kitt_anim_tick / 5) % 6;
    int l = 0, c = 0, r = 0;
    if (is_recording) {
        switch (p) { case 0: l=80; break; case 1: l=40; c=80; break; case 2: c=100; break; case 3: c=80; r=40; break; case 4: r=80; break; case 5: c=40; r=40; break; }
    } else {
        l = (kitt_anim_tick*7 + kitt_anim_tick/3) % 100;
        c = (kitt_anim_tick*11 + kitt_anim_tick/2) % 100;
        r = (kitt_anim_tick*13 + kitt_anim_tick) % 100;
        c = (c+30) > 100 ? 100 : c+30;
    }
    if (lvgl_port_lock(0)) {
        if (objects.kitt_bar_left) lv_bar_set_value(objects.kitt_bar_left, l, LV_ANIM_OFF);
        if (objects.kitt_bar_center) lv_bar_set_value(objects.kitt_bar_center, c, LV_ANIM_OFF);
        if (objects.kitt_bar_right) lv_bar_set_value(objects.kitt_bar_right, r, LV_ANIM_OFF);
        lvgl_port_unlock();
    }
}

static void chat_task(void *pv)
{
    (void)pv;
    bool last_k0 = false;
    uint32_t k0_start = 0;
    void *rx = audio_get_rx_handle();
    void *tx = audio_get_tx_handle();

    s_rec_buf = heap_caps_malloc(REC_BUF_SIZE, MALLOC_CAP_DMA);
    if (!s_rec_buf) { ESP_LOGE(TAG, "No mem"); vTaskDelete(NULL); return; }

    ESP_LOGI(TAG, "Chat task started RX=%p TX=%p", rx, tx);
    
    // Connect to voice server
    ESP_LOGI(TAG, "Connecting to voice server...");
    ws_init(VOICE_SERVER_URI);
    if (ws_connect() == ESP_OK) {
        ESP_LOGI(TAG, "Voice server connected!");
    } else {
        ESP_LOGE(TAG, "Voice server connection failed!");
    }
    
    while (1) {
        bool k0 = key0_pressed();
        lv_obj_t *cur = lv_scr_act();
        if (cur == objects.chat && !display_sleeping) {
            if (k0 && !last_k0) { vTaskDelay(pdMS_TO_TICKS(30)); k0 = key0_pressed(); }
            if (k0 && !last_k0 && rx && tx) {
                ws_clear_response();  // drain any stale audio before recording
                is_recording = true; k0_start = lv_tick_get(); s_rec_bytes = 0;
                if (lvgl_port_lock(0)) { lv_image_set_src(objects.mic, &img_record_button); lvgl_port_unlock(); }
                ESP_LOGI(TAG, "REC START");
                while (key0_pressed() && is_recording && s_rec_bytes < REC_BUF_SIZE) {
                    size_t ck = (s_rec_bytes+2048>REC_BUF_SIZE)?(REC_BUF_SIZE-s_rec_bytes):2048;
                    size_t rd = 0;
                    esp_err_t ret = i2s_channel_read((i2s_chan_handle_t)rx, s_rec_buf+s_rec_bytes, ck, &rd, pdMS_TO_TICKS(1000));
                    if (ret == ESP_OK && rd > 0) {
                        s_rec_bytes += rd;
                        ESP_LOGI(TAG, "REC %d bytes, total=%d", (int)rd, (int)s_rec_bytes);
                    }
                    kitt_anim(); vTaskDelay(pdMS_TO_TICKS(10));
                }
                uint32_t held = lv_tick_get() - k0_start;
                is_recording = false;
                if (lvgl_port_lock(0)) { lv_image_set_src(objects.mic, &img_microphone); lvgl_port_unlock(); }
                ESP_LOGI(TAG, "REC STOP %lu ms %d bytes", (unsigned long)held, (int)s_rec_bytes);
                
                if (held > 100 && s_rec_bytes > 4000) {
                    // Send to voice server and get AI response
                    ESP_LOGI(TAG, "Sending %d bytes to voice server...", (int)s_rec_bytes);
                    ws_clear_response();
                    ws_set_waiting(true);
                    
                    if (ws_send_audio(s_rec_buf, s_rec_bytes) == ESP_OK) {
                        ESP_LOGI(TAG, "Waiting for TTS response...");
                        is_ai_speaking = true;

                        uint8_t play_buf[4096];
                        size_t total_played = 0;
                        int empty_count = 0;
                        int timeout_count = 0;
                        const int MAX_EMPTY = 200;      // 1s empty after tts_end (5ms * 200)
                        const int MAX_TIMEOUT = 6000;   // 30s total timeout (5ms * 6000)

                        while (timeout_count < MAX_TIMEOUT) {
                            size_t chunk = ws_stream_read(play_buf, sizeof(play_buf), 20);
                            if (chunk > 0) {
                                size_t written = 0;
                                esp_err_t wret = i2s_channel_write((i2s_chan_handle_t)tx, play_buf, chunk, &written, pdMS_TO_TICKS(100));
                                if (wret == ESP_OK && written > 0) {
                                    total_played += written;
                                }
                                empty_count = 0;
                            } else {
                                empty_count++;
                            }

                            // Exit when tts_end received and ring buffer empty for 1s
                            if (ws_is_tts_complete() && empty_count > MAX_EMPTY) {
                                ESP_LOGI(TAG, "Streaming complete, played %d bytes", (int)total_played);
                                break;
                            }

                            timeout_count++;
                            vTaskDelay(pdMS_TO_TICKS(5));
                        }

                        if (timeout_count >= MAX_TIMEOUT) {
                            ESP_LOGW(TAG, "Streaming timeout, played %d bytes", (int)total_played);
                        }

                        is_ai_speaking = false;

                        float actual_dur = (float)total_played / (16000 * 2 * 2);
                        ESP_LOGI(TAG, "PLAY DONE: %d bytes (%.2fs)", (int)total_played, actual_dur);

                        ws_set_waiting(false);
                        ws_set_tts_complete(false);
                        ws_stream_clear();
                    } else {
                        ESP_LOGE(TAG, "Failed to send audio to voice server");
                    }
                }
                s_rec_bytes = 0;
            }
        } else {
            if (is_recording || is_ai_speaking) {
                is_recording = false; is_ai_speaking = false; s_rec_bytes = 0;
                if (lvgl_port_lock(0)) { lv_image_set_src(objects.mic, &img_microphone); lvgl_port_unlock(); }
            }
        }
        kitt_anim();
        last_k0 = key0_pressed();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void encoder_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    bool pushed = encoder_button_pressed();
    lv_obj_t *cur = lv_scr_act();
    if (display_sleeping && pushed) {
        display_backlight_on(); display_sleeping = false; wake_up_just_happened = true;
        last_activity_tick = lv_tick_get(); push_handled = true;
        data->enc_diff = 0; data->state = LV_INDEV_STATE_REL; return;
    }
    if (wake_up_just_happened && pushed) { data->enc_diff = 0; data->state = LV_INDEV_STATE_REL; return; }
    if (wake_up_just_happened && !pushed) { wake_up_just_happened = false; push_handled = false; }
    if (!display_sleeping) {
        int32_t diff = encoder_get_diff();
        if (diff != 0 || pushed) last_activity_tick = lv_tick_get();
        if (cur == objects.games) data->enc_diff = 0; else data->enc_diff = diff;
    } else { data->enc_diff = 0; }
    if (cur == objects.menu) {
        if (pushed) { if (menu_just_loaded) { data->state=LV_INDEV_STATE_REL; data->enc_diff=0; return; } data->state=LV_INDEV_STATE_PR; }
        else { if (menu_just_loaded) { menu_just_loaded=false; push_handled=false; } long_press_triggered=false; data->state=LV_INDEV_STATE_REL; }
        return;
    }
    if (cur == objects.games) {
        if (game_just_stopped_ms > 0 && (lv_tick_get()-game_just_stopped_ms<300)) { data->enc_diff=0; data->state=LV_INDEV_STATE_REL; push_handled=true; return; }
        game_just_stopped_ms = 0;
        if (pushed) { if (!push_handled) { push_handled=true; push_start_ms=lv_tick_get(); }
            if (lv_tick_get()-push_start_ms>=LONG_PRESS_MS) { if (lvgl_port_lock(0)) { pending_menu_load=false; lv_scr_load(objects.menu); lv_indev_set_group(encoder_indev,group_menu); if(objects.chat_button) lv_group_focus_obj(objects.chat_button); encoder_reset_count(); menu_just_loaded=true; push_handled=true; long_press_triggered=true; lvgl_port_unlock(); } data->state=LV_INDEV_STATE_REL; data->enc_diff=0; return; } }
        else { if (push_handled && !long_press_triggered && lv_tick_get()-push_start_ms<LONG_PRESS_MS && lv_tick_get()-push_start_ms>0) { if (lvgl_port_lock(0)) { bricks_breaker_start(); lvgl_port_unlock(); } } push_handled=false; long_press_triggered=false; }
        data->enc_diff=0; data->state=LV_INDEV_STATE_REL; return;
    }
    if (pushed) {
        if (!push_handled) { push_start_ms=lv_tick_get(); push_handled=true;
            if (cur==objects.splash) { data->state=LV_INDEV_STATE_REL; data->enc_diff=0; return; }
            if (cur==objects.main) { if (lvgl_port_lock(0)) { lv_scr_load(objects.menu); lv_indev_set_group(indev,group_menu); if(objects.chat_button) lv_group_focus_obj(objects.chat_button); encoder_reset_count(); lvgl_port_unlock(); } menu_just_loaded=true; data->state=LV_INDEV_STATE_REL; data->enc_diff=0; return; }
        }
        if (long_press_triggered) { data->state=LV_INDEV_STATE_REL; data->enc_diff=0; return; }
        if (lv_tick_get()-push_start_ms>=LONG_PRESS_MS) { long_press_triggered=true; pending_menu_load=true; menu_just_loaded=true; data->state=LV_INDEV_STATE_REL; data->enc_diff=0; return; }
        data->state=LV_INDEV_STATE_PR; data->enc_diff=0;
    } else { push_handled=false; long_press_triggered=false; data->state=LV_INDEV_STATE_REL; }
}

static void display_task(void *pv)
{
    (void)pv;
    lv_indev_set_read_cb(encoder_indev, encoder_read);
    lv_indev_set_group(encoder_indev, group_menu);
    lv_indev_enable(encoder_indev, true);
    if (lvgl_port_lock(0)) { 
        ui_init(); 
        lv_scr_load(objects.splash); 
        lv_indev_set_group(encoder_indev, NULL); 
        lvgl_port_unlock(); 
    }
    if (group_menu) {
        if (objects.chat_button) lv_group_add_obj(group_menu, objects.chat_button);
        if (objects.button_agent_button_) lv_group_add_obj(group_menu, objects.button_agent_button_);
        if (objects.button_smart_home_button_) lv_group_add_obj(group_menu, objects.button_smart_home_button_);
        if (objects.button_games_button_) lv_group_add_obj(group_menu, objects.button_games_button_);
        if (objects.button_settings_button_) lv_group_add_obj(group_menu, objects.button_settings_button_);
        if (objects.button_about_button_) lv_group_add_obj(group_menu, objects.button_about_button_);
    }
    last_activity_tick = lv_tick_get();
    ESP_LOGI(TAG, "Display task ready");
    while (1) {
        if (!display_sleeping) {
            if (lvgl_port_lock(0)) {
                ui_tick();
                if (pending_menu_load) { pending_menu_load=false; lv_scr_load(objects.menu); lv_indev_set_group(encoder_indev,group_menu); if(objects.chat_button) lv_group_focus_obj(objects.chat_button); encoder_reset_count(); }
                lvgl_port_unlock();
            }
            if (lv_scr_act()!=objects.games) { uint32_t idle=lv_tick_get()-last_activity_tick; if (idle>=SLEEP_TIMEOUT_MS) { display_backlight_off(); display_sleeping=true; ESP_LOGI(TAG,"Display sleep"); } }
        } else { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "========== System ==========");
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(display_init());
    ESP_ERROR_CHECK(encoder_init());
    ESP_LOGI(TAG, "LVGL...");
    const lvgl_port_cfg_t lcfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lcfg));
    const lvgl_port_display_cfg_t dcfg = {
        .io_handle=display_get_io_handle(), .panel_handle=display_get_panel_handle(),
        .buffer_size=240*320/4, .double_buffer=false, .hres=240, .vres=320,
        .flags={.buff_spiram=false, .buff_dma=true, .swap_bytes=true},
    };
    lvgl_port_add_disp(&dcfg);
    encoder_indev = lv_indev_create();
    lv_indev_set_type(encoder_indev, LV_INDEV_TYPE_ENCODER);
    group_menu = lv_group_create();
    wifi_station_config_t wc = {.ssid=WIFI_SSID, .password=WIFI_PASS, .max_retry=5};
    wifi_station_init(&wc);
    clock_weather_init();
    ESP_LOGI(TAG, "Audio...");
    audio_init();
    xTaskCreate(chat_task, "chat_task", 16384, NULL, 4, NULL);
    xTaskCreatePinnedToCore(display_task, "display_task", 16384, NULL, 5, NULL, 1);
    ESP_LOGI(TAG, "========== Ready ==========");
    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}
