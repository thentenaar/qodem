/*
 * input.c
 *
 * This module is licensed under the GNU General Public License Version 2.
 * Please see the file "COPYING" in this directory for more information about
 * the GNU General Public License Version 2.
 *
 *     Copyright (C) 2015  Kevin Lamonte
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "qcurses.h"
#include "common.h"

#include <string.h>
#include <assert.h>
#ifdef Q_PDCURSES_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "qodem.h"
#include "console.h"
#include "states.h"
#include "scrollback.h"
#include "netclient.h"
#include "linux.h"
#include "input.h"

/* Set this to a not-NULL value to enable debug log. */
/* static const char *DLOGNAME = "input"; */
static const char * DLOGNAME = NULL;

/**
 * The current rendering color.
 */
attr_t q_current_color;

/**
 * How long it's been since user input came in.
 */
time_t screensaver_time;

#if defined(Q_PDCURSES) || defined(Q_PDCURSES_WIN32)

/**
 * Convert a PDCurses key to a ncurses key + flags.
 *
 * @param key the PDCurses key
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.
 */
static void pdcurses_key(int * key, int * flags) {
    DLOG(("pdcurses_key() in: key '%c' %d %x %o flags %d\n",
            *key, *key, *key, *key, *flags));

    switch (*key) {
    case ALT_0:
        *key = '0';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_1:
        *key = '1';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_2:
        *key = '2';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_3:
        *key = '3';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_4:
        *key = '4';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_5:
        *key = '5';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_6:
        *key = '6';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_7:
        *key = '7';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_8:
        *key = '8';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_9:
        *key = '9';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_A:
        *key = 'a';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_B:
        *key = 'b';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_C:
        *key = 'c';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_D:
        *key = 'd';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_E:
        *key = 'e';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_F:
        *key = 'f';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_G:
        *key = 'g';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_H:
        *key = 'h';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_I:
        *key = 'i';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_J:
        *key = 'j';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_K:
        *key = 'k';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_L:
        *key = 'l';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_M:
        *key = 'm';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_N:
        *key = 'n';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_O:
        *key = 'o';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_P:
        *key = 'p';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_Q:
        *key = 'q';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_R:
        *key = 'r';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_S:
        *key = 's';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_T:
        *key = 't';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_U:
        *key = 'u';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_V:
        *key = 'v';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_W:
        *key = 'w';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_X:
        *key = 'x';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_Y:
        *key = 'y';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_Z:
        *key = 'z';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case CTL_LEFT:
        *key = KEY_LEFT;
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        break;
    case CTL_RIGHT:
        *key = KEY_RIGHT;
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        break;
    case CTL_PGUP:
        *key = KEY_PPAGE;
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        break;
    case CTL_PGDN:
        *key = KEY_NPAGE;
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        break;
    case CTL_HOME:
        *key = KEY_HOME;
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        break;
    case CTL_END:
        *key = KEY_END;
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        break;
    case ALT_MINUS:
        *key = '-';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_EQUAL:
        *key = '=';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case CTL_UP:
        *key = KEY_UP;
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        break;
    case CTL_DOWN:
        *key = KEY_DOWN;
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        break;
    case CTL_TAB:
        *key = 0x09;
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        break;
    case ALT_PGUP:
        *key = KEY_PPAGE;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_PGDN:
        *key = KEY_NPAGE;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_HOME:
        *key = KEY_HOME;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_END:
        *key = KEY_END;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case SHF_UP:
        *key = KEY_UP;
        break;
    case ALT_UP:
        *key = KEY_UP;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case SHF_DOWN:
        *key = KEY_DOWN;
        break;
    case ALT_DOWN:
        *key = KEY_DOWN;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_LEFT:
        *key = KEY_LEFT;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_RIGHT:
        *key = KEY_RIGHT;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_TAB:
        *key = 0x09;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case SHF_IC:
        *key = KEY_IC;
        break;
    case ALT_INS:
        *key = KEY_IC;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case CTL_INS:
        *key = KEY_IC;
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        break;
    case SHF_DC:
        *key = KEY_DC;
        break;
    case ALT_DEL:
        *key = KEY_DC;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case CTL_DEL:
        *key = KEY_DC;
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        break;
    case CTL_BKSP:
        *key = KEY_BACKSPACE;
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        break;
    case ALT_BKSP:
        *key = KEY_BACKSPACE;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_ENTER:
        *key = KEY_ENTER;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_ESC:
        *key = KEY_ESCAPE;
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_BQUOTE:
        *key = '`';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_FQUOTE:
        *key = '\'';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_LBRACKET:
        *key = '[';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_RBRACKET:
        *key = ']';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_SEMICOLON:
        *key = ';';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_COMMA:
        *key = ',';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_FSLASH:
        *key = '/';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case ALT_BSLASH:
        *key = '\\';
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        break;
    case CTL_ENTER:
        *key = KEY_ENTER;
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        break;

    case PADSLASH:
        *key = Q_KEY_PAD_SLASH;
        break;
    case SHF_PADSLASH:
        *key = Q_KEY_PAD_SLASH;
        break;
    case CTL_PADSLASH:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD_SLASH;
        break;
    case ALT_PADSLASH:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD_SLASH;
        break;

    case PADENTER:
    case SHF_PADENTER:
        *key = Q_KEY_PAD_ENTER;
        break;
    case CTL_PADENTER:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD_ENTER;
        break;
    case ALT_PADENTER:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD_ENTER;
        break;

    /*
     * case SHF_PADSTOP - seems an omission in the API
     */

    case PADSTOP:
        *key = Q_KEY_PAD_STOP;
        break;
    case CTL_PADSTOP:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD_STOP;
        break;
    case ALT_PADSTOP:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD_STOP;
        break;
    case ALT_STOP:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = '.';
        break;

    case PADSTAR:
        *key = Q_KEY_PAD_STAR;
        break;
    case SHF_PADSTAR:
        *key = Q_KEY_PAD_STAR;
        break;
    case CTL_PADSTAR:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD_STAR;
        break;
    case ALT_PADSTAR:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD_STAR;
        break;

    case PADMINUS:
        *key = Q_KEY_PAD_MINUS;
        break;
    case SHF_PADMINUS:
        *key = Q_KEY_PAD_MINUS;
        break;
    case CTL_PADMINUS:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD_MINUS;
        break;
    case ALT_PADMINUS:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD_MINUS;
        break;

    case PADPLUS:
        *key = Q_KEY_PAD_PLUS;
        break;
    case SHF_PADPLUS:
        *key = Q_KEY_PAD_PLUS;
        break;
    case CTL_PADPLUS:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD_PLUS;
        break;
    case ALT_PADPLUS:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD_PLUS;
        break;

    case PAD0:
        *key = Q_KEY_PAD0;
        break;
    case CTL_PAD0:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD0;
        break;
    case ALT_PAD0:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD0;
        break;

    case CTL_PAD1:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD1;
        break;
    case ALT_PAD1:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD1;
        break;

    case CTL_PAD2:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD2;
        break;
    case ALT_PAD2:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD2;
        break;

    case CTL_PAD3:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD3;
        break;
    case ALT_PAD3:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD3;
        break;

    case CTL_PAD4:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD4;
        break;
    case ALT_PAD4:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD4;
        break;

    case CTL_PADCENTER:
        *key = Q_KEY_PAD5;
        break;
    case CTL_PAD5:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD5;
        break;
    case ALT_PAD5:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD5;
        break;

    case CTL_PAD6:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD6;
        break;
    case ALT_PAD6:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD6;
        break;

    case CTL_PAD7:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD7;
        break;
    case ALT_PAD7:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD7;
        break;

    case CTL_PAD8:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD8;
        break;
    case ALT_PAD8:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD8;
        break;

    case CTL_PAD9:
        if (flags != NULL) {
            *flags = KEY_FLAG_CTRL;
        }
        *key = Q_KEY_PAD9;
        break;
    case ALT_PAD9:
        if (flags != NULL) {
            *flags = KEY_FLAG_ALT;
        }
        *key = Q_KEY_PAD9;
        break;

    }

    DLOG(("pdcurses_key() out: key '%c' %d flags %d\n",
            *key, *key, *flags));

}

