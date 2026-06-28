#include <string.h>
#include <stdio.h>
#include <esp_log.h>

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"
#include "bricks_breaker.h"
#include "esp_lvgl_port.h"

objects_t objects;

static const char *screen_names[] = { "Splash", "Main", "Menu", "Assistant", "Music", "SmartHome", "Games", "Settings", "About" };
static const char *object_names[] = { "splash", "main", "menu", "assistant", "music", "smart_home", "games", "settings", "about", "loading", "header_container", "wifiicon", "obj0", "battery_1", "time_container", "day_text", "year_text", "month_text", "date_text", "minute_text", "centre_dot", "hour_text", "centre_dot_1", "weather_container", "thermometer", "weather_icon", "weather_temp_label", "_c", "hum_icon", "weather_hum_label", "_", "weather_state", "bottom_container", "author", "obj1", "asistant_button", "_music_button_", "_smart_home_button_", "_games_button_", "_settings_button_", "_about_button_", "obj2", "battery", "wifiicon_1", "obj3", "bottom_container_1", "author_1", "chatfooter", "obj4", "bottom_container_2", "author_2", "aivoice", "microphone", "mic", "obj5", "obj6", "bottom_container_3", "author_3", "obj7", "obj8", "bottom_container_4", "author_4", "obj9", "obj10", "bottom_container_5", "author_5", "obj11", "obj12", "bottom_container_6", "author_6", "obj13", "obj14", "bottom_container_7", "author_7", "info_text", "title", "mail" };

//
// Event handlers
//

lv_obj_t *tick_value_change_obj;

static void event_handler_cb_splash_splash(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_SCREEN_LOADED) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 1, 0, e);
    }
}

static void event_handler_cb_main_main(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_SCREEN_LOADED) {
        ui_set_footer("Serhat SADAY");
    }
    if (event == LV_EVENT_RELEASED) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 1, 0, e);
    }
}

static void event_handler_cb_menu_menu(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_SCREEN_LOADED) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 1, 0, e);
    }
}

static void event_handler_cb_menu_asistant_button(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;

    if (event == LV_EVENT_FOCUSED) {
        ui_set_footer("Assistant");
    }
    if (event == LV_EVENT_RELEASED) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 0, 0, e);
    }
}

static void event_handler_cb_menu__music_button_(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_FOCUSED) {
        ui_set_footer("Music");
    }
    if (event == LV_EVENT_RELEASED) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 3, 0, e);
    }
}

static void event_handler_cb_menu__smart_home_button_(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_FOCUSED) {
        ui_set_footer("Smart Home");
    }
    if (event == LV_EVENT_RELEASED) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 5, 0, e);
    }
}

static void event_handler_cb_menu__games_button_(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_FOCUSED) {
        ui_set_footer("Games");
    }
    if (event == LV_EVENT_RELEASED) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 7, 0, e);
    }
}

static void event_handler_cb_menu__settings_button_(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_FOCUSED) {
        ui_set_footer("Settings");
    }
    if (event == LV_EVENT_RELEASED) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 9, 0, e);
    }
}

static void event_handler_cb_menu__about_button_(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_FOCUSED) {
        ui_set_footer("About");
    }
    if (event == LV_EVENT_RELEASED) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 11, 0, e);
    }
}

static void event_handler_cb_assistant_assistant(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_SCREEN_LOADED) {
        ui_set_footer("Assistant");
    }
    if (event == LV_EVENT_LONG_PRESSED) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 1, 0, e);
    }
}

static void event_handler_cb_music_music(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;

    if (event == LV_EVENT_SCREEN_LOADED) {
        ui_set_footer("Music");
        extern void ws_send_spotify_list(void);
        extern void ws_send_spotify_status(void);
        extern void ws_send_spotify_status_delayed(void);
        ws_send_spotify_status_delayed();
        ws_send_spotify_list();
    }
    if (event == LV_EVENT_LONG_PRESSED) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 1, 0, e);
    }
}

static void event_handler_cb_smart_home_smart_home(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_SCREEN_LOADED) {
        ui_set_footer("Smart Home");
    }
    if (event == LV_EVENT_LONG_PRESSED) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 1, 0, e);
    }
}

static void bricks_breaker_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        bricks_breaker_start();
    }
}

static void living_room_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        extern void ws_send_led_toggle(void);
        ws_send_led_toggle();
        ESP_LOGI("SmartHome", "Living Room LED toggle");
    }
}

static void event_handler_cb_games_games(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_SCREEN_LOADED) {
        ui_set_footer("Games");
    }
    if (event == LV_EVENT_LONG_PRESSED) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 1, 0, e);
    }
}

static void event_handler_cb_settings_settings(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_SCREEN_LOADED) {
        ui_set_footer("Settings");
    }
    if (event == LV_EVENT_LONG_PRESSED) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 1, 0, e);
    }
}

static void event_handler_cb_about_about(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_SCREEN_LOADED) {
        ui_set_footer("Info");
    }
    if (event == LV_EVENT_LONG_PRESSED) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 1, 0, e);
    }
}

//
// Screens
//

void create_screen_splash() {
    void *flowState = getFlowState(0, 0);
    (void)flowState;
    lv_obj_t *obj = lv_obj_create(0);
    objects.splash = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 240, 320);
    lv_obj_add_event_cb(obj, event_handler_cb_splash_splash, LV_EVENT_ALL, flowState);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        lv_obj_t *parent_obj = obj;
        {
            lv_obj_t *obj = lv_image_create(parent_obj);
            lv_obj_set_pos(obj, 20, 60);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_image_set_src(obj, &img_ai_asistant);
        }
        {
            // Loading
            lv_obj_t *obj = lv_spinner_create(parent_obj);
            objects.loading = obj;
            lv_obj_set_pos(obj, 50, 54);
            lv_obj_set_size(obj, 141, 127);
        }
    }
    
    tick_screen_splash();
}

void tick_screen_splash() {
    void *flowState = getFlowState(0, 0);
    (void)flowState;
}

