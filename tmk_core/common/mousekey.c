/*
Copyright 2011 Jun Wako <wakojun@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdint.h>
#include "keycode.h"
#include "host.h"
#include "timer.h"
#include "print.h"
#include "debug.h"
#include "mousekey.h"

inline int8_t times_inv_sqrt2(int8_t x) {
    // 181/256 is pretty close to 1/sqrt(2)
    // 0.70703125                 0.707106781
    // 1 too small for x=99 and x=198
    // This ends up being a mult and discard lower 8 bits
    return (x * 181) >> 8;
}

static report_mouse_t mouse_report = {0};
static void           mousekey_debug(void);
static uint8_t        mousekey_accel     = 0;
static uint8_t        mousekey_xy_repeat = 0;
static uint8_t        mousekey_wh_repeat = 0;
static uint16_t       last_timer_c       = 0;
static uint16_t       last_timer_w       = 0;

#ifndef MK_3_SPEED

/*
 * Mouse keys  acceleration algorithm
 *  http://en.wikipedia.org/wiki/Mouse_keys
 *
 *  speed = delta * max_speed * (repeat / time_to_max)**((1000+curve)/1000)
 */

/* Variable             Description
 * mk_xy_delay          tens of milliseconds between the initial key press and first repeated mouse motion event (0-255)
 * mk_wh_delay          tens of milliseconds between the initial mouse wheel event and first repeated mouse wheel event (0-255)
 * mk_xy_interval       milliseconds between repeated mouse motion events (0-255)
 * mk_wh_interval       milliseconds between repeated mouse wheel events (0-255)
 * mk_xy_max_speed      steady speed (in action_delta units) applied each mouse motion event (0-255)
 * mk_wh_max_speed      steady speed (in action_delta units) applied each mouse wheel event (0-255)
 * mk_xy_time_to_max    number of mouse motion events (count) accelerating to steady speed (0-255)
 * mk_wh_time_to_max    number of mouse wheel events (count) accelerating to steady speed (0-255)
 */
uint8_t mk_xy_delay       = MOUSEKEY_XY_DELAY / 10;
uint8_t mk_wh_delay       = MOUSEKEY_WH_DELAY / 10;
uint8_t mk_xy_interval    = MOUSEKEY_XY_INTERVAL;
uint8_t mk_wh_interval    = MOUSEKEY_WH_INTERVAL;
uint8_t mk_xy_max_speed   = MOUSEKEY_XY_MAX_SPEED;
uint8_t mk_xy_time_to_max = MOUSEKEY_XY_TIME_TO_MAX;
uint8_t mk_wh_max_speed   = MOUSEKEY_WH_MAX_SPEED;
uint8_t mk_wh_time_to_max = MOUSEKEY_WH_TIME_TO_MAX;

static uint8_t move_unit(void) {
    uint16_t unit;
    if (mousekey_accel & (1 << 0)) {
        unit = (MOUSEKEY_MOVE_DELTA * mk_xy_max_speed) / 4;
    } else if (mousekey_accel & (1 << 1)) {
        unit = (MOUSEKEY_MOVE_DELTA * mk_xy_max_speed) / 2;
    } else if (mousekey_accel & (1 << 2)) {
        unit = (MOUSEKEY_MOVE_DELTA * mk_xy_max_speed);
    } else if (mousekey_xy_repeat == 0) {
        unit = MOUSEKEY_MOVE_DELTA;
    } else if (mousekey_xy_repeat >= mk_xy_time_to_max) {
        unit = MOUSEKEY_MOVE_DELTA * mk_xy_max_speed;
    } else {
        unit = (MOUSEKEY_MOVE_DELTA * mk_xy_max_speed * mousekey_xy_repeat) / mk_xy_time_to_max;
    }
    return (unit > MOUSEKEY_MOVE_MAX ? MOUSEKEY_MOVE_MAX : (unit == 0 ? 1 : unit));
}

static uint8_t wheel_unit(void) {
    uint16_t unit;
    if (mousekey_accel & (1 << 0)) {
        unit = (MOUSEKEY_WHEEL_DELTA * mk_wh_max_speed) / 4;
    } else if (mousekey_accel & (1 << 1)) {
        unit = (MOUSEKEY_WHEEL_DELTA * mk_wh_max_speed) / 2;
    } else if (mousekey_accel & (1 << 2)) {
        unit = (MOUSEKEY_WHEEL_DELTA * mk_wh_max_speed);
    } else if (mousekey_wh_repeat == 0) {
        unit = MOUSEKEY_WHEEL_DELTA;
    } else if (mousekey_wh_repeat >= mk_wh_time_to_max) {
        unit = MOUSEKEY_WHEEL_DELTA * mk_wh_max_speed;
    } else {
        unit = (MOUSEKEY_WHEEL_DELTA * mk_wh_max_speed * mousekey_wh_repeat) / mk_wh_time_to_max;
    }
    return (unit > MOUSEKEY_WHEEL_MAX ? MOUSEKEY_WHEEL_MAX : (unit == 0 ? 1 : unit));
}

