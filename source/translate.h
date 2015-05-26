/*
 * translate.h
 *
 * qodem - Qodem Terminal Emulator
 *
 * Written 2003-2015 by Kevin Lamonte
 *
 * To the extent possible under law, the author(s) have dedicated all
 * copyright and related and neighboring rights to this software to the
 * public domain worldwide. This software is distributed without any
 * warranty.
 *
 * You should have received a copy of the CC0 Public Domain Dedication along
 * with this software. If not, see
 * <http://creativecommons.org/publicdomain/zero/1.0/>.
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
