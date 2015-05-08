/*
 * emulation.c
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

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "qodem.h"
#include "screen.h"
#include "forms.h"
#include "ansi.h"
#include "vt52.h"
#include "vt100.h"
#include "linux.h"
#include "avatar.h"
#include "debug.h"
#include "keyboard.h"
#include "states.h"
#include "options.h"
#include "console.h"
#include "help.h"

/* Local buffer for multiple returned characters. */
unsigned char q_emul_buffer[128];
int q_emul_buffer_n;
int q_emul_buffer_i;

/* Last returned state. */
static Q_EMULATION_STATUS last_state;

/* Some emulations need to wrap at special places */
int q_emulation_right_margin = -1;

/* The total number of bytes received on this connection. */
unsigned long q_connection_bytes_received;

/*
 * Avatar has a special command that requires the entire state machine
 * be re-run.  It is the responsibility of an emulation to set these
 * two variables and then return Q_EMUL_FSM_REPEAT_STATE.
 * terminal_emulator() will free it afterwards.
 */
unsigned char * q_emul_repeat_state_buffer = NULL;
int q_emul_repeat_state_count;

/*
 * emulation_from_string - return the emulation enum from the string
 */
Q_EMULATION emulation_from_string(const char * string) {

        if (strncmp(string, "TTY", sizeof("TTY")) == 0) {
                return Q_EMUL_TTY;
        } else if (strncmp(string, "ANSI", sizeof("ANSI")) == 0) {
                return Q_EMUL_ANSI;
        } else if (strncmp(string, "AVATAR", sizeof("AVATAR")) == 0) {
                return Q_EMUL_AVATAR;
        } else if (strncmp(string, "VT52", sizeof("VT52")) == 0) {
                return Q_EMUL_VT52;
        } else if (strncmp(string, "VT100", sizeof("VT100")) == 0) {
                return Q_EMUL_VT100;
        } else if (strncmp(string, "VT102", sizeof("VT102")) == 0) {
                return Q_EMUL_VT102;
        } else if (strncmp(string, "VT220", sizeof("VT220")) == 0) {
                return Q_EMUL_VT220;
        } else if (strncmp(string, "LINUX", sizeof("LINUX")) == 0) {
                return Q_EMUL_LINUX;
        } else if (strncmp(string, "L_UTF8", sizeof("L_UTF8")) == 0) {
                return Q_EMUL_LINUX_UTF8;
        } else if (strncmp(string, "XTERM", sizeof("XTERM")) == 0) {
                return Q_EMUL_XTERM;
        } else if (strncmp(string, "X_UTF8", sizeof("X_UTF8")) == 0) {
                return Q_EMUL_XTERM_UTF8;
        } else if (strncmp(string, "DEBUG", sizeof("DEBUG")) == 0) {
                return Q_EMUL_DEBUG;
        }

        return -1;
} /* ---------------------------------------------------------------------- */

/*
 * emulation_string - return the string representing the current emulation
 */
char * emulation_string(const Q_EMULATION emulation) {

        switch (emulation) {
        case Q_EMUL_TTY:
                return "TTY";

        case Q_EMUL_ANSI:
                return "ANSI";

        case Q_EMUL_AVATAR:
                return "AVATAR";

        case Q_EMUL_VT52:
                return "VT52";

        case Q_EMUL_VT100:
                return "VT100";

        case Q_EMUL_VT102:
                return "VT102";

        case Q_EMUL_VT220:
                return "VT220";

        case Q_EMUL_LINUX:
                return "LINUX";

        case Q_EMUL_LINUX_UTF8:
                return "L_UTF8";

        case Q_EMUL_XTERM:
                return "XTERM";

        case Q_EMUL_XTERM_UTF8:
                return "X_UTF8";

        case Q_EMUL_DEBUG:
                return "DEBUG";

        }

        /* Never get here */
        assert(1 == 0);
        return NULL;
} /* ---------------------------------------------------------------------- */

/*
 * reset_emulation - reset the emulation state
 */
