/*
 * linux.c
 *
 * This parser tries to emulate as close as possible the state diagram
 * described by Paul Williams at http://vt100.net/emu/dec_ansi_parser.
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
 * The Linux console is different enough that I've created a separate
 * state machine for it.  Besides a few other ANSI codes, and color!,
 * the Linux console driver is a way to communicate with the kernel:
 * you can select different virtual screens, cause beeps/music to
 * sound on the speaker, control VESA screen blanking, etc.
 *
 * One day I'd like to cleanly handle those functions inside Qodem, so
 * that for instance a request to alter the palette could change the
 * REAL palette.
 */
#include "common.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>     /* memset() */
#include "qodem.h"
#include "screen.h"
#include "options.h"
#include "netclient.h"
#include "linux.h"

/* #define DEBUG_LINUX_UTF8 1 */
#undef DEBUG_LINUX_UTF8

/* #define DEBUG_LINUX 1 */
#undef DEBUG_LINUX

#ifdef DEBUG_LINUX_UTF8
#define DEBUG_LINUX 1
#endif /* DEBUG_LINUX_UTF8 */
#ifdef DEBUG_LINUX
#define LINUX_DEBUG_FILE "debug_linux.txt"
static FILE * LINUX_DEBUG_FILE_HANDLE = NULL;
#endif /* DEBUG_LINUX */

/* Whether arrow keys send ANSI or VT100 sequences.  The default is
   false, meaning use VT100 arrow keys. */
Q_EMULATION q_linux_arrow_keys;

/* When true, VT100 new line mode is set */
/* If true, cursor_linefeed() puts the cursor on the first
   column of the next line.  If false, cursor_linefeed() puts
   the cursor one line down on the current line.  The default
   is false. */
Q_BOOL q_linux_new_line_mode;

/*
 * The linux defaults are in drivers/char/console.c, as of 2.4.22 it's
 * 750 Hz 250 milliseconds.
 */
#define DEFAULT_BEEP_FREQUENCY  750
#define DEFAULT_BEEP_DURATION   250

/* Used by qodem_beep() */
int q_linux_beep_frequency      = DEFAULT_BEEP_FREQUENCY;
int q_linux_beep_duration       = DEFAULT_BEEP_DURATION;

/* Use by handle_mouse() */
XTERM_MOUSE_PROTOCOL q_xterm_mouse_protocol = XTERM_MOUSE_OFF;
XTERM_MOUSE_ENCODING q_xterm_mouse_encoding = XTERM_MOUSE_ENCODING_X10;

/* Whether number pad keys send VT100 or VT52, application or numeric sequences. */
struct q_keypad_mode q_linux_keypad_mode = {
        Q_EMUL_VT100,
        Q_KEYPAD_MODE_NUMERIC
};

/* Scan states. */
typedef enum SCAN_STATES {
        SCAN_GROUND,
        SCAN_ESCAPE,
        SCAN_ESCAPE_INTERMEDIATE,
        SCAN_CSI_ENTRY,
        SCAN_CSI_PARAM,
        SCAN_CSI_INTERMEDIATE,
        SCAN_CSI_IGNORE,
        SCAN_DCS_ENTRY,
        SCAN_DCS_INTERMEDIATE,
        SCAN_DCS_PARAM,
        SCAN_DCS_PASSTHROUGH,
        SCAN_DCS_IGNORE,
        SCAN_SOSPMAPC_STRING,
        SCAN_OSC_STRING,
        SCAN_VT52_DIRECT_CURSOR_ADDRESS
} SCAN_STATE;

/* Current scanning state. */
static SCAN_STATE scan_state;

#define VT100_PARAM_LENGTH      16
#define VT100_PARAM_MAX         16
#define VT100_RESPONSE_LENGTH   32

/* "I am a VT102" */
#define LINUX_DEVICE_TYPE_STRING        "\033[?6c"

/* Available character sets */
typedef enum {
        CHARSET_US,
        CHARSET_UK,
        CHARSET_DRAWING,
        CHARSET_ROM,
        CHARSET_ROM_SPECIAL
} VT100_CHARACTER_SET;

/* Rather than have a bunch of globals, this one struct
   contains the state of the VT100. */
struct linux_state {

        /* Wide char to return for Q_EMUL_LINUX_UTF8 or Q_EMUL_XTERM_UTF8 */
        uint32_t utf8_char;

        /* State for the "Flexible and Economical UTF-8 Decoder" */
        uint32_t utf8_state;

        /* VT52 mode.  True means VT52, false means ANSI. Default is ANSI. */
        Q_BOOL vt52_mode;

        /* DEC private mode flag, set when CSI is followed by '?' */
        Q_BOOL dec_private_mode_flag;

        /* When true, use the G1 character set */
        Q_BOOL shift_out;

        /* When true, cursor positions are relative to the scrolling region */
        Q_BOOL saved_origin_mode;

        /* When true, the terminal is in 132-column mode */
        Q_BOOL columns_132;

        /* When true, this emulation has overridden the user's line wrap setting */
        Q_BOOL overridden_line_wrap;

        /* Which character set is currently selected in G0 */
        VT100_CHARACTER_SET g0_charset;

        /* Which character set is currently selected in G1 */
        VT100_CHARACTER_SET g1_charset;

        /* Saved cursor position*/
        int saved_cursor_x;
        int saved_cursor_y;

        /* Horizontal tab stops */
        /* tab_stops_n is the number of elements in
           tab_stops, so it begins as 0. */
        int tab_stops_n;
        int * tab_stops;

        /* Saved drawing attributes */
        attr_t saved_attributes;
        VT100_CHARACTER_SET saved_g0_charset;
        VT100_CHARACTER_SET saved_g1_charset;

        /* Character to repeat in rep() */
        wchar_t rep_ch;

        /* Parameters characters being collected */
        /* Sixteen rows with sixteen columns */

        /* params_n behaves DIFFERENTLY than tab_stops_n.
           params_n is originally set to -1 to indicate
           no parameter characters have been encounteres.
           At the first parameter character, params_n is
           set to 0, and incremented for each ';' in the
           sequence.  So params_n points to the currently-
           filling params[][]. */
        unsigned char params[VT100_PARAM_MAX][VT100_PARAM_LENGTH];
        int params_n;

};

/* I have to initialize this explicitly because
   tab_stops needs to be NULL BEFORE calling
   linux_reset(). */
static struct linux_state state = {
        0,
        0,
        Q_FALSE,
        Q_FALSE,
        Q_FALSE,
        Q_FALSE,
        Q_FALSE,
        Q_FALSE,
        CHARSET_US,
        CHARSET_DRAWING,
        -1,
        -1,
        0,
        NULL,
        -1,
        CHARSET_US,
        CHARSET_DRAWING,
        0
};

/*
 * Clear the parameter list
 */
static void clear_params() {
        memset(state.params, 0, sizeof(state.params));
        state.params_n = -1;
        state.dec_private_mode_flag = Q_FALSE;
} /* ---------------------------------------------------------------------- */

/*
 * Clear the parameter list
 */
static void clear_collect_buffer() {
        q_emul_buffer_n = 0;
        q_emul_buffer_i = 0;
} /* ---------------------------------------------------------------------- */

/*
 * Reset the tab stops list
 */
static void reset_tab_stops() {
        int i;
        if (state.tab_stops != NULL) {
                Xfree(state.tab_stops, __FILE__, __LINE__);
                state.tab_stops = NULL;
                state.tab_stops_n = 0;
        }
        for (i=0; (i*8) < WIDTH; i++) {
                state.tab_stops = (int *)Xrealloc(state.tab_stops, (state.tab_stops_n+1)*sizeof(int), __FILE__, __LINE__);
                state.tab_stops[i] = i*8;
                state.tab_stops_n++;
        }
} /* ---------------------------------------------------------------------- */

/*
 * Advance the cursor to the next tab stop
 */
static void advance_to_next_tab_stop() {
        int i;
        if (state.tab_stops == NULL) {
                /* Go to the rightmost column */
                cursor_right(WIDTH - 1 - q_status.cursor_x, Q_FALSE);
                return;
        }
        for (i=0; i<state.tab_stops_n; i++) {
                if (state.tab_stops[i] > q_status.cursor_x) {
                        cursor_right(state.tab_stops[i] - q_status.cursor_x, Q_FALSE);
                        return;
                }
        }
        /* We got here, meaning there isn't a tab stop beyond the
           current cursor position.  Place the cursor of the
           right-most edge of the screen. */
        cursor_right(WIDTH - 1 - q_status.cursor_x, Q_FALSE);
} /* ---------------------------------------------------------------------- */

/*
 * linux_reset - reset the emulation state
 */
void linux_reset() {
        scan_state = SCAN_GROUND;
        clear_params();
        clear_collect_buffer();

        /* Reset linux_state */
        state.saved_cursor_x            = -1;
        state.saved_cursor_y            = -1;
        q_emulation_right_margin        = 79;
        q_linux_new_line_mode           = Q_FALSE;
        q_linux_arrow_keys              = Q_EMUL_ANSI;
        q_linux_keypad_mode.keypad_mode = Q_KEYPAD_MODE_NUMERIC;
        q_linux_beep_frequency          = DEFAULT_BEEP_FREQUENCY;
        q_linux_beep_duration           = DEFAULT_BEEP_DURATION;
        q_xterm_mouse_protocol          = XTERM_MOUSE_OFF;
        q_xterm_mouse_encoding          = XTERM_MOUSE_ENCODING_X10;

        /* Default character sets */
        state.g0_charset                = CHARSET_US;
        state.g1_charset                = CHARSET_DRAWING;

        /* Curses attributes representing normal */
        state.saved_attributes          = q_current_color;
        state.saved_origin_mode         = Q_FALSE;
        state.saved_g0_charset          = CHARSET_US;
        state.saved_g1_charset          = CHARSET_DRAWING;

        /* Tab stops */
        reset_tab_stops();

        /* Flags */
        state.shift_out                 = Q_FALSE;
        state.vt52_mode                 = Q_FALSE;
        q_status.insert_mode            = Q_FALSE;
        state.dec_private_mode_flag     = Q_FALSE;
        state.columns_132               = Q_FALSE;
        state.overridden_line_wrap      = Q_FALSE;
        q_status.visible_cursor         = Q_TRUE;

#ifdef DEBUG_LINUX
        if (LINUX_DEBUG_FILE_HANDLE == NULL) {
                LINUX_DEBUG_FILE_HANDLE = fopen(LINUX_DEBUG_FILE, "w");
        }
        fprintf(LINUX_DEBUG_FILE_HANDLE, "linux_reset()\n");
#endif /* DEBUG_LINUX */

        /* Reset UTF-8 state */
        state.utf8_state = 0;

} /* ---------------------------------------------------------------------- */

/*
 * Hang onto one character in the buffer
 */
static void collect(const int keep_char, const wchar_t * to_screen) {
        q_emul_buffer[q_emul_buffer_n] = keep_char;
        q_emul_buffer_n++;
} /* ---------------------------------------------------------------------- */

/*
 * Add a character to the parameter list
 */
static void param(const unsigned char from_modem) {
        int param_length;

        if (state.params_n < 0) {
                state.params_n = 0;
        }

        if ((from_modem >= '0') && (from_modem <= '9')) {
                if (state.params_n < VT100_PARAM_MAX) {
                        param_length = strlen((char *)&state.params[state.params_n][0]);

                        if (param_length < VT100_PARAM_LENGTH - 1) {
                                state.params[state.params_n][param_length] = from_modem;
                        }
                }
        }
        if (from_modem == ';') {
                state.params_n++;
        }
} /* ---------------------------------------------------------------------- */

/*
 * Handle a control character function (C0 and C1 in the ECMA/ANSI spec)
 */
static void handle_control_char(const unsigned char control_char) {

#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "handle_control_char(): control_char = 0x%02x\n", control_char);
#endif /* DEBUG_LINUX */

        switch (control_char) {

        case 0x00:
                /* NUL */
                /* This is a special case, it's the only
                   control character that might need to surface. */
                if (q_status.display_null == Q_TRUE) {
                        print_character(' ');
                }
                break;

        case 0x05:
                /* ENQ */
                /* Transmit the answerback message.  Answerback is
                   usually programmed into user memory.  I believe
                   there is a DCS command to set it remotely, but
                   we won't support that (security hole). */
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
                advance_to_next_tab_stop();
                break;

        case 0x0A:
                /* LF */
                cursor_linefeed(q_linux_new_line_mode);
                break;

        case 0x0B:
                /* VT */
                cursor_linefeed(q_linux_new_line_mode);
                break;

        case 0x0C:
                /* FF */
                cursor_linefeed(q_linux_new_line_mode);
                break;

        case 0x0D:
                /* CR */
                cursor_carriage_return();
                break;

        case 0x0E:
                /* SO */
                state.shift_out = Q_TRUE;
                break;

        case 0x0F:
                /* SI */
                state.shift_out = Q_FALSE;
                break;

        default:
                break;
        }
} /* ---------------------------------------------------------------------- */

/*
 * Map a symbol in any one of the VT100 character sets to a PC VGA symbol
 */
static wchar_t map_character_charset(const unsigned char vt100_char, const VT100_CHARACTER_SET charset) {
        switch (charset) {

        case CHARSET_DRAWING:
                return dec_special_graphics_chars[vt100_char];

        case CHARSET_UK:
                return dec_uk_chars[vt100_char];

        case CHARSET_US:
        case CHARSET_ROM:
        case CHARSET_ROM_SPECIAL:
        default:
                return dec_us_chars[vt100_char];
        }
}

/*
 * Map a symbol in any one of the VT100 character sets to a PC VGA symbol
 */
static wchar_t map_character(const unsigned char vt100_char) {

#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "map_character: '%c' (0x%02x)\n", vt100_char, vt100_char);
#endif /* DEBUG_LINUX */

#if 1
        if (vt100_char >= 0x80) {
                /*
                 * Treat this like a CP437 character.  It could actually be
                 * from any 8-bit codepage, but most apps will only emit
                 * 8-bit characters to do box-drawing characters.
                 */
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "VGA CHAR: '%c' (0x%02x) --> '%uc' (0x%02x)\n", vt100_char, vt100_char, cp437_chars[vt100_char], cp437_chars[vt100_char]);
#endif /* DEBUG_LINUX */
                return cp437_chars[vt100_char];
        }
#endif

        if (state.vt52_mode == Q_TRUE) {
                if (state.shift_out == Q_TRUE) {
                        /* Shifted out character, pull from G1 */
                        return map_character_charset(vt100_char, state.g1_charset);
                } else {
                        /* Normal */
                        return map_character_charset(vt100_char, state.g0_charset);
                }
        }

        /* shift_out */
        if (state.shift_out == Q_TRUE) {
                /* Shifted out character, pull from G1 */
                return map_character_charset(vt100_char, state.g1_charset);
        }

        /* Normal, pull from G0 */
        return map_character_charset(vt100_char, state.g0_charset);
} /* ---------------------------------------------------------------------- */

/*
 * set_toggle - set or unset a toggle.  value is 'true' for set ('h'),
 * false for reset ('l').
 */
