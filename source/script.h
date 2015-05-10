/*
 * script.h
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

#ifndef __SCRIPT_H__
#define __SCRIPT_H__

/* Includes --------------------------------------------------------------- */

/* Defines ---------------------------------------------------------------- */

/**
 * The global script running status is used to coordinate behavior between
 * the script process and the qodem process.
 */
typedef struct {

    /**
     * If true, a script process is running.
     */
    Q_BOOL running;

    /**
     * If true, do not send IO to the script.
     */
    Q_BOOL paused;

#ifndef Q_PDCURSES_WIN32

    /**
     * PID of the script process.
     */
    pid_t script_pid;

    /**
     * Child TTY of the script process.
     */
    int script_tty_fd;

    /**
     * Name of child TTY the script is on.
     */
    char * script_tty_name;

    /**
     * FD pointing to the read end of a FIFO connected to the script process'
     * stderr.
     */
    int script_stderr_fd;

#endif

    /**
     * Filename of the running script.
     */
    char * filename;

    /**
     * If true, write() on script_tty_fd will not block.
     */
    Q_BOOL stdin_writeable;

    /**
     * If true, read() on script_tty_fd will not block.
     */
    Q_BOOL stdout_readable;

    /**
     * If true, the print buffer is full.
     */
    Q_BOOL print_buffer_full;

    /**
     * If true, the print buffer is empty.
     */
    Q_BOOL print_buffer_empty;

} Q_SCRIPT;

/* Globals ---------------------------------------------------------------- */

/**
 * Status of the running script.
 */
extern Q_SCRIPT q_running_script;

/* Functions -------------------------------------------------------------- */

/**
 * Spawn a new script process and start it.
 *
 * @param script_filename the filename to execute
 */
extern void script_start(const char * script_filename);

/**
 * Terminate the script process.  We try to do it nicely with SIGHUP on POSIX
 * systems, which has the minor risk that we might hang waiting on its exit
 * if it ignores that signal.  For Windows we brutally murder it with
 * TerminateProcess if it hasn't already exited before script_stop() is
 * called.
 */
extern void script_stop();

/**
 * Stop sending I/O to the script process.
 */
extern void script_pause();

/**
 * Resume sending I/O to the script process.
 */
extern void script_resume();

/**
 * Keyboard handler for script running mode.
 *
 * @param keystroke the keystroke from the user.
 * @param flags KEY_FLAG_ALT, KEY_FLAG_CTRL, etc.  See input.h.
 */
extern void script_keyboard_handler(const int keystroke, const int flags);

/**
 * Draw screen for script running mode.
 */
extern void script_refresh();

/**
 * Called by print_character() in scrollback.c to pass printable characters
 * to the running script's stdin.
 *
 * @param ch the character
 */
extern void script_print_character(const wchar_t ch);

/**
 * Process raw bytes from the remote side through the script.  See also
 * console_process_incoming_data().
 *
 * @param input the bytes from the remote side
 * @param input_n the number of bytes in input_n
 * @param remaining the number of un-processed bytes that should be sent
 * through a future invocation of script_process_data()
 * @param output a buffer to contain the bytes to send to the remote side
 * @param output_n the number of bytes that this function wrote to output
 * @param output_max the maximum number of bytes this function may write to
 * output
 */
extern void script_process_data(unsigned char * input, const int input_n,
                                int * remaining, unsigned char * output,
                                int * output_n, const int output_max);

#endif /* __SCRIPT_H__ */