void create_screen_main() {
    void *flowState = getFlowState(0, 1);
    (void)flowState;
    lv_obj_t *obj = lv_obj_create(0);
    objects.main = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 240, 320);
    lv_obj_add_event_cb(obj, event_handler_cb_main_main, LV_EVENT_ALL, flowState);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        lv_obj_t *parent_obj = obj;
        {
            // headerContainer
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.header_container = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 240, 34);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff1270dd), LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // wifiicon
                    lv_obj_t *obj = lv_image_create(parent_obj);
                    objects.wifiicon = obj;
                    lv_obj_set_pos(obj, 190, 0);
                    lv_obj_set_size(obj, 40, 40);
                    lv_image_set_src(obj, &img_wifi);
                    lv_image_set_scale(obj, 250);
                    lv_obj_set_style_image_recolor(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_image_recolor_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                }
                {
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.obj0 = obj;
                    lv_obj_set_pos(obj, 51, 4);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_bg_opa(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "Arkadash");
                }
                {
                    // Battery_1
                    lv_obj_t *obj = lv_image_create(parent_obj);
                    objects.battery_1 = obj;
                    lv_obj_set_pos(obj, 3, -3);
                    lv_obj_set_size(obj, 40, 40);
                    lv_image_set_src(obj, &img_full_battery);
                    lv_image_set_scale(obj, 75);
                    lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
                }
            }
        }
        {
            // timeContainer
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.time_container = obj;
            lv_obj_set_pos(obj, 0, 36);
            lv_obj_set_size(obj, 240, 99);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 25, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(obj, lv_color_hex(0xff41ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 70, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0xff212121), LV_PART_TEXTAREA_PLACEHOLDER | LV_STATE_EDITED);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // day_text
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.day_text = obj;
                    lv_obj_set_pos(obj, 114, 64);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "");
                }
                {
                    // year_text
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.year_text = obj;
                    lv_obj_set_pos(obj, 192, 59);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "");
                }
                {
                    // month_text
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.month_text = obj;
                    lv_obj_set_pos(obj, 49, 59);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xfffffcfc), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "");
                }
                {
                    // date_text
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.date_text = obj;
                    lv_obj_set_pos(obj, 7, 59);
                    lv_obj_set_size(obj, 38, LV_SIZE_CONTENT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "");
                }
                {
                    // minute_text
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.minute_text = obj;
                    lv_obj_set_pos(obj, 84, 7);
                    lv_obj_set_size(obj, 67, LV_SIZE_CONTENT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "");
                }
                {
                    // centre_dot
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.centre_dot = obj;
                    lv_obj_set_pos(obj, 72, 7);
                    lv_obj_set_size(obj, 15, LV_SIZE_CONTENT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, ":");
                }
                {
                    // hour_text
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.hour_text = obj;
                    lv_obj_set_pos(obj, 7, 7);
                    lv_obj_set_size(obj, 66, LV_SIZE_CONTENT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "");
                }
                {
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.obj1 = obj;
                    lv_obj_set_pos(obj, 161, 7);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "");
                }
                {
                    // centre_dot_1
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.centre_dot_1 = obj;
                    lv_obj_set_pos(obj, 151, 7);
                    lv_obj_set_size(obj, 15, LV_SIZE_CONTENT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, ":");
                }
            }
        }
        {
            // weatherContainer
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.weather_container = obj;
            lv_obj_set_pos(obj, 0, 140);
            lv_obj_set_size(obj, 240, 123);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 70, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 25, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(obj, lv_color_hex(0xffe0e0e0), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0xff212121), LV_PART_TEXTAREA_PLACEHOLDER | LV_STATE_EDITED);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // thermometer
                    lv_obj_t *obj = lv_image_create(parent_obj);
                    objects.thermometer = obj;
                    lv_obj_set_pos(obj, 100, 10);
                    lv_obj_set_size(obj, 40, 10);
                    lv_image_set_src(obj, &img_therm);
                    lv_image_set_scale(obj, 80);
                    lv_obj_set_style_image_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                }
                {
                    // weather_icon
                    lv_obj_t *obj = lv_image_create(parent_obj);
                    objects.weather_icon = obj;
                    lv_obj_set_pos(obj, 7, 5);
                    lv_obj_set_size(obj, 80, 80);
                    lv_image_set_src(obj, &img_sun);
                    lv_image_set_scale(obj, 150);
                }
                {
                    // weather_temp_label
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.weather_temp_label = obj;
                    lv_obj_set_pos(obj, 133, 5);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "");
                }
                {
                    // °C
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects._c = obj;
                    lv_obj_set_pos(obj, 198, 5);
                    lv_obj_set_size(obj, 30, LV_SIZE_CONTENT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "°C");
                }
                {
                    // hum_icon
                    lv_obj_t *obj = lv_image_create(parent_obj);
                    objects.hum_icon = obj;
                    lv_obj_set_pos(obj, 100, 40);
                    lv_obj_set_size(obj, 40, 40);
                    lv_image_set_src(obj, &img_hum);
                    lv_image_set_scale(obj, 80);
                    lv_obj_set_style_image_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                }
                {
                    // weather_hum_label
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.weather_hum_label = obj;
                    lv_obj_set_pos(obj, 133, 45);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "");
                }
                {
                    // %
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects._ = obj;
                    lv_obj_set_pos(obj, 195, 45);
                    lv_obj_set_size(obj, 22, LV_SIZE_CONTENT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "%");
                }
                {
                    // weather_state
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.weather_state = obj;
                    lv_obj_set_pos(obj, 16, 91);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "Sunny");
                }
            }
        }
        {
            // bottomContainer
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.bottom_container = obj;
            lv_obj_set_pos(obj, 0, 268);
            lv_obj_set_size(obj, 240, 52);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff1270dd), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // Author
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.author = obj;
                    lv_obj_set_pos(obj, 3, 15);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "Author : Serhat SADAY");
                }
            }
        }
    }
    
    tick_screen_main();
}

