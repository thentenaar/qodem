/*
 * vt100.c
 *
 * This parser tries to emulate as close as possible the state diagram
 * described by Paul Williams at http://vt100.net/emu/dec_ansi_parser.
 *
 * It supports most of VT102 and VT220.  It will act like VT102 even
 * if only VT100 was selected.
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
#include <stdlib.h>
#include <string.h>     /* memset() */
#include "qodem.h"
#include "screen.h"
#include "options.h"
#include "netclient.h"
#include "vt100.h"

/* #define DEBUG_VT100 1 */
#undef DEBUG_VT100
#ifdef DEBUG_VT100
#define VT100_DEBUG_FILE        "debug_vt100.txt"
static FILE * VT100_DEBUG_FILE_HANDLE = NULL;
#endif /* DEBUG_VT100 */

/* Whether arrow keys send ANSI or VT100 sequences.  The default is
   false, meaning use VT100 arrow keys. */
Q_EMULATION q_vt100_arrow_keys;

/* When true, VT100 new line mode is set */
/* If true, cursor_linefeed() puts the cursor on the first
   column of the next line.  If false, cursor_linefeed() puts
   the cursor one line down on the current line.  The default
   is false. */
Q_BOOL q_vt100_new_line_mode;

/* Whether number pad keys send VT100 or VT52, application or numeric sequences. */
struct q_keypad_mode q_vt100_keypad_mode = {
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

/* "I am a VT100 with advanced video option" (often VT102) */
#define VT100_DEVICE_TYPE_STRING        "\033[?1;2c"

/* "I am a VT102" */
#define VT102_DEVICE_TYPE_STRING        "\033[?6c"

/* "I am a VT220" */
#define VT220_DEVICE_TYPE_STRING        "\033[?62;1;6c"
/* "I am a VT220" in 8bit */
#define VT220_DEVICE_TYPE_STRING_S8C1T  "\233?62;1;6c"

/* Available character sets */
typedef enum {
        CHARSET_US,
        CHARSET_UK,
        CHARSET_DRAWING,
        CHARSET_ROM,
        CHARSET_ROM_SPECIAL,
        CHARSET_VT52_GRAPHICS,
        CHARSET_DEC_SUPPLEMENTAL,
        CHARSET_NRC_DUTCH,
        CHARSET_NRC_FINNISH,
        CHARSET_NRC_FRENCH,
        CHARSET_NRC_FRENCH_CA,
        CHARSET_NRC_GERMAN,
        CHARSET_NRC_ITALIAN,
        CHARSET_NRC_NORWEGIAN,
        CHARSET_NRC_SPANISH,
        CHARSET_NRC_SWEDISH,
        CHARSET_NRC_SWISS
} VT100_CHARACTER_SET;

#ifdef DEBUG_VT100
#if 0

static const char * charset_string(VT100_CHARACTER_SET charset) {
        switch (charset) {
        case CHARSET_US:
                return "US (ASCII)";
        case CHARSET_UK:
                return "UK (BRITISH)";
        case CHARSET_DRAWING:
                return "DRAWING (DEC Technical Graphics)";
        case CHARSET_ROM:
                return "DEC ROM";
        case CHARSET_ROM_SPECIAL:
                return "DEC ROM SPECIAL";
        case CHARSET_VT52_GRAPHICS:
                return "VT52 DRAWING (VT52 Graphics)";
        case CHARSET_DEC_SUPPLEMENTAL:
                return "DEC SUPPLEMENTAL (Multinational)";
        case CHARSET_NRC_DUTCH:
                return "DUTCH";
        case CHARSET_NRC_FINNISH:
                return "FINNISH";
        case CHARSET_NRC_FRENCH:
                return "FRENCH";
        case CHARSET_NRC_FRENCH_CA:
                return "FRENCH CANADIAN";
        case CHARSET_NRC_GERMAN:
                return "GERMAN";
        case CHARSET_NRC_ITALIAN:
                return "ITALIAN";
        case CHARSET_NRC_NORWEGIAN:
                return "NORWEGIAN";
        case CHARSET_NRC_SPANISH:
                return "SPANISH";
        case CHARSET_NRC_SWEDISH:
                return "SWEDISH";
        case CHARSET_NRC_SWISS:
                return "SWISS";
        }

        assert(1 == 0);
        return "";
};
#endif /* 0 */
#endif /* DEBUG_VT100 */

/* Single-shift states */
typedef enum {
        SS_NONE,
        SS2,
        SS3
} SINGLESHIFT;

/* VT220 lockshift states */
typedef enum {
        LOCKSHIFT_NONE,
        LOCKSHIFT_G1_GR,
        LOCKSHIFT_G2_GR,
        LOCKSHIFT_G2_GL,
        LOCKSHIFT_G3_GR,
        LOCKSHIFT_G3_GL,
} LOCKSHIFT_MODE;

/* Rather than have a bunch of globals, this one struct
   contains the state of the VT100. */
struct vt100_state {

        /* VT220 single shift flag */
        SINGLESHIFT singleshift;

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

        /* VT220 */

        /* S8C1T.  True means 8bit controls, false means 7bit controls. */
        Q_BOOL s8c1t_mode;

        /* Printer mode.  True means send all output to printer, which discards it. */
        Q_BOOL printer_controller_mode;

        VT100_CHARACTER_SET g2_charset;
        VT100_CHARACTER_SET g3_charset;
        VT100_CHARACTER_SET gr_charset;

        VT100_CHARACTER_SET saved_g2_charset;
        VT100_CHARACTER_SET saved_g3_charset;
        VT100_CHARACTER_SET saved_gr_charset;

        /* VT220 saves linewrap flag on decsc()/decrc() */
        Q_BOOL saved_linewrap;

        /* VT220 saves lockshift flag on decsc()/decrc() */
        LOCKSHIFT_MODE saved_lockshift_gl;
        LOCKSHIFT_MODE saved_lockshift_gr;

