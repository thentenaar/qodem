/*
 * debug.c
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

#include "common.h"
#include <string.h>
#include "qodem.h"
#include "screen.h"
#include "debug.h"

/**
 * Number of bytes displayed through debug_local_echo().
 */
static int local_echo_count;

/**
 * Advance to a column by printing spaces.
 *
 * @param new_col the column to advance to
 */
static void advance_to(const int new_col) {

    if (new_col < 0) {
        return;
    }

    while (q_status.cursor_x < new_col) {
        print_character(' ');
    }
}

/**
 * Print the current byte offset at the beginning of the line.
 */
static void print_byte_offset() {
    int i;

    char buffer[16];
    
    /*
     * Format is " 01234567 | "
     */
    memset(buffer, 0, sizeof(buffer));
    snprintf(buffer, sizeof(buffer) - 1, " %08lx | ",
             (q_connection_bytes_received + local_echo_count));

    /*
     * Print it out
     */
    for (i = 0; i < strlen(buffer); i++) {
        print_character(buffer[i]);
    }

    /*
     * Fix the colors
     */
    for (i = 0; i < WIDTH; i++) {
        q_scrollback_current->colors[i] = q_current_color;
    }

    /*
     * Add the "|" for the characters area.  We cheat and edit the scrollback
     * buffer directly WITHOUT printing it.
     */
    q_scrollback_current->chars[60] = '|';
    q_scrollback_current->colors[60] = q_current_color;
}

/**
 * Print the right-side "printable characters" column.
 */
static void print_printable_chars() {
    int i;

    /*
     * Get out to the right column
     */
    advance_to(60);

    /*
     * Put the vertical bar and space
     */
    print_character('|');
    print_character(' ');

    /*
     * Print out the characters
     */
    for (i = 0; i < 16; i++) {
        q_current_color = q_scrollback_current->colors[i + 62];
        print_character(q_scrollback_current->chars[i + 62]);
        q_current_color =
            Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);
    }

}

/**
 * Reset the emulation state.
 */
void debug_reset() {

    char header[80];
    int i;

    /*
     * Explicitly check, since this function alters the scrollback.
     */
    if (q_status.emulation != Q_EMUL_DEBUG) {
        return;
    }

    /*
     * Set default color.
     */
    q_current_color = Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);

    /*
     * Turn off half duplex. console.c has a check to prevent it
     * from being turned back on.
     */
    q_status.full_duplex = Q_TRUE;

    /*
     * Set the header.
     */
    memset(header, 0, sizeof(header));
    snprintf(header, sizeof(header) - 1,
             _(""
               " OFFSET   | BYTES"
               "                                           | CHARACTERS "));

    /*
     * Line feed on CR screws up the hex display.  Always reset it before
     * calling cursor_linefeed() in case the user tried to switch it back.
     * either.
     */
    q_status.line_feed_on_cr = Q_FALSE;

    /*
     * Print out the header line.
     */
    cursor_linefeed(Q_TRUE);
    for (i = 0; i < strlen(header); i++) {
        print_character(header[i]);
    }
    cursor_linefeed(Q_TRUE);

    /*
     * Reset local char count.
     */
    local_echo_count = 0;

    /*
     * Print the current byte offset.
     */
    print_byte_offset();

}

/**
 * Called when switching to another emulation to emit the pending bytes in
 * the hex display.
 */
void debug_finish() {
    /*
     * Print out the characters on the right-hand side.
     */
    print_printable_chars();
    cursor_linefeed(Q_TRUE);
}

/**
 * Print a character to the scrollback, using a color to distinguish local
 * and remote.
 *
 * @param ch the character (byte) to print
 * @param q_color a Q_COLOR enum
 */
static void debug_print_character(const unsigned char ch,
                                  const Q_COLOR q_color) {

    int offset;
    char buffer[4];

/* Format:

 OFFSET   | BYTES                                           | CHARACTERS
 01234567 | 00 11 22 33 44 55 66 77-00 11 22 33 44 55 66 77 | 0123456701234567

 */

    /*
     * Bytes received start at 1.  Offset needs to start at 0.
     */
    offset = (q_connection_bytes_received + local_echo_count - 1) % 16;

    /*
     * Get to the correct column.
     */
    advance_to((offset * 3) + 12);

    /*
     * Convert the byte to hex.
     */
    memset(buffer, 0, sizeof(buffer));
    snprintf(buffer, sizeof(buffer) - 1, "%02x", ch);

    /*
     * Print the hex characters out.
     */
    q_current_color = scrollback_full_attr(q_color);
    print_character(buffer[0]);
    print_character(buffer[1]);

    /*
     * Drop the codepage printable character directly into the scrollback
     * buffer.
     */
    q_scrollback_current->chars[62 + offset] = codepage_map_char(ch);
    q_scrollback_current->colors[62 + offset] = q_current_color;
    q_scrollback_current->length = 62 + offset + 1;
    q_current_color = Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);

    /*
     * Check if it's time to wrap the line.
     */
    if (offset == 15) {
        /*
         * It's time to wrap the line.  Print out the characters on the
         * right-hand side.
         */
        print_printable_chars();
        cursor_linefeed(Q_TRUE);

        /*
         * Print out the byte offset for the new line.
         */
        print_byte_offset();
    } else if (offset == 7) {
        /*
         * Halfway there, put in the dash separating the two columns of hex
         * digits.
         */
        print_character('-');
    } else {
        /*
         * Inside one of the hex columns, just insert a space to the next hex
         * value.
         */
        print_character(' ');
    }
}

/**
 * Echo local transmitted bytes to the hex display in a distinct color.
 *
 * @param ch the byte that was sent to the remote side
 */
void debug_local_echo(const unsigned char ch) {
    local_echo_count++;
    debug_print_character(ch, Q_COLOR_DEBUG_ECHO);
}

/**
 * Push one byte through the DEBUG emulator.
 *
 * @param from_modem one byte from the remote side.
 * @param to_screen if the return is Q_EMUL_FSM_ONE_CHAR or
 * Q_EMUL_FSM_MANY_CHARS, then to_screen will have a character to display on
 * the screen.
 * @return one of the Q_EMULATION_STATUS constants.  See emulation.h.
 */
Q_EMULATION_STATUS debug_emulator(const unsigned char from_modem,
                                  wchar_t * to_screen) {

    debug_print_character(from_modem, Q_COLOR_CONSOLE_TEXT);

    /*
     * Every character is consumed, and none are printed directly.
     */
    *to_screen = 1;
    return Q_EMUL_FSM_NO_CHAR_YET;
}