void tick_screen_main() {
    void *flowState = getFlowState(0, 1);
    (void)flowState;
    {
        const char *new_val = evalTextProperty(flowState, 6, 3, "Failed to evaluate Text in Label widget");
        const char *cur_val = lv_label_get_text(objects.day_text);
        if (strcmp(new_val, cur_val) != 0) {
            tick_value_change_obj = objects.day_text;
            lv_label_set_text(objects.day_text, new_val);
            tick_value_change_obj = NULL;
        }
    }
    {
        const char *new_val = evalTextProperty(flowState, 7, 3, "Failed to evaluate Text in Label widget");
        const char *cur_val = lv_label_get_text(objects.year_text);
        if (strcmp(new_val, cur_val) != 0) {
            tick_value_change_obj = objects.year_text;
            lv_label_set_text(objects.year_text, new_val);
            tick_value_change_obj = NULL;
        }
    }
    {
        const char *new_val = evalTextProperty(flowState, 8, 3, "Failed to evaluate Text in Label widget");
        const char *cur_val = lv_label_get_text(objects.month_text);
        if (strcmp(new_val, cur_val) != 0) {
            tick_value_change_obj = objects.month_text;
            lv_label_set_text(objects.month_text, new_val);
            tick_value_change_obj = NULL;
        }
    }
    {
        const char *new_val = evalTextProperty(flowState, 9, 3, "Failed to evaluate Text in Label widget");
        const char *cur_val = lv_label_get_text(objects.date_text);
        if (strcmp(new_val, cur_val) != 0) {
            tick_value_change_obj = objects.date_text;
            lv_label_set_text(objects.date_text, new_val);
            tick_value_change_obj = NULL;
        }
    }
    {
        const char *new_val = evalTextProperty(flowState, 10, 3, "Failed to evaluate Text in Label widget");
        const char *cur_val = lv_label_get_text(objects.minute_text);
        if (strcmp(new_val, cur_val) != 0) {
            tick_value_change_obj = objects.minute_text;
            lv_label_set_text(objects.minute_text, new_val);
            tick_value_change_obj = NULL;
        }
    }
    {
        const char *new_val = evalTextProperty(flowState, 12, 3, "Failed to evaluate Text in Label widget");
        const char *cur_val = lv_label_get_text(objects.hour_text);
        if (strcmp(new_val, cur_val) != 0) {
            tick_value_change_obj = objects.hour_text;
            lv_label_set_text(objects.hour_text, new_val);
            tick_value_change_obj = NULL;
        }
    }
    {
        const char *new_val = evalTextProperty(flowState, 13, 3, "Failed to evaluate Text in Label widget");
        const char *cur_val = lv_label_get_text(objects.obj1);
        if (strcmp(new_val, cur_val) != 0) {
            tick_value_change_obj = objects.obj1;
            lv_label_set_text(objects.obj1, new_val);
            tick_value_change_obj = NULL;
        }
    }
    {
        const char *new_val = evalTextProperty(flowState, 18, 3, "Failed to evaluate Text in Label widget");
        const char *cur_val = lv_label_get_text(objects.weather_temp_label);
        if (strcmp(new_val, cur_val) != 0) {
            tick_value_change_obj = objects.weather_temp_label;
            lv_label_set_text(objects.weather_temp_label, new_val);
            tick_value_change_obj = NULL;
        }
    }
    {
        const char *new_val = evalTextProperty(flowState, 21, 3, "Failed to evaluate Text in Label widget");
        const char *cur_val = lv_label_get_text(objects.weather_hum_label);
        if (strcmp(new_val, cur_val) != 0) {
            tick_value_change_obj = objects.weather_hum_label;
            lv_label_set_text(objects.weather_hum_label, new_val);
            tick_value_change_obj = NULL;
        }
    }
}

void create_screen_menu() {
    void *flowState = getFlowState(0, 2);
    (void)flowState;
    lv_obj_t *obj = lv_obj_create(0);
    objects.menu = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 240, 320);
    lv_obj_add_event_cb(obj, event_handler_cb_menu_menu, LV_EVENT_ALL, flowState);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        lv_obj_t *parent_obj = obj;
        {
            // asistant_button
            lv_obj_t *obj = lv_button_create(parent_obj);
            objects.asistant_button = obj;
            lv_obj_set_pos(obj, 5, 42);
            lv_obj_set_size(obj, 110, 60);
            lv_obj_add_event_cb(obj, event_handler_cb_menu_asistant_button, LV_EVENT_ALL, flowState);
            lv_obj_set_style_radius(obj, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    lv_obj_t *obj = lv_image_create(parent_obj);
                    lv_obj_set_pos(obj, 25, 0);
                    lv_obj_set_size(obj, 40, 40);
                    lv_image_set_src(obj, &img_chatbot);
                    lv_image_set_scale(obj, 100);
                }
            }
        }
        {
            // (music_button)
            lv_obj_t *obj = lv_button_create(parent_obj);
            objects._music_button_ = obj;
            lv_obj_set_pos(obj, 125, 42);
            lv_obj_set_size(obj, 110, 60);
            lv_obj_add_event_cb(obj, event_handler_cb_menu__music_button_, LV_EVENT_ALL, flowState);
            lv_obj_set_style_radius(obj, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    lv_obj_t *obj = lv_image_create(parent_obj);
                    lv_obj_set_pos(obj, 25, 0);
                    lv_obj_set_size(obj, 40, 40);
                    lv_image_set_src(obj, &img_music);
                    lv_image_set_scale(obj, 100);
                }
            }
        }
        {
            // (smartHome_button)
            lv_obj_t *obj = lv_button_create(parent_obj);
            objects._smart_home_button_ = obj;
            lv_obj_set_pos(obj, 5, 110);
            lv_obj_set_size(obj, 110, 60);
            lv_obj_add_event_cb(obj, event_handler_cb_menu__smart_home_button_, LV_EVENT_ALL, flowState);
            lv_obj_set_style_radius(obj, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    lv_obj_t *obj = lv_image_create(parent_obj);
                    lv_obj_set_pos(obj, 25, 0);
                    lv_obj_set_size(obj, 40, 40);
                    lv_image_set_src(obj, &img_smart_home);
                    lv_image_set_scale(obj, 100);
                }
            }
        }
        {
            // (games_button)
            lv_obj_t *obj = lv_button_create(parent_obj);
            objects._games_button_ = obj;
            lv_obj_set_pos(obj, 125, 110);
            lv_obj_set_size(obj, 110, 60);
            lv_obj_add_event_cb(obj, event_handler_cb_menu__games_button_, LV_EVENT_ALL, flowState);
            lv_obj_set_style_radius(obj, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    lv_obj_t *obj = lv_image_create(parent_obj);
                    lv_obj_set_pos(obj, 25, 0);
                    lv_obj_set_size(obj, 40, 40);
                    lv_image_set_src(obj, &img_games);
                    lv_image_set_scale(obj, 100);
                }
            }
        }
        {
            // (settings_button)
            lv_obj_t *obj = lv_button_create(parent_obj);
            objects._settings_button_ = obj;
            lv_obj_set_pos(obj, 5, 178);
            lv_obj_set_size(obj, 110, 60);
            lv_obj_add_event_cb(obj, event_handler_cb_menu__settings_button_, LV_EVENT_ALL, flowState);
            lv_obj_set_style_radius(obj, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    lv_obj_t *obj = lv_image_create(parent_obj);
                    lv_obj_set_pos(obj, 25, 0);
                    lv_obj_set_size(obj, 40, 40);
                    lv_image_set_src(obj, &img_settings);
                    lv_image_set_scale(obj, 100);
                }
            }
        }
        {
            // (about_button)
            lv_obj_t *obj = lv_button_create(parent_obj);
            objects._about_button_ = obj;
            lv_obj_set_pos(obj, 125, 178);
            lv_obj_set_size(obj, 110, 60);
            lv_obj_add_event_cb(obj, event_handler_cb_menu__about_button_, LV_EVENT_ALL, flowState);
            lv_obj_set_style_radius(obj, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    lv_obj_t *obj = lv_image_create(parent_obj);
                    lv_obj_set_pos(obj, 25, 0);
                    lv_obj_set_size(obj, 40, 40);
                    lv_image_set_src(obj, &img_info);
                    lv_image_set_scale(obj, 100);
                }
            }
        }
        {
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.obj2 = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 240, 34);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff1270dd), LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // Battery
                    lv_obj_t *obj = lv_image_create(parent_obj);
                    objects.battery = obj;
                    lv_obj_set_pos(obj, 3, -8);
                    lv_obj_set_size(obj, 50, 50);
                    lv_image_set_src(obj, &img_full_battery);
                    lv_image_set_scale(obj, 75);
                }
                {
                    // wifiicon_1
                    lv_obj_t *obj = lv_image_create(parent_obj);
                    objects.wifiicon_1 = obj;
                    lv_obj_set_pos(obj, 190, 0);
                    lv_obj_set_size(obj, 40, 40);
                    lv_image_set_src(obj, &img_wifi);
                    lv_image_set_scale(obj, 250);
                    lv_obj_set_style_image_recolor(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_image_recolor_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
                }
                {
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.obj3 = obj;
                    lv_obj_set_pos(obj, 51, 4);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_bg_opa(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "Arkadash");
                }
            }
        }
        {
            // bottomContainer_1
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.bottom_container_1 = obj;
            lv_obj_set_pos(obj, 0, 268);
            lv_obj_set_size(obj, 240, 52);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff1270dd), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // Author_1
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.author_1 = obj;
                    lv_obj_set_pos(obj, 83, 15);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "Serhat SADAY");
                }
            }
        }
    }
    
    tick_screen_menu();
}

