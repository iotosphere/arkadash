#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_SPLASH = 1,
    SCREEN_ID_MAIN = 2,
    SCREEN_ID_MENU = 3,
    SCREEN_ID_CHAT = 4,
    SCREEN_ID_AGENT = 5,
    SCREEN_ID_SMART_HOME = 6,
    SCREEN_ID_GAMES = 7,
    SCREEN_ID_SETTINGS = 8,
    SCREEN_ID_ABOUT = 9,
    _SCREEN_ID_LAST = 9
};

typedef struct _objects_t {
    lv_obj_t *splash;
    lv_obj_t *main;
    lv_obj_t *menu;
    lv_obj_t *chat;
    lv_obj_t *agent;
    lv_obj_t *smart_home;
    lv_obj_t *games;
    lv_obj_t *settings;
    lv_obj_t *about;
    lv_obj_t *loading;
    lv_obj_t *header_container;
    lv_obj_t *wifiicon;
    lv_obj_t *obj0;
    lv_obj_t *battery_1;
    lv_obj_t *time_container;
    lv_obj_t *day_text;
    lv_obj_t *year_text;
    lv_obj_t *month_text;
    lv_obj_t *date_text;
    lv_obj_t *minute_text;
    lv_obj_t *centre_dot;
    lv_obj_t *hour_text;
    lv_obj_t *centre_dot_1;
    lv_obj_t *weather_container;
    lv_obj_t *thermometer;
    lv_obj_t *weather_icon;
    lv_obj_t *weather_temp_label;
    lv_obj_t *_c;
    lv_obj_t *hum_icon;
    lv_obj_t *weather_hum_label;
    lv_obj_t *_;
    lv_obj_t *weather_state;
    lv_obj_t *bottom_container;
    lv_obj_t *author;
    lv_obj_t *obj1;
    lv_obj_t *chat_button;
    lv_obj_t *button_agent_button_;
    lv_obj_t *button_smart_home_button_;
    lv_obj_t *button_games_button_;
    lv_obj_t *button_settings_button_;
    lv_obj_t *button_about_button_;
    lv_obj_t *obj2;
    lv_obj_t *battery;
    lv_obj_t *wifiicon_1;
    lv_obj_t *obj3;
    lv_obj_t *bottom_container_1;
    lv_obj_t *author_1;
    lv_obj_t *chatfooter;
    lv_obj_t *obj4;
    lv_obj_t *bottom_container_2;
    lv_obj_t *author_2;
    lv_obj_t *aivoice;
    lv_obj_t *microphone;
    lv_obj_t *mic;
    lv_obj_t *kitt_bar_left;
    lv_obj_t *kitt_bar_center;
    lv_obj_t *kitt_bar_right;
    lv_obj_t *obj5;
    lv_obj_t *obj6;
    lv_obj_t *obj7;
    lv_obj_t *obj8;
    lv_obj_t *obj9;
    lv_obj_t *bottom_container_3;
    lv_obj_t *author_3;
    lv_obj_t *obj10;
    lv_obj_t *obj11;
    lv_obj_t *bottom_container_4;
    lv_obj_t *author_4;
    lv_obj_t *obj12;
    lv_obj_t *obj13;
    lv_obj_t *bottom_container_5;
    lv_obj_t *author_5;
    lv_obj_t *obj14;
    lv_obj_t *obj15;
    lv_obj_t *bottom_container_6;
    lv_obj_t *author_6;
    lv_obj_t *obj16;
    lv_obj_t *obj17;
    lv_obj_t *info_text;
    lv_obj_t *title;
    lv_obj_t *mail;
    lv_obj_t *bottom_container_7;
    lv_obj_t *author_7;
} objects_t;

extern objects_t objects;

void create_screen_splash();
void tick_screen_splash();

void create_screen_main();
void tick_screen_main();

void create_screen_menu();
void tick_screen_menu();

void create_screen_chat();
void tick_screen_chat();

void create_screen_agent();
void tick_screen_agent();

void create_screen_smart_home();
void tick_screen_smart_home();

void create_screen_games();
void tick_screen_games();

void create_screen_settings();
void tick_screen_settings();

void create_screen_about();
void tick_screen_about();

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/