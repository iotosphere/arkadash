#include "app_config.h"
#include "audio.h"
#include "bricks_breaker.h"
#include "clock_weather.h"
#include "display.h"
#include "driver/i2s_std.h"
#include "encoder.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "images.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "screens.h"
#include "ui.h"
#include "websocket_client.h"
#include "wifi_station.h"
#include <stdio.h>

static const char *TAG = "app_main";

#define WIFI_SSID "SUPERONLINE_WiFi_5292"
#define WIFI_PASS "EJPus6hfjy7z"
#define LONG_PRESS_MS 800u
#define SLEEP_TIMEOUT_MS 90000u /* 1.5 dk */
#define REC_BUF_SIZE (16000 * 2 * 5)
#define VOICE_SERVER_URI "ws://192.168.1.8:8765"

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
static lv_group_t *group_smart_home = NULL;
static lv_indev_t *encoder_indev = NULL;

/* stub - kitt anim was used for AI voice animation */
static void kitt_anim(void) { (void)kitt_anim_tick; }

/* -----------------------------------------------------------------------
 * activity_reset — hem chat_task hem display_task tarafından kullanılır.
 * AI konuştuktan sonra veya kayıt bitince çağrılır; ekran uykuya geçmez.
 * ----------------------------------------------------------------------- */
static void activity_reset(void) {
  last_activity_tick = lv_tick_get();
  if (display_sleeping) {
    display_backlight_on();
    display_sleeping = false;
  }
}

/* ----------------------------------------------------------------------- */

static void smart_home_living_room_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    extern void ws_send_led_toggle(void);
    ws_send_led_toggle();
    ESP_LOGI(TAG, "Living Room LED toggle");
  }
}

static void smart_home_kitchen_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    ESP_LOGI(TAG, "Kitchen pressed");
  }
}

static void smart_home_temperature_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    ESP_LOGI(TAG, "Temperature pressed");
  }
}

/* o-provision screen: button on settings page triggers this. */
static void settings_provision_cb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    ESP_LOGI(TAG, "Provision screen open");
    if (objects.provision) {
      lv_scr_load(objects.provision);
    }
  }
}

/* Settings: live-update audio volume from the slider. */
static void settings_volume_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  if (!objects.volume) return;
  int pct = lv_slider_get_value(objects.volume);
  extern void ui_set_volume(int pct);
  ui_set_volume(pct);
}

/* Settings: live-update display backlight from the slider. */
static void settings_brightness_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  if (!objects.brightness) return;
  int pct = lv_slider_get_value(objects.brightness);
  extern void ui_set_brightness(int pct);
  ui_set_brightness(pct);
}

static const char *smart_home_get_focused_name(void) {
  lv_obj_t *focused = lv_group_get_focused(group_smart_home);
  if (focused == objects.living_room_btn)
    return "Living Room";
  if (focused == objects.kitchen_btn)
    return "Kitchen";
  if (focused == objects.temperature_btn)
    return "Temperature";
  return "Smart Home";
}

/* ----------------------------------------------------------------------- */

