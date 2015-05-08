/*
 * debug.c
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
#include "qodem.h"
#include "screen.h"
#include "debug.h"

/*
 * Debug sets up this line, then prints it in one blast. Each line is
 * 80 columns to support old-style terminals.
 */
static char line_buffer[80];

/* Number of bytes displayed through debug_local_echo() */
static int local_echo_count;

/*
 * advance_to - advance to a column # by printing spaces
 */
static void advance_to(const int new_col) {

        if (new_col < 0) {
                return;
        }

        while (q_status.cursor_x < new_col) {
                print_character(' ');
        }
} /* ---------------------------------------------------------------------- */

/*
 * print_byte_offset - print the current byte offset at the beginning of the line
 */
static void print_byte_offset() {
        int i;

        /* Format is " 01234567 | " */
        memset(line_buffer, 0, sizeof(line_buffer));
        snprintf(line_buffer, sizeof(line_buffer), " %08lx | ", (q_connection_bytes_received + local_echo_count));

        /* Print it out */
        for (i=0; i<strlen(line_buffer); i++) {
                print_character(line_buffer[i]);
        }

        /* Fix the colors */
        for (i = 0; i < WIDTH; i++) {
                q_scrollback_current->colors[i] = q_current_color;
        }

        /*
         * Add the "|" for the characters area.  We cheat and
         * edit the scrollback buffer directly WITHOUT printing
         * it.
         */
        q_scrollback_current->chars[60] = '|';
        q_scrollback_current->colors[60] = q_current_color;
} /* ---------------------------------------------------------------------- */

/*
 * print_line - print the contents of line_buffer
 */
static void print_line() {
        int i;

        /* Print it out */
        for (i=0; i<strlen(line_buffer); i++) {
                print_character(line_buffer[i]);
        }
        memset(line_buffer, 0, sizeof(line_buffer));

        /* Line feed on CR screws it up.  Don't let the user set it
           manually either.
         */
        q_status.line_feed_on_cr = Q_FALSE;

        /* Newline */
        cursor_linefeed(Q_TRUE);

} /* ---------------------------------------------------------------------- */

/*
 * print_printable_chars - print the characters column
 */
static void print_printable_chars() {
        int i;

        /* Get out to the right column */
        advance_to(60);
        /* Put the vertical bar and space*/
        print_character('|');
        print_character(' ');

        /* Print out the character */
        for (i=0; i<16; i++) {
                q_current_color = q_scrollback_current->colors[i+62];
                print_character(q_scrollback_current->chars[i+62]);
                q_current_color = Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);
        }

} /* ---------------------------------------------------------------------- */

/*
 * debug_reset - reset the emulation state
 */
void debug_reset() {
        /* Explicitly check, since this function alters the scrollback. */
        if (q_status.emulation != Q_EMUL_DEBUG) {
                return;
        }

        /* Set default color */
        q_current_color = Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);

        /*
         * Turn off half duplex. console.c has a check to prevent it
         * from being turned back on.
         */
        q_status.full_duplex = Q_TRUE;

        /* Set the header */
        memset(line_buffer, 0, sizeof(line_buffer));
        snprintf(line_buffer, sizeof(line_buffer), _(""
" OFFSET   | BYTES                                           | CHARACTERS "));

        /* Newline */
        cursor_linefeed(Q_TRUE);

        /* Print it out */
        print_line();

        /* Reset local char count */
        local_echo_count = 0;

        /* Print the current byte offset */
        print_byte_offset();

} /* ---------------------------------------------------------------------- */

/*
 * debug_finish - finish out so we can switch to another
 * emulation
 */
void debug_finish() {
        /* Print out the characters on the right-hand side */
        print_printable_chars();

        /* New line */
        cursor_linefeed(Q_TRUE);
} /* ---------------------------------------------------------------------- */

static void debug_print_character(const unsigned char ch, const int q_color) {
        int offset;

/* Format:

 OFFSET   | BYTES                                           | CHARACTERS
 01234567 | 00 11 22 33 44 55 66 77-00 11 22 33 44 55 66 77 | 0123456701234567

 */

        /* Bytes received start at 1.  Offset needs to start at 0. */
        offset = (q_connection_bytes_received + local_echo_count - 1) % 16;

        /* Get to the right column */
        advance_to((offset * 3) + 12);

        /* Convert to hex */
        memset(line_buffer, 0, sizeof(line_buffer));
        snprintf(line_buffer, sizeof(line_buffer), "%02x", ch);

        /* Print the numeric form */
        q_current_color = scrollback_full_attr(q_color);
        print_character(line_buffer[0]);
        print_character(line_buffer[1]);

        /* Drop the character directly into the scrollback */
        q_scrollback_current->chars[62 + offset] = codepage_map_char(ch);
        q_scrollback_current->colors[62 + offset] = q_current_color;
        q_scrollback_current->length = 62 + offset + 1;
        q_current_color = Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);

        /* Check if it's time to wrap the line */
        if (offset == 15) {
                /* Print out the characters on the right-hand side */
                print_printable_chars();

                /* New line */
                cursor_linefeed(Q_TRUE);

                /* Print out the byte offset */
                print_byte_offset();
        } else if (offset == 7) {
                /* Put in the dash */
                print_character('-');
        } else {
                /* Push the cursor out one space */
                print_character(' ');
        }
} /* ---------------------------------------------------------------------- */

void debug_local_echo(const unsigned char ch) {
        local_echo_count++;
        debug_print_character(ch, Q_COLOR_DEBUG_ECHO);
} /* ---------------------------------------------------------------------- */

/*
 * debug_emulator - process through DEBUG emulator.
 */
Q_EMULATION_STATUS debug_emulator(const unsigned char from_modem, wchar_t * to_screen) {
        debug_print_character(from_modem, Q_COLOR_CONSOLE_TEXT);

        /* Every character is consumed, and none are printed directly. */
        *to_screen = 1;
        return Q_EMUL_FSM_NO_CHAR_YET;
} /* ---------------------------------------------------------------------- */
