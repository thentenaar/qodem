/*
 * avatar.c
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

#include "common.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "qodem.h"
#include "screen.h"
#include "scrollback.h"
#include "options.h"
#include "ansi.h"
#include "avatar.h"

/* #define DEBUG_AVATAR 1 */
#undef DEBUG_AVATAR
#ifdef DEBUG_AVATAR
#include <stdio.h>
#define AVATAR_DEBUG_FILE "debug_avatar.txt"
static FILE * AVATAR_DEBUG_FILE_HANDLE = NULL;
#endif /* DEBUG_AVATAR */

/* Scan states. */
typedef enum SCAN_STATES {
        SCAN_NONE,
        SCAN_V,
        SCAN_H_1,
        SCAN_H_2,
        SCAN_A_1,
        SCAN_Y_1,
        SCAN_Y_2,
        SCAN_Y_EMIT,
        SCAN_V_Y_1,
        SCAN_V_Y_2,
        SCAN_V_Y_3,
        SCAN_V_Y_EMIT,
        SCAN_V_M_1,
        SCAN_V_M_2,
        SCAN_V_M_3,
        SCAN_V_M_4,
        SCAN_V_JK_1,
        SCAN_V_JK_2,
        SCAN_V_JK_3,
        SCAN_V_JK_4,
        SCAN_V_JK_5,
        SCAN_ESC,
        SCAN_CSI,
        SCAN_CSI_PARAM,
        SCAN_ANSI_FALLBACK
} SCAN_STATE;

/* Current scanning state. */
static SCAN_STATE scan_state;

/* For the ^Y and ^V^Y sequences */
static unsigned char y_char;
static int y_count;
static unsigned char * v_y_chars = NULL;
static int v_y_chars_i;
static int v_y_chars_n;

/* For the ^V^J and ^V^K sequences */
static Q_BOOL v_jk_scrollup;
static int v_jk_numlines;
static int v_jk_upper;
static int v_jk_left;
static int v_jk_lower;
static int v_jk_right;


/*
 * ANSI fallback:  the unknown escape sequence is copied
 * here and then run through the ANSI emulator.
 */
static unsigned char ansi_buffer[sizeof(q_emul_buffer)];
static int ansi_buffer_n;
static int ansi_buffer_i;

/*
 * avatar_reset - reset the emulation state
 */
void avatar_reset() {
        scan_state = SCAN_NONE;
        if (v_y_chars != NULL) {
                Xfree(v_y_chars, __FILE__, __LINE__);
                v_y_chars = NULL;
        }
        v_y_chars_i = 0;
        v_y_chars_n = 0;
#ifdef DEBUG_AVATAR
        if (AVATAR_DEBUG_FILE_HANDLE == NULL) {
                AVATAR_DEBUG_FILE_HANDLE = fopen(AVATAR_DEBUG_FILE, "w");
        }
        fprintf(AVATAR_DEBUG_FILE_HANDLE, "avatar_reset()\n");
#endif /* DEBUG_AVATAR */
} /* ---------------------------------------------------------------------- */

/*
 * Reset the scan state for a new sequence
 */
static void clear_state(wchar_t * to_screen) {
        q_status.insert_mode = Q_FALSE;
        q_emul_buffer_n = 0;
        q_emul_buffer_i = 0;
        memset(q_emul_buffer, 0, sizeof(q_emul_buffer));
        scan_state = SCAN_NONE;
        *to_screen = 1;

        if (v_y_chars != NULL) {
                Xfree(v_y_chars, __FILE__, __LINE__);
                v_y_chars = NULL;
                v_y_chars_i = 0;
                v_y_chars_n = 0;
        }
} /* ---------------------------------------------------------------------- */

/*
 * Hang onto one character in the buffer
 */
static void save_char(unsigned char keep_char, wchar_t * to_screen) {
        q_emul_buffer[q_emul_buffer_n] = keep_char;
        q_emul_buffer_n++;
        *to_screen = 1;
} /* ---------------------------------------------------------------------- */

/* Originally from colors.c.
   Duplicated here because it is part of the AVATAR specification. */
static short pc_to_curses_map[] = {
        Q_COLOR_BLACK,
        Q_COLOR_BLUE,
        Q_COLOR_GREEN,
        Q_COLOR_CYAN,
        Q_COLOR_RED,
        Q_COLOR_MAGENTA,

        /* Really brown */
        Q_COLOR_YELLOW,

        /* Really light gray */
        Q_COLOR_WHITE

        /* Bold colors:
        dark gray
        light blue
        light green
        light cyan
        light red
        light magenta
        yellow
        white
        */
};