static void set_toggle(const Q_BOOL value) {
        int i;
        int x;
        for (i=0; i<q_emul_buffer_n; i++) {
                if (q_emul_buffer[i] == '?') {
                        state.dec_private_mode_flag = Q_TRUE;
                }
        }

        for (i=0; i<=state.params_n; i++) {
                x = atoi((char *)state.params[i]);

                switch (x) {

                case 1:
                        if (state.dec_private_mode_flag == Q_TRUE) {
                                /* DECCKM */
                                if (value == Q_TRUE) {
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "DECCRM: set (VT100 keys)\n");
#endif /* DEBUG_LINUX */
                                        /* Use application arrow keys */
                                        q_linux_arrow_keys = Q_EMUL_VT100;
                                } else {
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "DECCRM: reset (ANSI keys)\n");
#endif /* DEBUG_LINUX */
                                        /* Use ANSI arrow keys */
                                        q_linux_arrow_keys = Q_EMUL_ANSI;
                                }
                        }
                        break;
                case 2:
                        if (state.dec_private_mode_flag == Q_TRUE) {
                                if (value == Q_FALSE) {
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "DECANM: reset (Enter VT52 mode)\n");
#endif /* DEBUG_LINUX */
                                        /* DECANM */
                                        state.vt52_mode = Q_TRUE;
                                        q_linux_arrow_keys = Q_EMUL_VT52;
                                        q_linux_keypad_mode.emulation = Q_EMUL_VT52;

                                        /*
                                         * From the VT102 docs: "You use ANSI mode to select
                                         * most terminal features; the terminal uses the same
                                         * features when it switches to VT52 mode. You cannot,
                                         * however, change most of these features in VT52
                                         * mode."
                                         *
                                         * In other words, do not reset any other attributes
                                         * when switching between VT52 submode and ANSI.
                                         *
                                         * HOWEVER, the real vt100 does switch the character
                                         * set according to Usenet.
                                         */
                                        state.g0_charset = CHARSET_US;
                                        state.g1_charset = CHARSET_DRAWING;
                                        state.shift_out = Q_FALSE;
                                }
                        } else {
                                /* KAM */
                                if (value == Q_TRUE) {
                                        /* Turn off keyboard */
                                        /* Not supported */
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "KAM: set (turn off keyboard) NOP\n");
#endif /* DEBUG_LINUX */
                                } else {
                                        /* Turn on keyboard */
                                        /* Not supported */
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "KAM: reset (turn on keyboard) NOP\n");
#endif /* DEBUG_LINUX */
                                }
                        }
                        break;
                case 3:
                        if (state.dec_private_mode_flag == Q_TRUE) {
                                /* DECCOLM */
                                if (value == Q_TRUE) {
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "DECCOLM: set\n");
#endif /* DEBUG_LINUX */
                                        /* 132 columns */
                                        state.columns_132 = Q_TRUE;
                                        q_emulation_right_margin = 131;
                                } else {
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "DECCOLM: reset\n");
#endif /* DEBUG_LINUX */
                                        /* 80 columns */
                                        state.columns_132 = Q_FALSE;
                                        q_emulation_right_margin = 79;
                                }
                                /* Entire screen is cleared, and scrolling region is reset */
                                erase_screen(0, 0, HEIGHT - STATUS_HEIGHT - 1, WIDTH - 1, Q_FALSE);
                                q_status.scroll_region_top = 0;
                                q_status.scroll_region_bottom = HEIGHT - STATUS_HEIGHT - 1;
                                /* Also home the cursor */
                                cursor_position(0, 0);
                        }
                        break;
                case 4:
                        if (state.dec_private_mode_flag == Q_TRUE) {
                                /* DECSCLM */
                                if (value == Q_TRUE) {
                                        /* Smooth scroll */
                                        /* Not supported */
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "DECSCLM: set (smooth scroll) NOP\n");
#endif /* DEBUG_LINUX */
                                } else {
                                        /* Jump scroll */
                                        /* Not supported */
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "DECSCLM: reset (jump scroll) NOP\n");
#endif /* DEBUG_LINUX */
                                }
                        } else {
                                /* IRM */
                                if (value == Q_TRUE) {
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "IRM: set\n");
#endif /* DEBUG_LINUX */
                                        q_status.insert_mode = Q_TRUE;
                                } else {
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "IRM: reset\n");
#endif /* DEBUG_LINUX */
                                        q_status.insert_mode = Q_FALSE;
                                }
                        }
                        break;
                case 5:
                        if (state.dec_private_mode_flag == Q_TRUE) {
                                /* DECSCNM */
                                if (value == Q_TRUE) {
                                        /* Set selects reverse screen, a white
                                           screen background with black
                                           characters.
                                        */
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "DECSCNM: set\n");
#endif /* DEBUG_LINUX */
                                        if (q_status.reverse_video != Q_TRUE) {
                                                /* If in normal video, switch it back */
                                                invert_scrollback_colors();
                                        }
                                        q_status.reverse_video = Q_TRUE;
                                } else {
                                        /* Reset selects normal screen, a
                                           black screen background with white
                                           characters.
                                        */
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "DECSCNM: reset\n");
#endif /* DEBUG_LINUX */
                                        if (q_status.reverse_video == Q_TRUE) {
                                                /* If in reverse video already, switch it back */
                                                deinvert_scrollback_colors();
                                        }
                                        q_status.reverse_video = Q_FALSE;
                                }
                        }
                        break;
                case 6:
                        if (state.dec_private_mode_flag == Q_TRUE) {
                                /* DECOM */
                                if (value == Q_TRUE) {
                                        /* Origin is relative to scroll region */
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "DECOM: set\n");
#endif /* DEBUG_LINUX */
                                        /* Home cursor.  Cursor can NEVER leave scrolling
                                           region. */
                                        q_status.origin_mode = Q_TRUE;
                                        cursor_position(0, 0);
                                } else {
                                        /* Origin is absolute to entire screen */
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "DECOM: reset\n");
#endif /* DEBUG_LINUX */
                                        /* Home cursor.  Cursor can leave the scrolling
                                           region via cup() and hvp(). */
                                        q_status.origin_mode = Q_FALSE;
                                        cursor_position(0, 0);
                                }
                        }
                        break;
                case 7:
                        if (state.dec_private_mode_flag == Q_TRUE) {
                                /* DECAWM */
                                if (value == Q_TRUE) {
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "DECAWM: set\n");
#endif /* DEBUG_LINUX */
                                        /* Turn linewrap on */
                                        if (q_status.line_wrap == Q_FALSE) {
                                                state.overridden_line_wrap = Q_TRUE;
                                        }
                                        q_status.line_wrap = Q_TRUE;
                                } else {
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "DECAWM: reset\n");
#endif /* DEBUG_LINUX */
                                        /* Turn linewrap off */
                                        if (q_status.line_wrap == Q_TRUE) {
                                                state.overridden_line_wrap = Q_TRUE;
                                        }
                                        q_status.line_wrap = Q_FALSE;
                                }
                        }
                        break;
                case 8:
                        if (state.dec_private_mode_flag == Q_TRUE) {
                                /* DECARM */
                                if (value == Q_TRUE) {
                                        /* Keyboard auto-repeat on */
                                        /* Not supported */
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "DECARM: set (Keyboard auto-repeat on) NOP\n");
#endif /* DEBUG_LINUX */
                                } else {
                                        /* Keyboard auto-repeat off */
                                        /* Not supported */
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "DECARM: reset (Keyboard auto-repeat off) NOP\n");
#endif /* DEBUG_LINUX */
                                }
                        }
                        break;
                case 9:
                        /* X10 mouse reporting */
                        break;
                case 12:
                        if (state.dec_private_mode_flag == Q_FALSE) {
                                /* SRM */
                                if (value == Q_TRUE) {
                                        /* Local echo off */
                                        q_status.full_duplex = Q_TRUE;
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "SRM: set (Local echo on)\n");
#endif /* DEBUG_LINUX */
                                } else {
                                        /* Local echo on */
                                        q_status.full_duplex = Q_FALSE;
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "SRM: reset (Local echo off)\n");
#endif /* DEBUG_LINUX */
                                }
                        }
                        break;
                case 18:
                        if (state.dec_private_mode_flag == Q_TRUE) {
                                /* DECPFF */
                                /* Not supported */
#ifdef DEBUG_LINUX
                                fprintf(LINUX_DEBUG_FILE_HANDLE, "DECPFF: NOP\n");
#endif /* DEBUG_LINUX */
                        }
                        break;
                case 19:
                        if (state.dec_private_mode_flag == Q_TRUE) {
                                /* DECPEX */
                                /* Not supported */
#ifdef DEBUG_LINUX
                                fprintf(LINUX_DEBUG_FILE_HANDLE, "DECPEX: NOP\n");
#endif /* DEBUG_LINUX */
                        }
                        break;
                case 20:
                        if (state.dec_private_mode_flag == Q_FALSE) {
                                /* LNM */
                                if (value == Q_TRUE) {
                                        /* Set causes a received linefeed,
                                           form feed, or vertical tab to move
                                           cursor to first column of next
                                           line. RETURN transmits both a
                                           carriage return and linefeed. This
                                           selection is also called new line
                                           option.
                                        */
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "LNM: set (CRLF)\n");
#endif /* DEBUG_LINUX */
                                        q_linux_new_line_mode = Q_TRUE;
                                } else {
                                        /* Reset causes a received linefeed,
                                           form feed, or vertical tab to move
                                           cursor to next line in current
                                           column. RETURN transmits a carriage
                                           return.
                                        */
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "LNM: reset (CR)\n");
#endif /* DEBUG_LINUX */
                                        q_linux_new_line_mode = Q_FALSE;
                                }
                        }
                        break;
                case 25:
                        if (state.dec_private_mode_flag == Q_TRUE) {
                                /* DECCM - make cursor visible */
                                if (value == Q_TRUE) {
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "DECCM: set (cursor on)\n");
#endif /* DEBUG_LINUX */
                                        q_cursor_on();
                                        q_status.visible_cursor = Q_TRUE;
                                } else {
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "DECCM: reset (cursor off)\n");
#endif /* DEBUG_LINUX */
                                        q_cursor_off();
                                        q_status.visible_cursor = Q_FALSE;
                                }
                        }
                        break;

                case 1000:
                        if ((state.dec_private_mode_flag == Q_TRUE) &&
                            ((q_status.emulation == Q_EMUL_XTERM) ||
                             (q_status.emulation == Q_EMUL_XTERM_UTF8))
                        ) {
                                /* Mouse: normal tracking mode */
                                if (value == Q_TRUE) {
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "MOUSE: Normal tracking mode on\n");
#endif /* DEBUG_LINUX */
                                        q_xterm_mouse_protocol = XTERM_MOUSE_NORMAL;
                                } else {
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "MOUSE: Normal tracking mode off\n");
#endif /* DEBUG_LINUX */
                                        q_xterm_mouse_protocol = XTERM_MOUSE_OFF;
                                }
                        }
                        break;

                case 1002:
                        if ((state.dec_private_mode_flag == Q_TRUE) &&
                            ((q_status.emulation == Q_EMUL_XTERM) ||
                             (q_status.emulation == Q_EMUL_XTERM_UTF8))
                        ) {
                                /* Mouse: normal tracking mode */
                                if (value == Q_TRUE) {
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "MOUSE: Button-event tracking mode on\n");
#endif /* DEBUG_LINUX */
                                        q_xterm_mouse_protocol = XTERM_MOUSE_BUTTONEVENT;
                                } else {
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "MOUSE: Button-event tracking mode off\n");
#endif /* DEBUG_LINUX */
                                        q_xterm_mouse_protocol = XTERM_MOUSE_OFF;
                                }
                        }
                        break;

                case 1003:
                        if ((state.dec_private_mode_flag == Q_TRUE) &&
                            ((q_status.emulation == Q_EMUL_XTERM) ||
                             (q_status.emulation == Q_EMUL_XTERM_UTF8))
                        ) {
                                /* Mouse: Any-event tracking mode */
                                if (value == Q_TRUE) {
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "MOUSE: Any-event tracking mode on\n");
#endif /* DEBUG_LINUX */
                                        q_xterm_mouse_protocol = XTERM_MOUSE_ANYEVENT;
                                } else {
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "MOUSE: Any-event tracking mode off\n");
#endif /* DEBUG_LINUX */
                                        q_xterm_mouse_protocol = XTERM_MOUSE_OFF;
                                }
                        }
                        break;

                case 1005:
                        if ((state.dec_private_mode_flag == Q_TRUE) &&
                            ((q_status.emulation == Q_EMUL_XTERM) ||
                             (q_status.emulation == Q_EMUL_XTERM_UTF8))
                        ) {
                                /* Mouse: UTF-8 coordinates */
                                if (value == Q_TRUE) {
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "MOUSE: UTF-8 coordinates on");
#endif /* DEBUG_LINUX */
                                        q_xterm_mouse_encoding = XTERM_MOUSE_ENCODING_UTF8;
                                } else {
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "MOUSE: UTF-8 coordinates off\n");
#endif /* DEBUG_LINUX */
                                        q_xterm_mouse_encoding = XTERM_MOUSE_ENCODING_X10;
                                }
                        }
                        break;

                default:
                        break;

                } /* switch (x) */

        } /* for (i=0; i<=state.params_n; i++) */

} /* ---------------------------------------------------------------------- */

/*
 * DECRC - Restore cursor
 */
static void decrc() {
#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "decrc(): state.saved_cursor_y=%d state.saved_cursor_x=%d\n", state.saved_cursor_y, state.saved_cursor_x);
#endif /* DEBUG_LINUX */
        if (state.saved_cursor_x != -1) {
                cursor_position(state.saved_cursor_y, state.saved_cursor_x);
                q_current_color         = state.saved_attributes;
                q_status.origin_mode    = state.saved_origin_mode;
                state.g0_charset        = state.saved_g0_charset;
                state.g1_charset        = state.saved_g1_charset;
        }
} /* ---------------------------------------------------------------------- */

/*
 * DECSC - Save cursor
 */
static void decsc() {
        state.saved_cursor_x            = q_status.cursor_x;
        state.saved_cursor_y            = q_status.cursor_y;
        state.saved_attributes          = q_current_color;
        state.saved_origin_mode         = q_status.origin_mode;
        state.saved_g0_charset          = state.g0_charset;
        state.saved_g1_charset          = state.g1_charset;
#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "decsc(): state.saved_cursor_y=%d state.saved_cursor_x=%d\n", state.saved_cursor_y, state.saved_cursor_x);
#endif /* DEBUG_LINUX */
} /* ---------------------------------------------------------------------- */

/*
 * DECSWL - Single-width line
 */
static void decswl() {
#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "decswl()\n");
#endif /* DEBUG_LINUX */
        set_double_width(Q_FALSE);
} /* ---------------------------------------------------------------------- */

/*
 * DECDWL - Double-width line
 */
static void decdwl() {
#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "decdwl()\n");
#endif /* DEBUG_LINUX */
        set_double_width(Q_TRUE);
} /* ---------------------------------------------------------------------- */

/*
 * DECHDL - Double-height + double-width line
 */
static void dechdl(Q_BOOL top_half) {
#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "dechdl(%s)\n",
                (top_half == Q_TRUE ? "true" : "false"));
#endif /* DEBUG_LINUX */
        set_double_width(Q_TRUE);
        if (top_half == Q_TRUE) {
                set_double_height(1);
        } else {
                set_double_height(2);
        }
} /* ---------------------------------------------------------------------- */

/*
 * DECKPAM - Keypad application mode
 */
static void deckpam() {
#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "deckpam()\n");
#endif /* DEBUG_LINUX */
        q_linux_keypad_mode.keypad_mode = Q_KEYPAD_MODE_APPLICATION;
} /* ---------------------------------------------------------------------- */

/*
 * DECKPNM - Keypad numeric mode
 */
static void deckpnm() {
#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "deckpnm()\n");
#endif /* DEBUG_LINUX */
        q_linux_keypad_mode.keypad_mode = Q_KEYPAD_MODE_NUMERIC;
} /* ---------------------------------------------------------------------- */

/*
 * IND - Index
 */
static void ind() {
#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "ind()\n");
#endif /* DEBUG_LINUX */

        /* Move the cursor and scroll if necessary */
        /* If at the bottom line already, a scroll up is supposed to be performed. */
        if (q_status.cursor_y == q_status.scroll_region_bottom) {
                scrolling_region_scroll_up(q_status.scroll_region_top, q_status.scroll_region_bottom, 1);
        }
        cursor_down(1, Q_TRUE);
} /* ---------------------------------------------------------------------- */

/*
 * RI - Reverse index
 */
static void ri() {
#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "ri()\n");
#endif /* DEBUG_LINUX */

        /* Move the cursor and scroll if necessary */
        /* If at the top line already, a scroll down is supposed to be performed. */
        if (q_status.cursor_y == q_status.scroll_region_top) {
                scrolling_region_scroll_down(q_status.scroll_region_top, q_status.scroll_region_bottom, 1);
        }
        cursor_up(1, Q_TRUE);
} /* ---------------------------------------------------------------------- */

/*
 * NEL - Next line
 */
static void nel() {
#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "nel()\n");
#endif /* DEBUG_LINUX */

        /* Move the cursor and scroll if necessary */
        /* If at the bottom line already, a scroll up is supposed to be performed. */
        if (q_status.cursor_y == q_status.scroll_region_bottom) {
                scrolling_region_scroll_up(q_status.scroll_region_top, q_status.scroll_region_bottom, 1);
        }
        cursor_down(1, Q_TRUE);

        /* Reset to the beginning of the next line */
        q_status.cursor_x = 0;
} /* ---------------------------------------------------------------------- */

/*
 * HTS - Horizontal tabulation set
 */
static void hts() {
        int i;
#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "hts()\n");
#endif /* DEBUG_LINUX */
        for (i=0; i<state.tab_stops_n; i++) {
                if (state.tab_stops[i] == q_status.cursor_x) {
                        /* Already have a tab stop here */
                        return;
                }
                if (state.tab_stops[i] > q_status.cursor_x) {
                        /* Insert a tab stop */
                        state.tab_stops = (int *)Xrealloc(state.tab_stops, (state.tab_stops_n+1)*sizeof(int), __FILE__, __LINE__);
                        memmove(&state.tab_stops[i+1], &state.tab_stops[i], (state.tab_stops_n - i) * sizeof(int));
                        state.tab_stops_n++;
                        state.tab_stops[i] = q_status.cursor_x;
                        return;
                }
        }

        /* If we get here, we need to append a tab stop to the end of the array */
        state.tab_stops = (int *)Xrealloc(state.tab_stops, (state.tab_stops_n+1)*sizeof(int), __FILE__, __LINE__);
        state.tab_stops[state.tab_stops_n] = q_status.cursor_x;
        state.tab_stops_n++;
} /* ---------------------------------------------------------------------- */

/*
 * DECALN - Screen alignment display
 */
static void decaln() {
        int i, j;
        int x, y;
#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "decaln()\n");
#endif /* DEBUG_LINUX */
        x = q_status.cursor_x;
        y = q_status.cursor_y;

        cursor_position(0, 0);
        for (i=0; i<HEIGHT - STATUS_HEIGHT; i++) {
                for (j=0; j<WIDTH; j++) {
                        q_scrollback_current->chars[j] = 'E';
                        q_scrollback_current->colors[j] = scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);
                }
                q_scrollback_current->length = WIDTH;
                cursor_down(1, Q_FALSE);
        }
        cursor_position(y, x);
} /* ---------------------------------------------------------------------- */

/*
 * CUD - Cursor down
 */
static void cud() {
        int i;
        if (state.params_n < 0) {
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "cud(): 1\n");
#endif /* DEBUG_LINUX */
                cursor_down(1, Q_TRUE);
        } else {
                i = atoi((char *)state.params[0]);
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "cud(): %d\n", i);
#endif /* DEBUG_LINUX */
                if (i <= 0) {
                        cursor_down(1, Q_TRUE);
                } else {
                        cursor_down(i, Q_TRUE);
                }
        }
} /* ---------------------------------------------------------------------- */

