#ifndef EEZ_LVGL_UI_IMAGES_H
#define EEZ_LVGL_UI_IMAGES_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_img_dsc_t img_wifi;
extern const lv_img_dsc_t img_agent;
extern const lv_img_dsc_t img_settings;
extern const lv_img_dsc_t img_smart_home;
extern const lv_img_dsc_t img_games;
extern const lv_img_dsc_t img_chatbot;
extern const lv_img_dsc_t img_info;
extern const lv_img_dsc_t img_50;
extern const lv_img_dsc_t img_80;
extern const lv_img_dsc_t img_charging;
extern const lv_img_dsc_t img_full_battery;
extern const lv_img_dsc_t img_high_battery;
extern const lv_img_dsc_t img_plus;
extern const lv_img_dsc_t img_cloud;
extern const lv_img_dsc_t img_cloudy__1_;
extern const lv_img_dsc_t img_cloudy__2_;
extern const lv_img_dsc_t img_cloudy;
extern const lv_img_dsc_t img_foog;
extern const lv_img_dsc_t img_hail;
extern const lv_img_dsc_t img_night__1_;
extern const lv_img_dsc_t img_night;
extern const lv_img_dsc_t img_rainy__1_;
extern const lv_img_dsc_t img_rainy;
extern const lv_img_dsc_t img_snowy;
extern const lv_img_dsc_t img_storm;
extern const lv_img_dsc_t img_sun;
extern const lv_img_dsc_t img_umbrella;
extern const lv_img_dsc_t img_therm;
extern const lv_img_dsc_t img_hum;
extern const lv_img_dsc_t img_ai_asistant;
extern const lv_img_dsc_t img_microphone;
extern const lv_img_dsc_t img_record_button;
extern const lv_img_dsc_t img_infopic;

#ifndef EXT_IMG_DESC_T
#define EXT_IMG_DESC_T
typedef struct _ext_img_desc_t {
    const char *name;
    const lv_img_dsc_t *img_dsc;
} ext_img_desc_t;
#endif

extern const ext_img_desc_t images[33];

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_IMAGES_H*/