/*
 * Set the current drawing color based on PC attribute
 */
static void avatar_set_color(const unsigned char from_modem) {
        short fg;
        short bg;
        attr_t attr;

        /* Set color */
        attr = Q_A_NORMAL;
        fg = from_modem & 0x07;
        bg = (from_modem >> 4) & 0x07;
        fg = pc_to_curses_map[fg];
        bg = pc_to_curses_map[bg];
        if ((from_modem & 0x08) != 0) {
                attr |= Q_A_BOLD;
        }
        if ((from_modem & 0x80) != 0) {
                attr |= Q_A_BLINK;
        }
        q_current_color = attr | color_to_attr((fg << 3) | bg);

#ifdef DEBUG_AVATAR
        fprintf(AVATAR_DEBUG_FILE_HANDLE, "AVATAR: new color: %04x\n", (unsigned int)q_current_color);
#endif /* DEBUG_AVATAR */
} /* ---------------------------------------------------------------------- */


/*
 * avatar - process through AVATAR emulator.
 */
Q_EMULATION_STATUS avatar(const unsigned char from_modem, wchar_t * to_screen) {
        static unsigned char * count;
        static attr_t attributes;
        Q_EMULATION_STATUS rc;
        attr_t old_color;
        int old_x;
        int old_y;
        int i;

#ifdef DEBUG_AVATAR
        fprintf(AVATAR_DEBUG_FILE_HANDLE, "STATE: %d CHAR: 0x%02x '%c'\n", scan_state, from_modem, from_modem);
        fflush(AVATAR_DEBUG_FILE_HANDLE);
        /* render_screen_to_debug_file(AVATAR_DEBUG_FILE_HANDLE); */
#endif /* DEBUG_AVATAR */

avatar_start:

        switch (scan_state) {

        case SCAN_ANSI_FALLBACK:

                /*
                 * From here on out we pass through ANSI until we don't get
                 * Q_EMUL_FSM_NO_CHAR_YET.
                 */
                ansi_buffer[ansi_buffer_n] = from_modem;
                ansi_buffer_n++;

#ifdef DEBUG_AVATAR
                fprintf(AVATAR_DEBUG_FILE_HANDLE, "AVATAR: ANSI FALLBACK ansi()\n");
#endif /* DEBUG_AVATAR */

                rc = Q_EMUL_FSM_NO_CHAR_YET;
                while (rc == Q_EMUL_FSM_NO_CHAR_YET) {
                        rc = ansi(ansi_buffer[ansi_buffer_i], to_screen);

                        if (rc != Q_EMUL_FSM_NO_CHAR_YET) {
                                /* We can be ourselves again now */
#ifdef DEBUG_AVATAR
                                fprintf(AVATAR_DEBUG_FILE_HANDLE, "AVATAR: ANSI FALLBACK END\n");
#endif /* DEBUG_AVATAR */
                                scan_state = SCAN_NONE;
                        }

                        ansi_buffer_i++;
                        if (ansi_buffer_i == ansi_buffer_n) {
                                /* No more characters to send through ANSI */
                                ansi_buffer_n = 0;
                                ansi_buffer_i = 0;
                                break;
                        }
                }

                return rc;

        case SCAN_NONE:
                /* ESC */
                if (from_modem == KEY_ESCAPE) {
                        save_char(from_modem, to_screen);
                        scan_state = SCAN_ESC;
                        return Q_EMUL_FSM_NO_CHAR_YET;
                }

                /* ^V */
                if (from_modem == 0x16) {
                        save_char(from_modem, to_screen);
                        scan_state = SCAN_V;
                        return Q_EMUL_FSM_NO_CHAR_YET;
                }

                /* ^L */
                if (from_modem == 0x0C) {
#ifdef DEBUG_AVATAR
                        fprintf(AVATAR_DEBUG_FILE_HANDLE, "AVATAR: clear screen, home cursor\n");
#endif /* DEBUG_AVATAR */

                        /* Cursor position to (0,0) and erase entire screen */
                        cursor_formfeed();
                        q_current_color = Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);

                        clear_state(to_screen);
                        return Q_EMUL_FSM_NO_CHAR_YET;
                }

                /* ^Y */
                if (from_modem == 0x19) {
                        scan_state = SCAN_Y_1;
                        *to_screen = 1;
                        return Q_EMUL_FSM_NO_CHAR_YET;
                }

                /* Other control characters */
                if (iscntrl(from_modem)) {
#ifdef DEBUG_AVATAR
                        fprintf(AVATAR_DEBUG_FILE_HANDLE, "generic_handle_control_char(): control_char = 0x%02x\n", from_modem);
#endif /* DEBUG_AVATAR */
                        generic_handle_control_char(from_modem);

                        /* Consume */
                        *to_screen = 1;
                        return Q_EMUL_FSM_NO_CHAR_YET;
                }

                *to_screen = codepage_map_char(from_modem);
                return Q_EMUL_FSM_ONE_CHAR;

        case SCAN_A_1:
                /* from_modem has new color attribute */
                avatar_set_color(from_modem);

                clear_state(to_screen);
                return Q_EMUL_FSM_NO_CHAR_YET;

        case SCAN_H_1:
                save_char(from_modem, to_screen);
                scan_state = SCAN_H_2;
                return Q_EMUL_FSM_NO_CHAR_YET;

        case SCAN_H_2:
                /* from_modem has new column value */

                /* Cursor position */
#ifdef DEBUG_AVATAR
                fprintf(AVATAR_DEBUG_FILE_HANDLE, "AVATAR: cursor_position() %d %d\n", q_emul_buffer[2] - 1, from_modem - 1);
#endif /* DEBUG_AVATAR */
                old_y = q_emul_buffer[2] - 1;
                old_x = from_modem - 1;
                if (old_x < 0) {
                        old_x = 0;
                }
                if (old_y < 0) {
                        old_y = 0;
                }
                cursor_position(old_y, old_x);

                clear_state(to_screen);
                return Q_EMUL_FSM_NO_CHAR_YET;

        case SCAN_Y_1:
                y_char = from_modem;
                *to_screen = 1;
                scan_state = SCAN_Y_2;
                return Q_EMUL_FSM_NO_CHAR_YET;

        case SCAN_Y_2:
                y_count = from_modem;

#ifdef DEBUG_AVATAR
                fprintf(AVATAR_DEBUG_FILE_HANDLE, "AVATAR: RLE char '%c' count=%d\n", y_char, y_count);
#endif /* DEBUG_AVATAR */

                scan_state = SCAN_Y_EMIT;
                /* Fall through ... */
        repeat_loop:
        case SCAN_Y_EMIT:
                if (y_count == 0) {
                        if (q_status.insert_mode == Q_TRUE) {
                                /* Since clear_state() resets insert mode, we have to change it back after */
                                clear_state(to_screen);
                                q_status.insert_mode = Q_TRUE;
                        } else {
                                clear_state(to_screen);
                        }

                        return Q_EMUL_FSM_NO_CHAR_YET;
                }

                y_count--;

                /*
                 * Avatar allows repeated single characters to be control
                 * characters too.  They have to be handled but not displayed.
                 */
                if (iscntrl(y_char)) {

#ifdef DEBUG_AVATAR
                        fprintf(AVATAR_DEBUG_FILE_HANDLE, "REPEAT generic_handle_control_char(): control_char = 0x%02x\n", y_char);
#endif /* DEBUG_AVATAR */
                        generic_handle_control_char(y_char);
                        goto repeat_loop;
                }

                *to_screen = codepage_map_char(y_char);
                return Q_EMUL_FSM_MANY_CHARS;

        case SCAN_V_JK_1:
                v_jk_numlines = from_modem;
                save_char(from_modem, to_screen);
                scan_state = SCAN_V_JK_2;
                return Q_EMUL_FSM_NO_CHAR_YET;

        case SCAN_V_JK_2:
                v_jk_upper = from_modem;
                save_char(from_modem, to_screen);
                scan_state = SCAN_V_JK_3;
                return Q_EMUL_FSM_NO_CHAR_YET;

        case SCAN_V_JK_3:
                v_jk_left = from_modem;
                save_char(from_modem, to_screen);
                scan_state = SCAN_V_JK_4;
                return Q_EMUL_FSM_NO_CHAR_YET;

        case SCAN_V_JK_4:
                v_jk_lower = from_modem;
                save_char(from_modem, to_screen);
                scan_state = SCAN_V_JK_5;
                return Q_EMUL_FSM_NO_CHAR_YET;

        case SCAN_V_JK_5:
                v_jk_right = from_modem;
                /* Scroll region */
#ifdef DEBUG_AVATAR
                fprintf(AVATAR_DEBUG_FILE_HANDLE, "AVATAR: scroll_rectangle() %s %d %d %d %d %d\n",
                        (v_jk_scrollup == Q_TRUE ? "true" : "false"), v_jk_numlines,
                        v_jk_upper, v_jk_left, v_jk_lower, v_jk_right);
#endif /* DEBUG_AVATAR */
                if ((v_jk_numlines == 0) || (v_jk_numlines > HEIGHT - STATUS_HEIGHT)) {
                        erase_screen(0, 0, HEIGHT - STATUS_HEIGHT - 1, WIDTH - 1, Q_FALSE);
                } else {
                        if (v_jk_upper > 0) {
                                v_jk_upper--;
                        }
                        if (v_jk_left > 0) {
                                v_jk_left--;
                        }
                        if (v_jk_lower > 0) {
                                v_jk_lower--;
                        }
                        if (v_jk_right > 0) {
                                v_jk_right--;
                        }

                        if (v_jk_scrollup == Q_TRUE) {
                                rectangle_scroll_up(v_jk_upper, v_jk_left, v_jk_lower, v_jk_right, v_jk_numlines);
                        } else {
                                rectangle_scroll_down(v_jk_upper, v_jk_left, v_jk_lower, v_jk_right, v_jk_numlines);
                        }
                }

                clear_state(to_screen);
                return Q_EMUL_FSM_NO_CHAR_YET;

        case SCAN_V_Y_1:
                v_y_chars_n = from_modem;
                v_y_chars = (unsigned char *)Xmalloc(sizeof(unsigned char) * (v_y_chars_n + 1), __FILE__, __LINE__);
                memset(v_y_chars, 0, sizeof(unsigned char) * (v_y_chars_n + 1));
                v_y_chars_i = 0;

                scan_state = SCAN_V_Y_2;
                *to_screen = 1;
                return Q_EMUL_FSM_NO_CHAR_YET;

        case SCAN_V_Y_2:
                v_y_chars[v_y_chars_i] = from_modem;
                v_y_chars_i++;
                if (v_y_chars_i == v_y_chars_n) {
                        scan_state = SCAN_V_Y_3;
                }

                *to_screen = 1;
                return Q_EMUL_FSM_NO_CHAR_YET;

        case SCAN_V_Y_3:
                y_count = from_modem;
                v_y_chars_i = 0;

#ifdef DEBUG_AVATAR
                fprintf(AVATAR_DEBUG_FILE_HANDLE, "AVATAR: RLE pattern '%s' count=%d\n", v_y_chars, y_count);
#endif /* DEBUG_AVATAR */

                scan_state = SCAN_V_Y_EMIT;
                /* Fall through ... */

        case SCAN_V_Y_EMIT:

                /* It's possible to repeat the entire state machine...ick. */
                q_emul_repeat_state_count = v_y_chars_n * y_count;
                q_emul_repeat_state_buffer = (unsigned char *)Xmalloc(sizeof(unsigned char) * q_emul_repeat_state_count, __FILE__,__LINE__);
                while (y_count > 0) {
                        memcpy(q_emul_repeat_state_buffer + (v_y_chars_n * (y_count - 1)), v_y_chars, v_y_chars_n);
                        y_count--;
                }
                Xfree(v_y_chars, __FILE__, __LINE__);
                v_y_chars = NULL;

                if (q_status.insert_mode == Q_TRUE) {
                        /* Since clear_state() resets insert mode, we have to change it back after */
                        clear_state(to_screen);
                        q_status.insert_mode = Q_TRUE;
                } else {
                        clear_state(to_screen);
                }
                return Q_EMUL_FSM_REPEAT_STATE;

        case SCAN_V_M_1:
                save_char(from_modem, to_screen);
                scan_state = SCAN_V_M_2;
                return Q_EMUL_FSM_NO_CHAR_YET;

        case SCAN_V_M_2:
                save_char(from_modem, to_screen);
                scan_state = SCAN_V_M_3;
                return Q_EMUL_FSM_NO_CHAR_YET;

        case SCAN_V_M_3:
                save_char(from_modem, to_screen);
                scan_state = SCAN_V_M_4;
                return Q_EMUL_FSM_NO_CHAR_YET;

        case SCAN_V_M_4:
                /*
                 * q_emul_buffer contains all the parameters:
                 *
                 * ^M
                 * attr
                 * lines
                 * cols
                 */

#ifdef DEBUG_AVATAR
                assert(q_emul_buffer_n >= 5);
                fprintf(AVATAR_DEBUG_FILE_HANDLE, "AVATAR: clear area char='%c' attr=%02x lines=%d cols=%d\n",
                        q_emul_buffer[2],
                        q_emul_buffer[3],
                        q_emul_buffer[4],
                        from_modem);
#endif /* DEBUG_AVATAR */

                old_color = q_current_color;
                old_x = q_status.cursor_x;
                old_y = q_status.cursor_y;

                for (i = 0; i < from_modem; i++) {
                        avatar_set_color(q_emul_buffer[3]);
                        fill_line_with_character(q_status.cursor_x, q_status.cursor_x + q_emul_buffer[4], q_emul_buffer[2], Q_FALSE);
                        q_current_color = old_color;
                        if (q_status.cursor_y <= HEIGHT - STATUS_HEIGHT - 1) {
                                cursor_down(1, Q_FALSE);
                        }
                }

                q_current_color = old_color;
                cursor_position(old_y, old_x);

                clear_state(to_screen);
                return Q_EMUL_FSM_NO_CHAR_YET;

        case SCAN_V:

                /* ^A */
                if (from_modem == 0x01) {
                        save_char(from_modem, to_screen);
                        scan_state = SCAN_A_1;
                        return Q_EMUL_FSM_NO_CHAR_YET;
                }

                /* ^B */
                if (from_modem == 0x02) {
                        /* Blink on */
                        q_current_color |= Q_A_BLINK;

                        clear_state(to_screen);
                        return Q_EMUL_FSM_NO_CHAR_YET;
                }

                /* ^C */
                if (from_modem == 0x03) {
                        /* Cursor up */
                        cursor_up(1, Q_FALSE);
                        clear_state(to_screen);
                        return Q_EMUL_FSM_NO_CHAR_YET;
                }

                /* ^D */
                if (from_modem == 0x04) {
                        /* Cursor down */
                        cursor_down(1, Q_FALSE);
                        clear_state(to_screen);
                        return Q_EMUL_FSM_NO_CHAR_YET;
                }

                /* ^E */
                if (from_modem == 0x05) {
                        /* Cursor left */
                        cursor_left(1, Q_FALSE);
                        clear_state(to_screen);
                        return Q_EMUL_FSM_NO_CHAR_YET;
                }

                /* ^F */
                if (from_modem == 0x06) {
                        /* Cursor right */
                        cursor_right(1, Q_FALSE);
                        clear_state(to_screen);
                        return Q_EMUL_FSM_NO_CHAR_YET;
                }

                /* ^G */
                if (from_modem == 0x07) {
                        /* Erase from here to end of line */
                        erase_line(q_status.cursor_x, WIDTH - 1, Q_FALSE);

                        clear_state(to_screen);
                        return Q_EMUL_FSM_NO_CHAR_YET;
                }

                /* ^H */
                if (from_modem == 0x08) {
                        /* Cursor position */
                        save_char(from_modem, to_screen);
                        scan_state = SCAN_H_1;
                        return Q_EMUL_FSM_NO_CHAR_YET;
                }

                /* ^I */
                if (from_modem == 0x09) {
                        /* Clear first */
                        clear_state(to_screen);

                        /* Enable insert mode */
                        q_status.insert_mode = Q_TRUE;

                        return Q_EMUL_FSM_NO_CHAR_YET;
                }

                /* ^J or ^K */
                if ((from_modem == 0x0a) || (from_modem == 0x0b)) {
                        if (from_modem == 0x0a) {
                                v_jk_scrollup = Q_TRUE;
                        } else {
                                v_jk_scrollup = Q_FALSE;
                        }
                        save_char(from_modem, to_screen);
                        save_char(' ', to_screen);
                        scan_state = SCAN_V_JK_1;
                        return Q_EMUL_FSM_NO_CHAR_YET;
                }

                /* ^L */
                if (from_modem == 0x0c) {
                        save_char(from_modem, to_screen);
                        save_char(' ', to_screen);
                        scan_state = SCAN_V_M_2;
                        return Q_EMUL_FSM_NO_CHAR_YET;
                }

                /* ^M */
                if (from_modem == 0x0d) {
                        save_char(from_modem, to_screen);
                        scan_state = SCAN_V_M_1;
                        return Q_EMUL_FSM_NO_CHAR_YET;
                }

                /* ^N */
                if (from_modem == 0x0e) {
                        /* Delete char */
                        delete_character(1);

                        clear_state(to_screen);
                        return Q_EMUL_FSM_NO_CHAR_YET;
                }

                /* ^P */
                if (from_modem == 0x10) {
                        /* Disable insert mode */
                        q_status.insert_mode = Q_FALSE;

                        clear_state(to_screen);
                        return Q_EMUL_FSM_NO_CHAR_YET;
                }

                /* ^Y */
                if (from_modem == 0x19) {
                        scan_state = SCAN_V_Y_1;

                        return Q_EMUL_FSM_NO_CHAR_YET;
                }

                break;

        case SCAN_ESC:

                if (from_modem == '[') {
                        if (q_status.avatar_color == Q_TRUE) {
                                /* Fall into SCAN_CSI only if AVATAR_COLOR is enabled. */
                                save_char(from_modem, to_screen);
                                scan_state = SCAN_CSI;
                                return Q_EMUL_FSM_NO_CHAR_YET;
                        }
                }
                break;

        case SCAN_CSI:
                /*
                 * We are only going to support CSI Pn [ ; Pn ... ] m
                 * a.k.a. ANSI Select Graphics Rendition.
                 * We can see only a digit or 'm'.
                 */
                if (isdigit(from_modem)) {
                        /* Save the position for the counter */
                        count = q_emul_buffer + q_emul_buffer_n;
                        save_char(from_modem, to_screen);
                        scan_state = SCAN_CSI_PARAM;
                        return Q_EMUL_FSM_NO_CHAR_YET;
                }

                if (from_modem == 'm') {
                        /* ESC [ m mean ESC [ 0 m, all attributes off */
                        q_current_color = Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);

                        clear_state(to_screen);
                        return Q_EMUL_FSM_NO_CHAR_YET;
                }
                break;

        case SCAN_CSI_PARAM:
                /*
                 * Following through on the SGR code, we are now looking only
                 * for a digit, semicolon, or 'm'
                 *
                 */
                if ((isdigit(from_modem)) || (from_modem == ';')) {
                        save_char(from_modem, to_screen);
                        scan_state = SCAN_CSI_PARAM;
                        return Q_EMUL_FSM_NO_CHAR_YET;
                }

                if (from_modem == 'm') {
#ifdef DEBUG_AVATAR
                        fprintf(AVATAR_DEBUG_FILE_HANDLE, "AVATAR: ANSI SGR: change text attributes\n");
#endif /* DEBUG_AVATAR */
                        /* Text attributes */
                        if (ansi_color(&attributes, &count) == Q_TRUE) {
                                q_current_color = attributes;
                        } else {
                                break;
                        }

                        clear_state(to_screen);
                        return Q_EMUL_FSM_NO_CHAR_YET;
                }

                break;

        } /* switch (scan_state) */

