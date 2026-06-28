#pragma once

#include "lvgl.h"
#include <stdbool.h>

/**
 * HAL 9000 AI animasyonunu oluşturur (gizli, AI susarken)
 * @param parent Animasyonun ekleneceği parent obje (örn: aivoice container)
 * @note lvgl_port_lock ile korunur, kilit timeout'ta abort eder.
 */
void ai_hal9000_create(lv_obj_t *parent);

/**
 * Animasyonu başlatır (AI konuşmaya başladığında).
 * Eye pulse + ring rotation + reflect pulse başlar.
 */
void ai_hal9000_start(void);

/**
 * Animasyonu durdurur (AI sustuğunda).
 * Tüm lv_anim'leri siler, container'ı gizler.
 */
void ai_hal9000_stop(void);

/**
 * Animasyon aktif mi?
 */
bool ai_hal9000_is_active(void);

/**
 * Tüm objeleri siler (temizlik için).
 */
void ai_hal9000_destroy(void);
