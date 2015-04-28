/*
 * screensaver.h
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

#ifndef __SCREENSAVER_H__
#define __SCREENSAVER_H__

/* Includes --------------------------------------------------------------- */

#include "states.h"

/* Defines ---------------------------------------------------------------- */

/* Globals ---------------------------------------------------------------- */

/* State we were in before the screensaver was activated, stored in screensaver.c */
extern Q_PROGRAM_STATE original_state;

/* Functions -------------------------------------------------------------- */

extern void screensaver_keyboard_handler(const int keystroke, const int flags);
extern void screensaver_refresh();

#endif /* __SCREENSAVER_H__ */
