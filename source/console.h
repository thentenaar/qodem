/*
 * console.h
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

#ifndef __CONSOLE_H__
#define __CONSOLE_H__

/* Includes --------------------------------------------------------------- */

#include "common.h"

/* Defines ---------------------------------------------------------------- */

/* Globals ---------------------------------------------------------------- */

/**
 * Split screen dirty flag.  Stored in console.c.
 */
extern Q_BOOL q_split_screen_dirty;

/**
 * A flag to indicate a data flood on the console.  We need to not permit
 * download protocol autostarts during a flood.
 */
extern Q_BOOL q_console_flood;

/* Functions -------------------------------------------------------------- */

/**
 * Keyboard handler for the normal console.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
extern void console_keyboard_handler(int keystroke, int flags);

/**
 * Keyboard handler for the script quicklearn state.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
extern void console_quicklearn_keyboard_handler(int keystroke, int flags);

/**
 * Keyboard handler for the Alt-Z console menu dialog.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
extern void console_menu_keyboard_handler(const int keystroke, const int flags);

/**
 * Keyboard handler for the Alt-I information display dialog.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
extern void console_info_keyboard_handler(const int keystroke, const int flags);

/**
 * Draw screen for the normal console.
 *
 * @param status_line if true, draw the status line with online/offline,
 * codepage, etc.
 */
extern void console_refresh(Q_BOOL status_line);

/**
 * Draw screen for the Alt-Z console menu dialog.
 */
extern void console_menu_refresh();

/**
 * Draw screen for the Alt-I information display dialog.
 */
extern void console_info_refresh();

/**
 * Process raw bytes from the remote side through the emulation layer,
 * handling zmodem/kermit autostart, translation tables, etc.
 *
 * @param buffer the bytes from the remote side
 * @param n the number of bytes in buffer
 * @param remaining the number of un-processed bytes that should be sent
 * through a future invocation of console_process_incoming_data
 */
extern void console_process_incoming_data(unsigned char * buffer, const int n,
                                          int * remaining);
/**
 * Begin capturing the session to file.
 *
 * @param filename the file to save data to
 */
extern void start_capture(const char * filename);

/**
 * Stop capturing and close the capture file.
 */
extern void stop_capture();

/**
 * Begin logging major events for the session to file.
 *
 * @param filename the file to save the log to
 */
extern void start_logging(const char * filename);

/**
 * Stop logging and close the log file.
 */
extern void stop_logging();

/**
 * Begin saving prompts and responses to a Perl language script file.
 *
 * @param filename the file to save the script to
 */
extern void start_quicklearn(const char * filename);

/**
 * Stop the quicklearn and close the script file.
 */
extern void stop_quicklearn();

/**
 * Echo a character normally printed to the screen as script input (stdin) in
 * the quicklearn script.
 *
 * @param ch the character
 */
extern void quicklearn_print_character(const wchar_t ch);

/**
 * Save a byte normally sent to the remote side as script output (stdout) in
 * the quicklearn'd script.
 *
 * @param ch the byte
 */
extern void quicklearn_send_byte(const unsigned char ch);

/**
 * Convert the Q_OPTION_DOORWAY_MIXED_KEYS string in the options file to a
 * table of command keys that will be honored in doorway MIXED mode.
 */
extern void setup_doorway_handling();

/**
 * Enable or disable the Alt-Minus show status line flag.
 */
extern void set_status_line(Q_BOOL make_visible);

#endif /* __CONSOLE_H__ */