#if 1
        /* NORMAL: process through ANSI fallback code */

        /*
         * This is UGLY AS HELL, but lots of BBSes assume that Avatar
         * emulators will "fallback" to ANSI for sequences they don't
         * understand.
         */
        scan_state = SCAN_ANSI_FALLBACK;

#ifdef DEBUG_AVATAR
        fprintf(AVATAR_DEBUG_FILE_HANDLE, "AVATAR: ANSI FALLBACK BEGIN\n");
#endif /* DEBUG_AVATAR */

        /*
         * From here on out we pass through ANSI until we don't get
         * Q_EMUL_FSM_NO_CHAR_YET.
         */
        memcpy(ansi_buffer, q_emul_buffer, q_emul_buffer_n);
        ansi_buffer_i = 0;
        ansi_buffer_n = q_emul_buffer_n;
        q_emul_buffer_i = 0;
        q_emul_buffer_n = 0;

#ifdef DEBUG_AVATAR
        fprintf(AVATAR_DEBUG_FILE_HANDLE, "AVATAR: ANSI FALLBACK ansi()\n");
#endif /* DEBUG_AVATAR */

        /* Run through the emulator again */
        goto avatar_start;

#else

        /* DEBUG:  crap out on the unknown sequence */

#ifdef DEBUG_AVATAR
        fprintf(AVATAR_DEBUG_FILE_HANDLE, "AVATAR: NO IDEA WHAT TO DO!\n");
#endif /* DEBUG_AVATAR */

        clear_state(to_screen);
        return Q_EMUL_FSM_NO_CHAR_YET;

#endif

        /* Should never get here */
        return Q_EMUL_FSM_NO_CHAR_YET;
} /* ---------------------------------------------------------------------- */
