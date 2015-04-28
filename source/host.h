/*
 * host.h
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

#ifndef __HOST_H__
#define __HOST_H__

/* Includes --------------------------------------------------------------- */

/* Defines ---------------------------------------------------------------- */

typedef enum {

#ifndef Q_NO_SERIAL
        Q_HOST_TYPE_MODEM,              /* Wait for modem to answer */

        Q_HOST_TYPE_SERIAL,             /* Listen on serial port */
#endif /* Q_NO_SERIAL */

        Q_HOST_TYPE_SOCKET,             /* Listen on socket */

        Q_HOST_TYPE_TELNETD,            /* Listen on socket and speak
                                         * the telnet server protocol
                                         */

} Q_HOST_TYPE;

/* Globals ---------------------------------------------------------------- */

/* When in host mode, the type of host, stored in host.c */
extern Q_HOST_TYPE q_host_type;

/*
 * Whether or not host mode is active, even through file transfers,
 * stored in host.c
 */
extern Q_BOOL q_host_active;

/* Functions -------------------------------------------------------------- */

extern void host_start(Q_HOST_TYPE type, const char * port);
extern void host_keyboard_handler(const int keystroke, const int flags);
extern void host_refresh();

/* Analogous to console_process_incoming_data() */
extern void host_process_data(unsigned char * input, const int input_n, int * remaining, unsigned char * output, int * output_n, const int output_max);

#endif /* __HOST_H__ */
