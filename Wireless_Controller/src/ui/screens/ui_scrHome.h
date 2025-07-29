#ifndef ui_scrHome_h
#define ui_scrHome_h

#include <user_defines.h>
#include "ui/components/Scr.h"
#include "utils/util.h"

class ScrHome : public Scr
{
    using Scr::Scr;

public:
    void init() override;
    void runTouchInput(SimplePoint pos, bool down) override;
    void loop() override;
    lv_obj_t *icon_home_bg;

    // Group action helpers for double‑tap
    void fillAllCorners();
    void emptyAllCorners();

private:
    // State for double‑tap detection
    uint32_t last_tap_time    = 0;
    uint8_t  last_tap_region  = 0xFF;
    bool     last_tap_up      = false;  // true=up‑arrow, false=down‑arrow
};

extern ScrHome scrHome;

#endif // ui_scrHome_h