void reset_emulation() {
        q_emul_buffer_n = 0;
        q_emul_buffer_i = 0;
        memset(q_emul_buffer, 0, sizeof(q_emul_buffer));
        last_state = Q_EMUL_FSM_NO_CHAR_YET;

        if (q_emul_repeat_state_buffer != NULL) {
                Xfree(q_emul_repeat_state_buffer, __FILE__, __LINE__);
                q_emul_repeat_state_buffer = NULL;
        }

        q_current_color = Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);

        /* Reset ALL of the emulators */
        ansi_reset();
        vt52_reset();
        avatar_reset();
        vt100_reset();
        linux_reset();
        debug_reset();
        q_emulation_right_margin        = -1;
        q_status.scroll_region_top      = 0;
        q_status.scroll_region_bottom   = HEIGHT - STATUS_HEIGHT - 1;
        q_status.reverse_video          = Q_FALSE;
        q_status.origin_mode            = Q_FALSE;

        switch (q_status.emulation) {
        case Q_EMUL_LINUX:
        case Q_EMUL_LINUX_UTF8:
                /* LINUX emulation specifies that backspace is DEL. */
                q_status.hard_backspace = Q_FALSE;
                break;

        case Q_EMUL_VT220:
        case Q_EMUL_XTERM:
        case Q_EMUL_XTERM_UTF8:
                /* VT220 style emulations tend to use DEL */
                q_status.hard_backspace = Q_FALSE;
                break;

        case Q_EMUL_ANSI:
        case Q_EMUL_AVATAR:
        case Q_EMUL_VT52:
        case Q_EMUL_VT100:
        case Q_EMUL_VT102:
        case Q_EMUL_TTY:
        case Q_EMUL_DEBUG:
                q_status.hard_backspace = Q_TRUE;
                break;
        }

} /* ---------------------------------------------------------------------- */

/*
 * Handle a control character function (C0 and C1 in the ECMA/ANSI spec)
 */
void generic_handle_control_char(const unsigned char control_char) {

        /* Handle control characters */
        switch (control_char) {
        case 0x05:
                /* ENQ */
                /* Transmit the answerback message. */
                qodem_write(q_child_tty_fd, get_option(Q_OPTION_ENQ_ANSWERBACK), strlen(get_option(Q_OPTION_ENQ_ANSWERBACK)), Q_TRUE);
                break;

        case 0x07:
                /* BEL */
                screen_beep();
                break;

        case 0x08:
                /* BS */
                cursor_left(1, Q_FALSE);
                break;

        case 0x09:
                /* HT */
                while (q_status.cursor_x < 80) {
                        print_character(' ');
                        if (q_status.cursor_x % 8 == 0) {
                                break;
                        }
                }
                break;

        case 0x0A:
                /* LF */
                cursor_linefeed(Q_FALSE);
                break;

        case 0x0B:
                /* VT */
                cursor_linefeed(Q_FALSE);
                break;

        case 0x0C:
                /* FF */
                /*
                 * In VT100 land form feed is the same as vertical tab.
                 *
                 * In PC-DOS land form feed clears the screen and homes
                 * the cursor.
                 */
                cursor_formfeed();
                break;

        case 0x0D:
                /* CR */
                cursor_carriage_return();
                break;

        case 0x0E:
                /* SO */
                /* Fall through... */
        case 0x0F:
                /* SI */
                /* Fall through... */
        default:
                /*
                 * This is probably a CP437 glyph.
                 */
                print_character(cp437_chars[control_char]);
                break;
        }
} /* ---------------------------------------------------------------------- */

/*
 * tty - process through TTY emulator.
 */
