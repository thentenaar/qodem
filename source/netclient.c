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

/*
 * This contains a general-purpose telnet client.  Other than a few
 * references to some qodem variables and functions, it is basically
 * standalone and could easily be incorporated in another application.
 *
 * The API is:
 *
 *    net_connect(host, port) - provide a raw TCP connection to host:port
 *        and return the socket fd
 *
 *    net_close() - close the TCP port
 *
 *    telnet_read() - same API as read()
 *
 *    telnet_write() - same API as write()
 *
 *    telnet_resize_screen(lines, columns) - send the (new) screen
 *        dimensions to the remote side
 *
 * Its intended use is for the calling code to call net_connect(), and
 * then poll() or select() on the returned fd.
 *
 * When poll/select indicates bytes are ready for read, call
 * telnet_read(); telnet_read() will handle all of the telnet protocol
 * initial negotiation and return only the data payload bytes to the
 * caller.  NOTE: It IS possible for poll/select to report ready data
 * but for telnet_read() to return nothing!  This is due to all of the
 * bytes read()able by the socket being consumed by the telnet
 * protocol itself.  In such cases telnet_read() returns -1 and sets
 * errno to EAGAIN.  This is similar to but unlike a true socket or
 * pty, which will always have read() return 0 or more if the
 * poll/select indicates readable.
 *
 * Similarly call telnet_write() if the calling code wants to emit
 * bytes to the remote end.
 *
 * The telnet options this client desires are:
 *     Binary Transmission
 *     Suppress Go-Ahead
 *     Negotiate Window Size
 *     Terminal Type
 *     New Environment
 *
 * It also sends the environment variables TERM and LANG.  Not every
 * telnetd understands these variables, so this client breaks the
 * telnet protocol specification and sends the TERM variable as the
 * telnet terminal type.  This is incorrect behavior according to RFC
 * 1091: the only terminal types officially supported are those listed
 * in RFC 1010 Assigned Numbers (available at
 * http://tools.ietf.org/rfc/rfc1010.txt).  Of the officially
 * supported terminal types, only the following correspond to any of
 * the qodem terminals: DEC-VT52, DEC-VT100.  The missing ones are
 * ANSI(.SYS), Avatar, VT220, Linux, TTY, and Xterm.  In qodem's
 * defense, netkit-telnet does the same thing, and nearly all of the
 * RFC 1010 terminal types are now defunct.
 *
 */

/*
 * This contains a general-purpose rlogin client.  Other than a few
 * references to some qodem variables and functions, it is basically
 * standalone and could easily be incorporated in another application.
 *
 * The API is:
 *
 *    net_connect(host, "513") - provide a raw TCP connection to host:513
 *        and return the socket fd.  (Rlogin can only connect to port 513.)
 *
 *    net_close() - close the TCP port
 *
 *    rlogin_read() - same API as read() EXCEPT is also has an OOB flag
 *
 *    rlogin_write() - same API as write()
 *
 *    rlogin_resize_screen(lines, columns) - send the (new) screen
 *        dimensions to the remote side
 *
 * Its intended use is for the calling code to call net_connect(), and
 * then poll() or select() on the returned fd.
 *
 * When poll/select indicates bytes are ready for read, call
 * rlogin_read(); rlogin_read() will handle all of the rlogin protocol
 * initial negotiation and return only the data payload bytes to the
 * caller.  NOTE: It IS possible for poll/select to report ready data
 * but for rlogin_read() to return nothing!  This is due to all of the
 * bytes read()able by the socket being consumed by the rlogin
 * protocol itself.  In such cases rlogin_read() returns -1 and sets
 * errno to EAGAIN.  This is similar to but unlike a true socket or
 * pty, which will always have read() return 0 or more if the
 * poll/select indicates readable.  Also, OOB data must be looked for
 * by the caller and if some is present the oob parameter to
 * rlogin_read() must be set.
 *
 * Similarly call rlogin_write() if the calling code wants to emit
 * bytes to the remote end.
 *
 */

/*
 * This contains a general-purpose ssh client.  Other than a few
 * references to some qodem variables and functions, it is basically
 * standalone and could easily be incorporated in another application.
 *
 * The API is:
 *
 *    net_connect(host, port) - connect to the remote SSH server
 *
 *    net_close() - close the SSH session
 *
 *    ssh_read() - same API as read()
 *
 *    ssh_write() - same API as write()
 *
 *    ssh_resize_screen(lines, columns) - send the (new) screen
 *        dimensions to the remote side
 *
 * Its intended use is for the calling code to call net_connect(), and
 * then poll() or select() on the returned fd.
 *
 * When poll/select indicates bytes are ready for read, call
 * ssh_read(); ssh_read() will handle all of the ssh protocol initial
 * negotiation and return only the data payload bytes to the caller.
 * NOTE: It IS possible for poll/select to report ready data but for
 * ssh_read() to return nothing!  This is due to all of the bytes
 * read()able by the socket being consumed by the ssh protocol itself.
 * In such cases ssh_read() returns -1 and sets errno to EAGAIN.  This
 * is similar to but unlike a true socket or pty, which will always
 * have read() return 0 or more if the poll/select indicates readable.
 *
 * Similarly call ssh_write() if the calling code wants to emit bytes
 * to the remote end.
 *
 */

#include "qcurses.h"

#include <assert.h>
#ifdef Q_PDCURSES_WIN32
#ifdef __BORLANDC__
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wspiapi.h>
#define UNLEN 256
#else
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#include <ws2tcpip.h>
#include <lmcons.h>
#endif /* _WIN32_WINNT */
#endif /* __BORLANDC__ */
#else
#include <sys/socket.h>
#include <pwd.h>
#include <netdb.h>
#endif /* Q_PDCURSES_WIN32 */
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "common.h"

#include "dialer.h"
#include "qodem.h"
#include "forms.h"
#include "states.h"

#ifdef Q_LIBSSH2
#include <libssh2.h>
#include <ctype.h>
#include <gcrypt.h>
#define libssh2_md5_ctx gcry_md_hd_t
#define libssh2_md5_init(ctx) gcry_md_open (ctx,  GCRY_MD_MD5, 0);
#define libssh2_md5_update(ctx, data, len) gcry_md_write (ctx, data, len)
#define libssh2_md5_final(ctx, out) \
    memcpy (out, gcry_md_read (ctx, 0), 20), gcry_md_close (ctx)
#include "options.h"
#include "input.h"
#endif /* Q_LIBSSH2 */

#ifdef Q_UPNP
#include "miniupnpc.h"
#include "miniwget.h"
#include "upnpcommands.h"
#endif /* Q_UPNP */

#include "netclient.h"

/* #define DEBUG_NET 1 */
#undef DEBUG_NET
#ifdef DEBUG_NET
static FILE * DEBUG_FILE_HANDLE = NULL;
#endif

typedef enum {
        INIT,                   /* Raw connection established */

        SENT_OPTIONS,           /* Sent all the desired telnet options */

        SENT_LOGIN,             /* Sent the rlogin login data */

        ESTABLISHED             /* In 8-bit streaming mode */
} STATE;

/* Whether or not we are connected through net_connect() */
static Q_BOOL connected = Q_FALSE;

/* Whether or not we are listening through net_listen() */
static Q_BOOL listening = Q_FALSE;

/* Whether or not we are listening through net_listen() */
static Q_BOOL pending = Q_FALSE;

/* The actual IP address of the remote side */
static char remote_host[NI_MAXHOST];
static char remote_port[NI_MAXSERV];

/* The actual IP address of the local side when listening */
static char local_host[NI_MAXHOST];
/* [IP]:port */
static char local_host_full[NI_MAXHOST + NI_MAXSERV + 4];

/* The actual IP address of the local side when listening */
static int listen_fd = -1;

/* State of the session negotiation */
static STATE state = INIT;

/* Raw input buffer */
static unsigned char read_buffer[Q_BUFFER_SIZE];
static int read_buffer_n = 0;

/* Raw output buffer */
static unsigned char write_buffer[Q_BUFFER_SIZE];
static int write_buffer_n = 0;

/* Received sub-negotiation data */
#define SUBNEG_BUFFER_MAX 128
static unsigned char subneg_buffer[SUBNEG_BUFFER_MAX];
static int subneg_buffer_n;

/* Telnet protocol speaks to a Network Virtual Terminal (NVT) */
struct nvt_state {
        /* NVT flags */
        Q_BOOL echo_mode;
        Q_BOOL binary_mode;
        Q_BOOL go_ahead;
        Q_BOOL do_naws;
        Q_BOOL do_term_type;
        Q_BOOL do_term_speed;
        Q_BOOL do_environment;

        /* telnet_read() flags */
        Q_BOOL iac;
        Q_BOOL dowill;
        unsigned char dowill_type;
        Q_BOOL subneg_end;
        Q_BOOL is_eof;
        Q_BOOL eof_msg;
        Q_BOOL read_cr;

        /* telnet_write() flags */
        int write_rc;
        int write_last_errno;
        Q_BOOL write_last_error;
        Q_BOOL write_cr;
};
static struct nvt_state nvt;

/* Telnet protocol special characters */
#define TELNET_SE               240
#define TELNET_NOP              241
#define TELNET_DM               242
#define TELNET_BRK              243
#define TELNET_IP               244
#define TELNET_AO               245
#define TELNET_AYT              246
#define TELNET_EC               247
#define TELNET_EL               248
#define TELNET_GA               249
#define TELNET_SB               250
#define TELNET_WILL             251
#define TELNET_WONT             252
#define TELNET_DO               253
#define TELNET_DONT             254
#define TELNET_IAC              255

/* Reset NVT to default state as per RFC 854. */
static void reset_nvt() {
        nvt.echo_mode           = Q_FALSE;
        nvt.binary_mode         = Q_FALSE;
        nvt.go_ahead            = Q_TRUE;
        nvt.do_naws             = Q_FALSE;
        nvt.do_term_type        = Q_FALSE;
        nvt.do_term_speed       = Q_FALSE;
        nvt.do_environment      = Q_FALSE;

        nvt.iac                 = Q_FALSE;
        nvt.dowill              = Q_FALSE;
        nvt.subneg_end          = Q_FALSE;
        nvt.is_eof              = Q_FALSE;
        nvt.eof_msg             = Q_FALSE;
        nvt.read_cr             = Q_FALSE;

        nvt.write_rc            = 0;
        nvt.write_last_errno    = 0;
        nvt.write_last_error    = Q_FALSE;
        nvt.write_cr            = Q_FALSE;

} /* ---------------------------------------------------------------------- */

/* Whether or not we are connected */
Q_BOOL net_is_connected() {
        return connected;
} /* ---------------------------------------------------------------------- */

/* Whether or not a connect() is still pending */
Q_BOOL net_connect_pending() {
        return pending;
} /* ---------------------------------------------------------------------- */

/* Whether or not we are listening */
Q_BOOL net_is_listening() {
        return listening;
} /* ---------------------------------------------------------------------- */

/* Forward reference to a function called by net_connect() */
static void rlogin_send_login(const int fd);

#ifdef Q_LIBSSH2
/* Forward reference to a function called by net_connect() */
static int ssh_setup_connection(int fd, const char * host, const char * port);
/* Forward reference to a function called by net_close() */
static void ssh_close();
#endif /* Q_LIBSSH2 */

#ifdef Q_UPNP
static Q_BOOL upnp_is_initted = Q_FALSE;
static Q_BOOL upnp_forwarded = Q_FALSE;
static struct UPNPUrls upnp_urls;
static struct IGDdatas upnp_igd_datas;
static char upnp_local_port[NI_MAXSERV];
static char upnp_external_address[NI_MAXHOST];
/* [IP]:port */
static char local_host_external_full[NI_MAXHOST + NI_MAXSERV + 4];

/* Return TCP listener address/port in human-readable form */
const char * net_listen_external_string() {
        return local_host_external_full;
} /* ---------------------------------------------------------------------- */

#endif /* Q_UPNP */

/* Return the actual IP address of the remote system */
char * net_ip_address() {
        if (connected == Q_TRUE) {
                return remote_host;
        }
        return _("Unknown");
} /* ---------------------------------------------------------------------- */

/* Return the actual port number of the remote system */
char * net_port() {
        if (connected == Q_TRUE) {
                return remote_port;
        }
        return _("Unknown");
} /* ---------------------------------------------------------------------- */

#ifdef Q_UPNP

/* Setup UPnP system for port forwarding */
static Q_BOOL upnp_init() {
        struct UPNPDev * device_list;
        int rc;

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "upnp_init() : upnp_is_initted = %s\n",
                (upnp_is_initted == Q_TRUE ? "true" : "false"));
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        if (upnp_is_initted == Q_TRUE) {
                return Q_TRUE;
        }

        memset(&upnp_urls, 0, sizeof(struct UPNPUrls));
        memset(&upnp_igd_datas, 0, sizeof(struct IGDdatas));
#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "upnp_init() : upnpDiscover()\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        device_list = upnpDiscover(2000, NULL, NULL, 0);
        if (device_list != NULL) {

                rc = UPNP_GetValidIGD(device_list, &upnp_urls, &upnp_igd_datas, local_host, sizeof(local_host));
                switch (rc) {
                case 1:
#ifdef DEBUG_NET
                        fprintf(DEBUG_FILE_HANDLE, "Found valid IGD : %s\n", upnp_urls.controlURL);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                        break;
                case 2:
#ifdef DEBUG_NET
                        fprintf(DEBUG_FILE_HANDLE, "Found a (not connected?) IGD : %s\n", upnp_urls.controlURL);
                        fprintf(DEBUG_FILE_HANDLE, "Trying to continue anyway\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                        break;
                case 3:
#ifdef DEBUG_NET
                        fprintf(DEBUG_FILE_HANDLE, "UPnP device found. Is it an IGD ? : %s\n", upnp_urls.controlURL);
                        fprintf(DEBUG_FILE_HANDLE, "Trying to continue anyway\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                        break;
                default:
#ifdef DEBUG_NET
                        fprintf(DEBUG_FILE_HANDLE, "Found device (igd ?) : %s\n", upnp_urls.controlURL);
                        fprintf(DEBUG_FILE_HANDLE, "Trying to continue anyway\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                        break;
                }
#ifdef DEBUG_NET
                fprintf(DEBUG_FILE_HANDLE, "Local LAN ip address : %s\n", local_host);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

                /* Grab the external interface */
                rc = UPNP_GetExternalIPAddress(upnp_urls.controlURL,
                        upnp_igd_datas.servicetype,
                        upnp_external_address);
                if (rc != 0) {
#ifdef DEBUG_NET
                        fprintf(DEBUG_FILE_HANDLE, "upnp_init(): failed to discover external IP address: %d\n", rc);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                        freeUPNPDevlist(device_list);
                        upnp_is_initted = Q_FALSE;
                        return Q_FALSE;
                }

#ifdef DEBUG_NET
                fprintf(DEBUG_FILE_HANDLE, "upnp_init(): external address is %s\n", upnp_external_address);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

                freeUPNPDevlist(device_list);
                upnp_is_initted = Q_TRUE;
                return Q_TRUE;

        } /* if (device_list != NULL) */

        /* No UPnP devices found */
#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "upnp_init() : no UPnP devices found\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        freeUPNPDevlist(device_list);
        return Q_FALSE;
} /* ---------------------------------------------------------------------- */

/* Shutdown UPnP system */
static Q_BOOL upnp_teardown() {
#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "upnp_teardown()\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        if (upnp_forwarded == Q_TRUE) {
#ifdef DEBUG_NET
                fprintf(DEBUG_FILE_HANDLE, "upnp_teardown(): remove port forward for IP %s port %s\n",
                        local_host, upnp_local_port);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

                UPNP_DeletePortMapping(upnp_urls.controlURL,
                        upnp_igd_datas.servicetype,
                        upnp_local_port, "TCP", NULL);
                upnp_forwarded = Q_FALSE;
        }

        return Q_TRUE;
} /* ---------------------------------------------------------------------- */

/* Forward a port through a NAT using UPnP */
static Q_BOOL upnp_forward_port(int fd, int port) {
        struct sockaddr local_sockaddr;
        socklen_t local_sockaddr_length = sizeof(struct sockaddr);
        char my_local_host[NI_MAXHOST];
        int rc;

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "upnp_forward_port()\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        /*
         * Because this is a listening socket, my_local_host field will
         * end up pointing to 0.0.0.0.  I still need to call it though
         * to get the upnp_local_port set correctly.
         */
        getsockname(fd, &local_sockaddr, &local_sockaddr_length);
        getnameinfo(&local_sockaddr, local_sockaddr_length,
                my_local_host, sizeof(my_local_host),
                upnp_local_port, sizeof(upnp_local_port),
                NI_NUMERICHOST | NI_NUMERICSERV);

        /* Bring UPnP up as needed */
        if (upnp_init() != Q_TRUE) {
#ifdef DEBUG_NET
                fprintf(DEBUG_FILE_HANDLE, "upnp_forward_port(): failed to init UPnP\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                return Q_FALSE;
        }

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "upnp_forward_port(): local interface IP %s port %s\n",
                local_host, upnp_local_port);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        rc = UPNP_AddPortMapping(upnp_urls.controlURL,
                upnp_igd_datas.servicetype,
                upnp_local_port, upnp_local_port, local_host,
                "qodem", "TCP", NULL);

        if (rc != 0) {
#ifdef DEBUG_NET
                fprintf(DEBUG_FILE_HANDLE, "upnp_forward_port(): port forward failed: %d\n", rc);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

                /* Error forwarding port */
                return Q_FALSE;
        }

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "upnp_forward_port(): port forward OK\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        /* Save this string */
        sprintf(local_host_external_full, "[%s]:%s", upnp_external_address, upnp_local_port);

        /* Port forwarded OK */
        upnp_forwarded = Q_TRUE;
        return Q_TRUE;
} /* ---------------------------------------------------------------------- */

#endif /* Q_UPNP */

#ifdef Q_PDCURSES_WIN32

static Q_BOOL wsaStarted = Q_FALSE;

static Q_BOOL start_winsock() {
        int rc;
        char notify_message[DIALOG_MESSAGE_SIZE];

        if (wsaStarted == Q_TRUE) {
                return Q_TRUE;
        }

        WSADATA wsaData;
        /* Ask for Winsock 2.2 */
        rc = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (rc != 0) {
                /* Error starting Winsock */
                snprintf(notify_message, sizeof(notify_message),
                        _("Error calling WSAStartup(): %d (%s)"),
                        rc, strerror(rc));
                notify_form(notify_message, 0);
                return Q_FALSE;
        }

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "start_winsock() got version %d.%d\n",
                HIBYTE(wsaData.wVersion), LOBYTE(wsaData.wVersion));
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        /* All OK */
        wsaStarted = Q_TRUE;
        return Q_TRUE;
} /* ---------------------------------------------------------------------- */

