/*
 * screen.c
 *
 * This module is licensed under the GNU General Public License
 * Version 2.  Please see the file "COPYING" in this directory for
 * more information about the GNU General Public License Version 2.
 *
 *     Copyright (C) 2015  Kevin Lamonte
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

/*
 * This module contains the ncurses-based screen drawing functions.
 */

#include "qcurses.h"
#include "common.h"

#include <string.h>
#include <assert.h>
#include "qodem.h"
#include "music.h"
#include "linux.h"
#include "states.h"
#include "status.h"
#include "screen.h"

/*
 * map_screen_color - re-map a COLOR_X + A_BOLD into the specific DOS colors
 *                    that Qmodem(tm) used.
 */
static inline attr_t map_screen_color(const attr_t color) {
        attr_t new_color = color;
        if (color & A_BOLD) {
                new_color += 128;
                new_color &= ~(A_BOLD);
        }
        return new_color;
} /* ---------------------------------------------------------------------- */

/*
 * Turn a Q_COLOR_* enum into a color pair index.
 * This also performs the 0x38 <--> 0x00 switch.
 */
short screen_color(const int q_color) {
        short color = (q_text_colors[q_color].fg << 3) | q_text_colors[q_color].bg;
        if (color == 0x38) {
                color = q_white_color_pair_num;
        } else if (color == 0x00) {
                color = 0x38;
        }
        return color;
} /* ---------------------------------------------------------------------- */

/*
 * Turn a Q_COLOR_* enum into the color part of the ncurses attr_t.
 */
static attr_t scrollback_color(const int q_color) {
        short color = (q_text_colors[q_color].fg << 3) | q_text_colors[q_color].bg;
        return COLOR_PAIR(color);
} /* ---------------------------------------------------------------------- */

/*
 * Turn a Q_COLOR_* enum into the non-color part of the ncurses attr_t
 */
static attr_t screen_attr(const int q_color) {
        attr_t attr = A_NORMAL;
        if (q_text_colors[q_color].bold == Q_TRUE) {
                attr |= A_BOLD;
        }
        return attr;
} /* ---------------------------------------------------------------------- */

/*
 * Turn a Q_COLOR_* enum into a ncurses attr_t
 */
attr_t scrollback_full_attr(const int q_color) {
        attr_t color = scrollback_color(q_color) | screen_attr(q_color);
        return color;
} /* ---------------------------------------------------------------------- */

attr_t color_from_attr(const attr_t color) {
        return PAIR_NUMBER(color);
} /* ---------------------------------------------------------------------- */

attr_t color_to_attr(const attr_t color) {
        return COLOR_PAIR(color);
} /* ---------------------------------------------------------------------- */

/*
 * VT100 reverse video (DECSCNM) behaves differently than A_REVERSE.
 * DECSCNM switches the default foreground and background color;
 * A_REVERSE inverts the background color and nothing else.
 */
attr_t vt100_check_reverse_color(const attr_t color, const Q_BOOL reverse) {
        attr_t attrs = color & NO_COLOR_MASK;
        short old_color = color_from_attr(color);
        short old_color_bg = (old_color & 0x07);
        short old_color_fg = (old_color & 0x38) >> 3;
        short flip_color = 0x38;
        Q_BOOL do_flip = Q_FALSE;

        switch (q_status.emulation) {
        case Q_EMUL_TTY:
        case Q_EMUL_VT52:
        case Q_EMUL_DEBUG:
                /* These guys just do the braindead stuff */
                return color;
        case Q_EMUL_ANSI:
        case Q_EMUL_AVATAR:
        case Q_EMUL_VT100:
        case Q_EMUL_VT102:
        case Q_EMUL_VT220:
        case Q_EMUL_LINUX:
        case Q_EMUL_LINUX_UTF8:
        case Q_EMUL_XTERM:
        case Q_EMUL_XTERM_UTF8:
                break;
        }

        if ((reverse == Q_TRUE) && ((attrs & A_REVERSE) != 0)) {
                /*
                 * Reverse character on a reverse screen.  Keep the
                 * original color, turn off A_REVERSE.
                 */
                attrs &= ~A_REVERSE;
                do_flip = Q_FALSE;
        } else if ((reverse == Q_FALSE) && ((attrs & A_REVERSE) != 0)) {
                /*
                 * Reverse. on a normal screen.  Turn off A_REVERSE
                 * and flip foreground/background colors.
                 */
                attrs &= ~A_REVERSE;
                do_flip = Q_TRUE;
        } else if (reverse == Q_TRUE) {
                /* Normal character on a reverse screen.  Flip
                 * foreground/background
                 */
                do_flip = Q_TRUE;
        }

        /* At this point, A_REVERSE should NOT be set */
        assert((attrs & A_REVERSE) == 0);

        if (do_flip == Q_TRUE) {
                flip_color = old_color_fg;
                flip_color |= old_color_bg << 3;
        } else {
                flip_color = old_color;
        }

        return color_to_attr(flip_color) | attrs;
} /* ---------------------------------------------------------------------- */