/*
 * CUF - Cursor forward
 */
static void cuf() {
        int i;
        if (state.params_n < 0) {
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "cuf(): 1\n");
#endif /* DEBUG_LINUX */
                cursor_right(1, Q_TRUE);
        } else {
                i = atoi((char *)state.params[0]);
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "cuf(): %d\n", i);
#endif /* DEBUG_LINUX */
                if (i <= 0) {
                        cursor_right(1, Q_TRUE);
                } else {
                        cursor_right(i, Q_TRUE);
                }
        }
} /* ---------------------------------------------------------------------- */

/*
 * CUB - Cursor backward
 */
static void cub() {
        int i;
        if (state.params_n < 0) {
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "cub(): 1\n");
#endif /* DEBUG_LINUX */
                cursor_left(1, Q_TRUE);
        } else {
                i = atoi((char *)state.params[0]);
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "cub(): %d\n", i);
#endif /* DEBUG_LINUX */
                if (i <= 0) {
                        cursor_left(1, Q_TRUE);
                } else {
                        cursor_left(i, Q_TRUE);
                }
        }
} /* ---------------------------------------------------------------------- */

/*
 * CUU - Cursor up
 */
static void cuu() {
        int i;

        if (state.params_n < 0) {
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "cuu(): 1\n");
#endif /* DEBUG_LINUX */
                cursor_up(1, Q_TRUE);
        } else {
                i = atoi((char *)state.params[0]);
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "cuu(): %d\n", i);
#endif /* DEBUG_LINUX */
                if (i <= 0) {
                        cursor_up(1, Q_TRUE);
                } else {
                        cursor_up(i, Q_TRUE);
                }
        }
} /* ---------------------------------------------------------------------- */

/*
 * CUP - Cursor position
 */
static void cup() {
        int row;
        int col;
        if (state.params_n < 0) {
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "cup(): 0 0\n");
#endif /* DEBUG_LINUX */
                cursor_position(0, 0);
        } else if (state.params_n == 0) {
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "cup(): %d %d\n", atoi((char *)state.params[0]) - 1, 0);
#endif /* DEBUG_LINUX */
                row = atoi((char *)state.params[0]) - 1;
                if (row < 0) {
                        row = 0;
                }
                cursor_position(row, 0);
        } else {
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "cup(): %d %d\n", atoi((char *)state.params[0]) - 1, atoi((char *)state.params[1]) - 1);
#endif /* DEBUG_LINUX */
                row = atoi((char *)state.params[0]) - 1;
                if (row < 0) {
                        row = 0;
                }
                col = atoi((char *)state.params[1]) - 1;
                if (col < 0) {
                        col = 0;
                }
                cursor_position(row, col);
        }
} /* ---------------------------------------------------------------------- */

/*
 * DECSTBM - Set top and bottom margins
 */
static void decstbm() {
        int i, j;

#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "decstbm() param0 %s param1 %s\n", state.params[0], state.params[1]);
#endif /* DEBUG_LINUX */
        for (i = 0; i < q_emul_buffer_n; i++) {
                if (q_emul_buffer[i] == '?') {
                        state.dec_private_mode_flag = Q_TRUE;
                }
        }
        if (state.dec_private_mode_flag == Q_TRUE) {
                /* This is "restore DEC private mode values" for
                 * XTERM.  Ignore for now. */
                return;
        }

        if (state.params_n < 0) {
                q_status.scroll_region_top = 0;
                q_status.scroll_region_bottom = HEIGHT - STATUS_HEIGHT - 1;
        } else if (state.params_n == 0) {
                if (strlen((char *)state.params[0]) == 0) {
                        i = 0;
                } else {
                        i = atoi((char *)state.params[0]) - 1;
                }
                if ((i >= 0) && (i <= HEIGHT - 1)) {
                        q_status.scroll_region_top = i;
                }
                q_status.scroll_region_bottom = HEIGHT - STATUS_HEIGHT - 1;
        } else {
                if (strlen((char *)state.params[0]) == 0) {
                        i = 0;
                } else {
                        i = atoi((char *)state.params[0]) - 1;
                }
                if (strlen((char *)state.params[1]) == 0) {
                        j = HEIGHT - STATUS_HEIGHT - 1;
                } else {
                        j = atoi((char *)state.params[1]) - 1;
                }
                if ((i >= 0) && (i <= HEIGHT - 1) && (j >= 0) && (j <= HEIGHT - 1) && (j > i)) {
                        q_status.scroll_region_top = i;
                        q_status.scroll_region_bottom = j;
                } else {
                        q_status.scroll_region_top = 0;
                        q_status.scroll_region_bottom = HEIGHT - STATUS_HEIGHT - 1;
                }
        }

        /* Sanity check:  if the bottom margin is too big bring it back */
        if (q_status.scroll_region_bottom > HEIGHT - STATUS_HEIGHT - 1) {
                q_status.scroll_region_bottom = HEIGHT - STATUS_HEIGHT - 1;
        }
        /* If the top scroll region is off bring it back too */
        if (q_status.scroll_region_top > q_status.scroll_region_bottom) {
                q_status.scroll_region_top = q_status.scroll_region_bottom;
        }

#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "decstbm() %d %d\n", q_status.scroll_region_top, q_status.scroll_region_bottom);
#endif /* DEBUG_LINUX */

        /* Home cursor */
        cursor_position(0, 0);
} /* ---------------------------------------------------------------------- */

/*
 * DECREQTPARM - Request terminal parameters
 */
static void decreqtparm() {
        int i;
        char response_buffer[VT100_RESPONSE_LENGTH];

        if (state.params_n >= 0) {
                i = atoi((char *)state.params[0]);
        } else {
                i = 0;
        }

#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "decreqtparm(): %d\n", i);
#endif /* DEBUG_LINUX */

        if ((i != 0) && (i != 1)) {
                return;
        }

        /* Request terminal parameters. */
        /* Respond with:

              Parity NONE, 8 bits, xmitspeed 38400, recvspeed 38400.
              (CLoCk MULtiplier = 1, STP option flags = 0)

        (Same as xterm)

        */

        memset(response_buffer, 0, sizeof(response_buffer));
        snprintf(response_buffer, sizeof(response_buffer), "\033[%u;1;1;128;128;1;0x", i + 2);

        /* Send string directly to remote side */
        qodem_write(q_child_tty_fd, response_buffer, strlen(response_buffer), Q_TRUE);

} /* ---------------------------------------------------------------------- */

/*
 * DECSCA - Select Character Attributes
 */
static void decsca() {
        int i;

        if (state.params_n >= 0) {
                i = atoi((char *)state.params[0]);
        } else {
                i = 0;
        }

#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "decsca(): %d\n", i);
#endif /* DEBUG_LINUX */

        if ((i == 0) || (i == 2)) {
                /* Protect mode OFF */
                q_current_color &= ~Q_A_PROTECT;
        }
        if (i == 1) {
                /* Protect mode ON */
                q_current_color |= Q_A_PROTECT;
        }
} /* ---------------------------------------------------------------------- */

/*
 * DECSTR - Soft Terminal Reset
 */
static void decstr() {
#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "decstr()\n");
#endif /* DEBUG_LINUX */

        /* Do exactly like RIS - Reset to initial state */
        linux_reset();
        q_cursor_on();
        /* Do I clear screen too? I think so... */
        erase_screen(0, 0, HEIGHT - STATUS_HEIGHT - 1, WIDTH - 1, Q_FALSE);
} /* ---------------------------------------------------------------------- */

/*
 * DECLL - Load keyboard leds
 */
static void decll() {
        int i, j;

#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "decll(): ");
#endif /* DEBUG_LINUX */

        if (state.params_n < 0) {
                q_status.led_1 = Q_FALSE;
                q_status.led_2 = Q_FALSE;
                q_status.led_3 = Q_FALSE;
                q_status.led_4 = Q_FALSE;
        } else {
                for (i = 0; i <= state.params_n; i++) {
                        j = atoi((char *)state.params[i]);
#ifdef DEBUG_LINUX
                        fprintf(LINUX_DEBUG_FILE_HANDLE, "%d ", j);
#endif /* DEBUG_LINUX */
                        switch (j) {
                        case 0:
                                q_status.led_1 = Q_FALSE;
                                q_status.led_2 = Q_FALSE;
                                q_status.led_3 = Q_FALSE;
                                q_status.led_4 = Q_FALSE;
                                break;
                        case 1:
                                /*
                                 * Under LINUX, this is supposed to set
                                 * scroll lock.
                                 */
                                q_status.led_1 = Q_TRUE;
                                break;
                        case 2:
                                /*
                                 * Under LINUX, this is supposed to set
                                 * num lock.
                                 */
                                q_status.led_2 = Q_TRUE;
                                break;
                        case 3:
                                /*
                                 * Under LINUX, this is supposed to set
                                 * caps lock.
                                 */
                                q_status.led_3 = Q_TRUE;
                                break;
                        case 4:
                                /*
                                 * Under LINUX, this is supposed to do
                                 * nothing.
                                 */
                                q_status.led_4 = Q_TRUE;
                                break;
                        }
                }
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "\n");
#endif /* DEBUG_LINUX */
        }
} /* ---------------------------------------------------------------------- */

/*
 * ED - Erase in display
 */
static void ed() {
        int i;
        Q_BOOL honor_protected = Q_FALSE;

        for (i=0; i<q_emul_buffer_n; i++) {
                if (q_emul_buffer[i] == '?') {
                        state.dec_private_mode_flag = Q_TRUE;
                }
        }

        if (    ((q_status.emulation == Q_EMUL_XTERM) ||
                (q_status.emulation == Q_EMUL_XTERM_UTF8)) &&
                (state.dec_private_mode_flag == Q_TRUE)
        ) {
                honor_protected = Q_TRUE;
        }

        if (state.params_n >= 0) {
                i = atoi((char *)state.params[0]);
        } else {
                i = 0;
        }

        if (i == 0) {
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "ed(): %d %d %d %d\n", q_status.cursor_y, q_status.cursor_x, HEIGHT - STATUS_HEIGHT - 1, WIDTH - 1);
#endif /* DEBUG_LINUX */
                /* Erase from here to end of screen */
                if (q_status.cursor_y < HEIGHT - STATUS_HEIGHT - 1) {
                        erase_screen(q_status.cursor_y + 1, 0, HEIGHT - STATUS_HEIGHT - 1, WIDTH - 1, honor_protected);
                }
                erase_line(q_status.cursor_x, WIDTH-1, honor_protected);
        } else if (i == 1) {
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "ed(): 0 0 %d %d\n", q_status.cursor_y, q_status.cursor_x);
#endif /* DEBUG_LINUX */
                /* Erase from beginning of screen to here */
                erase_screen(0, 0, q_status.cursor_y - 1, WIDTH - 1, honor_protected);
                erase_line(0, q_status.cursor_x, honor_protected);
        } else if (i == 2) {
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "ed(): 0 0 %d %d\n", HEIGHT - STATUS_HEIGHT - 1, WIDTH - 1);
#endif /* DEBUG_LINUX */
                /* Erase entire screen */
                erase_screen(0, 0, HEIGHT - STATUS_HEIGHT - 1, WIDTH - 1, honor_protected);
        }

} /* ---------------------------------------------------------------------- */

/*
 * EL - Erase in line
 */
static void el() {
        int i;
        Q_BOOL honor_protected = Q_FALSE;

        for (i=0; i<q_emul_buffer_n; i++) {
                if (q_emul_buffer[i] == '?') {
                        state.dec_private_mode_flag = Q_TRUE;
                }
        }

        if (    ((q_status.emulation == Q_EMUL_XTERM) ||
                (q_status.emulation == Q_EMUL_XTERM_UTF8)) &&
                (state.dec_private_mode_flag == Q_TRUE)
        ) {
                honor_protected = Q_TRUE;
        }

        if (state.params_n >= 0) {
                i = atoi((char *)state.params[0]);
        } else {
                i = 0;
        }

        if (i == 0) {
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "el(): %d %d\n", q_status.cursor_x, WIDTH-1);
#endif /* DEBUG_LINUX */
                /* Erase from here to end of line */
                erase_line(q_status.cursor_x, WIDTH-1, honor_protected);
        } else if (i == 1) {
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "el(): 0 %d\n", q_status.cursor_x);
#endif /* DEBUG_LINUX */
                /* Erase from beginning of line to here */
                erase_line(0, q_status.cursor_x, honor_protected);
        } else if (i == 2) {
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "el(): 0 %d\n", WIDTH-1);
#endif /* DEBUG_LINUX */
                /* Erase entire line */
                erase_line(0, WIDTH-1, honor_protected);
        }

} /* ---------------------------------------------------------------------- */

/*
 * IL - Insert line
 */
static void il() {
        int i;

        if (state.params_n >= 0) {
                i = atoi((char *)state.params[0]);
        } else {
                i = 1;
        }

#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "il(): %d\n", i);
#endif /* DEBUG_LINUX */

        if ((q_status.cursor_y >= q_status.scroll_region_top) && (q_status.cursor_y <= q_status.scroll_region_bottom)) {
                /* I can get the same effect with a scroll-down */
                scrolling_region_scroll_down(q_status.cursor_y, q_status.scroll_region_bottom, i);
        }
} /* ---------------------------------------------------------------------- */

/*
 * DCH - Delete char
 */
static void dch() {
        int i;

        if (state.params_n >= 0) {
                i = atoi((char *)state.params[0]);
        } else {
                i = 1;
        }

#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "dch(): %d\n", i);
#endif /* DEBUG_LINUX */
        delete_character(i);
} /* ---------------------------------------------------------------------- */

/*
 * ICH - Insert blank char at cursor
 */
static void ich() {
        int i;

        if (state.params_n >= 0) {
                i = atoi((char *)state.params[0]);
        } else {
                i = 1;
        }

#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "ich(): %d\n", i);
#endif /* DEBUG_LINUX */
        insert_blanks(i);
} /* ---------------------------------------------------------------------- */

/*
 * DL - Delete line
 */
static void dl() {
        int i;

        if (state.params_n >= 0) {
                i = atoi((char *)state.params[0]);
        } else {
                i = 1;
        }

#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "dl(): %d\n", i);
#endif /* DEBUG_LINUX */

        if ((q_status.cursor_y >= q_status.scroll_region_top) && (q_status.cursor_y <= q_status.scroll_region_bottom)) {
                /* I can get the same effect with a scroll-up */
                scrolling_region_scroll_up(q_status.cursor_y, q_status.scroll_region_bottom, i);
        }
} /* ---------------------------------------------------------------------- */

/*
 * HVP - Horizontal and vertical position
 */
static void hvp() {
        cup();
} /* ---------------------------------------------------------------------- */

/*
 * SGR - Select graphics rendition
 */