        /* When a lockshift command comes in */
        LOCKSHIFT_MODE lockshift_gl;
        LOCKSHIFT_MODE lockshift_gr;

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
   vt100_reset(). */
static struct vt100_state state = {
        SS_NONE,
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
        Q_FALSE,
        Q_FALSE,
        CHARSET_US,
        CHARSET_US,
        CHARSET_DEC_SUPPLEMENTAL,
        CHARSET_US,
        CHARSET_US,
        CHARSET_DEC_SUPPLEMENTAL,
        Q_FALSE,
        LOCKSHIFT_NONE,
        LOCKSHIFT_NONE,
        LOCKSHIFT_NONE,
        LOCKSHIFT_NONE
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
 * vt100_reset - reset the emulation state
 */
void vt100_reset() {
        scan_state = SCAN_GROUND;
        clear_params();
        clear_collect_buffer();

        /* Reset vt100_state */
        state.saved_cursor_x            = -1;
        state.saved_cursor_y            = -1;
        q_emulation_right_margin        = 79;
        q_vt100_new_line_mode           = Q_FALSE;
        q_vt100_arrow_keys              = Q_EMUL_ANSI;
        q_vt100_keypad_mode.keypad_mode = Q_KEYPAD_MODE_NUMERIC;

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

        /* VT220 */
        state.singleshift               = SS_NONE;
        state.s8c1t_mode                = Q_FALSE;
        state.printer_controller_mode   = Q_FALSE;
        state.g2_charset                = CHARSET_US;
        state.g3_charset                = CHARSET_US;
        state.gr_charset                = CHARSET_DEC_SUPPLEMENTAL;
        state.lockshift_gl              = LOCKSHIFT_NONE;
        state.lockshift_gr              = LOCKSHIFT_NONE;
        state.saved_lockshift_gl        = LOCKSHIFT_NONE;
        state.saved_lockshift_gr        = LOCKSHIFT_NONE;

        state.saved_g2_charset          = CHARSET_US;
        state.saved_g3_charset          = CHARSET_US;
        state.saved_gr_charset          = CHARSET_DEC_SUPPLEMENTAL;
        state.saved_linewrap            = q_status.line_wrap;

#ifdef DEBUG_VT100
        if (VT100_DEBUG_FILE_HANDLE == NULL) {
                VT100_DEBUG_FILE_HANDLE = fopen(VT100_DEBUG_FILE, "w");
        }
        fprintf(VT100_DEBUG_FILE_HANDLE, "vt100_reset()\n");
#endif /* DEBUG_VT100 */

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
 * Map a symbol in any one of the VT100 character sets to a Unicode symbol
 */
static wchar_t map_character_charset(const unsigned char vt100_char,
        const VT100_CHARACTER_SET gl_charset,
        const VT100_CHARACTER_SET gr_charset) {

        unsigned char lookup_char = vt100_char;
        VT100_CHARACTER_SET lookup_charset = gl_charset;

        if (vt100_char >= 0x80) {
                assert(q_status.emulation == Q_EMUL_VT220);
                lookup_charset = gr_charset;
                lookup_char = vt100_char & 0x7F;
        }

        switch (lookup_charset) {

        case CHARSET_DRAWING:
                return dec_special_graphics_chars[lookup_char];

        case CHARSET_UK:
                return dec_uk_chars[lookup_char];

        case CHARSET_US:
                return dec_us_chars[lookup_char];

        case CHARSET_NRC_DUTCH:
                return dec_nl_chars[lookup_char];

        case CHARSET_NRC_FINNISH:
                return dec_fi_chars[lookup_char];

        case CHARSET_NRC_FRENCH:
                return dec_fr_chars[lookup_char];

        case CHARSET_NRC_FRENCH_CA:
                return dec_fr_CA_chars[lookup_char];

        case CHARSET_NRC_GERMAN:
                return dec_de_chars[lookup_char];

        case CHARSET_NRC_ITALIAN:
                return dec_it_chars[lookup_char];

        case CHARSET_NRC_NORWEGIAN:
                return dec_no_chars[lookup_char];

        case CHARSET_NRC_SPANISH:
                return dec_es_chars[lookup_char];

        case CHARSET_NRC_SWEDISH:
                return dec_sv_chars[lookup_char];

        case CHARSET_NRC_SWISS:
                return dec_swiss_chars[lookup_char];

        case CHARSET_DEC_SUPPLEMENTAL:
                return dec_supplemental_chars[lookup_char];

        case CHARSET_VT52_GRAPHICS:
                return vt52_special_graphics_chars[lookup_char];

        case CHARSET_ROM:
        case CHARSET_ROM_SPECIAL:
        default:
                return dec_us_chars[lookup_char];
        }
} /* ---------------------------------------------------------------------- */

/*
 * Map a symbol in any one of the VT100 character sets to a Unicode symbol
 */
static wchar_t map_character(const unsigned char vt100_char) {
        VT100_CHARACTER_SET gl_charset = state.g0_charset;
        VT100_CHARACTER_SET gr_charset = state.gr_charset;

#ifdef DEBUG_VT100
#if 0
        fprintf(VT100_DEBUG_FILE_HANDLE, "map_character: '%c' shift_out = %s singleshift = %d lockshift_gl = %d lockshift_gr = %d\n", vt100_char,
                (state.shift_out == Q_TRUE ? "true" : "false"),
                state.singleshift, state.lockshift_gl, state.lockshift_gr);
        fprintf(VT100_DEBUG_FILE_HANDLE, "    g0 %s g1 %s g2 %s g3 %s\n",
                charset_string(state.g0_charset),
                charset_string(state.g1_charset),
                charset_string(state.g2_charset),
                charset_string(state.g3_charset));
#endif
#endif /* DEBUG_VT100 */

        if (state.vt52_mode == Q_TRUE) {
                if (state.shift_out == Q_TRUE) {
                        /* Shifted out character, pull from VT52 graphics */
                        gl_charset = state.g1_charset;
                        gr_charset = CHARSET_US;
                } else {
                        /* Normal */
                        gl_charset = state.g0_charset;
                        gr_charset = CHARSET_US;
                }

                /* Pull the character */
                return map_character_charset(vt100_char, gl_charset, gr_charset);
        }

        /* shift_out */
        if (state.shift_out == Q_TRUE) {
                /* Shifted out character, pull from G1 */
                gl_charset = state.g1_charset;
                gr_charset = state.gr_charset;

                /* Pull the character */
                return map_character_charset(vt100_char, gl_charset, gr_charset);
        }

        /* SS2 */
        if (state.singleshift == SS2) {

                state.singleshift = SS_NONE;

                /* Shifted out character, pull from G2 */
                gl_charset = state.g2_charset;
                gr_charset = state.gr_charset;
        }

        /* SS3 */
        if (state.singleshift == SS3) {

                state.singleshift = SS_NONE;

                /* Shifted out character, pull from G3 */
                gl_charset = state.g3_charset;
                gr_charset = state.gr_charset;
        }

        if (q_status.emulation == Q_EMUL_VT220) {
                /* Check for locking shift */

                switch (state.lockshift_gl) {

                case LOCKSHIFT_G1_GR:
                case LOCKSHIFT_G2_GR:
                case LOCKSHIFT_G3_GR:
                        assert(1 == 0);
                        break;

                case LOCKSHIFT_G2_GL:
                        /* LS2 */
                        gl_charset = state.g2_charset;
                        break;

                case LOCKSHIFT_G3_GL:
                        /* LS3 */
                        gl_charset = state.g3_charset;
                        break;

                case LOCKSHIFT_NONE:
                        /* Normal */
                        gl_charset = state.g0_charset;
                        break;
                }

                switch (state.lockshift_gr) {

                case LOCKSHIFT_G2_GL:
                case LOCKSHIFT_G3_GL:
                        assert(1 == 0);
                        break;

                case LOCKSHIFT_G1_GR:
                        /* LS1R */
                        gr_charset = state.g1_charset;
                        break;

                case LOCKSHIFT_G2_GR:
                        /* LS2R */
                        gr_charset = state.g2_charset;
                        break;

                case LOCKSHIFT_G3_GR:
                        /* LS3R */
                        gr_charset = state.g3_charset;
                        break;

                case LOCKSHIFT_NONE:
                        /* Normal */
                        gr_charset = CHARSET_DEC_SUPPLEMENTAL;
                        break;
                }


        }

        /* Pull the character */
        return map_character_charset(vt100_char, gl_charset, gr_charset);
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
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "DECCRM: set (VT100 keys)\n");
#endif /* DEBUG_VT100 */
                                        /* Use application arrow keys */
                                        q_vt100_arrow_keys = Q_EMUL_VT100;
                                } else {
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "DECCRM: reset (ANSI keys)\n");
#endif /* DEBUG_VT100 */
                                        /* Use ANSI arrow keys */
                                        q_vt100_arrow_keys = Q_EMUL_ANSI;
                                }
                        }
                        break;
                case 2:
                        if (state.dec_private_mode_flag == Q_TRUE) {
                                if (value == Q_FALSE) {
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "DECANM: reset (Enter VT52 mode)\n");
#endif /* DEBUG_VT100 */
                                        /* DECANM */
                                        state.vt52_mode = Q_TRUE;
                                        q_vt100_arrow_keys = Q_EMUL_VT52;
                                        q_vt100_keypad_mode.emulation = Q_EMUL_VT52;

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

                                        if (q_status.emulation == Q_EMUL_VT220) {
                                                /* VT52 mode is explicitly 7-bit */
                                                state.s8c1t_mode = Q_FALSE;
                                                state.singleshift = SS_NONE;
                                        }
                                }
                        } else {
                                /* KAM */
                                if (value == Q_TRUE) {
                                        /* Turn off keyboard */
                                        /* Not supported */
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "KAM: set (turn off keyboard) NOP\n");
#endif /* DEBUG_VT100 */
                                } else {
                                        /* Turn on keyboard */
                                        /* Not supported */
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "KAM: reset (turn on keyboard) NOP\n");
#endif /* DEBUG_VT100 */
                                }
                        }
                        break;
                case 3:
                        if (state.dec_private_mode_flag == Q_TRUE) {
                                /* DECCOLM */
                                if (value == Q_TRUE) {
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "DECCOLM: set\n");
#endif /* DEBUG_VT100 */
                                        /* 132 columns */
                                        state.columns_132 = Q_TRUE;
                                        q_emulation_right_margin = 131;
                                } else {
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "DECCOLM: reset\n");
#endif /* DEBUG_VT100 */
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
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "DECSCLM: set (smooth scroll) NOP\n");
#endif /* DEBUG_VT100 */
                                } else {
                                        /* Jump scroll */
                                        /* Not supported */
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "DECSCLM: reset (jump scroll) NOP\n");
#endif /* DEBUG_VT100 */
                                }
                        } else {
                                /* IRM */
                                if (value == Q_TRUE) {
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "IRM: set\n");
#endif /* DEBUG_VT100 */
                                        q_status.insert_mode = Q_TRUE;
                                } else {
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "IRM: reset\n");
#endif /* DEBUG_VT100 */
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
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "DECSCNM: set\n");
#endif /* DEBUG_VT100 */
                                        if (q_status.reverse_video != Q_TRUE) {
                                                /*
                                                 * If in normal video,
                                                 * switch it back
                                                 */
                                                invert_scrollback_colors();
                                        }
                                        q_status.reverse_video = Q_TRUE;
                                } else {
                                        /* Reset selects normal screen, a
                                           black screen background with white
                                           characters.
                                        */
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "DECSCNM: reset\n");
#endif /* DEBUG_VT100 */
                                        if (q_status.reverse_video == Q_TRUE) {
                                                /*
                                                 * If in reverse video
                                                 * already, switch it
                                                 * back
                                                 */
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
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "DECOM: set\n");
#endif /* DEBUG_VT100 */
                                        /* Home cursor.  Cursor can NEVER leave scrolling
                                           region. */
                                        q_status.origin_mode = Q_TRUE;
                                        cursor_position(0, 0);
                                } else {
                                        /* Origin is absolute to entire screen */
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "DECOM: reset\n");
#endif /* DEBUG_VT100 */
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
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "DECAWM: set\n");
#endif /* DEBUG_VT100 */
                                        /* Turn linewrap on */
                                        if (q_status.line_wrap == Q_FALSE) {
                                                state.overridden_line_wrap = Q_TRUE;
                                        }
                                        q_status.line_wrap = Q_TRUE;
                                } else {
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "DECAWM: reset\n");
#endif /* DEBUG_VT100 */
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
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "DECARM: set (Keyboard auto-repeat on) NOP\n");
#endif /* DEBUG_VT100 */
                                } else {
                                        /* Keyboard auto-repeat off */
                                        /* Not supported */
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "DECARM: reset (Keyboard auto-repeat off) NOP\n");
#endif /* DEBUG_VT100 */
                                }
                        }
                        break;
                case 12:
                        if (state.dec_private_mode_flag == Q_FALSE) {
                                /* SRM */
                                if (value == Q_TRUE) {
                                        /* Local echo off */
                                        q_status.full_duplex = Q_TRUE;
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "SRM: set (Local echo off)\n");
#endif /* DEBUG_VT100 */
                                } else {
                                        /* Local echo on */
                                        q_status.full_duplex = Q_FALSE;
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "SRM: reset (Local echo on)\n");
#endif /* DEBUG_VT100 */
                                }
                        }
                        break;
                case 18:
                        if (state.dec_private_mode_flag == Q_TRUE) {
                                /* DECPFF */
                                /* Not supported */
#ifdef DEBUG_VT100
                                fprintf(VT100_DEBUG_FILE_HANDLE, "DECPFF: NOP\n");
#endif /* DEBUG_VT100 */
                        }
                        break;
                case 19:
                        if (state.dec_private_mode_flag == Q_TRUE) {
                                /* DECPEX */
                                /* Not supported */
#ifdef DEBUG_VT100
                                fprintf(VT100_DEBUG_FILE_HANDLE, "DECPEX: NOP\n");
#endif /* DEBUG_VT100 */
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
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "LNM: set (CRLF)\n");
#endif /* DEBUG_VT100 */
                                        q_vt100_new_line_mode = Q_TRUE;
                                } else {
                                        /* Reset causes a received linefeed,
                                           form feed, or vertical tab to move
                                           cursor to next line in current
                                           column. RETURN transmits a carriage
                                           return.
                                        */
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "LNM: reset (CR)\n");
#endif /* DEBUG_VT100 */
                                        q_vt100_new_line_mode = Q_FALSE;
                                }
                        }
                        break;

                case 25:
                        if (q_status.emulation == Q_EMUL_VT220) {
                                if (state.dec_private_mode_flag == Q_TRUE) {
                                        /* DECTCEM */
                                        if (value == Q_TRUE) {
                                                /* Visible cursor */
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "DECTCEM: set (visible cursor)\n");
#endif /* DEBUG_VT100 */
                                                q_status.visible_cursor = Q_TRUE;
                                                q_cursor_on();
                                        } else {
                                                /* Invisible cursor */
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "DECTCEM: reset (invisible cursor)\n");
#endif /* DEBUG_VT100 */
                                                q_status.visible_cursor = Q_FALSE;
                                                q_cursor_off();
                                        }
                                }
                        }
                        break;

                case 42:
                        if (q_status.emulation == Q_EMUL_VT220) {
                                if (state.dec_private_mode_flag == Q_TRUE) {
                                        /* DECNRCM */
                                        if (value == Q_TRUE) {
                                                /* Select national mode NRC */
                                                /* Not supported */
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "DECNRCM: set (national character set) NOP\n");
#endif /* DEBUG_VT100 */
                                        } else {
                                                /* Select multi-national mode */
                                                /* Not supported */
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "DECNRCM: reset (multi-national character set) NOP\n");
#endif /* DEBUG_VT100 */
                                        }
                                }
                        }

                        break;

                default:
                        break;

                } /* switch (x) */

        } /* for (i=0; i<=state.params_n; i++) */

} /* ---------------------------------------------------------------------- */

/*
 * printer_functions - VT220 printer functions.  All of these are parsed,
 * but won't do anything.
 */
static void printer_functions() {
        int i;
        for (i=0; i<q_emul_buffer_n; i++) {
                if (q_emul_buffer[i] == '?') {
                        state.dec_private_mode_flag = Q_TRUE;
                }
        }

        if (state.params_n >= 0) {
                i = atoi((char *)state.params[0]);
        } else {
                i = 0;
        }

        switch (i) {

        case 0:
                if (state.dec_private_mode_flag == Q_FALSE) {
                        /* Print screen */
#ifdef DEBUG_VT100
                        fprintf(VT100_DEBUG_FILE_HANDLE, "VT220:  Print screen\n");
#endif /* DEBUG_VT100 */
                }
                break;

        case 1:
                if (state.dec_private_mode_flag == Q_TRUE) {
                        /* Print cursor line */
#ifdef DEBUG_VT100
                        fprintf(VT100_DEBUG_FILE_HANDLE, "VT220:  Print cursor line\n");
#endif /* DEBUG_VT100 */
                }
                break;

        case 4:
                if (state.dec_private_mode_flag == Q_TRUE) {
                        /* Auto print mode OFF */
#ifdef DEBUG_VT100
                        fprintf(VT100_DEBUG_FILE_HANDLE, "VT220:  Auto-print mode OFF\n");
#endif /* DEBUG_VT100 */

                } else {
                        /* Printer controller OFF */
#ifdef DEBUG_VT100
                        fprintf(VT100_DEBUG_FILE_HANDLE, "VT220:  Printer controller OFF\n");
#endif /* DEBUG_VT100 */

                        /* Characters re-appear on the screen */
                        state.printer_controller_mode = Q_FALSE;

                }
                break;

        case 5:
                if (state.dec_private_mode_flag == Q_TRUE) {
                        /* Auto print mode */
#ifdef DEBUG_VT100
                        fprintf(VT100_DEBUG_FILE_HANDLE, "VT220:  Auto-print mode ON\n");
#endif /* DEBUG_VT100 */

                } else {
                        /* Printer controller */
#ifdef DEBUG_VT100
                        fprintf(VT100_DEBUG_FILE_HANDLE, "VT220:  Printer controller ON\n");
#endif /* DEBUG_VT100 */

                        /* Characters get sucked into oblivion */
                        state.printer_controller_mode = Q_TRUE;
                }
                break;

        default:
                break;

        } /* switch (i) */

} /* ---------------------------------------------------------------------- */

