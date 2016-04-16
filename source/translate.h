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

/* Globals ---------------------------------------------------------------- */

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
extern void translate_table_editor_8bit_keyboard_handler(const int keystroke,
                                                    const int flags);

/**
 * Draw screen for the Alt-A translation table editor screen.
 */
extern void translate_table_editor_8bit_refresh();

/**
 * Keyboard handler for the Alt-A translation table editor screen.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
extern void translate_table_editor_unicode_keyboard_handler(const int keystroke,
                                                            const int flags);

/**
 * Draw screen for the Alt-A translation table editor screen.
 */
extern void translate_table_editor_unicode_refresh();

/**
 * Loads the default translate table pairs.
 */
extern void initialize_translate_tables();

/**
 * Load an 8-bit translate table pair from a file and begin using it for
 * translate_8bit().
 *
 * @param filename the basename of a file in the data directory to read from
 */
extern void use_translate_table_8bit(const char * filename);

/**
 * Load a Unicode translate table pair from a file and begin using it for
 * translate_unicode().
 *
 * @param filename the basename of a file in the data directory to read from
 */
extern void use_translate_table_unicode(const char * filename);

/**
 * Translate an 8-bit byte using the tables read via
 * use_translate_table_8bit().
 *
 * @param in the byte to translate
 * @return the translated byte
 */
extern unsigned char translate_8bit(const unsigned char in);

/**
 * Translate a Unicode code point using the tables read via
 * use_translate_table_unicode().
 *
 * @param in the code point to translate
 * @return the translated code point
 */
extern unsigned char translate_unicode(const wchar_t in);

#ifdef __cplusplus
}
#endif

#endif /* __TRANSLATE_H__ */
