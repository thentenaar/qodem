/*
 * script.h
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

#ifndef __SCRIPT_H__
#define __SCRIPT_H__

/* Includes --------------------------------------------------------------- */

/* Defines ---------------------------------------------------------------- */

/*
 * The script status is shared between script.c and qodem.c.
 */
typedef struct {
        Q_BOOL running;                 /* If true, a script process is running */

        Q_BOOL paused;                  /* If true, do not send IO to the script */

#ifdef Q_PDCURSES_WIN32
#else
        pid_t script_pid;               /* PID of the script process */

        int script_tty_fd;              /* Child TTY of the script process */

        char * script_tty_name;         /* Name of child TTY the script is on */

        int script_stderr_fd;           /*
                                         * FD pointing to the read end
                                         * of a FIFO connected to the
                                         * script process' stderr.
                                         */
#endif /* Q_PDCURSES_WIN32 */

        char * filename;                /* Filename of the running script */

        Q_BOOL stdin_writeable;         /* If true, write() on script_tty_fd will not block */

        Q_BOOL stdout_readable;         /* If true, read() on script_tty_fd will not block */

        Q_BOOL print_buffer_full;       /* If true, the print buffer is full */

        Q_BOOL print_buffer_empty;      /* If true, the print buffer is empty */

} Q_SCRIPT;

/* Globals ---------------------------------------------------------------- */

/* Status of the running script, stored in script.c */
extern Q_SCRIPT q_running_script;

/* Functions -------------------------------------------------------------- */

extern void script_start(const char * script_filename);
extern void script_stop();
extern void script_pause();
extern void script_resume();
extern void script_keyboard_handler(const int keystroke, const int flags);
extern void script_refresh();
extern void script_print_character(const wchar_t ch);

/* Analogous to console_process_incoming_data() */
extern void script_process_data(unsigned char * input, const int input_n, int * remaining, unsigned char * output, int * output_n, const int output_max);

#endif /* __SCRIPT_H__ */
