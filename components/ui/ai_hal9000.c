/**
 * HAL 9000 AI animasyonu — LVGL vektör primitifleriyle (v2, daha gerçekçi görünüm).
 *
 * Mimari:
 *   - create()/destroy(): lvgl_port_lock korumalı
 *   - start()/stop(): lvgl_port_lock korumalı (chat_task'ten çağrılır)
 *   - Anim callbacks: LVGL task context'te koşar, lock GEREKMEZ
 *   - Üç anim paralel: eye opacity pulse + scan line y position + reflect pulse
 *   - Animasyonlar create anında init edilir ve PAUSED kalır; start()/stop()
 *     sadece lv_anim_resume/pause toggle eder (alloc/dealloc yok)
 *
 * v2 (2026-06-28) HAL 9000 estetik refactor:
 *   - Yuvarlak daireler yerine rounded rect lens frame
 *   - Küçük parlak kırmızı iris + glow shadow (lv_style_shadow, layer buffer YOK)
 *   - Scan line animasyonu (lv_obj_set_y, transform değil → güvenli)
 */

#include "ai_hal9000.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

static const char *TAG = "HAL9000";

/* HAL 9000'in klasik kırmızı (#EE1B2B — film/Stanley Kubrick) */
#define HAL_RED        lv_color_hex(0xEE1B2B)

/* Canvas ve lens boyutları */
#define HAL_SIZE         100
#define HAL_BODY_SIZE_W  90   /* lens frame width — HAL'ın yassı yüz plakası */
#define HAL_BODY_SIZE_H  70   /* lens frame height — width'ten kısa (HAL look) */

/* Widget handle'ları (hepsi hal_container içinde) */
static lv_obj_t *hal_container = NULL;
static lv_obj_t *hal_body      = NULL;   /* koyu rounded rect lens frame */
static lv_obj_t *hal_eye       = NULL;   /* parlak kırmızı iris (glow'lu) */
static lv_obj_t *hal_scan      = NULL;   /* yatay scan line (y animasyonlu) */
static lv_obj_t *hal_reflect1  = NULL;   /* iris üzerinde küçük beyaz yansıma */

/* lv_anim handle'ları — start() pause/resume, alloc yok */
static lv_anim_t anim_eye;
static lv_anim_t anim_reflect;
static lv_anim_t anim_scan;

static bool is_active = false;

/* ==== Animation callbacks (LVGL task context — lock gereksiz) ==== */

static bool is_on_active_screen(void) {
    /* GÜVENLİK: animasyon tick ederken container'ımız aktif ekranda değilse
     * (user başka ekrana geçtiyse) skip et. Aksi halde lv_obj_invalidate
     * LVGL task'a sürekli dirty mark gönderiyor, draw_buf alloc tetikleniyor. */
    if (!hal_container) return false;
    return (lv_obj_get_screen(hal_container) == lv_scr_act());
}

