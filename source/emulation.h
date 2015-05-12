/*
 * emulation.h
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

#ifndef __EMULATION_H__
#define __EMULATION_H__

/* Includes --------------------------------------------------------------- */

#include "codepage.h"

/* Defines ---------------------------------------------------------------- */

/**
 * Available terminal emulations.
 */
typedef enum Q_EMULATIONS {
    Q_EMUL_TTY,                 /* TTY */
    Q_EMUL_ANSI,                /* ANSI */
    Q_EMUL_VT52,                /* VT52 */
    Q_EMUL_VT100,               /* VT100 */
    Q_EMUL_VT102,               /* VT102 */
    Q_EMUL_VT220,               /* VT220 */
    Q_EMUL_AVATAR,              /* Avatar */
    Q_EMUL_DEBUG,               /* DEBUG */
    Q_EMUL_LINUX,               /* Linux console */
    Q_EMUL_LINUX_UTF8,          /* Linux console (UTF-8) */
    Q_EMUL_XTERM,               /* Xterm */
    Q_EMUL_XTERM_UTF8           /* Xterm (UTF-8) */
} Q_EMULATION;
#define Q_EMULATION_MAX (Q_EMUL_XTERM_UTF8 + 1)

/**
 * The available return values from terminal_emulator().
 */
typedef enum Q_EMULATION_STATUSS {
    Q_EMUL_FSM_NO_CHAR_YET,     /* Need more data */
    Q_EMUL_FSM_ONE_CHAR,        /* One screen character is ready */
    Q_EMUL_FSM_MANY_CHARS,      /* More screen characters are ready */
    Q_EMUL_FSM_REPEAT_STATE     /* Repeat the entire state */
} Q_EMULATION_STATUS;

/**
 * VT100-style emulations have several things they can return from keypad
 * keystrokes.  This is the application vs numeric mode flag.
 */
typedef enum Q_KEYPAD_MODES {
    Q_KEYPAD_MODE_APPLICATION,  /* Application mode */
    Q_KEYPAD_MODE_NUMERIC       /* Numeric mode */
} Q_KEYPAD_MODE;

/**
 * VT100-style emulations have several things they can return from keypad
 * keystrokes.  This has the application/numeric mode flag and the "ANSI"
 * (meaning ECMA-48 or VT100) vs VT52 flag.
 */
struct q_keypad_mode {
    Q_EMULATION emulation;
    Q_KEYPAD_MODE keypad_mode;
};

/* Globals ---------------------------------------------------------------- */

/**
 * The total number of bytes received on this connection.
 */
extern unsigned long q_connection_bytes_received;

/**
 * Local buffer for multiple returned characters.
 */
extern unsigned char q_emul_buffer[128];
extern int q_emul_buffer_n;
extern int q_emul_buffer_i;

/**
 * Some emulations need to wrap at special places.
 */
extern int q_emulation_right_margin;

/**
 * Avatar has a special command that requires the entire state machine be
 * re-run.  It is the responsibility of an emulation to set these two
 * variables and then return Q_EMUL_FSM_REPEAT_STATE.  terminal_emulator()
 * will free it afterwards.
 */
extern unsigned char * q_emul_repeat_state_buffer;
extern int q_emul_repeat_state_count;

/* Functions -------------------------------------------------------------- */

/**
 * Reset the emulation state.
 */
extern void reset_emulation();

/**
 * All the emulations use the same top-level function.  Q_EMULATION_STATE is
 * returned.
 *
 * If Q_EMUL_FSM_NO_CHAR_YET, then to_screen contains the number of
 * characters that can be safely discarded from the data stream, usually 1.
 *
 * If Q_EMUL_FSM_ONE_CHAR, then to_screen contains one character that can be
 * rendered.
 *
 * If Q_EMUL_FSM_MANY_CHARS, then to_screen contains one character that can
 * be rendered, AND more characters are ready.  Continue calling
 * terminal_emulator() until Q_EMUL_FSM_NO_CHAR_YET is returned.
 *
 * The emulator is expected to modify the following globals:
 *        q_current_color
 *        q_status.cursor_x
 *        q_status.cursor_y
 *        q_status.scroll_region_top
 *        q_status.scroll_region_bottom
 *
 * Also the emulator may modify data in the scrollback buffer.
 *
 * @param from_modem one byte from the remote side.
 * @param to_screen if the return is Q_EMUL_FSM_ONE_CHAR or
 * Q_EMUL_FSM_MANY_CHARS, then to_screen will have a character to display on
 * the screen.
 * @return one of the Q_EMULATION_STATUS constants.
 */
extern Q_EMULATION_STATUS terminal_emulator(const unsigned char from_modem,
                                            wchar_t * to_screen);

/**
 * Return a string for a Q_EMULATION enum.
 *
 * @param emulation Q_EMUL_TTY etc.
 * @return "TTY" etc.
 */
extern const char * emulation_string(const Q_EMULATION emulation);

/**
 * Given an emulation string, return a Q_EMULATION enum.
 *
 * @param string "TTY", "VT100", etc.  Note string is case-sensitive.
 * @return Q_EMUL_TTY, Q_EMUL_VT100, etc.
 */
extern Q_EMULATION emulation_from_string(const char * string);

/**
 * Process a control character.  This is used by ANSI, AVATAR, and TTY.
 *
 * @param control_char a byte in the C0 or C1 range.
 */
extern void generic_handle_control_char(const unsigned char control_char);

/**
 * Keyboard handler for the emulation selection dialog.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
extern void emulation_menu_keyboard_handler(const int keystroke,
                                            const int flags);

/**
 * Draw screen for the emulation selection dialog.
 */
extern void emulation_menu_refresh();

/**
 * Get the default 8-bit codepage for an emulation.  This is usually CP437 or
 * DEC.
 *
 * @param emulation Q_EMUL_TTY, Q_EMUL_ANSI, etc.
 * @return the codepage
 */
extern Q_CODEPAGE default_codepage(Q_EMULATION emulation);

/**
 * Get the appropriate TERM environment variable value for an emulation.
 *
 * @param emulation the emulation
 * @return "ansi", "xterm", etc.
 */
extern const char * emulation_term(Q_EMULATION emulation);

/**
 * Get the appropriate LANG environment variable value for an emulation.
 *
 * @param emulation the emulation
 * @return "en", "en_US", etc.
 */
extern const char * emulation_lang(Q_EMULATION emulation);

/**
 * Echo local transmitted bytes to DEBUG emulation the hex display in a
 * distinct color.
 *
 * @param ch the byte that was sent to the remote side
 */
extern void debug_local_echo(const unsigned char ch);

#endif /* __EMULATION_H__ */