/* Ignore the address warning in this file.  It is generated by
 * wattr_get() */
#pragma GCC diagnostic ignored "-Waddress"

/*
 * Drop a character on screen using EITHER mvaddch or mvadd_wch, depending on
 * whether or not attr has WA_ALTCHARSET set.
 */
static void screen_win_put_ch_attr_yx(WINDOW * win, const int y, const int x, const wchar_t wch, const cchar_t * ch, const attr_t attr, const short pair) {
        if (attr & WA_ALTCHARSET) {
                attr_t old_attr;
                short old_pair;
                wattr_get(win, &old_attr, &old_pair, NULL);
                wattr_set(win, attr, pair, NULL);
                mvwaddch(win, y, x, wch & 0xFF);
                wattr_set(win, old_attr, old_pair, NULL);
        } else {
                mvwadd_wch(win, y, x, ch);
        }
} /* ---------------------------------------------------------------------- */

/*
 * Drop a character on screen using EITHER addch or add_wch, depending on
 * whether or not attr has WA_ALTCHARSET set.
 */
static void screen_win_put_ch_attr(WINDOW * win, const wchar_t wch, const cchar_t * ch, const attr_t attr, const short pair) {
        if (attr & WA_ALTCHARSET) {
                attr_t old_attr;
                short old_pair;
                wattr_get(win, &old_attr, &old_pair, NULL);
                wattr_set(win, attr, pair, NULL);
                waddch(win, wch & 0xFF);
                wattr_set(win, old_attr, old_pair, NULL);
        } else {
                wadd_wch(win, ch);
        }
} /* ---------------------------------------------------------------------- */

void screen_put_scrollback_char_yx(const int y, const int x, const wchar_t ch, const attr_t attr) {
        static cchar_t ncurses_ch;
        static wchar_t ch_cached = -1;
        static attr_t attr_cached = A_BLINK | A_PROTECT;        /* Something I won't use often */
        static int cache_count = 0;
        static wchar_t wch[2];
        static attr_t attr2 = 0;
        static short color;
        if ((ch == ch_cached) && (attr == attr_cached)) {
                /* NOP */
                cache_count++;
        } else {
                /*
                 * We have to strip out the character from the color attributes,
                 * which means peeking inside the curses interface.
                 */
                attr2 = attr & Q_A_ATTRIBUTES;
                color = PAIR_NUMBER(attr);
                if (color == 0x38) {
                        color = q_white_color_pair_num;
                } else if (color == 0x00) {
                        color = 0x38;
                }
                wch[0] = ch;
                wch[1] = 0;
                wch_to_acs(&wch[0], &attr2);
                setcchar(&ncurses_ch, wch, attr2, map_screen_color(color), NULL);
                ch_cached = ch;
                attr_cached = attr;
        }
        screen_win_put_ch_attr_yx(stdscr, y, x, wch[0], &ncurses_ch, attr2, map_screen_color(color));
} /* ---------------------------------------------------------------------- */