static void opa_cb(void *obj, int32_t v) {
    if (!is_on_active_screen()) return;
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static void scan_y_cb(void *obj, int32_t y) {
    /* lv_obj_set_y sadece coord günceller, layer buffer / transform tetiklemez.
     * Bu yüzden layer-allocator watchdog riski yok (rotation'dan farklı). */
    if (!is_on_active_screen()) return;
    lv_obj_set_y((lv_obj_t *)obj, (lv_coord_t)y);
}

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

    /* === Container — 100x100, başta gizli === */
    hal_container = lv_obj_create(parent);
    lv_obj_set_size(hal_container, HAL_SIZE, HAL_SIZE);
    lv_obj_remove_style_all(hal_container);
    lv_obj_set_style_bg_opa(hal_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hal_container, 0, 0);
    lv_obj_clear_flag(hal_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(hal_container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(hal_container, LV_OBJ_FLAG_HIDDEN);

    /* === Body — rounded rect lens frame (HAL 9000'ün "yüz plakası") ===
     * Klasik HAL: koyu renkli rounded rectangle, içinde küçük parlak kırmızı iris. */
    hal_body = lv_obj_create(hal_container);
    lv_obj_set_size(hal_body, HAL_BODY_SIZE_W, HAL_BODY_SIZE_H);
    lv_obj_remove_style_all(hal_body);
    lv_obj_set_style_bg_color(hal_body, lv_color_hex(0x0c0c0c), 0);
    lv_obj_set_style_bg_opa(hal_body, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hal_body, 8, 0);  /* hafif rounded, kare görünüm */
    lv_obj_set_style_border_width(hal_body, 3, 0);
    lv_obj_set_style_border_color(hal_body, lv_color_hex(0x331111), 0);
    lv_obj_center(hal_body);

    /* === Eye — küçük parlak kırmızı iris (HAL'ın ikonik göz bebeği) ===
     * Glow efekti için shadow style — LVGL katman/layer buffer'a dokunmaz
     * çünkü shadow sadece paint sırasında uygulanır (transform değil). */
    hal_eye = lv_obj_create(hal_body);
    lv_obj_set_size(hal_eye, 16, 16);
    lv_obj_remove_style_all(hal_eye);
    lv_obj_set_style_bg_color(hal_eye, HAL_RED, 0);
    lv_obj_set_style_bg_opa(hal_eye, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hal_eye, LV_RADIUS_CIRCLE, 0);
    /* Glow: çevresine yumuşak kırmızı ışık yayar (filter gibi) */
    lv_obj_set_style_shadow_width(hal_eye, 18, 0);
    lv_obj_set_style_shadow_color(hal_eye, HAL_RED, 0);
    lv_obj_set_style_shadow_opa(hal_eye, LV_OPA_70, 0);
    lv_obj_set_style_shadow_spread(hal_eye, 2, 0);
    lv_obj_center(hal_eye);

    /* === Scan line — ince yatay kırmızı çubuk, animasyonlu y position ===
     * Klasik HAL efekti: lens içinde yukarı-aşağı kayan tarama çizgisi.
     * lv_obj_set_y sadece coord günceller, layer buffer / transform tetiklemez. */
    hal_scan = lv_obj_create(hal_body);
    lv_obj_set_size(hal_scan, HAL_BODY_SIZE_W - 12, 2);
    lv_obj_remove_style_all(hal_scan);
    lv_obj_set_style_bg_color(hal_scan, lv_color_hex(0xff0033), 0);
    lv_obj_set_style_bg_opa(hal_scan, LV_OPA_60, 0);
    lv_obj_set_style_radius(hal_scan, 1, 0);
    /* Başlangıç pozisyonu — animasyon buradan başlayacak (en üstte) */
    lv_obj_align(hal_scan, LV_ALIGN_TOP_MID, 0, 6);

    /* === Highlight — iris üzerinde küçük beyaz yansıma (canlı göz efekti) === */
    hal_reflect1 = lv_obj_create(hal_eye);
    lv_obj_set_size(hal_reflect1, 5, 3);
    lv_obj_remove_style_all(hal_reflect1);
    lv_obj_set_style_bg_color(hal_reflect1, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(hal_reflect1, LV_OPA_80, 0);
    lv_obj_set_style_radius(hal_reflect1, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(hal_reflect1, LV_ALIGN_TOP_LEFT, 2, 2);

    /* === Animasyonları create anında BIR KERE başlat ===
     * start()/stop() sadece pause/resume toggle eder — alloc/dealloc yok. */
    /* Eye opacity pulse: 1.2s nefes alma */
    lv_anim_init(&anim_eye);
    lv_anim_set_var(&anim_eye, hal_eye);
    lv_anim_set_exec_cb(&anim_eye, opa_cb);
    lv_anim_set_values(&anim_eye, LV_OPA_30, LV_OPA_100);
    lv_anim_set_time(&anim_eye, 1200);
    lv_anim_set_playback_time(&anim_eye, 1200);
    lv_anim_set_repeat_count(&anim_eye, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&anim_eye);
    lv_anim_pause(&anim_eye);

    /* Reflect pulse: 0.8s offset (eye ile faz farkı — canlı göz hissi) */
    lv_anim_init(&anim_reflect);
    lv_anim_set_var(&anim_reflect, hal_reflect1);
    lv_anim_set_exec_cb(&anim_reflect, opa_cb);
    lv_anim_set_values(&anim_reflect, LV_OPA_40, LV_OPA_90);
    lv_anim_set_time(&anim_reflect, 800);
    lv_anim_set_playback_time(&anim_reflect, 800);
    lv_anim_set_repeat_count(&anim_reflect, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&anim_reflect);
    lv_anim_pause(&anim_reflect);

    /* Scan line: y position 6 ↔ 56 (lens içi), 2.5s ping-pong */
    lv_anim_init(&anim_scan);
    lv_anim_set_var(&anim_scan, hal_scan);
    lv_anim_set_exec_cb(&anim_scan, scan_y_cb);
    lv_anim_set_values(&anim_scan, 6, HAL_BODY_SIZE_H - 16);
    lv_anim_set_time(&anim_scan, 2500);
    lv_anim_set_playback_time(&anim_scan, 2500);
    lv_anim_set_repeat_count(&anim_scan, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&anim_scan);
    lv_anim_pause(&anim_scan);

    lvgl_port_unlock();
    ESP_LOGI(TAG, "HAL 9000 oluşturuldu (3 animasyon pause'lı: eye + reflect + scan)");
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
    lv_anim_resume(&anim_eye);
    lv_anim_resume(&anim_reflect);
    lv_anim_resume(&anim_scan);

    lvgl_port_unlock();
    ESP_LOGI(TAG, "HAL 9000 başladı (3 anim resume — alloc yok)");
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

    lv_anim_pause(&anim_eye);
    lv_anim_pause(&anim_reflect);
    lv_anim_pause(&anim_scan);

    if (hal_container) {
        lv_obj_add_flag(hal_container, LV_OBJ_FLAG_HIDDEN);
    }

    lvgl_port_unlock();
    ESP_LOGI(TAG, "HAL 9000 durdu (3 anim pause)");
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

    lv_obj_del(hal_container);
    hal_container = NULL;
    hal_body      = NULL;
    hal_eye       = NULL;
    hal_scan      = NULL;
    hal_reflect1  = NULL;

    lvgl_port_unlock();
    ESP_LOGI(TAG, "HAL 9000 silindi");
}
