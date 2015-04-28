/*
 * forms.h
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

#ifndef __FORMS_H__
#define __FORMS_H__

/* Includes --------------------------------------------------------------- */

#include <sys/stat.h>                   /* stat() */
#include <dirent.h>                     /* mode_t */
#include "modem.h"                      /* Q_BAUD_RATE, Q_PARITY, etc. */
#include "status.h"                     /* Q_CAPTURE_TYPE */
#include "host.h"                       /* Q_HOST_TYPE */

/* Defines ---------------------------------------------------------------- */

/* File information is stored in one of these */
struct file_info {
        char * name;
        struct stat fstats;
};

/* Globals ---------------------------------------------------------------- */

/* Functions -------------------------------------------------------------- */

extern int notify_prompt_form(const char * message, const char * prompt, const char * status_prompt, const Q_BOOL visible_cursor, const double timeout, const char * allowed_chars);
extern void notify_form(const char * message, const double timeout);
extern void notify_form_long(char ** message, const double timeout, const int lines);
extern int notify_prompt_form_long(char ** message, const char * prompt, const char * status_prompt, const Q_BOOL visible_cursor, const double timeout, const char * allowed_chars, int lines);
extern char * save_form(const char * title, char * initial_value, const Q_BOOL is_directory, const Q_BOOL warn_overwrite);
extern struct file_info * view_directory(const char * initial_directory, const char * filter);
extern struct file_info * batch_entry_window(const char * initial_directory, const Q_BOOL zmodem);
extern char * file_mode_string(mode_t mode);

#ifndef Q_NO_SERIAL

/*
 * Prompt for new communication settings.  Returns true if the user
 * changed something.
 */
extern Q_BOOL comm_settings_form(const char * title, Q_BAUD_RATE * baud, DATA_BITS * data_bits, Q_PARITY * parity, STOP_BITS * stop_bits, Q_BOOL * xonxoff, Q_BOOL * rtscts);

#endif /* Q_NO_SERIAL */

/* Compose key function */
extern int compose_key(Q_BOOL utf8);

/* Pick a string for find/find again */
extern wchar_t * pick_find_string();

extern Q_CAPTURE_TYPE ask_capture_type();
extern Q_CAPTURE_TYPE ask_save_type();

extern Q_BOOL ask_host_type(Q_HOST_TYPE * type);
extern Q_BOOL prompt_listen_port(char ** port);

extern Q_BOOL check_subwin_result(void * window);

#endif /* __FORMS_H__ */
