#include "ui/ui.h"
#include <lvgl.h>

// tune to taste
static const uint16_t SWIPE_MIN_PX   = 30;   // min horizontal travel
static const uint16_t SWIPE_MAX_OFFY = 40;   // reject if vertical drift too big
static const uint16_t SWIPE_MAX_MS   = 700;  // must finish within this time

static lv_obj_t* gesture_area = nullptr;
static lv_point_t start_pt;
static uint32_t   start_ms;

static SCREEN next_screen(SCREEN s){
    switch(s){
        case SCREEN_HOME:     return SCREEN_PRESETS;
        case SCREEN_PRESETS:  return SCREEN_SETTINGS;
        case SCREEN_SETTINGS: return SCREEN_HOME;
        default:              return SCREEN_HOME;
    }
}
static SCREEN prev_screen(SCREEN s){
    switch(s){
        case SCREEN_HOME:     return SCREEN_SETTINGS;
        case SCREEN_SETTINGS: return SCREEN_PRESETS;
        case SCREEN_PRESETS:  return SCREEN_HOME;
        default:              return SCREEN_HOME;
    }
}

static void gesture_cb(lv_event_t* e){
    lv_indev_t* indev = lv_event_get_indev(e);
    if(!indev) return;

    if(lv_event_get_code(e) == LV_EVENT_PRESSED){
        lv_indev_get_point(indev, &start_pt);
        start_ms = lv_tick_get();
        return;
    }

    if(lv_event_get_code(e) == LV_EVENT_RELEASED){
        lv_point_t end_pt; lv_indev_get_point(indev, &end_pt);
        int16_t dx = end_pt.x - start_pt.x;
        int16_t dy = end_pt.y - start_pt.y;
        uint32_t dt = lv_tick_elaps(start_ms);

        if(dt <= SWIPE_MAX_MS && (int)LV_ABS(dy) <= SWIPE_MAX_OFFY){
            if(dx <= -(int)SWIPE_MIN_PX){
                changeScreen(next_screen(currentScreen));   // swipe left
            }else if(dx >= (int)SWIPE_MIN_PX){
                changeScreen(prev_screen(currentScreen));   // swipe right
            }
        }
    }
}

void gestures_init(){
    // full-screen transparent catcher on TOP layer so it persists across screens
    lv_obj_t* parent = lv_layer_top();
    gesture_area = lv_obj_create(parent);
    lv_obj_remove_style_all(gesture_area);
    lv_obj_set_size(gesture_area, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(gesture_area, LV_OBJ_FLAG_CLICKABLE);      // required to receive press/release
    lv_obj_add_event_cb(gesture_area, gesture_cb, LV_EVENT_PRESSED,  nullptr);
    lv_obj_add_event_cb(gesture_area, gesture_cb, LV_EVENT_RELEASED, nullptr);
}