/*
 * DECRC - Restore cursor
 */
static void decrc() {
#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "decrc(): state.saved_cursor_y=%d state.saved_cursor_x=%d\n", state.saved_cursor_y, state.saved_cursor_x);
#endif /* DEBUG_VT100 */
        if (state.saved_cursor_x != -1) {
                cursor_position(state.saved_cursor_y, state.saved_cursor_x);
                q_current_color         = state.saved_attributes;
                q_status.origin_mode    = state.saved_origin_mode;
                state.g0_charset        = state.saved_g0_charset;
                state.g1_charset        = state.saved_g1_charset;

                if (q_status.emulation == Q_EMUL_VT220) {
                        state.g2_charset        = state.saved_g2_charset;
                        state.g3_charset        = state.saved_g3_charset;
                        state.lockshift_gl      = state.saved_lockshift_gl;
                        state.lockshift_gr      = state.saved_lockshift_gr;
                        q_status.line_wrap      = state.saved_linewrap;
                        state.gr_charset        = state.saved_gr_charset;
                }

        } else {
                cursor_position(0, 0);
                q_current_color         = Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);
                q_status.origin_mode    = Q_FALSE;
                state.g0_charset        = CHARSET_US;
                state.g1_charset        = CHARSET_DRAWING;
                state.g2_charset        = CHARSET_US;
                state.g3_charset        = CHARSET_US;
                state.gr_charset        = CHARSET_DEC_SUPPLEMENTAL;
                state.lockshift_gl      = LOCKSHIFT_NONE;
                state.lockshift_gr      = LOCKSHIFT_NONE;
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
        state.saved_g2_charset          = state.g2_charset;
        state.saved_g3_charset          = state.g3_charset;
        state.saved_gr_charset          = state.gr_charset;
        state.saved_lockshift_gl        = state.lockshift_gl;
        state.saved_lockshift_gr        = state.lockshift_gr;
        state.saved_linewrap            = q_status.line_wrap;
#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "decsc(): state.saved_cursor_y=%d state.saved_cursor_x=%d\n", state.saved_cursor_y, state.saved_cursor_x);
#endif /* DEBUG_VT100 */

} /* ---------------------------------------------------------------------- */

/*
 * DECSWL - Single-width line
 */
static void decswl() {
#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "decswl()\n");
#endif /* DEBUG_VT100 */
        set_double_width(Q_FALSE);
} /* ---------------------------------------------------------------------- */

/*
 * DECDWL - Double-width line
 */
static void decdwl() {
#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "decdwl()\n");
#endif /* DEBUG_VT100 */
        set_double_width(Q_TRUE);
} /* ---------------------------------------------------------------------- */

/*
 * DECHDL - Double-height + double-width line
 */
static void dechdl(Q_BOOL top_half) {
#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "dechdl(%s)\n",
                (top_half == Q_TRUE ? "true" : "false"));
#endif /* DEBUG_VT100 */
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
#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "deckpam()\n");
#endif /* DEBUG_VT100 */
        q_vt100_keypad_mode.keypad_mode = Q_KEYPAD_MODE_APPLICATION;
} /* ---------------------------------------------------------------------- */

/*
 * DECKPNM - Keypad numeric mode
 */
static void deckpnm() {
#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "deckpnm()\n");
#endif /* DEBUG_VT100 */
        q_vt100_keypad_mode.keypad_mode = Q_KEYPAD_MODE_NUMERIC;
} /* ---------------------------------------------------------------------- */

/*
 * IND - Index
 */
