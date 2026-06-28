#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * Initialize the battery monitor subsystem.
 * - Configures ADC1 oneshot mode on GPIO48 (battery voltage divider tap).
 * - Hardware: 100k+100k divider → ADC sees Vbat / 2.
 * - Li-Ion 18650: 3.0V (empty) → 1.5V ADC, 4.2V (full) → 2.1V ADC.
 *
 * Call once from app_main after nvs_flash_init().
 */
void battery_monitor_init(void);

/**
 * Read the current battery state.
 *
 * @param percent_out   Battery percentage 0..100 (clamped).
 * @param charging_out  true if the voltage is rising (TP4056 CC/CV charge
 *                      cycle is feeding the cell).
 *
 * Safe to call from any context; uses ADC oneshot internally.
 */
void battery_monitor_read(uint8_t *percent_out, bool *charging_out);