/*
 * Put ch on screen with attr and color
 */
static void screen_win_put_char(void * win, const wchar_t ch, const attr_t attr, const short color) {
        cchar_t ncurses_ch;
        wchar_t wch[2];
        attr_t attr2 = attr;
        wch[0] = ch;
        wch[1] = 0;
        wch_to_acs(&wch[0], &attr2);
        setcchar(&ncurses_ch, wch, attr, map_screen_color(color), NULL);
        screen_win_put_ch_attr((WINDOW *)win, wch[0], &ncurses_ch, attr2, map_screen_color(color));
} /* ---------------------------------------------------------------------- */

static void screen_win_put_char_yx(void * win, const int y, const int x, const wchar_t ch, const attr_t attr, const short color) {
        cchar_t ncurses_ch;
        wchar_t wch[2];
        attr_t attr2 = attr;
        wch[0] = ch;
        wch[1] = 0;
        wch_to_acs(&wch[0], &attr2);
        setcchar(&ncurses_ch, wch, attr, map_screen_color(color), NULL);
        screen_win_put_ch_attr_yx((WINDOW *)win, y, x, wch[0], &ncurses_ch, attr2, map_screen_color(color));
} /* ---------------------------------------------------------------------- */

static void screen_win_put_str(void * win, const char * str, const attr_t attr, const short color) {
        int i;
        for (i = 0; i < strlen(str); i++) {
                screen_win_put_char(win, str[i], attr, map_screen_color(color));
        }
} /* ---------------------------------------------------------------------- */

static void screen_win_put_wcs(void * win, const wchar_t * wcs, const attr_t attr, const short color) {
        int i;
        for (i = 0; i < wcslen(wcs); i++) {
                screen_win_put_char(win, wcs[i], attr, map_screen_color(color));
        }
} /* ---------------------------------------------------------------------- */

static void screen_win_put_str_yx(void * win, const int y, const int x, const char * str, const attr_t attr, const short color) {
        int i;
        for (i = 0; i < strlen(str); i++) {
                screen_win_put_char_yx(win, y, x + i, str[i], attr, map_screen_color(color));
        }
} /* ---------------------------------------------------------------------- */

static void screen_win_put_strn(void * win, const char * str, const int n, const attr_t attr, const short color) {
        int i;
        for (i = 0; (i < strlen(str)) && (i < n); i++) {
                screen_win_put_char(win, str[i], attr, color);
        }
} /* ---------------------------------------------------------------------- */

static void screen_win_put_strn_yx(void * win, const int y, const int x, const char * str, const int n, const attr_t attr, const short color) {
        int i;
        for (i = 0; (i < strlen(str)) && (i < n); i++) {
                screen_win_put_char_yx(win, y, x + i, str[i], attr, color);
        }
} /* ---------------------------------------------------------------------- */

static void screen_win_put_wcs_yx(void * win, const int y, const int x, const wchar_t * wcs, const attr_t attr, const short color) {
        int i;
        for (i = 0; i < wcslen(wcs); i++) {
                screen_win_put_char_yx(win, y, x + i, wcs[i], attr, map_screen_color(color));
        }
} /* ---------------------------------------------------------------------- */

static void screen_win_put_hline_yx(void * win, const int y, const int x, const wchar_t ch, const int n, const attr_t attr, const short color) {
        cchar_t ncurses_ch;
        wchar_t wch[2];
        attr_t attr2 = attr;
        wch[0] = ch;
        wch[1] = 0;
        wch_to_acs(&wch[0], &attr2);
        setcchar(&ncurses_ch, wch, attr, map_screen_color(color), NULL);
        if (attr2 & WA_ALTCHARSET) {
                attr_t old_attr;
                short old_pair;
                attr_get(&old_attr, &old_pair, NULL);
                attr_set(attr2, map_screen_color(color), NULL);
                mvwhline((WINDOW *)win, y, x, wch[0], n);
                attr_set(old_attr, old_pair, NULL);
        } else {
                mvwhline_set((WINDOW *)win, y, x, &ncurses_ch, n);
        }
} /* ---------------------------------------------------------------------- */