static void ind() {
#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "ind()\n");
#endif /* DEBUG_VT100 */

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
#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "ri()\n");
#endif /* DEBUG_VT100 */

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
#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "nel()\n");
#endif /* DEBUG_VT100 */

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
#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "hts()\n");
#endif /* DEBUG_VT100 */
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
#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "decaln()\n");
#endif /* DEBUG_VT100 */
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
#ifdef DEBUG_VT100
                fprintf(VT100_DEBUG_FILE_HANDLE, "cud(): 1\n");
#endif /* DEBUG_VT100 */
                cursor_down(1, Q_TRUE);
        } else {
                i = atoi((char *)state.params[0]);
#ifdef DEBUG_VT100
                fprintf(VT100_DEBUG_FILE_HANDLE, "cud(): %d\n", i);
#endif /* DEBUG_VT100 */
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
#ifdef DEBUG_VT100
                fprintf(VT100_DEBUG_FILE_HANDLE, "cuf(): 1\n");
#endif /* DEBUG_VT100 */
                cursor_right(1, Q_TRUE);
        } else {
                i = atoi((char *)state.params[0]);
#ifdef DEBUG_VT100
                fprintf(VT100_DEBUG_FILE_HANDLE, "cuf(): %d\n", i);
#endif /* DEBUG_VT100 */
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
#ifdef DEBUG_VT100
                fprintf(VT100_DEBUG_FILE_HANDLE, "cub(): 1\n");
#endif /* DEBUG_VT100 */
                cursor_left(1, Q_TRUE);
        } else {
                i = atoi((char *)state.params[0]);
#ifdef DEBUG_VT100
                fprintf(VT100_DEBUG_FILE_HANDLE, "cub(): %d\n", i);
#endif /* DEBUG_VT100 */
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
#ifdef DEBUG_VT100
                fprintf(VT100_DEBUG_FILE_HANDLE, "cuu(): 1\n");
#endif /* DEBUG_VT100 */
                cursor_up(1, Q_TRUE);
        } else {
                i = atoi((char *)state.params[0]);
#ifdef DEBUG_VT100
                fprintf(VT100_DEBUG_FILE_HANDLE, "cuu(): %d\n", i);
#endif /* DEBUG_VT100 */
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
#ifdef DEBUG_VT100
                fprintf(VT100_DEBUG_FILE_HANDLE, "cup(): 0 0\n");
#endif /* DEBUG_VT100 */
                cursor_position(0, 0);
        } else if (state.params_n == 0) {
#ifdef DEBUG_VT100
                fprintf(VT100_DEBUG_FILE_HANDLE, "cup(): %d %d\n", atoi((char *)state.params[0]) - 1, 0);
#endif /* DEBUG_VT100 */
                row = atoi((char *)state.params[0]) - 1;
                if (row < 0) {
                        row = 0;
                }
                cursor_position(row, 0);
        } else {
#ifdef DEBUG_VT100
                fprintf(VT100_DEBUG_FILE_HANDLE, "cup(): %d %d\n", atoi((char *)state.params[0]) - 1, atoi((char *)state.params[1]) - 1);
#endif /* DEBUG_VT100 */
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

#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "decstbm() param0 %s param1 %s\n", state.params[0], state.params[1]);
#endif /* DEBUG_VT100 */

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

#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "decstbm() %d %d\n", q_status.scroll_region_top, q_status.scroll_region_bottom);
#endif /* DEBUG_VT100 */

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

#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "decreqtparm(): %d\n", i);
#endif /* DEBUG_VT100 */

        if ((i != 0) && (i != 1)) {
                return;
        }

        /* Request terminal parameters. */
        /* Respond with:

              Parity NONE, 8 bits, xmitspeed 38400, recvspeed 38400.
              (CLoCk MULtiplier = 1, STP option flags = 0)

        (Same as xterm)

        */

        if ((q_status.emulation == Q_EMUL_VT220) && (state.s8c1t_mode == Q_TRUE)) {
                memset(response_buffer, 0, sizeof(response_buffer));
                snprintf(response_buffer, sizeof(response_buffer), "\233%u;1;1;128;128;1;0x", i + 2);
        } else {
                memset(response_buffer, 0, sizeof(response_buffer));
                snprintf(response_buffer, sizeof(response_buffer), "\033[%u;1;1;128;128;1;0x", i + 2);
        }

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

#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "decsca(): %d\n", i);
#endif /* DEBUG_VT100 */

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
 * DECSCL - Compatibility level
 */
static void decscl() {
        int i, j;

        i = 0;
        j = 0;

        if (state.params_n > 0) {
                i = atoi((char *)state.params[0]);
        }
        if (state.params_n > 1) {
                j = atoi((char *)state.params[1]);
        }

        if (i == 61) {
#ifdef DEBUG_VT100
                fprintf(VT100_DEBUG_FILE_HANDLE, "decscl(): VT100\n");
#endif /* DEBUG_VT100 */
                /* Reset fonts */
                state.g0_charset = CHARSET_US;
                state.g1_charset = CHARSET_DRAWING;
                state.s8c1t_mode = Q_FALSE;
        } else if (i == 62) {

                if ((j == 0) || (j == 2)) {
#ifdef DEBUG_VT100
                        fprintf(VT100_DEBUG_FILE_HANDLE, "decscl(): VT200 8-bit\n");
#endif /* DEBUG_VT100 */
                        state.s8c1t_mode = Q_TRUE;
                } else if (j == 1) {
#ifdef DEBUG_VT100
                        fprintf(VT100_DEBUG_FILE_HANDLE, "decscl(): VT200 7-bit\n");
#endif /* DEBUG_VT100 */
                        state.s8c1t_mode = Q_FALSE;
                }
        }

} /* ---------------------------------------------------------------------- */

/*
 * DECLL - Load keyboard leds
 */
static void decll() {
        int i, j;

#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "decll(): ");
#endif /* DEBUG_VT100 */

        if (state.params_n < 0) {
                q_status.led_1 = Q_FALSE;
                q_status.led_2 = Q_FALSE;
                q_status.led_3 = Q_FALSE;
                q_status.led_4 = Q_FALSE;
        } else {
                for (i = 0; i <= state.params_n; i++) {
                        j = atoi((char *)state.params[i]);
#ifdef DEBUG_VT100
                        fprintf(VT100_DEBUG_FILE_HANDLE, "%d ", j);
#endif /* DEBUG_VT100 */
                        switch (j) {
                        case 0:
                                q_status.led_1 = Q_FALSE;
                                q_status.led_2 = Q_FALSE;
                                q_status.led_3 = Q_FALSE;
                                q_status.led_4 = Q_FALSE;
                                break;
                        case 1:
                                q_status.led_1 = Q_TRUE;
                                break;
                        case 2:
                                q_status.led_2 = Q_TRUE;
                                break;
                        case 3:
                                q_status.led_3 = Q_TRUE;
                                break;
                        case 4:
                                q_status.led_4 = Q_TRUE;
                                break;
                        }
                }
#ifdef DEBUG_VT100
                fprintf(VT100_DEBUG_FILE_HANDLE, "\n");
#endif /* DEBUG_VT100 */
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

        if ((q_status.emulation == Q_EMUL_VT220) && (state.dec_private_mode_flag == Q_TRUE)) {
                honor_protected = Q_TRUE;
        }

        if (state.params_n >= 0) {
                i = atoi((char *)state.params[0]);
        } else {
                i = 0;
        }

        if (i == 0) {
#ifdef DEBUG_VT100
                fprintf(VT100_DEBUG_FILE_HANDLE, "ed(): %d %d %d %d %s\n", q_status.cursor_y, q_status.cursor_x, HEIGHT - STATUS_HEIGHT - 1, WIDTH - 1,
                        (honor_protected == Q_TRUE ? "true" : "false"));
#endif /* DEBUG_VT100 */
                /* Erase from here to end of screen */
                if (q_status.cursor_y < HEIGHT - STATUS_HEIGHT - 1) {
                        erase_screen(q_status.cursor_y + 1, 0, HEIGHT - STATUS_HEIGHT - 1, WIDTH - 1, honor_protected);
                }
                erase_line(q_status.cursor_x, WIDTH-1, honor_protected);
        } else if (i == 1) {
#ifdef DEBUG_VT100
                fprintf(VT100_DEBUG_FILE_HANDLE, "ed(): 0 0 %d %d %s\n", q_status.cursor_y, q_status.cursor_x,
                        (honor_protected == Q_TRUE ? "true" : "false"));
#endif /* DEBUG_VT100 */
                /* Erase from beginning of screen to here */
                erase_screen(0, 0, q_status.cursor_y - 1, WIDTH - 1, honor_protected);
                erase_line(0, q_status.cursor_x, honor_protected);
        } else if (i == 2) {
#ifdef DEBUG_VT100
                fprintf(VT100_DEBUG_FILE_HANDLE, "ed(): 0 0 %d %d %s\n", HEIGHT - STATUS_HEIGHT - 1, WIDTH - 1,
                        (honor_protected == Q_TRUE ? "true" : "false"));
#endif /* DEBUG_VT100 */
                /* Erase entire screen */
                erase_screen(0, 0, HEIGHT - STATUS_HEIGHT - 1, WIDTH - 1, honor_protected);
        }

} /* ---------------------------------------------------------------------- */

/*
 * ECH - Erase character
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

#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "ech(): %d\n", i);
#endif /* DEBUG_VT100 */

        /* Erase from here to i characters */
        erase_line(q_status.cursor_x, q_status.cursor_x + i - 1, Q_FALSE);

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

        if ((q_status.emulation == Q_EMUL_VT220) && (state.dec_private_mode_flag == Q_TRUE)) {
                honor_protected = Q_TRUE;
        }

        if (state.params_n >= 0) {
                i = atoi((char *)state.params[0]);
        } else {
                i = 0;
        }

        if (i == 0) {
#ifdef DEBUG_VT100
                fprintf(VT100_DEBUG_FILE_HANDLE, "el(): %d %d\n", q_status.cursor_x, WIDTH-1);
#endif /* DEBUG_VT100 */
                /* Erase from here to end of line */
                erase_line(q_status.cursor_x, WIDTH-1, honor_protected);
        } else if (i == 1) {
#ifdef DEBUG_VT100
                fprintf(VT100_DEBUG_FILE_HANDLE, "el(): 0 %d\n", q_status.cursor_x);
#endif /* DEBUG_VT100 */
                /* Erase from beginning of line to here */
                erase_line(0, q_status.cursor_x, honor_protected);
        } else if (i == 2) {
#ifdef DEBUG_VT100
                fprintf(VT100_DEBUG_FILE_HANDLE, "el(): 0 %d\n", WIDTH-1);
#endif /* DEBUG_VT100 */
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

#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "il(): %d\n", i);
#endif /* DEBUG_VT100 */

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

#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "dch(): %d\n", i);
#endif /* DEBUG_VT100 */
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

#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "ich(): %d\n", i);
#endif /* DEBUG_VT100 */
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

#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "dl(): %d\n", i);
#endif /* DEBUG_VT100 */

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

#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "sgr(): ");
#endif /* DEBUG_VT100 */

        if (state.params_n < 0) {
                q_current_color = Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);
                foreground = q_text_colors[Q_COLOR_CONSOLE_TEXT].fg;
                background = q_text_colors[Q_COLOR_CONSOLE_TEXT].bg;
#ifdef DEBUG_VT100
                        fprintf(VT100_DEBUG_FILE_HANDLE, "RESET\n");
#endif /* DEBUG_VT100 */
        } else {
                for (i = 0; i <= state.params_n; i++) {
                        j = atoi((char *)state.params[i]);
#ifdef DEBUG_VT100
                        fprintf(VT100_DEBUG_FILE_HANDLE, "%d ", j);
#endif /* DEBUG_VT100 */
                        switch (j) {
                        case 0:
                                /* Normal */
                                q_current_color = Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);
                                break;

                        case 1:
                                /* Bold */
                                q_current_color |= Q_A_BOLD;
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
                        }

                        if (q_status.emulation == Q_EMUL_VT220) {
                                switch (j) {
                                case 22:
                                        /* Normal intensity */
                                        q_current_color &= ~Q_A_BOLD;
                                        break;

                                case 24:
                                        /* No underline */
                                        q_current_color &= ~Q_A_UNDERLINE;
                                        break;

                                case 25:
                                        /* No blink */
                                        q_current_color &= ~Q_A_BLINK;
                                        break;

                                case 27:
                                        /* Un-reverse */
                                        q_current_color &= ~Q_A_REVERSE;
                                        break;
                                }
                        }

                        /* Optional color support */
                        if (q_status.vt100_color == Q_TRUE) {

                                /* Pull the current foreground and background */
                                curses_color = color_from_attr(q_current_color);
                                foreground = (curses_color & 0x38) >> 3;
                                background = curses_color & 0x07;

                                switch(j) {

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
                                        break;
                                case 39:
                                        foreground = q_text_colors[Q_COLOR_CONSOLE_TEXT].fg;
                                        if (q_text_colors[Q_COLOR_CONSOLE_TEXT].bold == Q_TRUE) {
                                                q_current_color |= Q_A_BOLD;
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
                                } /* switch (j) */

                                /* Wipe out the existing colors and replace */
                                curses_color = (foreground << 3) | background;
                                q_current_color = q_current_color & NO_COLOR_MASK;
                                q_current_color |= color_to_attr(curses_color);
                        } /* if (q_status.vt100_color == Q_TRUE) */

                } /* for (i = 0; i <= state.params_n; i++) */
#ifdef DEBUG_VT100
                fprintf(VT100_DEBUG_FILE_HANDLE, "\n");
#endif /* DEBUG_VT100 */
        } /* if (state.params_n == 0) */

} /* ---------------------------------------------------------------------- */

/*
 * DSR - Device status report
 */
static void dsr() {
        int i;
        char response_buffer[VT100_RESPONSE_LENGTH];

        for (i=0; i<q_emul_buffer_n; i++) {
                if (q_emul_buffer[i] == '?') {
                        state.dec_private_mode_flag = Q_TRUE;
                }
        }

        if (state.params_n >= 0) {
                i = atoi((char *)state.params[0]);
        } else {
                i = 0;
        }

#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "dsr(): %d private_mode_flag = %s\n", i,
                (state.dec_private_mode_flag == Q_TRUE ? "true" : "false"));
#endif /* DEBUG_VT100 */

        switch (i) {

        case 5:
                /* Request status report.
                   Respond with "OK, no malfunction." */

                /* Send string directly to remote side */
                if ((q_status.emulation == Q_EMUL_VT220) && (state.s8c1t_mode == Q_TRUE)) {
                        qodem_write(q_child_tty_fd, "\2330n", 3, Q_TRUE);
                } else {
                        qodem_write(q_child_tty_fd, "\033[0n", 4, Q_TRUE);
                }
                break;

        case 6:
                /* Request cursor position.
                   Respond with current position */

                if ((q_status.emulation == Q_EMUL_VT220) && (state.s8c1t_mode == Q_TRUE)) {
                        memset(response_buffer, 0, sizeof(response_buffer));
                        snprintf(response_buffer, sizeof(response_buffer), "\233%u;%uR", q_status.cursor_y + 1, q_status.cursor_x + 1);
                } else {
                        memset(response_buffer, 0, sizeof(response_buffer));
                        snprintf(response_buffer, sizeof(response_buffer), "\033[%u;%uR", q_status.cursor_y + 1, q_status.cursor_x + 1);
                }

                /* Send string directly to remote side */
                qodem_write(q_child_tty_fd, response_buffer, strlen(response_buffer), Q_TRUE);
                break;

        case 15:
                if (state.dec_private_mode_flag == Q_TRUE) {

                        /* Request printer status report.
                           Respond with "Printer not connected." */

                        /* Send string directly to remote side */
                        if ((q_status.emulation == Q_EMUL_VT220) && (state.s8c1t_mode == Q_TRUE)) {
                                qodem_write(q_child_tty_fd, "\233?13n", 5, Q_TRUE);
                        } else {
                                qodem_write(q_child_tty_fd, "\033[?13n", 6, Q_TRUE);
                        }
                }
                break;

        case 25:
                if ((q_status.emulation == Q_EMUL_VT220) && (state.dec_private_mode_flag == Q_TRUE)) {

                        /* Request user-defined keys are locked or unlocked.
                           Respond with "User-defined keys are locked." */

                        /* Send string directly to remote side */
                        if (state.s8c1t_mode == Q_TRUE) {
                                qodem_write(q_child_tty_fd, "\233?21n", 5, Q_TRUE);
                        } else {
                                qodem_write(q_child_tty_fd, "\033[?21n", 6, Q_TRUE);
                        }
                }
                break;

        case 26:
                if ((q_status.emulation == Q_EMUL_VT220) && (state.dec_private_mode_flag == Q_TRUE)) {

                        /* Request keyboard language.
                           Respond with "Keyboard language is North American." */

                        /* Send string directly to remote side */
                        if (state.s8c1t_mode == Q_TRUE) {
                                qodem_write(q_child_tty_fd, "\233?27;1n", 7, Q_TRUE);
                        } else {
                                qodem_write(q_child_tty_fd, "\033[?27;1n", 8, Q_TRUE);
                        }
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

#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "da(): %d %d\n", extended_flag, i);
#endif /* DEBUG_VT100 */

        if ((i != 0) && (i != 1)) {
                return;
        }

        if ((extended_flag == 0) && (i == 0)) {
                /* Send string directly to remote side */
                if (q_status.emulation == Q_EMUL_VT100) {
                        qodem_write(q_child_tty_fd, VT100_DEVICE_TYPE_STRING, sizeof(VT100_DEVICE_TYPE_STRING), Q_TRUE);
                }
                if (q_status.emulation == Q_EMUL_VT102) {
                        qodem_write(q_child_tty_fd, VT102_DEVICE_TYPE_STRING, sizeof(VT102_DEVICE_TYPE_STRING), Q_TRUE);
                }
                if (q_status.emulation == Q_EMUL_VT220) {
                        if (state.s8c1t_mode == Q_TRUE) {
                                qodem_write(q_child_tty_fd, VT220_DEVICE_TYPE_STRING_S8C1T, sizeof(VT220_DEVICE_TYPE_STRING_S8C1T), Q_TRUE);
                        } else {
                                qodem_write(q_child_tty_fd, VT220_DEVICE_TYPE_STRING, sizeof(VT220_DEVICE_TYPE_STRING), Q_TRUE);
                        }
                }
        }

        if (q_status.emulation == Q_EMUL_VT220) {

                if ((extended_flag == 1) && (i == 0)) {
                        /* Request "What type of terminal are you,
                         what is your firmware version, and what
                         hardware options do you have installed?"

                         Respond: "I am a VT220 (identification code of 1),
                         my firmware version is _____ (Pv), and I have
                         _____ Po options installed."

                         (Same as xterm)

                         */

                        if ((q_status.emulation == Q_EMUL_VT220) && (state.s8c1t_mode == Q_TRUE)) {
                                memset(response_buffer, 0, sizeof(response_buffer));
                                snprintf(response_buffer, sizeof(response_buffer), "\233>1;10;0c");
                        } else {
                                memset(response_buffer, 0, sizeof(response_buffer));
                                snprintf(response_buffer, sizeof(response_buffer), "\033[>1;10;0c");
                        }

                        /* Send string directly to remote side */
                        qodem_write(q_child_tty_fd, response_buffer, strlen(response_buffer), Q_TRUE);
                }
        }

#if 0

        /* VT420 and up */

        if ((extended_flag == 2) && (i == 0)) {

                /* Request "What is your unit ID?"

                   Respond: "I was manufactured at site 00 and have
                   a unique ID number of 123."

                */

                memset(response_buffer, 0, sizeof(response_buffer));
                snprintf(response_buffer, sizeof(response_buffer), "\033P!|00010203\033\\");

                /* Send string directly to remote side */
                qodem_write(q_child_tty_fd, response_buffer, strlen(response_buffer), Q_TRUE);
        }

#endif

} /* ---------------------------------------------------------------------- */



/*
 * TBC - Tabulation clear
 */
static void tbc() {
        int i;
        i = atoi((char *)state.params[0]);

#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "tbc(): %d\n", i);
#endif /* DEBUG_VT100 */

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
 * Handle a control character function (C0 and C1 in the ECMA/ANSI spec)
 */
static void handle_control_char(const unsigned char control_char) {

#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "handle_control_char(): control_char = 0x%02x\n", control_char);
#endif /* DEBUG_VT100 */

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
                cursor_linefeed(q_vt100_new_line_mode);
                break;

        case 0x0B:
                /* VT */
                cursor_linefeed(q_vt100_new_line_mode);
                break;

        case 0x0C:
                /* FF */
                cursor_linefeed(q_vt100_new_line_mode);
                break;

        case 0x0D:
                /* CR */
                cursor_carriage_return();
                break;

        case 0x0E:
                /* SO */
                state.shift_out = Q_TRUE;
                state.lockshift_gl = LOCKSHIFT_NONE;
                break;

        case 0x0F:
                /* SI */
                state.shift_out = Q_FALSE;
                state.lockshift_gl = LOCKSHIFT_NONE;
                break;

        case 0x84:
                /* IND */
                ind();
                break;

        case 0x85:
                /* NEL */
                nel();
                break;

        case 0x88:
                /* HTS */
                hts();
                break;

        case 0x8D:
                /* RI */
                ri();
                break;

        case 0x8E:
                /* SS2 */
                state.singleshift = SS2;
                break;

        case 0x8F:
                /* SS3 */
                state.singleshift = SS3;
                break;

        default:
                break;
        }
} /* ---------------------------------------------------------------------- */

/*
 * osc_put - Handle the SCAN_OSC_STRING state.  Handle this in VT100 because
 * lots of remote systems will send an XTerm title sequence even if TERM
 * isn't xterm.
 */
static void osc_put(unsigned char xterm_char) {
        /* Collect first */
        q_emul_buffer[q_emul_buffer_n] = xterm_char;
        q_emul_buffer_n++;

#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "osc_put(): XTERM %c (%02x)\n", xterm_char, xterm_char);
#endif /* DEBUG_VT100 */

        /* Xterm cases... */
        if (xterm_char == 0x07) {
                /* Screen title */
                q_emul_buffer_n--;
                q_emul_buffer[q_emul_buffer_n] = 0;
#ifdef DEBUG_VT100
                fprintf(VT100_DEBUG_FILE_HANDLE, "osc_put(): xterm screen title: %s\n", q_emul_buffer);
#endif /* DEBUG_VT100 */
                /* Go to SCAN_GROUND state */
                clear_params();
                clear_collect_buffer();
                scan_state = SCAN_GROUND;
                return;
        }
} /* ---------------------------------------------------------------------- */

/*
 * vt100 - process through VT100 emulator.
 */
Q_EMULATION_STATUS vt100(const unsigned char from_modem1, wchar_t * to_screen) {
        Q_BOOL discard = Q_FALSE;
        unsigned char from_modem = from_modem1;

#ifdef DEBUG_VT100
        fprintf(VT100_DEBUG_FILE_HANDLE, "STATE: %d CHAR: 0x%02x '%c'\n", scan_state, from_modem, from_modem);
        fflush(VT100_DEBUG_FILE_HANDLE);
#endif /* DEBUG_VT100 */

        /* Special case for VT10x: 7-bit characters only */
        if ((q_status.emulation == Q_EMUL_VT100) || (q_status.emulation == Q_EMUL_VT102)) {
                from_modem = from_modem1 & 0x7F;
        } else {
                assert(q_status.emulation == Q_EMUL_VT220);
                from_modem = from_modem1;
        }

        /* Special "anywhere" states */

        /* 18, 1A                     --> execute, then switch to SCAN_GROUND */
        if ((from_modem == 0x18) || (from_modem == 0x1A)) {
                /* CAN and SUB abort escape sequences */
                clear_params();
                clear_collect_buffer();
                scan_state = SCAN_GROUND;
                discard = Q_TRUE;
        }

        /* 80-8F, 91-97, 99, 9A, 9C   --> execute, then switch to SCAN_GROUND */

        /* 0x1B == KEY_ESCAPE */
        if ((from_modem == KEY_ESCAPE) &&
                (scan_state != SCAN_DCS_ENTRY) &&
                (scan_state != SCAN_DCS_INTERMEDIATE) &&
                (scan_state != SCAN_DCS_IGNORE) &&
                (scan_state != SCAN_DCS_PARAM) &&
                (scan_state != SCAN_DCS_PASSTHROUGH)) {
                scan_state = SCAN_ESCAPE;
                discard = Q_TRUE;
        }

        /* 0x9B == CSI 8-bit sequence */
        if (from_modem == 0x9B) {
                scan_state = SCAN_CSI_ENTRY;
                discard = Q_TRUE;
        }

        /* 0x9D goes to SCAN_OSC_STRING */
        if (from_modem == 0x9D) {
                scan_state = SCAN_OSC_STRING;
                discard = Q_TRUE;
        }

        /* 0x90 goes to SCAN_DCS_ENTRY */
        if (from_modem == 0x90) {
                scan_state = SCAN_DCS_ENTRY;
                discard = Q_TRUE;
        }

        /* 0x98, 0x9E, and 0x9F go to SCAN_SOSPMAPC_STRING */
        if ((from_modem == 0x98) || (from_modem == 0x9E) || (from_modem == 0x9F)) {
                scan_state = SCAN_SOSPMAPC_STRING;
                discard = Q_TRUE;
        }

        /* 0x7F (DEL) is always discarded */
        if (from_modem == 0x7F) {
                discard = Q_TRUE;
        }

        /* If the character has been consumed, exit. */
        if (discard == Q_TRUE) {
                *to_screen = 1;
                return Q_EMUL_FSM_NO_CHAR_YET;
        }

        switch (scan_state) {

        case SCAN_GROUND:
                /* 00-17, 19, 1C-1F --> execute */
                /* 80-8F, 91-9A, 9C --> execute */
                if ((from_modem <= 0x1F) || ((from_modem >= 0x80) && (from_modem <= 0x9F))) {
                        handle_control_char(from_modem);
                        discard = Q_TRUE;
                        break;
                }

                /* 20-7F            --> print */
                if ((from_modem >= 0x20) && (from_modem <= 0x7F)) {

                        /* VT220 printer --> trash bin */
                        if ((q_status.emulation == Q_EMUL_VT220) && (state.printer_controller_mode == Q_TRUE)) {
                                discard = Q_TRUE;
                                break;
                        }

                        /* Immediately return this character */
                        *to_screen = map_character(from_modem);


#ifdef DEBUG_VT100
                        /* render_screen_to_debug_file(VT100_DEBUG_FILE_HANDLE); */
#endif /* DEBUG_VT100 */

                        return Q_EMUL_FSM_ONE_CHAR;
                }

                /* VT220: A0-FF            --> print */
                if (from_modem >= 0xA0) {

                        /* VT220 printer --> trash bin */
                        if (state.printer_controller_mode == Q_TRUE) {
                                discard = Q_TRUE;
                                break;
                        }

                        /* Immediately return this character */
                        *to_screen = map_character(from_modem);


#ifdef DEBUG_VT100
                        /* render_screen_to_debug_file(VT100_DEBUG_FILE_HANDLE); */
#endif /* DEBUG_VT100 */

                        return Q_EMUL_FSM_ONE_CHAR;
                }

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
                                /* Note this code overlaps both ANSI and VT52 mode */
                                decsc();
                                break;

                        case '8':
                                /* DECRC - Restore cursor */
                                /* Note this code overlaps both ANSI and VT52 mode */
                                decrc();
                                break;

                        case '9':
                        case ':':
                        case ';':
                                break;
                        case '<':
                                if (state.vt52_mode == Q_TRUE) {
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "VT52: DECANM (Exit VT52 mode)\n");
#endif /* DEBUG_VT100 */
                                        /* DECANM - Enter ANSI mode */
                                        state.vt52_mode = Q_FALSE;
                                        q_vt100_arrow_keys = Q_EMUL_VT100;
                                        q_vt100_keypad_mode.emulation = Q_EMUL_VT100;

                                        /*
                                           From the VT102 docs: "You use ANSI mode to select
                                           most terminal features; the terminal uses the same
                                           features when it switches to VT52 mode. You cannot,
                                           however, change most of these features in VT52
                                           mode."

                                           In other words, do not reset any other attributes
                                           when switching between VT52 submode and ANSI.
                                        */

                                        /* Reset fonts */
                                        state.g0_charset = CHARSET_US;
                                        state.g1_charset = CHARSET_DRAWING;
                                        state.s8c1t_mode = Q_FALSE;
                                        state.singleshift = SS_NONE;
                                        state.lockshift_gl = LOCKSHIFT_NONE;
                                        state.lockshift_gr = LOCKSHIFT_NONE;
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
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "VT52: cursor_up(1)\n");
#endif /* DEBUG_VT100 */
                                        /* Cursor up, and stop at the top without scrolling */
                                        cursor_up(1, Q_FALSE);
                                }
                                break;
                        case 'B':
                                if (state.vt52_mode == Q_TRUE) {
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "VT52: cursor_down(1)\n");
#endif /* DEBUG_VT100 */
                                        /* Cursor down, and stop at the bottom without scrolling */
                                        cursor_down(1, Q_FALSE);
                                }
                                break;
                        case 'C':
                                if (state.vt52_mode == Q_TRUE) {
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "VT52: cursor_right(1)\n");
#endif /* DEBUG_VT100 */
                                        /* Cursor right, and stop at the right without scrolling */
                                        cursor_right(1, Q_FALSE);
                                }
                                break;
                        case 'D':
                                if (state.vt52_mode == Q_TRUE) {
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "VT52: cursor_left(1)\n");
#endif /* DEBUG_VT100 */
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
                                        state.g0_charset = CHARSET_VT52_GRAPHICS;
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "VT52: CHARSET CHANGE: DRAWING in G0\n");
#endif /* DEBUG_VT100 */
                                }
                                break;
                        case 'G':
                                if (state.vt52_mode == Q_TRUE) {
                                        /* G0 --> ASCII set */
                                        state.g0_charset = CHARSET_US;
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "VT52: CHARSET CHANGE: US (ASCII) in G0\n");
#endif /* DEBUG_VT100 */
                                }
                                break;
                        case 'H':
                                if (state.vt52_mode == Q_TRUE) {
                                        /* Cursor to home */
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "VT52: Cursor to home\n");
#endif /* DEBUG_VT100 */
                                        cursor_position(0, 0);
                                } else {
                                        /* HTS - Horizontal tabulation set */
                                        hts();
                                }
                                break;
                        case 'I':
                                if (state.vt52_mode == Q_TRUE) {
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "VT52: Reverse line feed\n");
#endif /* DEBUG_VT100 */
                                        /* Reverse line feed.  Same as RI. */
                                        ri();
                                }
                                break;
                        case 'J':
                                if (state.vt52_mode == Q_TRUE) {
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "VT52: erase_line %d %d\n", q_status.cursor_x, WIDTH - 1);
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "VT52: erase_screen %d %d %d %d\n", q_status.cursor_y + 1, 0, HEIGHT - STATUS_HEIGHT - 1, WIDTH - 1);
#endif /* DEBUG_VT100 */
                                        /* Erase to end of screen */
                                        erase_line(q_status.cursor_x, WIDTH - 1, Q_FALSE);
                                        erase_screen(q_status.cursor_y + 1, 0, HEIGHT - STATUS_HEIGHT - 1, WIDTH - 1, Q_FALSE);
                                }
                                break;
                        case 'K':
                                if (state.vt52_mode == Q_TRUE) {
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "VT52: erase_line %d, %d\n", q_status.cursor_x, WIDTH - 1);
#endif /* DEBUG_VT100 */
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
                                if (state.vt52_mode == Q_FALSE) {
                                        /* SS2 */
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "SS2\n");
#endif /* DEBUG_VT100 */
                                        state.singleshift = SS2;
                                }
                                break;
                        case 'O':
                                if (state.vt52_mode == Q_FALSE) {
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "SS3\n");
#endif /* DEBUG_VT100 */
                                        /* SS3 */
                                        state.singleshift = SS3;
                                }
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
                                if (q_status.emulation == Q_EMUL_VT100) {
                                        qodem_write(q_child_tty_fd, VT100_DEVICE_TYPE_STRING, sizeof(VT100_DEVICE_TYPE_STRING), Q_TRUE);
                                }
                                if (q_status.emulation == Q_EMUL_VT102) {
                                        qodem_write(q_child_tty_fd, VT102_DEVICE_TYPE_STRING, sizeof(VT102_DEVICE_TYPE_STRING), Q_TRUE);
                                }
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        if (state.s8c1t_mode == Q_TRUE) {
                                                qodem_write(q_child_tty_fd, VT220_DEVICE_TYPE_STRING_S8C1T, sizeof(VT220_DEVICE_TYPE_STRING_S8C1T), Q_TRUE);
                                        } else {
                                                qodem_write(q_child_tty_fd, VT220_DEVICE_TYPE_STRING, sizeof(VT220_DEVICE_TYPE_STRING), Q_TRUE);
                                        }
                                }
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
                                vt100_reset();
                                /* Do I clear screen too? I think so... */
                                erase_screen(0, 0, HEIGHT - STATUS_HEIGHT - 1, WIDTH - 1, Q_FALSE);
                                cursor_position(0, 0);
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
                                break;
                        case 'n':
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        /* VT220 lockshift G2 into GL */
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "VT220:  LOCKSHIFT_G2_GL\n");
#endif /* DEBUG_VT100 */
                                        state.lockshift_gl = LOCKSHIFT_G2_GL;
                                        state.shift_out = Q_FALSE;
                                }
                                break;
                        case 'o':
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        /* VT220 lockshift G3 into GL */
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "VT220:  LOCKSHIFT_G3_GL\n");
#endif /* DEBUG_VT100 */
                                        state.lockshift_gl = LOCKSHIFT_G3_GL;
                                        state.shift_out = Q_FALSE;
                                }
                                break;
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
                                break;
                        case '|':
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        /* VT220 lockshift G3 into GR */
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "VT220:  LOCKSHIFT_G3_GR\n");
#endif /* DEBUG_VT100 */
                                        state.lockshift_gr = LOCKSHIFT_G3_GR;
                                        state.shift_out = Q_FALSE;
                                }
                                break;
                        case '}':
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        /* VT220 lockshift G2 into GR */
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "VT220:  LOCKSHIFT_G2_GR\n");
#endif /* DEBUG_VT100 */
                                        state.lockshift_gr = LOCKSHIFT_G2_GR;
                                        state.shift_out = Q_FALSE;
                                }
                                break;

                        case '~':
                                if (q_status.emulation == Q_EMUL_VT220) {
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "VT220:  LOCKSHIFT_G1_GR\n");
#endif /* DEBUG_VT100 */
                                        /* VT220 lockshift G1 into GR */
                                        state.lockshift_gr = LOCKSHIFT_G1_GR;
                                        state.shift_out = Q_FALSE;
                                }
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
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: DRAWING in G0\n");
#endif /* DEBUG_VT100 */
                                }
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == ')')) {
                                        /* G1 --> Special graphics */
                                        state.g1_charset = CHARSET_DRAWING;
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: DRAWING in G1\n");
#endif /* DEBUG_VT100 */
                                }
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '*')) {
                                                /* G2 --> Special graphics */
                                                state.g2_charset = CHARSET_DRAWING;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: DRAWING in G2\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '+')) {
                                                /* G3 --> Special graphics */
                                                state.g3_charset = CHARSET_DRAWING;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: DRAWING in G3\n");
#endif /* DEBUG_VT100 */
                                        }
                                }
                                break;
                        case '1':
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '(')) {
                                        /* G0 --> Alternate character ROM standard character set */
                                        state.g0_charset = CHARSET_ROM;
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: ROM in G0\n");
#endif /* DEBUG_VT100 */
                                }
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == ')')) {
                                        /* G1 --> Alternate character ROM standard character set */
                                        state.g1_charset = CHARSET_ROM;
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: ROM in G1\n");
#endif /* DEBUG_VT100 */
                                }
                                break;
                        case '2':
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '(')) {
                                        /* G0 --> Alternate character ROM special graphics */
                                        state.g0_charset = CHARSET_ROM_SPECIAL;
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: ROM ALTERNATE in G0\n");
#endif /* DEBUG_VT100 */
                                }
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == ')')) {
                                        /* G1 --> Alternate character ROM special graphics */
                                        state.g1_charset = CHARSET_ROM_SPECIAL;
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: ROM ALTERNATE in G1\n");
#endif /* DEBUG_VT100 */
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
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '(')) {
                                                /* G0 --> DUTCH */
                                                state.g0_charset = CHARSET_NRC_DUTCH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: DUTCH (NRC) in G0\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == ')')) {
                                                /* G1 --> DUTCH */
                                                state.g1_charset = CHARSET_NRC_DUTCH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: DUTCH (NRC) in G1\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '*')) {
                                                /* G2 --> DUTCH */
                                                state.g2_charset = CHARSET_NRC_DUTCH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: DUTCH (NRC) in G2\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '+')) {
                                                /* G3 --> DUTCH */
                                                state.g3_charset = CHARSET_NRC_DUTCH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: DUTCH (NRC) in G3\n");
#endif /* DEBUG_VT100 */
                                        }
                                }
                                break;
                        case '5':
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '#')) {
                                        /* DECSWL - Single-width line */
                                        decswl();
                                }
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '(')) {
                                                /* G0 --> FINNISH */
                                                state.g0_charset = CHARSET_NRC_FINNISH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: FINNISH (NRC) in G0\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == ')')) {
                                                /* G1 --> FINNISH */
                                                state.g1_charset = CHARSET_NRC_FINNISH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: FINNISH (NRC) in G1\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '*')) {
                                                /* G2 --> FINNISH */
                                                state.g2_charset = CHARSET_NRC_FINNISH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: FINNISH (NRC) in G2\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '+')) {
                                                /* G3 --> FINNISH */
                                                state.g3_charset = CHARSET_NRC_FINNISH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: FINNISH (NRC) in G3\n");
#endif /* DEBUG_VT100 */
                                        }
                                }
                                break;
                        case '6':
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '#')) {
                                        /* DECDWL - Double-width line */
                                        decdwl();
                                }
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '(')) {
                                                /* G0 --> NORWEGIAN */
                                                state.g0_charset = CHARSET_NRC_NORWEGIAN;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: NORWEGIAN (NRC) in G0\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == ')')) {
                                                /* G1 --> NORWEGIAN */
                                                state.g1_charset = CHARSET_NRC_NORWEGIAN;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: NORWEGIAN (NRC) in G1\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '*')) {
                                                /* G2 --> NORWEGIAN */
                                                state.g2_charset = CHARSET_NRC_NORWEGIAN;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: NORWEGIAN (NRC) in G2\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '+')) {
                                                /* G3 --> NORWEGIAN */
                                                state.g3_charset = CHARSET_NRC_NORWEGIAN;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: NORWEGIAN (NRC) in G3\n");
#endif /* DEBUG_VT100 */
                                        }
                                }
                                break;
                        case '7':
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '(')) {
                                                /* G0 --> SWEDISH */
                                                state.g0_charset = CHARSET_NRC_SWEDISH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: SWEDISH (NRC) in G0\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == ')')) {
                                                /* G1 --> SWEDISH */
                                                state.g1_charset = CHARSET_NRC_SWEDISH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: SWEDISH (NRC) in G1\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '*')) {
                                                /* G2 --> SWEDISH */
                                                state.g2_charset = CHARSET_NRC_SWEDISH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: SWEDISH (NRC) in G2\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '+')) {
                                                /* G3 --> SWEDISH */
                                                state.g3_charset = CHARSET_NRC_SWEDISH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: SWEDISH (NRC) in G3\n");
#endif /* DEBUG_VT100 */
                                        }
                                }
                                break;
                        case '8':
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '#')) {
                                        /* DECALN - Screen alignment display */
                                        decaln();
                                }
                                break;
                        case '9':
                        case ':':
                        case ';':
                                break;
                        case '<':
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '(')) {
                                                /* G0 --> DEC_SUPPLEMENTAL */
                                                state.g0_charset = CHARSET_DEC_SUPPLEMENTAL;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: DEC_SUPPLEMENTAL in G0\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == ')')) {
                                                /* G1 --> DEC_SUPPLEMENTAL */
                                                state.g1_charset = CHARSET_DEC_SUPPLEMENTAL;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: DEC_SUPPLEMENTAL in G1\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '*')) {
                                                /* G2 --> DEC_SUPPLEMENTAL */
                                                state.g2_charset = CHARSET_DEC_SUPPLEMENTAL;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: DEC_SUPPLEMENTAL in G2\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '+')) {
                                                /* G3 --> DEC_SUPPLEMENTAL */
                                                state.g3_charset = CHARSET_DEC_SUPPLEMENTAL;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: DEC_SUPPLEMENTAL in G3\n");
#endif /* DEBUG_VT100 */
                                        }
                                }
                                break;
                        case '=':
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '(')) {
                                                /* G0 --> SWISS */
                                                state.g0_charset = CHARSET_NRC_SWISS;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: SWISS (NRC) in G0\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == ')')) {
                                                /* G1 --> SWISS */
                                                state.g1_charset = CHARSET_NRC_SWISS;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: SWISS (NRC) in G1\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '*')) {
                                                /* G2 --> SWISS */
                                                state.g2_charset = CHARSET_NRC_SWISS;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: SWISS (NRC) in G2\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '+')) {
                                                /* G3 --> SWISS */
                                                state.g3_charset = CHARSET_NRC_SWISS;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: SWISS (NRC) in G3\n");
#endif /* DEBUG_VT100 */
                                        }
                                }
                                break;
                        case '>':
                        case '?':
                        case '@':
                                break;
                        case 'A':
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '(')) {
                                        /* G0 --> United Kingdom set */
                                        state.g0_charset = CHARSET_UK;
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: UK (BRITISH) in G0\n");
#endif /* DEBUG_VT100 */
                                }
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == ')')) {
                                        /* G1 --> United Kingdom set */
                                        state.g1_charset = CHARSET_UK;
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: UK (BRITISH) in G1\n");
#endif /* DEBUG_VT100 */
                                }
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '*')) {
                                                /* G2 --> United Kingdom set */
                                                state.g2_charset = CHARSET_UK;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: UK (BRITISH) in G2\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '+')) {
                                                /* G3 --> United Kingdom set */
                                                state.g3_charset = CHARSET_UK;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: UK (BRITISH) in G3\n");
#endif /* DEBUG_VT100 */
                                        }
                                }
                                break;
                        case 'B':
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '(')) {
                                        /* G0 --> ASCII set */
                                        state.g0_charset = CHARSET_US;
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: US (ASCII) in G0\n");
#endif /* DEBUG_VT100 */
                                }
                                if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == ')')) {
                                        /* G1 --> ASCII set */
                                        state.g1_charset = CHARSET_US;
#ifdef DEBUG_VT100
                                        fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: US (ASCII) in G1\n");
#endif /* DEBUG_VT100 */
                                }
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '*')) {
                                                /* G2 --> ASCII */
                                                state.g2_charset = CHARSET_US;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: US (ASCII) in G2\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '+')) {
                                                /* G3 --> ASCII */
                                                state.g3_charset = CHARSET_US;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: US (ASCII) in G3\n");
#endif /* DEBUG_VT100 */
                                        }
                                }
                                break;
                        case 'C':
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '(')) {
                                                /* G0 --> FINNISH */
                                                state.g0_charset = CHARSET_NRC_FINNISH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: FINNISH (NRC) in G0\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == ')')) {
                                                /* G1 --> FINNISH */
                                                state.g1_charset = CHARSET_NRC_FINNISH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: FINNISH (NRC) in G1\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '*')) {
                                                /* G2 --> FINNISH */
                                                state.g2_charset = CHARSET_NRC_FINNISH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: FINNISH (NRC) in G2\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '+')) {
                                                /* G3 --> FINNISH */
                                                state.g3_charset = CHARSET_NRC_FINNISH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: FINNISH (NRC) in G3\n");
#endif /* DEBUG_VT100 */
                                        }
                                }
                                break;
                        case 'D':
                                break;
                        case 'E':
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '(')) {
                                                /* G0 --> NORWEGIAN */
                                                state.g0_charset = CHARSET_NRC_NORWEGIAN;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: NORWEGIAN (NRC) in G0\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == ')')) {
                                                /* G1 --> NORWEGIAN */
                                                state.g1_charset = CHARSET_NRC_NORWEGIAN;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: NORWEGIAN (NRC) in G1\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '*')) {
                                                /* G2 --> NORWEGIAN */
                                                state.g2_charset = CHARSET_NRC_NORWEGIAN;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: NORWEGIAN (NRC) in G2\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '+')) {
                                                /* G3 --> NORWEGIAN */
                                                state.g3_charset = CHARSET_NRC_NORWEGIAN;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: NORWEGIAN (NRC) in G3\n");
#endif /* DEBUG_VT100 */
                                        }
                                }
                                break;
                        case 'F':
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == ' ')) {
                                                /* S7C1T */
                                                state.s8c1t_mode = Q_FALSE;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "S7C1T\n");