void stop_winsock() {
        /* Ignore the return from WSACleanup() */
        WSACleanup();
        wsaStarted = Q_FALSE;
} /* ---------------------------------------------------------------------- */

#endif /* Q_PDCURSES_WIN32 */

#ifdef Q_PDCURSES_WIN32
int get_errno() {
        return WSAGetLastError();
} /* ---------------------------------------------------------------------- */

static void set_errno(int x) {
        WSASetLastError(x);
} /* ---------------------------------------------------------------------- */

static struct _wsaerrtext {
        int err;
        const char *errconst;
        const char *errdesc;
} _wsaerrtext[] = {

    {
        WSA_E_CANCELLED, "WSA_E_CANCELLED", "Lookup cancelled."
    },
    {
        WSA_E_NO_MORE, "WSA_E_NO_MORE", "No more data available."
    },
    {
        WSAEACCES, "WSAEACCES", "Permission denied."
    },
    {
        WSAEADDRINUSE, "WSAEADDRINUSE", "Address already in use."
    },
    {
        WSAEADDRNOTAVAIL, "WSAEADDRNOTAVAIL", "Cannot assign requested address."
    },
    {
        WSAEAFNOSUPPORT, "WSAEAFNOSUPPORT", "Address family not supported by protocol family."
    },
    {
        WSAEALREADY, "WSAEALREADY", "Operation already in progress."
    },
    {
        WSAEBADF, "WSAEBADF", "Bad file number."
    },
    {
        WSAECANCELLED, "WSAECANCELLED", "Operation cancelled."
    },
    {
        WSAECONNABORTED, "WSAECONNABORTED", "Software caused connection abort."
    },
    {
        WSAECONNREFUSED, "WSAECONNREFUSED", "Connection refused."
    },
    {
        WSAECONNRESET, "WSAECONNRESET", "Connection reset by peer."
    },
    {
        WSAEDESTADDRREQ, "WSAEDESTADDRREQ", "Destination address required."
    },
    {
        WSAEDQUOT, "WSAEDQUOT", "Disk quota exceeded."
    },
    {
        WSAEFAULT, "WSAEFAULT", "Bad address."
    },
    {
        WSAEHOSTDOWN, "WSAEHOSTDOWN", "Host is down."
    },
    {
        WSAEHOSTUNREACH, "WSAEHOSTUNREACH", "No route to host."
    },
    {
        WSAEINPROGRESS, "WSAEINPROGRESS", "Operation now in progress."
    },
    {
        WSAEINTR, "WSAEINTR", "Interrupted function call."
    },
    {
        WSAEINVAL, "WSAEINVAL", "Invalid argument."
    },
    {
        WSAEINVALIDPROCTABLE, "WSAEINVALIDPROCTABLE", "Invalid procedure table from service provider."
    },
    {
        WSAEINVALIDPROVIDER, "WSAEINVALIDPROVIDER", "Invalid service provider version number."
    },
    {
        WSAEISCONN, "WSAEISCONN", "Socket is already connected."
    },
    {
        WSAELOOP, "WSAELOOP", "Too many levels of symbolic links."
    },
    {
        WSAEMFILE, "WSAEMFILE", "Too many open files."
    },
    {
        WSAEMSGSIZE, "WSAEMSGSIZE", "Message too long."
    },
    {
        WSAENAMETOOLONG, "WSAENAMETOOLONG", "File name is too long."
    },
    {
        WSAENETDOWN, "WSAENETDOWN", "Network is down."
    },
    {
        WSAENETRESET, "WSAENETRESET", "Network dropped connection on reset."
    },
    {
        WSAENETUNREACH, "WSAENETUNREACH", "Network is unreachable."
    },
    {
        WSAENOBUFS, "WSAENOBUFS", "No buffer space available."
    },
    {
        WSAENOMORE, "WSAENOMORE", "No more data available."
    },
    {
        WSAENOPROTOOPT, "WSAENOPROTOOPT", "Bad protocol option."
    },
    {
        WSAENOTCONN, "WSAENOTCONN", "Socket is not connected."
    },
    {
        WSAENOTEMPTY, "WSAENOTEMPTY", "Directory is not empty."
    },
    {
        WSAENOTSOCK, "WSAENOTSOCK", "Socket operation on nonsocket."
    },
    {
        WSAEOPNOTSUPP, "WSAEOPNOTSUPP", "Operation not supported."
    },
    {
        WSAEPFNOSUPPORT, "WSAEPFNOSUPPORT", "Protocol family not supported."
    },
    {
        WSAEPROCLIM, "WSAEPROCLIM", "Too many processes."
    },
    {
        WSAEPROTONOSUPPORT, "WSAEPROTONOSUPPORT", "Protocol not supported."
    },
    {
        WSAEPROTOTYPE, "WSAEPROTOTYPE", "Protocol wrong type for socket."
    },
    {
        WSAEPROVIDERFAILEDINIT, "WSAEPROVIDERFAILEDINIT", "Unable to initialise a service provider."
    },
    {
        WSAEREFUSED, "WSAEREFUSED", "Refused."
    },
    {
        WSAEREMOTE, "WSAEREMOTE", "Too many levels of remote in path."
    },
    {
        WSAESHUTDOWN, "WSAESHUTDOWN", "Cannot send after socket shutdown."
    },
    {
        WSAESOCKTNOSUPPORT, "WSAESOCKTNOSUPPORT", "Socket type not supported."
    },
    {
        WSAESTALE, "WSAESTALE", "Stale NFS file handle."
    },
    {
        WSAETIMEDOUT, "WSAETIMEDOUT", "Connection timed out."
    },
    {
        WSAETOOMANYREFS, "WSAETOOMANYREFS", "Too many references."
    },
    {
        WSAEUSERS, "WSAEUSERS", "Too many users."
    },
    {
        WSAEWOULDBLOCK, "WSAEWOULDBLOCK", "Resource temporarily unavailable."
    },
    {
        WSANOTINITIALISED, "WSANOTINITIALISED", "Successful WSAStartup not yet performed."
    },
    {
        WSASERVICE_NOT_FOUND, "WSASERVICE_NOT_FOUND", "Service not found."
    },
    {
        WSASYSCALLFAILURE, "WSASYSCALLFAILURE", "System call failure."
    },
    {
        WSASYSNOTREADY, "WSASYSNOTREADY", "Network subsystem is unavailable."
    },
    {
        WSATYPE_NOT_FOUND, "WSATYPE_NOT_FOUND", "Class type not found."
    },
    {
        WSAVERNOTSUPPORTED, "WSAVERNOTSUPPORTED", "Winsock.dll version out of range."
    },
    {
        WSAEDISCON, "WSAEDISCON", "Graceful shutdown in progress."
    }
};

const char * get_strerror(int err) {
        /*
         * This function is taken from win32lib.c shipped by the Squid
         * Web Proxy Cache.  Squid is licensed GPL v2 or later.
         * win32lib.c's authors are listed below:
         *
         * Windows support
         * AUTHOR: Guido Serassio <serassio@squid-cache.org>
         * inspired by previous work by Romeo Anghelache & Eric Stern.
         *
         * SQUID Web Proxy Cache          http://www.squid-cache.org/
         * ----------------------------------------------------------
         */

        static char xwsaerror_buf[BUFSIZ];
        int i, errind = -1;
        if (err == 0)
                return "(0) No error.";
        for (i = 0; i < sizeof(_wsaerrtext) / sizeof(struct _wsaerrtext); i++) {
                if (_wsaerrtext[i].err != err)
                        continue;
                errind = i;
                break;
        }
        if (errind == -1)
                snprintf(xwsaerror_buf, BUFSIZ, "Unknown");
        else
                snprintf(xwsaerror_buf, BUFSIZ, "%s, %s", _wsaerrtext[errind].errconst, _wsaerrtext[errind].errdesc);
        return xwsaerror_buf;
} /* ---------------------------------------------------------------------- */

#else

int get_errno() {
        return errno;
} /* ---------------------------------------------------------------------- */

static void set_errno(int x) {
        errno = x;
} /* ---------------------------------------------------------------------- */

const char * get_strerror(int x) {
        return strerror(x);
} /* ---------------------------------------------------------------------- */

#endif /* Q_PDCURSES_WIN32 */

static const char * connect_host = NULL;
static const char * connect_port = NULL;

/* Connect to a remote system over TCP */
int net_connect_start(const char * host, const char * port) {
        /* Setup for getaddrinfo() */
        char notify_message[DIALOG_MESSAGE_SIZE];
        int rc;
        int fd = -1;
        struct addrinfo hints;
        struct addrinfo * address;
        struct addrinfo * local_address;
        struct addrinfo * p;
        int i;
        char local_port[NI_MAXSERV];
        char * message[2];

#ifdef DEBUG_NET
        if (DEBUG_FILE_HANDLE == NULL) {
                DEBUG_FILE_HANDLE = fopen("debug_net.txt", "w");
        }
        fprintf(DEBUG_FILE_HANDLE, "net_connect() : %s %s\n", host, port);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        assert(connected == Q_FALSE);

        /* Hang onto these for the call to net_connect_finish() */
        connect_host = host;
        connect_port = port;

        memset(&hints, 0, sizeof(struct addrinfo));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
#ifdef Q_PDCURSES_WIN32
        hints.ai_flags = AI_CANONNAME;
        start_winsock();
#else
        hints.ai_flags = AI_NUMERICSERV | AI_CANONNAME;
#endif /* Q_PDCURSES_WIN32 */

        /* Pop up connection notice, since this could take a while... */
        snprintf(notify_message, sizeof(notify_message),
                _("Looking up IP address for %s port %s..."),
                host, port);
        snprintf(q_dialer_modem_message, sizeof(q_dialer_modem_message),
                "%s", notify_message);
        q_screen_dirty = Q_TRUE;
        refresh_handler();

        /* Get the remote IP address */
        rc = getaddrinfo(host, port, &hints, &address);

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "net_connect() : getaddrinfo() rc %d\n", rc);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        if (rc != 0) {
                /* Error resolving name */
                /* snprintf(notify_message, sizeof(notify_message),
                        _("Error connecting to %s port %s: %s"),
                        host, port, get_strerror(get_errno())); */
                /* notify_form(notify_message, 0); */
                snprintf(notify_message, sizeof(notify_message),
                        _("Error: %s"),
#ifdef __BORLANDC__
                        get_strerror(rc));
#else
                        gai_strerror(rc));
#endif
                snprintf(q_dialer_modem_message, sizeof(q_dialer_modem_message),
                        "%s", notify_message);
                /* We failed to connect, cycle to the next */
                q_dial_state = Q_DIAL_LINE_BUSY;
                time(&q_dialer_cycle_start_time);
                q_screen_dirty = Q_TRUE;
                refresh_handler();
                return -1;
        }

        /* Loop through the results */
        rc = 0;
        for (p = address; p != NULL; p = p->ai_next) {
#ifdef DEBUG_NET
                fprintf(DEBUG_FILE_HANDLE, "net_connect() : p %p\n", p);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */


                fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
#ifdef DEBUG_NET
                fprintf(DEBUG_FILE_HANDLE, "net_connect() : socket() fd %d\n", fd);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

                if (fd == -1) {
                        continue;
                }

                if (q_status.dial_method == Q_DIAL_METHOD_RLOGIN) {
                        /*
                         * Rlogin only: bind to a "privileged" port (between
                         * 512 and 1023, inclusive)
                         */
                        for (i = 1023; i >= 512; i--) {
                                snprintf(local_port, sizeof(local_port),
                                        "%d", i);
                                hints.ai_family = p->ai_family;
                                hints.ai_socktype = SOCK_STREAM;
                                hints.ai_flags = AI_PASSIVE;
                                rc = getaddrinfo(NULL, local_port, &hints,
                                        &local_address);
                                if (rc != 0) {
                                        /*
                                         * Can't lookup on this local
                                         * interface ?
                                         */
                                        goto try_next_interface;
                                }
                                rc = bind(fd, local_address->ai_addr,
                                        local_address->ai_addrlen);
                                freeaddrinfo(local_address);

                                if (rc != 0) {
                                        /* Can't bind to this port */
                                        continue;
                                } else {
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "net_connect() : rlogin bound to port %d\n", i);
                                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                                        /*
                                         * We found a privileged port,
                                         * break out of the inner loop.
                                         */
                                        goto rlogin_bound_ok;
                                }
                        }

                        /*
                         * This is rlogin, and we failed to bind to the
                         * local privileged port
                         */
                        message[0] = _("Rlogin was unable to bind to a local privileged port.  Consider");
                        message[1] = _("setting use_external_rlogin=true in qodem configuration file.");
                        notify_form_long(message, 0, 2);
                        freeaddrinfo(address);

                        /* We failed to connect, cycle to the next */
                        q_dial_state = Q_DIAL_LINE_BUSY;
                        time(&q_dialer_cycle_start_time);
                        return -1;
                }

        rlogin_bound_ok:
                /* Attempt the connection */
                snprintf(notify_message, sizeof(notify_message),
                        _("Connecting to %s port %s..."),
                        host, port);
                snprintf(q_dialer_modem_message, sizeof(q_dialer_modem_message),
                        "%s", notify_message);
                q_screen_dirty = Q_TRUE;
                refresh_handler();
                set_nonblock(fd);
                pending = Q_TRUE;
                rc = connect(fd, p->ai_addr, p->ai_addrlen);
                break;

        try_next_interface:
                ;

        } /* for (p = address; p != NULL; p = p->ai_next) */

        /* At this point, fd is connect()'ing or failed. */
        freeaddrinfo(address);

        return fd;
} /* ---------------------------------------------------------------------- */