static void sgr() {
        int i, j;
        short foreground, background;
        short curses_color;

        /* Pull the current foreground and background */
        curses_color = color_from_attr(q_current_color);
        foreground = (curses_color & 0x38) >> 3;
        background = curses_color & 0x07;

#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "sgr(): foreground = %02x background = %02x\n", foreground, background);
        fprintf(LINUX_DEBUG_FILE_HANDLE, "sgr(): Pn...Pn = ");
#endif /* DEBUG_LINUX */

        if (state.params_n < 0) {
                q_current_color = Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);
                foreground = q_text_colors[Q_COLOR_CONSOLE_TEXT].fg;
                background = q_text_colors[Q_COLOR_CONSOLE_TEXT].bg;
#ifdef DEBUG_LINUX
                        fprintf(LINUX_DEBUG_FILE_HANDLE, "RESET\n");
#endif /* DEBUG_LINUX */
        } else {
                for (i = 0; i <= state.params_n; i++) {
                        j = atoi((char *)state.params[i]);
#ifdef DEBUG_LINUX
                        fprintf(LINUX_DEBUG_FILE_HANDLE, " - %d - \n", j);
#endif /* DEBUG_LINUX */

                        switch (j) {
                        case 0:
                                /* Normal */
                                foreground = q_text_colors[Q_COLOR_CONSOLE_TEXT].fg;
                                background = q_text_colors[Q_COLOR_CONSOLE_TEXT].bg;
                                q_current_color = Q_A_NORMAL;
                                if (q_text_colors[Q_COLOR_CONSOLE_TEXT].bold == Q_TRUE) {
                                        q_current_color |= Q_A_BOLD;
                                }
                                break;

                        case 1:
                                /* Bold */
                                q_current_color |= Q_A_BOLD;
                                break;

                        case 2:
                                /* Half bright */
                                q_current_color |= Q_A_DIM;
                                break;

                        case 4:
                                /* Underline */
                                q_current_color |= Q_A_UNDERLINE;
                                break;

                        case 5:
                                /* Blink */
                                q_current_color |= Q_A_BLINK;
                                break;

                        case 7:
                                /* Reverse */
                                q_current_color |= Q_A_REVERSE;
                                break;

                        case 8:
                                /* Invisible */
                                if (    (q_status.emulation == Q_EMUL_XTERM) ||
                                        (q_status.emulation == Q_EMUL_XTERM_UTF8)
                                ) {
                                        q_current_color |= Q_A_INVIS;
                                }
                                break;

                        case 21:
                                /* Fall through... */
                        case 22:
                                /* Normal intensity */
                                q_current_color &= ~Q_A_BOLD;
                                break;

                        case 24:
                                /* Underline off */
                                q_current_color &= ~Q_A_UNDERLINE;
                                break;

                        case 25:
                                /* Blink off */
                                q_current_color &= ~Q_A_BLINK;
                                break;

                        case 27:
                                /* Reverse off */
                                q_current_color &= ~Q_A_REVERSE;
                                break;

                        case 30:
                                /* Set black foreground */
                                foreground = Q_COLOR_BLACK;
                                break;
                        case 31:
                                /* Set red foreground */
                                foreground = Q_COLOR_RED;
                                break;
                        case 32:
                                /* Set green foreground */
                                foreground = Q_COLOR_GREEN;
                                break;
                        case 33:
                                /* Set yellow foreground */
                                foreground = Q_COLOR_YELLOW;
                                break;
                        case 34:
                                /* Set blue foreground */
                                foreground = Q_COLOR_BLUE;
                                break;
                        case 35:
                                /* Set magenta foreground */
                                foreground = Q_COLOR_MAGENTA;
                                break;
                        case 36:
                                /* Set cyan foreground */
                                foreground = Q_COLOR_CYAN;
                                break;
                        case 37:
                                /* Set white foreground */
                                foreground = Q_COLOR_WHITE;
                                break;
                        case 38:
                                foreground = q_text_colors[Q_COLOR_CONSOLE_TEXT].fg;
                                if (q_text_colors[Q_COLOR_CONSOLE_TEXT].bold == Q_TRUE) {
                                        q_current_color |= Q_A_BOLD;
                                }
                                if (    (q_status.emulation == Q_EMUL_LINUX_UTF8) ||
                                        (q_status.emulation == Q_EMUL_LINUX)
                                ) {
                                        /* Linux console also flips underline */
                                        q_current_color |= Q_A_UNDERLINE;
                                }
                                break;
                        case 39:
                                foreground = q_text_colors[Q_COLOR_CONSOLE_TEXT].fg;
                                if (q_text_colors[Q_COLOR_CONSOLE_TEXT].bold == Q_TRUE) {
                                        q_current_color |= Q_A_BOLD;
                                }
                                if (    (q_status.emulation == Q_EMUL_LINUX_UTF8) ||
                                        (q_status.emulation == Q_EMUL_LINUX)
                                ) {
                                        /* Linux console also flips underline */
                                        q_current_color &= ~Q_A_UNDERLINE;
                                }
                                break;
                        case 40:
                                /* Set black background */
                                background = Q_COLOR_BLACK;
                                break;
                        case 41:
                                /* Set red background */
                                background = Q_COLOR_RED;
                                break;
                        case 42:
                                /* Set green background */
                                background = Q_COLOR_GREEN;
                                break;
                        case 43:
                                /* Set yellow background */
                                background = Q_COLOR_YELLOW;
                                break;
                        case 44:
                                /* Set blue background */
                                background = Q_COLOR_BLUE;
                                break;
                        case 45:
                                /* Set magenta background */
                                background = Q_COLOR_MAGENTA;
                                break;
                        case 46:
                                /* Set cyan background */
                                background = Q_COLOR_CYAN;
                                break;
                        case 47:
                                /* Set white background */
                                background = Q_COLOR_WHITE;
                                break;
                        case 49:
                                background = q_text_colors[Q_COLOR_CONSOLE_TEXT].bg;
                                break;

/*
       10    reset selected mapping, display control flag,
             and toggle meta flag.
       11    select null mapping, set display control flag,
             reset toggle meta flag.
       12    select null mapping, set display control flag,
             set toggle meta flag. (The toggle meta flag
             causes the high bit of a byte to be toggled
             before the mapping table translation is done.)
*/

                        } /* switch (j) */
#ifdef DEBUG_LINUX
                        fprintf(LINUX_DEBUG_FILE_HANDLE, "sgr(): new foreground = %02x new background = %02x\n", foreground, background);
                        fprintf(LINUX_DEBUG_FILE_HANDLE, "SGR: old color=%08x ", (unsigned)q_current_color);
#endif /* DEBUG_LINUX */

                        /* Wipe out the existing colors and replace */
                        curses_color = (foreground << 3) | background;
                        q_current_color = q_current_color & NO_COLOR_MASK;
                        q_current_color |= color_to_attr(curses_color);

#ifdef DEBUG_LINUX
                        fprintf(LINUX_DEBUG_FILE_HANDLE, "curses_color=%02x q_current_color=%08x\n", curses_color, (unsigned)q_current_color);
#endif /* DEBUG_LINUX */

                } /* for (i = 0; i <= state.params_n; i++) */

        } /* if (state.params_n == 0) */

} /* ---------------------------------------------------------------------- */

/*
 * DSR - Device status report
 */
static void dsr() {
        int i;
        char response_buffer[VT100_RESPONSE_LENGTH];

        if (state.params_n >= 0) {
                i = atoi((char *)state.params[0]);
        } else {
                i = 0;
        }

#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "dsr(): %d\n", i);
#endif /* DEBUG_LINUX */

        switch (i) {

        case 5:
                /* Request status report.
                   Respond with "OK, no malfunction." */

                /* Send string directly to remote side */
                qodem_write(q_child_tty_fd, "\033[0n", 4, Q_TRUE);
                break;

        case 6:
                /* Request cursor position.
                   Respond with current position */

                memset(response_buffer, 0, sizeof(response_buffer));
                snprintf(response_buffer, sizeof(response_buffer), "\033[%u;%uR", q_status.cursor_y + 1, q_status.cursor_x + 1);
                /* Send string directly to remote side */
                qodem_write(q_child_tty_fd, response_buffer, strlen(response_buffer), Q_TRUE);
                break;

        case 15:
                if (state.dec_private_mode_flag == Q_TRUE) {

                        /* Request printer status report.
                           Respond with "Printer not connected." */

                        /* Send string directly to remote side */
                        qodem_write(q_child_tty_fd, "\033[?13n", 6, Q_TRUE);
                }
                break;
        }
} /* ---------------------------------------------------------------------- */

/*
 * DA - Device attributes
 */
static void da() {
        int i;
        char response_buffer[VT100_RESPONSE_LENGTH];
        int extended_flag = 0;
        char ch[2];

        if (q_emul_buffer_n > 0) {
                if (q_emul_buffer[0] == '>') {
                        extended_flag = 1;
                        if (q_emul_buffer_n > 1) {
                                ch[0] = q_emul_buffer[1];
                                ch[1] = 0;
                                i = atoi(ch);
                        } else {
                                i = 0;
                        }

                } else if (q_emul_buffer[0] == '=') {
                        extended_flag = 2;
                        if (q_emul_buffer_n > 1) {
                                ch[0] = q_emul_buffer[1];
                                ch[1] = 0;
                                i = atoi(ch);
                        } else {
                                i = 0;
                        }
                } else {
                        /* Unknown code. */
                        return;
                }

        } else {
                i = 0;
        }

#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "da(): %d %d\n", extended_flag, i);
#endif /* DEBUG_LINUX */

        if ((i != 0) && (i != 1)) {
                return;
        }


        if ((extended_flag == 1) && (i == 0)) {
                /* Request "What type of terminal are you,
                   what is your firmware version, and what
                   hardware options do you have installed?"

                   Respond: "I am a VT220 (identification code of 1),
                   my firmware version is _____ (Pv), and I have
                   _____ Po options installed."

                   (Same as xterm)

                */

                memset(response_buffer, 0, sizeof(response_buffer));
                snprintf(response_buffer, sizeof(response_buffer), "\033[>0;10;0c");

                /* Send string directly to remote side */
                qodem_write(q_child_tty_fd, response_buffer, strlen(response_buffer), Q_TRUE);

        } else if ((extended_flag == 2) && (i == 0)) {

                /* Request "What is your unit ID?"

                   Respond: "I was terminal was manufactured at site 00 and have
                   a unique ID number of 123."

                */

                memset(response_buffer, 0, sizeof(response_buffer));
                snprintf(response_buffer, sizeof(response_buffer), "\033P!|00010203\033\\");

                /* Send string directly to remote side */
                qodem_write(q_child_tty_fd, response_buffer, strlen(response_buffer), Q_TRUE);

        } else if (i == 0) {
                /* Send string directly to remote side */
                qodem_write(q_child_tty_fd, LINUX_DEVICE_TYPE_STRING, sizeof(LINUX_DEVICE_TYPE_STRING), Q_TRUE);
        }

} /* ---------------------------------------------------------------------- */

/*
 * TBC - Tabulation clear
 */
static void tbc() {
        int i;
        i = atoi((char *)state.params[0]);

#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "tbc(): %d\n", i);
#endif /* DEBUG_LINUX */

        if (i == 0) {
                /* Clear the tab stop at this position */
                for (i=0; i<state.tab_stops_n; i++) {
                        if (state.tab_stops[i] > q_status.cursor_x) {
                                /* No tab stop here */
                                return;
                        }
                        if (state.tab_stops[i] == q_status.cursor_x) {
                                /* Remove this tab stop */
                                memmove(&state.tab_stops[i], &state.tab_stops[i+1], (state.tab_stops_n - i - 1) * sizeof(int));
                                state.tab_stops = (int *)Xrealloc(state.tab_stops, (state.tab_stops_n - 1)*sizeof(int), __FILE__, __LINE__);
                                state.tab_stops_n--;
                                return;
                        }
                }

                /* If we get here, the array ended before we found a tab stop. */
                /* NOP */

        } else if (i == 3) {
                /* Clear all tab stops */
                /* I believe this means NO tabs whatsoever, need to check later... */
                Xfree(state.tab_stops, __FILE__, __LINE__);
                state.tab_stops = NULL;
                state.tab_stops_n = 0;
        }

} /* ---------------------------------------------------------------------- */

/*
 * CNL - Cursor down and to column 1
 */
static void cnl() {
        int i;

        if (state.params_n < 0) {
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "cnl(): 1\n");
#endif /* DEBUG_LINUX */
                cursor_down(1, Q_TRUE);
        } else {
                i = atoi((char *)state.params[0]);
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "cnl(): %d\n", i);
#endif /* DEBUG_LINUX */
                if (i <= 0) {
                        cursor_down(1, Q_TRUE);
                } else {
                        cursor_down(i, Q_TRUE);
                }
        }
        /* To column 0 */
        cursor_left(q_status.cursor_x, Q_TRUE);
} /* ---------------------------------------------------------------------- */

/*
 * CPL - Cursor up and to column 1
 */
static void cpl() {
        int i;

        if (state.params_n < 0) {
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "cpl(): 1\n");
#endif /* DEBUG_LINUX */
                cursor_up(1, Q_TRUE);
        } else {
                i = atoi((char *)state.params[0]);
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "cpl(): %d\n", i);
#endif /* DEBUG_LINUX */
                if (i <= 0) {
                        cursor_up(1, Q_TRUE);
                } else {
                        cursor_up(i, Q_TRUE);
                }
        }
        /* To column 0 */
        cursor_left(q_status.cursor_x, Q_TRUE);
} /* ---------------------------------------------------------------------- */

/*
 * CHA - Cursor to column # in current row
 */
static void cha() {
        int i;

        if (state.params_n < 0) {
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "cha(): 1\n");
#endif /* DEBUG_LINUX */
                cursor_position(q_status.cursor_y, 0);
        } else {
                i = atoi((char *)state.params[0]) - 1;
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "cha(): %d\n", i);
#endif /* DEBUG_LINUX */
                cursor_position(q_status.cursor_y, i);
        }
} /* ---------------------------------------------------------------------- */

/*
 * ECH - Erase # of characters in current row
 */
static void ech() {
        int i;

        if (state.params_n >= 0) {
                i = atoi((char *)state.params[0]);
        } else {
                i = 0;
        }

        if (i == 0) {
                i = 1;
        }

#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "ech(): %d\n", i);
#endif /* DEBUG_LINUX */

        /* Erase from here to i characters */
        erase_line(q_status.cursor_x, q_status.cursor_x + i - 1, Q_FALSE);
} /* ---------------------------------------------------------------------- */

/*
 * VPA - Cursor to row #, same column
 */
static void vpa() {
        int i;

        if (state.params_n < 0) {
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "vpa(): 1\n");
#endif /* DEBUG_LINUX */
                cursor_position(0, q_status.cursor_x);
        } else {
                i = atoi((char *)state.params[0]) - 1;
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "vpa(): %d\n", i);
#endif /* DEBUG_LINUX */
                cursor_position(i, q_status.cursor_x);
        }
} /* ---------------------------------------------------------------------- */

/*
 * osc_put - Handle the SCAN_OSC_STRING state
 */
static void osc_put(unsigned char linux_char) {

        /* Collect first */
        q_emul_buffer[q_emul_buffer_n] = linux_char;
        q_emul_buffer_n++;

        if ((q_status.emulation == Q_EMUL_LINUX) || (q_status.emulation == Q_EMUL_LINUX_UTF8)) {

#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "osc_put(): LINUX %c (%02x)\n", linux_char, linux_char);
#endif /* DEBUG_LINUX */
                if (q_emul_buffer[0] == 'R') {
                        /* ESC ] R - Reset palette */

                        /* Go to SCAN_GROUND state */
                        clear_params();
                        clear_collect_buffer();
                        scan_state = SCAN_GROUND;
                        return;

                } else if (q_emul_buffer[0] == 'P') {
                        /* ESC ] P nrrggbb - Set palette entry */
                        if (q_emul_buffer_n < 8) {
                                /* Still collecting characters for it */
                                return;
                        }
                        /* Go to SCAN_GROUND state */
                        clear_params();
                        clear_collect_buffer();
                        scan_state = SCAN_GROUND;
                        return;
                }

                /* Fall through to xterm checks */
        }

#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "osc_put(): XTERM %c (%02x)\n", linux_char, linux_char);
#endif /* DEBUG_LINUX */

        /* Xterm cases... */
        if (linux_char == 0x07) {
                /* Screen title */
                q_emul_buffer_n--;
                q_emul_buffer[q_emul_buffer_n] = 0;
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "osc_put(): xterm screen title: %s\n", q_emul_buffer);
#endif /* DEBUG_LINUX */
                /* Go to SCAN_GROUND state */
                clear_params();
                clear_collect_buffer();
                scan_state = SCAN_GROUND;
                return;
        }

} /* ---------------------------------------------------------------------- */

/*
 * linux_csi - Handle the private Linux CSI codes (CSI [ Pn ])
 */
static void linux_csi() {
        int i = 0;
        int j = 0;

        if (state.params_n < 0) {
                /* Invalid command */
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "linux_csi(): no command given\n");
#endif /* DEBUG_LINUX */
                return;
        }

        if (state.params_n >= 0) {
                i = atoi((char *)state.params[0]);
        }
        if (state.params_n >= 1) {
                j = atoi((char *)state.params[1]);
        }

        switch (i) {

        case 1:
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "linux_csi(): Set underline color to %04x\n", j);
#endif /* DEBUG_LINUX */
                /* NOP */
                break;

        case 2:
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "linux_csi(): Set dim color to %04x\n", j);
#endif /* DEBUG_LINUX */
                /* NOP */
                break;

        case 8:
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "linux_csi(): Set current pair as default\n");
#endif /* DEBUG_LINUX */
                /* NOP */
                break;

        case 9:
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "linux_csi(): Set screen blank timeout to %d minutes\n", j);
#endif /* DEBUG_LINUX */
                /* NOP */
                break;

        case 10:
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "linux_csi(): Set bell frequency to %d hertz\n", j);
#endif /* DEBUG_LINUX */
                q_linux_beep_frequency = j;
                break;

        case 11:
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "linux_csi(): Set bell duration to %d milliseconds\n", j);
#endif /* DEBUG_LINUX */
                q_linux_beep_duration = j;
                break;

        case 12:
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "linux_csi(): Bring console %d to front\n", j);
#endif /* DEBUG_LINUX */
                /* NOP */
                break;

        case 13:
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "linux_csi(): Unblank screen\n");
#endif /* DEBUG_LINUX */
                /* NOP */
                break;

        case 14:
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "linux_csi(): Set VESA powerdown interval to %d minutes\n", j);
#endif /* DEBUG_LINUX */
                /* NOP */
                break;

        default:
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "linux_csi(): Unknown command %d %d\n", i, j);
#endif /* DEBUG_LINUX */
                /* NOP */
                break;

        } /* switch (i) */

} /* ---------------------------------------------------------------------- */

/*
 * REP - Repeat character
 */
static void rep() {
        int i;

        if (state.params_n < 0) {
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "rep(): 1\n");
#endif /* DEBUG_LINUX */
                print_character(state.rep_ch);
        } else {
                i = atoi((char *)state.params[0]);
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "rep(): %d\n", i);
#endif /* DEBUG_LINUX */
                if (i <= 0) {
                        print_character(state.rep_ch);
                } else {
                        while (i > 0) {
                                print_character(state.rep_ch);
                                i--;
                        }
                }
        }
} /* ---------------------------------------------------------------------- */

/*
 * SU - Scroll up
 */
static void su() {
        int i;

        if (state.params_n < 0) {
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "su(): 1\n");
#endif /* DEBUG_LINUX */
                /* Default 1 */
                scrolling_region_scroll_up(q_status.scroll_region_top, q_status.scroll_region_bottom, 1);
        } else {
                i = atoi((char *)state.params[0]);
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "su(): %d\n", i);
#endif /* DEBUG_LINUX */
                if (i <= 0) {
                        /* Default 1 */
                        scrolling_region_scroll_up(q_status.scroll_region_top, q_status.scroll_region_bottom, 1);
                } else {
                        scrolling_region_scroll_up(q_status.scroll_region_top, q_status.scroll_region_bottom, i);
                }
        }
} /* ---------------------------------------------------------------------- */

/*
 * SD - Scroll down
 */