void tick_screen_menu() {
    void *flowState = getFlowState(0, 2);
    (void)flowState;
}

void create_screen_assistant() {
    void *flowState = getFlowState(0, 3);
    (void)flowState;
    lv_obj_t *obj = lv_obj_create(0);
    objects.assistant = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 240, 320);
    lv_obj_add_event_cb(obj, event_handler_cb_assistant_assistant, LV_EVENT_ALL, flowState);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        lv_obj_t *parent_obj = obj;
        {
            // chatfooter
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.chatfooter = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 240, 34);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff1270dd), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        {
            lv_obj_t *obj = lv_image_create(parent_obj);
            lv_obj_set_pos(obj, 16, 17);
            lv_obj_set_size(obj, 40, 40);
            lv_image_set_src(obj, &img_chatbot);
            lv_image_set_scale(obj, 100);
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.obj4 = obj;
            lv_obj_set_pos(obj, 92, 4);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(obj, "Asistant");
        }
        {
            // bottomContainer_2
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.bottom_container_2 = obj;
            lv_obj_set_pos(obj, 0, 268);
            lv_obj_set_size(obj, 240, 52);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff1270dd), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // Author_2
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.author_2 = obj;
                    lv_obj_set_pos(obj, 83, 15);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "");
                }
            }
        }
        {
            // aivoice
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.aivoice = obj;
            lv_obj_set_pos(obj, -1, 34);
            lv_obj_set_size(obj, 240, 110);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(obj, lv_color_hex(0xfff50303), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_outline_width(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_outline_color(obj, lv_color_hex(0xffff0000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_clip_corner(obj, true, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xffa1a1a1), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 70, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 25, LV_PART_MAIN | LV_STATE_DEFAULT);
            // HAL 9000 eye (aianim) — 2026-06-28 izole test: aianim widget tamamen kaldırıldı
            // Sorunun kaynağını bulmak için. Asistan ekranına girmek watchdog tetikliyordu,
            // aianim gizli olsa bile tetikleniyordu. Şimdi sadece clean aivoice container var.
        }
        {
            // microphone
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.microphone = obj;
            lv_obj_set_pos(obj, 0, 160);
            lv_obj_set_size(obj, 239, 100);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xffacacac), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 70, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(obj, lv_color_hex(0xff19c1ff), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 25, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // Mic
                    lv_obj_t *obj = lv_image_create(parent_obj);
                    objects.mic = obj;
                    lv_obj_set_pos(obj, 62, 4);
                    lv_obj_set_size(obj, 100, 80);
                    lv_image_set_src(obj, &img_microphone);
                    lv_image_set_scale(obj, 128);
                }
            }
        }
    }
    
    tick_screen_assistant();
}

void tick_screen_assistant() {
    void *flowState = getFlowState(0, 3);
    (void)flowState;
}

static void event_handler_cb_music_prev_btn(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        extern void ws_send_spotify_skip(void);
        ws_send_spotify_skip();
        ESP_LOGI("MUSIC", "Previous track requested");
    }
}

static void event_handler_cb_music_playpause_btn(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        extern void ws_send_spotify_toggle(void);
        ws_send_spotify_toggle();
        ESP_LOGI("MUSIC", "Play/Pause toggled");
    }
}

static void event_handler_cb_music_next_btn(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        extern void ws_send_spotify_skip(void);
        ws_send_spotify_skip();
        ESP_LOGI("MUSIC", "Next track requested");
    }
}

