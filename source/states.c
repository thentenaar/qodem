/*
 * states.c
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

#include "qcurses.h"
#include "common.h"

#include <assert.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include "qodem.h"
#include "console.h"
#include "script.h"
#include "protocols.h"
#include "keyboard.h"
#include "translate.h"
#include "screen.h"
#include "options.h"
#include "states.h"

/* Global program state */
Q_PROGRAM_STATE q_program_state;

/*
 * Whether or not the keyboard is supposed to be blocking
 * (last argument to nodelay())
 */
Q_BOOL q_keyboard_blocks;

/*
 * keyboard_handler - all keystrokes come through here first.  I like
 *                    seeing ALT and CTRL as flags rather than tty characters.
 */
void keyboard_handler() {

        int flags = 0;                  /* passed to *_keyboard_handler() */
        int keystroke;                  /* " */

        /* Grab keystroke */
        if (q_keyboard_blocks == Q_TRUE) {
                qodem_getch(&keystroke, &flags, Q_KEYBOARD_DELAY);
        } else {
                qodem_getch(&keystroke, &flags, 0);
        }

        if (keystroke == Q_ERR) {
                /* No data, return */
                return;
        }

        switch (q_program_state) {

        case Q_STATE_CONSOLE:
                if (q_status.quicklearn == Q_FALSE) {
                        console_keyboard_handler(keystroke, flags);
                } else {
                        console_quicklearn_keyboard_handler(keystroke, flags);
                }
                break;
        case Q_STATE_CONSOLE_MENU:
                console_menu_keyboard_handler(keystroke, flags);
                break;
        case Q_STATE_INFO:
                console_info_keyboard_handler(keystroke, flags);
                break;
        case Q_STATE_SCROLLBACK:
                scrollback_keyboard_handler(keystroke, flags);
                break;
        case Q_STATE_SCRIPT_EXECUTE:
                script_keyboard_handler(keystroke, flags);
                break;
        case Q_STATE_HOST:
                host_keyboard_handler(keystroke, flags);
                break;

        case Q_STATE_DOWNLOAD_MENU:
        case Q_STATE_UPLOAD_MENU:
                protocol_menu_keyboard_handler(keystroke, flags);
                break;
        case Q_STATE_DOWNLOAD_PATHDIALOG:
        case Q_STATE_UPLOAD_PATHDIALOG:
        case Q_STATE_UPLOAD_BATCH_DIALOG:
                protocol_pathdialog_keyboard_handler(keystroke, flags);
                break;
        case Q_STATE_UPLOAD:
        case Q_STATE_UPLOAD_BATCH:
        case Q_STATE_DOWNLOAD:
                protocol_transfer_keyboard_handler(keystroke, flags);
                break;

        case Q_STATE_EMULATION_MENU:
                emulation_menu_keyboard_handler(keystroke, flags);
                break;

        case Q_STATE_TRANSLATE_MENU:
                translate_table_menu_keyboard_handler(keystroke, flags);
                break;

        case Q_STATE_TRANSLATE_EDITOR:
                translate_table_editor_keyboard_handler(keystroke, flags);
                break;

        case Q_STATE_PHONEBOOK:
                phonebook_keyboard_handler(keystroke, flags);
                break;

        case Q_STATE_DIALER:
                dialer_keyboard_handler(keystroke, flags);
                break;

        case Q_STATE_SCREENSAVER:
                screensaver_keyboard_handler(keystroke, flags);
                break;

        case Q_STATE_FUNCTION_KEY_EDITOR:
                function_key_editor_keyboard_handler(keystroke, flags);
                break;

#ifndef Q_NO_SERIAL
        case Q_STATE_MODEM_CONFIG:
                modem_config_keyboard_handler(keystroke, flags);
                break;
#endif /* Q_NO_SERIAL */

        case Q_STATE_CODEPAGE:
                codepage_keyboard_handler(keystroke, flags);
                break;

        case Q_STATE_INITIALIZATION:
        case Q_STATE_EXIT:
                /* Program BUG */
                assert(1 == 0);
        }
} /* ---------------------------------------------------------------------- */

/*
 * refresh_handler
 */
