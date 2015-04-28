/*
 * netclient.h
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

#ifndef __NETCLIENT_H__
#define __NETCLIENT_H__

/* Includes --------------------------------------------------------------- */

#ifdef __BORLANDC__
typedef int ssize_t;
#else
#include <unistd.h>     /* ssize_t */
#endif

/* Defines ---------------------------------------------------------------- */

/* These can be passed into net_listen(). */

#ifdef Q_UPNP
/* Use libminiupnpc to open a firewall port */
#define UPNP_PORT_STRING "UPnP"
#endif /* Q_UPNP */

/* Bind to the next available non-privileged port */
#define NEXT_AVAILABLE_PORT_STRING "NEXT"

/* Globals ---------------------------------------------------------------- */

/* Functions -------------------------------------------------------------- */

/* Connect to a remote system over TCP */
extern int net_connect_start(const char * host, const char * port);

/* Connect to a remote system over TCP */
extern Q_BOOL net_connect_finish();

/* Listen for a remote connection over TCP */
extern int net_listen(const char * port);

/* See if we have a new connection */
extern int net_accept();

/* Close TCP listener socket */
extern void net_listen_close();

/* Return TCP listener address/port in human-readable form */
extern const char * net_listen_string();

#ifdef Q_UPNP
/* Return TCP listener address/port external interface in human-readable form */
extern const char * net_listen_external_string();
#endif /* Q_UPNP */

/* Return the actual IP address of the remote system */
extern char * net_ip_address();

/* Return the actual port number of the remote system */
extern char * net_port();

/* Close TCP connection */
extern void net_close();

/* Whether or not we are listening for a connection */
extern Q_BOOL net_is_listening();

/* Whether or not a connect() is still pending */
extern Q_BOOL net_connect_pending();

/* Whether or not we are connected */
extern Q_BOOL net_is_connected();

/* Just like read(), but do extra logging */
extern ssize_t raw_read(const int fd, void * buf, size_t count);

/* Just like write(), but do extra logging */
extern ssize_t raw_write(const int fd, void * buf, size_t count);

/* Just like read(), but perform the telnet protocol */
extern ssize_t telnet_read(const int fd, void * buf, size_t count);

/* Just like write(), but perform the telnet protocol */
extern ssize_t telnet_write(const int fd, void * buf, size_t count);

/* Send new screen dimensions to the remote side */
extern void telnet_resize_screen(const int lines, const int columns);

/* Telnet server/client is in ASCII mode */
extern Q_BOOL telnet_is_ascii();

/* Just like read(), but perform the rlogin protocol */
extern ssize_t rlogin_read(const int fd, void * buf, size_t count, Q_BOOL oob);

/* Just like write(), but perform the rlogin protocol */
extern ssize_t rlogin_write(const int fd, void * buf, size_t count);

/* Send new screen dimensions to the remote side */
extern void rlogin_resize_screen(const int lines, const int columns);

#ifdef Q_LIBSSH2

/* Just like read(), but perform the ssh protocol */
extern ssize_t ssh_read(const int fd, void * buf, size_t count);

/* Just like write(), but perform the ssh protocol */
extern ssize_t ssh_write(const int fd, void * buf, size_t count);

/* Send new screen dimensions to the remote side */
extern void ssh_resize_screen(const int lines, const int columns);

/* Flag to indicate some more data MIGHT be ready to read */
extern Q_BOOL ssh_maybe_readable();

/* Currently-connected ssh server key fingerprint */
extern const char * ssh_server_key_str();

#endif /* Q_LIBSSH2 */

/* Thanks to Winsock I need to check for either errno or WSAGetLastError() */
extern int get_errno();

#ifdef Q_PDCURSES_WIN32
extern void stop_winsock();
#endif /* Q_PDCURSES_WIN32 */

#endif /* __NETCLIENT_H__ */