static Q_EMULATION_STATUS tty(const unsigned char from_modem, wchar_t * to_screen) {

        /* Handle control characters */
        switch (from_modem) {
        case 0x05:
                /* ENQ */
                /* Transmit the answerback message. */
                qodem_write(q_child_tty_fd, get_option(Q_OPTION_ENQ_ANSWERBACK), strlen(get_option(Q_OPTION_ENQ_ANSWERBACK)), Q_TRUE);
                break;

        case 0x07:
                /* BEL */
                screen_beep();
                break;

        case 0x08:
                /* BS */
                cursor_left(1, Q_FALSE);
                break;

        case 0x09:
                /* HT */
                while (q_status.cursor_x < 80) {
                        print_character(' ');
                        if (q_status.cursor_x % 8 == 0) {
                                break;
                        }
                }
                break;

        case 0x0A:
                /* LF */
                cursor_linefeed(Q_FALSE);
                break;

        case 0x0B:
                /* VT */
                cursor_linefeed(Q_FALSE);
                break;

        case 0x0C:
                /* FF */
                cursor_linefeed(Q_FALSE);
                break;

        case 0x0D:
                /* CR */
                cursor_carriage_return();
                break;

        case 0x0E:
                /* SO */
                break;

        case 0x0F:
                /* SI */
                break;

        case '_':
                /* One special case: underscores.  TTY emulation will turn on
                 * A_UNDERLINE if a character already exists here.
                 *
                 * Yeah, it's probably overkill.  But it's the closest thing
                 * to a "faithful" reproduction of a forty-year-old standard
                 * I can manage.  :)
                 */
                if (q_status.emulation == Q_EMUL_TTY) {
                        if (q_scrollback_current->chars[q_status.cursor_x] != ' ') {
                                q_scrollback_current->colors[q_status.cursor_x] |= Q_A_UNDERLINE;
                                q_status.cursor_x++;
                                break;
                        }
                }

                /* Else fall through...*/
        default:
                /* Return to screen */
                *to_screen = codepage_map_char(from_modem);
                return Q_EMUL_FSM_ONE_CHAR;
        }

        /* Consume */
        *to_screen = 1;
        return Q_EMUL_FSM_NO_CHAR_YET;
} /* ---------------------------------------------------------------------- */

/*
 * terminal_emulator - process through terminal emulator.
 */
