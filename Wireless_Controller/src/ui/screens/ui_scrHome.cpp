#include "ui_scrHome.h"
#include "ui/ui.h" // sketchy backwards import may break in the future

LV_IMG_DECLARE(navbar_home);

ScrHome scrHome(navbar_home, true);

//–– shared style for notifications
static lv_style_t style_notify;
static void init_notify_style() {
    lv_style_init(&style_notify);
    lv_style_set_bg_color(&style_notify, lv_color_hex(0x333333));
    lv_style_set_text_color(&style_notify, lv_color_hex(0xFFFFFF));
    lv_style_set_text_font(&style_notify, &lv_font_montserrat_20);
    lv_style_set_radius(&style_notify, 10);
    lv_style_set_pad_all(&style_notify, 20);
}

//–– for front-axle double-tap detection
static uint32_t last_axle_tap_time = 0;
static constexpr uint32_t DBL_CLICK_TIME_MS = 400;
static lv_obj_t* notify_label = nullptr;

void ScrHome::fillAllCorners() {
    setValveBit(FRONT_DRIVER_IN);
    setValveBit(REAR_DRIVER_IN);
    setValveBit(FRONT_PASSENGER_IN);
    setValveBit(REAR_PASSENGER_IN);
}

void ScrHome::emptyAllCorners() {
    setValveBit(FRONT_DRIVER_OUT);
    setValveBit(REAR_DRIVER_OUT);
    setValveBit(FRONT_PASSENGER_OUT);
    setValveBit(REAR_PASSENGER_OUT);
}

//–– arrow, circle, rect, pill drawing funcs
void draw_arrow(lv_obj_t *parent, CenterRect cr, int direction) {
    int x0 = cr.cx - 7;
    int y0 = cr.cy - 3 * direction;
    int x1 = cr.cx;
    int y1 = cr.cy + 4 * direction;
    int x2 = cr.cx + 7;
    int y2 = cr.cy - 3 * direction;

    lv_point_precise_t pts[3] = {
        { x0, y0 },
        { x1, y1 },
        { x2, y2 }
    };

    static lv_style_t style_line;
    lv_style_init(&style_line);
    lv_style_set_line_width(&style_line, 2);
    lv_style_set_line_color(&style_line, lv_color_hex(THEME_COLOR_LIGHT));
    lv_style_set_line_rounded(&style_line, true);

    lv_obj_t *line1 = lv_line_create(parent);
    lv_line_set_points(line1, pts, 3);
    lv_obj_add_style(line1, &style_line, 0);
}