static void chat_task(void *pv) {
  (void)pv;

  bool last_k0 = false;
  uint32_t k0_start = 0;

  void *rx = audio_get_rx_handle();
  void *tx = audio_get_tx_handle();

  s_rec_buf = heap_caps_malloc(REC_BUF_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_SPIRAM);
  if (!s_rec_buf) {
    ESP_LOGE(TAG, "No mem");
    vTaskDelete(NULL);
    return;
  }

  ESP_LOGI(TAG, "Chat task started RX=%p TX=%p", rx, tx);

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

    /* PTT press debug — sadece edge transition'larda */
    if (k0 && !last_k0) {
      ESP_LOGI(
          TAG, "PT T PRESS detected! cur=%p assistant=%p is_ai=%d is_rec=%d",
          (void *)cur, (void *)objects.assistant, is_ai_speaking, is_recording);
    }

    static uint32_t last_debug = 0;
    if (lv_tick_get() - last_debug > 5000) {
      ESP_LOGI(TAG,
               "DEBUG: cur=%p assistant=%p sleeping=%d is_rec=%d is_ai=%d "
               "k0=%d last_k0=%d",
               (void *)cur, (void *)objects.assistant, display_sleeping,
               is_recording, is_ai_speaking, k0, last_k0);
      last_debug = lv_tick_get();
    }

    if ((cur == objects.assistant) && !display_sleeping) {

      /* debounce */
      if (k0 && !last_k0) {
        vTaskDelay(pdMS_TO_TICKS(30));
        k0 = key0_pressed();
      }

      if (k0 && !last_k0 && rx && tx) {

        ws_clear_response();
        is_recording = true;
        k0_start = lv_tick_get();
        s_rec_bytes = 0;

        if (lvgl_port_lock(0)) {
          lv_image_set_src(objects.mic, &img_record_button);
          lvgl_port_unlock();
        }
        ESP_LOGI(TAG, "REC START");

        /* ---- RX channel enable et ---- */
        /* Ensure RX is disabled first to avoid "already enabled" error */
        if (rx) {
          i2s_channel_disable((i2s_chan_handle_t)rx);
        }
        i2s_channel_enable((i2s_chan_handle_t)rx);

        /* ---- kayıt döngüsü ---- */
        while (key0_pressed() && is_recording && s_rec_bytes < REC_BUF_SIZE) {
          size_t ck = (s_rec_bytes + 2048 > REC_BUF_SIZE)
                          ? (REC_BUF_SIZE - s_rec_bytes)
                          : 2048;
          size_t rd = 0;
          esp_err_t ret =
              i2s_channel_read((i2s_chan_handle_t)rx, s_rec_buf + s_rec_bytes,
                               ck, &rd, pdMS_TO_TICKS(1000));
          if (ret == ESP_OK && rd > 0) {
            s_rec_bytes += rd;
            ESP_LOGI(TAG, "REC %d bytes, total=%d", (int)rd, (int)s_rec_bytes);
          }
          // KITT sadece AI konuşurken gösterilir - kayıt sırasında çağrılmaz
          vTaskDelay(pdMS_TO_TICKS(10));
        }

        uint32_t held = lv_tick_get() - k0_start;
        is_recording = false;

        if (lvgl_port_lock(0)) {
          lv_image_set_src(objects.mic, &img_microphone);
          lvgl_port_unlock();
        }
        ESP_LOGI(TAG, "REC STOP %lu ms %d bytes", (unsigned long)held,
                 (int)s_rec_bytes);

        /* Kaydı sunucuya gönder ve yanıtı çal */
        if (held > 100 && s_rec_bytes > 4000) {

          ESP_LOGI(TAG, "Sending %d bytes to voice server...",
                   (int)s_rec_bytes);
          ws_clear_response();
          ws_set_waiting(true);

          if (ws_send_audio(s_rec_buf, s_rec_bytes) == ESP_OK) {

            ESP_LOGI(TAG, "Waiting for TTS response...");
            is_ai_speaking = true;

            /* MONO PCM direkt çal - gerçek zamanlı hızda */
            uint8_t play_buf[4096];
            size_t total_played = 0;
            int empty_count = 0;
            int timeout_count = 0;
            const int MAX_TIMEOUT = 24000; /* 120s artırıldı - uzun TTS için */

            while (timeout_count < MAX_TIMEOUT) {
              /* Error state kontrolü — ring buffer invalid */
              if (ws_has_error()) {
                ESP_LOGW(TAG, "Server error during playback, aborting");
                break;
              }

              /* Gerçek zamanlı gecikme: 4096 bytes @ 16kHz stereo ≈ 64ms */
              vTaskDelay(pdMS_TO_TICKS(70));

              size_t chunk = ws_stream_read(play_buf, sizeof(play_buf), 10);
              if (chunk > 0) {
                size_t written = 0;
                esp_err_t wret =
                    i2s_channel_write((i2s_chan_handle_t)tx, play_buf, chunk,
                                      &written, pdMS_TO_TICKS(5000));
                if (wret == ESP_OK && written > 0) {
                  total_played += written;
                }
                empty_count = 0;
              } else {
                empty_count++;
              }

              /* TTS bitti işareti geldiyse, ring buffer boşalana kadar bekle */
              if (ws_is_tts_complete()) {
                ESP_LOGI(
                    TAG,
                    "TTS complete signal received, empty=%d total_played=%d",
                    empty_count, (int)total_played);
                /* MIN_PLAYED bytes oynamadan çıkma — TTS bitmiş ama buffer dolu
                 * olabilir */
                const size_t MIN_PLAYED = 8192;
                if (total_played >= MIN_PLAYED && empty_count > 5) {
                  ESP_LOGI(TAG, "Streaming complete, played %d bytes (drained)",
                           (int)total_played);
                  break;
                }
                /* buffer hâlâ dolu -> bekle */
              }
              if (empty_count > 300) {
                ESP_LOGI(TAG, "Streaming complete (timeout), played %d bytes",
                         (int)total_played);
                break;
              } else {
                /* TTS hâlâ devam ediyor - ses kesilmemeli */
                if (empty_count > 100) {
                  ESP_LOGW(TAG, "Buffer underflow, continuing...");
                  empty_count = 0;
                }
              }

              timeout_count++;
              kitt_anim();
            }

            if (timeout_count >= MAX_TIMEOUT) {
              ESP_LOGW(TAG, "Streaming timeout, played %d bytes",
                       (int)total_played);
            }

            is_ai_speaking = false;
            last_k0 =
                0; // Reset — buton hâlâ basılıysa next iteration algılanmalı
            ESP_LOGI(TAG, "Pipeline done, last_k0=%d reset", last_k0);
            float actual_dur = (float)total_played / (16000 * 2 * 2);
            ESP_LOGI(TAG, "PLAY DONE: %d bytes (%.2fs)", (int)total_played,
                     actual_dur);

            /* ---- FIX: AI konuştuktan sonra idle timer'ı sıfırla ---- */
            activity_reset();
            /* -------------------------------------------------------- */

            ws_set_waiting(false);
            ws_set_tts_complete(false);
            ws_stream_clear();
            /* Prevent menu reload after TTS */
            pending_menu_load = false;

            /* ---- RX/TX kanal resetle - sonraki kayıt için ---- */
            ESP_LOGI(TAG, "Resetting audio channels for next recording");
            if (rx) {
              i2s_channel_disable((i2s_chan_handle_t)rx);
              ESP_LOGI(TAG, "RX channel disabled");
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            if (rx) {
              i2s_channel_enable((i2s_chan_handle_t)rx);
              ESP_LOGI(TAG, "RX channel re-enabled");
            }
            /* -------------------------------------------------------- */

          } else {
            ESP_LOGE(TAG, "Failed to send audio to voice server");
          }
        }

        s_rec_bytes = 0;
      }

    } else {
      if (is_recording || is_ai_speaking) {
        is_recording = false;
        is_ai_speaking = false;
        s_rec_bytes = 0;
        if (lvgl_port_lock(0)) {
          lv_image_set_src(objects.mic, &img_microphone);
          lvgl_port_unlock();
        }
        /* ---- RX/TX kanal resetle - ekrandan ayrıldığında ---- */
        ESP_LOGI(TAG, "Resetting audio channels leaving assistant screen");
        if (rx) {
          i2s_channel_disable((i2s_chan_handle_t)rx);
          ESP_LOGI(TAG, "RX channel disabled");
        }
        if (tx) {
          i2s_channel_disable((i2s_chan_handle_t)tx);
          ESP_LOGI(TAG, "TX channel disabled");
        }
      }
    }

    kitt_anim();
    last_k0 = key0_pressed();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

/* ----------------------------------------------------------------------- */

static void encoder_read(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;

  bool pushed = encoder_button_pressed();
  lv_obj_t *cur = lv_scr_act();

  /* Ekran uyku modundayken encoder veya push ile uyandır */
  if (display_sleeping) {
    int32_t enc_diff = encoder_get_diff();
    if (pushed || enc_diff != 0) {
      display_backlight_on();
      display_sleeping = false;
      wake_up_just_happened = true;
      last_activity_tick = lv_tick_get();
      push_handled = false;
      data->enc_diff = 0;
      data->state = LV_INDEV_STATE_REL;
      /* Uyandıktan sonra menu ekranına git */
      lv_scr_load(objects.menu);
      ui_set_footer("Menu");
      lv_indev_set_group(encoder_indev, group_menu);
      if (objects.asistant_button)
        lv_group_focus_obj(objects.asistant_button);
      encoder_reset_count();
      menu_just_loaded = true;
      return;
    }
    /* Uyku modundayken encoder verisi gönderme */
    data->enc_diff = 0;
    data->state = LV_INDEV_STATE_REL;
    return;
  }

  if (wake_up_just_happened && pushed) {
    data->enc_diff = 0;
    data->state = LV_INDEV_STATE_REL;
    return;
  }
  if (wake_up_just_happened && !pushed) {
    wake_up_just_happened = false;
    push_handled = false;
  }

  if (!display_sleeping) {
    int32_t diff = encoder_get_diff();
    if (diff != 0 || pushed)
      last_activity_tick = lv_tick_get();
    if (cur == objects.games)
      data->enc_diff = 0;
    else
      data->enc_diff = diff;
  } else {
    data->enc_diff = 0;
  }

  /* ---- Menu ekranı ---- */
  if (cur == objects.menu) {
    if (pushed) {
      if (menu_just_loaded) {
        data->state = LV_INDEV_STATE_REL;
        data->enc_diff = 0;
        return;
      }
      data->state = LV_INDEV_STATE_PR;
    } else {
      if (menu_just_loaded) {
        menu_just_loaded = false;
        push_handled = false;
      }
      long_press_triggered = false;
      data->state = LV_INDEV_STATE_REL;
    }
    return;
  }

  /* ---- Smart Home ekranı ---- */
  if (cur == objects.smart_home) {
    if (lv_indev_get_group(encoder_indev) != group_smart_home) {
      lv_indev_set_group(encoder_indev, group_smart_home);
      if (objects.living_room_btn)
        lv_group_focus_obj(objects.living_room_btn);
    }
    if (pushed) {
      if (menu_just_loaded) {
        data->state = LV_INDEV_STATE_REL;
        data->enc_diff = 0;
        return;
      }
      data->state = LV_INDEV_STATE_PR;
    } else {
      if (menu_just_loaded) {
        menu_just_loaded = false;
        push_handled = false;
      }
      long_press_triggered = false;
      data->state = LV_INDEV_STATE_REL;
    }
    return;
  }

  /* ---- Games ekranı ---- */
  if (cur == objects.games) {
    if (game_just_stopped_ms > 0 &&
        (lv_tick_get() - game_just_stopped_ms < 300)) {
      data->enc_diff = 0;
      data->state = LV_INDEV_STATE_REL;
      push_handled = true;
      return;
    }
    game_just_stopped_ms = 0;

    if (pushed) {
      if (!push_handled) {
        push_handled = true;
        push_start_ms = lv_tick_get();
      }
      if (lv_tick_get() - push_start_ms >= LONG_PRESS_MS) {
        if (lvgl_port_lock(0)) {
          pending_menu_load = false;
          lv_scr_load(objects.menu);
          lv_indev_set_group(encoder_indev, group_menu);
          if (objects.asistant_button)
            lv_group_focus_obj(objects.asistant_button);
          encoder_reset_count();
          menu_just_loaded = true;
          push_handled = true;
          long_press_triggered = true;
          lvgl_port_unlock();
        }
        data->state = LV_INDEV_STATE_REL;
        data->enc_diff = 0;
        return;
      }
    } else {
      if (push_handled && !long_press_triggered &&
          lv_tick_get() - push_start_ms < LONG_PRESS_MS &&
          lv_tick_get() - push_start_ms > 0) {
        if (lvgl_port_lock(0)) {
          bricks_breaker_start();
          lvgl_port_unlock();
        }
      }
      push_handled = false;
      long_press_triggered = false;
    }
    data->enc_diff = 0;
    data->state = LV_INDEV_STATE_REL;
    return;
  }

  /* ---- Diğer ekranlar (main, chat, vb.) ---- */
  if (pushed) {
    if (!push_handled) {
      push_start_ms = lv_tick_get();
      push_handled = true;

      if (cur == objects.splash) {
        data->state = LV_INDEV_STATE_REL;
        data->enc_diff = 0;
        return;
      }

      if (cur == objects.main) {
        if (lvgl_port_lock(0)) {
          lv_scr_load(objects.menu);
          lv_indev_set_group(indev, group_menu);
          if (objects.asistant_button)
            lv_group_focus_obj(objects.asistant_button);
          encoder_reset_count();
          lvgl_port_unlock();
        }
        menu_just_loaded = true;
        data->state = LV_INDEV_STATE_REL;
        data->enc_diff = 0;
        return;
      }
    }

    if (long_press_triggered) {
      data->state = LV_INDEV_STATE_REL;
      data->enc_diff = 0;
      return;
    }

    if (lv_tick_get() - push_start_ms >= LONG_PRESS_MS) {
      long_press_triggered = true;
      pending_menu_load = true;
      menu_just_loaded = true;
      data->state = LV_INDEV_STATE_REL;
      data->enc_diff = 0;
      return;
    }

    data->state = LV_INDEV_STATE_PR;
    data->enc_diff = 0;

  } else {
    push_handled = false;
    long_press_triggered = false;
    data->state = LV_INDEV_STATE_REL;
  }
}

/* ----------------------------------------------------------------------- */

static void display_task(void *pv) {
  (void)pv;

  lv_indev_set_read_cb(encoder_indev, encoder_read);
  lv_indev_set_group(encoder_indev, group_menu);
  lv_indev_enable(encoder_indev, true);

  if (lvgl_port_lock(0)) {
    ui_init();
    /* Wire up smart-home button click events. */
    if (objects.living_room_btn)
      lv_obj_add_event_cb(objects.living_room_btn, smart_home_living_room_cb,
                          LV_EVENT_CLICKED, NULL);
    if (objects.kitchen_btn)
      lv_obj_add_event_cb(objects.kitchen_btn, smart_home_kitchen_cb,
                          LV_EVENT_CLICKED, NULL);
    if (objects.temperature_btn)
      lv_obj_add_event_cb(objects.temperature_btn, smart_home_temperature_cb,
                          LV_EVENT_CLICKED, NULL);
    /* Settings page: hook the Provision button. The page also has
     * volume and brightness sliders whose initial value is set by
     * eez_wrapper.cpp (ui_set_volume / ui_set_brightness). */
    if (objects.prov) {
      lv_obj_add_event_cb(objects.prov, settings_provision_cb,
                          LV_EVENT_CLICKED, NULL);
      lv_group_add_obj(group_menu, objects.prov);
    }
    if (objects.volume) {
      lv_obj_add_event_cb(objects.volume, settings_volume_cb,
                          LV_EVENT_VALUE_CHANGED, NULL);
      lv_group_add_obj(group_menu, objects.volume);
    }
    if (objects.brightness) {
      lv_obj_add_event_cb(objects.brightness, settings_brightness_cb,
                          LV_EVENT_VALUE_CHANGED, NULL);
      lv_group_add_obj(group_menu, objects.brightness);
    }
    lv_scr_load(objects.splash);
    lv_indev_set_group(encoder_indev, NULL);
    lvgl_port_unlock();
  }

  if (group_menu) {
    if (objects.asistant_button)
      lv_group_add_obj(group_menu, objects.asistant_button);
    if (objects._music_button_)
      lv_group_add_obj(group_menu, objects._music_button_);
    if (objects._smart_home_button_)
      lv_group_add_obj(group_menu, objects._smart_home_button_);
    if (objects._games_button_)
      lv_group_add_obj(group_menu, objects._games_button_);
    if (objects.bricks_btn)
      lv_group_add_obj(group_menu, objects.bricks_btn);
    if (objects._settings_button_)
      lv_group_add_obj(group_menu, objects._settings_button_);
    if (objects._about_button_)
      lv_group_add_obj(group_menu, objects._about_button_);
  }

  if (group_smart_home) {
    if (objects.living_room_btn)
      lv_group_add_obj(group_smart_home, objects.living_room_btn);
    if (objects.kitchen_btn)
      lv_group_add_obj(group_smart_home, objects.kitchen_btn);
    if (objects.temperature_btn)
      lv_group_add_obj(group_smart_home, objects.temperature_btn);
  }

  last_activity_tick = lv_tick_get();
  ESP_LOGI(TAG, "Display task ready");

  while (1) {
    if (!display_sleeping) {
      if (lvgl_port_lock(0)) {
        ui_tick();
        if (pending_menu_load) {
          pending_menu_load = false;
          lv_scr_load(objects.menu);
          lv_indev_set_group(encoder_indev, group_menu);
          if (objects.asistant_button)
            lv_group_focus_obj(objects.asistant_button);
          encoder_reset_count();
        }
        lvgl_port_unlock();
      }

      if (lv_scr_act() != objects.games) {
        uint32_t idle = lv_tick_get() - last_activity_tick;
        if (idle >= SLEEP_TIMEOUT_MS) {
          display_backlight_off();
          display_sleeping = true;
          ESP_LOGI(TAG, "Display sleep");
        }
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

/* ----------------------------------------------------------------------- */

void app_main(void) {
  ESP_LOGI(TAG, "========== System ==========");
  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(display_init());
  ESP_ERROR_CHECK(encoder_init());

  /* PSRAM warmup: read 1 MB of PSRAM linearly so the SPI PSRAM pipeline
   * pre-fetches the data cache lines. Without this, the first LVGL
   * widget-tree access in PSRAM hits an uncached line and triggers a
   * Load access fault on RISC-V (cleanup_event_list_core crashes). */
  ESP_LOGI(TAG, "PSRAM warmup...");
  uint32_t *probe = (uint32_t *)heap_caps_malloc(1024 * 1024, MALLOC_CAP_SPIRAM);
  if (probe) {
    volatile uint32_t sum = 0;
    for (int i = 0; i < (1024 * 1024) / 4; i += 64) {
      sum += probe[i];
    }
    free(probe);
    (void)sum;
    ESP_LOGI(TAG, "PSRAM warmup done");
  } else {
    ESP_LOGW(TAG, "PSRAM warmup skipped (alloc failed)");
  }

  ESP_LOGI(TAG, "LVGL...");
  const lvgl_port_cfg_t lcfg = ESP_LVGL_PORT_INIT_CONFIG();
  ESP_ERROR_CHECK(lvgl_port_init(&lcfg));

  const lvgl_port_display_cfg_t dcfg = {
      .io_handle = display_get_io_handle(),
      .panel_handle = display_get_panel_handle(),
      .buffer_size = 240 * 32, // 7.5 KB
      .double_buffer = false,
      .hres = 240,
      .vres = 320,
      /* PSRAM draw buffer + warmup avoids the lv_tlsf_free infinite
       * loop and the RISC-V Load access fault on first widget-tree
       * access. See agent memory MEMORY.md 2026-06-13. */
      .flags = {.buff_spiram = true, .buff_dma = false, .swap_bytes = true},
  };
  lv_display_t *disp = lvgl_port_add_disp(&dcfg);
  if (disp) {
    ESP_LOGI(TAG, "LVGL display registered: %p", (void *)disp);
  } else {
    ESP_LOGE(TAG, "LVGL display registration FAILED");
  }

  encoder_indev = lv_indev_create();
  lv_indev_set_type(encoder_indev, LV_INDEV_TYPE_ENCODER);
  group_menu = lv_group_create();
  group_smart_home = lv_group_create();

  wifi_station_config_t wc = {
      .ssid = WIFI_SSID, .password = WIFI_PASS, .max_retry = 5};
  wifi_station_init(&wc);

  clock_weather_init();

  ESP_LOGI(TAG, "Audio...");
  audio_init();

  xTaskCreate(chat_task, "chat_task", 16384, NULL, 4, NULL);
  // Pin display_task to CPU0 so it runs on same core as LVGL task
  xTaskCreatePinnedToCore(display_task, "display_task", 16384, NULL, 5, NULL,
                          0);

  ESP_LOGI(TAG, "========== Ready ==========");
  while (1)
    vTaskDelay(pdMS_TO_TICKS(1000));
}