/* Connect to a remote system over TCP */
Q_BOOL net_connect_finish() {
        struct sockaddr remote_sockaddr;
        socklen_t remote_sockaddr_length = sizeof(struct sockaddr);
        int socket_errno;
        socklen_t socket_errno_length = sizeof(socket_errno);
        char notify_message[DIALOG_MESSAGE_SIZE];
        int rc = 0;

#ifdef Q_PDCURSES_WIN32
        rc = getsockopt(q_child_tty_fd, SOL_SOCKET, SO_ERROR,
                (char *)&socket_errno, &socket_errno_length);
#else
        rc = getsockopt(q_child_tty_fd, SOL_SOCKET, SO_ERROR,
                &socket_errno, &socket_errno_length);
#endif /* Q_PDCURSES_WIN32 */

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "net_connect_finish() : getsockopt() rc %d\n", rc);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
        if ((rc < 0) || (socket_errno != 0)) {
                /* Error connecting */
                if (rc == 0) {
                        /* If getsockopt() worked, report the socket error */
                        set_errno(socket_errno);
                }

                /* The last connection attempt failed */
                /* snprintf(notify_message, sizeof(notify_message),
                        _("Error connecting to %s port %s: %s"),
                        host, port, get_strerror(get_errno())); */
                /* notify_form(notify_message, 0); */
                snprintf(notify_message, sizeof(notify_message),
                        _("Error: %s"),
                        get_strerror(get_errno()));
                snprintf(q_dialer_modem_message, sizeof(q_dialer_modem_message),
                        "%s", notify_message);
                q_screen_dirty = Q_TRUE;
                refresh_handler();

#ifdef __BORLANDC__
                closesocket(q_child_tty_fd);
#else
                close(q_child_tty_fd);
#endif
                q_child_tty_fd = -1;

                /* We failed to connect, cycle to the next */
                q_dial_state = Q_DIAL_LINE_BUSY;
                time(&q_dialer_cycle_start_time);
                /* Don't call me again */
                pending = Q_FALSE;
                return Q_FALSE;
        }

        /* We connected ok. */
        getpeername(q_child_tty_fd, &remote_sockaddr, &remote_sockaddr_length);
        getnameinfo(&remote_sockaddr, remote_sockaddr_length,
                remote_host, sizeof(remote_host),
                remote_port, sizeof(remote_port),
                NI_NUMERICHOST | NI_NUMERICSERV);

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "net_connect() : connected.  Remote host is %s %s\n", remote_host, remote_port);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

#ifdef Q_LIBSSH2
        /* SSH has its session management here */
        if (q_status.dial_method == Q_DIAL_METHOD_SSH) {
                rc = ssh_setup_connection(q_child_tty_fd,
                        connect_host, connect_port);
                if (rc == -1) {
                        /* There was an error connecting, break out */
                        close(q_child_tty_fd);
                        q_child_tty_fd = -1;

                        /* We failed to connect, cycle to the next */
                        q_dial_state = Q_DIAL_LINE_BUSY;
                        time(&q_dialer_cycle_start_time);

                        snprintf(notify_message, sizeof(notify_message),
                                _("Error: Failed to negotiate SSH connection"));
                        snprintf(q_dialer_modem_message,
                                sizeof(q_dialer_modem_message),
                                "%s", notify_message);
                        q_screen_dirty = Q_TRUE;
                        refresh_handler();

                        /* Don't call me again */
                        pending = Q_FALSE;
                        return Q_FALSE;
                }
        }
#endif /* Q_LIBSSH2 */

        /* Reset connection state machine */
        state = INIT;
        memset(read_buffer, 0, sizeof(read_buffer));
        read_buffer_n = 0;
        memset(write_buffer, 0, sizeof(write_buffer));
        write_buffer_n = 0;
        reset_nvt();

        /*
         * Drop the connected message on the receive buffer.  We explicitly
         * do CRLF here.
         */
        snprintf((char *)read_buffer, sizeof(read_buffer),
                _("Connected to %s:%s...\r\n"), remote_host, remote_port);
        read_buffer_n = strlen((char *)read_buffer);

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "net_connect() : CONNECTED OK\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        connected = Q_TRUE;

        if (q_status.dial_method == Q_DIAL_METHOD_RLOGIN) {
                /*
                 * Rlogin special case: immediately send login header.
                 */
                rlogin_send_login(q_child_tty_fd);
                state = SENT_LOGIN;
        }

        /* Wrap up the connection logic */
        dial_success();
        /*
         * Cheat on the dialer time so we only display the
         * CONNECTED message for 1 second instead of 3.
         */
        q_dialer_cycle_start_time -= 2;

        /* Don't call me again */
        pending = Q_FALSE;
        return Q_TRUE;
} /* ---------------------------------------------------------------------- */

/* Listen for a remote connection over TCP */
int net_listen(const char * port) {
        /* Setup for getaddrinfo() */
        char notify_message[DIALOG_MESSAGE_SIZE];
        int rc;
        int fd = -1;
        struct addrinfo hints;
        struct addrinfo * address;
        struct addrinfo * local_address;
        struct addrinfo * p;
        struct sockaddr local_sockaddr;
        socklen_t local_sockaddr_length = sizeof(struct sockaddr);
        char local_port[NI_MAXSERV];
        Q_BOOL find_port_number = Q_FALSE;
        int port_number;

#ifdef Q_UPNP
        Q_BOOL upnp = Q_FALSE;

        /* Try up to three times to find an open port */
        int upnp_tries = 3;
#endif /* Q_UPNP */

#ifdef DEBUG_NET
        if (DEBUG_FILE_HANDLE == NULL) {
                DEBUG_FILE_HANDLE = fopen("debug_net.txt", "w");
        }
        fprintf(DEBUG_FILE_HANDLE, "net_listen() : %s\n", port);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        assert(listening == Q_FALSE);
        assert(connected == Q_FALSE);

        memset(&hints, 0, sizeof(struct addrinfo));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;

#ifdef Q_PDCURSES_WIN32
        start_winsock();
#endif /* Q_PDCURSES_WIN32 */

        if (strcmp(port, NEXT_AVAILABLE_PORT_STRING) == 0) {
                /*
                 * Find the next available port number.  We'll count down from
                 * the top (65535).
                 */
                port_number = 65535;
                find_port_number = Q_TRUE;
                sprintf(local_port, "%d", port_number);
                rc = getaddrinfo(NULL, local_port, &hints, &address);
#ifdef Q_UPNP
        } else if (strcmp(port, UPNP_PORT_STRING) == 0) {
                /*
                 * Find an available port number from UPnP
                 */
                port_number = 65535;
                find_port_number = Q_TRUE;
                upnp = Q_TRUE;
                sprintf(local_port, "%d", port_number);
                rc = getaddrinfo(NULL, local_port, &hints, &address);
#endif /* Q_UPNP */
        } else {
                memset(local_port, 0, sizeof(local_port));
                snprintf(local_port, sizeof(local_port) - 1, "%s", port);
                rc = getaddrinfo(NULL, port, &hints, &address);
        }

        if (rc < 0) {
                /* Error resolving port information */
                snprintf(notify_message, sizeof(notify_message),
                        _("Error converting port string %s to socket: %s"),
#ifdef __BORLANDC__
                        port, get_strerror(get_errno()));
#else
                        port, gai_strerror(get_errno()));
#endif
                notify_form(notify_message, 0);
                return -1;
        }

        /* Loop through the results */
        rc = 0;
        for (p = address; p != NULL; p = p->ai_next) {
                fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
                if (fd == -1) {
                        continue;
                }

                if (find_port_number == Q_TRUE) {
                        for (;;) {
                                /* Pick a random port between 2048 and 65535 */
#ifdef Q_PDCURSES_WIN32
                                port_number = (rand() % (65535 - 2048)) + 2048;
#else
                                port_number = (random() % (65535 - 2048)) + 2048;
#endif /* Q_PDCURSES_WIN32 */

#ifdef DEBUG_NET
                                fprintf(DEBUG_FILE_HANDLE, "net_listen() : attempt to bind to port %d\n", port_number);
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                                snprintf(local_port, sizeof(local_port),
                                        "%d", port_number);
                                hints.ai_family = p->ai_family;
                                hints.ai_socktype = SOCK_STREAM;
                                hints.ai_flags = AI_PASSIVE;
                                rc = getaddrinfo(NULL, local_port, &hints,
                                        &local_address);

                                if (rc != 0) {
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "net_listen() : getaddrinfo() error: %d %s\n",
#ifdef __BORLANDC__
                                                get_errno(), get_strerror(get_errno()));
#else
                                                get_errno(), gai_strerror(get_errno()));
#endif
                                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                                        /*
                                         * Can't lookup on this local
                                         * interface ?
                                         */
                                        freeaddrinfo(local_address);
                                        goto try_next_interface;
                                }

                                freeaddrinfo(local_address);
                                rc = bind(fd, local_address->ai_addr,
                                        local_address->ai_addrlen);

                                if (rc != 0) {
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "net_listen() : bind() error: %d %s\n",
                                                get_errno(), get_strerror(get_errno()));
                                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

#ifdef Q_PDCURSES_WIN32
                                        if (get_errno() == WSAEADDRINUSE) {
#else
                                        if (get_errno() == EADDRINUSE) {
#endif /* Q_PDCURSES_WIN32 */
                                                /* Can't bind to this port, look for another one */
                                                continue;
                                        } else {
                                                /* Another error, bail out of this interface */
                                                goto try_next_interface;
                                        }
                                } else {
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "net_listen() : bound to port %d\n", port_number);
                                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */


#ifdef Q_UPNP
                                        if (upnp == Q_TRUE) {
                                                /*
                                                 * Try to open this port on
                                                 * the NAT remote side.
                                                 */
                                                if (upnp_forward_port(fd, port_number) == Q_FALSE) {
                                                        upnp_tries--;
                                                        if (upnp_tries == 0) {
                                                                /* Hit max retries */
                                                                snprintf(notify_message, sizeof(notify_message),
                                                                        _("Cannot open a port through UPnP"));
                                                                notify_form(notify_message, 0);
                                                                return -1;
                                                        }

                                                        /* See if UPnP is working at all */
                                                        if (upnp_is_initted == Q_FALSE) {
                                                                snprintf(notify_message, sizeof(notify_message),
                                                                        _("Cannot communicate with gateway through UPnP"));
                                                                notify_form(notify_message, 0);
                                                                return -1;
                                                        }

                                                        /*
                                                         * Try another port.  We have to close
                                                         * and re-open the socket though because
                                                         * the bind() call earlier was successful.
                                                         */
                                                        close(fd);
                                                        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
                                                        if (fd == -1) {
                                                                goto try_next_interface;
                                                        }
                                                        continue;
                                                }
#ifdef DEBUG_NET
                                                fprintf(DEBUG_FILE_HANDLE, "net_listen() : UPnP OK\n");
                                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                                        }

#endif /* Q_UPNP */

                                        /*
                                         * We found an open port,
                                         * break out of the inner loop.
                                         */
                                        goto listen_info;
                                }
                        } /* for (;;) */

                } else {
                        /*
                         * Try the port they asked for, if it didn't work then
                         * tough.
                         */
                        rc = bind(fd, p->ai_addr, p->ai_addrlen);
                }

                if (rc != 0) {
                        /* Can't bind to this port */
                        continue;
                }

        listen_info:

                getsockname(fd, &local_sockaddr, &local_sockaddr_length);
                getnameinfo(&local_sockaddr, local_sockaddr_length,
                        local_host, sizeof(local_host),
                        local_port, sizeof(local_port),
                        NI_NUMERICHOST | NI_NUMERICSERV);
#ifdef DEBUG_NET
                fprintf(DEBUG_FILE_HANDLE, "net_listen() : bound to IP %s port %s\n",
                        local_host, local_port);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                goto listen_bound_ok;

        try_next_interface:
#ifdef DEBUG_NET
                fprintf(DEBUG_FILE_HANDLE, "net_listen() : try next interface\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                ;
        } /* for (p = address; p != NULL; p = p->ai_next) */

        if (rc < 0) {
                /* The last bind attempt failed */
                snprintf(notify_message, sizeof(notify_message),
                        _("Error bind()'ing to port %s: %s"),
                        port, get_strerror(get_errno()));
                notify_form(notify_message, 0);
                return -1;
        }

listen_bound_ok:

        /* At this point, fd is bound. */
        freeaddrinfo(address);

        /* Now make fd listen() */
        rc = listen(fd, 5);
        if (rc < 0) {
                /* The last bind attempt failed */
                snprintf(notify_message, sizeof(notify_message),
                        _("Error listen()'ing on port %s: %s"),
                        local_port, get_strerror(get_errno()));
                notify_form(notify_message, 0);
                return -1;
        }

        /* Make fd non-blocking */
        set_nonblock(fd);

        /* Save this string */
        sprintf(local_host_full, "[%s]:%s", local_host, local_port);

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "net_listen() : return fd = %d\n", fd);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        listening = Q_TRUE;
        listen_fd = fd;
        return fd;
} /* ---------------------------------------------------------------------- */

/* Return TCP listener address/port in human-readable form */
const char * net_listen_string() {
        return local_host_full;
} /* ---------------------------------------------------------------------- */

/*
 * See if we have a new connection.  Returns -1 if there is no new
 * connection.
 */
int net_accept() {
        char notify_message[DIALOG_MESSAGE_SIZE];
        int fd = -1;
        struct sockaddr remote_sockaddr;
        socklen_t remote_sockaddr_length = sizeof(struct sockaddr);
        struct sockaddr local_sockaddr;
        socklen_t local_sockaddr_length = sizeof(struct sockaddr);
        char local_port[NI_MAXSERV];

        fd = accept(listen_fd, &remote_sockaddr, &remote_sockaddr_length);
        if (fd < 0) {

#ifdef Q_PDCURSES_WIN32
                if ((get_errno() == EAGAIN) || (get_errno() == WSAEWOULDBLOCK)) {
#else
                if ((get_errno() == EAGAIN) || (get_errno() == EWOULDBLOCK)) {
#endif /* Q_PDCURSES_WIN32 */
                        /* No one is there, return cleanly */
                        return -1;
                }

                /* The last bind attempt failed */
                snprintf(notify_message, sizeof(notify_message),
                        _("Error in accept(): %s"),
                        get_strerror(get_errno()));
                notify_form(notify_message, 1.5);
                return -1;
        }

        /* We connected ok. */
        getpeername(fd, &remote_sockaddr, &remote_sockaddr_length);
        getnameinfo(&remote_sockaddr, remote_sockaddr_length,
                remote_host, sizeof(remote_host),
                remote_port, sizeof(remote_port),
                NI_NUMERICHOST | NI_NUMERICSERV);

        getsockname(fd, &local_sockaddr, &local_sockaddr_length);
        getnameinfo(&local_sockaddr, local_sockaddr_length,
                local_host, sizeof(local_host),
                local_port, sizeof(local_port),
                NI_NUMERICHOST | NI_NUMERICSERV);

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "net_accept() : connected.\n");
        fprintf(DEBUG_FILE_HANDLE, "             Remote host is %s %s\n", remote_host, remote_port);
        fprintf(DEBUG_FILE_HANDLE, "             Local host is  %s %s\n", local_host, local_port);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        connected = Q_TRUE;

        /* Reset connection state machine */
        state = INIT;
        memset(read_buffer, 0, sizeof(read_buffer));
        read_buffer_n = 0;
        memset(write_buffer, 0, sizeof(write_buffer));
        write_buffer_n = 0;
        reset_nvt();

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "net_accept() : return fd = %d\n", fd);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        return fd;
} /* ---------------------------------------------------------------------- */

/* Close TCP connection.  If not connected, this is NOP. */
void net_close() {
#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "net_close()\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        if (connected == Q_FALSE) {
                return;
        }

        assert(q_child_tty_fd != -1);

#ifdef Q_LIBSSH2
        /* SSH needs to destroy the crypto session */
        if (q_status.dial_method == Q_DIAL_METHOD_SSH) {
                ssh_close();
        }
#endif /* Q_LIBSSH2 */

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "net_close() : shutdown(q_child_tty_fd, SHUT_RDWR)\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        /*
         * All we do is shutdown().  read() will return 0 when the remote side
         * close()s.
         */
#ifdef Q_PDCURSES_WIN32
        shutdown(q_child_tty_fd, SD_BOTH);
#else
        shutdown(q_child_tty_fd, SHUT_RDWR);
#endif /* Q_PDCURSES_WIN32 */
        connected = Q_FALSE;

#ifdef Q_UPNP
        if (upnp_is_initted == Q_TRUE) {
                upnp_teardown();
        }
#endif /* Q_UPNP */
} /* ---------------------------------------------------------------------- */

/* Close TCP listener socket */
void net_listen_close() {
#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "net_listen_close()\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        if (listening == Q_FALSE) {
                return;
        }

#ifdef Q_PDCURSES_WIN32
        /* Win32 case */

#else

        /* Normal case */
        assert(listen_fd != -1);

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "net_listen_close() : close(listen_fd)\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
        close(listen_fd);

#endif /* Q_PDCURSES_WIN32 */

        listening = Q_FALSE;
} /* ---------------------------------------------------------------------- */

/*
 * Send raw bytes to the other side.
 */