static void sd() {
        int i;

        if (state.params_n < 0) {
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "sd(): 1\n");
#endif /* DEBUG_LINUX */
                /* Default 1 */
                scrolling_region_scroll_down(q_status.scroll_region_top, q_status.scroll_region_bottom, 1);
        } else {
                i = atoi((char *)state.params[0]);
#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "sd(): %d\n", i);
#endif /* DEBUG_LINUX */
                if (i <= 0) {
                        /* Default 1 */
                        scrolling_region_scroll_down(q_status.scroll_region_top, q_status.scroll_region_bottom, 1);
                } else {
                        scrolling_region_scroll_down(q_status.scroll_region_top, q_status.scroll_region_bottom, i);
                }
        }
} /* ---------------------------------------------------------------------- */

/*
 * CBT - Go back X tab stops
 */
static void cbt() {
        int i, j, tab_i;
        int tabs_to_move = 0;

        if (state.params_n < 0) {
                /* Default 1 */
                tabs_to_move = 1;
        } else {
                i = atoi((char *)state.params[0]);
                if (i <= 0) {
                        /* Default 1 */
                        tabs_to_move = 1;
                } else {
                        tabs_to_move = i;
                }
        }

#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "cbt(): %d\n", tabs_to_move);
#endif /* DEBUG_LINUX */

        for (i = 0; i < tabs_to_move; i++) {
                j = q_status.cursor_x;
                for (tab_i = 0; tab_i < state.tab_stops_n; tab_i++) {
                        if (state.tab_stops[tab_i] >= q_status.cursor_x) {
                                break;
                        }
                }
                tab_i--;
                if (tab_i <= 0) {
                        j = 0;
                } else {
                        j = state.tab_stops[tab_i];
                }
                cursor_position(q_status.cursor_y, j);
        }
} /* ---------------------------------------------------------------------- */

/*
 * CHT - Advance X tab stops
 */
static void cht() {
        int i;
        int tabs_to_move = 0;

        if (state.params_n < 0) {
                /* Default 1 */
                tabs_to_move = 1;
        } else {
                i = atoi((char *)state.params[0]);
                if (i <= 0) {
                        /* Default 1 */
                        tabs_to_move = 1;
                } else {
                        tabs_to_move = i;
                }
        }

#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "cht(): %d\n", tabs_to_move);
#endif /* DEBUG_LINUX */

        for (i = 0; i < tabs_to_move; i++) {
                advance_to_next_tab_stop();
        }
} /* ---------------------------------------------------------------------- */

/*
 * linux_emulator - process through LINUX emulator.
 */