void mousekey_task(void) {
    bool xyevent, whevent;
    xyevent = false;
    whevent = false;

    if (timer_elapsed(last_timer_c) >= (mousekey_xy_repeat ? mk_xy_interval : mk_xy_delay * 10)) {
        if (mouse_report.x != 0 || mouse_report.y != 0) {
            xyevent = true;
            if (mousekey_xy_repeat != UINT8_MAX) mousekey_xy_repeat++;
            if (mouse_report.x > 0) mouse_report.x = move_unit();
            if (mouse_report.x < 0) mouse_report.x = move_unit() * -1;
            if (mouse_report.y > 0) mouse_report.y = move_unit();
            if (mouse_report.y < 0) mouse_report.y = move_unit() * -1;
            /* diagonal move [1/sqrt(2)] */
            if (mouse_report.x && mouse_report.y) {
                mouse_report.x = times_inv_sqrt2(mouse_report.x);
                if (mouse_report.x == 0) {
                    mouse_report.x = 1;
                }
                mouse_report.y = times_inv_sqrt2(mouse_report.y);
                if (mouse_report.y == 0) {
                    mouse_report.y = 1;
                }
            }
        }
    }

    if (timer_elapsed(last_timer_w) >= (mousekey_wh_repeat ? mk_wh_interval : mk_wh_delay * 10)) {
        if (mouse_report.v != 0 || mouse_report.h != 0) {
            whevent = true;
            if (mousekey_wh_repeat != UINT8_MAX) mousekey_wh_repeat++;
            if (mouse_report.v > 0) mouse_report.v = wheel_unit();
            if (mouse_report.v < 0) mouse_report.v = wheel_unit() * -1;
            if (mouse_report.h > 0) mouse_report.h = wheel_unit();
            if (mouse_report.h < 0) mouse_report.h = wheel_unit() * -1;
        }
    }

    if (xyevent || whevent) {
        uint16_t tim;
        mousekey_send();
        tim = timer_read();
        if (xyevent) last_timer_c = tim;
        if (whevent) last_timer_w = tim;
    }
}

void mousekey_on(uint8_t code) {
    if (code == KC_MS_UP) {
        mouse_report.y = move_unit() * -1;
        last_timer_c   = timer_read();
    } else if (code == KC_MS_DOWN) {
        mouse_report.y = move_unit();
        last_timer_c   = timer_read();
    } else if (code == KC_MS_LEFT) {
        mouse_report.x = move_unit() * -1;
        last_timer_c   = timer_read();
    } else if (code == KC_MS_RIGHT) {
        mouse_report.x = move_unit();
        last_timer_c   = timer_read();
    } else if (code == KC_MS_WH_UP) {
        mouse_report.v = wheel_unit();
        last_timer_w   = timer_read();
    } else if (code == KC_MS_WH_DOWN) {
        mouse_report.v = wheel_unit() * -1;
        last_timer_w   = timer_read();
    } else if (code == KC_MS_WH_LEFT) {
        mouse_report.h = wheel_unit() * -1;
        last_timer_w   = timer_read();
    } else if (code == KC_MS_WH_RIGHT) {
        mouse_report.h = wheel_unit();
        last_timer_w   = timer_read();
    } else if (code == KC_MS_BTN1)
        mouse_report.buttons |= MOUSE_BTN1;
    else if (code == KC_MS_BTN2)
        mouse_report.buttons |= MOUSE_BTN2;
    else if (code == KC_MS_BTN3)
        mouse_report.buttons |= MOUSE_BTN3;
    else if (code == KC_MS_BTN4)
        mouse_report.buttons |= MOUSE_BTN4;
    else if (code == KC_MS_BTN5)
        mouse_report.buttons |= MOUSE_BTN5;
    else if (code == KC_MS_ACCEL0)
        mousekey_accel |= (1 << 0);
    else if (code == KC_MS_ACCEL1)
        mousekey_accel |= (1 << 1);
    else if (code == KC_MS_ACCEL2)
        mousekey_accel |= (1 << 2);
}