void create_screen_music() {
    void *flowState = getFlowState(0, 4);
    (void)flowState;
    lv_obj_t *obj = lv_obj_create(0);
    objects.music = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 240, 320);
    lv_obj_add_event_cb(obj, event_handler_cb_music_music, LV_EVENT_ALL, flowState);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        lv_obj_t *parent_obj = obj;
        {
            // Header container - Arkadash title only
            lv_obj_t *hdr = lv_obj_create(parent_obj);
            objects.obj5 = hdr;
            lv_obj_set_pos(hdr, 0, 0);
            lv_obj_set_size(hdr, 240, 34);
            lv_obj_set_style_pad_left(hdr, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(hdr, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(hdr, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(hdr, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(hdr, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(hdr, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(hdr, lv_color_hex(0xff1270dd), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(hdr, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *h = hdr;
                {
                    lv_obj_t *icon = lv_image_create(h);
                    lv_obj_set_pos(icon, 13, 17);
                    lv_obj_set_size(icon, 40, 40);
                    lv_image_set_src(icon, &img_music);
                    lv_image_set_scale(icon, 100);
                }
                {
                    lv_obj_t *lbl = lv_label_create(h);
                    objects.obj6 = lbl;
                    lv_obj_set_pos(lbl, 82, 2);
                    lv_obj_set_size(lbl, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_text_color(lbl, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(lbl, "Music");
                }
            }
        }
        {
            // Now Playing container - matches eezPilot layout
            lv_obj_t *card = lv_obj_create(parent_obj);
            lv_obj_set_pos(card, -1, 37);
            lv_obj_set_size(card, 240, 110);
            lv_obj_set_style_pad_left(card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(card, 25, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(card, lv_color_hex(0xffa1a1a1), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(card, 70, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(card, lv_color_hex(0xfff50303), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(card, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *c = card;
                {
                    // Song name label (bottom)
                    lv_obj_t *title = lv_label_create(c);
                    objects.music_now_playing_label = title;
                    lv_obj_set_pos(title, 82, 43);
                    lv_obj_set_size(title, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_text_color(title, lv_color_hex(0xffffffff), LV_PART_MAIN);
                    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, LV_PART_MAIN);
                    lv_label_set_text(title, "No track");
                }
                {
                    // music note icon (middle)
                    lv_obj_t *note_img = lv_image_create(c);
                    lv_obj_set_pos(note_img, 23, 34);
                    lv_obj_set_size(note_img, 40, 40);
                    lv_image_set_src(note_img, &img_notes);
                    lv_image_set_scale(note_img, 100);
                }
                {
                    // nowplaytext (top)
                    lv_obj_t *icon = lv_label_create(c);
                    lv_obj_set_pos(icon, 63, 11);
                    lv_obj_set_style_text_color(icon, lv_color_hex(0xffffffff), LV_PART_MAIN);
                    lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, LV_PART_MAIN);
                    lv_label_set_text(icon, "Now Playing");
                }
                // Prev button (inside nowplay container)
                lv_obj_t *prev_btn = lv_btn_create(c);
                objects.music_prev_btn = prev_btn;
                lv_obj_set_pos(prev_btn, 85, 75);
                lv_obj_set_size(prev_btn, 30, 30);
                lv_obj_set_style_radius(prev_btn, 0, LV_PART_MAIN);
                lv_obj_set_style_bg_color(prev_btn, lv_color_hex(0xff1270dd), LV_PART_MAIN);
                lv_obj_set_style_bg_opa(prev_btn, 255, LV_PART_MAIN);
                lv_obj_add_event_cb(prev_btn, event_handler_cb_music_prev_btn, LV_EVENT_ALL, NULL);
                {
                    lv_obj_t *img = lv_image_create(prev_btn);
                    lv_obj_set_pos(img, -10, -5);
                    lv_obj_set_size(img, 25, 25);
                    lv_image_set_src(img, &img_prev);
                    lv_image_set_scale(img, 50);
                }
                // Play/Pause button (inside nowplay container)
                lv_obj_t *play_btn = lv_btn_create(c);
                objects.music_playpause_btn = play_btn;
                lv_obj_set_pos(play_btn, 120, 75);
                lv_obj_set_size(play_btn, 30, 30);
                lv_obj_set_style_radius(play_btn, 0, LV_PART_MAIN);
                lv_obj_set_style_bg_color(play_btn, lv_color_hex(0xff1270dd), LV_PART_MAIN);
                lv_obj_set_style_bg_opa(play_btn, 255, LV_PART_MAIN);
                lv_obj_add_event_cb(play_btn, event_handler_cb_music_playpause_btn, LV_EVENT_ALL, NULL);
                {
                    lv_obj_t *img = lv_image_create(play_btn);
                    lv_obj_set_pos(img, -10, -5);
                    lv_obj_set_size(img, 25, 25);
                    lv_image_set_src(img, &img_play);
                    lv_image_set_scale(img, 50);
                }
                // Next button (inside nowplay container)
                lv_obj_t *next_btn = lv_btn_create(c);
                objects.music_next_btn = next_btn;
                lv_obj_set_pos(next_btn, 155, 75);
                lv_obj_set_size(next_btn, 30, 30);
                lv_obj_set_style_radius(next_btn, 0, LV_PART_MAIN);
                lv_obj_set_style_bg_color(next_btn, lv_color_hex(0xff1270dd), LV_PART_MAIN);
                lv_obj_set_style_bg_opa(next_btn, 255, LV_PART_MAIN);
                lv_obj_add_event_cb(next_btn, event_handler_cb_music_next_btn, LV_EVENT_ALL, NULL);
                {
                    lv_obj_t *img = lv_image_create(next_btn);
                    lv_obj_set_pos(img, -10, -5);
                    lv_obj_set_size(img, 25, 25);
                    lv_image_set_src(img, &img_next);
                    lv_image_set_scale(img, 50);
                }
            }
        }
        {
            // Playlist card
            lv_obj_t *pcard = lv_obj_create(parent_obj);
            lv_obj_set_pos(pcard, 0, 160);
            lv_obj_set_size(pcard, 239, 100);
            lv_obj_set_style_radius(pcard, 25, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(pcard, lv_color_hex(0xffacacac), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(pcard, 70, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(pcard, lv_color_hex(0xff19c1ff), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(pcard, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *pc = pcard;
                {
                    // Playlist icon (eezPilot layout)
                    lv_obj_t *pl_icon = lv_image_create(pc);
                    lv_obj_set_pos(pl_icon, 20, 27);
                    lv_obj_set_size(pl_icon, 40, 40);
                    lv_image_set_src(pl_icon, &img_playlist);
                    lv_image_set_scale(pl_icon, 100);
                }
                {
                    // Playlist label (eezPilot layout)
                    lv_obj_t *title = lv_label_create(pc);
                    lv_obj_set_pos(title, 71, 11);
                    lv_obj_set_style_text_color(title, lv_color_hex(0xffffffff), LV_PART_MAIN);
                    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);
                    lv_label_set_text(title, "Playlists");
                }
                {
                    // Playlist labels (eezPilot static layout - 3 labels)
                    // Single combined label for dynamic Spotify updates
                    lv_obj_t *pl_lbl = lv_label_create(pc);
                    objects.music_playlist_list = pl_lbl;
                    lv_obj_set_pos(pl_lbl, 91, 25);
                    lv_obj_set_size(pl_lbl, 130, 65);
                    lv_obj_set_style_text_color(pl_lbl, lv_color_hex(0xffffffff), LV_PART_MAIN);
                    lv_obj_set_style_text_font(pl_lbl, &lv_font_montserrat_20, LV_PART_MAIN);
                    lv_label_set_text(pl_lbl, "Listname1\nListname2\nListname3");
                }
            }
        }
        {
            // bottomContainer_3
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.bottom_container_3 = obj;
            lv_obj_set_pos(obj, 0, 268);
            lv_obj_set_size(obj, 240, 52);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff1270dd), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *f = obj;
                lv_obj_t *author = lv_label_create(f);
                objects.author_3 = author;
                lv_obj_set_pos(author, 83, 15);
                lv_obj_set_size(author, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                lv_obj_set_style_text_font(author, &lv_font_montserrat_20, LV_PART_MAIN);
                lv_obj_set_style_text_color(author, lv_color_hex(0xffffffff), LV_PART_MAIN);
                lv_label_set_text(author, "");
            }
        }

        tick_screen_music();
    }
}

void tick_screen_music() {
    void *flowState = getFlowState(0, 4);
    (void)flowState;
}

void create_screen_smart_home() {
    void *flowState = getFlowState(0, 5);
    (void)flowState;
    lv_obj_t *obj = lv_obj_create(0);
    objects.smart_home = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 240, 320);
    lv_obj_add_event_cb(obj, event_handler_cb_smart_home_smart_home, LV_EVENT_ALL, flowState);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        lv_obj_t *parent_obj = obj;
        {
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.obj7 = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 240, 34);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff1270dd), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        {
            lv_obj_t *obj = lv_image_create(parent_obj);
            lv_obj_set_pos(obj, 15, 19);
            lv_obj_set_size(obj, 40, 40);
            lv_image_set_src(obj, &img_smart_home);
            lv_image_set_scale(obj, 100);
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.obj8 = obj;
            lv_obj_set_pos(obj, 67, 4);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(obj, "Smart Home");
        }
        // Living Room button
        {
            lv_obj_t *btn = lv_btn_create(parent_obj);
            objects.living_room_btn = btn;
            lv_obj_set_pos(btn, 65, 60);
            lv_obj_set_size(btn, 165, 50);
            lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);
            lv_obj_add_event_cb(btn, living_room_btn_cb, LV_EVENT_CLICKED, NULL);
            {
                lv_obj_t *img = lv_image_create(btn);
                lv_obj_set_pos(img, -3, -9);
                lv_obj_set_size(img, 50, 50);
                lv_image_set_src(img, &img_living);
                lv_image_set_scale(img, 100);
                lv_obj_t *lbl = lv_label_create(btn);
                lv_obj_set_pos(lbl, 31, 0);
                lv_obj_set_size(lbl, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                lv_obj_set_style_align(lbl, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_label_set_text(lbl, "Living Room");
            }
        }
        // Kitchen button
        {
            lv_obj_t *btn = lv_btn_create(parent_obj);
            objects.kitchen_btn = btn;
            lv_obj_set_pos(btn, 65, 120);
            lv_obj_set_size(btn, 165, 50);
            lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);
            {
                lv_obj_t *img = lv_image_create(btn);
                lv_obj_set_pos(img, -3, -9);
                lv_obj_set_size(img, 50, 50);
                lv_image_set_src(img, &img_kitchen);
                lv_image_set_scale(img, 100);
                lv_obj_t *lbl = lv_label_create(btn);
                lv_obj_set_pos(lbl, 48, 0);
                lv_obj_set_size(lbl, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                lv_obj_set_style_align(lbl, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_label_set_text(lbl, "Kitchen");
            }
        }
        // Temperature button
        {
            lv_obj_t *btn = lv_btn_create(parent_obj);
            objects.temperature_btn = btn;
            lv_obj_set_pos(btn, 65, 180);
            lv_obj_set_size(btn, 165, 50);
            lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);
            {
                lv_obj_t *img = lv_image_create(btn);
                lv_obj_set_pos(img, -3, -6);
                lv_obj_set_size(img, 50, 50);
                lv_image_set_src(img, &img_temperature);
                lv_image_set_scale(img, 100);
                lv_obj_t *lbl = lv_label_create(btn);
                lv_obj_set_pos(lbl, 29, 0);
                lv_obj_set_size(lbl, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                lv_obj_set_style_align(lbl, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_label_set_text(lbl, "Temperature");
            }
        }
        // Thread icon
        {
            lv_obj_t *img = lv_image_create(parent_obj);
            lv_obj_set_pos(img, 5, 80);
            lv_obj_set_size(img, 50, 50);
            lv_image_set_src(img, &img_thread);
            lv_image_set_scale(img, 180);
        }
        // Matter icon
        {
            lv_obj_t *img = lv_image_create(parent_obj);
            lv_obj_set_pos(img, 5, 147);
            lv_obj_set_size(img, 50, 50);
            lv_image_set_src(img, &img_matter);
            lv_image_set_scale(img, 70);
        }
        {
            // bottomContainer_4
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.bottom_container_4 = obj;
            lv_obj_set_pos(obj, 0, 268);
            lv_obj_set_size(obj, 240, 52);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff1270dd), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // Author_4
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.author_4 = obj;
                    lv_obj_set_pos(obj, 83, 15);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "");
                }
            }
        }
    }
    
    tick_screen_smart_home();
}

void tick_screen_smart_home() {
    void *flowState = getFlowState(0, 5);
    (void)flowState;
}

void create_screen_games() {
    void *flowState = getFlowState(0, 6);
    (void)flowState;
    lv_obj_t *obj = lv_obj_create(0);
    objects.games = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 240, 320);
    lv_obj_add_event_cb(obj, event_handler_cb_games_games, LV_EVENT_ALL, flowState);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        lv_obj_t *parent_obj = obj;
        {
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.obj9 = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 240, 34);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff1270dd), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        {
            lv_obj_t *obj = lv_image_create(parent_obj);
            lv_obj_set_pos(obj, 16, 17);
            lv_obj_set_size(obj, 40, 40);
            lv_image_set_src(obj, &img_games);
            lv_image_set_scale(obj, 100);
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.obj10 = obj;
            lv_obj_set_pos(obj, 78, 0);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(obj, "Games");
        }
        {
            // bottomContainer_5
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.bottom_container_5 = obj;
            lv_obj_set_pos(obj, 0, 268);
            lv_obj_set_size(obj, 240, 52);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff1270dd), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // Author_5
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.author_5 = obj;
                    lv_obj_set_pos(obj, 83, 15);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "");
                }
            }
        }
    }
    // Bricks Breaker Button
    objects.bricks_btn = lv_btn_create(obj);
    lv_obj_set_pos(objects.bricks_btn, 40, 80);
    lv_obj_set_size(objects.bricks_btn, 160, 60);
    lv_obj_set_style_bg_color(objects.bricks_btn, lv_color_hex(0xff1270dd), LV_PART_MAIN);
    lv_obj_set_style_radius(objects.bricks_btn, 10, LV_PART_MAIN);
    {
        lv_obj_t *lbl = lv_label_create(objects.bricks_btn);
        lv_label_set_text(lbl, "Bricks Breaker");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xffffffff), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, LV_PART_MAIN);
        lv_obj_center(lbl);
    }
    lv_obj_add_event_cb(objects.bricks_btn, bricks_breaker_btn_cb, LV_EVENT_CLICKED, NULL);

    bricks_breaker_init(objects.games);

    tick_screen_games();
}

