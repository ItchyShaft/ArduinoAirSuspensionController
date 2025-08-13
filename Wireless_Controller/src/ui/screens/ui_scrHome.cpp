#include "ui_scrHome.h"
#include "ui/ui.h" // sketchy backwards import may break in the future

#include <Arduino.h>
extern "C" {
  #include "utils/BAT_Driver.h"
}

// -------- Battery widget config --------
#define VBAT_MIN_V             3.30f
#define VBAT_MAX_V_CHG         4.15f   // while charging (on USB)
#define VBAT_MAX_V_RELAX       4.00f   // after unplug (resting full)
#define BATTERY_UPDATE_MS      1000
#define BATTERY_EMA_ALPHA      0.15f

// --- software-only charging inference (no STAT pin) ---
#define STEP_WINDOW_MS     4000   // look ~4s back
#define STEP_PLUG_MV       35     // > +35 mV within window → assume plug/charging
#define STEP_UNPLUG_MV     35     // < -35 mV within window → assume unplug
#define CHG_LOCK_MS       90000   // hold inferred state for 90s to ignore relaxation

static bool     chg_flag = false;         // inferred charging state (trend)
static uint32_t chg_lock_until = 0;

// small history buffer for voltage trend detection
static const int HIST_N = 8;
static float     v_hist[HIST_N];
static uint32_t  t_hist[HIST_N];
static int       hist_head = 0, hist_count = 0;

static inline void hist_push(float v, uint32_t t) {
  v_hist[hist_head] = v;
  t_hist[hist_head] = t;
  hist_head = (hist_head + 1) % HIST_N;
  if (hist_count < HIST_N) hist_count++;
}

static inline bool charging_by_trend(float v_now, uint32_t now_ms) {
  if (hist_count == 0) return chg_flag;

  // find a sample ~STEP_WINDOW_MS ago (or the oldest we have)
  int idx = hist_head;
  for (int i = 0; i < hist_count; ++i) {
    idx = (idx - 1 + HIST_N) % HIST_N;
    if ((now_ms - t_hist[idx]) >= STEP_WINDOW_MS) break;
  }
  float dv_mV = (v_now - v_hist[idx]) * 1000.0f;  // <-- fixed stray char

  // if we're inside the lock window, keep current inference
  if (now_ms < chg_lock_until) return chg_flag;

  if (dv_mV > STEP_PLUG_MV) {
    chg_flag = true;
    chg_lock_until = now_ms + CHG_LOCK_MS;
  } else if (dv_mV < -STEP_UNPLUG_MV) {
    chg_flag = false;
    chg_lock_until = now_ms + CHG_LOCK_MS;
  }
  return chg_flag;
}



// Display smoothing
static int shown_pct = -1;
#define PCT_RISE_MAX_PER_UPDATE  3     // allow faster increases
#define PCT_DROP_MAX_PER_UPDATE  1     // normal drop limit (per update)

// Extra protection against the post-USB “cliff”
#define CHARGE_RELAX_GRACE_MS  60000   // freeze % for 60s after unplug
#define DROP_STEP_MS           5000    // after grace: at most 1% drop per 5s

// UI objects
static lv_obj_t* ui_batt_cont = nullptr;
static lv_obj_t* ui_batt_fill = nullptr;
static lv_obj_t* ui_batt_head = nullptr;
static lv_obj_t* ui_batt_label = nullptr;

static uint32_t batt_next_ms = 0;
static float vbat_ema = -1.0f;

// charging state tracking
static bool     was_charging = false;
static uint32_t relax_until  = 0;      // time until which we “hold” the % after unplug
static uint32_t last_down_ms = 0;      // last time we allowed a 1% down step





