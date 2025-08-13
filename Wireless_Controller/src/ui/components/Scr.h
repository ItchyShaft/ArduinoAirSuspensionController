#ifndef ui_scr_h
#define ui_scr_h

#include <Arduino.h>
#include "lvgl.h"
#include "../ui_helpers.h"
#include "../ui_events.h"

#include "utils/util.h"
#include "utils/touch_lib.h"

#include "alert.h"

// Forward decls
class Alert;
struct SimplePoint;

class Scr
{
public:
    // Construction
    Scr(lv_image_dsc_t navbarImage, bool showPressures);

    // Lifecycle / behavior
    virtual void init();
    virtual void loop();
    virtual void runTouchInput(SimplePoint pos, bool down);

    // UI helpers
    void updatePressureValues();

    // Safe, non-blocking message box API (uses LVGL msgbox)
    void showMsgBox(const char* title,
                    const char* body,
                    const char* btnLeft,
                    const char* btnRight,
                    void (*onLeft)(),
                    void (*onRight)(),
                    bool modal);
    bool isMsgBoxDisplayed() const { return msgbox_visible && msgbox; }
    void closeMsgBox();

    // Public UI elements/state shared by derived screens
    lv_image_dsc_t navbarImage;
    bool           showPressures;

    lv_obj_t* scr              = nullptr;
    lv_obj_t* rect_bg          = nullptr;
    lv_obj_t* icon_navbar      = nullptr;

    Alert*    alert            = nullptr;

    // Top-bar labels (optional based on showPressures)
    lv_obj_t* ui_lblPressureFrontDriver     = nullptr;
    lv_obj_t* ui_lblPressureRearDriver      = nullptr;
    lv_obj_t* ui_lblPressureFrontPassenger  = nullptr;
    lv_obj_t* ui_lblPressureRearPassenger   = nullptr;
    lv_obj_t* ui_lblPressureTank            = nullptr;

    int       prevPressures[5] = {0,0,0,0,0};

protected:
    // New msgbox internals (replace old mb_dialog/deleteNextFrame/etc.)
    lv_obj_t* msgbox           = nullptr;
    lv_obj_t* msgbox_backdrop  = nullptr;
    bool      msgbox_visible   = false;
};

#endif