Q_EMULATION_STATUS linux_emulator(const unsigned char from_modem, wchar_t * to_screen) {
        Q_BOOL discard = Q_FALSE;
        uint32_t last_utf8_state;

#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "STATE: %d CHAR: 0x%02x '%c'\n", scan_state, from_modem, from_modem);
        fflush(LINUX_DEBUG_FILE_HANDLE);
#endif /* DEBUG_LINUX */

        if ((q_status.emulation == Q_EMUL_LINUX_UTF8) || (q_status.emulation == Q_EMUL_XTERM_UTF8) || (q_status.emulation == Q_EMUL_XTERM)) {

#ifdef DEBUG_LINUX_UTF8
                fprintf(LINUX_DEBUG_FILE_HANDLE, "    UTF-8: decode before VTxxx state: %d\n",
                        state.utf8_state);
#endif /* DEBUG_LINUX_UTF8 */

                last_utf8_state = state.utf8_state;
                utf8_decode(&state.utf8_state, &state.utf8_char, from_modem);

#ifdef DEBUG_LINUX_UTF8
                fprintf(LINUX_DEBUG_FILE_HANDLE, "    UTF-8: decode state: %d\n",
                        state.utf8_state);
#endif /* DEBUG_LINUX_UTF8 */
                if ((last_utf8_state == state.utf8_state) && (state.utf8_state != UTF8_ACCEPT)) {
                        /* Bad character, reset UTF8 decoder state */
                        state.utf8_state = 0;

                        /* Discard character */
                        *to_screen = 1;
                        return Q_EMUL_FSM_NO_CHAR_YET;
                }

                if (state.utf8_state != UTF8_ACCEPT) {
                        /* Not enough characters to convert yet */
                        *to_screen = 1;
                        return Q_EMUL_FSM_NO_CHAR_YET;
                }

        }

        /* Special "anywhere" states */

        /* 18                         --> execute, then switch to SCAN_GROUND */
        if (from_modem == 0x18) {
                if (scan_state == SCAN_GROUND) {
                        /*
                         * CAN aborts an escape sequence, but it is also
                         * used as up-arrow.
                         */
                        print_character(cp437_chars[UPARROW]);
                } else {
                        /* CAN aborts escape sequences */
                        clear_params();
                        clear_collect_buffer();
                        scan_state = SCAN_GROUND;
                }
                discard = Q_TRUE;
        }

        /* 19                         --> printable */
        if ((from_modem == 0x19) && (scan_state == SCAN_GROUND)) {
                /*
                 * EM is down-arrow.
                 */
                print_character(cp437_chars[DOWNARROW]);
                discard = Q_TRUE;
        }

        /* 1A                         --> execute, then switch to SCAN_GROUND */
        if (from_modem == 0x1A) {
                /* SUB aborts escape sequences */
                clear_params();
                clear_collect_buffer();
                scan_state = SCAN_GROUND;
                discard = Q_TRUE;
        }

        /* 80-8F, 91-97, 99, 9A, 9C   --> execute, then switch to SCAN_GROUND */

        /* 0x1B == KEY_ESCAPE */
        if (from_modem == KEY_ESCAPE) {
                scan_state = SCAN_ESCAPE;
                discard = Q_TRUE;
        }

        /*
         * 0x9B == CSI 8-bit sequence: not recognized by linux or xterm
         */

        /* 0x9D goes to SCAN_OSC_STRING: not recognized by linux or xterm */

        /* 0x90 goes to SCAN_DCS_ENTRY: not recognized by linux or xterm */

        /* 0x98, 0x9E, and 0x9F go to SCAN_SOSPMAPC_STRING: not recognized by linux or xterm */

        /* If the character has been consumed, exit. */
        if (discard == Q_TRUE) {
                *to_screen = 1;
                return Q_EMUL_FSM_NO_CHAR_YET;
        }

        switch (scan_state) {

        case SCAN_GROUND:
                /* 00-17, 19, 1C-1F --> execute */
                if (from_modem <= 0x1F) {
                        handle_control_char(from_modem);
                        discard = Q_TRUE;
                        break;
                }

                /* 20-7F            --> print */
                if ((from_modem >= 0x20) && (from_modem <= 0x7F)) {
                        /* Immediately return this character */
                        *to_screen = map_character(from_modem);


#ifdef DEBUG_LINUX
                        /* render_screen_to_debug_file(LINUX_DEBUG_FILE_HANDLE); */
#endif /* DEBUG_LINUX */

                        state.rep_ch = *to_screen;
                        return Q_EMUL_FSM_ONE_CHAR;
                }

                /* 80-8F, 91-9A, 9C --> execute */

                break;

        case SCAN_ESCAPE:
                /* 00-17, 19, 1C-1F --> execute */
                if (from_modem <= 0x1F) {
                        handle_control_char(from_modem);
                        discard = Q_TRUE;
                        break;
                }

                /* 20-2F            --> collect, then switch to SCAN_ESCAPE_INTERMEDIATE */
                if ((from_modem >= 0x20) && (from_modem <= 0x2F)) {
                        collect(from_modem, to_screen);
                        scan_state = SCAN_ESCAPE_INTERMEDIATE;
                        discard = Q_TRUE;
                        break;
                }

                /* 30-4F, 51-57, 59, 5A, 5C, 60-7E   --> dispatch, then switch to SCAN_GROUND */
                if ((from_modem >= 0x30) && (from_modem <= 0x4F)) {
                        switch (from_modem) {
                        case '0':
                        case '1':
                        case '2':
                        case '3':
                        case '4':
                        case '5':
                        case '6':
                                break;
                        case '7':
                                /* DECSC - Save cursor */
                                decsc();
                                break;

                        case '8':
                                /* DECRC - Restore cursor */
                                decrc();
                                break;

                        case '9':
                        case ':':
                        case ';':
                                break;
                        case '<':
                                if (state.vt52_mode == Q_TRUE) {
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "VT52: DECANM (Exit VT52 mode)\n");
#endif /* DEBUG_LINUX */
                                        /* DECANM - Enter ANSI mode */
                                        state.vt52_mode = Q_FALSE;
                                        q_linux_arrow_keys = Q_EMUL_VT100;
                                        q_linux_keypad_mode.emulation = Q_EMUL_VT100;

                                        /*
                                           From the VT102 docs: "You use ANSI mode to select
                                           most terminal features; the terminal uses the same
                                           features when it switches to VT52 mode. You cannot,
                                           however, change most of these features in VT52
                                           mode."

                                           In other words, do not reset any other attributes
                                           when switching between VT52 submode and ANSI.
                                        */
                                }
                                break;
                        case '=':
                                /* DECKPAM - Keypad application mode */
                                /* Note this code overlaps both ANSI and VT52 mode */
                                deckpam();
                                break;
                        case '>':
                                /* DECKPNM - Keypad numeric mode */
                                /* Note this code overlaps both ANSI and VT52 mode */
                                deckpnm();
                                break;
                        case '?':
                        case '@':
                                break;
                        case 'A':
                                if (state.vt52_mode == Q_TRUE) {
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "VT52: cursor_up(1)\n");
#endif /* DEBUG_LINUX */
                                        /* Cursor up, and stop at the top without scrolling */
                                        cursor_up(1, Q_FALSE);
                                }
                                break;
                        case 'B':
                                if (state.vt52_mode == Q_TRUE) {
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "VT52: cursor_down(1)\n");
#endif /* DEBUG_LINUX */
                                        /* Cursor down, and stop at the bottom without scrolling */
                                        cursor_down(1, Q_FALSE);
                                }
                                break;
                        case 'C':
                                if (state.vt52_mode == Q_TRUE) {
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "VT52: cursor_right(1)\n");
#endif /* DEBUG_LINUX */
                                        /* Cursor right, and stop at the right without scrolling */
                                        cursor_right(1, Q_FALSE);
                                }
                                break;
                        case 'D':
                                if (state.vt52_mode == Q_TRUE) {
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "VT52: cursor_left(1)\n");
#endif /* DEBUG_LINUX */
                                        /* Cursor left, and stop at the left without scrolling */
                                        cursor_left(1, Q_FALSE);
                                } else {
                                        /* IND - Index */
                                        ind();
                                }
                                break;
                        case 'E':
                                if (state.vt52_mode == Q_TRUE) {
                                        /* Nothing */
                                } else {
                                        /* NEL - Next line */
                                        nel();
                                }
                                break;
                        case 'F':
                                if (state.vt52_mode == Q_TRUE) {
                                        /* G0 --> Special graphics */
                                        state.g0_charset = CHARSET_DRAWING;
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "VT52: CHARSET CHANGE: DRAWING in G0\n");
#endif /* DEBUG_LINUX */
                                }
                                break;
                        case 'G':
                                if (state.vt52_mode == Q_TRUE) {
                                        /* G0 --> ASCII set */
                                        state.g0_charset = CHARSET_US;
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "VT52: CHARSET CHANGE: US (ASCII) in G0\n");
#endif /* DEBUG_LINUX */
                                }
                                break;
                        case 'H':
                                if (state.vt52_mode == Q_TRUE) {
                                        /* Cursor to home */
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "VT52: Cursor to home\n");
#endif /* DEBUG_LINUX */
                                        cursor_position(0, 0);
                                } else {
                                        /* HTS - Horizontal tabulation set */
                                        hts();
                                }
                                break;
                        case 'I':
                                if (state.vt52_mode == Q_TRUE) {
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "VT52: Reverse line feed\n");
#endif /* DEBUG_LINUX */
                                        /* Reverse line feed.  Same as RI. */
                                        ri();
                                }
                                break;
                        case 'J':
                                if (state.vt52_mode == Q_TRUE) {
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "VT52: erase_line %d %d\n", q_status.cursor_x, WIDTH - 1);
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "VT52: erase_screen %d %d %d %d\n", q_status.cursor_y + 1, 0, HEIGHT - STATUS_HEIGHT - 1, WIDTH - 1);
#endif /* DEBUG_LINUX */
                                        /* Erase to end of screen */
                                        erase_line(q_status.cursor_x, WIDTH - 1, Q_FALSE);
                                        erase_screen(q_status.cursor_y + 1, 0, HEIGHT - STATUS_HEIGHT - 1, WIDTH - 1, Q_FALSE);
                                }
                                break;
                        case 'K':
                                if (state.vt52_mode == Q_TRUE) {
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "VT52: erase_line %d, %d\n", q_status.cursor_x, WIDTH - 1);
#endif /* DEBUG_LINUX */
                                        /* Erase to end of line */
                                        erase_line(q_status.cursor_x, WIDTH - 1, Q_FALSE);
                                }
                                break;
                        case 'L':
                                break;
                        case 'M':
                                if (state.vt52_mode == Q_TRUE) {
                                        /* Nothing */
                                } else {
                                        /* RI - Reverse index */
                                        ri();
                                }
                                break;
                        case 'N':
                                /* SS2 */
                                /* Not supported */
                                break;
                        case 'O':
                                /* SS3 */
                                /* Not supported */
                                break;
                        }
                        clear_params();
                        clear_collect_buffer();
                        scan_state = SCAN_GROUND;
                        discard = Q_TRUE;
                        break;
                }
                if ((from_modem >= 0x51) && (from_modem <= 0x57)) {
                        switch (from_modem) {
                        case 'Q':
                        case 'R':
                        case 'S':
                        case 'T':
                        case 'U':
                        case 'V':
                        case 'W':
                                break;
                        }
                        clear_params();
                        clear_collect_buffer();
                        scan_state = SCAN_GROUND;
                        discard = Q_TRUE;
                        break;
                }
                if (from_modem == 0x59) {
                        /* 'Y' */
                        if (state.vt52_mode == Q_TRUE) {
                                scan_state = SCAN_VT52_DIRECT_CURSOR_ADDRESS;
                        } else {
                                clear_params();
                                clear_collect_buffer();
                                scan_state = SCAN_GROUND;
                        }
                        discard = Q_TRUE;
                        break;
                }
                if (from_modem == 0x5A) {
                        /* 'Z' */
                        if (state.vt52_mode == Q_TRUE) {
                                /* Identify */
                                /* Send string directly to remote side */
                                qodem_write(q_child_tty_fd, "\033/Z", 3, Q_TRUE);
                        } else {
                                /* DECID */
                                /* Send string directly to remote side */
                                qodem_write(q_child_tty_fd, LINUX_DEVICE_TYPE_STRING, sizeof(LINUX_DEVICE_TYPE_STRING), Q_TRUE);
                        }

                        clear_params();
                        clear_collect_buffer();
                        scan_state = SCAN_GROUND;
                        discard = Q_TRUE;
                        break;
                }
                if (from_modem == 0x5C) {
                        /* '\' */
                        clear_params();
                        clear_collect_buffer();
                        scan_state = SCAN_GROUND;
                        discard = Q_TRUE;
                        break;
                }

                /* VT52 cannot get to any of these other states */
                if (state.vt52_mode == Q_TRUE) {
                        clear_params();
                        clear_collect_buffer();
                        scan_state = SCAN_GROUND;
                        discard = Q_TRUE;
                        break;
                }

                if ((from_modem >= 0x60) && (from_modem <= 0x7E)) {
                        switch (from_modem) {
                        case '`':
                        case 'a':
                        case 'b':
                                break;
                        case 'c':
                                /* RIS - Reset to initial state */
                                linux_reset();
                                q_cursor_on();
                                /* Do I clear screen too? I think so... */
                                erase_screen(0, 0, HEIGHT - STATUS_HEIGHT - 1, WIDTH - 1, Q_FALSE);
                                break;
                        case 'd':
                        case 'e':
                        case 'f':
                        case 'g':
                        case 'h':
                        case 'i':
                        case 'j':
                        case 'k':
                        case 'l':
                        case 'm':
                        case 'n':
                        case 'o':
                        case 'p':
                        case 'q':
                        case 'r':
                        case 's':
                        case 't':
                        case 'u':
                        case 'v':
                        case 'w':
                        case 'x':
                        case 'y':
                        case 'z':
                        case '{':
                        case '|':
                        case '}':
                        case '~':
                                break;
                        }
                        clear_params();
                        clear_collect_buffer();
                        scan_state = SCAN_GROUND;
                        discard = Q_TRUE;
                        break;
                }

                /* 7F               --> ignore */
                if (from_modem == 0x7F) {
                        discard = Q_TRUE;
                        break;
                }

                /* 0x5B goes to SCAN_CSI_ENTRY */
                if (from_modem == 0x5B) {
                        scan_state = SCAN_CSI_ENTRY;
                        discard = Q_TRUE;
                        break;
                }

                /* 0x5D goes to SCAN_OSC_STRING */
                if (from_modem == 0x5D) {
                        scan_state = SCAN_OSC_STRING;
                        discard = Q_TRUE;
                        break;
                }

                /* 0x50 goes to SCAN_DCS_ENTRY */
                if (from_modem == 0x50) {
                        scan_state = SCAN_DCS_ENTRY;
                        discard = Q_TRUE;
                        break;
                }

                /* 0x58, 0x5E, and 0x5F go to SCAN_SOSPMAPC_STRING */
                if ((from_modem == 0x58) || (from_modem == 0x5E) || (from_modem == 0x5F)) {
                        scan_state = SCAN_SOSPMAPC_STRING;
                        discard = Q_TRUE;
                        break;
                }

                break;

        case SCAN_ESCAPE_INTERMEDIATE:
                /* 00-17, 19, 1C-1F    --> execute */
                if (from_modem <= 0x1F) {
                        handle_control_char(from_modem);
                        discard = Q_TRUE;
                        break;
                }

                /* 20-2F               --> collect */
                if ((from_modem >= 0x20) && (from_modem <= 0x2F)) {
                        collect(from_modem, to_screen);
                        discard = Q_TRUE;
                        break;
                }

                /* 30-7E               --> dispatch, then switch to SCAN_GROUND */
                if ((from_modem >= 0x30) && (from_modem <= 0x7E)) {
                        switch (from_modem) {
                        case '0':
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '(')) {
                                        /* G0 --> Special graphics */
                                        state.g0_charset = CHARSET_DRAWING;
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "CHARSET CHANGE: DRAWING in G0\n");
#endif /* DEBUG_LINUX */
                                }
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == ')')) {
                                        /* G1 --> Special graphics */
                                        state.g1_charset = CHARSET_DRAWING;
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "CHARSET CHANGE: DRAWING in G1\n");
#endif /* DEBUG_LINUX */
                                }
                                break;
                        case '1':
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '(')) {
                                        /* G0 --> Alternate character ROM standard character set */
                                        state.g0_charset = CHARSET_ROM;
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "CHARSET CHANGE: ROM in G0\n");
#endif /* DEBUG_LINUX */
                                }
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == ')')) {
                                        /* G1 --> Alternate character ROM standard character set */
                                        state.g1_charset = CHARSET_ROM;
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "CHARSET CHANGE: ROM in G1\n");
#endif /* DEBUG_LINUX */
                                }
                                break;
                        case '2':
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '(')) {
                                        /* G0 --> Alternate character ROM special graphics */
                                        state.g0_charset = CHARSET_ROM_SPECIAL;
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "CHARSET CHANGE: ROM ALTERNATE in G0\n");
#endif /* DEBUG_LINUX */
                                }
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == ')')) {
                                        /* G1 --> Alternate character ROM special graphics */
                                        state.g1_charset = CHARSET_ROM_SPECIAL;
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "CHARSET CHANGE: ROM ALTERNATE in G1\n");
#endif /* DEBUG_LINUX */
                                }
                                break;
                        case '3':
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '#')) {
                                        /* DECDHL - Double-height line (top half) */
                                        dechdl(Q_TRUE);
                                }
                                break;
                        case '4':
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '#')) {
                                        /* DECDHL - Double-height line (bottom half) */
                                        dechdl(Q_FALSE);
                                }
                                break;
                        case '5':
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '#')) {
                                        /* DECSWL - Single-width line */
                                        decswl();
                                }
                                break;
                        case '6':
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '#')) {
                                        /* DECDWL - Double-width line */
                                        decdwl();
                                }
                                break;
                        case '7':
                                break;
                        case '8':
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '#')) {
                                        /* DECALN - Screen alignment display */
                                        decaln();
                                }
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '(')) {
                                        /* ESC % G --> Select UTF-8 (Obsolete) */
                                }
                                break;
                        case '9':
                        case ':':
                        case ';':
                        case '<':
                        case '=':
                        case '>':
                        case '?':
                                break;
                        case '@':
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '(')) {
                                        /* ESC % @ --> Select default font (ISO 646 / ISO 8859-1) */
                                }
                                break;
                        case 'A':
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '(')) {
                                        /* G0 --> United Kingdom set */
                                        state.g0_charset = CHARSET_UK;
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "CHARSET CHANGE: UK (BRITISH) in G0\n");
#endif /* DEBUG_LINUX */
                                }
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == ')')) {
                                        /* G1 --> United Kingdom set */
                                        state.g1_charset = CHARSET_UK;
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "CHARSET CHANGE: UK (BRITISH) in G1\n");
#endif /* DEBUG_LINUX */
                                }
                                break;
                        case 'B':
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '(')) {
                                        /* G0 --> ASCII set */
                                        state.g0_charset = CHARSET_US;
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "CHARSET CHANGE: US (ASCII) in G0\n");
#endif /* DEBUG_LINUX */
                                }
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == ')')) {
                                        /* G1 --> ASCII set */
                                        state.g1_charset = CHARSET_US;
#ifdef DEBUG_LINUX
                                        fprintf(LINUX_DEBUG_FILE_HANDLE, "CHARSET CHANGE: US (ASCII) in G1\n");
#endif /* DEBUG_LINUX */
                                }
                                break;
                        case 'C':
                        case 'D':
                        case 'E':
                        case 'F':
                                break;
                        case 'G':
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '(')) {
                                        /* ESC % G --> Select UTF-8 */
                                }
                                break;
                        case 'H':
                        case 'I':
                        case 'J':
                        case 'K':
                        case 'L':
                        case 'M':
                        case 'N':
                        case 'O':
                        case 'P':
                        case 'Q':
                        case 'R':
                        case 'S':
                        case 'T':
                        case 'U':
                        case 'V':
                        case 'W':
                        case 'X':
                        case 'Y':
                        case 'Z':
                        case '[':
                        case '\\':
                        case ']':
                        case '^':
                        case '_':
                        case '`':
                        case 'a':
                        case 'b':
                        case 'c':
                        case 'd':
                        case 'e':
                        case 'f':
                        case 'g':
                        case 'h':
                        case 'i':
                        case 'j':
                        case 'k':
                        case 'l':
                        case 'm':
                        case 'n':
                        case 'o':
                        case 'p':
                        case 'q':
                        case 'r':
                        case 's':
                        case 't':
                        case 'u':
                        case 'v':
                        case 'w':
                        case 'x':
                        case 'y':
                        case 'z':
                        case '{':
                        case '|':
                        case '}':
                        case '~':
                                break;
                        }
                        clear_params();
                        clear_collect_buffer();
                        scan_state = SCAN_GROUND;
                        discard = Q_TRUE;
                        break;
                }

                /* 7F                  --> ignore */
                if (from_modem <= 0x7F) {
                        discard = Q_TRUE;
                        break;
                }

                /* 0x9C goes to SCAN_GROUND */
                if (from_modem == 0x9C) {
                        clear_params();
                        clear_collect_buffer();
                        scan_state = SCAN_GROUND;
                        discard = Q_TRUE;
                        break;
                }

                break;

        case SCAN_CSI_ENTRY:
                /* 00-17, 19, 1C-1F    --> execute */
                if (from_modem <= 0x1F) {
                        handle_control_char(from_modem);
                        discard = Q_TRUE;
                        break;
                }

                /* 20-2F               --> collect, then switch to SCAN_CSI_INTERMEDIATE */
                if ((from_modem >= 0x20) && (from_modem <= 0x2F)) {
                        collect(from_modem, to_screen);
                        scan_state = SCAN_CSI_INTERMEDIATE;
                        discard = Q_TRUE;
                        break;
                }

                /* 30-39, 3B           --> param, then switch to SCAN_CSI_PARAM */
                if ((from_modem >= '0') && (from_modem <= '9')) {
                        param(from_modem);
                        scan_state = SCAN_CSI_PARAM;
                        discard = Q_TRUE;
                        break;
                }
                if (from_modem == ';') {
                        param(from_modem);
                        scan_state = SCAN_CSI_PARAM;
                        discard = Q_TRUE;
                        break;
                }

                /* 3C-3F               --> collect, then switch to SCAN_CSI_PARAM */
                if ((from_modem >= 0x3C) && (from_modem <= 0x3F)) {
                        collect(from_modem, to_screen);
                        scan_state = SCAN_CSI_PARAM;
                        discard = Q_TRUE;
                        break;
                }

                /* 40-7E               --> dispatch, then switch to SCAN_GROUND */
                if ((from_modem >= 0x40) && (from_modem <= 0x7E)) {
                        switch (from_modem) {
                        case '@':
                                /* ICH - Insert character */
                                ich();
                                break;
                        case 'A':
                                /* CUU - Cursor up */
                                cuu();
                                break;
                        case 'B':
                                /* CUD - Cursor down */
                                cud();
                                break;
                        case 'C':
                                /* CUF - Cursor forward */
                                cuf();
                                break;
                        case 'D':
                                /* CUB - Cursor backward */
                                cub();
                                break;
                        case 'E':
                                /* CNL - Cursor down and to column 1 */
                                cnl();
                                break;
                        case 'F':
                                /* CPL - Cursor up and to column 1 */
                                cpl();
                                break;
                        case 'G':
                                /* CHA - Cursor to column # in current row */
                                cha();
                                break;
                        case 'H':
                                /* CUP - Cursor position */
                                cup();
                                break;
                        case 'I':
                                /* CHT - Cursor forward X tab stops (default 1) */
                                if (    (q_status.emulation == Q_EMUL_XTERM) ||
                                        (q_status.emulation == Q_EMUL_XTERM_UTF8)
                                ) {
                                        cht();
                                }
                                break;
                        case 'J':
                                /* ED - Erase in display */
                                ed();
                                break;
                        case 'K':
                                /* EL - Erase in line */
                                el();
                                break;
                        case 'L':
                                /* IL - Insert line */
                                il();
                                break;
                        case 'M':
                                /* DL - Delete line */
                                dl();
                                break;
                        case 'N':
                        case 'O':
                                break;
                        case 'P':
                                /* DCH - Delete character */
                                dch();
                                break;
                        case 'Q':
                        case 'R':
                                break;
                        case 'S':
                                /* Scroll up X lines (default 1) */
                                if (    (q_status.emulation == Q_EMUL_XTERM) ||
                                        (q_status.emulation == Q_EMUL_XTERM_UTF8)
                                ) {
                                        su();
                                }
                                break;
                        case 'T':
                                /* Scroll down X lines (default 1) */
                                if (    (q_status.emulation == Q_EMUL_XTERM) ||
                                        (q_status.emulation == Q_EMUL_XTERM_UTF8)
                                ) {
                                        sd();
                                }
                                break;
                        case 'U':
                        case 'V':
                        case 'W':
                                break;
                        case 'X':
                                /* ECH - Erase # of characters in current row */
                                ech();
                                break;
                        case 'Y':
                                break;
                        case 'Z':
                                /* CBT - Cursor backward X tab stops (default 1) */
                                cbt();
                                break;
                        case '[':
                        case '\\':
                                break;
                        case ']':
                                /* Linux mode private CSI sequence OR xterm OSC */
                                linux_csi();
                                break;
                        case '^':
                        case '_':
                                break;
                        case '`':
                                /* HPA - Cursor to column # in current row.  Same as CHA */
                                cha();
                                break;
                        case 'a':
                                /* HPR - Cursor right.  Same as CUF */
                                cuf();
                                break;
                        case 'b':
                                /* REP - Repeat last char X times */
                                if (    (q_status.emulation == Q_EMUL_XTERM) ||
                                        (q_status.emulation == Q_EMUL_XTERM_UTF8)
                                ) {
                                        rep();
                                }
                                break;
                        case 'c':
                                /* DA - Device attributes */
                                /* Send string directly to remote side */
                                qodem_write(q_child_tty_fd, LINUX_DEVICE_TYPE_STRING, sizeof(LINUX_DEVICE_TYPE_STRING), Q_TRUE);
                                break;
                        case 'd':
                                /* VPA - Cursor to row, current column. */
                                vpa();
                                break;
                        case 'e':
                                /* VPR - Cursor down.  Same as CUD */
                                cud();
                                break;
                        case 'f':
                                /* HVP - Horizontal and vertical position */
                                hvp();
                                break;
                        case 'g':
                                /* TBC - Tabulation clear */
                                tbc();
                                break;
                        case 'h':
                                /* Sets an ANSI or DEC private toggle */
                                set_toggle(Q_TRUE);
                                break;
                        case 'i':
                        case 'j':
                        case 'k':
                                break;
                        case 'l':
                                /* Sets an ANSI or DEC private toggle */
                                set_toggle(Q_FALSE);
                                break;
                        case 'm':
                                /* SGR - Select graphics rendition */
                                sgr();
                                break;
                        case 'n':
                                /* DSR - Device status report */
                                dsr();
                                break;
                        case 'o':
                        case 'p':
                                break;
                        case 'q':
                                /* DECLL - Load leds */
                                decll();
                                break;
                        case 'r':
                                /* DECSTBM - Set top and bottom margins */
                                decstbm();
                                break;
                        case 's':
                                /* Save cursor (ANSI.SYS) */
                                if (    (q_status.emulation == Q_EMUL_XTERM) ||
                                        (q_status.emulation == Q_EMUL_XTERM_UTF8)
                                ) {
                                        state.saved_cursor_x = q_status.cursor_x;
                                        state.saved_cursor_y = q_status.cursor_y;
                                }
                                break;
                        case 't':
                                break;
                        case 'u':
                                /* Restore cursor (ANSI.SYS) */
                                if (    (q_status.emulation == Q_EMUL_XTERM) ||
                                        (q_status.emulation == Q_EMUL_XTERM_UTF8)
                                ) {
                                        if (state.saved_cursor_x != -1) {
                                                cursor_position(state.saved_cursor_y, state.saved_cursor_x);
                                        }
                                }
                                break;
                        case 'v':
                        case 'w':
                                break;
                        case 'x':
                                /* DECREQTPARM - Request terminal parameters */
                                decreqtparm();
                                break;
                        case 'y':
                        case 'z':
                        case '{':
                        case '|':
                        case '}':
                        case '~':
                                break;
                        }
                        clear_params();
                        clear_collect_buffer();
                        scan_state = SCAN_GROUND;
                        discard = Q_TRUE;
                        break;
                }

                /* 7F                  --> ignore */
                if (from_modem <= 0x7F) {
                        discard = Q_TRUE;
                        break;
                }

                /* 0x9C goes to SCAN_GROUND */
                if (from_modem == 0x9C) {
                        clear_params();
                        clear_collect_buffer();
                        scan_state = SCAN_GROUND;
                        discard = Q_TRUE;
                        break;
                }

                /* 0x3A goes to SCAN_CSI_IGNORE */
                if (from_modem == 0x3A) {
                        scan_state = SCAN_CSI_IGNORE;
                        discard = Q_TRUE;
                        break;
                }

                break;

        case SCAN_CSI_PARAM:
                /* 00-17, 19, 1C-1F    --> execute */
                if (from_modem <= 0x1F) {
                        handle_control_char(from_modem);
                        discard = Q_TRUE;
                        break;
                }

                /* 20-2F               --> collect, then switch to SCAN_CSI_INTERMEDIATE */
                if ((from_modem >= 0x20) && (from_modem <= 0x2F)) {
                        collect(from_modem, to_screen);
                        scan_state = SCAN_CSI_INTERMEDIATE;
                        discard = Q_TRUE;
                        break;
                }

                /* 30-39, 3B           --> param */
                if ((from_modem >= '0') && (from_modem <= '9')) {
                        param(from_modem);
                        discard = Q_TRUE;
                        break;
                }
                if (from_modem == ';') {
                        param(from_modem);
                        discard = Q_TRUE;
                        break;
                }

                /* 0x3A goes to SCAN_CSI_IGNORE */
                if (from_modem == 0x3A) {
                        scan_state = SCAN_CSI_IGNORE;
                        discard = Q_TRUE;
                        break;
                }
                /* 0x3C-3F goes to SCAN_CSI_IGNORE */
                if ((from_modem >= 0x3C) && (from_modem <= 0x3F)) {
                        scan_state = SCAN_CSI_IGNORE;
                        discard = Q_TRUE;
                        break;
                }

                /* 40-7E               --> dispatch, then switch to SCAN_GROUND */
                if ((from_modem >= 0x40) && (from_modem <= 0x7E)) {
                        switch (from_modem) {
                        case '@':
                                /* ICH - Insert character */
                                ich();
                                break;
                        case 'A':
                                /* CUU - Cursor up */
                                cuu();
                                break;
                        case 'B':
                                /* CUD - Cursor down */
                                cud();
                                break;
                        case 'C':
                                /* CUF - Cursor forward */
                                cuf();
                                break;
                        case 'D':
                                /* CUB - Cursor backward */
                                cub();
                                break;
                        case 'E':
                                /* CNL - Cursor down and to column 1 */
                                cnl();
                                break;
                        case 'F':
                                /* CPL - Cursor up and to column 1 */
                                cpl();
                                break;
                        case 'G':
                                /* CHA - Cursor to column # in current row */
                                cha();
                                break;
                        case 'H':
                                /* CUP - Cursor position */
                                cup();
                                break;
                        case 'I':
                                /* CHT - Cursor forward X tab stops (default 1) */
                                if (    (q_status.emulation == Q_EMUL_XTERM) ||
                                        (q_status.emulation == Q_EMUL_XTERM_UTF8)
                                ) {
                                        cht();
                                }
                                break;
                        case 'J':
                                /* ED - Erase in display */
                                ed();
                                break;
                        case 'K':
                                /* EL - Erase in line */
                                el();
                                break;
                        case 'L':
                                /* IL - Insert line */
                                il();
                                break;
                        case 'M':
                                /* DL - Delete line */
                                dl();
                                break;
                        case 'N':
                        case 'O':
                                break;
                        case 'P':
                                /* DCH - Delete character */
                                dch();
                                break;
                        case 'Q':
                        case 'R':
                                break;
                        case 'S':
                                /* Scroll up X lines (default 1) */
                                if (    (q_status.emulation == Q_EMUL_XTERM) ||
                                        (q_status.emulation == Q_EMUL_XTERM_UTF8)
                                ) {
                                        su();
                                }
                                break;
                        case 'T':
                                /* Scroll down X lines (default 1) */
                                if (    (q_status.emulation == Q_EMUL_XTERM) ||
                                        (q_status.emulation == Q_EMUL_XTERM_UTF8)
                                ) {
                                        sd();
                                }
                                break;
                        case 'U':
                        case 'V':
                        case 'W':
                                break;
                        case 'X':
                                /* ECH - Erase # of characters in current row */
                                ech();
                                break;
                        case 'Y':
                                break;
                        case 'Z':
                                /* CBT - Cursor backward X tab stops (default 1) */
                                cbt();
                                break;
                        case '[':
                        case '\\':
                                break;
                        case ']':
                                /* Linux mode private CSI sequence */
                                linux_csi();
                                break;
                        case '^':
                        case '_':
                        case '`':
                                /* HPA - Cursor to column # in current row.  Same as CHA */
                                cha();
                                break;
                        case 'a':
                                /* HPR - Cursor right.  Same as CUF */
                                cuf();
                                break;
                        case 'b':
                                /* REP - Repeat last char X times */
                                if (    (q_status.emulation == Q_EMUL_XTERM) ||
                                        (q_status.emulation == Q_EMUL_XTERM_UTF8)
                                ) {
                                        rep();
                                }
                                break;
                        case 'c':
                                /* DA - Device attributes */
                                da();
                                break;
                        case 'd':
                                /* VPA - Cursor to row, current column. */
                                vpa();
                                break;
                        case 'e':
                                /* VPR - Cursor down.  Same as CUD */
                                cud();
                                break;
                        case 'f':
                                /* HVP - Horizontal and vertical position */
                                hvp();
                                break;
                        case 'g':
                                /* TBC - Tabulation clear */
                                tbc();
                                break;
                        case 'h':
                                /* Sets an ANSI or DEC private toggle */
                                set_toggle(Q_TRUE);
                                break;
                        case 'i':
                        case 'j':
                        case 'k':
                                break;
                        case 'l':
                                /* Sets an ANSI or DEC private toggle */
                                set_toggle(Q_FALSE);
                                break;
                        case 'm':
                                /* SGR - Select graphics rendition */
                                sgr();
                                break;
                        case 'n':
                                /* DSR - Device status report */
                                dsr();
                                break;
                        case 'o':
                        case 'p':
                                break;
                        case 'q':
                                /* DECLL - Load leds */
                                decll();
                                break;
                        case 'r':
                                /* DECSTBM - Set top and bottom margins */
                                decstbm();
                                break;
                        case 's':
                        case 't':
                        case 'u':
                        case 'v':
                        case 'w':
                                break;
                        case 'x':
                                /* DECREQTPARM - Request terminal parameters */
                                decreqtparm();
                                break;
                        case 'y':
                        case 'z':
                        case '{':
                        case '|':
                        case '}':
                        case '~':
                                break;
                        }
                        clear_params();
                        clear_collect_buffer();
                        scan_state = SCAN_GROUND;
                        discard = Q_TRUE;
                        break;
                }

                /* 7F                  --> ignore */
                if (from_modem <= 0x7F) {
                        discard = Q_TRUE;
                        break;
                }

                break;

        case SCAN_CSI_INTERMEDIATE:
                /* 00-17, 19, 1C-1F    --> execute */
                if (from_modem <= 0x1F) {
                        handle_control_char(from_modem);
                        discard = Q_TRUE;
                        break;
                }

                /* 20-2F               --> collect */
                if ((from_modem >= 0x20) && (from_modem <= 0x2F)) {
                        collect(from_modem, to_screen);
                        discard = Q_TRUE;
                        break;
                }

                /* 0x30-3F goes to SCAN_CSI_IGNORE */
                if ((from_modem >= 0x30) && (from_modem <= 0x3F)) {
                        scan_state = SCAN_CSI_IGNORE;
                        discard = Q_TRUE;
                        break;
                }

                /* 40-7E               --> dispatch, then switch to SCAN_GROUND */
                if ((from_modem >= 0x40) && (from_modem <= 0x7E)) {
                        switch (from_modem) {
                        case '@':
                        case 'A':
                        case 'B':
                        case 'C':
                        case 'D':
                        case 'E':
                        case 'F':
                        case 'G':
                        case 'H':
                        case 'I':
                        case 'J':
                        case 'K':
                        case 'L':
                        case 'M':
                        case 'N':
                        case 'O':
                        case 'P':
                        case 'Q':
                        case 'R':
                        case 'S':
                        case 'T':
                        case 'U':
                        case 'V':
                        case 'W':
                        case 'X':
                        case 'Y':
                        case 'Z':
                        case '[':
                        case '\\':
                        case ']':
                        case '^':
                        case '_':
                        case '`':
                        case 'a':
                        case 'b':
                        case 'c':
                        case 'd':
                        case 'e':
                        case 'f':
                        case 'g':
                        case 'h':
                        case 'i':
                        case 'j':
                        case 'k':
                        case 'l':
                        case 'm':
                        case 'n':
                        case 'o':
                                break;
                        case 'p':
                                if ((   (q_status.emulation == Q_EMUL_XTERM) ||
                                        (q_status.emulation == Q_EMUL_XTERM_UTF8)) &&
                                        (q_emul_buffer[q_emul_buffer_n-1] == '!')
                                ) {
                                        /* DECSTR */
                                        decstr();
                                }
                                break;
                        case 'q':
                                if ((   (q_status.emulation == Q_EMUL_XTERM) ||
                                        (q_status.emulation == Q_EMUL_XTERM_UTF8)) &&
                                        (q_emul_buffer[q_emul_buffer_n-1] == '\"')
                                ) {
                                        /* DECSCA */
                                        decsca();
                                }
                                break;
                        case 'r':
                        case 's':
                        case 't':
                        case 'u':
                        case 'v':
                        case 'w':
                        case 'x':
                        case 'y':
                        case 'z':
                        case '{':
                        case '|':
                        case '}':
                        case '~':
                                break;
                        }
                        clear_params();
                        clear_collect_buffer();
                        scan_state = SCAN_GROUND;
                        discard = Q_TRUE;
                        break;
                }

                /* 7F                  --> ignore */
                if (from_modem <= 0x7F) {
                        discard = Q_TRUE;
                        break;
                }

                break;

        case SCAN_CSI_IGNORE:
                /* 00-17, 19, 1C-1F    --> execute */
                if (from_modem <= 0x1F) {
                        handle_control_char(from_modem);
                        discard = Q_TRUE;
                        break;
                }

                /* 20-2F               --> collect */
                if ((from_modem >= 0x20) && (from_modem <= 0x2F)) {
                        collect(from_modem, to_screen);
                        discard = Q_TRUE;
                        break;
                }

                /* 40-7E               --> ignore, then switch to SCAN_GROUND */
                if ((from_modem >= 0x40) && (from_modem <= 0x7E)) {
                        clear_params();
                        clear_collect_buffer();
                        scan_state = SCAN_GROUND;
                        discard = Q_TRUE;
                        break;
                }

                /* 20-3F, 7F           --> ignore */
                if ((from_modem >= 0x20) && (from_modem <= 0x3F)) {
                        discard = Q_TRUE;
                        break;
                }
                if (from_modem <= 0x7F) {
                        discard = Q_TRUE;
                        break;
                }

                break;

        case SCAN_DCS_ENTRY:
                /* 20-2F               --> collect, then switch to SCAN_DCS_INTERMEDIATE */
                if ((from_modem >= 0x20) && (from_modem <= 0x2F)) {
                        collect(from_modem, to_screen);
                        scan_state = SCAN_DCS_INTERMEDIATE;
                        discard = Q_TRUE;
                        break;
                }

                /* 30-39, 3B           --> param, then switch to SCAN_DCS_PARAM */
                if ((from_modem >= '0') && (from_modem <= '9')) {
                        param(from_modem);
                        scan_state = SCAN_DCS_PARAM;
                        discard = Q_TRUE;
                        break;
                }
                if (from_modem == ';') {
                        param(from_modem);
                        scan_state = SCAN_DCS_PARAM;
                        discard = Q_TRUE;
                        break;
                }

                /* 3C-3F               --> collect, then switch to SCAN_DCS_PARAM */
                if ((from_modem >= 0x3C) && (from_modem <= 0x3F)) {
                        collect(from_modem, to_screen);
                        scan_state = SCAN_DCS_PARAM;
                        discard = Q_TRUE;
                        break;
                }

                /* 00-17, 19, 1C-1F, 7F    --> ignore */
                if (from_modem <= 0x17) {
                        discard = Q_TRUE;
                        break;
                }
                if (from_modem == 0x19) {
                        discard = Q_TRUE;
                        break;
                }
                if ((from_modem >= 0x1C) && (from_modem <= 0x1F)) {
                        discard = Q_TRUE;
                        break;
                }
                if (from_modem == 0x7F) {
                        discard = Q_TRUE;
                        break;
                }

                /* 0x3A goes to SCAN_DCS_IGNORE */
                if (from_modem == 0x3F) {
                        scan_state = SCAN_DCS_IGNORE;
                        discard = Q_TRUE;
                        break;
                }

                /* 0x40-7E goes to SCAN_DCS_PASSTHROUGH */
                if ((from_modem >= 0x40) && (from_modem <= 0x7E)) {
                        scan_state = SCAN_DCS_PASSTHROUGH;
                        discard = Q_TRUE;
                        break;
                }

                break;

        case SCAN_DCS_INTERMEDIATE:
                /* 0x30-3F goes to SCAN_DCS_IGNORE */
                if ((from_modem >= 0x30) && (from_modem <= 0x3F)) {
                        scan_state = SCAN_DCS_IGNORE;
                        discard = Q_TRUE;
                        break;
                }

                /* 0x40-7E goes to SCAN_DCS_PASSTHROUGH */
                if ((from_modem >= 0x40) && (from_modem <= 0x7E)) {
                        scan_state = SCAN_DCS_PASSTHROUGH;
                        discard = Q_TRUE;
                        break;
                }

                /* 00-17, 19, 1C-1F, 7F    --> ignore */
                if (from_modem <= 0x17) {
                        discard = Q_TRUE;
                        break;
                }
                if (from_modem == 0x19) {
                        discard = Q_TRUE;
                        break;
                }
                if ((from_modem >= 0x1C) && (from_modem <= 0x1F)) {
                        discard = Q_TRUE;
                        break;
                }
                if (from_modem == 0x7F) {
                        discard = Q_TRUE;
                        break;
                }
                break;

        case SCAN_DCS_PARAM:
                /* 20-2F                   --> collect, then switch to SCAN_DCS_INTERMEDIATE */
                if ((from_modem >= 0x20) && (from_modem <= 0x2F)) {
                        collect(from_modem, to_screen);
                        scan_state = SCAN_DCS_INTERMEDIATE;
                        discard = Q_TRUE;
                        break;
                }

                /* 30-39, 3B               --> param */
                if ((from_modem >= '0') && (from_modem <= '9')) {
                        param(from_modem);
                        discard = Q_TRUE;
                        break;
                }
                if (from_modem == ';') {
                        param(from_modem);
                        discard = Q_TRUE;
                        break;
                }

                /* 00-17, 19, 1C-1F, 7F    --> ignore */
                if (from_modem <= 0x17) {
                        discard = Q_TRUE;
                        break;
                }
                if (from_modem == 0x19) {
                        discard = Q_TRUE;
                        break;
                }
                if ((from_modem >= 0x1C) && (from_modem <= 0x1F)) {
                        discard = Q_TRUE;
                        break;
                }
                if (from_modem == 0x7F) {
                        discard = Q_TRUE;
                        break;
                }

                /* 0x3A, 3C-3F goes to SCAN_DCS_IGNORE */
                if (from_modem == 0x3F) {
                        scan_state = SCAN_DCS_IGNORE;
                        discard = Q_TRUE;
                        break;
                }
                if ((from_modem >= 0x3C) && (from_modem <= 0x3F)) {
                        scan_state = SCAN_DCS_IGNORE;
                        discard = Q_TRUE;
                        break;
                }

                /* 0x40-7E goes to SCAN_DCS_PASSTHROUGH */
                if ((from_modem >= 0x40) && (from_modem <= 0x7E)) {
                        scan_state = SCAN_DCS_PASSTHROUGH;
                        discard = Q_TRUE;
                        break;
                }

                break;

        case SCAN_DCS_PASSTHROUGH:
                /* 00-17, 19, 1C-1F, 20-7E   --> put */

                /* 7F                        --> ignore */
                if (from_modem == 0x7F) {
                        discard = Q_TRUE;
                        break;
                }

                /* 0x9C goes to SCAN_GROUND */
                if (from_modem == 0x9C) {
                        clear_params();
                        clear_collect_buffer();
                        scan_state = SCAN_GROUND;
                        discard = Q_TRUE;
                        break;
                }

                break;

        case SCAN_DCS_IGNORE:
                /* 00-17, 19, 1C-1F, 20-7F --> ignore */
                if (from_modem <= 0x17) {
                        discard = Q_TRUE;
                        break;
                }
                if (from_modem == 0x19) {
                        discard = Q_TRUE;
                        break;
                }
                if ((from_modem >= 0x1C) && (from_modem <= 0x7F)) {
                        discard = Q_TRUE;
                        break;
                }

                /* 0x9C goes to SCAN_GROUND */
                if (from_modem == 0x9C) {
                        clear_params();
                        clear_collect_buffer();
                        scan_state = SCAN_GROUND;
                        discard = Q_TRUE;
                        break;
                }

                break;

        case SCAN_SOSPMAPC_STRING:
                /* 00-17, 19, 1C-1F, 20-7F --> ignore */
                if (from_modem <= 0x17) {
                        discard = Q_TRUE;
                        break;
                }
                if (from_modem == 0x19) {
                        discard = Q_TRUE;
                        break;
                }
                if ((from_modem >= 0x1C) && (from_modem <= 0x7F)) {
                        discard = Q_TRUE;
                        break;
                }

                /* 0x9C goes to SCAN_GROUND */
                if (from_modem == 0x9C) {
                        clear_params();
                        clear_collect_buffer();
                        scan_state = SCAN_GROUND;
                        discard = Q_TRUE;
                        break;
                }

                break;

        case SCAN_OSC_STRING:

                /*
                 * Special case for Xterm: OSC can pass control characters.
                 * Some linux emulations also use it erroneously, so parse it
                 * for linux too.
                 */
                if ((from_modem == 0x9C) || (from_modem <= 0x07)) {
                        osc_put(from_modem);
                        discard = Q_TRUE;
                        break;
                }

                /* 00-17, 19, 1C-1F        --> ignore */
                if (from_modem <= 0x17) {
                        discard = Q_TRUE;
                        break;
                }
                if (from_modem == 0x19) {
                        discard = Q_TRUE;
                        break;
                }
                if ((from_modem >= 0x1C) && (from_modem <= 0x1F)) {
                        discard = Q_TRUE;
                        break;
                }

                /* 20-7F                   --> osc_put */
                if ((from_modem >= 0x20) && (from_modem <= 0x7F)) {
                        osc_put(from_modem);
                        discard = Q_TRUE;
                        break;
                }

                /* 0x9C goes to SCAN_GROUND */
                if (from_modem == 0x9C) {
                        clear_params();
                        clear_collect_buffer();
                        scan_state = SCAN_GROUND;
                        discard = Q_TRUE;
                        break;
                }

                break;

        case SCAN_VT52_DIRECT_CURSOR_ADDRESS:
                /* This is a special case for the VT52 sequence "ESC Y l c" */
                if (q_emul_buffer_n == 0) {
                        collect(from_modem, to_screen);
                } else if (q_emul_buffer_n == 1) {
                        /* We've got the two characters, one in the buffer
                           and the other in from_modem. */
#ifdef DEBUG_LINUX
                        fprintf(LINUX_DEBUG_FILE_HANDLE, "VT52: cursor_position %d, %d\n", q_emul_buffer[0] - '\040', from_modem - '\040');
#endif /* DEBUG_LINUX */
                        cursor_position(q_emul_buffer[0] - '\040', from_modem - '\040');
                        clear_params();
                        clear_collect_buffer();
                        scan_state = SCAN_GROUND;
                }

                discard = Q_TRUE;
                break;
        }