static void battery_widget_create(lv_obj_t* parent)
{
  // --- tuning knobs ---
  const int W       = 30, H = 14;   // frame size
  const int BORDER  = 2;            // frame border px
  const int PAD     = 0;            // gap between frame & fill
  const int R_FRAME = 2;            // frame outer radius
  const int HEAD_W  = 3;            // battery head width
  const int HEAD_H  = H - 6;        // battery head height
  const int MARGIN  = 6;            // distance from right edge

  // Top-right position (room for head)
  const int box_x = DISPLAY_WIDTH - (W + HEAD_W) - MARGIN;
  const int box_y = 4;

  // Frame
  ui_batt_cont = lv_obj_create(parent);
  lv_obj_remove_style_all(ui_batt_cont);
  lv_obj_set_size(ui_batt_cont, W, H);
  lv_obj_set_pos(ui_batt_cont, box_x, box_y);
  lv_obj_set_style_bg_opa(ui_batt_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ui_batt_cont, BORDER, 0);
  lv_obj_set_style_border_color(ui_batt_cont, lv_color_hex(0xC0C0C0), 0);
  lv_obj_set_style_border_opa(ui_batt_cont, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(ui_batt_cont, R_FRAME, 0);
  lv_obj_set_style_pad_all(ui_batt_cont, PAD, 0);  // controls the visual gap

  // Fill (child → lives in padded content box)
  ui_batt_fill = lv_obj_create(ui_batt_cont);
  lv_obj_remove_style_all(ui_batt_fill);
  lv_obj_set_style_bg_color(ui_batt_fill, lv_color_hex(0x55D16E), 0);
  lv_obj_set_style_bg_opa(ui_batt_fill, LV_OPA_COVER, 0);
  int R_fill = R_FRAME - BORDER - PAD;             // match inner radius
  if (R_fill < 0) R_fill = 0;
  lv_obj_set_style_radius(ui_batt_fill, R_fill, 0);
  lv_obj_set_height(ui_batt_fill, LV_PCT(100));    // full inner height
  lv_obj_align(ui_batt_fill, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_width(ui_batt_fill, 1);               // small nonzero start

  // Battery head
  ui_batt_head = lv_obj_create(parent);
  lv_obj_remove_style_all(ui_batt_head);
  lv_obj_set_size(ui_batt_head, HEAD_W, HEAD_H);
  lv_obj_set_style_bg_color(ui_batt_head, lv_color_hex(0xC0C0C0), 0);
  lv_obj_set_style_bg_opa(ui_batt_head, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(ui_batt_head, 1, 0);
  lv_obj_set_pos(ui_batt_head, box_x + W, box_y + (H - HEAD_H) / 2);

  // % label
  ui_batt_label = lv_label_create(parent);
  lv_label_set_text(ui_batt_label, "--%");
  lv_obj_set_style_text_color(ui_batt_label, lv_color_hex(0xC0C0C0), 0);
  lv_obj_align(ui_batt_label, LV_ALIGN_TOP_RIGHT, -MARGIN, box_y + H + 2);
}

static void battery_widget_update()
{
  const uint32_t now = millis();
  if (now < batt_next_ms) return;
  batt_next_ms = now + BATTERY_UPDATE_MS;

  // 1) Read + smooth voltage
  const float v = BAT_Get_Volts();
  if (v <= 0.05f) return;

  if (vbat_ema < 0) vbat_ema = v;
  vbat_ema += BATTERY_EMA_ALPHA * (v - vbat_ema);

  // keep history for trend detection
  hist_push(vbat_ema, now);

  // infer charging from voltage trend
  const bool charging = charging_by_trend(vbat_ema, now);

  // >>> start a grace window when we go from charging -> not charging
  if (was_charging && !charging) {
    relax_until  = now + CHARGE_RELAX_GRACE_MS;  // freeze % for a bit
    last_down_ms = now;                           // reset drop timer
  }
  was_charging = charging;

  const bool in_relax = (now < relax_until);

  // 4) Pick the “full” voltage for mapping
  const float v_full = (charging || in_relax) ? VBAT_MAX_V_CHG : VBAT_MAX_V_RELAX;

  // 5) Compute target % and rate-limit the displayed %
  int target = (int)((vbat_ema - VBAT_MIN_V) * 100.0f / (v_full - VBAT_MIN_V) + 0.5f);
  if (target < 0) target = 0;
  if (target > 100) target = 100;

  if (shown_pct < 0) shown_pct = target;

  int delta = target - shown_pct;
  if (delta > 0) {
    if (delta > PCT_RISE_MAX_PER_UPDATE) delta = PCT_RISE_MAX_PER_UPDATE;
    shown_pct += delta;
  } else if (delta < 0) {
    if (in_relax) {
      // freeze during grace
    } else if (now - last_down_ms >= DROP_STEP_MS) {
      if (delta < -PCT_DROP_MAX_PER_UPDATE) delta = -PCT_DROP_MAX_PER_UPDATE;
      shown_pct += delta;
      last_down_ms = now;
    }
  }

  // 6) Resize fill inside the content box
  const int inner_w = lv_obj_get_content_width(ui_batt_cont);
  const int inner_h = lv_obj_get_content_height(ui_batt_cont);
  int fill_w = (inner_w * shown_pct) / 100;
  if (shown_pct > 0 && fill_w == 0) fill_w = 1;

  lv_obj_set_size(ui_batt_fill, fill_w, inner_h);
  lv_obj_align(ui_batt_fill, LV_ALIGN_LEFT_MID, 0, 0);

  // 7) Color + label
  const lv_color_t c = lv_color_hex(shown_pct > 50 ? 0x55D16E : (shown_pct > 20 ? 0xE7C04A : 0xE06C75));
  lv_obj_set_style_bg_color(ui_batt_fill, c, 0);

  static char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", shown_pct);
  lv_label_set_text(ui_batt_label, buf);
}


LV_IMG_DECLARE(navbar_home);

ScrHome scrHome(navbar_home, true);

void draw_arrow(lv_obj_t *parent, CenterRect cr, int direction)
{
    lv_point_precise_t *line_points = new lv_point_precise_t[3];
    line_points[0].x = -7 + cr.cx;
    line_points[0].y = -3 * direction + cr.cy;
    line_points[1].x = 0 + cr.cx;
    line_points[1].y = 4 * direction + cr.cy;
    line_points[2].x = 7 + cr.cx;
    line_points[2].y = -3 * direction + cr.cy;

    /*Create style*/
    static lv_style_t style_line;
    lv_style_init(&style_line);
    lv_style_set_line_width(&style_line, 2);
    lv_style_set_line_color(&style_line, lv_color_hex(THEME_COLOR_LIGHT));
    lv_style_set_line_rounded(&style_line, true);

    /*Create a line and apply the new style*/
    lv_obj_t *line1;
    line1 = lv_line_create(parent);
    lv_line_set_points(line1, line_points, 3); /*Set the points*/
    lv_obj_add_style(line1, &style_line, 0);
}

void drawCircle(lv_obj_t *parent, CenterRect cr, int direction)
{
    lv_obj_t *my_Cir = lv_obj_create(parent);
    lv_obj_set_scrollbar_mode(my_Cir, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(my_Cir, cr.w, cr.h);
    lv_obj_set_pos(my_Cir, cr.cx - cr.w / 2, cr.cy - cr.h / 2);
    lv_obj_set_style_bg_color(my_Cir, lv_color_hex(GENERIC_GREY_VERY_DARK), 0);
    // lv_obj_set_style_border_color(my_Cir, lv_color_hex(THEME_COLOR_LIGHT), 0);
    lv_obj_set_style_radius(my_Cir, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(my_Cir, 0, 0);

    draw_arrow(parent, cr, direction);
}

void drawRect(lv_obj_t *parent, SimpleRect sr)
{
    lv_obj_t *my_Cir = lv_obj_create(parent);
    lv_obj_set_scrollbar_mode(my_Cir, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(my_Cir, sr.w, sr.h);
    lv_obj_set_pos(my_Cir, sr.x, sr.y);
    lv_obj_set_style_bg_color(my_Cir, lv_color_hex(GENERIC_GREY_VERY_DARK), 0);
    lv_obj_set_style_radius(my_Cir, 0, 0);
    lv_obj_set_style_border_width(my_Cir, 0, 0);
}

void drawPill(lv_obj_t *parent, CenterRect up, CenterRect down)
{
    SimpleRect sr = {up.cx - up.w / 2, up.cy, up.w, down.cy - up.cy};
    drawRect(parent, sr);
    drawCircle(parent, up, -1);
    drawCircle(parent, down, 1);

    lv_point_precise_t *line_points = new lv_point_precise_t[3];
    line_points[0].x = up.cx - up.w / 3.5;
    line_points[0].y = (up.cy + down.cy) / 2;
    line_points[1].x = up.cx + up.w / 3.5;
    line_points[1].y = line_points[0].y;

    /*Create style*/
    static lv_style_t style_line;
    lv_style_init(&style_line);
    lv_style_set_line_width(&style_line, 1);
    lv_style_set_line_color(&style_line, lv_color_hex(THEME_COLOR_DARK));
    lv_style_set_line_rounded(&style_line, true);

    /*Create a line and apply the new style*/
    lv_obj_t *line1;
    line1 = lv_line_create(parent);
    lv_line_set_points(line1, line_points, 2); /*Set the points*/
    lv_obj_add_style(line1, &style_line, 0);
}

void ScrHome::init(void)
{
    Scr::init();

    drawPill(this->scr, ctr_row0col0up, ctr_row0col0down);
    drawPill(this->scr, ctr_row1col0up, ctr_row1col0down);

    drawPill(this->scr, ctr_row0col1up, ctr_row0col1down);
    drawPill(this->scr, ctr_row1col1up, ctr_row1col1down);

    drawPill(this->scr, ctr_row0col2up, ctr_row0col2down);
    drawPill(this->scr, ctr_row1col2up, ctr_row1col2down);

    lv_obj_move_foreground(this->icon_navbar);                  // bring navbar to foreground
    lv_obj_move_foreground(this->ui_lblPressureFrontPassenger); // pressures to foreground front
    lv_obj_move_foreground(this->ui_lblPressureRearPassenger);  // pressures to foreground front
    lv_obj_move_foreground(this->ui_lblPressureFrontDriver);    // pressures to foreground front
    lv_obj_move_foreground(this->ui_lblPressureRearDriver);     // pressures to foreground front
    lv_obj_move_foreground(this->ui_lblPressureTank);           // pressures to foreground front

    battery_widget_create(this->scr);

}

// down = true when just pressed, false when just released
void ScrHome::runTouchInput(SimplePoint pos, bool down)
{
    Scr::runTouchInput(pos, down);

    // AirupPacket aup;
    // sendRestPacket(&aup);

    if (down == false)
    {
        closeValves();
    }
    else
    {
        // driver side
        bool _FRONT_DRIVER_IN = cr_contains(ctr_row0col0up, pos);
        bool _FRONT_DRIVER_OUT = cr_contains(ctr_row0col0down, pos);

        bool _REAR_DRIVER_IN = cr_contains(ctr_row1col0up, pos);
        bool _REAR_DRIVER_OUT = cr_contains(ctr_row1col0down, pos);

        // passenger side
        bool _FRONT_PASSENGER_IN = cr_contains(ctr_row0col2up, pos);
        bool _FRONT_PASSENGER_OUT = cr_contains(ctr_row0col2down, pos);

        bool _REAR_PASSENGER_IN = cr_contains(ctr_row1col2up, pos);
        bool _REAR_PASSENGER_OUT = cr_contains(ctr_row1col2down, pos);

        // axles
        bool _FRONT_AXLE_IN = cr_contains(ctr_row0col1up, pos);
        bool _FRONT_AXLE_OUT = cr_contains(ctr_row0col1down, pos);

        bool _REAR_AXLE_IN = cr_contains(ctr_row1col1up, pos);
        bool _REAR_AXLE_OUT = cr_contains(ctr_row1col1down, pos);

        // driver side
        if (_FRONT_DRIVER_IN)
        {
            setValveBit(FRONT_DRIVER_IN);
        }

        if (_FRONT_DRIVER_OUT)
        {
            setValveBit(FRONT_DRIVER_OUT);
        }

        if (_REAR_DRIVER_IN)
        {
            setValveBit(REAR_DRIVER_IN);
        }

        if (_REAR_DRIVER_OUT)
        {
            setValveBit(REAR_DRIVER_OUT);
        }

        // passenger side
        if (_FRONT_PASSENGER_IN)
        {
            setValveBit(FRONT_PASSENGER_IN);
        }

        if (_FRONT_PASSENGER_OUT)
        {
            setValveBit(FRONT_PASSENGER_OUT);
        }

        if (_REAR_PASSENGER_IN)
        {
            setValveBit(REAR_PASSENGER_IN);
        }

        if (_REAR_PASSENGER_OUT)
        {
            setValveBit(REAR_PASSENGER_OUT);
        }

        // axles
        if (_FRONT_AXLE_IN)
        {
            setValveBit(FRONT_DRIVER_IN);
            setValveBit(FRONT_PASSENGER_IN);
        }

        if (_FRONT_AXLE_OUT)
        {
            setValveBit(FRONT_DRIVER_OUT);
            setValveBit(FRONT_PASSENGER_OUT);
        }

        if (_REAR_AXLE_IN)
        {
            setValveBit(REAR_DRIVER_IN);
            setValveBit(REAR_PASSENGER_IN);
        }

        if (_REAR_AXLE_OUT)
        {
            setValveBit(REAR_DRIVER_OUT);
            setValveBit(REAR_PASSENGER_OUT);
        }
    }
}

void ScrHome::loop()
{
    Scr::loop();
    battery_widget_update();
}