Q_EMULATION_STATUS terminal_emulator(const unsigned char from_modem, wchar_t * to_screen) {
        int i;

        /* Junk extraneous data */
        if (q_emul_buffer_n >= sizeof(q_emul_buffer) - 1) {
                q_emul_buffer_n = 0;
                q_emul_buffer_i = 0;
                memset(q_emul_buffer, 0, sizeof(q_emul_buffer));
                last_state = Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (last_state == Q_EMUL_FSM_MANY_CHARS) {

                if (q_status.emulation == Q_EMUL_AVATAR) {
                        /* Avatar has its own logic for RLE strings */
                        last_state = avatar(from_modem, to_screen);
                        return last_state;
                } else {
                        /* Everybody else just dumps the string to q_emul_buffer */
                        if (q_emul_buffer_n == 0) {
                                /* We just emitted the last character */
                                last_state = Q_EMUL_FSM_NO_CHAR_YET;
                                *to_screen = 0;
                                return last_state;
                        }

                        *to_screen = codepage_map_char(q_emul_buffer[q_emul_buffer_i]);
                        q_emul_buffer_i++;
                        if (q_emul_buffer_i == q_emul_buffer_n) {
                                /* This is the last character */
                                q_emul_buffer_n = 0;
                                q_emul_buffer_i = 0;
                                memset(q_emul_buffer, 0, sizeof(q_emul_buffer));
                        }
                }
                return Q_EMUL_FSM_MANY_CHARS;
        }

        /* A new character has arrived.  Increase the byte counter. */
        q_connection_bytes_received++;

        /*
         * VT100 scrolling regions require that the vt100() function
         * sees these characters.
         *
         * DEBUG emulation also performs its own CR/LF handling.
         *
         * AVATAR uses the other control characters as its codes, so we
         * can't process them here.
         */
        if ((q_status.emulation != Q_EMUL_VT100) &&
            (q_status.emulation != Q_EMUL_VT102) &&
            (q_status.emulation != Q_EMUL_VT220) &&
            (q_status.emulation != Q_EMUL_LINUX) &&
            (q_status.emulation != Q_EMUL_LINUX_UTF8) &&
            (q_status.emulation != Q_EMUL_XTERM) &&
            (q_status.emulation != Q_EMUL_XTERM_UTF8) &&
            (q_status.emulation != Q_EMUL_AVATAR) &&
            (q_status.emulation != Q_EMUL_DEBUG)
        ) {
                if (from_modem == C_CR) {
                        cursor_carriage_return();
                        *to_screen = 1;
                        return Q_EMUL_FSM_NO_CHAR_YET;
                } else if (from_modem == C_LF) {
                        cursor_linefeed(Q_FALSE);
                        *to_screen = 1;
                        return Q_EMUL_FSM_NO_CHAR_YET;
                }
        }

        switch (q_status.emulation) {
        case Q_EMUL_ANSI:
                last_state = ansi(from_modem, to_screen);
                break;
        case Q_EMUL_VT52:
                last_state = vt52(from_modem, to_screen);
                break;
        case Q_EMUL_AVATAR:
                last_state = avatar(from_modem, to_screen);
                break;
        case Q_EMUL_VT100:
        case Q_EMUL_VT102:
        case Q_EMUL_VT220:
                last_state = vt100(from_modem, to_screen);
                break;
        case Q_EMUL_TTY:
                last_state = tty(from_modem, to_screen);
                break;
        case Q_EMUL_LINUX:
        case Q_EMUL_LINUX_UTF8:
        case Q_EMUL_XTERM:
        case Q_EMUL_XTERM_UTF8:
                last_state = linux_emulator(from_modem, to_screen);
                break;
        case Q_EMUL_DEBUG:
                last_state = debug_emulator(from_modem, to_screen);
                break;
        }

        if (last_state == Q_EMUL_FSM_REPEAT_STATE) {

                for (i = 0; i < q_emul_repeat_state_count; i++) {

                        switch (q_status.emulation) {
                        case Q_EMUL_ANSI:
                                last_state = ansi(q_emul_repeat_state_buffer[i], to_screen);
                                break;
                        case Q_EMUL_VT52:
                                last_state = vt52(q_emul_repeat_state_buffer[i], to_screen);
                                break;
                        case Q_EMUL_AVATAR:
                                last_state = avatar(q_emul_repeat_state_buffer[i], to_screen);
                                break;
                        case Q_EMUL_VT100:
                        case Q_EMUL_VT102:
                        case Q_EMUL_VT220:
                                last_state = vt100(q_emul_repeat_state_buffer[i], to_screen);
                                break;
                        case Q_EMUL_TTY:
                                last_state = tty(q_emul_repeat_state_buffer[i], to_screen);
                                break;
                        case Q_EMUL_LINUX:
                        case Q_EMUL_LINUX_UTF8:
                        case Q_EMUL_XTERM:
                        case Q_EMUL_XTERM_UTF8:
                                last_state = linux_emulator(q_emul_repeat_state_buffer[i], to_screen);
                                break;
                        case Q_EMUL_DEBUG:
                                last_state = debug_emulator(q_emul_repeat_state_buffer[i], to_screen);
                                break;
                        }

                        /* Ugly hack, this should be console */
                        if (last_state == Q_EMUL_FSM_ONE_CHAR) {
                                /* Print this character */
                                print_character(codepage_map_char(*to_screen));
                        }

                }

                Xfree(q_emul_repeat_state_buffer, __FILE__, __LINE__);
                q_emul_repeat_state_buffer = NULL;

                *to_screen = 1;
                last_state = Q_EMUL_FSM_NO_CHAR_YET;
        }

        return last_state;
} /* ---------------------------------------------------------------------- */

/*
 * emulation_menu_refresh
 */
void emulation_menu_refresh() {
        char * status_string;
        int status_left_stop;
        char * message;
        int message_left;
        int window_left;
        int window_top;
        int window_height = 18;
        int window_length;

        if (q_screen_dirty == Q_FALSE) {
                return;
        }

        /* Clear screen for when it resizes */
        console_refresh(Q_FALSE);

        /* Put up the status line */
        screen_put_color_hline_yx(HEIGHT - 1, 0, cp437_chars[HATCH], WIDTH, Q_COLOR_STATUS);

        status_string = _(" LETTER-Select an Emulation   ESC/`-Exit ");
        status_left_stop = WIDTH - strlen(status_string);
        if (status_left_stop <= 0) {
                status_left_stop = 0;
        } else {
                status_left_stop /= 2;
        }
        screen_put_color_str_yx(HEIGHT - 1, status_left_stop, status_string, Q_COLOR_STATUS);

        window_length = 20;

        /* Add room for border + 1 space on each side */
        window_length += 4;

        /* Window will be centered on the screen */
        window_left = WIDTH - 1 - window_length;
        if (window_left < 0) {
                window_left = 0;
        } else {
                window_left /= 2;
        }
        window_top = HEIGHT - 1 - window_height;
        if (window_top < 0) {
                window_top = 0;
        } else {
                window_top /= 10;
        }

        /* Draw the sub-window */
        screen_draw_box(window_left, window_top, window_left + window_length, window_top + window_height);

        /* Place the title */
        message = _("Set Emulation");
        message_left = window_length - (strlen(message) + 2);
        if (message_left < 0) {
                message_left = 0;
        } else {
                message_left /= 2;
        }
        screen_put_color_printf_yx(window_top + 0, window_left + message_left, Q_COLOR_WINDOW_BORDER, " %s ", message);

        /* Add the "F1 Help" part */
        screen_put_color_str_yx(window_top + window_height - 1, window_left + window_length - 10, _("F1 Help"), Q_COLOR_WINDOW_BORDER);

        screen_put_color_str_yx(window_top + 1, window_left + 2, _("Emulation is "), Q_COLOR_MENU_TEXT);
        screen_put_color_printf(Q_COLOR_MENU_COMMAND, "%s", emulation_string(q_status.emulation));

        screen_put_color_str_yx(window_top + 3, window_left + 7, "A", Q_COLOR_MENU_COMMAND);
        screen_put_color_printf(Q_COLOR_MENU_TEXT, "  TTY");
        screen_put_color_str_yx(window_top + 4, window_left + 7, "B", Q_COLOR_MENU_COMMAND);
        screen_put_color_printf(Q_COLOR_MENU_TEXT, "  ANSI");
        screen_put_color_str_yx(window_top + 5, window_left + 7, "C", Q_COLOR_MENU_COMMAND);
        screen_put_color_printf(Q_COLOR_MENU_TEXT, "  AVATAR");
        screen_put_color_str_yx(window_top + 6, window_left + 7, "D", Q_COLOR_MENU_COMMAND);
        screen_put_color_printf(Q_COLOR_MENU_TEXT, "  VT52");
        screen_put_color_str_yx(window_top + 7, window_left + 7, "E", Q_COLOR_MENU_COMMAND);
        screen_put_color_printf(Q_COLOR_MENU_TEXT, "  VT100");
        screen_put_color_str_yx(window_top + 8, window_left + 7, "F", Q_COLOR_MENU_COMMAND);
        screen_put_color_printf(Q_COLOR_MENU_TEXT, "  VT102");
        screen_put_color_str_yx(window_top + 9, window_left + 7, "G", Q_COLOR_MENU_COMMAND);
        screen_put_color_printf(Q_COLOR_MENU_TEXT, "  VT220");
        screen_put_color_str_yx(window_top + 10, window_left + 7, "L", Q_COLOR_MENU_COMMAND);
        screen_put_color_printf(Q_COLOR_MENU_TEXT, "  LINUX");
        screen_put_color_str_yx(window_top + 11, window_left + 7, "T", Q_COLOR_MENU_COMMAND);
        screen_put_color_printf(Q_COLOR_MENU_TEXT, "  LINUX UTF-8");
        screen_put_color_str_yx(window_top + 12, window_left + 7, "X", Q_COLOR_MENU_COMMAND);
        screen_put_color_printf(Q_COLOR_MENU_TEXT, "  XTERM");
        screen_put_color_str_yx(window_top + 13, window_left + 7, "8", Q_COLOR_MENU_COMMAND);
        screen_put_color_printf(Q_COLOR_MENU_TEXT, "  XTERM UTF-8");
        screen_put_color_str_yx(window_top + 14, window_left + 7, "U", Q_COLOR_MENU_COMMAND);
        screen_put_color_printf(Q_COLOR_MENU_TEXT, "  DEBUG");

        /* Prompt */
        screen_put_color_str_yx(window_top + 16, window_left + 2, _("Your Choice ? "), Q_COLOR_MENU_COMMAND);

        screen_flush();
        q_screen_dirty = Q_FALSE;
} /* ---------------------------------------------------------------------- */

/*
 * emulation_menu_keyboard_handler
 */
void emulation_menu_keyboard_handler(const int keystroke, const int flags) {
        int new_keystroke;

        /* Default to ANSI */
        Q_EMULATION new_emulation = Q_EMUL_ANSI;

        switch (keystroke) {
        case 'A':
        case 'a':
                new_emulation = Q_EMUL_TTY;
                break;

        case 'B':
        case 'b':
                new_emulation = Q_EMUL_ANSI;
                break;

        case 'C':
        case 'c':
                new_emulation = Q_EMUL_AVATAR;
                break;

        case 'D':
        case 'd':
                new_emulation = Q_EMUL_VT52;
                break;

        case 'E':
        case 'e':
                new_emulation = Q_EMUL_VT100;
                break;

        case 'F':
        case 'f':
                new_emulation = Q_EMUL_VT102;
                break;

        case 'G':
        case 'g':
                new_emulation = Q_EMUL_VT220;
                break;

        case 'L':
        case 'l':
                new_emulation = Q_EMUL_LINUX;
                break;

        case 'T':
        case 't':
                new_emulation = Q_EMUL_LINUX_UTF8;
                break;

        case 'X':
        case 'x':
                new_emulation = Q_EMUL_XTERM;
                break;

        case '8':
                new_emulation = Q_EMUL_XTERM_UTF8;
                break;

        case 'U':
        case 'u':
                new_emulation = Q_EMUL_DEBUG;
                break;

        case Q_KEY_F(1):
                launch_help(Q_HELP_EMULATION_MENU);

                /* Refresh the whole screen. */
                console_refresh(Q_FALSE);
                q_screen_dirty = Q_TRUE;
                return;

        case '`':
                /* Backtick works too */
        case KEY_ESCAPE:
                /* ESC return to TERMINAL mode */
                switch_state(Q_STATE_CONSOLE);

                /* The ABORT exit point */
                return;

        default:
                /* Ignore keystroke */
                return;
        }

        if (new_emulation == q_status.emulation) {
                /* Ask for emulation reset */
                /* Weird.  I had the last parameter set to
                 * "0" and GCC didn't turn it into a double
                 * that equaled 0.  I have to explicitly pass
                 * 0.0 here.  But for some reason I didn't have
                 * this behavior in console.c ?
                 */
                new_keystroke = tolower(notify_prompt_form(
                        _("Emulation"),
                        _("Reset Current Emulation? [y/N] "),
                        _(" Y-Reset Emulation   N-Exit "),
                        Q_TRUE,
                        0.0, "YyNn\r"));

                /* Reset only if the user said so */
                if (new_keystroke == 'y') {
                        /*
                         * If we're finishing off DEBUG emulation, call debug_finish()
                         * to get the printable characters in the capture file.
                         */
                        if (q_status.emulation == Q_EMUL_DEBUG) {
                                debug_finish();
                        }
                        reset_emulation();
                }
        } else {
                /*
                 * If we're finishing off DEBUG emulation, call debug_finish()
                 * to get the printable characters in the capture file.
                 */
                if (q_status.emulation == Q_EMUL_DEBUG) {
                        debug_finish();
                }
                q_status.emulation = new_emulation;
                reset_emulation();

                /* Switch the keyboard to the current emulation keyboard */
                switch_current_keyboard("");
        }

        /* Set the right codepage */
        q_status.codepage = default_codepage(q_status.emulation);

        /* The OK exit point */
        switch_state(Q_STATE_CONSOLE);

} /* ---------------------------------------------------------------------- */

Q_CODEPAGE default_codepage(Q_EMULATION emulation) {
        /* Set the right codepage */
        switch (emulation) {
        case Q_EMUL_TTY:
                return Q_CODEPAGE_ISO8859_1;
        case Q_EMUL_VT52:
        case Q_EMUL_VT100:
        case Q_EMUL_VT102:
        case Q_EMUL_VT220:
        case Q_EMUL_LINUX_UTF8:
        case Q_EMUL_XTERM_UTF8:
                return Q_CODEPAGE_DEC;
        case Q_EMUL_DEBUG:
        case Q_EMUL_ANSI:
        case Q_EMUL_AVATAR:
        case Q_EMUL_LINUX:
        case Q_EMUL_XTERM:
                return Q_CODEPAGE_CP437;
        }

        /* BUG: should never get here */
        abort();
} /* ---------------------------------------------------------------------- */

