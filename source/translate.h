/*
 * translate.h
 *
 * qodem - Qodem Terminal Emulator
 *
 * Written 2003-2016 by Kevin Lamonte
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

#ifdef __cplusplus
extern "C" {
#endif

/* Defines ---------------------------------------------------------------- */

/**
 * Type of translate table.
 */
typedef enum {
    Q_TRANSLATE_TABLE_INPUT,    /* Input table */
    Q_TRANSLATE_TABLE_OUTPUT    /* Output table */
} Q_TRANSLATE_TABLE_TYPE;

/**
 * An 8-bit translation table.
 */
struct q_translate_table_8bit_struct {
    Q_TRANSLATE_TABLE_TYPE type;
    unsigned char map_to[256];
};

/**
 * A <wchar_t, wchar_t> tuple.
 */
struct q_wchar_tuple {
    wchar_t key;
    wchar_t value;
};

/**
 * A Unicode translation table.  This is currently a very stupid list of
 * tuples.
 */
struct q_translate_table_unicode_struct {
    Q_TRANSLATE_TABLE_TYPE type;
    struct q_wchar_tuple * mappings;
    size_t mappings_n;
};

/* Globals ---------------------------------------------------------------- */

/**
 * The input translation table.
 */
extern struct q_translate_table_8bit_struct q_translate_table_input;

/**
 * The output translation table.
 */
extern struct q_translate_table_8bit_struct q_translate_table_output;

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
 * Initialize the global translate pairs to do nothing.
 */
extern void initialize_translate_tables();

/**
 * Load an 8-bit translate table pair from a file into the global translate
 * table structs.
 *
 * @param filename the basename of a file in the data directory to read from
 */
extern void load_translate_table_8bit(const char * filename);

/**
 * Load a Unicode translate table pair from a file into the global translate
 * table structs.
 *
 * @param filename the basename of a file in the data directory to read from
 */
extern void load_translate_table_unicode(const char * filename);

#ifdef __cplusplus
}
#endif

#endif /* __TRANSLATE_H__ */