#endif /* Q_PDCURSES || Q_PDCURSES_WIN32 */

/**
 * Perform the necessary screen resizing if a KEY_RESIZE comes in.
 */
static void handle_resize() {
    int new_height, new_width;

#if defined(Q_PDCURSES) || defined(Q_PDCURSES_WIN32)
    /*
     * Update the internal window state with the new size the user selected.
     */
    resize_term(0, 0);
#endif

    /*
     * Special case: KEY_RESIZE
     */
    getmaxyx(stdscr, new_height, new_width);

    /*
     * At this point, the display is hosed.
     */

    /*
     * Increase the # of lines to match the new screen size if necessary.
     *
     * Note this check is (new_height - STATUS_HEIGHT), not
     * (new_height - STATUS_HEIGHT - 1)
     */
    if (q_status.scrollback_lines < new_height - STATUS_HEIGHT) {
        while (q_status.scrollback_lines < new_height - STATUS_HEIGHT) {
            new_scrollback_line();
        }
        q_scrollback_position = q_scrollback_last;
        q_scrollback_current = q_scrollback_last;

        /*
         * Reset scrolling regions
         */
        q_status.scroll_region_top = 0;
        q_status.scroll_region_bottom = new_height - STATUS_HEIGHT - 1;

        /*
         * q_status.cursor_x and cursor_y are almost certainly in the wrong
         * place.  Let's get them in the bottom left corner.
         */
        q_status.cursor_x = 0;
        q_status.cursor_y = new_height - STATUS_HEIGHT - 1;

    } else {
        /*
         * We have enough scrollback to cover the new size, we just need to
         * bring q_status.cursor_y down or up.
         */
        q_status.cursor_y += (new_height - HEIGHT);
        q_status.scroll_region_bottom += (new_height - HEIGHT);
    }
    WIDTH = new_width;
    HEIGHT = new_height;

    if (q_status.cursor_x > WIDTH - 1) {
        q_status.cursor_x = WIDTH - 1;
    }

    /*
     * Fix the phonebook display
     */
    phonebook_reset();

    /*
     * Pass the new dimensions to the remote side
     */
    if (q_status.online == Q_TRUE) {
        if (q_status.dial_method == Q_DIAL_METHOD_TELNET) {
            telnet_resize_screen(HEIGHT - STATUS_HEIGHT, WIDTH);
        }
        if (q_status.dial_method == Q_DIAL_METHOD_RLOGIN) {
            rlogin_resize_screen(HEIGHT - STATUS_HEIGHT, WIDTH);
        }
#ifdef Q_LIBSSH2
        if (q_status.dial_method == Q_DIAL_METHOD_SSH) {
            ssh_resize_screen(HEIGHT - STATUS_HEIGHT, WIDTH);
        }
#endif /* Q_LIBSSH2 */
    }

    q_screen_dirty = Q_TRUE;
}