void tick_screen_games() {
    void *flowState = getFlowState(0, 6);
    (void)flowState;
}

void create_screen_settings() {
    void *flowState = getFlowState(0, 7);
    (void)flowState;
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 240, 320);
    lv_obj_add_event_cb(obj, event_handler_cb_settings_settings, LV_EVENT_ALL, flowState);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        lv_obj_t *parent_obj = obj;
        {
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.obj11 = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 240, 34);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff1270dd), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        {
            lv_obj_t *obj = lv_image_create(parent_obj);
            lv_obj_set_pos(obj, 17, 19);
            lv_obj_set_size(obj, 40, 40);
            lv_image_set_src(obj, &img_settings);
            lv_image_set_scale(obj, 100);
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.obj12 = obj;
            lv_obj_set_pos(obj, 86, 4);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(obj, "Settings");
        }
        {
            // bottomContainer_6
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.bottom_container_6 = obj;
            lv_obj_set_pos(obj, 0, 268);
            lv_obj_set_size(obj, 240, 52);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff1270dd), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // Author_6
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.author_6 = obj;
                    lv_obj_set_pos(obj, 83, 15);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "");
                }
            }
        }
    }
    
    /* Manual additions: volume/brightness sliders + Provision button.
     * Snipped from the EEZ Studio v9.5 export (create_screen_settings,
     * the part that follows the bottom_container_6 footer block). The
     * event_handler_cb_settings_prov callback is not exported by EEZ
     * Studio into our build because the original export relies on
     * eez-flow state. We do not need it: main.c triggers the
     * provision screen via lv_scr_load(objects.provision) from the
     * Provision button's LV_EVENT_CLICKED handler. */
    lv_obj_t *settings_parent = obj;  /* the screen object itself */
    {
        // brightness slider
        lv_obj_t *obj = lv_slider_create(settings_parent);
        objects.brightness = obj;
        lv_obj_set_pos(obj, 0, 89);
        lv_obj_set_size(obj, 150, 10);
        lv_slider_set_value(obj, 25, LV_ANIM_OFF);
        lv_obj_set_style_bg_image_recolor(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_border_opa(obj, 0, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_border_color(obj, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_PRESSED);
    }
    {
        // volume slider
        lv_obj_t *obj = lv_slider_create(settings_parent);
        objects.volume = obj;
        lv_obj_set_pos(obj, 0, 123);
        lv_obj_set_size(obj, 150, 10);
        lv_slider_set_value(obj, 25, LV_ANIM_OFF);
    }
    {
        // Provision button
        lv_obj_t *obj = lv_button_create(settings_parent);
        objects.prov = obj;
        lv_obj_set_pos(obj, 4, 160);
        lv_obj_set_size(obj, 232, 22);
        /* Click handler is wired in main.c via lv_obj_add_event_cb
         * once objects.prov is non-NULL. We deliberately skip the
         * EEZ-flow event_handler_cb_settings_prov reference here. */
        {
            lv_obj_t *prov_parent = obj;
            {
                lv_obj_t *label = lv_label_create(prov_parent);
                lv_obj_set_pos(label, 0, 0);
                lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                lv_obj_set_style_align(label, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_label_set_text_static(label, "Wifi Provisioning");
            }
        }
    }
    {
        // Volume label
        lv_obj_t *obj = lv_label_create(settings_parent);
        objects.volumetext = obj;
        lv_obj_set_pos(obj, 158, 120);
        lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_text_color(obj, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text_static(obj, "Volume");
    }
    {
        // Brightness label
        lv_obj_t *obj = lv_label_create(settings_parent);
        objects.brightness_text = obj;
        lv_obj_set_pos(obj, 158, 86);
        lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_text_color(obj, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text_static(obj, "Brightness");
    }

    tick_screen_settings();
}

void tick_screen_settings() {
    void *flowState = getFlowState(0, 7);
    (void)flowState;
}

void create_screen_about() {
    void *flowState = getFlowState(0, 8);
    (void)flowState;
    lv_obj_t *obj = lv_obj_create(0);
    objects.about = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 240, 320);
    lv_obj_add_event_cb(obj, event_handler_cb_about_about, LV_EVENT_ALL, flowState);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        lv_obj_t *parent_obj = obj;
        {
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.obj13 = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 240, 34);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff1270dd), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        {
            lv_obj_t *obj = lv_image_create(parent_obj);
            lv_obj_set_pos(obj, 16, 17);
            lv_obj_set_size(obj, 40, 40);
            lv_image_set_src(obj, &img_info);
            lv_image_set_scale(obj, 100);
        }
        {
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.obj14 = obj;
            lv_obj_set_pos(obj, 107, 4);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(obj, "Info");
        }
        {
            // bottomContainer_7
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.bottom_container_7 = obj;
            lv_obj_set_pos(obj, 0, 268);
            lv_obj_set_size(obj, 240, 52);
            lv_obj_set_style_pad_left(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xff1270dd), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            {
                lv_obj_t *parent_obj = obj;
                {
                    // Author_7
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    objects.author_7 = obj;
                    lv_obj_set_pos(obj, 83, 15);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_bg_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "");
                }
            }
        }
        {
            lv_obj_t *obj = lv_image_create(parent_obj);
            lv_obj_set_pos(obj, 153, 57);
            lv_obj_set_size(obj, 66, 108);
            lv_image_set_src(obj, &img_infopic);
            lv_image_set_scale(obj, 128);
        }
        {
            // infoText
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.info_text = obj;
            lv_obj_set_pos(obj, 107, 183);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(obj, "Serhat B. SADAY");
        }
        {
            // title
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.title = obj;
            lv_obj_set_pos(obj, 102, 199);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(obj, "Teknik Ogretmen");
        }
        {
            // mail
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.mail = obj;
            lv_obj_set_pos(obj, 53, 215);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0xffffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(obj, "serhatsaday@gmail.com");
        }
    }
    
    tick_screen_about();
}