ssize_t raw_write(const int fd, void * buf, size_t count) {
        int count_original = count;
        int rc;
        do {
#ifdef Q_PDCURSES_WIN32
                rc = send(fd, (const char *)buf, count, 0);
#else
                rc = write(fd, buf, count);
#endif /* Q_PDCURSES_WIN32 */

                if (rc <= 0) {
#ifdef DEBUG_NET
                        fprintf(DEBUG_FILE_HANDLE, "raw_write() : error on write(): %s\n", get_strerror(get_errno()));
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                        switch (get_errno()) {
                        case EAGAIN:
                                /* Keep trying, this is a busy spin loop */
                                break;
                        default:
                                /* Unknown, bail out */
                                return rc;
                        }
                } else {
#ifdef DEBUG_NET
                        int i;
                        fprintf(DEBUG_FILE_HANDLE, "raw_write() : sent %d bytes: ", rc);
                        for (i = 0; i < rc; i++) {
                                fprintf(DEBUG_FILE_HANDLE, "%02x ", ((char *)buf)[i]);
                        }
                        fprintf(DEBUG_FILE_HANDLE, "\n");
                        fprintf(DEBUG_FILE_HANDLE, "                             ");
                        for (i = 0; i < rc; i++) {
                                fprintf(DEBUG_FILE_HANDLE, "%c  ", ((char *)buf)[i]);
                        }
                        fprintf(DEBUG_FILE_HANDLE, "\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                        count -= rc;
                }
        } while (count > 0);

        /* Everything got pushed out successfully */
        return count_original;
} /* ---------------------------------------------------------------------- */

/*
 * Get raw bytes from the other side.
 */
ssize_t raw_read(const int fd, void * buf, size_t count) {
        int rc;
        int total = 0;
        size_t max_read;

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "raw_read() : %d bytes in read_buffer\n", read_buffer_n);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        /* Perform the raw read */
        if (count == 0) {
                /* NOP */
                return 0;
        }

        if (nvt.is_eof == Q_TRUE) {
#ifdef DEBUG_NET
                fprintf(DEBUG_FILE_HANDLE, "raw_read() : no read because EOF\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

                /* Let the caller know this is now closed */
                set_errno(EIO);
                return -1;
        } else {

                max_read = sizeof(read_buffer) - read_buffer_n;
                if (max_read > count) {
                        max_read = count;
                }

                /* Read some data from the other end */
#ifdef Q_PDCURSES_WIN32
                rc = recv(fd, (char *)read_buffer + read_buffer_n, max_read, 0);
#else
                rc = read(fd, read_buffer + read_buffer_n, max_read);
#endif /* Q_PDCURSES_WIN32 */

#ifdef DEBUG_NET
                int i;
                fprintf(DEBUG_FILE_HANDLE, "raw_read() : read %d bytes:\n", rc);
                for (i = 0; i < rc; i++) {
                        fprintf(DEBUG_FILE_HANDLE, " %02x", (read_buffer[read_buffer_n + i] & 0xFF));
                }
                fprintf(DEBUG_FILE_HANDLE, "\n");
                for (i = 0; i < rc; i++) {
                        if ((read_buffer[read_buffer_n + i] & 0xFF) >= 0x80) {
                                fprintf(DEBUG_FILE_HANDLE, " %02x", (read_buffer[read_buffer_n + i] & 0xFF));
                        } else {
                                fprintf(DEBUG_FILE_HANDLE, " %c ", (read_buffer[read_buffer_n + i] & 0xFF));
                        }
                }
                fprintf(DEBUG_FILE_HANDLE, "\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                /* Check for EOF or error */
                if (rc < 0) {
                        if (read_buffer_n == 0) {
                                /* Something bad happened, just return it */
                                return rc;
                        }
                } else if (rc == 0) {
                        /* EOF - Drop a connection close message */
                        nvt.is_eof = Q_TRUE;
                } else {
                        /* More data came in */
                        read_buffer_n += rc;
                }
        } /* if (nvt.is_eof == Q_TRUE) */

        if ((read_buffer_n == 0) && (nvt.eof_msg == Q_TRUE)) {
                /* We are done, return the final EOF */
                return 0;
        }

        if ((read_buffer_n == 0) && (nvt.is_eof == Q_TRUE)) {
                /* EOF - Drop "Connection closed." */
                if (q_program_state != Q_STATE_HOST) {
                        snprintf((char *)read_buffer, sizeof(read_buffer), "%s",
                                _("Connection closed.\r\n"));
                        read_buffer_n = strlen((char *)read_buffer);
                }
                nvt.eof_msg = Q_TRUE;
        }

        /* Copy the bytes raw to the other side */
        memcpy(buf, read_buffer, read_buffer_n);
        total = read_buffer_n;

        /* Return bytes read */
#ifdef DEBUG_NET
        int i;
        fprintf(DEBUG_FILE_HANDLE, "raw_read() : send %d bytes to caller:\n", (int)total);
        for (i = 0; i < total; i++) {
                fprintf(DEBUG_FILE_HANDLE, " %02x", (((char *)buf)[i] & 0xFF));
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        for (i = 0; i < total; i++) {
                if ((((char *)buf)[i] & 0xFF) >= 0x80) {
                        fprintf(DEBUG_FILE_HANDLE, " %02x", (((char *)buf)[i] & 0xFF));
                } else {
                        fprintf(DEBUG_FILE_HANDLE, " %c ", (((char *)buf)[i] & 0xFF));
                }
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        /* read_buffer is always fully consumed */
        read_buffer_n = 0;

        if (total == 0) {
#ifdef DEBUG_NET
                fprintf(DEBUG_FILE_HANDLE, "raw_read() : EAGAIN\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                /* We consumed everything, but it's not EOF.  Return EAGAIN. */
#ifdef Q_PDCURSES_WIN32
                set_errno(WSAEWOULDBLOCK);
#else
                set_errno(EAGAIN);
#endif /* Q_PDCURSES_WIN32 */
                return -1;
        }

        return total;
} /* ---------------------------------------------------------------------- */

#ifdef DEBUG_NET
/*
 * For debugging, return a descriptive string for this telnet option.
 * These are pulled from: http://www.iana.org/assignments/telnet-options
 */
static const char * telnet_option_string(const unsigned char option) {
        switch (option) {
        case 0: return "Binary Transmission";
        case 1: return "Echo";
        case 2: return "Reconnection";
        case 3: return "Suppress Go Ahead";
        case 4: return "Approx Message Size Negotiation";
        case 5: return "Status";
        case 6: return "Timing Mark";
        case 7: return "Remote Controlled Trans and Echo";
        case 8: return "Output Line Width";
        case 9: return "Output Page Size";
        case 10: return "Output Carriage-Return Disposition";
        case 11: return "Output Horizontal Tab Stops";
        case 12: return "Output Horizontal Tab Disposition";
        case 13: return "Output Formfeed Disposition";
        case 14: return "Output Vertical Tabstops";
        case 15: return "Output Vertical Tab Disposition";
        case 16: return "Output Linefeed Disposition";
        case 17: return "Extended ASCII";
        case 18: return "Logout";
        case 19: return "Byte Macro";
        case 20: return "Data Entry Terminal";
        case 21: return "SUPDUP";
        case 22: return "SUPDUP Output";
        case 23: return "Send Location";
        case 24: return "Terminal Type";
        case 25: return "End of Record";
        case 26: return "TACACS User Identification";
        case 27: return "Output Marking";
        case 28: return "Terminal Location Number";
        case 29: return "Telnet 3270 Regime";
        case 30: return "X.3 PAD";
        case 31: return "Negotiate About Window Size";
        case 32: return "Terminal Speed";
        case 33: return "Remote Flow Control";
        case 34: return "Linemode";
        case 35: return "X Display Location";
        case 36: return "Environment Option";
        case 37: return "Authentication Option";
        case 38: return "Encryption Option";
        case 39: return "New Environment Option";
        case 40: return "TN3270E";
        case 41: return "XAUTH";
        case 42: return "CHARSET";
        case 43: return "Telnet Remote Serial Port (RSP)";
        case 44: return "Com Port Control Option";
        case 45: return "Telnet Suppress Local Echo";
        case 46: return "Telnet Start TLS";
        case 47: return "KERMIT";
        case 48: return "SEND-URL";
        case 49: return "FORWARD_X";
        case 138: return "TELOPT PRAGMA LOGON";
        case 139: return "TELOPT SSPI LOGON";
        case 140: return "TELOPT PRAGMA HEARTBEAT";
        case 255: return "Extended-Options-List";
        default:
                if ((option >= 50) && (option <= 137)) {
                        return "Unassigned";
                }
                return "UNKNOWN - OTHER";
        }

} /* ---------------------------------------------------------------------- */
#endif /* DEBUG_NET */

/* Telnet server/client is in ASCII mode */
Q_BOOL telnet_is_ascii() {
        if (net_is_connected() == Q_FALSE) {
                return Q_FALSE;
        }
        if (q_status.dial_method != Q_DIAL_METHOD_TELNET) {
                return Q_FALSE;
        }
        if (nvt.binary_mode == Q_TRUE) {
                return Q_FALSE;
        }
        return Q_TRUE;
} /* ---------------------------------------------------------------------- */

/*
 * Send a DO/DON'T/WILL/WON'T response to the remote side.  Response is
 * one of TELNET_DO, TELNET_DON'T, TELNET_WILL, or TELNET_WON'T.  Option is
 * the telnet option in question.
 */
static void telnet_respond(const int fd, unsigned char response, unsigned char option) {
        int n;
        unsigned char buffer[3];
        buffer[0] = TELNET_IAC;
        buffer[1] = response;
        buffer[2] = option;
        n = 3;
#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "telnet_respond() : ");
        switch (response) {
        case TELNET_DO:
                fprintf(DEBUG_FILE_HANDLE, "DO %s\n",
                        telnet_option_string(option));
                break;
        case TELNET_DONT:
                fprintf(DEBUG_FILE_HANDLE, "DONT %s\n",
                        telnet_option_string(option));
                break;
        case TELNET_WILL:
                fprintf(DEBUG_FILE_HANDLE, "WILL %s\n",
                        telnet_option_string(option));
                break;
        case TELNET_WONT:
                fprintf(DEBUG_FILE_HANDLE, "WONT %s\n",
                        telnet_option_string(option));
                break;
        }
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
        raw_write(fd, buffer, n);
} /* ---------------------------------------------------------------------- */

/*
 * Tell the remote side we WILL support an option.
 */
static void telnet_will(const int fd, unsigned char option) {
        telnet_respond(fd, TELNET_WILL, option);
} /* ---------------------------------------------------------------------- */

/*
 * Tell the remote side we WON'T support an option.
 */
static void telnet_wont(const int fd, unsigned char option) {
        telnet_respond(fd, TELNET_WONT, option);
} /* ---------------------------------------------------------------------- */

/*
 * Tell the remote side we DO support an option.
 */
static void telnet_do(const int fd, unsigned char option) {
        telnet_respond(fd, TELNET_DO, option);
} /* ---------------------------------------------------------------------- */

/*
 * Tell the remote side we DON'T support an option.
 */
static void telnet_dont(const int fd, unsigned char option) {
        telnet_respond(fd, TELNET_DONT, option);
} /* ---------------------------------------------------------------------- */

/*
 * Tell the remote side we WON't or DON'T support an option.
 */
static void telnet_refuse(unsigned char remote_query, const int fd,
        unsigned char option) {

        if (remote_query == TELNET_DO) {
                telnet_wont(fd, option);
        } else {
                telnet_dont(fd, option);
        }
} /* ---------------------------------------------------------------------- */

/*
 * Telnet option: Terminal Speed
 */
static void telnet_send_subneg_response(const int fd,
        const unsigned char option, const unsigned char * response,
        const int response_n) {

        unsigned char buffer[SUBNEG_BUFFER_MAX + 5];
        assert(response_n <= SUBNEG_BUFFER_MAX);
        buffer[0] = TELNET_IAC;
        buffer[1] = TELNET_SB;
        buffer[2] = option;
        memcpy(buffer + 3, response, response_n);
        buffer[response_n + 3] = TELNET_IAC;
        buffer[response_n + 4] = TELNET_SE;
        raw_write(fd, buffer, response_n + 5);
} /* ---------------------------------------------------------------------- */

/*
 * Telnet option: Terminal Speed
 */
static void telnet_send_terminal_speed(const int fd) {
        char * response = "\0" "38400,38400";
        telnet_send_subneg_response(fd, 32, (unsigned char *)response, 12);
} /* ---------------------------------------------------------------------- */

/*
 * Telnet option: Terminal Type
 */
static void telnet_send_terminal_type(const int fd) {
        char response[SUBNEG_BUFFER_MAX];
        int response_n = 0;

        /* "IS" */
        response[response_n] = 0;
        response_n++;

        /* TERM */
        snprintf(response + response_n, sizeof(response) - response_n, "%s", dialer_get_term());
        response_n += strlen(dialer_get_term());

        telnet_send_subneg_response(fd, 24, (unsigned char *)response, response_n);
} /* ---------------------------------------------------------------------- */

/*
 * Telnet option: New Environment.  We send:
 *     TERM
 *     LANG
 */
static void telnet_send_environment(const int fd) {
        char response[SUBNEG_BUFFER_MAX];
        int response_n = 0;

        /* "IS" */
        response[response_n] = 0;
        response_n++;

        /* TERM */
        response[response_n] = 3;               /* "USERVAR" */
        response_n++;
        snprintf(response + response_n, sizeof(response) - response_n, "TERM");
        response_n += 4;
        response[response_n] = 1;               /* "VALUE" */
        response_n++;
        snprintf(response + response_n, sizeof(response) - response_n,
                "%s", dialer_get_term());
        response_n += strlen(dialer_get_term());

        /* LANG */
        response[response_n] = 3;               /* "USERVAR" */
        response_n++;
        snprintf(response + response_n, sizeof(response) - response_n, "LANG");
        response_n += 4;
        response[response_n] = 1;               /* "VALUE" */
        response_n++;
        snprintf(response + response_n, sizeof(response) - response_n,
                "%s", dialer_get_lang());
        response_n += strlen(dialer_get_lang());

        telnet_send_subneg_response(fd, 39, (unsigned char *)response,
                response_n);
} /* ---------------------------------------------------------------------- */

/*
 * Send the options we want to negotiate on:
 *     Binary Transmission
 *     Suppress Go Ahead
 *     Negotiate About Window Size
 *     Terminal Type
 *     New Environment
 *
 * When run as a server:
 *     Echo
 */
static void telnet_send_options(const int fd) {
        if (nvt.binary_mode == Q_FALSE) {
                /* Binary Transmission: must ask both do and will */
                telnet_do(fd, 0);
                telnet_will(fd, 0);
        }

        if (nvt.go_ahead == Q_TRUE) {
                /* Suppress Go Ahead */
                telnet_do(fd, 3);
                telnet_will(fd, 3);
        }

        if (q_program_state == Q_STATE_HOST) {
                /* Server only options */

                /* Enable Echo - I echo to them, they do not echo back to me. */
                telnet_dont(fd, 1);
                telnet_will(fd, 1);
                return;
        }

        /* Client only options */
        if (nvt.do_naws == Q_FALSE) {
                /* NAWS - we need to use WILL instead of DO */
                telnet_will(fd, 31);
        }

        if (nvt.do_term_type == Q_FALSE) {
                /* Terminal Type - we need to use WILL instead of DO */
                telnet_will(fd, 24);
        }

        if (nvt.do_environment == Q_FALSE) {
                /* New Environment - we need to use WILL instead of DO */
                telnet_will(fd, 39);
        }
} /* ---------------------------------------------------------------------- */

/*
 * Send our window size to the remote side.
 */
static void telnet_send_naws(const int fd, const int lines, const int columns) {
        unsigned char buffer[16];
        unsigned int n = 0;
        buffer[n] = TELNET_IAC;
        n++;
        buffer[n] = TELNET_SB;
        n++;
        buffer[n] = 31;
        n++;
        buffer[n] = columns / 256;
        n++;
        if (buffer[n - 1] == TELNET_IAC) {
                buffer[n] = TELNET_IAC;
                n++;
        }
        buffer[n] = columns % 256;
        n++;
        if (buffer[n - 1] == TELNET_IAC) {
                buffer[n] = TELNET_IAC;
                n++;
        }

        buffer[n] = lines / 256;
        n++;
        if (buffer[n - 1] == TELNET_IAC) {
                buffer[n] = TELNET_IAC;
                n++;
        }
        buffer[n] = lines % 256;
        n++;
        if (buffer[n - 1] == TELNET_IAC) {
                buffer[n] = TELNET_IAC;
                n++;
        }
        buffer[n] = TELNET_IAC;
        n++;
        buffer[n] = TELNET_SE;
        n++;
        raw_write(fd, buffer, n);
} /* ---------------------------------------------------------------------- */

/* Send new screen dimensions to the remote side */
void telnet_resize_screen(const int lines, const int columns) {

        if (connected == Q_FALSE) {
                return;
        }

        if (nvt.do_naws == Q_FALSE) {
                /* We can't do this because the server refuses to handle it */
                return;
        }

        /* Send the new dimensions */
        assert(q_child_tty_fd != -1);

        telnet_send_naws(q_child_tty_fd, lines, columns);
} /* ---------------------------------------------------------------------- */

/*
 * Handle an option "sub-negotiation".
 */
static void handle_subneg(const int fd) {
        unsigned char option;

        /* Sanity check: there must be at least 1 byte in subneg_buffer */
        if (subneg_buffer_n < 1) {
#ifdef DEBUG_NET
                fprintf(DEBUG_FILE_HANDLE, "handle_subneg() : BUFFER TOO SMALL!  The other side is a broken telnetd, it did not send the right sub-negotiation data.\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                return;
        }
        option = subneg_buffer[0];

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "handle_subneg() : %s\n", telnet_option_string(option));
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        switch (option) {
        case 24:
                /* Terminal Type */
                if ((subneg_buffer_n > 1) && (subneg_buffer[1] == 1)) {
                        /* Server sent "SEND", we say "IS" */
                        telnet_send_terminal_type(fd);
                }
                break;
        case 32:
                /* Terminal Speed */
                if ((subneg_buffer_n > 1) && (subneg_buffer[1] == 1)) {
                        /* Server sent "SEND", we say "IS" */
                        telnet_send_terminal_speed(fd);
                }
                break;
        case 39:
                /* New Environment Option */
                if ((subneg_buffer_n > 1) && (subneg_buffer[1] == 1)) {
                        /*
                         * Server sent "SEND", we send the environment
                         * (ignoring any specific variables it asked for)
                         */
                        telnet_send_environment(fd);
                }
                break;
        }

} /* ---------------------------------------------------------------------- */

/* Just like read(), but perform the telnet protocol */
ssize_t telnet_read(const int fd, void * buf, size_t count) {
        unsigned char ch;
        int i;
        int rc;
        int total = 0;
        size_t max_read;

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "telnet_read() : %d bytes in read_buffer\n", read_buffer_n);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        if (state == INIT) {
                /*
                 * Start the telnet protocol negotiation.
                 */
                telnet_send_options(fd);
                state = SENT_OPTIONS;
        }

        /* Perform the raw read */
        if (count == 0) {
                /* NOP */
                return 0;
        }

        if (nvt.is_eof == Q_TRUE) {
#ifdef DEBUG_NET
                fprintf(DEBUG_FILE_HANDLE, "telnet_read() : no read because EOF\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                /* Do nothing */

        } else {

                max_read = sizeof(read_buffer) - read_buffer_n;
                if (max_read > count) {
                        max_read = count;
                }

                /* Read some data from the other end */
#ifdef Q_PDCURSES_WIN32
                rc = recv(fd, (char *)read_buffer + read_buffer_n, max_read, 0);
#else
                rc = read(fd, read_buffer + read_buffer_n, max_read);
#endif /* Q_PDCURSES_WIN32 */

#ifdef DEBUG_NET
                fprintf(DEBUG_FILE_HANDLE, "telnet_read() : read %d bytes:\n", rc);
                for (i = 0; i < rc; i++) {
                        fprintf(DEBUG_FILE_HANDLE, " %02x", (read_buffer[read_buffer_n + i] & 0xFF));
                }
                fprintf(DEBUG_FILE_HANDLE, "\n");
                for (i = 0; i < rc; i++) {
                        if ((read_buffer[read_buffer_n + i] & 0xFF) >= 0x80) {
                                fprintf(DEBUG_FILE_HANDLE, " %02x", (read_buffer[read_buffer_n + i] & 0xFF));
                        } else {
                                fprintf(DEBUG_FILE_HANDLE, " %c ", (read_buffer[read_buffer_n + i] & 0xFF));
                        }
                }
                fprintf(DEBUG_FILE_HANDLE, "\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                /* Check for EOF or error */
                if (rc < 0) {
                        if (read_buffer_n == 0) {
                                /* Something bad happened, just return it */
                                return rc;
                        }
                } else if (rc == 0) {
                        /* EOF - Drop a connection close message */
                        nvt.is_eof = Q_TRUE;
                } else {
                        /* More data came in */
                        read_buffer_n += rc;
                }
        } /* if (nvt.is_eof == Q_TRUE) */

        if ((read_buffer_n == 0) && (nvt.eof_msg == Q_TRUE)) {
                /* We are done, return the final EOF */
                return 0;
        }

        if ((read_buffer_n == 0) && (nvt.is_eof == Q_TRUE)) {
                /* EOF - Drop "Connection closed." */
                if (q_program_state != Q_STATE_HOST) {
                        snprintf((char *)read_buffer, sizeof(read_buffer), "%s",
                                _("Connection closed.\r\n"));
                        read_buffer_n = strlen((char *)read_buffer);
                }
                nvt.eof_msg = Q_TRUE;
        }

        /* Loop through the read bytes */
        for (i = 0; i < read_buffer_n; i++) {
                ch = read_buffer[i];

#ifdef DEBUG_NET
                /* fprintf(DEBUG_FILE_HANDLE, "  ch: %d \\%03o 0x%02x '%c'\n", ch, ch, ch, ch); */
                /* fflush(DEBUG_FILE_HANDLE); */
#endif /* DEBUG_NET */

                if (nvt.subneg_end == Q_TRUE) {
                        /* Looking for IAC SE to end this subnegotiation */
                        if (ch == TELNET_SE) {
                                if (nvt.iac == Q_TRUE) {
                                        /* IAC SE */
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, " <--> End Subnegotiation <-->\n");
                                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                                        nvt.iac = Q_FALSE;
                                        nvt.subneg_end = Q_FALSE;
                                        handle_subneg(fd);
                                }
                        } else if (ch == TELNET_IAC) {
                                if (nvt.iac == Q_TRUE) {
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, " - IAC within subneg -\n");
                                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                                        /* An argument to the subnegotiation option */
                                        subneg_buffer[subneg_buffer_n] = TELNET_IAC;
                                        subneg_buffer_n++;
                                } else {
                                        nvt.iac = Q_TRUE;
                                }
                        } else {
                                /* An argument to the subnegotiation option */
                                subneg_buffer[subneg_buffer_n] = ch;
                                subneg_buffer_n++;
                        }
                        continue;
                }

                /* Look for DO/DON'T/WILL/WON'T option */
                if (nvt.dowill == Q_TRUE) {
#ifdef DEBUG_NET
                        fprintf(DEBUG_FILE_HANDLE, "   OPTION: %s\n", telnet_option_string(ch));
#endif /* DEBUG_NET */
                        /* Look for option */
                        switch (ch) {
                        case 0:
                                /* Binary Transmission */
                                if (nvt.dowill_type == TELNET_WILL) {
                                        /*
                                         * Server will use binary transmission, yay
                                         */
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "** BINARY TRANSMISSION ON (we initiated) **\n");
#endif /* DEBUG_NET */
                                        nvt.binary_mode = Q_TRUE;
                                } else if (nvt.dowill_type == TELNET_DO) {
                                        /*
                                         * Server asks for binary transmission
                                         */
                                        telnet_will(fd, ch);
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "** BINARY TRANSMISSION ON (they initiated) **\n");
#endif /* DEBUG_NET */
                                        nvt.binary_mode = Q_TRUE;
                                } else if (nvt.dowill_type == TELNET_WONT) {
                                        /*
                                         * We're screwed, server won't do binary transmission
                                         */
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "Asked for binary, server refused.\n");
#endif /* DEBUG_NET */
                                        nvt.binary_mode = Q_FALSE;
                                } else {
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "Server demands NVT ASCII mode.\n");
#endif /* DEBUG_NET */
                                        nvt.binary_mode = Q_FALSE;
                                }
                                break;
                        case 1:
                                /* Echo */
                                if (nvt.dowill_type == TELNET_WILL) {
                                        /*
                                         * Server will use echo, yay
                                         */
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "** ECHO ON (we initiated) **\n");
#endif /* DEBUG_NET */
                                        nvt.echo_mode = Q_TRUE;
                                } else if (nvt.dowill_type == TELNET_DO) {
                                        /*
                                         * Server asks for echo
                                         */
                                        telnet_will(fd, ch);
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "** ECHO ON (they initiated) **\n");
#endif /* DEBUG_NET */
                                        nvt.echo_mode = Q_TRUE;
                                } else if (nvt.dowill_type == TELNET_WONT) {
                                        /*
                                         * We're screwed, server won't do echo
                                         */
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "Asked for echo, server refused.\n");
#endif /* DEBUG_NET */
                                        nvt.echo_mode = Q_FALSE;
                                } else {
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "Server demands no echo.\n");
#endif /* DEBUG_NET */
                                        nvt.echo_mode = Q_FALSE;
                                }
                                break;

                        case 3:
                                /* Suppress Go Ahead */
                                if (nvt.dowill_type == TELNET_WILL) {
                                        /*
                                         * Server will use suppress go-ahead, yay
                                         */
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "** SUPPRESS GO-AHEAD ON (we initiated) **\n");
#endif /* DEBUG_NET */
                                        nvt.go_ahead = Q_FALSE;
                                } else if (nvt.dowill_type == TELNET_DO) {
                                        /*
                                         * Server asks for suppress go-ahead
                                         */
                                        telnet_will(fd, ch);
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "** SUPPRESS GO-AHEAD ON (they initiated) **\n");
#endif /* DEBUG_NET */
                                        nvt.go_ahead = Q_FALSE;
                                } else if (nvt.dowill_type == TELNET_WONT) {
                                        /*
                                         * We're screwed, server won't do
                                         * suppress go-ahead
                                         */
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "Asked for Suppress Go-Ahead, server refused.\n");
#endif /* DEBUG_NET */
                                        nvt.go_ahead = Q_TRUE;
                                } else {
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "Server demands Go-Ahead mode.\n");
#endif /* DEBUG_NET */
                                        nvt.go_ahead = Q_TRUE;
                                }
                                break;

                        case 24:
                                /* Terminal Type - send what's in TERM */
                                if (nvt.dowill_type == TELNET_WILL) {
                                        /*
                                         * Server will use terminal type, yay
                                         */
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "** TERMINAL TYPE ON (we initiated) **\n");
#endif /* DEBUG_NET */
                                        nvt.do_term_type = Q_TRUE;
                                } else if (nvt.dowill_type == TELNET_DO) {
                                        /*
                                         * Server asks for terminal type
                                         */
                                        telnet_will(fd, ch);
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "** TERMINAL TYPE ON (they initiated) **\n");
#endif /* DEBUG_NET */
                                        nvt.do_term_type = Q_TRUE;
                                } else if (nvt.dowill_type == TELNET_WONT) {
                                        /*
                                         * We're screwed, server won't do terminal type
                                         */
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "Asked for Terminal Type, server refused.\n");
#endif /* DEBUG_NET */
                                        nvt.do_term_type = Q_FALSE;
                                } else {
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "Server will not listen to terminal type.\n");
#endif /* DEBUG_NET */
                                        nvt.do_term_type = Q_FALSE;
                                }
                                break;

                        case 31:
                                /* Window Size Option */
                                if (nvt.dowill_type == TELNET_WILL) {
                                        /*
                                         * Server asks for NAWS
                                         */
                                        telnet_will(fd, ch);
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "** Negotiate About Window Size ON (they initiated) **\n");
#endif /* DEBUG_NET */
                                        nvt.do_naws = Q_TRUE;

                                        /* Send our window size */
                                        telnet_send_naws(fd, HEIGHT - STATUS_HEIGHT, WIDTH);
                                } else if (nvt.dowill_type == TELNET_DO) {
                                        /*
                                         * Server will use NAWS, yay
                                         */
                                        nvt.do_naws = Q_TRUE;

                                        /* Send our window size */
                                        telnet_send_naws(fd, HEIGHT - STATUS_HEIGHT, WIDTH);
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "** Negotiate About Window Size ON (we initiated) **\n");
#endif /* DEBUG_NET */
                                } else if (nvt.dowill_type == TELNET_WONT) {
                                        /*
                                         * We're screwed, server won't do NAWS
                                         */
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "Asked for Negotiate About Window Size, server refused.\n");
#endif /* DEBUG_NET */
                                        nvt.do_naws = Q_FALSE;
                                } else {
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "Asked for Negotiate About Window Size, server refused.\n");
#endif /* DEBUG_NET */
                                        nvt.do_naws = Q_FALSE;
                                }
                                break;

                        case 32:
                                /* Terminal Speed */
                                if (nvt.dowill_type == TELNET_WILL) {
                                        /*
                                         * Server will use terminal speed, yay
                                         */
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "** TERMINAL SPEED ON (we initiated) **\n");
#endif /* DEBUG_NET */
                                        nvt.do_term_speed = Q_TRUE;
                                } else if (nvt.dowill_type == TELNET_DO) {
                                        /*
                                         * Server asks for terminal speed
                                         */
                                        telnet_will(fd, ch);
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "** TERMINAL SPEED ON (they initiated) **\n");
#endif /* DEBUG_NET */
                                        nvt.do_term_speed = Q_TRUE;
                                } else if (nvt.dowill_type == TELNET_WONT) {
                                        /*
                                         * We're screwed, server won't do
                                         * terminal speed
                                         */
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "Asked for Terminal Speed, server refused.\n");
#endif /* DEBUG_NET */
                                        nvt.do_term_speed = Q_FALSE;
                                } else {
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "Server will not listen to terminal speed.\n");
#endif /* DEBUG_NET */
                                        nvt.do_term_speed = Q_FALSE;
                                }
                                break;
                        case 35:
                                /* X Display Location - don't do this option */
                                telnet_refuse(nvt.dowill_type, fd, ch);
                                break;
                        case 39:
                                /* New Enviroment Options */
                                if (nvt.dowill_type == TELNET_WILL) {
                                        /*
                                         * Server will use new environment, yay
                                         */
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "** NEW ENVIRONMENT ON (we initiated) **\n");
#endif /* DEBUG_NET */
                                        nvt.do_environment = Q_TRUE;
                                } else if (nvt.dowill_type == TELNET_DO) {
                                        /*
                                         * Server asks for new environment
                                         */
                                        telnet_will(fd, ch);
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "** NEW ENVIRONMENT ON (they initiated) **\n");
#endif /* DEBUG_NET */
                                        nvt.do_environment = Q_TRUE;
                                } else if (nvt.dowill_type == TELNET_WONT) {
                                        /*
                                         * We're screwed, server won't do
                                         * new environment
                                         */
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "Asked for New Environment, server refused.\n");
#endif /* DEBUG_NET */
                                        nvt.do_environment = Q_FALSE;
                                } else {
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "Server will not listen to new environment.\n");
#endif /* DEBUG_NET */
                                        nvt.do_environment = Q_FALSE;
                                }
                                break;
                        default:
