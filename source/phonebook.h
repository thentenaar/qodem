/*
 * phonebook.h
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

#ifndef __PHONEBOOK_H__
#define __PHONEBOOK_H__

/* Includes --------------------------------------------------------------- */

#include "common.h"             /* Q_BOOL */
#include <time.h>               /* time_t */
#include "modem.h"
#include "emulation.h"          /* Q_EMULATION */
#include "codepage.h"           /* Q_CODEPAGE */

/* Defines ---------------------------------------------------------------- */

/* The available connection methods */
typedef enum {
#ifndef Q_NO_SERIAL
        Q_DIAL_METHOD_MODEM,
#endif /* Q_NO_SERIAL */
        Q_DIAL_METHOD_SHELL,
        Q_DIAL_METHOD_RLOGIN,
        Q_DIAL_METHOD_SSH,
        Q_DIAL_METHOD_TELNET,
        /* Q_DIAL_METHOD_TN3270, */
        Q_DIAL_METHOD_SOCKET,
        Q_DIAL_METHOD_COMMANDLINE
} Q_DIAL_METHOD;

#define Q_DIAL_METHOD_MAX (Q_DIAL_METHOD_COMMANDLINE + 1)

typedef enum {
        Q_DOORWAY_CONFIG,               /* Follow the option in .qodemrc */
        Q_DOORWAY_ALWAYS_DOORWAY,       /* Always start full doorway on connect */
        Q_DOORWAY_ALWAYS_MIXED,         /* Always start mixed-mode doorway on connect */
        Q_DOORWAY_NEVER                 /* Never start doorway on connect */
} Q_DOORWAY;

/* A single entry in a phonebook. */
struct q_phone_struct {
        Q_DIAL_METHOD method;
        wchar_t * name;
        char * address;
        char * port;
        wchar_t * username;
        wchar_t * password;
        wchar_t ** notes;
        char * script_filename;
        char * keybindings_filename;
        char * capture_filename;
        Q_EMULATION emulation;
        Q_CODEPAGE codepage;
        time_t last_call;
        unsigned int times_on;
        Q_DOORWAY doorway;
        Q_BOOL use_default_toggles;
        int toggles;
        Q_BOOL tagged;

#ifndef Q_NO_SERIAL
        Q_BOOL use_modem_cfg;
        Q_BAUD_RATE baud;
        DATA_BITS data_bits;
        STOP_BITS stop_bits;
        Q_PARITY parity;
        Q_BOOL xonxoff;
        Q_BOOL rtscts;
        Q_BOOL lock_dte_baud;
#endif /* Q_NO_SERIAL */

        Q_BOOL quicklearn;

        struct q_phone_struct * next;
        struct q_phone_struct * prev;
};

/* A phonebook */
struct q_phonebook_struct {
        char * filename;
        int tagged;
        int view_mode;
        struct q_phone_struct * entries;
        int entry_count;
        struct q_phone_struct * selected_entry;
};

#define Q_PHONEBOOK_VIEW_MODE_MAX       5

#define DEFAULT_PHONEBOOK       "fonebook.txt"

#define PHONEBOOK_LINE_SIZE     1024

/* The phonebook, stored in phonebook.c */
extern struct q_phonebook_struct q_phonebook;

/* Globals ---------------------------------------------------------------- */

/* Currently-connected entry, stored in phonebook.c */
extern struct q_phone_struct * q_current_dial_entry;

/* Functions -------------------------------------------------------------- */

extern void phonebook_reset();
extern void phonebook_keyboard_handler(const int keystroke, const int flags);
extern void phonebook_refresh();
extern void load_phonebook(const Q_BOOL backup_version);
extern void create_phonebook();

extern char * method_string(const Q_DIAL_METHOD method);

extern void dialer_keyboard_handler(const int keystroke, const int flags);
extern void dialer_process_data(unsigned char * input, const int input_n,
        int * remaining, unsigned char * output, int * output_n,
        const int output_max);
extern void set_dial_out_toggles(int toggles);
extern void phonebook_normalize();
extern void do_dialer();

#endif /* __PHONEBOOK_H__ */