static void screen_win_put_vline_yx(void * win, const int y, const int x, const wchar_t ch, const int n, const attr_t attr, const short color) {
        cchar_t ncurses_ch;
        wchar_t wch[2];
        attr_t attr2 = attr;
        wch[0] = ch;
        wch[1] = 0;
        wch_to_acs(&wch[0], &attr2);
        setcchar(&ncurses_ch, wch, attr, map_screen_color(color), NULL);

        if (attr2 & WA_ALTCHARSET) {
                attr_t old_attr;
                short old_pair;
                attr_get(&old_attr, &old_pair, NULL);
                attr_set(attr2, map_screen_color(color), NULL);
                mvwvline((WINDOW *)win, y, x, wch[0], n);
                attr_set(old_attr, old_pair, NULL);
        } else {
                mvwvline_set((WINDOW *)win, y, x, &ncurses_ch, n);
        }
} /* ---------------------------------------------------------------------- */

void screen_put_char_yx(const int y, const int x, const wchar_t ch, const attr_t attr, const short color) {
        screen_win_put_char_yx(stdscr, y, x, ch, attr, color);
} /* ---------------------------------------------------------------------- */

void screen_put_str_yx(const int y, const int x, const char * str, const attr_t attr, const short color) {
#if defined(Q_PDCURSES) || defined(Q_PDCURSES_WIN32)
        /*
         * PDCurses doesn't display "\n"'s as processed newlines, instead it shows
         * little boxes.  Since this function is only called with newlines when
         * I am about to spawn an X11 terminal in another window, I will NOP
         * if I see a newline in the string.
         */
        int i;
        for (i = 0; i < strlen(str); i++) {
                if (str[i] == '\n') {
                        return;
                }
        }
#endif
        screen_win_put_str_yx(stdscr, y, x, str, attr, color);
} /* ---------------------------------------------------------------------- */

void screen_put_printf_yx(const int y, const int x, const attr_t attr, const short color, const char * format, ...) {
        va_list arglist;
        char outbuf[DIALOG_MESSAGE_SIZE];
        assert(strlen(format) < DIALOG_MESSAGE_SIZE);
        memset(outbuf, 0, sizeof(outbuf));
        va_start(arglist, format);
#ifdef __BORLANDC__
        vsprintf((char *)(outbuf + strlen(outbuf)), format, arglist);
#else
        vsnprintf((char *)(outbuf + strlen(outbuf)), (DIALOG_MESSAGE_SIZE - strlen(outbuf)), format, arglist);
#endif
        va_end(arglist);

        screen_win_put_strn_yx(stdscr, y, x, outbuf, strlen(outbuf), attr, color);
} /* ---------------------------------------------------------------------- */

void screen_win_put_color_char(void * win, const wchar_t ch, const int q_color) {
        screen_win_put_char((WINDOW *)win, ch, screen_attr(q_color), screen_color(q_color));
} /* ---------------------------------------------------------------------- */

void screen_win_put_color_char_yx(void * win, const int y, const int x, const wchar_t ch, const int q_color) {
        screen_win_put_char_yx((WINDOW *)win, y, x, ch, screen_attr(q_color), screen_color(q_color));
} /* ---------------------------------------------------------------------- */

void screen_win_put_color_str(void * win, const char * str, const int q_color) {
        screen_win_put_str((WINDOW *)win, str, screen_attr(q_color), screen_color(q_color));
} /* ---------------------------------------------------------------------- */

void screen_win_put_color_wcs(void * win, const wchar_t * wcs, const int q_color) {
        screen_win_put_wcs((WINDOW *)win, wcs, screen_attr(q_color), screen_color(q_color));
} /* ---------------------------------------------------------------------- */

void screen_win_put_color_str_yx(void * win, const int y, const int x, const char * str, const int q_color) {
        screen_win_put_str_yx((WINDOW *)win, y, x, str, screen_attr(q_color), screen_color(q_color));
} /* ---------------------------------------------------------------------- */