#endif /* DEBUG_VT100 */
                                        }
                                }
                                break;
                        case 'G':
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == ' ')) {
                                                /* S8C1T */
                                                state.s8c1t_mode = Q_TRUE;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "S8C1T\n");
#endif /* DEBUG_VT100 */
                                        }
                                }
                                break;
                        case 'H':
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '(')) {
                                                /* G0 --> SWEDISH */
                                                state.g0_charset = CHARSET_NRC_SWEDISH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: SWEDISH (NRC) in G0\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == ')')) {
                                                /* G1 --> SWEDISH */
                                                state.g1_charset = CHARSET_NRC_SWEDISH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: SWEDISH (NRC) in G1\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '*')) {
                                                /* G2 --> SWEDISH */
                                                state.g2_charset = CHARSET_NRC_SWEDISH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: SWEDISH (NRC) in G2\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '+')) {
                                                /* G3 --> SWEDISH */
                                                state.g3_charset = CHARSET_NRC_SWEDISH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: SWEDISH (NRC) in G3\n");
#endif /* DEBUG_VT100 */
                                        }
                                }
                                break;
                        case 'I':
                        case 'J':
                                break;
                        case 'K':
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '(')) {
                                                /* G0 --> GERMAN */
                                                state.g0_charset = CHARSET_NRC_GERMAN;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: GERMAN (NRC) in G0\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == ')')) {
                                                /* G1 --> GERMAN */
                                                state.g1_charset = CHARSET_NRC_GERMAN;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: GERMAN (NRC) in G1\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '*')) {
                                                /* G2 --> GERMAN */
                                                state.g2_charset = CHARSET_NRC_GERMAN;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: GERMAN (NRC) in G2\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '+')) {
                                                /* G3 --> GERMAN */
                                                state.g3_charset = CHARSET_NRC_GERMAN;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: GERMAN (NRC) in G3\n");
#endif /* DEBUG_VT100 */
                                        }
                                }
                                break;
                        case 'L':
                        case 'M':
                        case 'N':
                        case 'O':
                        case 'P':
                                break;
                        case 'Q':
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '(')) {
                                                /* G0 --> FRENCH_CA */
                                                state.g0_charset = CHARSET_NRC_FRENCH_CA;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: FRENCH_CA (NRC) in G0\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == ')')) {
                                                /* G1 --> FRENCH_CA */
                                                state.g1_charset = CHARSET_NRC_FRENCH_CA;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: FRENCH_CA (NRC) in G1\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '*')) {
                                                /* G2 --> FRENCH_CA */
                                                state.g2_charset = CHARSET_NRC_FRENCH_CA;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: FRENCH_CA (NRC) in G2\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '+')) {
                                                /* G3 --> FRENCH_CA */
                                                state.g3_charset = CHARSET_NRC_FRENCH_CA;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: FRENCH_CA (NRC) in G3\n");
#endif /* DEBUG_VT100 */
                                        }
                                }
                                break;
                        case 'R':
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '(')) {
                                                /* G0 --> FRENCH */
                                                state.g0_charset = CHARSET_NRC_FRENCH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: FRENCH (NRC) in G0\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == ')')) {
                                                /* G1 --> FRENCH */
                                                state.g1_charset = CHARSET_NRC_FRENCH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: FRENCH (NRC) in G1\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '*')) {
                                                /* G2 --> FRENCH */
                                                state.g2_charset = CHARSET_NRC_FRENCH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: FRENCH (NRC) in G2\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '+')) {
                                                /* G3 --> FRENCH */
                                                state.g3_charset = CHARSET_NRC_FRENCH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: FRENCH (NRC) in G3\n");
#endif /* DEBUG_VT100 */
                                        }
                                }
                                break;
                        case 'S':
                        case 'T':
                        case 'U':
                        case 'V':
                        case 'W':
                        case 'X':
                                break;
                        case 'Y':
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '(')) {
                                                /* G0 --> ITALIAN */
                                                state.g0_charset = CHARSET_NRC_ITALIAN;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: ITALIAN (NRC) in G0\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == ')')) {
                                                /* G1 --> ITALIAN */
                                                state.g1_charset = CHARSET_NRC_ITALIAN;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: ITALIAN (NRC) in G1\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '*')) {
                                                /* G2 --> ITALIAN */
                                                state.g2_charset = CHARSET_NRC_ITALIAN;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: ITALIAN (NRC) in G2\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '+')) {
                                                /* G3 --> ITALIAN */
                                                state.g3_charset = CHARSET_NRC_ITALIAN;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: ITALIAN (NRC) in G3\n");
#endif /* DEBUG_VT100 */
                                        }
                                }
                                break;
                        case 'Z':
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '(')) {
                                                /* G0 --> SPANISH */
                                                state.g0_charset = CHARSET_NRC_SPANISH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: SPANISH (NRC) in G0\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == ')')) {
                                                /* G1 --> SPANISH */
                                                state.g1_charset = CHARSET_NRC_SPANISH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: SPANISH (NRC) in G1\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '*')) {
                                                /* G2 --> SPANISH */
                                                state.g2_charset = CHARSET_NRC_SPANISH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: SPANISH (NRC) in G2\n");
#endif /* DEBUG_VT100 */
                                        }
                                        if ((q_emul_buffer_n == 1) && (q_emul_buffer[0] == '+')) {
                                                /* G3 --> SPANISH */
                                                state.g3_charset = CHARSET_NRC_SPANISH;
#ifdef DEBUG_VT100
                                                fprintf(VT100_DEBUG_FILE_HANDLE, "CHARSET CHANGE: SPANISH (NRC) in G3\n");
#endif /* DEBUG_VT100 */
                                        }
                                }
                                break;
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
                        case 'F':
                        case 'G':
                                break;
                        case 'H':
                                /* CUP - Cursor position */
                                cup();
                                break;
                        case 'I':
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
                        case 'S':
                        case 'T':
                        case 'U':
                        case 'V':
                        case 'W':
                                break;
                        case 'X':
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        /* ECH - Erase character */
                                        ech();
                                }
                                break;
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
                                break;
                        case 'c':
                                /* DA - Device attributes */
                                /* Send string directly to remote side */
                                if (q_status.emulation == Q_EMUL_VT100) {
                                        qodem_write(q_child_tty_fd, VT100_DEVICE_TYPE_STRING, sizeof(VT100_DEVICE_TYPE_STRING), Q_TRUE);
                                }
                                if (q_status.emulation == Q_EMUL_VT102) {
                                        qodem_write(q_child_tty_fd, VT102_DEVICE_TYPE_STRING, sizeof(VT102_DEVICE_TYPE_STRING), Q_TRUE);
                                }
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        if (state.s8c1t_mode == Q_TRUE) {
                                                qodem_write(q_child_tty_fd, VT220_DEVICE_TYPE_STRING_S8C1T, sizeof(VT220_DEVICE_TYPE_STRING_S8C1T), Q_TRUE);
                                        } else {
                                                qodem_write(q_child_tty_fd, VT220_DEVICE_TYPE_STRING, sizeof(VT220_DEVICE_TYPE_STRING), Q_TRUE);
                                        }
                                }
                                break;
                        case 'd':
                        case 'e':
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
                                break;
                        case 'i':
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        /* Printer functions */
                                        printer_functions();
                                }
                                break;
                        case 'j':
                        case 'k':
                        case 'l':
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
                        case 'F':
                        case 'G':
                                break;
                        case 'H':
                                /* CUP - Cursor position */
                                cup();
                                break;
                        case 'I':
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
                        case 'S':
                        case 'T':
                        case 'U':
                        case 'V':
                        case 'W':
                                break;
                        case 'X':
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        /* ECH - Erase character */
                                        ech();
                                }
                                break;
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
                                break;
                        case 'c':
                                /* DA - Device attributes */
                                da();
                                break;
                        case 'd':
                        case 'e':
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
                                if (q_status.emulation == Q_EMUL_VT220) {
                                        /* Printer functions */
                                        printer_functions();
                                }
                                break;
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
                                if ((q_status.emulation == Q_EMUL_VT220) && (q_emul_buffer[q_emul_buffer_n-1] == '\"')) {
                                        /* DECSCL - compatibility level */
                                        decscl();
                                }
                                break;
                        case 'q':
                                if ((q_status.emulation == Q_EMUL_VT220) && (q_emul_buffer[q_emul_buffer_n-1] == '\"')) {
                                        /* DESCSCA */
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

                /* 0x9C goes to SCAN_GROUND */
                if (from_modem == 0x9C) {
                        clear_params();
                        clear_collect_buffer();
                        scan_state = SCAN_GROUND;
                        discard = Q_TRUE;
                        break;
                }

                /* 0x1B 0x5C goes to SCAN_GROUND */
                if (from_modem == 0x1B) {
                        collect(from_modem, to_screen);
                        discard = Q_TRUE;
                        break;
                }
                if (from_modem == 0x5C) {
                        if ((q_emul_buffer_n > 0) && (q_emul_buffer[q_emul_buffer_n - 1] == 0x1B)) {
                                clear_params();
                                clear_collect_buffer();
                                scan_state = SCAN_GROUND;
                                discard = Q_TRUE;
                                break;
                        }
                }

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

                /* 0x9C goes to SCAN_GROUND */
                if (from_modem == 0x9C) {
                        clear_params();
                        clear_collect_buffer();
                        scan_state = SCAN_GROUND;
                        discard = Q_TRUE;
                        break;
                }

                /* 0x1B 0x5C goes to SCAN_GROUND */
                if (from_modem == 0x1B) {
                        collect(from_modem, to_screen);
                        discard = Q_TRUE;
                        break;
                }
                if (from_modem == 0x5C) {
                        if ((q_emul_buffer_n > 0) && (q_emul_buffer[q_emul_buffer_n - 1] == 0x1B)) {
                                clear_params();
                                clear_collect_buffer();
                                scan_state = SCAN_GROUND;
                                discard = Q_TRUE;
                                break;
                        }
                }

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

                /* 0x9C goes to SCAN_GROUND */
                if (from_modem == 0x9C) {
                        clear_params();
                        clear_collect_buffer();
                        scan_state = SCAN_GROUND;
                        discard = Q_TRUE;
                        break;
                }

                /* 0x1B 0x5C goes to SCAN_GROUND */
                if (from_modem == 0x1B) {
                        collect(from_modem, to_screen);
                        discard = Q_TRUE;
                        break;
                }
                if (from_modem == 0x5C) {
                        if ((q_emul_buffer_n > 0) && (q_emul_buffer[q_emul_buffer_n - 1] == 0x1B)) {
                                clear_params();
                                clear_collect_buffer();
                                scan_state = SCAN_GROUND;
                                discard = Q_TRUE;
                                break;
                        }
                }

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
                /* 0x9C goes to SCAN_GROUND */
                if (from_modem == 0x9C) {
                        clear_params();
                        clear_collect_buffer();
                        scan_state = SCAN_GROUND;
                        discard = Q_TRUE;
                        break;
                }

                /* 0x1B 0x5C goes to SCAN_GROUND */
                if (from_modem == 0x1B) {
                        collect(from_modem, to_screen);
                        discard = Q_TRUE;
                        break;
                }
                if (from_modem == 0x5C) {
                        if ((q_emul_buffer_n > 0) && (q_emul_buffer[q_emul_buffer_n - 1] == 0x1B)) {
                                clear_params();
                                clear_collect_buffer();
                                scan_state = SCAN_GROUND;
                                discard = Q_TRUE;
                                break;
                        }
                }

                /* 00-17, 19, 1C-1F, 20-7E   --> put */
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
                if ((from_modem >= 0x20) && (from_modem <= 0x7E)) {
                        discard = Q_TRUE;
                        break;
                }

                /* 7F                        --> ignore */
                if (from_modem == 0x7F) {
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
                /* Special case for Xterm: OSC can pass control characters */
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
#ifdef DEBUG_VT100
                        fprintf(VT100_DEBUG_FILE_HANDLE, "VT52: cursor_position %d, %d\n", q_emul_buffer[0] - '\040', from_modem - '\040');
#endif /* DEBUG_VT100 */
                        cursor_position(q_emul_buffer[0] - '\040', from_modem - '\040');
                        clear_params();
                        clear_collect_buffer();
                        scan_state = SCAN_GROUND;
                }

                discard = Q_TRUE;
                break;
        }

