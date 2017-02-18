/*
 * atascii.c
 *
 * qodem - Qodem Terminal Emulator
 *
 * Written 2003-2017 by Kevin Lamonte
 *
 * To the extent possible under law, the author(s) have dedicated all
 * copyright and related and neighboring rights to this software to the
 * public domain worldwide. This software is distributed without any
 * warranty.
 *
 * You should have received a copy of the CC0 Public Domain Dedication along
 * with this software. If not, see
 * <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

#include "common.h"
#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "qodem.h"
#include "screen.h"
#include "scrollback.h"
#include "options.h"
#include "ansi.h"
#include "atascii.h"

/* Set this to a not-NULL value to enable debug log. */
/* static const char * DLOGNAME = "atascii"; */
static const char * DLOGNAME = NULL;

/**
 * Scan states for the parser state machine.
 */
typedef enum SCAN_STATES {
    SCAN_NONE,
    SCAN_ESC,
    SCAN_CSI,
    SCAN_CSI_PARAM,
    SCAN_ANSI_FALLBACK,
    DUMP_UNKNOWN_SEQUENCE
} SCAN_STATE;

/* Current scanning state. */
static SCAN_STATE scan_state;

/**
 * State change flags for the Commodore keyboard/screen.
 */
struct atari_state {
    /**
     * If true, reverse video is enabled.
     */
    Q_BOOL reverse;
};

/**
 * The current keyboard/screen state.
 */
static struct atari_state state = {
    Q_FALSE,
};

/**
 * ANSI fallback: the unknown escape sequence is copied here and then run
 * through the ANSI emulator.
 */
static unsigned char ansi_buffer[sizeof(q_emul_buffer)];
static int ansi_buffer_n;
static int ansi_buffer_i;

/**
 * ATASCII (Atari) to Unicode map.
 */
static wchar_t atascii_chars[128] = {
    0x2665, 0x251C, 0x23B9, 0x2518, 0x2524, 0x2510, 0x2571, 0x2572,
    0x25E2, 0x2597, 0x25E3, 0x259D, 0x2598, 0x23BA, 0x23BD, 0x2596,
    0x2663, 0x250C, 0x2500, 0x253C, 0x25CF, 0x2584, 0x23B8, 0x252C,
    0x2534, 0x258C, 0x2514, 0x241B, 0x2191, 0x2193, 0x2190, 0x2192,
    0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
    0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
    0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
    0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
    0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
    0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
    0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
    0x2666, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
    0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
    0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
    0x0078, 0x0079, 0x007A, 0x2660, 0x007C, 0x2196, 0x25C0, 0x25B6
};

/**
 * Reset the emulation state.
 */
void atascii_reset() {
    DLOG(("atascii_reset()\n"));

    scan_state = SCAN_NONE;
    state.reverse = Q_FALSE;
}

/**
 * Reset the scan state for a new sequence.
 *
 * @param to_screen one of the Q_EMULATION_STATUS constants.  See emulation.h.
 */
static void clear_state(wchar_t * to_screen) {
    q_status.insert_mode = Q_FALSE;
    q_emul_buffer_n = 0;
    q_emul_buffer_i = 0;
    memset(q_emul_buffer, 0, sizeof(q_emul_buffer));
    scan_state = SCAN_NONE;
    *to_screen = 1;
}

/**
 * Hang onto one character in the buffer.
 *
 * @param keep_char the character to save into q_emul_buffer
 * @param to_screen one of the Q_EMULATION_STATUS constants.  See emulation.h.
 */
static void save_char(unsigned char keep_char, wchar_t * to_screen) {
    q_emul_buffer[q_emul_buffer_n] = keep_char;
    q_emul_buffer_n++;
    *to_screen = 1;
}

/**
 * Process a control character.
 *
 * @param control_char a byte in the C0 or C1 range.
 */
static void atascii_handle_control_char(const unsigned char control_char) {
    short foreground, background;
    short curses_color;
    attr_t attributes = q_current_color & NO_COLOR_MASK;

    /*
     * Pull the current foreground and background.
     */
    curses_color = color_from_attr(q_current_color);
    foreground = (curses_color & 0x38) >> 3;
    background = curses_color & 0x07;

    switch (control_char) {
        // TODO
    default:
        break;
    }

    /* Change to whatever attribute was selected. */
    curses_color = (foreground << 3) | background;
    attributes |= color_to_attr(curses_color);
    q_current_color = attributes;

}