void screen_win_put_color_wcs_yx(void * win, const int y, const int x, const wchar_t * wcs, const int q_color) {
        screen_win_put_wcs_yx((WINDOW *)win, y, x, wcs, screen_attr(q_color), screen_color(q_color));
} /* ---------------------------------------------------------------------- */

void screen_win_put_color_strn(void * win, const char * str, const int n, const int q_color) {
        screen_win_put_strn((WINDOW *)win, str, n, screen_attr(q_color), screen_color(q_color));
} /* ---------------------------------------------------------------------- */

void screen_win_put_color_strn_yx(void * win, const int y, const int x, const char * str, const int n, const int q_color) {
        screen_win_put_strn_yx((WINDOW *)win, y, x, str, n, screen_attr(q_color), screen_color(q_color));
} /* ---------------------------------------------------------------------- */

void screen_win_put_color_hline_yx(void * win, const int y, const int x, const wchar_t ch, const int n, const int q_color) {
        screen_win_put_hline_yx((WINDOW *)win, y, x, ch, n, screen_attr(q_color), screen_color(q_color));
} /* ---------------------------------------------------------------------- */

void screen_win_put_color_vline_yx(void * win, const int y, const int x, const wchar_t ch, const int n, const int q_color) {
        screen_win_put_vline_yx((WINDOW *)win, y, x, ch, n, screen_attr(q_color), screen_color(q_color));
} /* ---------------------------------------------------------------------- */

void screen_win_put_color_printf(void * win, const int q_color, const char * format, ...) {
        va_list arglist;
        char outbuf[DIALOG_MESSAGE_SIZE];
        assert(strlen(format) < DIALOG_MESSAGE_SIZE);
        memset(outbuf, 0, sizeof(outbuf));
        va_start(arglist, format);
#ifdef __BORLANDC__
        vsprintf((char *)(outbuf + strlen(outbuf)), format, arglist);
#else
        vsnprintf((char *)(outbuf + strlen(outbuf)), (DIALOG_MESSAGE_SIZE - strlen(outbuf)), format, arglist);
#endif
        va_end(arglist);

        screen_win_put_strn((WINDOW *)win, outbuf, strlen(outbuf), screen_attr(q_color), screen_color(q_color));
} /* ---------------------------------------------------------------------- */

void screen_win_put_color_printf_yx(void * win, const int y, const int x, const int q_color, const char * format, ...) {
        va_list arglist;
        char outbuf[DIALOG_MESSAGE_SIZE];
        assert(strlen(format) < DIALOG_MESSAGE_SIZE);
        memset(outbuf, 0, sizeof(outbuf));
        va_start(arglist, format);
#ifdef __BORLANDC__
        vsprintf((char *)(outbuf + strlen(outbuf)), format, arglist);
#else
        vsnprintf((char *)(outbuf + strlen(outbuf)), (DIALOG_MESSAGE_SIZE - strlen(outbuf)), format, arglist);
#endif
        va_end(arglist);

        screen_win_put_strn_yx((WINDOW *)win, y, x, outbuf, strlen(outbuf), screen_attr(q_color), screen_color(q_color));
} /* ---------------------------------------------------------------------- */

void screen_put_color_char(const wchar_t ch, const int q_color) {
        screen_win_put_char(stdscr, ch, screen_attr(q_color), screen_color(q_color));
} /* ---------------------------------------------------------------------- */

void screen_put_color_char_yx(const int y, const int x, const wchar_t ch, const int q_color) {
        screen_win_put_char_yx(stdscr, y, x, ch, screen_attr(q_color), screen_color(q_color));
} /* ---------------------------------------------------------------------- */

void screen_put_color_str(const char * str, const int q_color) {
        screen_win_put_str(stdscr, str, screen_attr(q_color), screen_color(q_color));
} /* ---------------------------------------------------------------------- */

void screen_put_color_wcs(const wchar_t * wcs, const int q_color) {
        screen_win_put_wcs(stdscr, wcs, screen_attr(q_color), screen_color(q_color));
} /* ---------------------------------------------------------------------- */