void refresh_handler() {
#ifdef Q_PDCURSES_WIN32
        static long last_time = 1000000;
#else
        static suseconds_t last_time = 1000000;
#endif /* Q_PDCURSES_WIN32 */
        struct timeval tv;

        switch (q_program_state) {

        case Q_STATE_CONSOLE:
                /* Only update the console 8 times a second */
                gettimeofday(&tv, NULL);
                if ((tv.tv_usec < last_time) || (tv.tv_usec - last_time > 125000)) {
                        console_refresh(Q_TRUE);
                        last_time = tv.tv_usec;
                }
                break;
        case Q_STATE_SCRIPT_EXECUTE:
                /* Only update the console 8 times a second */
                gettimeofday(&tv, NULL);
                if ((tv.tv_usec < last_time) || (tv.tv_usec - last_time > 125000)) {
                        script_refresh();
                        last_time = tv.tv_usec;
                }
                break;
        case Q_STATE_HOST:
                /* Only update the console 8 times a second */
                gettimeofday(&tv, NULL);
                if ((tv.tv_usec < last_time) || (tv.tv_usec - last_time > 125000)) {
                        host_refresh();
                        last_time = tv.tv_usec;
                }
                break;
        case Q_STATE_CONSOLE_MENU:
                console_menu_refresh();
                break;
        case Q_STATE_INFO:
                console_info_refresh();
                break;
        case Q_STATE_SCROLLBACK:
                scrollback_refresh();
                break;

        case Q_STATE_DOWNLOAD_MENU:
        case Q_STATE_UPLOAD_MENU:
                protocol_menu_refresh();
                break;
        case Q_STATE_DOWNLOAD_PATHDIALOG:
        case Q_STATE_UPLOAD_PATHDIALOG:
        case Q_STATE_UPLOAD_BATCH_DIALOG:
                protocol_pathdialog_refresh();
                break;
        case Q_STATE_UPLOAD:
        case Q_STATE_UPLOAD_BATCH:
        case Q_STATE_DOWNLOAD:
                protocol_transfer_refresh();
                break;

        case Q_STATE_EMULATION_MENU:
                emulation_menu_refresh();
                break;

        case Q_STATE_TRANSLATE_MENU:
                translate_table_menu_refresh();
                break;

        case Q_STATE_TRANSLATE_EDITOR:
                translate_table_editor_refresh();
                break;

        case Q_STATE_PHONEBOOK:
        case Q_STATE_DIALER:
                phonebook_refresh();
                break;

        case Q_STATE_SCREENSAVER:
                screensaver_refresh();
                break;

        case Q_STATE_FUNCTION_KEY_EDITOR:
                function_key_editor_refresh();
                break;

#ifndef Q_NO_SERIAL
        case Q_STATE_MODEM_CONFIG:
                modem_config_refresh();
                break;
#endif /* Q_NO_SERIAL */

        case Q_STATE_CODEPAGE:
                codepage_refresh();
                break;

        case Q_STATE_INITIALIZATION:
        case Q_STATE_EXIT:
                /* Program BUG */
                assert(1 == 0);

        }
} /* ---------------------------------------------------------------------- */

/*
 * switch_state - switch to a new program state
 */