/**
 * Sends mous tracking sequences to the other side if KEY_MOUSE comes in.
 */
void handle_mouse() {

    wchar_t raw_buffer[6];
    char utf8_buffer[6 * 2 + 4 + 1];
    MEVENT mouse;
    int rc;
    int i;
    /*
     * ncurses appears to be providing a PRESS followed by MOUSE_POSITION
     * rather than PRESS/RELEASE/MOUSE_POSITION.  This looks like a bug in
     * that it is forgetting button state.
     *
     * Hang onto the prior states and convert MOTION into RELEASE events as
     * needed.
     */
    static Q_BOOL mouse1 = Q_FALSE;
    static Q_BOOL mouse2 = Q_FALSE;
    static Q_BOOL mouse3 = Q_FALSE;
    static Q_BOOL mouse4 = Q_FALSE;
    static Q_BOOL mouse5 = Q_FALSE;
    static int old_x = -1;
    static int old_y = -1;
    Q_BOOL release = Q_FALSE;
    Q_BOOL motion = Q_FALSE;
    Q_BOOL real_motion = Q_FALSE;

#if defined(Q_PDCURSES) || defined(Q_PDCURSES_WIN32)
    if ((rc = nc_getmouse(&mouse)) == OK) {
#else
    if ((rc = getmouse(&mouse)) == OK) {
#endif /* Q_PDCURSES */

        DLOG(("MOUSE mode %s %s\t",
                q_xterm_mouse_protocol == XTERM_MOUSE_OFF ? "XTERM_MOUSE_OFF" :
                q_xterm_mouse_protocol == XTERM_MOUSE_X10 ? "XTERM_MOUSE_X10" :
                q_xterm_mouse_protocol ==
                XTERM_MOUSE_NORMAL ? "XTERM_MOUSE_NORMAL" :
                q_xterm_mouse_protocol ==
                XTERM_MOUSE_BUTTONEVENT ? "XTERM_MOUSE_BUTTONEVENT" :
                q_xterm_mouse_protocol ==
                XTERM_MOUSE_ANYEVENT ? "XTERM_MOUSE_ANYEVENT" : "UNKOWN",
                q_xterm_mouse_encoding ==
                XTERM_MOUSE_ENCODING_X10 ? "XTERM_MOUSE_ENCODING_X10" :
                "XTERM_MOUSE_ENCODING_UTF8"));
        DLOG(("raw: %d %d %d %08lx\t", mouse.x, mouse.y, mouse.z,
                mouse.bstate));

        if (mouse.bstate & REPORT_MOUSE_POSITION) {
            motion = Q_TRUE;
        }
        if ((old_x != mouse.x) || (old_y != mouse.y)) {
            real_motion = Q_TRUE;
        }

        if ((mouse.bstate & BUTTON1_PRESSED) && (mouse1 == Q_FALSE)) {
            mouse1 = Q_TRUE;
        } else if ((mouse1 == Q_TRUE) && (real_motion == Q_FALSE)) {
            /*
             * Convert to RELEASE
             */
            mouse1 = Q_FALSE;
            release = Q_TRUE;
            motion = Q_FALSE;
        }
        if ((mouse.bstate & BUTTON2_PRESSED) && (mouse2 == Q_FALSE)) {
            mouse2 = Q_TRUE;
        } else if ((mouse2 == Q_TRUE) && (real_motion == Q_FALSE)) {
            /*
             * Convert to RELEASE
             */
            mouse2 = Q_FALSE;
            release = Q_TRUE;
            motion = Q_FALSE;
        }
        if ((mouse.bstate & BUTTON3_PRESSED) && (mouse3 == Q_FALSE)) {
            mouse3 = Q_TRUE;
        } else if ((mouse3 == Q_TRUE) && (real_motion == Q_FALSE)) {
            /*
             * Convert to RELEASE
             */
            mouse3 = Q_FALSE;
            release = Q_TRUE;
            motion = Q_FALSE;
        }

        if ((mouse.bstate & BUTTON4_PRESSED) &&
            ((mouse1 == Q_TRUE) || (mouse2 == Q_TRUE) || (mouse3 == Q_TRUE))
        ) {
            /*
             * This is actually a motion event with another mouse button
             * down.
             */
            motion = Q_TRUE;
        } else if ((mouse.bstate & BUTTON4_PRESSED) && (mouse4 == Q_FALSE)) {
            mouse4 = Q_TRUE;
        } else if ((mouse4 == Q_TRUE) && (real_motion == Q_FALSE)) {
            /*
             * Convert to RELEASE
             */
            mouse4 = Q_FALSE;
            release = Q_TRUE;
            motion = Q_FALSE;
        }
#ifdef BUTTON5_PRESSED
        /*
         * ncurses doesn't always support button 5
         */
        if ((mouse.bstate & BUTTON5_PRESSED) && (mouse5 == Q_FALSE)) {
            mouse5 = Q_TRUE;
        } else if ((mouse5 == Q_TRUE) && (real_motion == Q_FALSE)) {
            /*
             * Convert to RELEASE
             */
            mouse5 = Q_FALSE;
            release = Q_TRUE;
            motion = Q_FALSE;
        }
#endif

        if (mouse.bstate & BUTTON1_RELEASED) {
            mouse1 = Q_FALSE;
            release = Q_TRUE;
            motion = Q_FALSE;
        }
        if (mouse.bstate & BUTTON2_RELEASED) {
            mouse2 = Q_FALSE;
            release = Q_TRUE;
            motion = Q_FALSE;
        }
        if (mouse.bstate & BUTTON3_RELEASED) {
            mouse3 = Q_FALSE;
            release = Q_TRUE;
            motion = Q_FALSE;
        }
        if (mouse.bstate & BUTTON4_RELEASED) {
            mouse4 = Q_FALSE;
            release = Q_TRUE;
            motion = Q_FALSE;
        }
#ifdef BUTTON5_RELEASED
        /*
         * ncurses doesn't always support button 5
         */
        if (mouse.bstate & BUTTON5_RELEASED) {
            mouse5 = Q_FALSE;
            release = Q_TRUE;
            motion = Q_FALSE;
        }
#endif

        /*
         * Default to a mouse motion event
         */
        raw_buffer[0] = C_ESC;
        raw_buffer[1] = '[';
        raw_buffer[2] = 'M';
        raw_buffer[3] = 3;
        raw_buffer[4] = mouse.x + 33;
        raw_buffer[5] = mouse.y + 33;

        DLOG(("buttons: %s %s %s %s %s %s %s\n",
                (release == Q_TRUE ? "RELEASE" : "PRESS  "),
                (motion == Q_TRUE ? "MOTION" : "      "),
                (mouse1 == Q_TRUE ? "1" : " "),
                (mouse2 == Q_TRUE ? "2" : " "),
                (mouse3 == Q_TRUE ? "3" : " "),
                (mouse4 == Q_TRUE ? "4" : " "),
                (mouse5 == Q_TRUE ? "5" : " ")));

        /*
         * Encode button information
         */
        if (release == Q_TRUE) {
            raw_buffer[3] = 3;
        } else if (mouse1 == Q_TRUE) {
            raw_buffer[3] = 0;
        } else if (mouse2 == Q_TRUE) {
            raw_buffer[3] = 1;
        } else if (mouse3 == Q_TRUE) {
            raw_buffer[3] = 2;
        } else if (mouse4 == Q_TRUE) {
            raw_buffer[3] = 4;
        } else if (mouse5 == Q_TRUE) {
            raw_buffer[3] = 5;
        }

        /*
         * At this point, if raw_buffer[3] == 3 and release == false, then
         * this was a mouse motion event without a button down.  Otherwise
         * there is a button and release either true or false.
         */

        if (!((raw_buffer[3] == 3) && (release == Q_FALSE))) {
            DLOG(("   button press or button release or motion while button down\n"));
        } else if (!((release == Q_TRUE) || (raw_buffer[3] == 3))) {
            DLOG(("   button press only\n"));
        } else if (!((raw_buffer[3] == 3))) {
            DLOG(("   button press or button release only\n"));
        } else {
            DLOG(("   motion with no button down: %s\n",
                    (motion == Q_TRUE ? "MOTION" : "      ")));
        }

        old_x = mouse.x;
        old_y = mouse.y;

        /*
         * Mouse event is parsed, now decide what to do with it.
         */

        if (q_program_state != Q_STATE_CONSOLE) {
            DLOG((" DISCARD not in console\n"));

            /*
             * Discard.  We only care about the mouse when connected and in
             * the console.
             */
            return;
        }
        if ((q_status.online == Q_FALSE) && !Q_SERIAL_OPEN) {
            DLOG((" DISCARD not online\n"));
            return;
        }

        switch (q_xterm_mouse_protocol) {
        case XTERM_MOUSE_OFF:
            /*
             * Do nothing
             */
            return;

        case XTERM_MOUSE_X10:
            /*
             * Only report button presses
             */
            if ((release == Q_TRUE) || (raw_buffer[3] == 3)) {
                return;
            }
            break;

        case XTERM_MOUSE_NORMAL:
            /*
             * Only report button presses and releases
             */
            if (raw_buffer[3] == 3) {
                return;
            }
            break;

        case XTERM_MOUSE_BUTTONEVENT:
            /*
             * Only report button presses and releases with button down
             */
            if ((raw_buffer[3] == 3) && (release == Q_FALSE)) {
                return;
            }
            break;

        case XTERM_MOUSE_ANYEVENT:
            /*
             * Report everything
             */
            break;
        }

        DLOG(("   -- DO ENCODE --\n"));

        if ((raw_buffer[3] == 4) || (raw_buffer[3] == 5)) {
            DLOG(("   mouse wheel\n"));
            raw_buffer[3] += 64;
        } else {
            raw_buffer[3] += 32;
        }

        if (motion == Q_TRUE) {
            assert(release == Q_FALSE);
            DLOG(("   -motion only\n"));
            /*
             * Motion-only event
             */
            raw_buffer[3] += 32;
        }

        memset(utf8_buffer, 0, sizeof(utf8_buffer));
        switch (q_xterm_mouse_encoding) {
        case XTERM_MOUSE_ENCODING_X10:
            for (i = 0; i < 6; i++) {
                utf8_buffer[i] = raw_buffer[i];
            }
            break;
        case XTERM_MOUSE_ENCODING_UTF8:
            rc = 0;
            for (i = 0; i < 6; i++) {
                rc += utf8_encode(raw_buffer[i], utf8_buffer + rc);
            }
            break;
        }
        DLOG((" * WRITE %ld bytes: ", (long) strlen(utf8_buffer)));
        for (i = 0; i < strlen(utf8_buffer); i++) {
            DLOG2(("%c", utf8_buffer[i]));
        }
        DLOG2(("\n"));
        qodem_write(q_child_tty_fd, utf8_buffer, strlen(utf8_buffer), Q_TRUE);
    }

    /*
     * All done
     */
    return;
}

/**
 * Obtain a keyboard input event from a window.
 *
 * @param window the curses WINDOW to query
 * @param keystroke the output keystroke, or ERR if nothing came in
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.
 * @param usleep_time the number of MICROseconds to wait for input before
 * returning ERR
 */
void qodem_win_getch(void * window, int * keystroke, int * flags,
                     const unsigned int usleep_time) {
    time_t current_time;
    int param = 0;
    wint_t utf_keystroke;
    Q_BOOL modifier = Q_FALSE;
    int return_keystroke = ERR;
    Q_BOOL linux_fkey = Q_FALSE;
    int res;

    /*
     * Check for screensaver
     */
    if ((q_screensaver_timeout > 0) &&
        (q_program_state != Q_STATE_SCREENSAVER) &&
        (q_program_state != Q_STATE_DOWNLOAD) &&
        (q_program_state != Q_STATE_UPLOAD) &&
        (q_program_state != Q_STATE_UPLOAD_BATCH) &&
        (q_program_state != Q_STATE_SCRIPT_EXECUTE) &&
        (q_program_state != Q_STATE_HOST) &&
        (q_program_state != Q_STATE_DIALER)
    ) {

        time(&current_time);
        if (difftime(current_time, screensaver_time) > q_screensaver_timeout) {
            qlog(_("SCREENSAVER activating...\n"));
            switch_state(Q_STATE_SCREENSAVER);
            *flags = 0;
            *keystroke = ERR;
            return;
        }
    }

    /*
     * Assume no KEY_FLAG_ALT or KEY_FLAG_CTRL
     */
    if (flags != NULL) {
        *flags = 0;
    }

    /*
     * Switch to non-block
     */
    if (q_keyboard_blocks == Q_TRUE) {
        nodelay((WINDOW *) window, FALSE);
    } else {
        nodelay((WINDOW *) window, TRUE);
    }

#if defined(Q_PDCURSES) || defined(Q_PDCURSES_WIN32)
    PDC_save_key_modifiers(TRUE);
#endif

    /*
     * Grab keystroke
     */
    res = wget_wch((WINDOW *) window, &utf_keystroke);
    *keystroke = utf_keystroke;
    if (res == ERR) {
        *keystroke = ERR;
    }

    if (*keystroke != ERR) {
        time(&screensaver_time);

#if defined(Q_PDCURSES) || defined(Q_PDCURSES_WIN32)
        unsigned long modifiers = PDC_get_key_modifiers();
        if ((modifiers & PDC_KEY_MODIFIER_CONTROL) != 0) {

            DLOG(("PDC: CTRL\n"));

            if (flags != NULL) {
                *flags |= KEY_FLAG_CTRL;
            }
        }
        if ((modifiers & PDC_KEY_MODIFIER_ALT) != 0) {

            DLOG(("PDC: ALT\n"));

            if (flags != NULL) {
                *flags |= KEY_FLAG_ALT;
            }
        }
        if ((modifiers & PDC_KEY_MODIFIER_NUMLOCK) != 0) {

            DLOG(("PDC: NUMLOCK\n"));

            if (*keystroke == PADENTER) {
                *keystroke = KEY_ENTER;
            }
        }
#endif /* Q_PDCURSES */

    }

    if (*keystroke == ERR) {
        if ((usleep_time > 0) && (q_keyboard_blocks == Q_FALSE)) {
#ifdef Q_PDCURSES_WIN32
            Sleep(usleep_time / 1000);
#else
            usleep(usleep_time);
#endif
        }
    } else if ((*keystroke == KEY_RESIZE) && (res == KEY_CODE_YES)) {
        handle_resize();
        *keystroke = ERR;
    } else if ((*keystroke == KEY_MOUSE) && (res == KEY_CODE_YES)) {
        handle_mouse();
        *keystroke = ERR;
    } else if ((*keystroke == KEY_SUSPEND) && (res == KEY_CODE_YES)) {
        /*
         * Special case: KEY_SUSPEND (Ctrl-Z usually)
         *
         * For now, map it to Ctrl-Z (decimal 26, hex 1A, ASCII [SUB])
         */
        *keystroke = 0x1A;

    } else if ((*keystroke == 0x7F) && (res == OK)) {
        /*
         * Special case: map DEL to KEY_BACKSPACE
         */
        *keystroke = KEY_BACKSPACE;

#if defined(Q_PDCURSES) || defined(Q_PDCURSES_WIN32)
    } else if (res == KEY_CODE_YES) {
        /*
         * Handle PDCurses alternate keystrokes
         */
        pdcurses_key(keystroke, flags);
#endif

    } else if (*keystroke == KEY_ESCAPE) {
        if (flags != NULL) {
            *flags |= KEY_FLAG_ALT;
        }
        /*
         * Grab the next keystroke
         */
        nodelay((WINDOW *) window, TRUE);
        res = wget_wch((WINDOW *) window, &utf_keystroke);
        if (q_keyboard_blocks == Q_TRUE) {
            nodelay((WINDOW *) window, FALSE);
        } else {
            nodelay((WINDOW *) window, TRUE);
        }
        *keystroke = utf_keystroke;
        if (res == ERR) {
            *keystroke = ERR;
        }
        if (*keystroke == ERR) {
            /*
             * This is actually ESCAPE, not ALT-x
             */
            if (flags != NULL) {
                *flags &= ~KEY_FLAG_ALT;
            }
            *keystroke = KEY_ESCAPE;
        } else {
            /*
             * A more complex keyboard sequence has come in that ncurses
             * doesn't know about.  For now just use some simple mapping and
             * assume that all the bytes in the keystroke are present.
             */

            DLOG(("1 Keystroke: '%c' %d %o\n",
                    *keystroke, *keystroke, *keystroke));


            if ((*keystroke == '[') && (res == OK)) {
                /*
                 * CSI
                 */
                param = 0;

                while (*keystroke != ERR) {
                    /*
                     * Grab the next keystroke
                     */
                    res = wget_wch((WINDOW *) window, &utf_keystroke);
                    *keystroke = utf_keystroke;
                    if (res == ERR) {
                        *keystroke = ERR;
                    }

                    DLOG(("2 Keystroke: '%c' %d %o\n",
                            *keystroke, *keystroke, *keystroke));

                    if (*keystroke == '[') {
                        /*
                         * Linux-style function keys
                         */
                        linux_fkey = Q_TRUE;
                        continue;
                    }

                    if ((*keystroke >= '0') && (*keystroke <= '9')
                        && (res == OK)) {
                        param *= 10;
                        param += (*keystroke - '0');
                    } else if (((*keystroke >= 'A') || (*keystroke <= 'E'))
                               && (res == OK) && (linux_fkey == Q_TRUE)) {
                        /*
                         * Linux-style function key
                         */
                        switch (*keystroke) {
                        case 'A':
                            *keystroke = KEY_F(1);
                            break;
                        case 'B':
                            *keystroke = KEY_F(2);
                            break;
                        case 'C':
                            *keystroke = KEY_F(3);
                            break;
                        case 'D':
                            *keystroke = KEY_F(4);
                            break;
                        case 'E':
                            *keystroke = KEY_F(5);
                            break;
                        }

                        /*
                         * Exit the while loop
                         */
                        break;
                    } else if (((*keystroke == '~') || (*keystroke == ';'))
                               && (res == OK)) {

                        DLOG(("2 param: %d\n", param));

                        if ((*keystroke == ';') && (res == OK)) {

                            DLOG(("3 - modifier -\n"));

                            /*
                             * Param is followed by a modifier
                             */
                            modifier = Q_TRUE;
                        }

                        if ((*keystroke == '~') && (modifier == Q_TRUE)
                            && (res == OK)) {

                            DLOG(("3 param: %d\n", param));

                            /*
                             * Param is a modifier:
                             *
                             * SHIFT 2
                             * ALT   3
                             * CTRL  5
                             */
                            switch (param) {
                            case 2:

                                DLOG(("2 SHIFT\n"));

                                if (flags != NULL) {
                                    *flags |= KEY_FLAG_SHIFT;
                                }
                                break;
                            case 3:

                                DLOG(("2 ALT\n"));

                                if (flags != NULL) {
                                    *flags |= KEY_FLAG_ALT;
                                }
                                break;
                            case 5:

                                DLOG(("2 CTRL\n"));

                                if (flags != NULL) {
                                    *flags |= KEY_FLAG_CTRL;
                                }
                                break;
                            }
                            *keystroke = return_keystroke;
                            if ((return_keystroke > 0xFF) && (res == OK)) {
                                /*
                                 * Unicode character
                                 */
                                *flags |= KEY_FLAG_UNICODE;
                            }
                            /*
                             * Exit the while loop
                             */
                            break;
                        }

                        if (((*keystroke == '~') && (modifier == Q_FALSE)
                             && (res == OK)) || ((*keystroke == ';')
                                                 && (modifier == Q_TRUE)
                                                 && (res == OK))) {

                            switch (param) {
                            case 1:

                                DLOG(("2 Home\n"));

                                *keystroke = KEY_HOME;
                                break;
                            case 4:

                                DLOG(("2 End\n"));

                                *keystroke = KEY_END;
                                break;
                            case 5:

                                DLOG(("2 PgUp\n"));

                                *keystroke = KEY_PPAGE;
                                break;
                            case 6:

                                DLOG(("2 PgDn\n"));

                                *keystroke = KEY_NPAGE;
                                break;
                            default:
                                *keystroke = ERR;
                                break;
                            }

                            /*
                             * Reset for possible modifier
                             */
                            param = 0;

                            if ((*keystroke == '~') && (res == OK)) {
                                /*
                                 * Exit the while loop
                                 */
                                break;
                            } else {
                                return_keystroke = *keystroke;
                                if ((return_keystroke > 0xFF) && (res == OK)) {
                                    /*
                                     * Unicode character
                                     */
                                    *flags |= KEY_FLAG_UNICODE;
                                }
                            }
                        }
                    }
                }

            } else if ((*keystroke == 'O') && (res == OK)) {
                /*
                 * VT100-style function key
                 */
                param = 0;

                /*
                 * Grab the next keystroke
                 */
                res = wget_wch((WINDOW *) window, &utf_keystroke);
                *keystroke = utf_keystroke;
                if (res == ERR) {
                    *keystroke = ERR;
                }

                DLOG(("2 Keystroke: '%c' %d %o\n",
                        *keystroke, *keystroke, *keystroke));

                switch (*keystroke) {
                case 'H':
                    *keystroke = KEY_HOME;
                    break;
                case 'F':
                    *keystroke = KEY_END;
                    break;
                case 'P':
                    *keystroke = KEY_F(1);
                    break;
                case 'Q':
                    *keystroke = KEY_F(2);
                    break;
                case 'R':
                    *keystroke = KEY_F(3);
                    break;
                case 'S':
                    *keystroke = KEY_F(4);
                    break;
                case 't':
                    *keystroke = KEY_F(5);
                    break;
                case 'u':
                    *keystroke = KEY_F(6);
                    break;
                case 'v':
                    *keystroke = KEY_F(7);
                    break;
                case 'l':
                    *keystroke = KEY_F(8);
                    break;
                case 'w':
                    *keystroke = KEY_F(9);
                    break;
                case 'x':
                    *keystroke = KEY_F(10);
                    break;
                }
            }
        }
    }

    /*
     * Restore the normal keyboard
     */
    if (q_keyboard_blocks == Q_TRUE) {
        nodelay((WINDOW *) window, FALSE);
    } else {
        nodelay((WINDOW *) window, TRUE);
    }

    /*
     * Set CTRL for normal keystrokes
     */
    if ((*keystroke < 0x20) && (res == OK)) {
        if (flags != NULL) {
            *flags |= KEY_FLAG_CTRL;
        }
    }

    /*
     * Special case: remap KEY_FIND to KEY_HOME and KEY_SELECT to KEY_END
     */
    if (*keystroke == KEY_FIND) {
        *keystroke = KEY_HOME;
    }
    if (*keystroke == KEY_SELECT) {
        *keystroke = KEY_END;
    }

    if (*keystroke != -1) {
        if (flags != NULL) {
            DLOG(("Keystroke: %d %o FLAGS: %02x\n",
                    *keystroke, *keystroke, *flags));
        } else {
            DLOG(("Keystroke: %d %o FLAGS: NULL\n",
                    *keystroke, *keystroke));
        }
    }

    return;
}

/**
 * Obtain a keyboard input event from stdscr.  This is the main keyboard and
 * mouse input function, called by keyboard_handler().
 *
 * @param keystroke the output keystroke, or ERR if nothing came in
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.
 * @param usleep_time the number of MICROseconds to wait for input before
 * returning ERR
 */
void qodem_getch(int * keystroke, int * flags, const unsigned int usleep_time) {
    qodem_win_getch(stdscr, keystroke, flags, usleep_time);
}

/**
 * Read data from the keyboard/mouse and throw it away.
 */
void discarding_getch() {
    wgetch(stdscr);
}

/**
 * Tell curses whether or not we want calls to getch() to block.
 *
 * @param block if true, block on input.  If false, when no input is
 * available then return ERR.
 */
void set_blocking_input(Q_BOOL block) {
    if (block == Q_TRUE) {
        nodelay(stdscr, FALSE);
    } else {
        nodelay(stdscr, TRUE);
    }
}

/**
 * Make the cursor invisible.
 *
 * @return the previous cursor state
 */
int q_cursor_off() {
    return curs_set(0);
}

/**
 * Make the cursor visible.
 *
 * @return the previous cursor state
 */
int q_cursor_on() {
    return curs_set(1);
}

/**
 * Make the cursor visible or invisible.
 *
 * @param cursor if 0, make the cursor invisible.  If 1, make it visible.  If
 * 2, make it "very visible".
 * @return the previous cursor state
 */
int q_cursor(const int cursor) {
    return curs_set(cursor);
}

/**
 * Determine if a keystroke is a "special key" like a function key, arrow
 * key, or number pad key, etc.
 *
 * @param keystroke key to check
 * @return 1 if this is a special key, 0 otherwise
 */
int q_key_code_yes(int keystroke) {
    if ((keystroke >= Q_KEY_PAD_MIN) && (keystroke <= Q_KEY_PAD_MAX)) {
        return 1;
    }
    if ((keystroke >= KEY_MIN) && (keystroke <= KEY_MAX)) {
        return 1;
    }
    return 0;
}