void screen_put_color_str_yx(const int y, const int x, const char * str, const int q_color) {
        screen_win_put_str_yx(stdscr, y, x, str, screen_attr(q_color), screen_color(q_color));
} /* ---------------------------------------------------------------------- */

void screen_put_color_wcs_yx(const int y, const int x, const wchar_t * wcs, const int q_color) {
        screen_win_put_wcs_yx(stdscr, y, x, wcs, screen_attr(q_color), screen_color(q_color));
} /* ---------------------------------------------------------------------- */

void screen_put_color_strn(const char * str, const int n, const int q_color) {
        screen_win_put_strn(stdscr, str, n, screen_attr(q_color), screen_color(q_color));
} /* ---------------------------------------------------------------------- */

void screen_put_color_strn_yx(const int y, const int x, const char * str, const int n, const int q_color) {
        screen_win_put_strn_yx(stdscr, y, x, str, n, screen_attr(q_color), screen_color(q_color));
} /* ---------------------------------------------------------------------- */

void screen_put_color_hline_yx(const int y, const int x, const wchar_t ch, const int n, const int q_color) {
        screen_win_put_hline_yx(stdscr, y, x, ch, n, screen_attr(q_color), screen_color(q_color));
} /* ---------------------------------------------------------------------- */

void screen_put_color_vline_yx(const int y, const int x, const wchar_t ch, const int n, const int q_color) {
        screen_win_put_vline_yx(stdscr, y, x, ch, n, screen_attr(q_color), screen_color(q_color));
} /* ---------------------------------------------------------------------- */

void screen_put_color_printf(const int q_color, const char * format, ...) {
        va_list arglist;
        char outbuf[DIALOG_MESSAGE_SIZE];
        assert(strlen(format) < DIALOG_MESSAGE_SIZE);
        memset(outbuf, 0, sizeof(outbuf));
        va_start(arglist, format);
#ifdef __BORLANDC__
        vsprintf((char *)(outbuf + strlen(outbuf)), format, arglist);
#else
        vsnprintf((char *)(outbuf + strlen(outbuf)), (DIALOG_MESSAGE_SIZE - strlen(outbuf)), format, arglist);
#endif
        va_end(arglist);

        screen_win_put_strn(stdscr, outbuf, strlen(outbuf), screen_attr(q_color), screen_color(q_color));
} /* ---------------------------------------------------------------------- */

void screen_put_color_printf_yx(const int y, const int x, const int q_color, const char * format, ...) {
        va_list arglist;
        char outbuf[DIALOG_MESSAGE_SIZE];
        assert(strlen(format) < DIALOG_MESSAGE_SIZE);
        memset(outbuf, 0, sizeof(outbuf));
        va_start(arglist, format);
#ifdef __BORLANDC__
        vsprintf((char *)(outbuf + strlen(outbuf)), format, arglist);
#else
        vsnprintf((char *)(outbuf + strlen(outbuf)), (DIALOG_MESSAGE_SIZE - strlen(outbuf)), format, arglist);
#endif
        va_end(arglist);

        screen_win_put_strn_yx(stdscr, y, x, outbuf, strlen(outbuf), screen_attr(q_color), screen_color(q_color));
} /* ---------------------------------------------------------------------- */

void screen_move_yx(const int y, const int x) {
        move(y, x);
} /* ---------------------------------------------------------------------- */

void screen_win_move_yx(void * win, const int y, const int x) {
        wmove((WINDOW *)win, y, x);
} /* ---------------------------------------------------------------------- */

void screen_flush() {
        refresh();
} /* ---------------------------------------------------------------------- */

void screen_win_flush(void * win) {
        wrefresh((WINDOW *)win);
} /* ---------------------------------------------------------------------- */

void screen_clear() {
        werase(stdscr);
} /* ---------------------------------------------------------------------- */