void switch_state(const Q_PROGRAM_STATE new_state) {

        switch (new_state) {

        case Q_STATE_DOWNLOAD_MENU:
        case Q_STATE_UPLOAD_MENU:
        case Q_STATE_DOWNLOAD_PATHDIALOG:
        case Q_STATE_UPLOAD_PATHDIALOG:
        case Q_STATE_EMULATION_MENU:
        case Q_STATE_TRANSLATE_MENU:
        case Q_STATE_INITIALIZATION:
        case Q_STATE_UPLOAD_BATCH_DIALOG:
        case Q_STATE_CODEPAGE:
                set_blocking_input(Q_TRUE);
                q_keyboard_blocks = Q_TRUE;
                q_screen_dirty = Q_TRUE;
                q_cursor_on();
                break;

        case Q_STATE_SCROLLBACK:
                if (q_scrollback_search_string != NULL) {
                        Xfree(q_scrollback_search_string, __FILE__, __LINE__);
                        q_scrollback_search_string = NULL;
                }
                q_scrollback_highlight_search_string = Q_FALSE;
                /* Fall through... */
        case Q_STATE_DIALER:
        case Q_STATE_UPLOAD:
        case Q_STATE_UPLOAD_BATCH:
        case Q_STATE_DOWNLOAD:
        case Q_STATE_CONSOLE_MENU:
        case Q_STATE_INFO:
        case Q_STATE_SCRIPT_EXECUTE:
                set_blocking_input(Q_FALSE);
                q_keyboard_blocks = Q_FALSE;
                q_screen_dirty = Q_TRUE;
                q_cursor_off();
                original_state = q_program_state;
                break;

        case Q_STATE_HOST:
                set_blocking_input(Q_FALSE);
                q_keyboard_blocks = Q_FALSE;
                q_screen_dirty = Q_TRUE;
                q_cursor_on();
                break;

        case Q_STATE_CONSOLE:
                set_blocking_input(Q_FALSE);
                q_keyboard_blocks = Q_FALSE;
                screen_clear();
                q_screen_dirty = Q_TRUE;
                if (q_status.split_screen == Q_TRUE) {
                        q_split_screen_dirty = Q_TRUE;
                }
                if (((q_status.emulation == Q_EMUL_LINUX) || (q_status.emulation == Q_EMUL_LINUX_UTF8) || (q_status.emulation == Q_EMUL_VT220)) && (q_status.visible_cursor == Q_FALSE)) {
                        q_cursor_off();
                } else {
                        q_cursor_on();
                }
                break;

        case Q_STATE_FUNCTION_KEY_EDITOR:
                /* Fall through ... */
                original_state = q_program_state;
#ifndef Q_NO_SERIAL
        case Q_STATE_MODEM_CONFIG:
#endif /* Q_NO_SERIAL */
        case Q_STATE_PHONEBOOK:
        case Q_STATE_TRANSLATE_EDITOR:
                set_blocking_input(Q_TRUE);
                q_keyboard_blocks = Q_TRUE;
                q_screen_dirty = Q_TRUE;
                q_cursor_off();
                break;

        case Q_STATE_EXIT:
                q_cursor_on();
                break;

        case Q_STATE_SCREENSAVER:
                if (q_program_state == Q_STATE_SCREENSAVER) {
                        break;
                }
                original_state = q_program_state;
                set_blocking_input(Q_TRUE);
                q_keyboard_blocks = Q_TRUE;
                q_cursor_off();
                break;

        }

        q_program_state = new_state;
} /* ---------------------------------------------------------------------- */

/* By default password is 16 chars. */
static char password_buffer[16];
static int password_buffer_n = 0;

/* State we were in before the screensaver was activated. */
Q_PROGRAM_STATE original_state;

/*
 * screensaver_keyboard_handler
 */
void screensaver_keyboard_handler(const int keystroke, const int flags) {
        qlog(_("SCREENSAVER ending, returning to original state %u...\n"), original_state);

        if ((keystroke == Q_KEY_ENTER) || (keystroke == C_CR)) {
                if ((password_buffer_n > 0) && (strcmp(password_buffer, get_option(Q_OPTION_SCREENSAVER_PASSWORD)) == 0)) {
                        /* UNLOCK */
                        switch_state(original_state);
                }

                password_buffer_n = 0;
        } else {
                password_buffer[password_buffer_n] = keystroke & 0xFF;
                password_buffer_n++;
                if (password_buffer_n == sizeof(password_buffer)) {
                        password_buffer_n = 0;
                }
        }
} /* ---------------------------------------------------------------------- */

/*
 * screensaver_refresh
 */
void screensaver_refresh() {
        int i, j;
        int width, height;

        screen_get_dimensions(&height, &width);

        /* Manual clear, REALLY wipe the characters */
        for (i=0; i<height; i++) {
                screen_move_yx(i, 0);
                for (j=0; j<width; j++) {
                        screen_put_color_char(' ', Q_COLOR_CONSOLE);
                }
        }
        screen_put_color_str_yx(height - 1, 0, _("Enter password to unlock: "), Q_COLOR_CONSOLE);

        screen_flush();

        if (password_buffer_n == 0) {
                memset(password_buffer, 0, sizeof(password_buffer));
        }
} /* ---------------------------------------------------------------------- */
