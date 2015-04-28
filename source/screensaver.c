/*
 * screensaver.c
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

#include "common.h"

#include <string.h>
#include "screen.h"
#include "qodem.h"
#include "options.h"
#include "codepage.h"
#include "screensaver.h"

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
