/**
 * HAL 9000 AI animasyonu — animated GIF ile (v3, gerçek sinematik görünüm).
 *
 * Mimari (2026-06-28 refactor):
 *   - create()/destroy()/start()/stop(): lvgl_port_lock korumalı (chat_task'ten çağrılır)
 *   - Animation: GIF widget'ın kendi internal timer'ı (lv_timer_create, 10ms tick)
 *     GIF duration değerlerini okur, her frame'i ayrı ayrı gösterir
 *   - start() = widget görünür yap (animasyon zaten otomatik)
 *   - stop()  = widget gizle (timer arka planda çalışmaya devam eder ama
 *               render edilmediği için watchdog'a etki yok)
 *
 * v3 (2026-06-28) GIF migration:
 *   - Eski statik PNG + lv_anim ile rotation → çalışmıyor (lv_image_set_rotation
 *     + PSRAM layer buffer + screen visibility check çakışması)
 *   - Yeni: 76-frame animated GIF (150x150, 30ms/frame = 2.3s döngü)
 *     LV_USE_GIF decoder otomatik oynatır, biz sadece show/hize yaparız
 *   - GIF datası flash'ta 142 KB (compressed), decoder runtime'da
 *     128x128 ARGB8888 draw_buf allocate eder (PSRAM, 65 KB)
 */

#include "ai_hal9000.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

#if LV_USE_GIF
#include "widgets/gif/lv_gif.h"   /* GIF widget — src olarak raw GIF byte array alır */
#endif

static const char *TAG = "HAL9000";

/* HAL9000 GIF datası — Python ile 150x150 GIF'ten 128x128'e resize edilmiş
 * (76 frame, 30-40-60ms durations, optimize+disposal=2).
 * Toplam ~139 KB flash, decoder runtime'da ARGB8888 draw_buf (65 KB PSRAM).
 * ui_image_hal9000_gif.c içinde extern const lv_image_dsc_t hal9000_gif. */
extern const lv_image_dsc_t hal9000_gif;

/* Canvas boyutu — aivoice container 240x110 içinde sığıyor.
 * HAL_SIZE = 110 → aivoice yüksekliğini full kullanır, GIF widget 108x108 STRETCH (1px margin). */
#define HAL_SIZE         110

/* Widget handle'ları */
static lv_obj_t *hal_container = NULL;
static lv_obj_t *hal_gif       = NULL;   /* HAL 9000 animated GIF widget */

static bool is_active = false;

/* ==== Public API ==== */

void ai_hal9000_create(lv_obj_t *parent) {
    if (!parent) {
        ESP_LOGE(TAG, "Parent obj yok!");
        return;
    }
    if (hal_container) {
        ESP_LOGW(TAG, "HAL 9000 zaten oluşturulmuş");
        return;
    }

    if (!lvgl_port_lock(500)) {
        ESP_LOGE(TAG, "create: lvgl_port_lock timeout");
        return;
    }

    /* === Container — 110x110, başta gizli === */
    hal_container = lv_obj_create(parent);
    lv_obj_set_size(hal_container, HAL_SIZE, HAL_SIZE);
    lv_obj_remove_style_all(hal_container);
    lv_obj_set_style_bg_opa(hal_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hal_container, 0, 0);
    lv_obj_clear_flag(hal_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(hal_container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(hal_container, LV_OBJ_FLAG_HIDDEN);

    /* === HAL 9000 GIF widget ===
     * lv_gif_set_src: raw GIF byte array'i verir, decoder otomatik çözer ve
     * internal timer (10ms) ile frame'leri sırayla gösterir.
     * 76 frame × ~30ms = 2.3s tam döngü, INFINITE loop.
     *
     * lv_obj_set_size: widget 108x108 (1px margin hal_container içinde).
     * STRETCH alignment gerekmez — GIF widget image davranışı kalıtım alır,
     * default CENTER ile native boyutta (128x128) çizer, biz 108x108 widget
     * ile sığdırırız. */
    hal_gif = lv_gif_create(hal_container);
    lv_obj_set_size(hal_gif, 108, 108);
    lv_gif_set_src(hal_gif, &hal9000_gif);
    lv_obj_center(hal_gif);

    lvgl_port_unlock();
    ESP_LOGI(TAG, "HAL 9000 oluşturuldu (GIF 76 frame, 2.3s döngü, PSRAM draw_buf)");
}

void ai_hal9000_start(void) {
    if (!hal_container) {
        ESP_LOGW(TAG, "start: HAL 9000 oluşturulmamış, önce create() çağır");
        return;
    }
    if (is_active) {
        return;  /* zaten çalışıyor */
    }

    if (!lvgl_port_lock(500)) {
        ESP_LOGW(TAG, "start: lvgl_port_lock timeout");
        return;
    }

    is_active = true;
    lv_obj_clear_flag(hal_container, LV_OBJ_FLAG_HIDDEN);
    /* GIF timer otomatik çalışıyor (constructor'da resume edildi), restart etmeye gerek yok */

    lvgl_port_unlock();
    ESP_LOGI(TAG, "HAL 9000 görünür + GIF otomatik animasyon (AI konuşuyor)");
}

void ai_hal9000_stop(void) {
    if (!is_active) {
        return;
    }
    is_active = false;

    if (!lvgl_port_lock(500)) {
        ESP_LOGW(TAG, "stop: lvgl_port_lock timeout");
        return;
    }

    if (hal_container) {
        lv_obj_add_flag(hal_container, LV_OBJ_FLAG_HIDDEN);
    }
    /* GIF timer arka planda çalışmaya devam eder ama hidden widget render edilmez
     * → display_task iş yükü yok, watchdog riski yok.
     * İsterseniz ileride lv_timer_pause(hal_gif->timer) eklenebilir. */

    lvgl_port_unlock();
    ESP_LOGI(TAG, "HAL 9000 gizli (AI sustu, GIF timer arka planda)");
}

bool ai_hal9000_is_active(void) {
    return is_active;
}

void ai_hal9000_destroy(void) {
    ai_hal9000_stop();

    if (!hal_container) {
        return;
    }

    if (!lvgl_port_lock(500)) {
        ESP_LOGE(TAG, "destroy: lvgl_port_lock timeout");
        return;
    }

    lv_obj_del(hal_container);   /* GIF widget + timer otomatik temizlenir (lv_gif_destructor) */
    hal_container = NULL;
    hal_gif       = NULL;

    lvgl_port_unlock();
    ESP_LOGI(TAG, "HAL 9000 silindi");
}
