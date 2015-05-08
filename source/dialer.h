/*
 * dialer.h
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

#ifndef __DIALER_H__
#define __DIALER_H__

/* Includes --------------------------------------------------------------- */

#include <time.h>
#include "phonebook.h"

/* Defines ---------------------------------------------------------------- */

/**
 * The states for the phonebook dialer display used during Q_STATE_DIALER.
 */
typedef enum {
    Q_DIAL_DIALING,
    Q_DIAL_MANUAL_CYCLE,
    Q_DIAL_CYCLE,
    Q_DIAL_KILLED,
    Q_DIAL_LINE_BUSY,
    Q_DIAL_BETWEEN_PAUSE,
    Q_DIAL_NO_NUMBERS_LEFT,
    Q_DIAL_USER_ABORTED,
    Q_DIAL_CONNECTED
} Q_DIAL_STATE;

/* Globals ---------------------------------------------------------------- */

/**
 * Our current dialing state.
 */
extern Q_DIAL_STATE q_dial_state;

/**
 * When we started dialing.
 */
extern time_t q_dialer_start_time;

/**
 * How much time is left (in seconds) on the cycle clock.
 */
extern time_t q_dialer_cycle_time;

/**
 * When the cycle clock started.
 */
extern time_t q_dialer_cycle_start_time;

/**
 * How many calls have been attempted.
 */
extern int q_dialer_attempts;

/**
 * The status line to report on the redialer screen.
 */
extern char q_dialer_status_message[DIALOG_MESSAGE_SIZE];

/**
 * The modem line to report on the redialer screen.
 */
extern char q_dialer_modem_message[DIALOG_MESSAGE_SIZE];

/* Functions -------------------------------------------------------------- */

/**
 * Convert a command line string with spaces into a NULL-terminated array of
 * strings appropriate to passing to the execvp().
 *
 * @param argv the command line string
 * @return the array
 */
extern char ** tokenize_command(const char * argv);

/**
 * Connect to a remote system.
 *
 * @param number the phonebook entry to connect to
 */
extern void dial_out(struct q_phone_struct * number);

/**
 * Set a file descriptor or Winsock socket handle to non-blocking mode.
 *
 * @param fd the descriptor
 */
extern void set_nonblock(const int fd);

/**
 * Set a tty into raw mode.
 *
 * @param tty_fd the tty descriptor
 */
extern void set_raw_termios(const int tty_fd);

/**
 * Called upon the completion of a successful connection.
 */
extern void dial_success();

/**
 * Get the appropriate TERM variable value for the current emulation.
 *
 * @return "ansi", "xterm", etc.
 */
extern const char * dialer_get_term();

/**
 * Get the appropriate LANG variable value for the current emulation.
 *
 * @return "en", "en_US", etc.
 */
extern const char * dialer_get_lang();

#endif /* __DIALER_H__ */
