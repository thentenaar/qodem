/*
 * translate.h
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

#ifndef __TRANSLATE_H__
#define __TRANSLATE_H__

/* Includes --------------------------------------------------------------- */

/* Defines ---------------------------------------------------------------- */

/**
 * Type of translate table.
 */
typedef enum {
    Q_TRANSLATE_TABLE_INPUT,    /* Input table */
    Q_TRANSLATE_TABLE_OUTPUT    /* Output table */
} Q_TRANSLATE_TABLE_TYPE;

/**
 * An ASCII translation table.
 */
struct q_translate_table_struct {
    Q_TRANSLATE_TABLE_TYPE type;
    unsigned char map_to[256];
};

/* Globals ---------------------------------------------------------------- */

/**
 * The input translation table.
 */
extern struct q_translate_table_struct q_translate_table_input;

/**
 * The output translation table.
 */
extern struct q_translate_table_struct q_translate_table_output;

/* Functions -------------------------------------------------------------- */

/**
 * Keyboard handler for the Alt-A translation table selection dialog.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
extern void translate_table_menu_keyboard_handler(const int keystroke,
                                                  const int flags);

/**
 * Draw screen for the Alt-A translation table selection dialog.
 */
extern void translate_table_menu_refresh();

/**
 * Keyboard handler for the Alt-A translation table editor screen.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
extern void translate_table_editor_keyboard_handler(const int keystroke,
                                                    const int flags);

/**
 * Draw screen for the Alt-A translation table editor screen.
 */
extern void translate_table_editor_refresh();

/**
 * Create the config file for the translate tables (translate.tbl).
 */
extern void create_translate_table_file();

/**
 * This must be called to initialize the translate tables from the config
 * file.
 */
extern void load_translate_tables();

#endif /* __TRANSLATE_H__ */
