/*
 * vt100.h
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

#ifndef __VT100_H__
#define __VT100_H__

/* Includes --------------------------------------------------------------- */

#include "emulation.h"

/* Defines ---------------------------------------------------------------- */

/* Globals ---------------------------------------------------------------- */

/**
 * Whether arrow keys send ANSI, VT100, or VT52 sequences.  Stored in
 * vt100.c.
 */
extern Q_EMULATION q_vt100_arrow_keys;

/**
 * Whether number pad keys send VT100 or VT52, application or numeric
 * sequences.  Stored in vt100.c.
 */
extern struct q_keypad_mode q_vt100_keypad_mode;

/**
 * When true, VT100 new line mode is set.  Exposed for keyboard.c so that
 * ENTER generates appropriate code.  Stored in vt100.c
 */
extern Q_BOOL q_vt100_new_line_mode;

/* Functions -------------------------------------------------------------- */

/**
 * Push one byte through the VT100, VT102, or VT220 emulator.
 *
 * @param from_modem one byte from the remote side.
 * @param to_screen if the return is Q_EMUL_FSM_ONE_CHAR or
 * Q_EMUL_FSM_MANY_CHARS, then to_screen will have a character to display on
 * the screen.
 * @return one of the Q_EMULATION_STATUS constants.  See emulation.h.
 */
extern Q_EMULATION_STATUS vt100(const unsigned char from_modem,
                                wchar_t * to_screen);

/**
 * Reset the emulation state.
 */
extern void vt100_reset();

/**
 * Generate a sequence of bytes to send to the remote side that correspond to
 * a keystroke.
 *
 * @param keystroke one of the Q_KEY values, OR a Unicode code point.  See
 * input.h.
 * @return a wide string that is appropriate to send to the remote side.
 * Note that these emulations are 7-bit and 8-bit: only the bottom 7/8 bits
 * are transmitted to the remote side.  See post_keystroke().
 */
extern wchar_t * vt100_keystroke(const int keystroke);

#endif /* __VT100_H__ */
