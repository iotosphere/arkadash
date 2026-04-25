#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void screens_init(void);
void screen_main_create(lv_obj_t *parent);
void screen_wifi_scan_create(lv_obj_t *parent);
void screen_wifi_connect_create(lv_obj_t *parent);
void screen_settings_create(lv_obj_t *parent);

extern lv_obj_t *scr_main;
extern lv_obj_t *scr_wifi_scan;
extern lv_obj_t *scr_wifi_connect;
extern lv_obj_t *scr_settings;

/* Main screen widgets */
extern lv_obj_t *lbl_wifi_status;
extern lv_obj_t *lbl_ip_address;
extern lv_obj_t *lbl_ssid;
extern lv_obj_t *btn_scan;

/* WiFi scan screen widgets */
extern lv_obj_t *list_networks;
extern lv_obj_t *btn_back;

/* WiFi connect screen widgets */
extern lv_obj_t *ta_ssid_input;
extern lv_obj_t *ta_pass_input;
extern lv_obj_t *btn_connect;
extern lv_obj_t *btn_cancel;

#ifdef __cplusplus
}
#endif
