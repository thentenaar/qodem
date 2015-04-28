/*
 * linux.h
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
 * Why a completely separate duplicate mess of code for Linux console?
 * Well, because Linux console has quite a few "private" codes, plus
 * it supports a number of ECMA-48 sequences that VT100 does not.  I'd
 * like to keep the serious divergences from actual VTxxx processing
 * separated right now.
 *
 * On the other hand, it's very likely Linux console will NEVER become
 * the kind of legacy console where I'll need to support the
 * undocumented behavior.  By this I mean that I don't expect to
 * encounter any host applications that have coded to a specific Linux
 * console escape sequence.
 */
#ifndef __LINUX_H__
#define __LINUX_H__

/* Includes --------------------------------------------------------------- */

#include "emulation.h"

/* Defines ---------------------------------------------------------------- */

/* Globals ---------------------------------------------------------------- */

/* Whether arrow keys send ANSI, VT100, or VT52 sequences, stored in linux.c */
extern Q_EMULATION q_linux_arrow_keys;

/* Whether number pad keys send VT100 or VT52, application or numeric sequences.
   Stored in linux.c */
extern struct q_keypad_mode q_linux_keypad_mode;

/* When true, VT100 new line mode is set, stored in linux.c
   Exposed for keyboard.c so that ENTER generates appropriate code. */
extern Q_BOOL q_linux_new_line_mode;

/* Used by qodem_beep() */
extern int q_linux_beep_frequency;
extern int q_linux_beep_duration;

typedef enum {
        XTERM_MOUSE_OFF,
        XTERM_MOUSE_X10,
        XTERM_MOUSE_NORMAL,
        XTERM_MOUSE_BUTTONEVENT,
        XTERM_MOUSE_ANYEVENT
} XTERM_MOUSE_PROTOCOL;

typedef enum {
        XTERM_MOUSE_ENCODING_X10,
        XTERM_MOUSE_ENCODING_UTF8
} XTERM_MOUSE_ENCODING;

/* Use by handle_mouse() */
extern XTERM_MOUSE_PROTOCOL q_xterm_mouse_protocol;
extern XTERM_MOUSE_ENCODING q_xterm_mouse_encoding;

/* Functions -------------------------------------------------------------- */

/* Can't call the function 'linux' because that keyword is used by gcc. */
extern Q_EMULATION_STATUS linux_emulator(const unsigned char from_modem, wchar_t * to_screen);
extern void linux_reset();
extern wchar_t * linux_keystroke(const int keystroke);
extern wchar_t * xterm_keystroke(const int keystroke);

#endif /* __LINUX_H__ */
