/*
 * qodem.h
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

#ifndef __QODEM_H__
#define __QODEM_H__

/* Includes --------------------------------------------------------------- */

#include <stdio.h>
#include <sys/types.h>
#include "common.h"

/* Defines ---------------------------------------------------------------- */

#ifndef INSTALL_DIR
#define INSTALL_DIR             "/usr/local/qodem"
#endif

#define KEY_FLAG_ALT            0x0001          /* ALT (META) key was pressed */
#define KEY_FLAG_CTRL           0x0002          /* CTRL key was pressed */
#define KEY_FLAG_SHIFT          0x0004          /* SHIFT key was pressed */
#define KEY_FLAG_UNICODE        0x0008          /* Key contains a Unicode character */
#define KEY_ESCAPE              0x1B

#define Q_BUFFER_SIZE           4096

#define Q_KEYBOARD_DELAY        10000

/* Globals ---------------------------------------------------------------- */

/* Global child TTY name, stored in qodem.c */
extern char * q_child_ttyname;

/* Global child TTY descriptor, stored in qodem.c */
extern int q_child_tty_fd;

/* Global child process ID, stored in qodem.c */
extern pid_t q_child_pid;

/* Global width value, stored in qodem.c */
extern int WIDTH;

/* Global height value, stored in qodem.c */
extern int HEIGHT;

/* Global status height value, stored in qodem.c */
extern int STATUS_HEIGHT;

/* Global base directory, stored in qodem.c */
extern char * q_home_directory;

/* Whether the console needs a repaint, stored in console.c */
extern Q_BOOL q_screen_dirty;

/* Global screensaver timeout, stored in qodem.c */
extern int q_screensaver_timeout;

/* Global keepalive timeout, stored in qodem.c */
extern int q_keepalive_timeout;
extern char q_keepalive_bytes[128];
extern unsigned int q_keepalive_bytes_n;

/* The last time we sent data, stored in qodem.c */
extern time_t q_data_sent_time;

/* Functions -------------------------------------------------------------- */

extern void qlog(const char * format, ...);
extern FILE * open_workingdir_file(const char * filename, char ** new_filename);
extern char * get_datadir_filename(const char * filename);
extern char * get_workingdir_filename(const char * filename);
extern char * get_scriptdir_filename(const char * filename);
extern FILE * open_datadir_file(const char * filename, char ** new_filename, const char * mode);

/* All outgoing data goes through here */
extern int qodem_write(const int fd, char * data, const int data_n, Q_BOOL sync);

/* Spawn external terminal */
extern void spawn_terminal(const char * command);

/* Close remote connection */
extern void close_connection();

#endif /* __QODEM_H__ */