#ifdef DEBUG_NET
                                fprintf(DEBUG_FILE_HANDLE, "   OTHER: %d \\%3o 0x%02x\n", ch, ch, ch);
#endif /* DEBUG_NET */
                                /* Don't do this option */
                                telnet_refuse(nvt.dowill_type, fd, ch);
                                break;
                        }
                        nvt.dowill = Q_FALSE;
                        continue;
                } /* if (nvt.dowill == Q_TRUE) */

                /* Perform read processing */
                if (ch == TELNET_IAC) {

                        /* Telnet command */
                        if (nvt.iac == Q_TRUE) {
                                /* IAC IAC -> IAC */
#ifdef DEBUG_NET
                                fprintf(DEBUG_FILE_HANDLE, "IAC IAC --> IAC\n");
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                                ((char *)buf)[total] = TELNET_IAC;
                                total++;
                                nvt.iac = Q_FALSE;
                        } else {
                                nvt.iac = Q_TRUE;
                        }
                        continue;
                } else {
                        if (nvt.iac == Q_TRUE) {

#ifdef DEBUG_NET
                                fprintf(DEBUG_FILE_HANDLE, "Telnet command: ");
#endif /* DEBUG_NET */
                                switch (ch) {

                                case TELNET_SE:
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, " END Sub-Negotiation\n");
#endif /* DEBUG_NET */
                                        break;
                                case TELNET_NOP:
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, " NOP\n");
#endif /* DEBUG_NET */
                                        break;
                                case TELNET_DM:
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, " Data Mark\n");
#endif /* DEBUG_NET */
                                        break;
                                case TELNET_BRK:
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, " Break\n");
#endif /* DEBUG_NET */
                                        break;
                                case TELNET_IP:
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, " Interrupt Process\n");
#endif /* DEBUG_NET */
                                        break;
                                case TELNET_AO:
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, " Abort Output\n");
#endif /* DEBUG_NET */
                                        break;
                                case TELNET_AYT:
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, " Are You There?\n");
#endif /* DEBUG_NET */
                                        break;
                                case TELNET_EC:
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, " Erase Character\n");
#endif /* DEBUG_NET */
                                        break;
                                case TELNET_EL:
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, " Erase Line\n");
#endif /* DEBUG_NET */
                                        break;
                                case TELNET_GA:
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, " Go Ahead\n");
#endif /* DEBUG_NET */
                                        break;
                                case TELNET_SB:
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, " START Sub-Negotiation\n");
#endif /* DEBUG_NET */
                                        /* From here we wait for the IAC SE */
                                        nvt.subneg_end = Q_TRUE;
                                        subneg_buffer_n = 0;
                                        break;
                                case TELNET_WILL:
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, " WILL\n");
#endif /* DEBUG_NET */
                                        nvt.dowill = Q_TRUE;
                                        nvt.dowill_type = ch;
                                        break;
                                case TELNET_WONT:
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, " WON'T\n");
#endif /* DEBUG_NET */
                                        nvt.dowill = Q_TRUE;
                                        nvt.dowill_type = ch;
                                        break;
                                case TELNET_DO:
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, " DO\n");
#endif /* DEBUG_NET */
                                        nvt.dowill = Q_TRUE;
                                        nvt.dowill_type = ch;

                                        if (nvt.binary_mode == Q_TRUE) {
#ifdef DEBUG_NET
                                                fprintf(DEBUG_FILE_HANDLE, "Telnet DO in binary mode\n");

#endif /* DEBUG_NET */
                                        }

                                        break;
                                case TELNET_DONT:
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, " DON'T\n");
#endif /* DEBUG_NET */
                                        nvt.dowill = Q_TRUE;
                                        nvt.dowill_type = ch;
                                        break;
                                default:
                                        /* This should be equivalent to IAC NOP */
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, " Unknown: %d \\%03d 0x%02x %c\n",
                                                ch, ch, ch, ch);
                                        fprintf(DEBUG_FILE_HANDLE, "Will treat as IAC NOP\n");
