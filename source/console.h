/*
 * console.h
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

#ifndef __CONSOLE_H__
#define __CONSOLE_H__

/* Includes --------------------------------------------------------------- */

#include "common.h"

/* Defines ---------------------------------------------------------------- */

/* Globals ---------------------------------------------------------------- */

/* Split screen dirty flag, stored in console.c */
extern Q_BOOL q_split_screen_dirty;

/* A flag to indicate a data flood on the console.  We need */
extern Q_BOOL q_console_flood;

/* Functions -------------------------------------------------------------- */

extern void console_keyboard_handler(int keystroke, int flags);
extern void console_quicklearn_keyboard_handler(int keystroke, int flags);
extern void console_menu_keyboard_handler(const int keystroke, const int flags);
extern void console_info_keyboard_handler(const int keystroke, const int flags);
extern void console_refresh(Q_BOOL status_line);
extern void console_menu_refresh();
extern void console_info_refresh();
extern void console_process_incoming_data(unsigned char * buffer, const int n, int * remaining);
extern void start_capture(const char * filename);
extern void stop_capture();
extern void start_logging(const char * filename);
extern void stop_logging();
extern void start_quicklearn(const char * filename);
extern void stop_quicklearn();
extern void quicklearn_print_character(const wchar_t ch);
extern void quicklearn_send_byte(const unsigned char ch);
extern void setup_doorway_handling();
extern void set_status_line(Q_BOOL make_visible);

#endif /* __CONSOLE_H__ */
