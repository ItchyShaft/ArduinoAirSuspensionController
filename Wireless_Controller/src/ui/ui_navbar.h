#pragma once
#include "lvgl.h"

class NavBar {
public:
    enum Tab { HOME = 0, PRESETS, SETTINGS };

    /**
     * @brief Construct a new NavBar
     * @param parent The parent LVGL object (typically the screen container)
     * @param active The initially active tab
     * @param on_tab_pressed Callback for tab press events
     */
    NavBar(lv_obj_t *parent, Tab active, lv_event_cb_t on_tab_pressed) {
        // Container for the navbar
        cont = lv_obj_create(parent);
        lv_obj_set_size(cont, LV_PCT(100), 40);
        lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_pad_all(cont, 0, 0);
        lv_obj_set_style_bg_color(cont, lv_color_hex(0x1a1a1a), 0);
        lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

        // Initialize shared label style only once
        static lv_style_t style_lbl;
        static bool style_lbl_inited = false;
        if (!style_lbl_inited) {
            lv_style_init(&style_lbl);
            lv_style_set_text_font(&style_lbl, &lv_font_montserrat_14);
            lv_style_set_text_color(&style_lbl, lv_color_white());
            style_lbl_inited = true;
        }

        // Create tabs
        const char *names[3] = { "Home", "Presets", "Settings" };
        for (int i = 0; i < 3; i++) {
            lbl[i] = lv_label_create(cont);
            lv_label_set_text(lbl[i], names[i]);
            lv_obj_add_style(lbl[i], &style_lbl, 0);
            lv_obj_align(lbl[i], LV_ALIGN_LEFT_MID, 20 + i * 80, 0);
            lv_obj_add_event_cb(lbl[i], on_tab_pressed, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        }

        // Indicator bar
        indicator = lv_obj_create(cont);
        lv_obj_set_size(indicator, 60, 2);
        lv_obj_set_style_bg_color(indicator, lv_color_hex(0xca8cff), 0);
        lv_obj_clear_flag(indicator, LV_OBJ_FLAG_CLICKABLE);

        // Set the initially active tab
        set_active(active);
    }

    /**
     * @brief Highlight the selected tab
     */
    void set_active(Tab t) {
        active = t;
        // Resize and position indicator
        lv_coord_t w = lv_obj_get_width(lbl[t]);
        lv_obj_set_width(indicator, w);
        lv_obj_align_to(indicator, lbl[t], LV_ALIGN_OUT_BOTTOM_MID, 0, 4);

        // Tint text for active/inactive
        for (int i = 0; i < 3; i++) {
            lv_obj_set_style_text_color(lbl[i],
                i == t ? lv_color_hex(0xca8cff) : lv_color_white(), 0);
        }
    }

    Tab get_active() const { return active; }

    /**
     * @brief Get the root LVGL object for the navbar
     */
    lv_obj_t *getLvObj() const { return cont; }

private:
    lv_obj_t *cont;
    lv_obj_t *lbl[3];
    lv_obj_t *indicator;
    Tab       active;
};