void tick_screen_about() {
    void *flowState = getFlowState(0, 8);
    (void)flowState;
}

typedef void (*tick_screen_func_t)();
tick_screen_func_t tick_screen_funcs[] = {
    tick_screen_splash,
    tick_screen_main,
    tick_screen_menu,
    tick_screen_assistant,
    tick_screen_music,
    tick_screen_smart_home,
    tick_screen_games,
    tick_screen_settings,
    tick_screen_provision,  // o-provision page (added 2026-06-13)
    tick_screen_about,
};
void tick_screen(int screen_index) {
    tick_screen_funcs[screen_index]();
}
void tick_screen_by_id(enum ScreensEnum screenId) {
    tick_screen_funcs[screenId - 1]();
}

//
// Fonts
//

ext_font_desc_t fonts[] = {
#if LV_FONT_MONTSERRAT_8
    { "MONTSERRAT_8", &lv_font_montserrat_8 },
#endif
#if LV_FONT_MONTSERRAT_10
    { "MONTSERRAT_10", &lv_font_montserrat_10 },
#endif
#if LV_FONT_MONTSERRAT_12
    { "MONTSERRAT_12", &lv_font_montserrat_12 },
#endif
#if LV_FONT_MONTSERRAT_14
    { "MONTSERRAT_14", &lv_font_montserrat_14 },
#endif
#if LV_FONT_MONTSERRAT_16
    { "MONTSERRAT_16", &lv_font_montserrat_16 },
#endif
#if LV_FONT_MONTSERRAT_18
    { "MONTSERRAT_18", &lv_font_montserrat_18 },
#endif
#if LV_FONT_MONTSERRAT_20
    { "MONTSERRAT_20", &lv_font_montserrat_20 },
#endif
#if LV_FONT_MONTSERRAT_22
    { "MONTSERRAT_22", &lv_font_montserrat_22 },
#endif
#if LV_FONT_MONTSERRAT_24
    { "MONTSERRAT_24", &lv_font_montserrat_24 },
#endif
#if LV_FONT_MONTSERRAT_26
    { "MONTSERRAT_26", &lv_font_montserrat_26 },
#endif
#if LV_FONT_MONTSERRAT_28
    { "MONTSERRAT_28", &lv_font_montserrat_28 },
#endif
#if LV_FONT_MONTSERRAT_30
    { "MONTSERRAT_30", &lv_font_montserrat_30 },
#endif
#if LV_FONT_MONTSERRAT_32
    { "MONTSERRAT_32", &lv_font_montserrat_32 },
#endif
#if LV_FONT_MONTSERRAT_34
    { "MONTSERRAT_34", &lv_font_montserrat_34 },
#endif
#if LV_FONT_MONTSERRAT_36
    { "MONTSERRAT_36", &lv_font_montserrat_36 },
#endif
#if LV_FONT_MONTSERRAT_38
    { "MONTSERRAT_38", &lv_font_montserrat_38 },
#endif
#if LV_FONT_MONTSERRAT_40
    { "MONTSERRAT_40", &lv_font_montserrat_40 },
#endif
#if LV_FONT_MONTSERRAT_42
    { "MONTSERRAT_42", &lv_font_montserrat_42 },
#endif
#if LV_FONT_MONTSERRAT_44
    { "MONTSERRAT_44", &lv_font_montserrat_44 },
#endif
#if LV_FONT_MONTSERRAT_46
    { "MONTSERRAT_46", &lv_font_montserrat_46 },
#endif
#if LV_FONT_MONTSERRAT_48
    { "MONTSERRAT_48", &lv_font_montserrat_48 },
#endif
};