#endif /* DEBUG_NET */
                                        break;
                                }
#ifdef DEBUG_NET
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                                nvt.iac = Q_FALSE;
                                continue;

                        } /* if (nvt.iac == Q_TRUE) */

                        /*
                         * All of the regular IAC processing is completed at
                         * this point.  Now we need to handle the CR and CR LF
                         * cases.
                         *
                         * According to RFC 854, in NVT ASCII mode:
                         *     Bare CR -> CR NUL
                         *     CR LF -> CR LF
                         *
                         */
                        if (nvt.binary_mode == Q_FALSE) {

                                if (ch == C_LF) {
                                        if (nvt.read_cr == Q_TRUE) {
#ifdef DEBUG_NET
                                                fprintf(DEBUG_FILE_HANDLE, "CRLF\n");
#endif /* DEBUG_NET */
                                                /*
                                                 * This is CR LF.  Send CR LF
                                                 * and turn the cr flag off.
                                                 */
                                                ((char *)buf)[total] = C_CR;
                                                total++;
                                                ((char *)buf)[total] = C_LF;
                                                total++;
                                                nvt.read_cr = Q_FALSE;
                                                continue;
                                        }
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "Bare LF\n");
#endif /* DEBUG_NET */
                                        /* This is bare LF.  Send LF. */
                                        ((char *)buf)[total] = C_LF;
                                        total++;
                                        continue;
                                }

                                if (ch == C_NUL) {
                                        if (nvt.read_cr == Q_TRUE) {
#ifdef DEBUG_NET
                                                fprintf(DEBUG_FILE_HANDLE, "CR NUL\n");
#endif /* DEBUG_NET */
                                                /*
                                                 * This is CR NUL.  Send CR
                                                 * and turn the cr flag off.
                                                 */
                                                ((char *)buf)[total] = C_CR;
                                                total++;
                                                nvt.read_cr = Q_FALSE;
                                                continue;
                                        }
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "Bare NUL\n");
#endif /* DEBUG_NET */
                                        /* This is bare NUL.  Send NUL. */
                                        ((char *)buf)[total] = C_NUL;
                                        total++;
                                        continue;
                                }

                                if (ch == C_CR) {
                                        if (nvt.read_cr == Q_TRUE) {
#ifdef DEBUG_NET
                                                fprintf(DEBUG_FILE_HANDLE, "CR CR\n");
#endif /* DEBUG_NET */
                                                /*
                                                 * This is CR CR.  Send a
                                                 * CR NUL and leave the cr
                                                 * flag on.
                                                 */
                                                ((char *)buf)[total] = C_CR;
                                                total++;
                                                ((char *)buf)[total] = C_NUL;
                                                total++;
                                                continue;
                                        }
                                        /*
                                         * This is the first CR.  Set the
                                         * cr flag.
                                         */
                                        nvt.read_cr = Q_TRUE;
                                        continue;
                                }

                                if (nvt.read_cr == Q_TRUE) {
#ifdef DEBUG_NET
                                        fprintf(DEBUG_FILE_HANDLE, "Bare CR\n");
#endif /* DEBUG_NET */
                                        /* This was a bare CR in the stream. */
                                        ((char *)buf)[total] = C_CR;
                                        total++;
                                        nvt.read_cr = Q_FALSE;
                                }

                                /* This is a regular character.  Pass it on. */
                                ((char *)buf)[total] = ch;
                                total++;
                                continue;
                        }

                        /*
                         * This is the case for any of:
                         *
                         *     1) A NVT ASCII character that isn't CR, LF,
                         *        or NUL.
                         *
                         *     2) A NVT binary character.
                         *
                         * For all of these cases, we just pass the character
                         * on.
                         */
                        ((char *)buf)[total] = ch;
                        total++;

                } /* if (ch == TELNET_IAC) */

        } /* for (i = 0; i < read_buffer_n; i++) */

        /* Return bytes read */

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "telnet_read() : send %d bytes to caller:\n", (int)total);
        for (i = 0; i < total; i++) {
                fprintf(DEBUG_FILE_HANDLE, " %02x", (((char *)buf)[i] & 0xFF));
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        for (i = 0; i < total; i++) {
                if ((((char *)buf)[i] & 0xFF) >= 0x80) {
                        fprintf(DEBUG_FILE_HANDLE, " %02x", (((char *)buf)[i] & 0xFF));
                } else {
                        fprintf(DEBUG_FILE_HANDLE, " %c ", (((char *)buf)[i] & 0xFF));
                }
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        /* read_buffer is always fully consumed */
        read_buffer_n = 0;

        if (total == 0) {
#ifdef DEBUG_NET
                fprintf(DEBUG_FILE_HANDLE, "telnet_read() : EAGAIN\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                /* We consumed everything, but it's not EOF.  Return EAGAIN. */
#ifdef Q_PDCURSES_WIN32
                set_errno(WSAEWOULDBLOCK);
#else
                set_errno(EAGAIN);
#endif /* Q_PDCURSES_WIN32 */
                return -1;
        }

        return total;
} /* ---------------------------------------------------------------------- */

/* Just like write(), but perform the telnet protocol */
ssize_t telnet_write(const int fd, void * buf, size_t count) {
        unsigned char ch;
        int i;
        int sent = 0;
        Q_BOOL flush = Q_FALSE;

        if (state == INIT) {
                /*
                 * Start the telnet protocol negotiation.
                 */
                telnet_send_options(fd);
                state = SENT_OPTIONS;
        }

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "telnet_write() : write %d bytes:\n", (int)count);
        for (i = 0; i < count; i++) {
                fprintf(DEBUG_FILE_HANDLE, " %02x", (((char *)buf)[i] & 0xFF));
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        /* If we had an error last time, return that */
        if (nvt.write_last_error == Q_TRUE) {
                set_errno(nvt.write_last_errno);
                nvt.write_last_error = Q_FALSE;
                return nvt.write_rc;
        }

        if (count == 0) {
                /* NOP */
                return 0;
        }

        /* Flush whatever we didn't send last time */
        if (write_buffer_n > 0) {
                flush = Q_TRUE;
        }

        /* Setup for loop */
        i = 0;

write_flush:

        /* See if we need to sync with the remote side now */
        if (flush == Q_TRUE) {

#ifdef DEBUG_NET
                int j;
                fprintf(DEBUG_FILE_HANDLE, "telnet_write() : write to remote side %d bytes:\n", write_buffer_n);
                for (j = 0; j < write_buffer_n; j++) {
                        fprintf(DEBUG_FILE_HANDLE, " %02x", (write_buffer[j] & 0xFF));
                }
                fprintf(DEBUG_FILE_HANDLE, "\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

#ifdef Q_PDCURSES_WIN32
                nvt.write_rc = send(fd, (const char *)write_buffer, write_buffer_n, 0);
#else
                nvt.write_rc = write(fd, write_buffer, write_buffer_n);
#endif /* Q_PDCURSES_WIN32 */
                if (nvt.write_rc <= 0) {
                        /* Encountered an error */
                        nvt.write_last_errno = get_errno();
#ifdef Q_PDCURSES_WIN32
                        if ((get_errno() == EAGAIN) || (get_errno() == WSAEWOULDBLOCK)) {
#else
                        if ((get_errno() == EAGAIN) || (get_errno() == EWOULDBLOCK)) {
#endif /* Q_PDCURSES_WIN32 */
                                /*
                                 * We filled up the other side, bail out.
                                 * Don't flag this as an error to the caller
                                 * unless no data got out.
                                 */
                                nvt.write_last_error = Q_FALSE;
                                if (sent > 0) {
                                        /* Something good got out. */
                                        return sent;
                                } else {
                                        /*
                                         * Let the caller see the original
                                         * EAGAIN.  errno is already set.
                                         */
                                        return -1;
                                }
                        }

                        /* This is either another error or EOF. */
                        if (sent > 0) {
                                /*
                                 * We've sent good stuff before.  Return the
                                 * known good sent bytes, then return the error
                                 * on the next call to telnet_write().
                                 */
                                nvt.write_last_error = Q_TRUE;
                                return sent;
                        } else {
                                /* This is the first error, just return it. */
                                nvt.write_last_error = Q_FALSE;
                                return nvt.write_rc;
                        }
                } else {
                        /*
                         * Note: sent is following the _input_ count, not the
                         * actual output count.
                         */
                        sent = i;
                        memmove(write_buffer, write_buffer + nvt.write_rc,
                                write_buffer_n - nvt.write_rc);
                        write_buffer_n -= nvt.write_rc;
                }
                flush = Q_FALSE;
        }

        while (i < count) {

                /* We must have at least 2 bytes free in write_buffer */
                if (sizeof(write_buffer) - write_buffer_n < 4) {
                        break;
                }

                /* Pull the next character */
                ch = ((unsigned char  *)buf)[i];
                i++;

                if (nvt.binary_mode == Q_TRUE) {
#ifdef DEBUG_NET
                        fprintf(DEBUG_FILE_HANDLE, "telnet_write() : BINARY: %c \\%o %02x\n", ch, ch, ch);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                        if (ch == TELNET_IAC) {
                                /* IAC -> IAC IAC */
                                write_buffer[write_buffer_n] = TELNET_IAC;
                                write_buffer_n++;
                                write_buffer[write_buffer_n] = TELNET_IAC;
                                write_buffer_n++;
                                flush = Q_TRUE;
                                goto write_flush;
                        } else {
                                /* Anything else -> just send */
                                write_buffer[write_buffer_n] = ch;
                                write_buffer_n++;
                                continue;
                        }
                }

                /* Non-binary mode: more complicated */
#ifdef DEBUG_NET
                fprintf(DEBUG_FILE_HANDLE, "telnet_write() : ASCII: %c \\%o %02x\n", ch, ch, ch);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

                /* Bare carriage return -> CR NUL */
                if (ch == C_CR) {
                        if (nvt.write_cr == Q_TRUE) {
                                /* CR <anything> -> CR NULL */
                                write_buffer[write_buffer_n] = C_CR;
                                write_buffer_n++;
                                write_buffer[write_buffer_n] = C_NUL;
                                write_buffer_n++;
                                flush = Q_TRUE;
                        }
                        nvt.write_cr = Q_TRUE;
                } else if (ch == C_LF) {
                        if (nvt.write_cr == Q_TRUE) {
                                /* CR LF -> CR LF */
                                write_buffer[write_buffer_n] = C_CR;
                                write_buffer_n++;
                                write_buffer[write_buffer_n] = C_LF;
                                write_buffer_n++;
                                flush = Q_TRUE;
                        } else {
                                /* Bare LF -> LF */
                                write_buffer[write_buffer_n] = ch;
                                write_buffer_n++;
                        }
                        nvt.write_cr = Q_FALSE;
                } else if (ch == TELNET_IAC) {
                        if (nvt.write_cr == Q_TRUE) {
                                /* CR <anything> -> CR NULL */
                                write_buffer[write_buffer_n] = C_CR;
                                write_buffer_n++;
                                write_buffer[write_buffer_n] = C_NUL;
                                write_buffer_n++;
                        }
                        /* IAC -> IAC IAC */
                        write_buffer[write_buffer_n] = TELNET_IAC;
                        write_buffer_n++;
                        write_buffer[write_buffer_n] = TELNET_IAC;
                        write_buffer_n++;

                        nvt.write_cr = Q_FALSE;
                        flush = Q_TRUE;
                } else {
                        /* Normal character */
                        write_buffer[write_buffer_n] = ch;
                        write_buffer_n++;
                }

                if (flush == Q_TRUE) {
                        goto write_flush;
                }
        } /* while (i < count) */

        if (    (nvt.write_cr == Q_TRUE) &&
                (       (q_program_state == Q_STATE_CONSOLE) ||
                        (q_program_state == Q_STATE_HOST))
        ) {
                /*
                 * Assume that any bare CR sent from the console needs to go
                 * out.
                 */
                write_buffer[write_buffer_n] = C_CR;
                write_buffer_n++;
                nvt.write_cr = Q_FALSE;
        }

        if ((write_buffer_n > 0) && (flush == Q_FALSE)) {
                /*
                 * We've got more data, push it out.  If we hit EAGAIN or
                 * some other error the flush block will do the exit.
                 */
                flush = Q_TRUE;
                goto write_flush;
        }

        /* Return total bytes sent. */
        return sent;
} /* ---------------------------------------------------------------------- */

/* Send new screen dimensions to the remote side */
void rlogin_resize_screen(const int lines, const int columns) {
        unsigned char buffer[12];
        buffer[0] = 0xFF;
        buffer[1] = 0xFF;
        buffer[2] = 's';
        buffer[3] = 's';
        buffer[4] = lines / 256;
        buffer[5] = lines % 256;
        buffer[6] = columns / 256;
        buffer[7] = columns % 256;
        /* Assume 9 x 16 characters ? */
        buffer[8] = (columns * 9) / 256;
        buffer[9] = (columns * 9) % 256;
        buffer[10] = (lines * 16) / 256;
        buffer[11] = (lines * 16) % 256;
        raw_write(q_child_tty_fd, buffer, 12);
} /* ---------------------------------------------------------------------- */

/* Send the rlogin header as per RFC 1258 */
static void rlogin_send_login(const int fd) {
        unsigned char buffer[128];
#ifdef Q_PDCURSES_WIN32
        char username[UNLEN + 1];
        DWORD username_n = sizeof(username) - 1;
#endif /* Q_PDCURSES_WIN32 */

        /* Empty string */
        buffer[0] = 0;
        raw_write(fd, buffer, 1);

        /* Local username */
#ifdef Q_PDCURSES_WIN32
        memset(username, 0, sizeof(username));
        char notify_message[DIALOG_MESSAGE_SIZE];
        if (GetUserNameA(username, &username_n) == FALSE) {
                /* Error: can't get local username */
                snprintf(notify_message, sizeof(notify_message),
                        _("Error getting local username: %d %s"),
                         GetLastError(), strerror(GetLastError()));
                notify_form(notify_message, 0);
        } else {
                snprintf((char *)buffer, sizeof(buffer) - 1, "%s",
                         username);
        }
#else
        snprintf((char *)buffer, sizeof(buffer) - 1, "%s",
                getpwuid(geteuid())->pw_name);
#endif /* Q_PDCURSES_WIN32 */
        raw_write(fd, buffer, strlen((char *)buffer) + 1);

        /* Remote username */
        if (    (q_status.current_username != NULL) &&
                (wcslen(q_status.current_username) > 0)) {

                snprintf((char *)buffer, sizeof(buffer) - 1, "%ls",
                        q_status.current_username);
        } else {
#ifdef Q_PDCURSES_WIN32
                snprintf((char *)buffer, sizeof(buffer) - 1, "%s",
                         username);
#else
                snprintf((char *)buffer, sizeof(buffer) - 1, "%s",
                        getpwuid(geteuid())->pw_name);
#endif /* Q_PDCURSES_WIN32 */
        }
        raw_write(fd, buffer, strlen((char *)buffer) + 1);

        /* terminal/speed */
        snprintf((char *)buffer, sizeof(buffer) - 1, "%s/38400",
                dialer_get_term());
        raw_write(fd, buffer, strlen((char *)buffer) + 1);
} /* ---------------------------------------------------------------------- */

/* Just like read(), but perform the rlogin protocol */
ssize_t rlogin_read(const int fd, void * buf, size_t count, Q_BOOL oob) {
        unsigned char ch;
        int rc;
        int total = 0;
        size_t max_read;

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "rlogin_read() : %d bytes in read_buffer\n", read_buffer_n);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        /* Perform the raw read */
        if (count == 0) {
                /* NOP */
                return 0;
        }

        if (nvt.is_eof == Q_TRUE) {
#ifdef DEBUG_NET
                fprintf(DEBUG_FILE_HANDLE, "rlogin_read() : no read because EOF\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

                /* Let the caller know this is now closed */
                set_errno(EIO);
                return -1;
        } else {
                if (oob == Q_TRUE) {
                        /* Look for OOB data */
#ifdef Q_PDCURSES_WIN32
                        rc = recv(fd, (char *)&ch, 1, MSG_OOB);
#else
                        rc = recv(fd, &ch, 1, MSG_OOB);
#endif /* Q_PDCURSES_WIN32 */
                        if (rc == 1) {
#ifdef DEBUG_NET
                                fprintf(DEBUG_FILE_HANDLE, "rlogin_read() : OOB DATA: 0x%02x\n", ch);
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                                /* An OOB message came through */
                                if (ch == 0x80) {
                                        /* Resize screen */
                                        rlogin_resize_screen(HEIGHT - STATUS_HEIGHT, WIDTH);
                                        state = ESTABLISHED;
                                } else if (ch == 0x02) {
                                        /* Discard unprocessed screen data */
                                } else if (ch == 0x10) {
                                        /*
                                         * Switch to "raw" mode (pass XON/XOFF
                                         * to remote side)
                                         */
                                } else if (ch == 0x20) {
                                        /*
                                         * Switch to "cooked" mode
                                         * (handle XON/XOFF locally)
                                         */
                                }
                        }
#ifdef Q_PDCURSES_WIN32
                        set_errno(WSAEWOULDBLOCK);
#else
                        set_errno(EAGAIN);
#endif /* Q_PDCURSES_WIN32 */
                        return -1;
                } /* if (oob == Q_TRUE) */

                max_read = sizeof(read_buffer) - read_buffer_n;
                if (max_read > count) {
                        max_read = count;
                }

                /* Read some data from the other end */
#ifdef Q_PDCURSES_WIN32
                rc = recv(fd, (char *)read_buffer + read_buffer_n, max_read, 0);
#else
                rc = read(fd, read_buffer + read_buffer_n, max_read);
#endif /* Q_PDCURSES_WIN32 */

#ifdef DEBUG_NET
                int i;
                fprintf(DEBUG_FILE_HANDLE, "rlogin_read() : read %d bytes:\n", rc);
                for (i = 0; i < rc; i++) {
                        fprintf(DEBUG_FILE_HANDLE, " %02x", (read_buffer[read_buffer_n + i] & 0xFF));
                }
                fprintf(DEBUG_FILE_HANDLE, "\n");
                for (i = 0; i < rc; i++) {
                        if ((read_buffer[read_buffer_n + i] & 0xFF) >= 0x80) {
                                fprintf(DEBUG_FILE_HANDLE, " %02x", (read_buffer[read_buffer_n + i] & 0xFF));
                        } else {
                                fprintf(DEBUG_FILE_HANDLE, " %c ", (read_buffer[read_buffer_n + i] & 0xFF));
                        }
                }
                fprintf(DEBUG_FILE_HANDLE, "\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                /* Check for EOF or error */
                if (rc < 0) {
                        if (read_buffer_n == 0) {
                                /* Something bad happened, just return it */
                                return rc;
                        }
                } else if (rc == 0) {
#ifdef DEBUG_NET
                        fprintf(DEBUG_FILE_HANDLE, "rlogin_read() : rc = 0 errno = %d (%s)\n", get_errno(), get_strerror(get_errno()));
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

#ifdef Q_PDCURSES_WIN32
                        if ((get_errno() == EAGAIN) || (get_errno() == WSAEWOULDBLOCK)) {
#else
                        if (get_errno() == EAGAIN) {
#endif /* Q_PDCURSES_WIN32 */
                                /* NOP */
                        } else {
                                /* EOF - Drop a connection close message */
                                nvt.is_eof = Q_TRUE;
                        }
                } else {
                        /* More data came in */
                        read_buffer_n += rc;
                }
        } /* if (nvt.is_eof == Q_TRUE) */

        if ((read_buffer_n == 0) && (nvt.eof_msg == Q_TRUE)) {
                /* We are done, return the final EOF */
                return 0;
        }

        if ((read_buffer_n == 0) && (nvt.is_eof == Q_TRUE)) {
                /* EOF - Drop "Connection closed." */
                snprintf((char *)read_buffer, sizeof(read_buffer), "%s",
                        _("Connection closed.\r\n"));
                read_buffer_n = strlen((char *)read_buffer);
                nvt.eof_msg = Q_TRUE;
        }

        /* Copy the bytes raw to the other side */
        memcpy(buf, read_buffer, read_buffer_n);
        total = read_buffer_n;

        /* Return bytes read */
#ifdef DEBUG_NET
        int i;
        fprintf(DEBUG_FILE_HANDLE, "rlogin_read() : send %d bytes to caller:\n", (int)total);
        for (i = 0; i < total; i++) {
                fprintf(DEBUG_FILE_HANDLE, " %02x", (((char *)buf)[i] & 0xFF));
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        for (i = 0; i < total; i++) {
                if ((((char *)buf)[i] & 0xFF) >= 0x80) {
                        fprintf(DEBUG_FILE_HANDLE, " %02x", (((char *)buf)[i] & 0xFF));
                } else {
                        fprintf(DEBUG_FILE_HANDLE, " %c ", (((char *)buf)[i] & 0xFF));
                }
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        /* read_buffer is always fully consumed */
        read_buffer_n = 0;

        if (total == 0) {
#ifdef DEBUG_NET
                fprintf(DEBUG_FILE_HANDLE, "rlogin_read() : EAGAIN\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                /* We consumed everything, but it's not EOF.  Return EAGAIN. */
#ifdef Q_PDCURSES_WIN32
                set_errno(WSAEWOULDBLOCK);
#else
                set_errno(EAGAIN);
#endif /* Q_PDCURSES_WIN32 */
                return -1;
        }

        return total;
} /* ---------------------------------------------------------------------- */

/* Just like write(), but perform the rlogin protocol */
ssize_t rlogin_write(const int fd, void * buf, size_t count) {
#ifdef Q_PDCURSES_WIN32
        return send(fd, (const char *)buf, count, 0);
#else
        return write(fd, buf, count);
#endif /* Q_PDCURSES_WIN32 */
} /* ---------------------------------------------------------------------- */

#ifdef Q_LIBSSH2

typedef enum
{
        step_a, step_b, step_c, step_d
} base64_decodestep;

typedef struct
{
        base64_decodestep step;
        char plainchar;
} base64_decodestate;

/* Forward declaration for ssh_setup_connection() */
static int base64_decode_block(const char* code_in, const int length_in, char* plaintext_out, base64_decodestate* state_in);
static void base64_init_decodestate(base64_decodestate* state_in);

static LIBSSH2_SESSION * q_ssh_session = NULL;
static LIBSSH2_CHANNEL * q_ssh_channel = NULL;

static char * ssh_server_key = NULL;

static void emit_ssh_error(LIBSSH2_SESSION * session) {
#ifdef DEBUG_NET
        char * msg;
        int rc = libssh2_session_last_error(session, &msg, NULL, 0);
        fprintf(DEBUG_FILE_HANDLE, "libssh2 ERROR: %d %s\n", rc, msg);
#endif /* DEBUG_NET */
} /* ---------------------------------------------------------------------- */

static void md5_to_string(const unsigned char * md5, char * dest) {
        const int len = 16;
        int i;
        memset(dest, 0, len * 3 + 2);
        for (i = 0; i < len/2; i++) {
                sprintf(dest + strlen(dest),
                        "%x", (md5[i] >> 4) & 0x0F);
                sprintf(dest + strlen(dest),
                        "%x", md5[i] & 0x0F);
                sprintf(dest + strlen(dest),
                        ":");
        }
        for (; i < len; i++) {
                sprintf(dest + strlen(dest),
                        "%x", (md5[i] >> 4) & 0x0F);
                sprintf(dest + strlen(dest),
                        "%x", md5[i] & 0x0F);
                sprintf(dest + strlen(dest),
                        ":");
        }
        dest[strlen(dest) - 1] = 0;
} /* ---------------------------------------------------------------------- */

/* Currently-connected ssh server key fingerprint */
const char * ssh_server_key_str() {
        assert(q_ssh_session != NULL);
        if (ssh_server_key == NULL) {
                const unsigned char * hash;
                const int len = 16;
                hash = (unsigned char *)libssh2_hostkey_hash(q_ssh_session,
                        LIBSSH2_HOSTKEY_HASH_MD5);
                if (hash == NULL) {
                        emit_ssh_error(q_ssh_session);
                        ssh_server_key = Xstrdup("*** UNKNOWN! ***",
                                __FILE__, __LINE__);
                } else {
                        ssh_server_key = (char *)Xmalloc((len * 3 + 2) * sizeof(char),
                                __FILE__, __LINE__);
                        md5_to_string(hash, ssh_server_key);
                }
        }
        return ssh_server_key;
} /* ---------------------------------------------------------------------- */

/* Establish connection to remote SSH server */
static int ssh_setup_connection(int fd, const char * host, const char * port) {
        assert(q_ssh_session == NULL);
        char username[128];
        char password[128];
        int rc = LIBSSH2_ERROR_SOCKET_NONE;
        LIBSSH2_KNOWNHOSTS * hosts = NULL;
        char * knownhosts_filename;
        struct libssh2_knownhost * knownhost = NULL;
        char * knownhost_key = NULL;
        char knownhost_key_str[1024];
        int knownhost_key_str_n = 0;
        unsigned char knownhost_key_md5[16];
        char knownhost_key_md5_str[16 * 3 + 2];
        libssh2_md5_ctx md5_ctx;

        base64_decodestate b64_state;
        int knownhosts_i = 0;
        size_t hostkey_length;
        int hostkey_type;
        const char * hostkey = NULL;
        int hostkey_status;
        char notify_message[DIALOG_MESSAGE_SIZE];
        char * message_lines[8];
        int keystroke;
#ifdef DEBUG_NET
        int i;
#endif /* DEBUG_NET */

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "ssh_setup_connection() ENTER\n");
#endif /* DEBUG_NET */

        q_ssh_session = libssh2_session_init();
        if (q_ssh_session == NULL) {
                /* Unable to initialize SSH */
                return -1;
        }

        /* Timeout after 10 seconds */
        libssh2_session_set_timeout(q_ssh_session, 10 * 1000);

        /* Handshake with server */
#if LIBSSH2_VERSION_NUM < 0x010208
        rc = libssh2_session_startup(q_ssh_session, fd);
#else
        rc = libssh2_session_handshake(q_ssh_session, fd);
#endif /* LIBSSH2_VERSION_NUM */
        if (rc != 0) {
                /* Error connecting to the remote server */
                emit_ssh_error(q_ssh_session);
                ssh_close();
                return -1;
        }

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "ssh_setup_connection() handshake OK\n");
#endif /* DEBUG_NET */

        /* Check host key against known_hosts */
        hosts = libssh2_knownhost_init(q_ssh_session);
        if (hosts == NULL) {
#ifdef DEBUG_NET
                fprintf(DEBUG_FILE_HANDLE, "ssh_setup_connection() hosts NULL\n");
#endif /* DEBUG_NET */
                /* This isn't terribly bad, just move on. */
        } else {
                hostkey = libssh2_session_hostkey(q_ssh_session,
                        &hostkey_length, &hostkey_type);
                if (hostkey == NULL) {
                        goto skip_hostkey_check;
                }
#ifdef DEBUG_NET
                fprintf(DEBUG_FILE_HANDLE, "hostkey:");
                for (i = 0; i < hostkey_length; i++) {
                        fprintf(DEBUG_FILE_HANDLE, " %02x", (hostkey[i] & 0xFF));
                }
                fprintf(DEBUG_FILE_HANDLE, "\n");
#endif /* DEBUG_NET */

                knownhosts_filename = get_option(Q_OPTION_SSH_KNOWNHOSTS);
#ifdef Q_PDCURSES_WIN32
                /* Ensure known_hosts exists */
                if (file_exists(knownhosts_filename) != Q_TRUE) {
                        FILE * file = NULL;
                        file = fopen(knownhosts_filename, "w");
                        fclose(file);
                }
#endif /* Q_PDCURSES_WIN32 */
                knownhosts_i = libssh2_knownhost_readfile(hosts,
                        knownhosts_filename,
                        LIBSSH2_KNOWNHOST_FILE_OPENSSH);
                if (knownhosts_i < 0) {
#ifdef DEBUG_NET
                        fprintf(DEBUG_FILE_HANDLE, "knownhosts_readfile failed\n");
#endif /* DEBUG_NET */
                        /* known_hosts isn't working, move on */
                        goto free_knownhosts;
                } else {
#ifdef DEBUG_NET
                        fprintf(DEBUG_FILE_HANDLE, "knownhosts_readfile %d hosts read\n", knownhosts_i);
#endif /* DEBUG_NET */
                }

                hostkey_status = libssh2_knownhost_check(hosts, host,
                        hostkey, hostkey_length,
                        LIBSSH2_KNOWNHOST_TYPE_PLAIN |
                        LIBSSH2_KNOWNHOST_KEYENC_RAW,
                        &knownhost);
                if (hostkey_status == LIBSSH2_KNOWNHOST_CHECK_FAILURE) {
#ifdef DEBUG_NET
                        fprintf(DEBUG_FILE_HANDLE, "LIBSSH2_KNOWNHOST_CHECK_FAILURE\n");
#endif /* DEBUG_NET */
                        /* known_hosts isn't working, move on */
                        goto free_knownhosts;
                }
                if (hostkey_status == LIBSSH2_KNOWNHOST_CHECK_MATCH) {
#ifdef DEBUG_NET
                        fprintf(DEBUG_FILE_HANDLE, "LIBSSH2_KNOWNHOST_CHECK_MATCH\n");
#endif /* DEBUG_NET */
                        /* known_hosts entry matches, move on */
                        goto free_knownhosts;
                }
                if (hostkey_status == LIBSSH2_KNOWNHOST_CHECK_NOTFOUND) {
#ifdef DEBUG_NET
                        fprintf(DEBUG_FILE_HANDLE, "LIBSSH2_KNOWNHOST_CHECK_NOTFOUND\n");
#endif /* DEBUG_NET */
                        /* New entry, ask if the user wants to add it */
                        sprintf(notify_message, _("Host key for %s:%s not found: "),  host, port);
                                message_lines[0] = notify_message;
                                message_lines[1] = (char *)ssh_server_key_str();
                                message_lines[2] = "";
                                message_lines[3] = "   Add to known hosts?  [Y/n/z] ";
                        keystroke = tolower(notify_prompt_form_long(
                                message_lines,
                                _("Host Key Not Found"),
                                _(" Y-Connect And Add Key   N-Connect   Z-Disconnect "),
                                Q_TRUE,
                                0.0, "YyNnZz\r", 4));
                        q_cursor_off();

                        if ((keystroke == 'y') || (keystroke == C_CR)) {
                                libssh2_knownhost_addc(hosts, host, NULL,
                                        hostkey, hostkey_length,
                                        NULL, 0,
                                        LIBSSH2_KNOWNHOST_TYPE_PLAIN |
                                        LIBSSH2_KNOWNHOST_KEYENC_RAW |
                                        (hostkey_type & LIBSSH2_HOSTKEY_TYPE_RSA ? LIBSSH2_KNOWNHOST_KEY_SSHRSA : 0) |
                                        (hostkey_type & LIBSSH2_HOSTKEY_TYPE_DSS ? LIBSSH2_KNOWNHOST_KEY_SSHDSS : 0),
                                        NULL);
                                libssh2_knownhost_writefile(hosts,
                                        knownhosts_filename,
                                        LIBSSH2_KNOWNHOST_FILE_OPENSSH);
                                goto free_knownhosts;
                        } else if (keystroke == 'n') {
                                goto free_knownhosts;
                        } else {
                                libssh2_knownhost_free(hosts);
                                ssh_close();
                                return -1;
                        }
                }

                if (hostkey_status == LIBSSH2_KNOWNHOST_CHECK_MISMATCH) {
#ifdef DEBUG_NET
                        fprintf(DEBUG_FILE_HANDLE, "LIBSSH2_KNOWNHOST_CHECK_MISMATCH\n");
#endif /* DEBUG_NET */
                        /*
                         * Entry changed!  Show the user the old and
                         * new and ask if they want to continue and/or
                         * update the key.
                         */
                        knownhost_key = knownhost->key;

                        /* Decode host key from base64 */
                        base64_init_decodestate(&b64_state);
                        knownhost_key_str_n = base64_decode_block(knownhost_key,
                                strlen(knownhost_key), knownhost_key_str,
                                &b64_state);
                        /*
                         * Take MD5 hash.  I'm following the same
                         * procedure as libssh2, using OpenSSL.
                         */
                        libssh2_md5_init(&md5_ctx);
                        libssh2_md5_update(md5_ctx, knownhost_key_str,
                                knownhost_key_str_n);
                        libssh2_md5_final(md5_ctx, knownhost_key_md5);
                        md5_to_string(knownhost_key_md5, knownhost_key_md5_str);

                        /* New entry, ask if the user wants to add it */
                        sprintf(notify_message, _("Host key for %s:%s has changed! "),  host, port);
                                message_lines[0] = notify_message;
                                message_lines[1] = "Old key:";
                                message_lines[2] = knownhost_key_md5_str;
                                message_lines[3] = "";
                                message_lines[4] = "New key:";
                                message_lines[5] = (char *)ssh_server_key_str();
                                message_lines[6] = "";
                                message_lines[7] = "   Update known hosts?  [y/n/Z] ";
                        keystroke = tolower(notify_prompt_form_long(
                                message_lines,
                                _("Host Key Has Changed!"),
                                _(" Y-Connect And Update Key   N-Connect   Z-Disconnect "),
                                Q_TRUE,
                                0.0, "YyNnZz\r", 8));
                        q_cursor_off();

                        if (keystroke == 'y') {
                                /* Delete the old key */
                                libssh2_knownhost_del(hosts, knownhost);

                                /* Add to known_hosts */
                                libssh2_knownhost_addc(hosts, host, NULL,
                                        hostkey, hostkey_length,
                                        NULL, 0,
                                        LIBSSH2_KNOWNHOST_TYPE_PLAIN |
                                        LIBSSH2_KNOWNHOST_KEYENC_RAW |
                                        (hostkey_type & LIBSSH2_HOSTKEY_TYPE_RSA ? LIBSSH2_KNOWNHOST_KEY_SSHRSA : 0) |
                                        (hostkey_type & LIBSSH2_HOSTKEY_TYPE_DSS ? LIBSSH2_KNOWNHOST_KEY_SSHDSS : 0),
                                        NULL);
                                libssh2_knownhost_writefile(hosts,
                                        knownhosts_filename,
                                        LIBSSH2_KNOWNHOST_FILE_OPENSSH);
                                goto free_knownhosts;
                        } else if (keystroke == 'n') {
                                goto free_knownhosts;
                        } else {
                                libssh2_knownhost_free(hosts);
                                ssh_close();
                                return -1;
                        }
                }


free_knownhosts:
                /* Free resources */
                libssh2_knownhost_free(hosts);
                hosts = NULL;
        }

skip_hostkey_check:

        /* Username and password need to be converted to char, not wchar_t */
        snprintf(username, sizeof(username) - 1, "%ls",
                q_status.current_username);
        snprintf(password, sizeof(password) - 1, "%ls",
                q_status.current_password);

        /* Authenticate with username and password */
        rc = libssh2_userauth_password(q_ssh_session, username, password);
        if (rc != 0) {
                emit_ssh_error(q_ssh_session);
                ssh_close();
                return -1;
        }

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "ssh_setup_connection() password authenticated OK\n");
#endif /* DEBUG_NET */

        /* Open channel and shell session */
        q_ssh_channel = libssh2_channel_open_session(q_ssh_session);
        if (q_ssh_channel == NULL) {
                emit_ssh_error(q_ssh_session);
                ssh_close();
                return -1;
        }

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "ssh_setup_connection() channel open\n");
#endif /* DEBUG_NET */

        /*
         * Set the LANG - we do this ahead of the shell, but most hosts I'm
         * looking at seem to override it anyway.  Ugh.
         */
        rc = libssh2_channel_setenv(q_ssh_channel, "LANG", dialer_get_lang());
        if ((rc != 0) && (rc != LIBSSH2_ERROR_CHANNEL_REQUEST_DENIED)) {
                emit_ssh_error(q_ssh_session);
                ssh_close();
                return -1;
        }

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "ssh_setup_connection() LANG passed: %s\n",
                dialer_get_lang());
#endif /* DEBUG_NET */

        rc = libssh2_channel_request_pty_ex(q_ssh_channel, dialer_get_term(),
                strlen(dialer_get_term()), NULL, 0,
                WIDTH, HEIGHT - STATUS_HEIGHT, 0, 0);
        if (rc != 0) {
                emit_ssh_error(q_ssh_session);
                ssh_close();
                return -1;
        }

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "ssh_setup_connection() PTY\n");
#endif /* DEBUG_NET */


        /* Get the shell */
        rc = libssh2_channel_shell(q_ssh_channel);
        if (rc != 0) {
                emit_ssh_error(q_ssh_session);
                ssh_close();
                return -1;
        }

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "ssh_setup_connection() shell open\n");
#endif /* DEBUG_NET */

        /* Set to non-blocking */
        libssh2_channel_set_blocking(q_ssh_channel, 0);

        /* All is OK */
        return fd;
} /* ---------------------------------------------------------------------- */

/* Close SSH session */
static void ssh_close() {
        assert(q_ssh_session != NULL);
        if (q_ssh_channel != NULL) {
                libssh2_channel_free(q_ssh_channel);
                q_ssh_channel = NULL;
        }
        libssh2_session_disconnect(q_ssh_session,
                _("Connection closed by user."));
        libssh2_session_free(q_ssh_session);
        q_ssh_session = NULL;
        if (ssh_server_key != NULL) {
                Xfree(ssh_server_key, __FILE__, __LINE__);
                ssh_server_key = NULL;
        }
} /* ---------------------------------------------------------------------- */

/* Send new screen dimensions to the remote side */
void ssh_resize_screen(const int lines, const int columns) {
#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "ssh_resize_screen() %d cols %d lines\n",
                columns, lines);
#endif /* DEBUG_NET */
        assert(q_ssh_channel != NULL);
        libssh2_channel_request_pty_size(q_ssh_channel, columns, lines);
} /* ---------------------------------------------------------------------- */

static Q_BOOL maybe_readable = Q_FALSE;

/* Flag to indicate some more data MIGHT be ready to read */
Q_BOOL ssh_maybe_readable() {
        return maybe_readable;
} /* ---------------------------------------------------------------------- */

/* Just like read(), but perform the ssh protocol */
ssize_t ssh_read(const int fd, void * buf, size_t count) {
        int readBytes;

#ifdef DEBUG_NET
        fprintf(DEBUG_FILE_HANDLE, "SSH_READ()\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        /* Return read_buffer first - it has the connect message */
        if (read_buffer_n > 0) {
#ifdef DEBUG_NET
                fprintf(DEBUG_FILE_HANDLE, "ssh_read(): direct string bypass: %s\n", read_buffer);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                memcpy(buf, read_buffer, read_buffer_n);
                readBytes = read_buffer_n;
                read_buffer_n = 0;
                return readBytes;
        }

        if (nvt.is_eof == Q_TRUE) {
#ifdef DEBUG_NET
                fprintf(DEBUG_FILE_HANDLE, "ssh_read() : no read because EOF\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                /* Return EOF */
                return 0;
        }

        /*
         * Read some more bytes from the remote side.  By default don't ask
         * about the stderr stream.
         */
        readBytes = libssh2_channel_read(q_ssh_channel,
                buf, count);
        if (readBytes < 1) {
                if (    (       (readBytes == LIBSSH2_ERROR_EAGAIN) ||
                                (readBytes == 0)
                        ) &&
                        (!libssh2_channel_eof(q_ssh_channel))
                ) {
                        /*
                         * The message will be returned on the next
                         * ssh_read()
                         */
                        maybe_readable = Q_TRUE;
#ifdef Q_PDCURSES_WIN32
                        set_errno(WSAEWOULDBLOCK);
#else
                        set_errno(EAGAIN);
#endif /* Q_PDCURSES_WIN32 */
                        goto read_done;
                }

                if (readBytes <= 0) {

#ifdef DEBUG_NET
                        fprintf(DEBUG_FILE_HANDLE, "EOF EOF EOF\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

                        /* Remote end has closed connection */
                        snprintf((char *)read_buffer,
                                sizeof(read_buffer), "%s",
                                _("Connection closed.\r\n"));
                        read_buffer_n = strlen((char *)read_buffer);
                        nvt.is_eof = Q_TRUE;

                        if (readBytes < 0) {
#ifdef DEBUG_NET
                                emit_ssh_error(q_ssh_session);
#endif /* DEBUG_NET */
                                /*
                                 * This is a dead connection, we can't
                                 * select() on it again.
                                 */
                                set_errno(EIO);
                        } else {
                                /*
                                 * The message will be returned on the
                                 * next ssh_read()
                                 */
                                maybe_readable = Q_TRUE;
#ifdef Q_PDCURSES_WIN32
                                set_errno(WSAEWOULDBLOCK);
#else
                                set_errno(EAGAIN);
#endif /* Q_PDCURSES_WIN32 */
                        }
                        return -1;
                }
        }

read_done:
        ;

#ifdef DEBUG_NET
        int i;
        fprintf(DEBUG_FILE_HANDLE, "ssh_read() : read %d bytes (count = %d) (errno = %d):\n", readBytes, (int)count, errno);
        for (i = 0; i < readBytes; i++) {
                fprintf(DEBUG_FILE_HANDLE, " %02x", (((char *)buf)[i] & 0xFF));
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        for (i = 0; i < readBytes; i++) {
                if ((((char *)buf)[i] & 0xFF) >= 0x80) {
                        fprintf(DEBUG_FILE_HANDLE, " %02x", (((char *)buf)[i] & 0xFF));
                } else {
                        fprintf(DEBUG_FILE_HANDLE, " %c ", (((char *)buf)[i] & 0xFF));
                }
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        if (readBytes == 0) {
                /* SSH protocol consumed everything.  Come back again. */
                maybe_readable = Q_TRUE;
#ifdef Q_PDCURSES_WIN32
                set_errno(WSAEWOULDBLOCK);
#else
                set_errno(EAGAIN);
#endif /* Q_PDCURSES_WIN32 */
                return -1;
        }

        if (readBytes == count) {
#ifdef DEBUG_NET
                fprintf(DEBUG_FILE_HANDLE, "MAYBE READABLE: TRUE\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                /*
                 * There might still be more data to read due to
                 * decompression, have process_incoming_data() call us again.
                 */
                maybe_readable = Q_TRUE;
        } else {
#ifdef DEBUG_NET
                fprintf(DEBUG_FILE_HANDLE, "MAYBE READABLE: FALSE\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */
                maybe_readable = Q_FALSE;
        }

        /* We read something, pass it on. */
        return readBytes;
} /* ---------------------------------------------------------------------- */

/* Just like write(), but perform the ssh protocol */
ssize_t ssh_write(const int fd, void * buf, size_t count) {
        int writtenBytes;

        /* Make sure we're supposed to send something */
        assert(count > 0);

        writtenBytes = libssh2_channel_write(q_ssh_channel, buf, count);
        if ((writtenBytes < 0) && (writtenBytes != LIBSSH2_ERROR_EAGAIN)) {
                /* ssh error */
                emit_ssh_error(q_ssh_session);
                /* This will be an error */
                set_errno(EIO);
                return -1;
        }

        if (writtenBytes == 0) {
                /* This isn't EOF yet */
#ifdef Q_PDCURSES_WIN32
                set_errno(WSAEWOULDBLOCK);
#else
                set_errno(EAGAIN);
#endif /* Q_PDCURSES_WIN32 */
                return -1;
        }

#ifdef DEBUG_NET
        int i;
        fprintf(DEBUG_FILE_HANDLE, "ssh_write() : wrote %d bytes (count = %d):\n", writtenBytes, (int)count);
        for (i = 0; i < writtenBytes; i++) {
                fprintf(DEBUG_FILE_HANDLE, " %02x", (((char *)buf)[i] & 0xFF));
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        for (i = 0; i < writtenBytes; i++) {
                if ((((char *)buf)[i] & 0xFF) >= 0x80) {
                        fprintf(DEBUG_FILE_HANDLE, " %02x", (((char *)buf)[i] & 0xFF));
                } else {
                        fprintf(DEBUG_FILE_HANDLE, " %c ", (((char *)buf)[i] & 0xFF));
                }
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_NET */

        /* We wrote something, pass it on. */
        return writtenBytes;
} /* ---------------------------------------------------------------------- */

/*
cdecoder.c - c source to a base64 decoding algorithm implementation

This is part of the libb64 project, and has been placed in the public domain.
For details, see http://sourceforge.net/projects/libb64

Copied into qodem by Kevin Lamonte.  Originally written by Chris Venter
chris.venter@gmail.com : http://man9.wordpress.com
*/
static int base64_decode_value(char value_in)
{
        static const char decoding[] = {62,-1,-1,-1,63,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-2,-1,-1,-1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,-1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51};
        static const char decoding_size = sizeof(decoding);
        value_in -= 43;
        if (value_in < 0 || value_in > decoding_size) return -1;
        return decoding[(int)value_in];
}

static void base64_init_decodestate(base64_decodestate* state_in)
{
        state_in->step = step_a;
        state_in->plainchar = 0;
}

static int base64_decode_block(const char* code_in, const int length_in, char* plaintext_out, base64_decodestate* state_in)
{
        const char* codechar = code_in;
        char* plainchar = plaintext_out;
        char fragment;

        *plainchar = state_in->plainchar;

        switch (state_in->step)
        {
                while (1)
                {
        case step_a:
                        do {
                                if (codechar == code_in+length_in)
                                {
                                        state_in->step = step_a;
                                        state_in->plainchar = *plainchar;
                                        return plainchar - plaintext_out;
                                }
                                fragment = (char)base64_decode_value(*codechar++);
                        } while (fragment < 0);
                        *plainchar    = (fragment & 0x03f) << 2;
        case step_b:
                        do {
                                if (codechar == code_in+length_in)
                                {
                                        state_in->step = step_b;
                                        state_in->plainchar = *plainchar;
                                        return plainchar - plaintext_out;
                                }
                                fragment = (char)base64_decode_value(*codechar++);
                        } while (fragment < 0);
                        *plainchar++ |= (fragment & 0x030) >> 4;
                        *plainchar    = (fragment & 0x00f) << 4;
        case step_c:
                        do {
                                if (codechar == code_in+length_in)
                                {
                                        state_in->step = step_c;
                                        state_in->plainchar = *plainchar;
                                        return plainchar - plaintext_out;
                                }
                                fragment = (char)base64_decode_value(*codechar++);
                        } while (fragment < 0);
                        *plainchar++ |= (fragment & 0x03c) >> 2;
                        *plainchar    = (fragment & 0x003) << 6;
        case step_d:
                        do {
                                if (codechar == code_in+length_in)
                                {
                                        state_in->step = step_d;
                                        state_in->plainchar = *plainchar;
                                        return plainchar - plaintext_out;
                                }
                                fragment = (char)base64_decode_value(*codechar++);
                        } while (fragment < 0);
                        *plainchar++   |= (fragment & 0x03f);
                }
        }
        /* control should not reach here */
        return plainchar - plaintext_out;
}

#endif /* Q_LIBSSH2 */
