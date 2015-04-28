/*
 * vt52.h
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

#ifndef __VT52_H__
#define __VT52_H__

/* Includes --------------------------------------------------------------- */

#include "emulation.h"

/* Defines ---------------------------------------------------------------- */

/* Globals ---------------------------------------------------------------- */

/* True means alternate keypad mode, false means numeric keypad mode.
   Used by keyboard.c, stored in vt52.c */
extern Q_BOOL q_vt52_alternate_keypad_mode;

/* Functions -------------------------------------------------------------- */

extern Q_EMULATION_STATUS vt52(const unsigned char from_modem, wchar_t * to_screen);
extern void vt52_reset();
extern wchar_t * vt52_keystroke(const int keystroke);

#endif /* __VT52_H__ */
