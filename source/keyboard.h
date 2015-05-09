/*
 * keyboard.h
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

#ifndef __KEYBOARD_H__
#define __KEYBOARD_H__

/* Includes --------------------------------------------------------------- */

/* Defines ---------------------------------------------------------------- */

/* Maximum length of a keyboard macro */
#define KEYBOARD_MACRO_SIZE 128

/* Globals ---------------------------------------------------------------- */

/* Functions -------------------------------------------------------------- */

/**
 * This must be called to initialize the various keyboards from the config
 * files.
 */
extern void initialize_keyboard();

/**
 * Send a local keystroke to the remote side.
 *
 * @param keystroke the keypress
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
extern void post_keystroke(const int keystroke, const int flags);

/**
 * Keyboard handler for the Alt-J function key editor screen.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
extern void function_key_editor_keyboard_handler(const int keystroke,
                                                 const int flags);
/**
 * Draw screen for the Alt-J function key editor screen.
 */
extern void function_key_editor_refresh();

/**
 * Load keybindings from a filename into the currently-bound keyboard.
 *
 * @param filename name of file with key bindings
 */
extern void switch_current_keyboard(const char * filename);

/**
 * Create the config files for the keybindings (default.key, ansi.key,
 * vt100.key, etc.)
 */
extern void create_keybindings_files();

#endif /* __KEYBOARD_H__ */