void screen_really_clear() {
        int i;
        cchar_t ncurses_ch;
        wchar_t wch[2];
        wch[0] = ' ';
        wch[1] = 0;
        setcchar(&ncurses_ch, wch, A_NORMAL, 0x1, NULL);
        for (i = 0; i < HEIGHT; i++) {
                mvhline_set(i, 0, &ncurses_ch, WIDTH);
        }
        refresh();
} /* ---------------------------------------------------------------------- */

void screen_win_get_yx(void * win, int * y, int * x) {
        int screen_x;
        int screen_y;
        getyx((WINDOW *)win, screen_y, screen_x);
        *y = screen_y;
        *x = screen_x;
} /* ---------------------------------------------------------------------- */

/* Beep support */
void screen_beep() {
        struct q_music_struct p;
        static time_t last_beep = 0;
        time_t now;

        if (q_status.beeps == Q_FALSE) {
                /* Don't beep */
                return;
        }

        /* Do not beep more than once per second.  Ever. */
        time(&now);
        if ((now - last_beep) < 1) {
                return;
        }
        last_beep = now;

        switch (q_status.emulation) {

        case Q_EMUL_TTY:
        case Q_EMUL_DEBUG:
        case Q_EMUL_ANSI:
        case Q_EMUL_AVATAR:
        case Q_EMUL_VT52:
        case Q_EMUL_VT100:
        case Q_EMUL_VT102:
        case Q_EMUL_VT220:
        case Q_EMUL_XTERM:
        case Q_EMUL_XTERM_UTF8:
                /* Most emulations just beep normally. */
                beep();
                break;

        case Q_EMUL_LINUX:
        case Q_EMUL_LINUX_UTF8:
                /*
                 * Linux emulation is different:  we have to beep using the
                 * correct frequency and duration.
                 */
                /* "Play" the beep */
                memset(&p, 0, sizeof(struct q_music_struct));
                p.hertz = q_linux_beep_frequency;
                p.duration = q_linux_beep_duration;
                play_music(&p, Q_TRUE);
                break;
        }

} /* ---------------------------------------------------------------------- */

void screen_setup() {
#if defined(Q_PDCURSES) || defined(Q_PDCURSES_WIN32)
#ifdef XCURSES
        /* Setup for X11-based PDCurses */
        char * pdcursesOptions[6] = {
                "qodem",
                "-lines",
                "25",
                "-cols",
                "80",
                0
        };
        Xinitscr(5, pdcursesOptions);
#else
        /* Setup for Win32-based PDCurses */
        initscr();
        /* Make it bigger. */
        /* resize_term(50, 132); */
        resize_term(25, 80);
#endif /* XCURSES */

        /* Additional common setup for PDCurses */
        PDC_set_blink(TRUE);
        PDC_set_title("qodem " Q_VERSION);

#else
        initscr();
#endif /* Q_PDCURSES */

        getmaxyx(stdscr, HEIGHT, WIDTH);
        STATUS_HEIGHT = 1;
        /*
         * I remember re-reading the worklog.html and wondering how I
         * managed to get ^Z and ^C passed in.  Here it is: curses
         * call to enable raw mode.
         */
        raw();
        nodelay(stdscr, TRUE);
        q_keyboard_blocks = Q_FALSE;
        noecho();
        nonl();
        intrflush(stdscr, FALSE);
        meta(stdscr, TRUE);
        keypad(stdscr, TRUE);
        start_color();
        q_setup_colors();

        /* Set color AFTER they've been initialized! */
        q_current_color = scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);

        /* Enable the mouse.  Do not resolve double and triple clicks. */
        mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
        mouseinterval(0);
} /* ---------------------------------------------------------------------- */

void screen_teardown() {
        /* Disable the mouse */
        mousemask(0, NULL);

        endwin();
} /* ---------------------------------------------------------------------- */