void drawCircle(lv_obj_t *parent, CenterRect cr, int direction) {
    lv_obj_t *cir = lv_obj_create(parent);
    lv_obj_set_scrollbar_mode(cir, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(cir, cr.w, cr.h);
    lv_obj_set_pos(cir, cr.cx - cr.w/2, cr.cy - cr.h/2);
    lv_obj_add_style(cir, &style_notify, 0); // reuse darker bg
    draw_arrow(parent, cr, direction);
}

void drawRect(lv_obj_t *parent, SimpleRect sr) {
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_set_scrollbar_mode(r, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(r, sr.w, sr.h);
    lv_obj_set_pos(r, sr.x, sr.y);
    lv_obj_add_style(r, &style_notify, 0);
}

void drawPill(lv_obj_t *parent, CenterRect up, CenterRect down) {
    // draw background pill shape
    SimpleRect sr = { up.cx - up.w/2, up.cy, up.w, down.cy - up.cy };
    drawRect(parent, sr);

    // draw circles with arrows
    drawCircle(parent, up, -1);
    drawCircle(parent, down, 1);

    // draw middle bar
    int midY = (up.cy + down.cy) / 2;
    int offset = up.w / 3;
    lv_point_precise_t barPts[2] = {
        { up.cx - offset, midY },
        { up.cx + offset, midY }
    };

    static lv_style_t style_bar;
    lv_style_init(&style_bar);
    lv_style_set_line_width(&style_bar, 1);
    lv_style_set_line_color(&style_bar, lv_color_hex(THEME_COLOR_DARK));
    lv_style_set_line_rounded(&style_bar, true);

    lv_obj_t* barLine = lv_line_create(parent);
    lv_line_set_points(barLine, barPts, 2);
    lv_obj_add_style(barLine, &style_bar, 0);
}

void ScrHome::init(void) {
    Scr::init();
    init_notify_style();
    drawPill(this->scr, ctr_row0col0up, ctr_row0col0down);
    drawPill(this->scr, ctr_row1col0up, ctr_row1col0down);
    drawPill(this->scr, ctr_row0col1up, ctr_row0col1down);
    drawPill(this->scr, ctr_row1col1up, ctr_row1col1down);
    drawPill(this->scr, ctr_row0col2up, ctr_row0col2down);
    drawPill(this->scr, ctr_row1col2up, ctr_row1col2down);
    lv_obj_move_foreground(this->icon_navbar);
    lv_obj_move_foreground(this->ui_lblPressureFrontPassenger);
    lv_obj_move_foreground(this->ui_lblPressureRearPassenger);
    lv_obj_move_foreground(this->ui_lblPressureFrontDriver);
    lv_obj_move_foreground(this->ui_lblPressureRearDriver);
    lv_obj_move_foreground(this->ui_lblPressureTank);
}

void ScrHome::runTouchInput(SimplePoint pos, bool down) {
    Scr::runTouchInput(pos, down);
    // on release clear notif
    if (!down) {
        closeValves();
        if (notify_label) { lv_obj_del(notify_label); notify_label = nullptr; }
        return;
    }

    // front-axle double-tap
    bool FA_IN  = cr_contains(ctr_row0col1up,   pos);
    bool FA_OUT = cr_contains(ctr_row0col1down, pos);
    if (FA_IN || FA_OUT) {
        uint32_t now = lv_tick_get();
        bool isUp = FA_IN;
        if (now - last_axle_tap_time < DBL_CLICK_TIME_MS) {
            if (isUp) fillAllCorners(); else emptyAllCorners();
            // show persistent notification
            notify_label = lv_label_create(this->scr);
            lv_label_set_text(notify_label, isUp ? "ALL UP" : "ALL DOWN");
            lv_obj_add_style(notify_label, &style_notify, 0);
            lv_obj_align(notify_label, LV_ALIGN_CENTER, 0, 0);
        } else {
            if (isUp) { setValveBit(FRONT_DRIVER_IN); setValveBit(FRONT_PASSENGER_IN); }
            else      { setValveBit(FRONT_DRIVER_OUT); setValveBit(FRONT_PASSENGER_OUT); }
        }
        last_axle_tap_time = now;
        return;
    }

    // rear-axle
    if (cr_contains(ctr_row1col1up,   pos)) { setValveBit(REAR_DRIVER_IN); setValveBit(REAR_PASSENGER_IN); }
    if (cr_contains(ctr_row1col1down, pos)) { setValveBit(REAR_DRIVER_OUT); setValveBit(REAR_PASSENGER_OUT); }

    // other buttons
    if (cr_contains(ctr_row0col0up,   pos)) setValveBit(FRONT_DRIVER_IN);
    if (cr_contains(ctr_row0col0down, pos)) setValveBit(FRONT_DRIVER_OUT);
    if (cr_contains(ctr_row1col0up,   pos)) setValveBit(REAR_DRIVER_IN);
    if (cr_contains(ctr_row1col0down, pos)) setValveBit(REAR_DRIVER_OUT);
    if (cr_contains(ctr_row0col2up,   pos)) setValveBit(FRONT_PASSENGER_IN);
    if (cr_contains(ctr_row0col2down, pos)) setValveBit(FRONT_PASSENGER_OUT);
    if (cr_contains(ctr_row1col2up,   pos)) setValveBit(REAR_PASSENGER_IN);
    if (cr_contains(ctr_row1col2down, pos)) setValveBit(REAR_PASSENGER_OUT);
}

void ScrHome::loop() {
    Scr::loop();
}