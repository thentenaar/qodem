/*
 * netclient.h
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

#ifndef __NETCLIENT_H__
#define __NETCLIENT_H__

/* Includes --------------------------------------------------------------- */

#ifdef __BORLANDC__
typedef int ssize_t;
#else
#include <unistd.h>             /* ssize_t */
#endif

/* Defines ---------------------------------------------------------------- */

#ifdef Q_UPNP

/*
 * If passed as the port string to net_listen(), it will use libminiupnpc to
 * open a firewall port.
 */
#define UPNP_PORT_STRING "UPnP"

#endif

/*
 * If passed as the port string to net_listen(), it will bind to the next
 * available non-privileged port.
 */
#define NEXT_AVAILABLE_PORT_STRING "NEXT"

/* Globals ---------------------------------------------------------------- */

/* Functions -------------------------------------------------------------- */

/**
 * Connect to a remote system over TCP.  This performs the first part of a
 * non-blocking connect() sequence.  net_connect_pending() will return true
 * between the calls to net_connect_start() and net_connect_finish().
 *
 * @param host the hostname.  This can be either a numeric string or a name
 * for DNS lookup.
 * @param the port, for example "23"
 * @return the descriptor for the socket, or -1 if there was an error
 */
extern int net_connect_start(const char * host, const char * port);

/**
 * Complete the connection logic when connecting to a remote system over TCP.
 * If using a layer that has further work such as rlogin or ssh, start that
 * session negotiation.
 *
 * @return true if the connection was established successfully.  If false,
 * the socket has already been closed and q_child_tty_fd is -1.
 */
extern Q_BOOL net_connect_finish();

/**
 * Listen for a remote connection over TCP.
 *
 * @param the port.  This can be a number, NEXT_AVAILABLE_PORT_STRING, or
 * UPNP_PORT_STRING.
 * @return the listening socket descriptor, or -1 if there was an error.
 */
extern int net_listen(const char * port);

/**
 * See if we have a new connection.
 *
 * @return the accepted socket descriptor, or -1 if no new connection is
 * available.
 */
extern int net_accept();

/**
 * Close TCP listener socket.
 */
extern void net_listen_close();

/**
 * Get the TCP listener address/port in a human-readable form.  Note that the
 * string returned is a single static buffer, i.e. this is NOT thread-safe.
 *
 * @return a string like "[1.2.3.4]:23"
 */
extern const char * net_listen_string();

#ifdef Q_UPNP

/**
 * Get the TCP listener address/port for the external gateway interface in
 * human-readable form.  Note that the string returned is a single static
 * buffer, i.e. this is NOT thread-safe.
 *
 * @return a string like "[1.2.3.4]:23"
 */
extern const char * net_listen_external_string();

#endif

/**
 * Return the actual IP address of the remote system.
 *
 * @return a string, or "Unknown" if not connected
 */
extern char * net_ip_address();

/**
 * Return the actual port number of the remote system.
 *
 * @return a string, or "Unknown" if not connected
 */
extern char * net_port();

/**
 * Close the TCP connection.
 */
extern void net_close();

/**
 * Whether or not we are listening for a connection.
 *
 * @return if true, the host mode is listening
 */
extern Q_BOOL net_is_listening();

/**
 * Whether or not a connect() is pending.
 *
 * @return if true, a connect() call is waiting to complete
 */
extern Q_BOOL net_connect_pending();

/**
 * Whether or not we are connected.
 *
 * @return if true, q_child_tty_fd is connected to a remote system
 */
extern Q_BOOL net_is_connected();

/**
 * Read data from remote system to a buffer, via an 8-bit clean channel
 * through the telnet protocol.
 *
 * @param fd the socket descriptor
 * @param buf the buffer to write to
 * @param count the number of bytes requested
 * @return the number of bytes read into buf
 */
extern ssize_t telnet_read(const int fd, void * buf, size_t count);

/**
 * Write data from a buffer to the remote system, via an 8-bit clean channel
 * through the telnet protocol.
 *
 * @param fd the socket descriptor
 * @param buf the buffer to read from
 * @param count the number of bytes to write to the remote side
 * @return the number of bytes written
 */
extern ssize_t telnet_write(const int fd, void * buf, size_t count);

/**
 * Send new screen dimensions to the remote side.  This uses the Negotiate
 * About Window Size telnet option (RFC 1073).
 *
 * @param lines the number of screen rows
 * @param columns the number of screen columns
 */
extern void telnet_resize_screen(const int lines, const int columns);

/**
 * See if the telnet session is in ASCII mode.
 *
 * @return if true, the session is in ASCII mode
 */
extern Q_BOOL telnet_is_ascii();

/**
 * Read data from remote system to a buffer, via an 8-bit clean channel
 * through the rlogin protocol.
 *
 * @param fd the socket descriptor
 * @param buf the buffer to write to
 * @param count the number of bytes requested
 * @return the number of bytes read into buf
 */
extern ssize_t rlogin_read(const int fd, void * buf, size_t count, Q_BOOL oob);

/**
 * Write data from a buffer to the remote system, via an 8-bit clean channel
 * through the rlogin protocol.
 *
 * @param fd the socket descriptor
 * @param buf the buffer to read from
 * @param count the number of bytes to write to the remote side
 * @return the number of bytes written
 */
extern ssize_t rlogin_write(const int fd, void * buf, size_t count);

/**
 * Send new screen dimensions to the remote side.
 *
 * @param lines the number of screen rows
 * @param columns the number of screen columns
 */
extern void rlogin_resize_screen(const int lines, const int columns);

#ifdef Q_LIBSSH2

/**
 * Read data from remote system to a buffer, via an 8-bit clean channel
 * through the ssh protocol.
 *
 * @param fd the socket descriptor
 * @param buf the buffer to write to
 * @param count the number of bytes requested
 * @return the number of bytes read into buf
 */
extern ssize_t ssh_read(const int fd, void * buf, size_t count);

/**
 * Write data from a buffer to the remote system, via an 8-bit clean channel
 * through the ssh protocol.
 *
 * @param fd the socket descriptor
 * @param buf the buffer to read from
 * @param count the number of bytes to write to the remote side
 * @return the number of bytes written
 */
extern ssize_t ssh_write(const int fd, void * buf, size_t count);

/**
 * Send new screen dimensions to the remote side.
 *
 * @param lines the number of screen rows
 * @param columns the number of screen columns
 */
extern void ssh_resize_screen(const int lines, const int columns);

/**
 * Flag to indicate some more data MIGHT be ready to read.  This can happen
 * if the last call to ssh_read() resulted in a length of 0 or EAGAIN.  The
 * socket will not be readable to select(), but another call to ssh_read()
 * could read some data.
 *
 * @return true if there might be data to read from the ssh session
 */
extern Q_BOOL ssh_maybe_readable();

/**
 * Get the ssh server key fingerprint as a hex-encoded MD5 hash of the server
 * key, the same as the key fingerprint exposed by most ssh clients.
 *
 * @return the key string
 */
extern const char * ssh_server_key_str();

#endif /* Q_LIBSSH2 */

/**
 * Thanks to Winsock I need to check for either errno or WSAGetLastError().
 *
 * @return the appropriate error value after a network call
 */
extern int get_errno();

/**
 * Get the error message that goes with get_errno().
 *
 * @return the appropriate error message for a network error value
 */
extern const char * get_strerror(int err);

#ifdef Q_PDCURSES_WIN32

/**
 * Shut down winsock before exiting qodem.
 */
extern void stop_winsock();

#endif

#endif /* __NETCLIENT_H__ */