/**
 * Push one byte through the ATASCII emulator.
 *
 * @param from_modem one byte from the remote side.
 * @param to_screen if the return is Q_EMUL_FSM_ONE_CHAR or
 * Q_EMUL_FSM_MANY_CHARS, then to_screen will have a character to display on
 * the screen.
 * @return one of the Q_EMULATION_STATUS constants.  See emulation.h.
 */
Q_EMULATION_STATUS atascii(const unsigned char from_modem,
                           wchar_t * to_screen) {

    static unsigned char * count;
    static attr_t attributes;
    Q_EMULATION_STATUS rc;

    DLOG(("STATE: %d CHAR: 0x%02x '%c'\n", scan_state, from_modem, from_modem));

    if (q_status.atascii_has_wide_font == Q_FALSE) {
        /*
         * We don't think our font is double-width, so ask xterm/X11 to make
         * it bigger for us.
         */
        set_double_width(Q_TRUE);
    }

atascii_start:

    switch (scan_state) {

    /* ANSI Fallback ------------------------------------------------------- */

    case SCAN_ANSI_FALLBACK:

        /*
         * From here on out we pass through ANSI until we don't get
         * Q_EMUL_FSM_NO_CHAR_YET.
         */

        DLOG(("ANSI FALLBACK ansi_buffer_i %d ansi_buffer_n %d\n",
                ansi_buffer_i, ansi_buffer_n));
        DLOG(("              q_emul_buffer_i %d q_emul_buffer_n %d\n",
                q_emul_buffer_i, q_emul_buffer_n));

        if (ansi_buffer_n == 0) {
            assert(ansi_buffer_i == 0);
            /*
             * We have already cleared the old buffer, now push one byte at a
             * time through ansi until it is finished with its state machine.
             */
            ansi_buffer[ansi_buffer_n] = from_modem;
            ansi_buffer_n++;
        }

        DLOG(("ANSI FALLBACK ansi()\n"));

        rc = Q_EMUL_FSM_NO_CHAR_YET;
        while (rc == Q_EMUL_FSM_NO_CHAR_YET) {
            rc = ansi(ansi_buffer[ansi_buffer_i], to_screen);

            DLOG(("ANSI FALLBACK ansi() RC %d\n", rc));

            if (rc != Q_EMUL_FSM_NO_CHAR_YET) {
                /*
                 * We can be ourselves again now.
                 */
                DLOG(("ANSI FALLBACK END\n"));
                scan_state = SCAN_NONE;
            }

            ansi_buffer_i++;
            if (ansi_buffer_i == ansi_buffer_n) {
                /*
                 * No more characters to send through ANSI.
                 */
                ansi_buffer_n = 0;
                ansi_buffer_i = 0;
                break;
            }
        }

        if (rc == Q_EMUL_FSM_MANY_CHARS) {
            /*
             * ANSI is dumping q_emul_buffer.  Finish the job.
             */
            scan_state = DUMP_UNKNOWN_SEQUENCE;
        }

        return rc;

    case DUMP_UNKNOWN_SEQUENCE:

        DLOG(("DUMP_UNKNOWN_SEQUENCE q_emul_buffer_i %d q_emul_buffer_n %d\n",
                q_emul_buffer_i, q_emul_buffer_n));

        /*
         * Dump the string in q_emul_buffer
         */
        assert(q_emul_buffer_n > 0);

        *to_screen = codepage_map_char(q_emul_buffer[q_emul_buffer_i]);
        q_emul_buffer_i++;
        if (q_emul_buffer_i == q_emul_buffer_n) {
            /*
             * This was the last character.
             */
            q_emul_buffer_n = 0;
            q_emul_buffer_i = 0;
            memset(q_emul_buffer, 0, sizeof(q_emul_buffer));
            scan_state = SCAN_NONE;
            return Q_EMUL_FSM_ONE_CHAR;

        } else {
            return Q_EMUL_FSM_MANY_CHARS;
        }

    case SCAN_ESC:
        save_char(from_modem, to_screen);

        if (from_modem == '[') {
            if (q_status.atascii_color == Q_TRUE) {
                /*
                 * Fall into SCAN_CSI only if ATASCII_COLOR is enabled.
                 */
                scan_state = SCAN_CSI;
                return Q_EMUL_FSM_NO_CHAR_YET;
            }
        }

        /*
         * Fall-through to ANSI fallback.
         */
        break;

    case SCAN_CSI:
        save_char(from_modem, to_screen);

        /*
         * We are only going to support CSI Pn [ ; Pn ... ] m a.k.a. ANSI
         * Select Graphics Rendition.  We can see only a digit or 'm'.
         */
        if (q_isdigit(from_modem)) {
            /*
             * Save the position for the counter.
             */
            count = q_emul_buffer + q_emul_buffer_n - 1;
            scan_state = SCAN_CSI_PARAM;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'm') {
            /*
             * ESC [ m mean ESC [ 0 m, all attributes off.
             */
            q_current_color =
                Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * Fall-through to ANSI fallback.
         */
        break;

    case SCAN_CSI_PARAM:
        save_char(from_modem, to_screen);
        /*
         * Following through on the SGR code, we are now looking only for a
         * digit, semicolon, or 'm'.
         */
        if ((q_isdigit(from_modem)) || (from_modem == ';')) {
            scan_state = SCAN_CSI_PARAM;
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        if (from_modem == 'm') {

            DLOG(("ANSI SGR: change text attributes\n"));

            /*
             * Text attributes
             */
            if (ansi_color(&attributes, &count) == Q_TRUE) {
                q_current_color = attributes;
            } else {
                break;
            }

            clear_state(to_screen);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /*
         * Fall-through to ANSI fallback.
         */
        break;

    /* ATASCII ------------------------------------------------------------- */

    case SCAN_NONE:
        /*
         * ESC
         */
        if (from_modem == C_ESC) {
            if ((q_status.atascii_color == Q_TRUE) ||
                (q_status.atascii_ansi_fallback == Q_TRUE)
            ) {
                /* Permit parsing of ANSI escape sequences. */
                save_char(from_modem, to_screen);
                scan_state = SCAN_ESC;
                return Q_EMUL_FSM_NO_CHAR_YET;
            }
        }

        if ((from_modem < 0x20) ||
            ((from_modem >= 0x80) && (from_modem < 0xA0))
        ) {
            /* This is a C0/C1 control character, process it there. */
            atascii_handle_control_char(from_modem);
            return Q_EMUL_FSM_NO_CHAR_YET;
        }

        /* This is a printable character, send it out. */
        *to_screen = atascii_chars[from_modem & 0x7F];
        return Q_EMUL_FSM_ONE_CHAR;

    } /* switch (scan_state) */

    if (q_status.atascii_ansi_fallback == Q_TRUE) {
        /*
         * Process through ANSI fallback code.
         *
         * This is UGLY AS HELL, but lots of BBSes assume that every emulator
         * will "fallback" to ANSI for sequences they don't understand.
         */
        scan_state = SCAN_ANSI_FALLBACK;
        DLOG(("ANSI FALLBACK BEGIN\n"));

        /*
         * From here on out we pass through ANSI until we don't get
         * Q_EMUL_FSM_NO_CHAR_YET.
         */
        memcpy(ansi_buffer, q_emul_buffer, q_emul_buffer_n);
        ansi_buffer_i = 0;
        ansi_buffer_n = q_emul_buffer_n;
        q_emul_buffer_i = 0;
        q_emul_buffer_n = 0;

        DLOG(("ANSI FALLBACK ansi()\n"));

        /*
         * Run through the emulator again
         */
        assert(ansi_buffer_n > 0);
        goto atascii_start;

    } else {

        DLOG(("Unknown sequence, and no ANSI fallback\n"));
        scan_state = DUMP_UNKNOWN_SEQUENCE;

        /*
         * This point means we got most, but not all, of a sequence.
         */
        *to_screen = codepage_map_char(q_emul_buffer[q_emul_buffer_i]);
        q_emul_buffer_i++;

        /*
         * Special case: one character returns Q_EMUL_FSM_ONE_CHAR.
         */
        if (q_emul_buffer_n == 1) {
            q_emul_buffer_i = 0;
            q_emul_buffer_n = 0;
            return Q_EMUL_FSM_ONE_CHAR;
        }

        /*
         * Tell the emulator layer that I need to be called many more times
         * to dump the string in q_emul_buffer.
         */
        return Q_EMUL_FSM_MANY_CHARS;
    }

    /*
     * Should never get here.
     */
    abort();
    return Q_EMUL_FSM_NO_CHAR_YET;
}

/**
 * Generate a sequence of bytes to send to the remote side that correspond to
 * a keystroke.
 *
 * @param keystroke one of the Q_KEY values, OR a Unicode code point.  See
 * input.h.
 * @return a wide string that is appropriate to send to the remote side.
 * Note that ANSI emulation is an 8-bit emulation: only the bottom 8 bits are
 * transmitted to the remote side.  See post_keystroke().
 */
wchar_t * atascii_keystroke(const int keystroke) {

    switch (keystroke) {

    case Q_KEY_ESCAPE:
        return L"\033";

    case Q_KEY_TAB:
        return L"\011";

    case Q_KEY_BACKSPACE:
        return L"\024";

    case Q_KEY_LEFT:
        return L"\235";

    case Q_KEY_RIGHT:
        return L"\035";

    case Q_KEY_UP:
        return L"\221";

    case Q_KEY_DOWN:
        return L"\021";

    case Q_KEY_PPAGE:
    case Q_KEY_NPAGE:
        return L"";
    case Q_KEY_IC:
        return L"\224";
    case Q_KEY_DC:
        return L"\024";
    case Q_KEY_SIC:
    case Q_KEY_SDC:
        return L"";
    case Q_KEY_HOME:
        return L"\023";
    case Q_KEY_END:
        return L"";
    case Q_KEY_F(1):
        return L"\205";
    case Q_KEY_F(2):
        return L"\211";
    case Q_KEY_F(3):
        return L"\206";
    case Q_KEY_F(4):
        return L"\212";
    case Q_KEY_F(5):
        return L"\207";
    case Q_KEY_F(6):
        return L"\213";
    case Q_KEY_F(7):
        return L"\210";
    case Q_KEY_F(8):
        return L"\214";
    case Q_KEY_F(9):
    case Q_KEY_F(10):
    case Q_KEY_F(11):
    case Q_KEY_F(12):
    case Q_KEY_F(13):
    case Q_KEY_F(14):
    case Q_KEY_F(15):
    case Q_KEY_F(16):
    case Q_KEY_F(17):
    case Q_KEY_F(18):
    case Q_KEY_F(19):
    case Q_KEY_F(20):
    case Q_KEY_F(21):
    case Q_KEY_F(22):
    case Q_KEY_F(23):
    case Q_KEY_F(24):
    case Q_KEY_F(25):
    case Q_KEY_F(26):
    case Q_KEY_F(27):
    case Q_KEY_F(28):
    case Q_KEY_F(29):
    case Q_KEY_F(30):
    case Q_KEY_F(31):
    case Q_KEY_F(32):
    case Q_KEY_F(33):
    case Q_KEY_F(34):
    case Q_KEY_F(35):
    case Q_KEY_F(36):
    case Q_KEY_PAD0:
    case Q_KEY_C1:
    case Q_KEY_PAD1:
    case Q_KEY_C2:
    case Q_KEY_PAD2:
    case Q_KEY_C3:
    case Q_KEY_PAD3:
    case Q_KEY_B1:
    case Q_KEY_PAD4:
    case Q_KEY_B2:
    case Q_KEY_PAD5:
    case Q_KEY_B3:
    case Q_KEY_PAD6:
    case Q_KEY_A1:
    case Q_KEY_PAD7:
    case Q_KEY_A2:
    case Q_KEY_PAD8:
    case Q_KEY_A3:
    case Q_KEY_PAD9:
    case Q_KEY_PAD_STOP:
    case Q_KEY_PAD_SLASH:
    case Q_KEY_PAD_STAR:
    case Q_KEY_PAD_MINUS:
    case Q_KEY_PAD_PLUS:
        return L"";

    case Q_KEY_PAD_ENTER:
    case Q_KEY_ENTER:
        return L"\015";

    default:
        break;

    }

    return NULL;
}