void mousekey_off(uint8_t code) {
    if (code == KC_MS_UP && mouse_report.y < 0)
        mouse_report.y = 0;
    else if (code == KC_MS_DOWN && mouse_report.y > 0)
        mouse_report.y = 0;
    else if (code == KC_MS_LEFT && mouse_report.x < 0)
        mouse_report.x = 0;
    else if (code == KC_MS_RIGHT && mouse_report.x > 0)
        mouse_report.x = 0;
    else if (code == KC_MS_WH_UP && mouse_report.v > 0)
        mouse_report.v = 0;
    else if (code == KC_MS_WH_DOWN && mouse_report.v < 0)
        mouse_report.v = 0;
    else if (code == KC_MS_WH_LEFT && mouse_report.h < 0)
        mouse_report.h = 0;
    else if (code == KC_MS_WH_RIGHT && mouse_report.h > 0)
        mouse_report.h = 0;
    else if (code == KC_MS_BTN1)
        mouse_report.buttons &= ~MOUSE_BTN1;
    else if (code == KC_MS_BTN2)
        mouse_report.buttons &= ~MOUSE_BTN2;
    else if (code == KC_MS_BTN3)
        mouse_report.buttons &= ~MOUSE_BTN3;
    else if (code == KC_MS_BTN4)
        mouse_report.buttons &= ~MOUSE_BTN4;
    else if (code == KC_MS_BTN5)
        mouse_report.buttons &= ~MOUSE_BTN5;
    else if (code == KC_MS_ACCEL0)
        mousekey_accel &= ~(1 << 0);
    else if (code == KC_MS_ACCEL1)
        mousekey_accel &= ~(1 << 1);
    else if (code == KC_MS_ACCEL2)
        mousekey_accel &= ~(1 << 2);
    if (mouse_report.x == 0 && mouse_report.y == 0) {
        mousekey_xy_repeat = 0;
    }
    if (mouse_report.v == 0 && mouse_report.h == 0) {
        mousekey_wh_repeat = 0;
    }
}

#else /* #ifndef MK_3_SPEED */

enum { mkspd_unmod, mkspd_0, mkspd_1, mkspd_2, mkspd_COUNT };
#    ifndef MK_MOMENTARY_ACCEL
static uint8_t mk_speed                 = mkspd_1;
#    else
static uint8_t mk_speed      = mkspd_unmod;
static uint8_t mkspd_DEFAULT = mkspd_unmod;
#    endif
uint16_t       c_offsets[mkspd_COUNT]   = {MK_C_OFFSET_UNMOD, MK_C_OFFSET_0, MK_C_OFFSET_1, MK_C_OFFSET_2};
uint16_t       c_intervals[mkspd_COUNT] = {MK_C_INTERVAL_UNMOD, MK_C_INTERVAL_0, MK_C_INTERVAL_1, MK_C_INTERVAL_2};
uint16_t       w_offsets[mkspd_COUNT]   = {MK_W_OFFSET_UNMOD, MK_W_OFFSET_0, MK_W_OFFSET_1, MK_W_OFFSET_2};
uint16_t       w_intervals[mkspd_COUNT] = {MK_W_INTERVAL_UNMOD, MK_W_INTERVAL_0, MK_W_INTERVAL_1, MK_W_INTERVAL_2};

void mousekey_task(void) {
    // report cursor and scroll movement independently
    report_mouse_t const tmpmr = mouse_report;
    if ((mouse_report.x || mouse_report.y) && timer_elapsed(last_timer_c) > c_intervals[mk_speed]) {
        mouse_report.h = 0;
        mouse_report.v = 0;
        mousekey_send();
        last_timer_c = timer_read();
        mouse_report = tmpmr;
    }
    if ((mouse_report.h || mouse_report.v) && timer_elapsed(last_timer_w) > w_intervals[mk_speed]) {
        mouse_report.x = 0;
        mouse_report.y = 0;
        mousekey_send();
        last_timer_w = timer_read();
        mouse_report = tmpmr;
    }
}

void adjust_speed(void) {
    uint16_t const c_offset = c_offsets[mk_speed];
    uint16_t const w_offset = w_offsets[mk_speed];
    if (mouse_report.x > 0) mouse_report.x = c_offset;
    if (mouse_report.x < 0) mouse_report.x = c_offset * -1;
    if (mouse_report.y > 0) mouse_report.y = c_offset;
    if (mouse_report.y < 0) mouse_report.y = c_offset * -1;
    if (mouse_report.h > 0) mouse_report.h = w_offset;
    if (mouse_report.h < 0) mouse_report.h = w_offset * -1;
    if (mouse_report.v > 0) mouse_report.v = w_offset;
    if (mouse_report.v < 0) mouse_report.v = w_offset * -1;
    // adjust for diagonals
    if (mouse_report.x && mouse_report.y) {
        mouse_report.x = times_inv_sqrt2(mouse_report.x);
        if (mouse_report.x == 0) {
            mouse_report.x = 1;
        }
        mouse_report.y = times_inv_sqrt2(mouse_report.y);
        if (mouse_report.y == 0) {
            mouse_report.y = 1;
        }
    }
    if (mouse_report.h && mouse_report.v) {
        mouse_report.h = times_inv_sqrt2(mouse_report.h);
        mouse_report.v = times_inv_sqrt2(mouse_report.v);
    }
}