#ifdef DEBUG_LINUX
        /* render_screen_to_debug_file(LINUX_DEBUG_FILE_HANDLE); */
#endif /* DEBUG_LINUX */

        /* If the character has been consumed, exit. */
        if (discard == Q_TRUE) {
                *to_screen = 1;
                return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (q_status.emulation == Q_EMUL_LINUX) {

#ifdef DEBUG_LINUX
                fprintf(LINUX_DEBUG_FILE_HANDLE, "Fell off the bottom, assume VGA CHAR: '%c' (0x%02x) --> '%uc' (0x%02x)\n",
                        from_modem,
                        from_modem,
                        codepage_map_char(from_modem),
                        codepage_map_char(from_modem));
#endif /* DEBUG_LINUX */
                *to_screen = codepage_map_char(from_modem);
                state.rep_ch = *to_screen;
                clear_params();
                clear_collect_buffer();
                scan_state = SCAN_GROUND;
                return Q_EMUL_FSM_ONE_CHAR;
        }

        /* UTF-8 char */
        assert((q_status.emulation == Q_EMUL_LINUX_UTF8) || (q_status.emulation == Q_EMUL_XTERM_UTF8) || (q_status.emulation == Q_EMUL_XTERM));
#ifdef DEBUG_LINUX
        fprintf(LINUX_DEBUG_FILE_HANDLE, "Fell off the bottom, assume UTF-8 CHAR: '%c' (0x%04x)\n",
                from_modem,
                state.utf8_char);
#endif /* DEBUG_LINUX */
        *to_screen = state.utf8_char;
        state.rep_ch = *to_screen;
        clear_params();
        clear_collect_buffer();
        scan_state = SCAN_GROUND;
        return Q_EMUL_FSM_ONE_CHAR;
} /* ---------------------------------------------------------------------- */