void screen_clear_remaining_line(Q_BOOL double_width) {
        int x, y;
        int i;
        /* Use the TERMINAL mode background color */
        getyx(stdscr, y, x);
        for (i = x; i < WIDTH; i++) {
                if (double_width == Q_TRUE) {
                        if ((2 * i) >= WIDTH) {
                                break;
                        } else if (i >= WIDTH) {
                                break;
                        }
                }
                screen_put_char_yx(y, i, ' ', 0, screen_color(Q_COLOR_CONSOLE_BACKGROUND));
        }
        move(y, x);
} /* ---------------------------------------------------------------------- */

void screen_get_dimensions(int * height, int * width) {
        int local_height;
        int local_width;
        getmaxyx(stdscr, local_height, local_width);
        *height = local_height;
        *width = local_width;
} /* ---------------------------------------------------------------------- */

static void * screen_win_subwin(void * win, int height, int width, int top, int left) {
        WINDOW * window = subwin(stdscr, height, width, top, left);
        if (window != NULL) {
                meta(window, TRUE);
                keypad(window, TRUE);
        }
        return window;
} /* ---------------------------------------------------------------------- */

void * screen_subwin(int height, int width, int top, int left) {
        return screen_win_subwin(stdscr, height, width, top, left);
} /* ---------------------------------------------------------------------- */

void screen_delwin(void * win) {
        assert(win != NULL);
        delwin((WINDOW *)win);
} /* ---------------------------------------------------------------------- */

/*
 * Draw a standard-looking box
 */
void screen_draw_box(const int left, const int top, const int right, const int bottom) {
        screen_win_draw_box(stdscr, left, top, right, bottom);
} /* ---------------------------------------------------------------------- */

/*
 * Draw a standard-looking box
 */
void screen_win_draw_box(void * window, const int left, const int top, const int right, const int bottom) {
        screen_win_draw_box_color(window, left, top, right, bottom, Q_COLOR_WINDOW_BORDER, Q_COLOR_WINDOW);
} /* ---------------------------------------------------------------------- */

/*
 * Draw a standard-looking box
 */
void screen_win_draw_box_color(void * window, const int left, const int top, const int right, const int bottom, const int border, const int background) {
        int i;
        int window_height;
        int window_length;
        int window_top;
        int window_left;

        window_length = right - left;
        window_height = bottom - top;

        screen_win_put_color_char_yx(window, top, left, cp437_chars[Q_WINDOW_LEFT_TOP], border);
        screen_win_put_color_char_yx(window, top, left + window_length - 1, cp437_chars[Q_WINDOW_RIGHT_TOP], border);
        screen_win_put_color_char_yx(window, top + window_height - 1, left, cp437_chars[Q_WINDOW_LEFT_BOTTOM], border);
        screen_win_put_color_char_yx(window, top + window_height - 1, left + window_length - 1, cp437_chars[Q_WINDOW_RIGHT_BOTTOM], border);
        screen_win_put_color_hline_yx(window, top, left + 1, cp437_chars[Q_WINDOW_TOP], window_length - 2, border);
        screen_win_put_color_vline_yx(window, top + 1, left, cp437_chars[Q_WINDOW_SIDE], window_height - 2, border);
        screen_win_put_color_hline_yx(window, top + window_height - 1, left + 1, cp437_chars[Q_WINDOW_TOP], window_length - 2, border);
        screen_win_put_color_vline_yx(window, top + 1, left + window_length - 1, cp437_chars[Q_WINDOW_SIDE], window_height - 2, border);

        /* Background */
        for (i = 1; i < window_height - 1; i++) {
                screen_win_put_color_hline_yx(window, i + top, 1 + left, ' ', window_length - 2, background);
        }

        /* Draw a shadow directly on stdscr */
        if (window == stdscr) {
                window_top = top;
                window_left = left;
        } else {
                getbegyx((WINDOW *)window, window_top, window_left);
        }

        for (i = 1; i < window_height + 1; i++) {
                mvwchgat(stdscr, window_top + i, window_left + window_length, 2, 0, q_white_color_pair_num, NULL);
        }
        mvwchgat(stdscr, window_top + window_height, window_left + 2, window_length, 0, q_white_color_pair_num, NULL);
} /* ---------------------------------------------------------------------- */