#ifdef DEBUG_VT100
        /* render_screen_to_debug_file(VT100_DEBUG_FILE_HANDLE); */
#endif /* DEBUG_VT100 */

        /* If the character has been consumed, exit. */
        if (discard == Q_TRUE) {
                *to_screen = 1;
                return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /* Discard everything else */
        *to_screen = 1;
        return Q_EMUL_FSM_NO_CHAR_YET;
} /* ---------------------------------------------------------------------- */

/*
 * vt100_keystroke
 */
wchar_t * vt100_keystroke(const int keystroke) {

        switch (keystroke) {
        case Q_KEY_BACKSPACE:
                if ((q_status.hard_backspace == Q_TRUE) && (q_status.emulation != Q_EMUL_VT220)) {
                        return L"\010";
                } else {
                        return L"\177";
                }

        case Q_KEY_LEFT:
                switch (q_vt100_arrow_keys) {
                case Q_EMUL_ANSI:
                        return L"\033[D";
                case Q_EMUL_VT52:
                        return L"\033D";
                default:
                        return L"\033OD";
                }

        case Q_KEY_RIGHT:
                switch (q_vt100_arrow_keys) {
                case Q_EMUL_ANSI:
                        return L"\033[C";
                case Q_EMUL_VT52:
                        return L"\033C";
                default:
                        return L"\033OC";
                }

        case Q_KEY_UP:
                switch (q_vt100_arrow_keys) {
                case Q_EMUL_ANSI:
                        return L"\033[A";
                case Q_EMUL_VT52:
                        return L"\033A";
                default:
                        return L"\033OA";
                }

        case Q_KEY_DOWN:
                switch (q_vt100_arrow_keys) {
                case Q_EMUL_ANSI:
                        return L"\033[B";
                case Q_EMUL_VT52:
                        return L"\033B";
                default:
                        return L"\033OB";
                }

        case Q_KEY_HOME:
                switch (q_vt100_arrow_keys) {
                case Q_EMUL_ANSI:
                        return L"\033[H";
                case Q_EMUL_VT52:
                        return L"\033H";
                default:
                        return L"\033OH";
                }

        case Q_KEY_END:
                switch (q_vt100_arrow_keys) {
                case Q_EMUL_ANSI:
                        return L"\033[F";
                case Q_EMUL_VT52:
                        return L"\033F";
                default:
                        return L"\033OF";
                }

        case Q_KEY_F(1):
                /* PF1 */
                switch (q_vt100_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\033P";
                default:
                        return L"\033OP";
                }

        case Q_KEY_F(2):
                /* PF2 */
                switch (q_vt100_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\033Q";
                default:
                        return L"\033OQ";
                }
                return L"\033OQ";

        case Q_KEY_F(3):
                /* PF3 */
                switch (q_vt100_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\033R";
                default:
                        return L"\033OR";
                }

        case Q_KEY_F(4):
                /* PF4 */
                switch (q_vt100_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\033S";
                default:
                        return L"\033OS";
                }

        case Q_KEY_F(5):
                return L"\033Ot";

        case Q_KEY_F(6):
                return L"\033Ou";

        case Q_KEY_F(7):
                return L"\033Ov";

        case Q_KEY_F(8):
                return L"\033Ol";

        case Q_KEY_F(9):
                return L"\033Ow";

        case Q_KEY_F(10):
                return L"\033Ox";

        case Q_KEY_F(11):
                return L"\033[23~";

        case Q_KEY_F(12):
                return L"\033[24~";

        case Q_KEY_F(13):
                /* Shifted PF1 */
                switch (q_vt100_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\0332P";
                default:
                        return L"\033O2P";
                }

        case Q_KEY_F(14):
                /* Shifted PF2 */
                switch (q_vt100_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\0332Q";
                default:
                        return L"\033O2Q";
                }

        case Q_KEY_F(15):
                /* Shifted PF3 */
                switch (q_vt100_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\0332R";
                default:
                        return L"\033O2R";
                }

        case Q_KEY_F(16):
                /* Shifted PF4 */
                switch (q_vt100_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\0332S";
                default:
                        return L"\033O2S";
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
                switch (q_vt100_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\0335P";
                default:
                        return L"\033O5P";
                }

        case Q_KEY_F(26):
                /* Control PF2 */
                switch (q_vt100_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\0335Q";
                default:
                        return L"\033O5Q";
                }

        case Q_KEY_F(27):
                /* Control PF3 */
                switch (q_vt100_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\0335R";
                default:
                        return L"\033O5R";
                }

        case Q_KEY_F(28):
                /* Control PF4 */
                switch (q_vt100_keypad_mode.emulation) {
                case Q_EMUL_VT52:
                        return L"\0335S";
                default:
                        return L"\033O5S";
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
                return L"\033[2~";

        case Q_KEY_SIC:
                /* This is what xterm sends for SHIFT-INS */
                return L"\033[2;2~";
                /* This is what xterm sends for CTRL-INS */
                /* return L"\033[2;5~"; */

        case Q_KEY_SDC:
                /* This is what xterm sends for SHIFT-DEL */
                return L"\033[3;2~";
                /* This is what xterm sends for CTRL-DEL */
                /* return L"\033[3;5~"; */

        case Q_KEY_DC:
                /* Delete sends real delete for VTxxx */
                return L"\177";
                /* return L"\033[3~"; */

        case Q_KEY_PAD0:
                if (q_vt100_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 0 */
                        switch (q_vt100_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?p";
                        default:
                                return L"\033Op";
                        }
                }
                return L"0";

        case Q_KEY_C1:
        case Q_KEY_PAD1:
                if (q_vt100_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 1 */
                        switch (q_vt100_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?q";
                        default:
                                return L"\033Oq";
                        }
                }
                return L"1";

        case Q_KEY_C2:
        case Q_KEY_PAD2:
                if (q_vt100_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 2 */
                        switch (q_vt100_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?r";
                        default:
                                return L"\033Or";
                        }
                }
                return L"2";

        case Q_KEY_C3:
        case Q_KEY_PAD3:
                if (q_vt100_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 3 */
                        switch (q_vt100_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?s";
                        default:
                                return L"\033Os";
                        }
                }
                return L"3";

        case Q_KEY_B1:
        case Q_KEY_PAD4:
                if (q_vt100_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 4 */
                        switch (q_vt100_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?t";
                        default:
                                return L"\033Ot";
                        }
                }
                return L"4";

        case Q_KEY_B2:
        case Q_KEY_PAD5:
                if (q_vt100_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 5 */
                        switch (q_vt100_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?u";
                        default:
                                return L"\033Ou";
                        }
                }
                return L"5";

        case Q_KEY_B3:
        case Q_KEY_PAD6:
                if (q_vt100_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 6 */
                        switch (q_vt100_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?v";
                        default:
                                return L"\033Ov";
                        }
                }
                return L"6";

        case Q_KEY_A1:
        case Q_KEY_PAD7:
                if (q_vt100_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 7 */
                        switch (q_vt100_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?w";
                        default:
                                return L"\033Ow";
                        }

                }
                return L"7";

        case Q_KEY_A2:
        case Q_KEY_PAD8:
                if (q_vt100_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 8 */
                        switch (q_vt100_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?x";
                        default:
                                return L"\033Ox";
                        }
                }
                return L"8";

        case Q_KEY_A3:
        case Q_KEY_PAD9:
                if (q_vt100_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad 9 */
                        switch (q_vt100_keypad_mode.emulation) {
                        case Q_EMUL_VT52:
                                return L"\033?y";
                        default:
                                return L"\033Oy";
                        }
                }
                return L"9";

        case Q_KEY_PAD_STOP:
                if (q_vt100_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad . */
                        switch (q_vt100_keypad_mode.emulation) {
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
                if (q_vt100_keypad_mode.keypad_mode != Q_KEYPAD_MODE_NUMERIC) {
                        /* Number pad - */
                        switch (q_vt100_keypad_mode.emulation) {
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