/*
 * linux_keystroke
 *
 * LINUX terminal is quite unlike VT10x for function keys.
 */
wchar_t * linux_keystroke(const int keystroke) {

        switch (keystroke) {
        case Q_KEY_BACKSPACE:
                if (q_status.hard_backspace == Q_TRUE) {
                        return L"\010";
                } else {
                        return L"\177";
                }

        case Q_KEY_LEFT:
                switch (q_linux_arrow_keys) {
                case Q_EMUL_ANSI:
                        return L"\033[D";
                case Q_EMUL_VT52:
                        return L"\033D";
                default:
                        return L"\033OD";
                }

        case Q_KEY_RIGHT:
                switch (q_linux_arrow_keys) {
                case Q_EMUL_ANSI:
                        return L"\033[C";
                case Q_EMUL_VT52:
                        return L"\033C";
                default:
                        return L"\033OC";
                }

        case Q_KEY_UP:
                switch (q_linux_arrow_keys) {
                case Q_EMUL_ANSI:
                        return L"\033[A";
                case Q_EMUL_VT52:
                        return L"\033A";
                default:
                        return L"\033OA";
                }

        case Q_KEY_DOWN:
                switch (q_linux_arrow_keys) {
                case Q_EMUL_ANSI:
                        return L"\033[B";
                case Q_EMUL_VT52:
                        return L"\033B";
                default:
                        return L"\033OB";
                }

        case Q_KEY_HOME:
                return L"\033[1~";

        case Q_KEY_END:
                return L"\033[4~";

        case Q_KEY_F(1):
                /* PF1 */
                switch (q_linux_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\033P";
                default:
                        return L"\033[[A";
                }

        case Q_KEY_F(2):
                /* PF2 */
                switch (q_linux_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\033Q";
                default:
                        return L"\033[[B";
                }

        case Q_KEY_F(3):
                /* PF3 */
                switch (q_linux_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\033R";
                default:
                        return L"\033[[C";
                }

        case Q_KEY_F(4):
                /* PF4 */
                switch (q_linux_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\033S";
                default:
                        return L"\033[[D";
                }

        case Q_KEY_F(5):
                return L"\033[[E";

        case Q_KEY_F(6):
                return L"\033[17~";

        case Q_KEY_F(7):
                return L"\033[18~";

        case Q_KEY_F(8):
                return L"\033[19~";

        case Q_KEY_F(9):
                return L"\033[20~";

        case Q_KEY_F(10):
                return L"\033[21~";

        case Q_KEY_F(11):
                return L"\033[23~";

        case Q_KEY_F(12):
                return L"\033[24~";

        case Q_KEY_F(13):
                /* Shifted PF1 */
                switch (q_linux_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\0332P";
                default:
                        return L"\033[25~";
                }

        case Q_KEY_F(14):
                /* Shifted PF2 */
                switch (q_linux_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\0332Q";
                default:
                        return L"\03326~";
                }

        case Q_KEY_F(15):
                /* Shifted PF3 */
                switch (q_linux_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\0332R";
                default:
                        return L"\03328~";
                }

        case Q_KEY_F(16):
                /* Shifted PF4 */
                switch (q_linux_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\0332S";
                default:
                        return L"\03329~";
                }

        case Q_KEY_F(17):
                /* Shifted F5 */
                return L"\033[31~";

        case Q_KEY_F(18):
                /* Shifted F6 */
                return L"\033[32~";

        case Q_KEY_F(19):
                /* Shifted F7 */
                return L"\033[33~";

        case Q_KEY_F(20):
                /* Shifted F8 */
                return L"\033[34~";

        case Q_KEY_F(21):
                /* Shifted F9 */
                return L"\033[35~";

        case Q_KEY_F(22):
                /* Shifted F10 */
                return L"\033[36~";

        case Q_KEY_F(23):
                /* Shifted F11 */
                return L"";

        case Q_KEY_F(24):
                /* Shifted F12 */
                return L"";

        case Q_KEY_F(25):
                /* Control PF1 */
                switch (q_linux_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\0335P";
                default:
                        return L"";
                }

        case Q_KEY_F(26):
                /* Control PF2 */
                switch (q_linux_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\0335Q";
                default:
                        return L"";
                }

        case Q_KEY_F(27):
                /* Control PF3 */
                switch (q_linux_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\0335R";
                default:
                        return L"";
                }

        case Q_KEY_F(28):
                /* Control PF4 */
                switch (q_linux_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\0335S";
                default:
                        return L"";
                }

        case Q_KEY_F(29):
                /* Control F5 */
                return L"";

        case Q_KEY_F(30):
                /* Control F6 */
                return L"";

        case Q_KEY_F(31):
                /* Control F7 */
                return L"";

        case Q_KEY_F(32):
                /* Control F8 */
                return L"";

        case Q_KEY_F(33):
                /* Control F9 */
                return L"";

        case Q_KEY_F(34):
                /* Control F10 */
                return L"";

        case Q_KEY_F(35):
                /* Control F11 */
                return L"";

        case Q_KEY_F(36):
                /* Control F12 */
                return L"";

        case Q_KEY_PPAGE:
                return L"\033[5~";

        case Q_KEY_NPAGE:
                return L"\033[6~";

        case Q_KEY_IC:
        case Q_KEY_SIC:
                return L"\033[2~";

        case Q_KEY_DC:
        case Q_KEY_SDC:
                return L"\033[3~";

        case Q_KEY_PAD0:
                if (q_linux_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 0 */
                        switch (q_linux_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?p";
                        default:
                                return L"\033Op";
                        }
                }
                return L"0";

        case Q_KEY_C1:
        case Q_KEY_PAD1:
                if (q_linux_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 1 */
                        switch (q_linux_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?q";
                        default:
                                return L"\033Oq";
                        }
                }
                return L"1";

        case Q_KEY_C2:
        case Q_KEY_PAD2:
                if (q_linux_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 2 */
                        switch (q_linux_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?r";
                        default:
                                return L"\033Or";
                        }
                }
                return L"2";

        case Q_KEY_C3:
        case Q_KEY_PAD3:
                if (q_linux_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 3 */
                        switch (q_linux_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?s";
                        default:
                                return L"\033Os";
                        }
                }
                return L"3";

        case Q_KEY_B1:
        case Q_KEY_PAD4:
                if (q_linux_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 4 */
                        switch (q_linux_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?t";
                        default:
                                return L"\033Ot";
                        }
                }
                return L"4";

        case Q_KEY_B2:
        case Q_KEY_PAD5:
                if (q_linux_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 5 */
                        switch (q_linux_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?u";
                        default:
                                return L"\033Ou";
                        }
                }
                return L"5";

        case Q_KEY_B3:
        case Q_KEY_PAD6:
                if (q_linux_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 6 */
                        switch (q_linux_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?v";
                        default:
                                return L"\033Ov";
                        }
                }
                return L"6";

        case Q_KEY_A1:
        case Q_KEY_PAD7:
                if (q_linux_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 7 */
                        switch (q_linux_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?w";
                        default:
                                return L"\033Ow";
                        }

                }
                return L"7";

        case Q_KEY_A2:
        case Q_KEY_PAD8:
                if (q_linux_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 8 */
                        switch (q_linux_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?x";
                        default:
                                return L"\033Ox";
                        }
                }
                return L"8";

        case Q_KEY_A3:
        case Q_KEY_PAD9:
                if (q_linux_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 9 */
                        switch (q_linux_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?y";
                        default:
                                return L"\033Oy";
                        }
                }
                return L"9";

        case Q_KEY_PAD_STOP:
                if (q_linux_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad . */
                        switch (q_linux_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?n";
                        default:
                                return L"\033On";
                        }
                }
                return L".";

        case Q_KEY_PAD_SLASH:
                /* Number pad / */
                return L"/";

        case Q_KEY_PAD_STAR:
                /* Number pad * */
                return L"*";

        case Q_KEY_PAD_MINUS:
                if (q_linux_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad - */
                        switch (q_linux_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?m";
                        default:
                                return L"\033Om";
                        }
                }
                return L"-";

        case Q_KEY_PAD_PLUS:
                /* Number pad + */
                return L"+";

        case Q_KEY_PAD_ENTER:
        case Q_KEY_ENTER:
                /* Number pad Enter */
                if (telnet_is_ascii()) {
                        return L"\015\012";
                }
                return L"\015";


        default:
                break;
        }

        return NULL;
} /* ---------------------------------------------------------------------- */

/*
 * xterm_keystroke
 *
 * XTERM terminal is quite unlike VT10x for function keys.
 */
wchar_t * xterm_keystroke(const int keystroke) {

        switch (keystroke) {
        case Q_KEY_BACKSPACE:
                if (q_status.hard_backspace == Q_TRUE) {
                        return L"\010";
                } else {
                        return L"\177";
                }

        case Q_KEY_LEFT:
                switch (q_linux_arrow_keys) {
                case Q_EMUL_ANSI:
                        return L"\033[D";
                case Q_EMUL_VT52:
                        return L"\033D";
                default:
                        return L"\033OD";
                }

        case Q_KEY_RIGHT:
                switch (q_linux_arrow_keys) {
                case Q_EMUL_ANSI:
                        return L"\033[C";
                case Q_EMUL_VT52:
                        return L"\033C";
                default:
                        return L"\033OC";
                }

        case Q_KEY_UP:
                switch (q_linux_arrow_keys) {
                case Q_EMUL_ANSI:
                        return L"\033[A";
                case Q_EMUL_VT52:
                        return L"\033A";
                default:
                        return L"\033OA";
                }

        case Q_KEY_DOWN:
                switch (q_linux_arrow_keys) {
                case Q_EMUL_ANSI:
                        return L"\033[B";
                case Q_EMUL_VT52:
                        return L"\033B";
                default:
                        return L"\033OB";
                }

        case Q_KEY_SLEFT:
                /* Shifted left */
                return L"\033[1;2D";

        case Q_KEY_SRIGHT:
                /* Shifted right */
                return L"\033[1;2C";

        case Q_KEY_SR:
                /* Shifted up */
                return L"\033[1;2A";

        case Q_KEY_SF:
                /* Shifted down */
                return L"\033[1;2B";

        case Q_KEY_HOME:
                return L"\033[H";

        case Q_KEY_END:
                return L"\033[F";

        case Q_KEY_F(1):
                /* PF1 */
                switch (q_linux_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\033P";
                default:
                        return L"\033OP";
                }

        case Q_KEY_F(2):
                /* PF2 */
                switch (q_linux_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\033Q";
                default:
                        return L"\033OQ";
                }

        case Q_KEY_F(3):
                /* PF3 */
                switch (q_linux_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\033R";
                default:
                        return L"\033OR";
                }

        case Q_KEY_F(4):
                /* PF4 */
                switch (q_linux_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\033S";
                default:
                        return L"\033OS";
                }

        case Q_KEY_F(5):
                return L"\033[15~";

        case Q_KEY_F(6):
                return L"\033[17~";

        case Q_KEY_F(7):
                return L"\033[18~";

        case Q_KEY_F(8):
                return L"\033[19~";

        case Q_KEY_F(9):
                return L"\033[20~";

        case Q_KEY_F(10):
                return L"\033[21~";

        case Q_KEY_F(11):
                return L"\033[23~";

        case Q_KEY_F(12):
                return L"\033[24~";

        case Q_KEY_F(13):
                /* Shifted PF1 */
                switch (q_linux_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\0332P";
                default:
                        return L"\033[1;2P";
                }

        case Q_KEY_F(14):
                /* Shifted PF2 */
                switch (q_linux_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\0332Q";
                default:
                        return L"\033[1;2Q";
                }

        case Q_KEY_F(15):
                /* Shifted PF3 */
                switch (q_linux_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\0332R";
                default:
                        return L"\033[1;2R";
                }

        case Q_KEY_F(16):
                /* Shifted PF4 */
                switch (q_linux_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\0332S";
                default:
                        return L"\033[1;2S";
                }

        case Q_KEY_F(17):
                /* Shifted F5 */
                return L"\033[15;2~";

        case Q_KEY_F(18):
                /* Shifted F6 */
                return L"\033[17;2~";

        case Q_KEY_F(19):
                /* Shifted F7 */
                return L"\033[18;2~";

        case Q_KEY_F(20):
                /* Shifted F8 */
                return L"\033[19;2~";

        case Q_KEY_F(21):
                /* Shifted F9 */
                return L"\033[20;2~";

        case Q_KEY_F(22):
                /* Shifted F10 */
                return L"\033[21;2~";

        case Q_KEY_F(23):
                /* Shifted F11 */
                return L"\033[23;2~";

        case Q_KEY_F(24):
                /* Shifted F12 */
                return L"\033[24;2~";

        case Q_KEY_F(25):
                /* Control PF1 */
                switch (q_linux_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\0335P";
                default:
                        return L"\033[1;5P";
                }

        case Q_KEY_F(26):
                /* Control PF2 */
                switch (q_linux_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\0335Q";
                default:
                        return L"\033[1;5Q";
                }

        case Q_KEY_F(27):
                /* Control PF3 */
                switch (q_linux_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\0335R";
                default:
                        return L"\033[1;5R";
                }

        case Q_KEY_F(28):
                /* Control PF4 */
                switch (q_linux_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\0335S";
                default:
                        return L"\033[1;5S";
                }

        case Q_KEY_F(29):
                /* Control F5 */
                return L"\033[15;5~";

        case Q_KEY_F(30):
                /* Control F6 */
                return L"\033[17;5~";

        case Q_KEY_F(31):
                /* Control F7 */
                return L"\033[18;5~";

        case Q_KEY_F(32):
                /* Control F8 */
                return L"\033[19;5~";

        case Q_KEY_F(33):
                /* Control F9 */
                return L"\033[20;5~";

        case Q_KEY_F(34):
                /* Control F10 */
                return L"\033[21;5~";

        case Q_KEY_F(35):
                /* Control F11 */
                return L"\033[23;5~";

        case Q_KEY_F(36):
                /* Control F12 */
                return L"\033[24;5~";

        case Q_KEY_PPAGE:
                return L"\033[5~";

        case Q_KEY_NPAGE:
                return L"\033[6~";

        case Q_KEY_IC:
        case Q_KEY_SIC:
                return L"\033[2~";

        case Q_KEY_DC:
        case Q_KEY_SDC:
                return L"\033[3~";

        case Q_KEY_PAD0:
                if (q_linux_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 0 */
                        switch (q_linux_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?p";
                        default:
                                return L"\033Op";
                        }
                }
                return L"0";

        case Q_KEY_C1:
        case Q_KEY_PAD1:
                if (q_linux_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 1 */
                        switch (q_linux_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?q";
                        default:
                                return L"\033Oq";
                        }
                }
                return L"1";

        case Q_KEY_C2:
        case Q_KEY_PAD2:
                if (q_linux_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 2 */
                        switch (q_linux_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?r";
                        default:
                                return L"\033Or";
                        }
                }
                return L"2";

        case Q_KEY_C3:
        case Q_KEY_PAD3:
                if (q_linux_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 3 */
                        switch (q_linux_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?s";
                        default:
                                return L"\033Os";
                        }
                }
                return L"3";

        case Q_KEY_B1:
        case Q_KEY_PAD4:
                if (q_linux_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 4 */
                        switch (q_linux_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?t";
                        default:
                                return L"\033Ot";
                        }
                }
                return L"4";

        case Q_KEY_B2:
        case Q_KEY_PAD5:
                if (q_linux_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 5 */
                        switch (q_linux_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?u";
                        default:
                                return L"\033Ou";
                        }
                }
                return L"5";

        case Q_KEY_B3:
        case Q_KEY_PAD6:
                if (q_linux_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 6 */
                        switch (q_linux_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?v";
                        default:
                                return L"\033Ov";
                        }
                }
                return L"6";

        case Q_KEY_A1:
        case Q_KEY_PAD7:
                if (q_linux_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 7 */
                        switch (q_linux_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?w";
                        default:
                                return L"\033Ow";
                        }

                }
                return L"7";

        case Q_KEY_A2:
        case Q_KEY_PAD8:
                if (q_linux_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 8 */
                        switch (q_linux_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?x";
                        default:
                                return L"\033Ox";
                        }
                }
                return L"8";

        case Q_KEY_A3:
        case Q_KEY_PAD9:
                if (q_linux_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 9 */
                        switch (q_linux_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?y";
                        default:
                                return L"\033Oy";
                        }
                }
                return L"9";

        case Q_KEY_PAD_STOP:
                if (q_linux_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad . */
                        switch (q_linux_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?n";
                        default:
                                return L"\033On";
                        }
                }
                return L".";

        case Q_KEY_PAD_SLASH:
                /* Number pad / */
                return L"/";

        case Q_KEY_PAD_STAR:
                /* Number pad * */
                return L"*";

        case Q_KEY_PAD_MINUS:
                if (q_linux_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad - */
                        switch (q_linux_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?m";
                        default:
                                return L"\033Om";
                        }
                }
                return L"-";

        case Q_KEY_PAD_PLUS:
                /* Number pad + */
                return L"+";

        case Q_KEY_PAD_ENTER:
        case Q_KEY_ENTER:
                /* Number pad Enter */
                if (telnet_is_ascii()) {
                        return L"\015\012";
                }
                return L"\015";

        default:
                break;
        }

        return NULL;
} /* ---------------------------------------------------------------------- */