void mousekey_on(uint8_t code) {
    uint16_t const c_offset  = c_offsets[mk_speed];
    uint16_t const w_offset  = w_offsets[mk_speed];
    uint8_t const  old_speed = mk_speed;
    if (code == KC_MS_UP)
        mouse_report.y = c_offset * -1;
    else if (code == KC_MS_DOWN)
        mouse_report.y = c_offset;
    else if (code == KC_MS_LEFT)
        mouse_report.x = c_offset * -1;
    else if (code == KC_MS_RIGHT)
        mouse_report.x = c_offset;
    else if (code == KC_MS_WH_UP)
        mouse_report.v = w_offset;
    else if (code == KC_MS_WH_DOWN)
        mouse_report.v = w_offset * -1;
    else if (code == KC_MS_WH_LEFT)
        mouse_report.h = w_offset * -1;
    else if (code == KC_MS_WH_RIGHT)
        mouse_report.h = w_offset;
    else if (code == KC_MS_BTN1)
        mouse_report.buttons |= MOUSE_BTN1;
    else if (code == KC_MS_BTN2)
        mouse_report.buttons |= MOUSE_BTN2;
    else if (code == KC_MS_BTN3)
        mouse_report.buttons |= MOUSE_BTN3;
    else if (code == KC_MS_BTN4)
        mouse_report.buttons |= MOUSE_BTN4;
    else if (code == KC_MS_BTN5)
        mouse_report.buttons |= MOUSE_BTN5;
    else if (code == KC_MS_ACCEL0)
        mk_speed = mkspd_0;
    else if (code == KC_MS_ACCEL1)
        mk_speed = mkspd_1;
    else if (code == KC_MS_ACCEL2)
        mk_speed = mkspd_2;
    if (mk_speed != old_speed) adjust_speed();
}

void mousekey_off(uint8_t code) {
#    ifdef MK_MOMENTARY_ACCEL
    uint8_t const old_speed = mk_speed;
#    endif
    if (code == KC_MS_UP && mouse_report.y < 0)
        mouse_report.y = 0;
    else if (code == KC_MS_DOWN && mouse_report.y > 0)
        mouse_report.y = 0;
    else if (code == KC_MS_LEFT && mouse_report.x < 0)
        mouse_report.x = 0;
    else if (code == KC_MS_RIGHT && mouse_report.x > 0)
        mouse_report.x = 0;
    else if (code == KC_MS_WH_UP && mouse_report.v > 0)
        mouse_report.v = 0;
    else if (code == KC_MS_WH_DOWN && mouse_report.v < 0)
        mouse_report.v = 0;
    else if (code == KC_MS_WH_LEFT && mouse_report.h < 0)
        mouse_report.h = 0;
    else if (code == KC_MS_WH_RIGHT && mouse_report.h > 0)
        mouse_report.h = 0;
    else if (code == KC_MS_BTN1)
        mouse_report.buttons &= ~MOUSE_BTN1;
    else if (code == KC_MS_BTN2)
        mouse_report.buttons &= ~MOUSE_BTN2;
    else if (code == KC_MS_BTN3)
        mouse_report.buttons &= ~MOUSE_BTN3;
    else if (code == KC_MS_BTN4)
        mouse_report.buttons &= ~MOUSE_BTN4;
    else if (code == KC_MS_BTN5)
        mouse_report.buttons &= ~MOUSE_BTN5;
#    ifdef MK_MOMENTARY_ACCEL
    else if (code == KC_MS_ACCEL0)
        mk_speed = mkspd_DEFAULT;
    else if (code == KC_MS_ACCEL1)
        mk_speed = mkspd_DEFAULT;
    else if (code == KC_MS_ACCEL2)
        mk_speed = mkspd_DEFAULT;
    if (mk_speed != old_speed) adjust_speed();
#    endif
}

#endif /* #ifndef MK_3_SPEED */

void mousekey_send(void) {
    mousekey_debug();
    host_mouse_send(&mouse_report);
}

void mousekey_clear(void) {
    mouse_report       = (report_mouse_t){};
    mousekey_xy_repeat = 0;
    mousekey_wh_repeat = 0;
    mousekey_accel     = 0;
}

static void mousekey_debug(void) {
    if (!debug_mouse) return;
    print("mousekey [btn|x y v h](rep/acl): [");
    phex(mouse_report.buttons);
    print("|");
    print_decs(mouse_report.x);
    print(" ");
    print_decs(mouse_report.y);
    print(" ");
    print_decs(mouse_report.v);
    print(" ");
    print_decs(mouse_report.h);
    print("](");
    print_dec(mousekey_xy_repeat);
    print("/");
    print_dec(mousekey_wh_repeat);
    print("/");
    print_dec(mousekey_accel);
    print(")\n");
}