//
//
//

void create_screens() {
    
    eez_flow_init_fonts(fonts, sizeof(fonts) / sizeof(ext_font_desc_t));

// Set default LVGL theme
    lv_display_t *dispp = lv_display_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), false, LV_FONT_DEFAULT);
    lv_display_set_theme(dispp, theme);
    
    // Initialize screens
    eez_flow_init_screen_names(screen_names, sizeof(screen_names) / sizeof(const char *));
    eez_flow_init_object_names(object_names, sizeof(object_names) / sizeof(const char *));
    
    // Create screens
    create_screen_splash();
    create_screen_main();
    create_screen_menu();
    create_screen_assistant();
    create_screen_music();
    create_screen_smart_home();
    create_screen_games();
    create_screen_settings();
    create_screen_provision();  // o-provision page (added 2026-06-13)
    create_screen_about();
}

void ui_spotify_update_playlists(const char *json) {
    if (!objects.music_playlist_list) {
        ESP_LOGI("SPOTIFY", "playlist label is NULL");
        return;
    }
    ESP_LOGI("SPOTIFY", "update_playlists called, label=%p", objects.music_playlist_list);
    ESP_LOGI("SPOTIFY", "json raw: %.80s", json);

    char buf[512] = {0};
    const char *p = json;
    int idx = 0;

    while ((p = strstr(p, "\"name\":")) != NULL && idx < sizeof(buf) - 20) {
        p += 7;
        while (*p == ' ' || *p == ':' || *p == '"') p++;
        const char *end = strchr(p, '"');
        if (end && end - p < 30) {
            int len = end - p;
            if (idx + len + 2 < sizeof(buf)) {
                memcpy(buf + idx, p, len);
                buf[idx + len] = '\n';
                idx += len + 1;
            }
        }
        p = end;
    }

    if (lvgl_port_lock(0)) {
        lv_label_set_text(objects.music_playlist_list, buf[0] ? buf : "No playlists");
        lvgl_port_unlock();
    }
}

void ui_spotify_update_status(const char *json) {
    ESP_LOGI("SPOTIFY", "update_status called, label=%p", objects.music_now_playing_label);
    ESP_LOGI("SPOTIFY", "json raw: %.80s", json);
    if (!objects.music_now_playing_label) return;

    char buf[256] = "No track";
    const char *title = NULL, *artist = NULL;

    const char *tp = strstr(json, "\"title\":");
    if (tp) {
        tp += 8;
        while (*tp == ' ' || *tp == ':' || *tp == '"') tp++;
        const char *te = strchr(tp, '"');
        if (te && te - tp < 64) {
            static char title_buf[64];
            int len = te - tp < sizeof(title_buf) - 1 ? te - tp : sizeof(title_buf) - 1;
            memcpy(title_buf, tp, len);
            title_buf[len] = '\0';
            title = title_buf;
        }
    }

    const char *ap = strstr(json, "\"artist\":");
    if (ap) {
        ap += 9;
        while (*ap == ' ' || *ap == ':' || *ap == '"') ap++;
        const char *ae = strchr(ap, '"');
        if (ae && ae - ap < 64) {
            static char artist_buf[64];
            int len = ae - ap < sizeof(artist_buf) - 1 ? ae - ap : sizeof(artist_buf) - 1;
            memcpy(artist_buf, ap, len);
            artist_buf[len] = '\0';
            artist = artist_buf;
        }
    }

    if (title && strlen(title) > 0) {
        if (artist && strlen(artist) > 0) {
            snprintf(buf, sizeof(buf), "%s - %s", title, artist);
        } else {
            snprintf(buf, sizeof(buf), "%s", title);
        }
    }

    if (lvgl_port_lock(0)) {
        lv_label_set_text(objects.music_now_playing_label, buf);
        lvgl_port_unlock();
    }
}
/* ----------------------------------------------------------------------
 * o-provision screen — minimal LVGL implementation.
 * Added on top of the original UI; mirrors the EEZ export layout but
 * without dragging in eez-flow state. The page is reachable via
 * settings page's "Provision" button.
 * --------------------------------------------------------------------- */
void create_screen_provision() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.provision = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 240, 320);
    lv_obj_set_style_bg_opa(obj, 0, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x020202), LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Blue header bar */
    lv_obj_t *header = lv_obj_create(obj);
    objects.prov_header = header;
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, 240, 34);
    lv_obj_set_style_pad_left(header, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(header, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(header, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(header, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(header, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x1270dd), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(header, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* "Provision" title */
    lv_obj_t *title = lv_label_create(header);
    objects.prov_cap = title;
    lv_obj_set_pos(title, 64, 7);
    lv_obj_set_size(title, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text_static(title, "Provision");

    /* Blue footer bar */
    lv_obj_t *footer = lv_obj_create(obj);
    objects.prov_footer = footer;
    lv_obj_set_pos(footer, 0, 268);
    lv_obj_set_size(footer, 240, 52);
    lv_obj_set_style_pad_left(footer, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(footer, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(footer, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(footer, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(footer, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(footer, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(footer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(footer, lv_color_hex(0x1270dd), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(footer, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Author / signature */
    lv_obj_t *sig = lv_label_create(footer);
    objects.author_8 = sig;
    lv_obj_set_pos(sig, 83, 15);
    lv_obj_set_size(sig, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(sig, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(sig, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(sig, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text_static(sig, "Serhat SADAY");
}

void tick_screen_provision() {
    /* No flow-state animations yet. */
}
