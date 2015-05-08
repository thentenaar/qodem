/*
 * linux.h
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

/*
 * Why a completely separate duplicate mess of code for Linux console?  Well,
 * because Linux console has quite a few "private" codes, plus it supports a
 * number of ECMA-48 sequences that VT100 does not.  I'd like to keep the
 * serious divergences from actual VTxxx processing separated right now.
 *
 * On the other hand, it's very likely Linux console will NEVER become the
 * kind of legacy console where I'll need to support the undocumented
 * behavior.  By this I mean that I don't expect to encounter any host
 * applications that have coded to a specific Linux console escape sequence.
 */
#ifndef __LINUX_H__
#define __LINUX_H__

/* Includes --------------------------------------------------------------- */

#include "emulation.h"

/* Defines ---------------------------------------------------------------- */

/* Globals ---------------------------------------------------------------- */

/**
 * Whether arrow keys send ANSI, VT100, or VT52 sequences.  Stored in
 * linux.c.
 */
extern Q_EMULATION q_linux_arrow_keys;

/**
 * Whether number pad keys send VT100 or VT52, application or numeric
 * sequences.  Stored in linux.c.
 */
extern struct q_keypad_mode q_linux_keypad_mode;

/**
 * When true, VT100 new line mode is set.  Exposed for keyboard.c so that
 * ENTER generates appropriate code.  Stored in linux.c.
 */
extern Q_BOOL q_linux_new_line_mode;

/**
 * The bell frequency in Hz set by ESC [ 10 ; n ] .  Used by qodem_beep().
 * Stored in linux.c.
 */
extern int q_linux_beep_frequency;

/**
 * The bell duration in milliseconds set by ESC [ 10 ; n ] .  Used by
 * qodem_beep().  Stored in linux.c.
 */
extern int q_linux_beep_duration;

/**
 * The available mouse tracking protocols.  See handle_mouse() in input.c.
 */
typedef enum {
    XTERM_MOUSE_OFF,
    XTERM_MOUSE_X10,
    XTERM_MOUSE_NORMAL,
    XTERM_MOUSE_BUTTONEVENT,
    XTERM_MOUSE_ANYEVENT
} XTERM_MOUSE_PROTOCOL;

/**
 * The available mouse tracking encodings.  See handle_mouse() in input.c.
 */
typedef enum {
    XTERM_MOUSE_ENCODING_X10,
    XTERM_MOUSE_ENCODING_UTF8
} XTERM_MOUSE_ENCODING;

/**
 * The current mouse tracking protocol.  See handle_mouse() in input.c.
 */
extern XTERM_MOUSE_PROTOCOL q_xterm_mouse_protocol;

/**
 * The current mouse tracking encoding.  See handle_mouse() in input.c.
 */
extern XTERM_MOUSE_ENCODING q_xterm_mouse_encoding;

/* Functions -------------------------------------------------------------- */

/**
 * Push one byte through the LINUX, L_UTF8, XTERM, or X_UTF8 emulator.
 *
 * @param from_modem one byte from the remote side.
 * @param to_screen if the return is Q_EMUL_FSM_ONE_CHAR or
 * Q_EMUL_FSM_MANY_CHARS, then to_screen will have a character to display on
 * the screen.
 * @return one of the Q_EMULATION_STATUS constants.  See emulation.h.
 */
extern Q_EMULATION_STATUS linux_emulator(const unsigned char from_modem,
                                         wchar_t * to_screen);

/**
 * Reset the emulation state.
 */
extern void linux_reset();

/**
 * Generate a sequence of bytes to send to the remote side that correspond to
 * a keystroke.  Used by LINUX and L_UTF8.
 *
 * @param keystroke one of the Q_KEY values, OR a Unicode code point.  See
 * input.h.
 * @return a wide string that is appropriate to send to the remote side.
 * Note that LINUX emulation is an 8-bit emulation: only the bottom 8 bits
 * are transmitted to the remote side.  L_UTF8 emulation sends a true Unicode
 * sequence.  See post_keystroke().
 */
extern wchar_t * linux_keystroke(const int keystroke);

/**
 * Generate a sequence of bytes to send to the remote side that correspond to
 * a keystroke.  Used by XTERM and X_UTF8.
 *
 * @param keystroke one of the Q_KEY values, OR a Unicode code point.  See
 * input.h.
 * @return a wide string that is appropriate to send to the remote side.
 * Note that XTERM emulation is an 8-bit emulation: only the bottom 8 bits
 * are transmitted to the remote side.  X_UTF8 emulation sends a true Unicode
 * sequence.  See post_keystroke().
 */
extern wchar_t * xterm_keystroke(const int keystroke);

#endif /* __LINUX_H__ */
