/*
 * qodem.c
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

#include "qcurses.h"
#include "common.h"

#include <stdlib.h>
#ifdef Q_PDCURSES_WIN32
#include <tchar.h>
#include <windows.h>
#else
#include <signal.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <pwd.h>
#endif /* Q_PDCURSES_WIN32 */
#include <errno.h>
#include <string.h>
#include <assert.h>

#ifdef __BORLANDC__
#include <io.h>         /* read(), write() */
#endif

#ifdef Q_LIBSSH2
#include <sys/time.h>
#include <libssh2.h>
#endif /* Q_LIBSSH */

#include "qodem.h"
#include "screen.h"
#include "protocols.h"
#include "console.h"
#include "keyboard.h"
#include "translate.h"
#include "music.h"
#include "states.h"
#include "options.h"
#include "dialer.h"
#include "script.h"
#include "help.h"
#include "netclient.h"
#include "getopt.h"

/* #define DEBUG_IO 1 */
#undef DEBUG_IO
#ifdef DEBUG_IO
#include <stdio.h>
static FILE * DEBUG_IO_HANDLE = NULL;
#endif

/*
 * Define this to enable random line noise on both sides of the link.
 * There is a random chance that 1 in every line_noise_per_bytes/2 is
 * turned into crap.
 */
/* #define LINE_NOISE 1 */
#undef LINE_NOISE
#ifdef LINE_NOISE
static const int line_noise_per_bytes = 10000;
static Q_BOOL noise_stop = Q_FALSE;
#endif /* LINE_NOISE */

/* Global exit return code */
static int q_exitrc = EXIT_OK;

/* Global child TTY name */
char * q_child_ttyname = NULL;

/* Global child tty file descriptor */
int q_child_tty_fd = -1;

/* Global child process ID */
pid_t q_child_pid = -1;

#ifdef Q_PDCURSES_WIN32
/*
 * Win32 case: we have to create pipes that are connected to the child
 * process' streams.  These are initialized in spawn_process() and
 * stored in dialer.c.
 */
extern HANDLE q_child_stdin;
extern HANDLE q_child_stdout;
extern HANDLE q_child_process;
extern HANDLE q_child_thread;
extern HANDLE q_script_stdout;
#endif /* Q_PDCURSES_WIN32 */

/* Global status struct */
struct q_status_struct q_status;

/* Global width value */
int WIDTH;

/* Global height value */
int HEIGHT;

/* Global status height value */
int STATUS_HEIGHT;

/* Global base directory */
char * q_home_directory = NULL;

/* Global screensaver timeout */
int q_screensaver_timeout;

/* Global keepalive timeout */
int q_keepalive_timeout;
char q_keepalive_bytes[128];
unsigned int q_keepalive_bytes_n;

/* Raw input buffer */
static unsigned char q_buffer_raw[Q_BUFFER_SIZE];
static int q_buffer_raw_n = 0;

/* Transfer output buffer */
static unsigned char q_transfer_buffer_raw[Q_BUFFER_SIZE];
static int q_transfer_buffer_raw_n = 0;

/* These are used by the select() call in data_handler() */
static fd_set readfds;                  /* read FD_SET */
static fd_set writefds;                 /* write FD_SET */
static fd_set exceptfds;                /* exception FD_SET */

/* The last time we saw data. */
static time_t data_time;

/* The last time we sent data. */
time_t q_data_sent_time;

/* The initial call to make as requested by the command line arguments */
static struct q_phone_struct initial_call;
static int dial_phonebook_entry_n;

/* For the --play and --play-exit arguments */
static unsigned char * play_music_string = NULL;
static Q_BOOL play_music_exit = Q_FALSE;

/* Command-line options */
static struct option q_getopt_long_options[] = {
        {"dial",                1,      0,      0},
        {"connect",             1,      0,      0},
        {"connect-method",      1,      0,      0},
        {"enable-capture",      1,      0,      0},
        {"enable-logging",      1,      0,      0},
        {"help",                0,      0,      0},
        {"username",            1,      0,      0},
        {"play",                1,      0,      0},
        {"play-exit",           0,      0,      0},
        {"version",             0,      0,      0},
        {0,                     0,      0,      0}
};

/* All outgoing data goes through here */
int qodem_write(const int fd, char * data, const int data_n, Q_BOOL sync) {
#ifdef Q_PDCURSES_WIN32
        char notify_message[DIALOG_MESSAGE_SIZE];
#endif /* Q_PDCURSES_WIN32 */
        int i;
        int rc;
        int old_errno;

        if (data_n == 0) {
                /* NOP */
                return 0;
        }

        /* Quicklearn */
        if (q_status.quicklearn == Q_TRUE) {
                for (i = 0; i < data_n; i++) {
                        quicklearn_send_byte(data[i]);
                }
        }

#ifndef Q_NO_SERIAL
        /* Mark/space parity */
        if (Q_SERIAL_OPEN && (q_serial_port.parity == Q_PARITY_MARK)) {
                /* Outgoing data as MARK parity:  set the 8th bit */
                for (i = 0; i < data_n; i++) {
                        data[i] |= 0x80;
                }
        }
        if (Q_SERIAL_OPEN && (q_serial_port.parity == Q_PARITY_SPACE)) {
                /* Outgoing data as SPACE parity:  strip the 8th bit */
                for (i = 0; i < data_n; i++) {
                        data[i] &= 0x7F;
                }
        }
#endif /* Q_NO_SERIAL */

#ifdef DEBUG_IO
        fprintf(DEBUG_IO_HANDLE, "qodem_write() OUTPUT bytes: ");
        for (i = 0; i < data_n; i++) {
                fprintf(DEBUG_IO_HANDLE, "%02x ", data[i] & 0xFF);
        }
        fprintf(DEBUG_IO_HANDLE, "\n");
        fprintf(DEBUG_IO_HANDLE, "qodem_write() OUTPUT bytes (ASCII): ");
        for (i = 0; i < data_n; i++) {
                fprintf(DEBUG_IO_HANDLE, "%c ", data[i] & 0xFF);
        }
        fprintf(DEBUG_IO_HANDLE, "\n");
        fflush(DEBUG_IO_HANDLE);
#endif

        /* Write bytes out */
        /* Which function to call depends on the connection method */
        if (    (       (q_status.dial_method == Q_DIAL_METHOD_TELNET) &&
                        (net_is_connected() == Q_TRUE)) ||
                (       (       (q_program_state == Q_STATE_HOST) ||
                                (q_host_active == Q_TRUE)) &&
                        (q_host_type == Q_HOST_TYPE_TELNETD))
        ) {
                /* Telnet */
                rc = telnet_write(fd, data, data_n);
        } else if ((q_status.dial_method == Q_DIAL_METHOD_RLOGIN) && (net_is_connected() == Q_TRUE)) {
                /* Rlogin */
                rc = rlogin_write(fd, data, data_n);
        } else if (     (       (q_status.dial_method == Q_DIAL_METHOD_SOCKET) &&
                        (net_is_connected() == Q_TRUE)) ||
                (       (       (q_program_state == Q_STATE_HOST) ||
                        (q_host_active == Q_TRUE)) &&
                        (q_host_type == Q_HOST_TYPE_SOCKET))
        ) {
                /* Socket */
                rc = raw_write(fd, data, data_n);
#ifdef Q_LIBSSH2
        } else if (     (q_status.dial_method == Q_DIAL_METHOD_SSH) &&
                        (net_is_connected() == Q_TRUE)
        ) {
                /* SSH */
                rc = ssh_write(fd, data, data_n);
#endif /* Q_LIBSSH2 */
        } else {
#ifdef Q_PDCURSES_WIN32
                /*
                 * If wrapping a process (e.g. LOCAL or CMDLINE),
                 * write to the q_child_stdin handle.
                 */
                if (    (q_status.dial_method == Q_DIAL_METHOD_COMMANDLINE) ||
                        (q_status.dial_method == Q_DIAL_METHOD_SHELL)
                ) {
                        DWORD bytes_written = 0;
                        if (WriteFile(q_child_stdin, data, data_n,
                                     &bytes_written, NULL) == TRUE) {
                                rc = bytes_written;
#ifdef DEBUG_IO
                                fprintf(DEBUG_IO_HANDLE, "qodem_write() WriteFile() %d bytes written\n", bytes_written);
                                fflush(DEBUG_IO_HANDLE);
#endif
                                /* Force this sucker to flush */
                                FlushFileBuffers(q_child_stdin);
                        } else {
                                /* Error in write */
                                snprintf(notify_message, sizeof(notify_message), _("Call to WriteFile() failed: %d (%s)"), GetLastError(), strerror(GetLastError()));
                                notify_form(notify_message, 0);
                                rc = -1;
                        }

                } else {
#ifdef DEBUG_IO
                        fprintf(DEBUG_IO_HANDLE, "qodem_write() write() %d bytes to fd %d\n", data_n, fd);
                        fflush(DEBUG_IO_HANDLE);
#endif
                        /* Everyone else */
                        rc = write(fd, data, data_n);
                }
#else
                /* Everyone else */
                rc = write(fd, data, data_n);
#endif /* Q_PDCURSES_WIN32 */
        }
        old_errno = errno;
        if (rc < 0) {
#ifdef DEBUG_IO
                fprintf(DEBUG_IO_HANDLE, "qodem_write() write() error %s (%d)\n", strerror(errno), errno);
                fflush(DEBUG_IO_HANDLE);
#endif
        }

        /* Reset clock for keepalive/idle timeouts */
        time(&q_data_sent_time);

        errno = old_errno;
        return rc;
} /* ---------------------------------------------------------------------- */

/* All incoming data goes through here */
static ssize_t qodem_read(const int fd, void * buf, size_t count) {
#ifdef Q_PDCURSES_WIN32
        char notify_message[DIALOG_MESSAGE_SIZE];
#endif /* Q_PDCURSES_WIN32 */

        /* Which function to call depends on the connection method */
        if (    (       (q_status.dial_method == Q_DIAL_METHOD_TELNET) &&
                        (net_is_connected() == Q_TRUE)) ||
                (       (       (q_program_state == Q_STATE_HOST) ||
                                (q_host_active == Q_TRUE)) &&
                        (q_host_type == Q_HOST_TYPE_TELNETD))
        ) {
                /* Telnet */
                return telnet_read(fd, buf, count);
        }
        if ((q_status.dial_method == Q_DIAL_METHOD_RLOGIN) && (net_is_connected() == Q_TRUE)) {
                /* Rlogin */
                if (FD_ISSET(fd, &exceptfds)) {
                        return rlogin_read(fd, buf, count, Q_TRUE);
                }
                return rlogin_read(fd, buf, count, Q_FALSE);
        }
        if (    (q_status.dial_method == Q_DIAL_METHOD_SOCKET) ||
                (       (       (q_program_state == Q_STATE_HOST) ||
                                (q_host_active == Q_TRUE)) &&
                        (q_host_type == Q_HOST_TYPE_SOCKET))
        ) {
                /* Socket */
                return raw_read(fd, buf, count);
        }
#ifdef Q_LIBSSH2
        if (    (q_status.dial_method == Q_DIAL_METHOD_SSH) &&
                (net_is_connected() == Q_TRUE)
        ) {
                /* SSH */
                return ssh_read(fd, buf, count);
        }
#endif /* Q_LIBSSH2 */

#ifdef Q_PDCURSES_WIN32
        /*
         * If wrapping a process (e.g. LOCAL or CMDLINE), read from the
         * q_child_stdout handle.
         */
        if ((q_status.online == Q_TRUE) &&
                ((q_status.dial_method == Q_DIAL_METHOD_COMMANDLINE) ||
                 (q_status.dial_method == Q_DIAL_METHOD_SHELL))
        ) {
                assert(q_child_process != NULL);
                DWORD actual_bytes = 0;
                if (PeekNamedPipe(q_child_stdout, NULL, 0, NULL,
                                  &actual_bytes, NULL) == 0) {

                        /* Error peeking */
                        if (GetLastError() == ERROR_BROKEN_PIPE) {
                                /* This is EOF */
                                SetLastError(EIO);
                                return -1;
                        }

                        snprintf(notify_message, sizeof(notify_message), _("Call to PeekNamedPipe() failed: %d (%s)"), GetLastError(), strerror(GetLastError()));
                        notify_form(notify_message, 0);
                        return -1;
                } else {
#ifdef DEBUG_IO
                        fprintf(DEBUG_IO_HANDLE, "qodem_read() PeekNamedPipe: %d bytes available\n", actual_bytes);
                        fflush(DEBUG_IO_HANDLE);
#endif
                        if (actual_bytes == 0) {
                                errno = EAGAIN;
                                return -1;
                        } else if (actual_bytes > count) {
                                actual_bytes = count;
                        }
                        DWORD bytes_read = 0;
                        if (ReadFile(q_child_stdout, buf, actual_bytes,
                                     &bytes_read, NULL) == TRUE) {
                                return bytes_read;
                        } else {
                                /* Error in read */
                                snprintf(notify_message, sizeof(notify_message), _("Call to ReadFile() failed: %d (%s)"), GetLastError(), strerror(GetLastError()));
                                notify_form(notify_message, 0);
                                return -1;
                        }
                }
        }
#endif /* Q_PDCURSES_WIN32 */

        /* Everyone else */
        return read(fd, buf, count);
} /* ---------------------------------------------------------------------- */

/* Close remote connection */
void close_connection() {
        /* How to close depends on the connection method */
        if (net_is_connected() == Q_TRUE) {
                /* Telnet, Rlogin, SOCKET, SSH */
                net_close();
                return;
        }

#ifdef Q_PDCURSES_WIN32
        /* Win32 case */
        /* Terminate process */
        assert(q_child_process != NULL);
        TerminateProcess(q_child_process, -1);
#else
        /* Killing -1 kills EVERYTHING.  Not good! */
        assert(q_child_pid != -1);
        kill(q_child_pid, SIGHUP);
#endif /* Q_PDCURSES_WIN32 */
} /* ---------------------------------------------------------------------- */

static char * usage_string() {
        return _(""
"'qodem' is a terminal connection manager with scrollback, capture,\n"
"and basic scripting support.\n"
"\n"
"Usage: qodem [OPTIONS] { [--connect] | [command line] }\n"
"\n"
"Options:\n"
"\n"
"If a long option shows an argument as mandatory, then it is mandatory\n"
"for the equivalent short option also.  Similarly for optional arguments.\n"
"\n"
"      --dial n                        Immediately connect to the phonebook\n"
"                                      entry numbered n.\n"
"\n"
"      --connect HOST                  Immediately open a connection to "
"HOST.\n"
"                                      The default connection method is \"ssh"
"\".\n"
"      --connect-method METHOD         Use METHOD to connect for the --"
"connect\n"
"                                      option.  Valid values are\n"
"                                      \"ssh\", \"rlogin\", \"telnet,"
"\",\n"
"                                      and \"shell\".\n"
"      --enable-capture FILENAME       Capture the entire session and save "
"to\n"
"                                      FILENAME.\n"
"      --enable-logging FILENAME       Enable the session log and save to\n"
"                                      FILENAME.\n"
"      --username USERNAME             Log in as USERNAME\n"
"      --play MUSIC                    Play MUSIC as ANSI Music\n"
"      --play-exit                     Immediately exit after playing MUSIC\n"
"      --version                       Version\n"
"  -h, --help                          This help screen\n"
"\n"
"qodem can also open a raw shell with the command line given.\n"
"So for example 'qodem --connect my.host --connect-method ssh' is\n"
"equivalent to 'qodem ssh my.host' .  The --connect option cannot\n"
"be used if a command line is specified.\n"
"\n");
} /* ---------------------------------------------------------------------- */

/*
 * check_for_help - see if the user asked for help or version information
 */
static int check_for_help(int argc, char * const argv[]) {
        int i;

        for (i = 0; i < argc; i++) {

                /* Special case: help means exit */
                if (strncmp(argv[i], "--help", strlen("--help")) == 0) {
                        printf("%s", usage_string());
                        return EXIT_HELP;
                }
                if (strncmp(argv[i], "-h", strlen("-h")) == 0) {
                        printf("%s", usage_string());
                        return EXIT_HELP;
                }
                if (strncmp(argv[i], "-?", strlen("-?")) == 0) {
                        printf("%s", usage_string());
                        return EXIT_VERSION;
                }

        }

        /* The user did not ask for help */
        return 0;
} /* ---------------------------------------------------------------------- */

/*
 * process_command_line_option
 */
static void process_command_line_option(const char * option, const char * value) {

        wchar_t value_wchar[128];

        /*
         fprintf(stdout, "OPTION=%s VALUE=%s\n", option, value);
         */

        /* Special case: help means exit */
        if (strncmp(option, "help", strlen("help")) == 0) {
                printf("%s", usage_string());
                q_program_state = Q_STATE_EXIT;
        }

        /* Special case: version means exit */
        if (strncmp(option, "version", strlen("version")) == 0) {
                printf("%s", _(""
"qodem version 1.0beta\n"
"Copyright(c) 2012 Kevin Lamonte\n"
"\n"
"This program is free software; you can redistribute it and/or modify\n"
"it under the terms of the GNU General Public License as published by\n"
"the Free Software Foundation; either version 2 of the License, or\n"
"(at your option) any later version.\n"
"\n"));
                q_program_state = Q_STATE_EXIT;
        }

        if (strncmp(option, "enable-capture", strlen("enable-capture")) == 0) {
                start_capture(value);
        }

        if (strncmp(option, "enable-logging", strlen("enable-logging")) == 0) {
                start_logging(value);
        }

        if (strcmp(option, "dial") == 0) {
                dial_phonebook_entry_n = atoi(value);
                if (errno == EINVAL) {
                        dial_phonebook_entry_n = -1;
                }
        }

        if (strcmp(option, "play") == 0) {
                play_music_string = (unsigned char *)Xstrdup(value, __FILE__, __LINE__);
        }

        if (strcmp(option, "play-exit") == 0) {
                play_music_exit = Q_TRUE;
        }

        if (strcmp(option, "connect") == 0) {
                initial_call.address = (char *)value;
                memset(value_wchar, 0, sizeof(value_wchar));
                mbstowcs(value_wchar, value, strlen(value));
                initial_call.name = Xwcsdup(value_wchar, __FILE__, __LINE__);
        }

        if (strncmp(option, "username", strlen("username")) == 0) {
                memset(value_wchar, 0, sizeof(value_wchar));
                mbstowcs(value_wchar, value, strlen(value));
                initial_call.username = Xwcsdup(value_wchar, __FILE__, __LINE__);
        }

        if (strncmp(option, "connect-method", strlen("connect-method")) == 0) {
                initial_call.port = "";
                if (strncmp(value, "ssh", strlen("ssh")) == 0) {
                        initial_call.method = Q_DIAL_METHOD_SSH;
                        initial_call.port = "22";
                } else if (strncmp(value, "shell", strlen("shell")) == 0) {
                        initial_call.method = Q_DIAL_METHOD_SHELL;
                        initial_call.address = "";
                } else if (strncmp(value, "rlogin", strlen("rlogin")) == 0) {
                        initial_call.method = Q_DIAL_METHOD_RLOGIN;
                } else if (strncmp(value, "telnet", strlen("telnet")) == 0) {
                        initial_call.method = Q_DIAL_METHOD_TELNET;
                        initial_call.port = "23";
                } else if (strncmp(value, "socket", strlen("socket")) == 0) {
                        initial_call.method = Q_DIAL_METHOD_SOCKET;
                        initial_call.port = "23";
                /*
                } else if (strncmp(value, "tn3270", strlen("tn3270")) == 0) {
                        initial_call.method = Q_DIAL_METHOD_TN3270;
                 */
                }
        }


} /* ---------------------------------------------------------------------- */

/*
 * qlog - emit a line to the session log
 */
void qlog(const char * format, ...) {
        char outbuf[SESSION_LOG_LINE_SIZE];
        time_t current_time;
        va_list arglist;

        if (q_status.logging == Q_FALSE) {
                return;
        }

        time(&current_time);
        strftime(outbuf, sizeof(outbuf), "[%Y-%m-%d %H:%M:%S] ", localtime(&current_time));

        va_start(arglist, format);
        vsprintf((char *)(outbuf+strlen(outbuf)), format, arglist);
        va_end(arglist);

        fprintf(q_status.logging_file, "%s", outbuf);
        fflush(q_status.logging_file);
} /* ---------------------------------------------------------------------- */

/*
 * cleanup_connection - shutdown q_child_tty_fd as needed
 */
void cleanup_connection() {

        if (net_is_connected() == Q_TRUE) {
                /* We did the connection */
                close_connection();
                q_child_tty_fd = -1;
                qlog(_("Remote end closed connection\n"));
        } else {
#ifdef Q_PDCURSES_WIN32
                /* Win32 case */
                assert(q_child_tty_fd == -1);

                DWORD status;
                if (GetExitCodeProcess(q_child_process, &status) == TRUE) {
                        /* Got return code */
                        if (status == STILL_ACTIVE) {
                                /*
                                 * Process thinks it's still running, DIE!
                                 */
                                TerminateProcess(q_child_process, -1);
                                status = -1;
                                qlog(_("Connection forcibly terminated: still thinks it is alive.\n"));
                        } else {
                                qlog(_("Connection exited with RC=%u\n"), status);
                        }
                } else {
                        /*
                         * Can't get process exit code
                         */
                        TerminateProcess(q_child_process, -1);
                        qlog(_("Connection forcibly terminated: unable to get exit code.\n"));
                }

                /* Close pipes */
                CloseHandle(q_child_stdin);
                q_child_stdin = NULL;
                CloseHandle(q_child_stdout);
                q_child_stdout = NULL;
                CloseHandle(q_child_process);
                q_child_process = NULL;
                CloseHandle(q_child_thread);
                q_child_thread = NULL;

#else
                int status;

                /* Close pty */
                close(q_child_tty_fd);
                q_child_tty_fd = -1;
                Xfree(q_child_ttyname, __FILE__, __LINE__);
                wait4(q_child_pid, &status, WNOHANG, NULL);
                if (WIFEXITED(status)) {
                        qlog(_("Connection exited with RC=%u\n"), WEXITSTATUS(status));
                } else if (WIFSIGNALED(status)) {
                        qlog(_("Connection exited with signal=%u\n"), WTERMSIG(status));
                }
                q_child_pid = -1;
#endif /* Q_PDCURSES_WIN32 */
        }

        if (q_program_state != Q_STATE_HOST) {
                /* Increment stats */
                if (q_current_dial_entry != NULL) {
                        q_current_dial_entry->times_on++;
                        time(&q_current_dial_entry->last_call);
                }
        }

        /* Offline now */
        q_status.online = Q_FALSE;

        /* See if the user wanted to disconnect */
        if (q_status.exit_on_disconnect == Q_TRUE) {
                q_program_state = Q_STATE_EXIT;
        }
} /* ---------------------------------------------------------------------- */

#ifdef Q_LIBSSH2
/*
 * libssh requires an extra read() that is invisible to select() in order
 * for its channel_read_nonblocking() to return EOF.
 */
#ifdef Q_PDCURSES_WIN32
static long ssh_last_time = 1000000;
#else
static suseconds_t ssh_last_time = 1000000;
#endif /* Q_PDCURSES_WIN32 */
static struct timeval ssh_tv;

#endif /* Q_LIBSSH2 */

/* Returns Q_TRUE if the fd is readable */
static Q_BOOL is_readable(int fd) {

#ifdef Q_PDCURSES_WIN32
        char notify_message[DIALOG_MESSAGE_SIZE];
#endif /* Q_PDCURSES_WIN32 */

        if (FD_ISSET(fd, &readfds)) {
                return Q_TRUE;
        }
        /* Rlogin special case: look for OOB data */
        if ((q_status.dial_method == Q_DIAL_METHOD_RLOGIN) && (net_is_connected() == Q_TRUE)) {
                if (FD_ISSET(fd, &exceptfds)) {
                        return Q_TRUE;
                }
        }

#ifdef Q_LIBSSH2

        /* SSH special case: see if we should read again anyway */
        if ((q_status.dial_method == Q_DIAL_METHOD_SSH) && (net_is_connected() == Q_TRUE)) {
                if (fd == q_child_tty_fd) {
                        if (ssh_maybe_readable() == Q_TRUE) {
                                return Q_TRUE;
                        }
                        /*
                         * ALWAYS try to read after 0.25 seconds, even if
                         * there is "nothing" on the socket itself.
                         */
                        gettimeofday(&ssh_tv, NULL);
                        if ((ssh_tv.tv_usec < ssh_last_time) || (ssh_tv.tv_usec - ssh_last_time > 250000)) {
#ifdef DEBUG_IO
                                fprintf(DEBUG_IO_HANDLE, "SSH OVERRIDE: check socket anyway\n");
                                fflush(DEBUG_IO_HANDLE);
#endif
                                return Q_TRUE;
                        }
                }
        }
#endif /* Q_LIBSSH2 */

#ifdef Q_PDCURSES_WIN32
        if (    (q_status.online == Q_TRUE) &&
                ((q_status.dial_method == Q_DIAL_METHOD_SHELL) ||
                 (q_status.dial_method == Q_DIAL_METHOD_COMMANDLINE))
        ) {
                /*
                 * Check for data on child process.  If we have some, set
                 * have_data to true so we can skip the select() call.
                 */
                DWORD actual_bytes = 0;
                if (PeekNamedPipe(q_child_stdout, NULL, 0, NULL,
                                &actual_bytes, NULL) == 0) {
                        /* Error peeking */
                        if (GetLastError() == ERROR_BROKEN_PIPE) {
                                /*
                                 * This is EOF.  Say that it's readable so
                                 * that qodem_read() can return the 0.
                                 */
                                errno = EIO;
                                return Q_TRUE;
                        }
                        snprintf(notify_message, sizeof(notify_message), _("Call to PeekNamedPipe() failed: %d (%s)"), GetLastError(), strerror(GetLastError()));
                        notify_form(notify_message, 0);
                } else {
#ifdef DEBUG_IO
                        fprintf(DEBUG_IO_HANDLE, "is_readable() PeekNamedPipe: %d bytes available\n", actual_bytes);
                        fflush(DEBUG_IO_HANDLE);
#endif
                        if (actual_bytes > 0) {
                                /*
                                 * There's something there, go after it.
                                 */
                                return Q_TRUE;
                        } else {
                                return Q_FALSE;
                        }
                }
        }
#endif /* Q_PDCURSES_WIN32 */


        return Q_FALSE;
} /* ---------------------------------------------------------------------- */

/*
 * process_incoming_data - grab data from the child tty
 */
static void process_incoming_data() {
        int rc;                         /* qodem_read() */
        int n;
        int unprocessed_n;
        char time_string[SHORT_TIME_SIZE];
        time_t current_time;
        int hours, minutes, seconds;
        double connect_time;
#if defined(DEBUG_IO) || !defined(Q_NO_SERIAL)
        int i;
#endif /* Q_NO_SERIAL */
        char notify_message[DIALOG_MESSAGE_SIZE];

        Q_BOOL wait_on_script = Q_FALSE;

        /*
         * For scripts: don't read any more data from the remote side if there
         * is no more room in the print buffer side.
         */
        if (q_program_state == Q_STATE_SCRIPT_EXECUTE) {
                if (q_running_script.print_buffer_full == Q_TRUE) {
                        wait_on_script = Q_TRUE;
                }
        }

#ifdef DEBUG_IO
        fprintf(DEBUG_IO_HANDLE, "IF CHECK: %s %s %s %s\n",
#ifdef Q_NO_SERIAL
                "N/A",
#else
                (q_status.serial_open == Q_TRUE ? "true" : "false"),
#endif
                (q_status.online == Q_TRUE ? "true" : "false"),
                (is_readable(q_child_tty_fd) == Q_TRUE ? "true" : "false"),
                (wait_on_script == Q_FALSE ? "true" : "false"));
#endif

        if (    (Q_SERIAL_OPEN || (q_status.online == Q_TRUE)) &&
                (is_readable(q_child_tty_fd) == Q_TRUE) &&
                (wait_on_script == Q_FALSE)
        ) {

#ifdef Q_LIBSSH2
                ssh_last_time = ssh_tv.tv_usec;
#endif /* Q_LIBSSH2 */

                /*
                 * I read() from q_child_tty_fd into q_raw_buffer,
                 * then depending on my state I call another function
                 * to handle it.
                 */
                n = Q_BUFFER_SIZE - q_buffer_raw_n;

#ifdef DEBUG_IO
                fprintf(DEBUG_IO_HANDLE, "n = %d\n", n);
#endif

                if (n > 0) {

                        /* Clear errno */
                        errno = 0;
                        rc = qodem_read(q_child_tty_fd, q_buffer_raw + q_buffer_raw_n, n);

#ifdef DEBUG_IO
                        fprintf(DEBUG_IO_HANDLE, "rc = %d errno=%d\n", rc,
                                get_errno());
                        fflush(DEBUG_IO_HANDLE);
#endif

                        if (rc < 0) {
                                if (get_errno() == EIO) {
                                        /* This is EOF. */
                                        rc = 0;
#ifdef Q_PDCURSES_WIN32
                                } else if ((get_errno() == WSAEWOULDBLOCK) &&
#else
                                } else if ((get_errno() == EAGAIN) &&
#endif /* Q_PDCURSES_WIN32 */
                                        (       ((q_status.dial_method == Q_DIAL_METHOD_TELNET) && (net_is_connected() == Q_TRUE)) ||
                                                ((q_status.dial_method == Q_DIAL_METHOD_SOCKET) && (net_is_connected() == Q_TRUE)) ||
                                                ((q_status.dial_method == Q_DIAL_METHOD_RLOGIN) && (net_is_connected() == Q_TRUE)) ||
#ifdef Q_LIBSSH2
                                                ((q_status.dial_method == Q_DIAL_METHOD_SSH) && (net_is_connected() == Q_TRUE)) ||
#endif /* Q_LIBSSH2 */
                                                ((q_host_active == Q_TRUE) && (q_host_type == Q_HOST_TYPE_TELNETD)) ||
                                                ((q_host_active == Q_TRUE) && (q_host_type == Q_HOST_TYPE_SOCKET))
                                        )
                                ) {
                                        /*
                                         * All of the bytes available
                                         * were for a telnet / rlogin
                                         * / ssh / etc. layer, nothing
                                         * for us here.
                                         */
                                        goto no_data;
#ifdef Q_PDCURSES_WIN32
                                } else if (get_errno() == WSAECONNRESET) {
#else
                                } else if (errno == ECONNRESET) {
#endif /* Q_PDCURSES_WIN32 */
                                        /* "Connection reset by peer".  This is EOF. */
                                        rc = 0;
                                } else {
                                        snprintf(notify_message, sizeof(notify_message), _("Call to read() failed: %d (%s)"),
                                                get_errno(), get_strerror(get_errno()));
                                        notify_form(notify_message, 0);
                                        /* Treat it like EOF.  This will terminate the connection. */
                                        rc = 0;
                                }
                        }
                        if (rc == 0) {
                                /* EOF */
#ifndef Q_NO_SERIAL
                                if (Q_SERIAL_OPEN) {
                                        /* Modem/serial */
                                        close_serial_port();
                                } else {
                                        /* All other */
                                        cleanup_connection();
                                }
#else
                                /* All other */
                                cleanup_connection();
#endif /* Q_NO_SERIAL */
                                /* Kill quicklearn script */
                                stop_quicklearn();

                                /* Kill running script */
                                script_stop();

                                /* Compute time */
                                /* time_string needs to be hours/minutes/seconds CONNECTED */
                                time(&current_time);
                                connect_time = difftime(current_time, q_status.connect_time);
                                hours = connect_time / 3600;
                                minutes = ((int)connect_time % 3600) / 60;
                                seconds = (int)connect_time % 60;
                                snprintf(time_string, sizeof(time_string), "%02u:%02u:%02u", hours, minutes, seconds);

                                qlog(_("CONNECTION CLOSED. Total time online: %s\n"), time_string);

                                /*
                                 * If we died before switching out of DIALING into CONNECTED,
                                 * (e.g. at Q_DIAL_CONNECTED in phonebook_refresh()), then
                                 * switch back to phonebook mode.
                                 */
                                if (q_program_state == Q_STATE_DIALER) {
                                        switch_state(Q_STATE_PHONEBOOK);
                                        q_screen_dirty = Q_TRUE;
                                        /*
                                         * We need to explicitly call refresh_hanlder() because
                                         * phonebook_keyboard_handler() blocks.
                                         */
                                        refresh_handler();
                                }

                                /* Wipe out current dial entry */
                                q_current_dial_entry = NULL;
                                return;
                        }

                        time(&data_time);

#ifndef Q_NO_SERIAL
                        /* Mark/space parity */
                        if (Q_SERIAL_OPEN && (q_serial_port.parity == Q_PARITY_MARK)) {
                                /* Incoming data as MARK parity:  strip the 8th bit */
                                for (i = 0; i < rc; i++) {
                                        q_buffer_raw[q_buffer_raw_n + i] &= 0x7F;
                                }
                        }
#endif /* Q_NO_SERIAL */

#ifdef LINE_NOISE
                        for (i = 0; i < rc; i++) {
                                int do_noise = random() % line_noise_per_bytes;
                                if ((do_noise == 1) && (noise_stop == Q_FALSE)) {
                                        q_buffer_raw[i] = random() % 0xFF;
                                        noise_stop = Q_TRUE;
                                        break;
                                }
                        }
#endif

                        /* Record # of new bytes in */
                        q_buffer_raw_n += rc;

#ifdef DEBUG_IO
                        fprintf(DEBUG_IO_HANDLE, "INPUT bytes: ");
                        for (i=0; i<q_buffer_raw_n; i++) {
                                fprintf(DEBUG_IO_HANDLE, "%02x ", q_buffer_raw[i] & 0xFF);
                        }
                        fprintf(DEBUG_IO_HANDLE, "\n");
                        fprintf(DEBUG_IO_HANDLE, "INPUT bytes (ASCII): ");
                        for (i=0; i<q_buffer_raw_n; i++) {
                                fprintf(DEBUG_IO_HANDLE, "%c ", q_buffer_raw[i] & 0xFF);
                        }
                        fprintf(DEBUG_IO_HANDLE, "\n");
#endif

                } /* if (n > 0) */
        } /* if (FD_ISSET(q_child_tty_fd, &readfds)) */

no_data:

#ifdef DEBUG_IO
        fprintf(DEBUG_IO_HANDLE, "\n");
        fprintf(DEBUG_IO_HANDLE, "q_program_state: %d q_transfer_buffer_raw_n %d\n", q_program_state, q_transfer_buffer_raw_n);
        if (q_transfer_buffer_raw_n > 0) {
                fprintf(DEBUG_IO_HANDLE, "LEFTOVER OUTPUT\n");
        }
        fprintf(DEBUG_IO_HANDLE, "\n");
#endif

        unprocessed_n = q_buffer_raw_n;

        /*
         * Modem dialer - allow everything to be sent first before looking
         * for more data.
         */
        if (    (q_program_state == Q_STATE_DIALER) &&
                (q_transfer_buffer_raw_n == 0)
        ) {

#ifndef Q_NO_SERIAL
                if (    (q_dial_state == Q_DIAL_CONNECTED) &&
                        (q_current_dial_entry->method == Q_DIAL_METHOD_MODEM)
                ) {
                        /*
                         * UGLY HACK TIME.  Just so that I can display
                         * a message in the Redialer window, I
                         * duplicate the call to
                         * console_process_incoming_data() below.
                         */

                        /* Let the console process the data */
                        console_process_incoming_data(q_buffer_raw, q_buffer_raw_n, &unprocessed_n);

                } else if (q_current_dial_entry->method != Q_DIAL_METHOD_MODEM) {
                        /*
                         * We're doing a network connection, do NOT
                         * consume the data.  Leave it in the buffer
                         * for the console to see later.
                         */

                        /* Do nothing */
                } else {
                        /*
                         * We're talking to the modem before the
                         * connection has been made.
                         */
                        dialer_process_data(q_buffer_raw, q_buffer_raw_n,
                                &unprocessed_n, q_transfer_buffer_raw,
                                &q_transfer_buffer_raw_n,
                                sizeof(q_transfer_buffer_raw));
                }
#endif /* Q_NO_SERIAL */
        }

        /* File transfer */
        if (    (q_program_state == Q_STATE_UPLOAD) ||
                (q_program_state == Q_STATE_UPLOAD_BATCH) ||
                (q_program_state == Q_STATE_DOWNLOAD) ||
                (q_program_state == Q_STATE_SCRIPT_EXECUTE) ||
                (q_program_state == Q_STATE_HOST)
        ) {


                /*
                 * File transfers, scripts, and host mode: run
                 * protocol_process_data() until old
                 * _q_transfer_buffer_raw_n == q_transfer_buffer_raw_n .
                 *
                 * Every time we come through process_incoming_data() we call
                 * protocol_process_data() at least once.
                 */

                int old_q_transfer_buffer_raw_n = -1;
#ifdef DEBUG_IO
                fprintf(DEBUG_IO_HANDLE, "ENTER TRANSFER LOOP\n");
#endif

                while (old_q_transfer_buffer_raw_n != q_transfer_buffer_raw_n) {
                        unprocessed_n = q_buffer_raw_n;
                        old_q_transfer_buffer_raw_n = q_transfer_buffer_raw_n;

#ifdef DEBUG_IO
                        fprintf(DEBUG_IO_HANDLE, "2 old_q_transfer_buffer_raw_n %d q_transfer_buffer_raw_n %d unprocessed_n %d\n", old_q_transfer_buffer_raw_n, q_transfer_buffer_raw_n, unprocessed_n);
#endif


                        if (    (q_program_state == Q_STATE_UPLOAD) ||
                                (q_program_state == Q_STATE_UPLOAD_BATCH) ||
                                (q_program_state == Q_STATE_DOWNLOAD)) {

                                /* File transfer protocol data handler */
                                protocol_process_data(q_buffer_raw,
                                        q_buffer_raw_n, &unprocessed_n,
                                        q_transfer_buffer_raw, &q_transfer_buffer_raw_n,
                                        sizeof(q_transfer_buffer_raw));
                        } else if (q_program_state == Q_STATE_SCRIPT_EXECUTE) {
                                /* Script data handler */
                                if (q_running_script.running == Q_TRUE) {
                                        script_process_data(q_buffer_raw,
                                                q_buffer_raw_n, &unprocessed_n,
                                                q_transfer_buffer_raw, &q_transfer_buffer_raw_n,
                                                sizeof(q_transfer_buffer_raw));
                                        /*
                                         * Reset the flags so the second call
                                         * is a timeout type.
                                         */
                                        q_running_script.stdout_readable = Q_FALSE;
                                        q_running_script.stdin_writeable = Q_FALSE;
                                }
                        } else if (q_program_state == Q_STATE_HOST) {
                                /* Host mode data handler */
                                host_process_data(q_buffer_raw,
                                        q_buffer_raw_n, &unprocessed_n,
                                        q_transfer_buffer_raw, &q_transfer_buffer_raw_n,
                                        sizeof(q_transfer_buffer_raw));
                        }

#ifdef DEBUG_IO
                        fprintf(DEBUG_IO_HANDLE, "3 old_q_transfer_buffer_raw_n %d q_transfer_buffer_raw_n %d unprocessed_n %d\n", old_q_transfer_buffer_raw_n, q_transfer_buffer_raw_n, unprocessed_n);
#endif

                        /* Hang onto whatever was unprocessed */
                        if (unprocessed_n > 0) {
                                memmove(q_buffer_raw, q_buffer_raw + q_buffer_raw_n - unprocessed_n, unprocessed_n);
                        }
                        q_buffer_raw_n = unprocessed_n;

#ifdef DEBUG_IO
                        fprintf(DEBUG_IO_HANDLE, "4 old_q_transfer_buffer_raw_n %d q_transfer_buffer_raw_n %d unprocessed_n %d\n", old_q_transfer_buffer_raw_n, q_transfer_buffer_raw_n, unprocessed_n);
#endif

                }

#ifdef DEBUG_IO
                fprintf(DEBUG_IO_HANDLE, "EXIT TRANSFER LOOP\n");
#endif

        }

        /* Terminal mode */
        if (q_program_state == Q_STATE_CONSOLE) {
#ifdef DEBUG_IO
                fprintf(DEBUG_IO_HANDLE, "console_process_incoming_data: > q_buffer_raw_n %d unprocessed_n %d\n",
                        q_buffer_raw_n, unprocessed_n);
#endif

                /*
                 * Usability issue: if we're in the middle of a very
                 * fast but botched download, the keyboard will become
                 * hard to use if we keep reading stuff and processing
                 * through the console.  So flag a potential console
                 * flood.
                 */
                if (q_program_state == Q_STATE_CONSOLE) {
                        if (q_transfer_buffer_raw_n > 512) {
                                q_console_flood = Q_TRUE;
                        } else {
                                q_console_flood = Q_FALSE;
                        }
                }

                /* Let the console process the data */
                console_process_incoming_data(q_buffer_raw, q_buffer_raw_n, &unprocessed_n);

#ifdef DEBUG_IO
                fprintf(DEBUG_IO_HANDLE, "console_process_incoming_data: < q_buffer_raw_n %d unprocessed_n %d\n",
                        q_buffer_raw_n, unprocessed_n);
#endif
        }

        assert(q_transfer_buffer_raw_n >= 0);
        assert(unprocessed_n >= 0);

        /* Hang onto whatever was unprocessed */
        if (unprocessed_n > 0) {
                memmove(q_buffer_raw, q_buffer_raw + q_buffer_raw_n - unprocessed_n, unprocessed_n);
        }
        q_buffer_raw_n = unprocessed_n;

#ifdef DEBUG_IO
        fprintf(DEBUG_IO_HANDLE, "serial_open = %s online = %s q_transfer_buffer_raw_n = %d\n",
#ifdef Q_NO_SERIAL
                "N/A",
#else
                (q_status.serial_open == Q_TRUE ? "true" : "false"),
#endif
                (q_status.online == Q_TRUE ? "true" : "false"),
                q_transfer_buffer_raw_n);
#endif

        /* Write the data in the output buffer to q_child_tty_fd */
        if (    (Q_SERIAL_OPEN || (q_status.online == Q_TRUE)) &&
                (q_transfer_buffer_raw_n > 0)
        ) {

#ifdef LINE_NOISE
                for (i = 0; i < q_transfer_buffer_raw_n; i++) {
                        int do_noise = random() % line_noise_per_bytes;
                        if ((do_noise == 1) && (noise_stop == Q_FALSE)) {
                                q_transfer_buffer_raw[i] = random() % 0xFF;
                                noise_stop = Q_TRUE;
                                break;
                        }
                }
#endif

                /* Write the data to q_child_tty_fd */
                rc = qodem_write(q_child_tty_fd,
                        (char *)q_transfer_buffer_raw,
                        q_transfer_buffer_raw_n, Q_TRUE);
                if (rc < 0) {

                        switch (get_errno()) {

                        case EAGAIN:
#ifdef Q_PDCURSES_WIN32
                        case WSAEWOULDBLOCK:
#endif /* Q_PDCURSES_WIN32 */
                                /*
                                 * Outgoing buffer is full, wait for
                                 * the next round
                                 */
                                break;
                        default:
                                /* Uh-oh, error */
                                snprintf(notify_message, sizeof(notify_message), _("Call to write() failed: %s"), strerror(errno));
                                notify_form(notify_message, 0);
                                return;
                        }
                } else {
#ifdef DEBUG_IO
                        fprintf(DEBUG_IO_HANDLE, "%d bytes written\n", rc);
#endif
                        /* Hang onto the difference for the next round */
                        assert(rc <= q_transfer_buffer_raw_n);

                        if (rc < q_transfer_buffer_raw_n) {
                                memmove(q_transfer_buffer_raw,
                                        q_transfer_buffer_raw + rc,
                                        q_transfer_buffer_raw_n - rc);
                        }
                        q_transfer_buffer_raw_n -= rc;
                }
        }

} /* ---------------------------------------------------------------------- */

/*
 * data_handler - block on data from either the control TTY or the data TTY
 */
static void data_handler() {
        int rc;                         /* select() */
        int select_fd_max;              /* Maximum fd descriptor # */
        struct timeval listen_timeout;  /* select() timeout */
        time_t current_time;
        int default_timeout;            /* Microseconds to wait for data */
        char notify_message[DIALOG_MESSAGE_SIZE];
        Q_BOOL have_data = Q_FALSE;
#ifndef Q_NO_SERIAL
        char time_string[SHORT_TIME_SIZE];
        int hours, minutes, seconds;
        double connect_time;
#endif /* Q_NO_SERIAL */

        /* Flush curses */
        screen_flush();

#ifdef Q_PDCURSES_WIN32
        /*
         * Win32 doesn't support select() on stdin or on sub-process pipe
         * handles.  So we call different functions to check for data
         * depending on what we're connected to.  We still use the select()
         * as a general idle call.
         */
        Q_BOOL check_net_data = Q_FALSE;

        if (Q_SERIAL_OPEN) {
                /*
                 * TODO: check for data on serial port.  If we have some,
                 * set have_data to true so we can skip the select()
                 * call.
                 */

        }

        if (    (net_is_connected() == Q_FALSE) &&
                (net_connect_pending() == Q_FALSE) &&
                (net_is_listening() == Q_FALSE)
        ) {
                if (is_readable(q_child_tty_fd) == Q_TRUE) {
                        have_data = Q_TRUE;
                }
        } else {
                check_net_data = Q_TRUE;
        }


        if (q_program_state == Q_STATE_SCRIPT_EXECUTE) {
                /* Check for readability on stdout */
                DWORD actual_bytes = 0;
                if (PeekNamedPipe(q_script_stdout, NULL, 0, NULL,
                                &actual_bytes, NULL) == 0) {
                        /*
                         * Error peeking.  Go after data anyway to catch a
                         * process termination.
                         */
                        q_running_script.stdout_readable = Q_TRUE;
                } else {
                        if (actual_bytes > 0) {
                                /* Data is available for reading */
                                q_running_script.stdout_readable = Q_TRUE;
                        } else {
                                /* Do not access stdout */
                                q_running_script.stdout_readable = Q_FALSE;
                        }
                }
                /*
                 * We always assume the script is writeable and look for
                 * EAGAIN when we write 0.
                 */
                q_running_script.stdin_writeable = Q_TRUE;
        }
#else
        /*
         * For network connections through the dialer, we might have
         * read data before we got to STATE_CONSOLE.  So look at
         * q_buffer_raw_n and set have_data appropriately.
         */
        if (q_buffer_raw_n > 0) {
                have_data = Q_TRUE;
        }

#endif /* Q_PDCURSES_WIN32 */

#if 0

        /* Verify that q_child_tty_fd is still valid */
        struct stat fstats;
        if (q_child_tty_fd != -1) {
                if (fstat(q_child_tty_fd, &fstats) < 0) {
                        /* "Oh shit" */
                        assert (1 == 0);
                }
        }

#endif

        /*
         * Default is to block 20 milliseconds (50Hz).
         */
        default_timeout = 20000;

        /* Initialize select() structures */
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        FD_ZERO(&exceptfds);

#if !defined(Q_PDCURSES) && !defined(Q_PDCURSES_WIN32)
        /* Add stdin */
        select_fd_max = STDIN_FILENO;
        FD_SET(STDIN_FILENO, &readfds);
#else
        /* PDCurses case: don't select on stdin */
        select_fd_max = 0;
#endif

        /* Add the child tty */
        if (q_child_tty_fd != -1) {
                /*
                 * Do not read data while in some program states.
                 */
                switch (q_program_state) {
                case Q_STATE_DIALER:
                        if (net_connect_pending() == Q_TRUE) {
#ifdef DEBUG_IO
                                fprintf(DEBUG_IO_HANDLE, "CHECK NET connect()\n");
                                fflush(DEBUG_IO_HANDLE);
#endif
                                FD_SET(q_child_tty_fd, &writefds);
                        }
                        /* Fall through... */
                case Q_STATE_HOST:
                case Q_STATE_UPLOAD:
                case Q_STATE_UPLOAD_BATCH:
                case Q_STATE_DOWNLOAD:
                case Q_STATE_SCRIPT_EXECUTE:
                case Q_STATE_CONSOLE:

#ifdef Q_PDCURSES_WIN32
                        if (check_net_data == Q_TRUE) {
#endif /* Q_PDCURSES_WIN32 */

#ifdef DEBUG_IO
                        fprintf(DEBUG_IO_HANDLE, "select on q_child_tty_fd = %d\n", q_child_tty_fd);
                        fflush(DEBUG_IO_HANDLE);
#endif
                        /* These states are OK to read() */
                        FD_SET(q_child_tty_fd, &readfds);

                        if ((q_status.dial_method == Q_DIAL_METHOD_RLOGIN) && (net_is_connected() == Q_TRUE)) {
                                FD_SET(q_child_tty_fd, &exceptfds);
                        }

                        /* Flag if we need to send data out to the child tty */
                        if (q_transfer_buffer_raw_n > 0) {
                                FD_SET(q_child_tty_fd, &writefds);
                        }
                        if (q_child_tty_fd > select_fd_max) {
                                select_fd_max = q_child_tty_fd;
                        }
#ifdef Q_PDCURSES_WIN32
                        }
#endif /* Q_PDCURSES_WIN32 */
                        break;
                case Q_STATE_DOWNLOAD_MENU:
                case Q_STATE_UPLOAD_MENU:
                case Q_STATE_DOWNLOAD_PATHDIALOG:
                case Q_STATE_UPLOAD_PATHDIALOG:
                case Q_STATE_EMULATION_MENU:
                case Q_STATE_TRANSLATE_MENU:
                case Q_STATE_INITIALIZATION:
                case Q_STATE_UPLOAD_BATCH_DIALOG:
                case Q_STATE_CODEPAGE:
                case Q_STATE_SCROLLBACK:
                case Q_STATE_CONSOLE_MENU:
                case Q_STATE_INFO:
                case Q_STATE_FUNCTION_KEY_EDITOR:
#ifndef Q_NO_SERIAL
                case Q_STATE_MODEM_CONFIG:
#endif /* Q_NO_SERIAL */
                case Q_STATE_PHONEBOOK:
                case Q_STATE_TRANSLATE_EDITOR:
                case Q_STATE_EXIT:
                case Q_STATE_SCREENSAVER:
                        /* For these states, do NOT read() */
#ifdef Q_PDCURSES_WIN32
                        check_net_data = Q_FALSE;
#endif /* Q_PDCURSES_WIN32 */
                        break;
                }
        }

        if (q_program_state == Q_STATE_SCRIPT_EXECUTE) {
#ifndef Q_PDCURSES_WIN32
                /* Add the script tty fd */
                if ((q_running_script.script_tty_fd != -1) && (q_running_script.paused == Q_FALSE)) {
                        FD_SET(q_running_script.script_tty_fd, &readfds);

                        /* Only check for writeability if we have something to send */
                        if (q_running_script.print_buffer_empty == Q_FALSE) {
                                FD_SET(q_running_script.script_tty_fd, &writefds);
                        }

                        if (q_running_script.script_tty_fd > select_fd_max) {
                                select_fd_max = q_running_script.script_tty_fd;
                        }
                }
#endif /* Q_PDCURSES_WIN32 */
        }

        /* select() needs 1 + MAX */
        select_fd_max++;

#ifdef DEBUG_IO
        fprintf(DEBUG_IO_HANDLE, "call select(): select_fd_max = %d\n",
                select_fd_max
        );
        fflush(DEBUG_IO_HANDLE);
#endif

        /* Set the timeout */
        if (default_timeout >= 0) {
                /* Non-blocking select() */
                listen_timeout.tv_sec = default_timeout / 1000000;
                listen_timeout.tv_usec = default_timeout % 1000000;
#ifdef Q_PDCURSES_WIN32
                if ((have_data == Q_FALSE) && (check_net_data == Q_TRUE)) {
                        rc = select(select_fd_max, &readfds, &writefds, &exceptfds, &listen_timeout);
                } else {
                        /* Go straight to timeout case */
                        if (have_data == Q_FALSE) {
                                DWORD millis = listen_timeout.tv_sec * 1000 + listen_timeout.tv_usec / 1000;
                                Sleep(millis);
                        }
                        rc = 0;
                }
#else
                rc = select(select_fd_max, &readfds, &writefds, &exceptfds, &listen_timeout);
#endif /* Q_PDCURSES_WIN32 */
        } else {
                /* Blocking select() */
#ifdef Q_PDCURSES_WIN32
                if ((have_data == Q_FALSE) && (check_net_data == Q_TRUE)) {
                        rc = select(select_fd_max, &readfds, &writefds, &exceptfds, NULL);
                } else {
                        /* Go straight to timeout case */
                        if (have_data == Q_FALSE) {
                                DWORD millis = listen_timeout.tv_sec * 1000 + listen_timeout.tv_usec / 1000;
                                Sleep(millis);
                        }
                        rc = 0;
                }
#else
                rc = select(select_fd_max, &readfds, &writefds, &exceptfds, NULL);
#endif /* Q_PDCURSES_WIN32 */
        }

        /* fprintf(DEBUG_IO_HANDLE, "q_program_state = %d select() returned %d\n", q_program_state, rc); */
        switch (rc) {

        case -1:
                /* ERROR */
                switch (errno) {
                case EINTR:
                        /* Interrupted system call, say from a SIGWINCH */
                        break;
                default:
                        snprintf(notify_message, sizeof(notify_message), _("Call to select() failed: %s"), strerror(errno));
                        notify_form(notify_message, 0);
                        exit(EXIT_ERROR_SELECT_FAILED);
                }
                break;
        case 0:
                /* Timeout, do nothing */
                /* Flush capture file if necessary */
                if (q_status.capture == Q_TRUE) {
                        if (q_status.capture_flush_time < time(NULL)) {
                                fflush(q_status.capture_file);
                                q_status.capture_flush_time = time(NULL);
                        }
                }

#ifndef Q_NO_SERIAL
                /*
                 * Check for DCD drop, but NOT if the host is running in serial
                 * mode.
                 */
                if ((q_status.online == Q_TRUE) &&
                        Q_SERIAL_OPEN &&
                        !((q_program_state == Q_STATE_HOST) &&
                                (q_host_type == Q_HOST_TYPE_SERIAL))
                ) {
                        query_serial_port();
                        if (q_serial_port.rs232.DCD == Q_FALSE) {
                                qlog(_("OFFLINE: modem DCD line went down, lost carrier\n"));

                                time(&current_time);
                                connect_time = difftime(current_time, q_status.connect_time);
                                hours = connect_time / 3600;
                                minutes = ((int)connect_time % 3600) / 60;
                                seconds = (int)connect_time % 60;
                                snprintf(time_string, sizeof(time_string), "%02u:%02u:%02u", hours, minutes, seconds);

                                /* Kill quicklearn script */
                                stop_quicklearn();

                                /* Kill running script */
                                script_stop();

                                qlog(_("CONNECTION CLOSED. Total time online: %s\n"), time_string);

                                /* Modem/serial */
                                close_serial_port();
                        }
                }
#endif /* Q_NO_SERIAL */

                if ((q_child_tty_fd != -1) && (q_status.idle_timeout > 0)) {
                        /* See if idle timeout was specified */
                        time(&current_time);
                        if (    (difftime(current_time, data_time) > q_status.idle_timeout) &&
                                (difftime(current_time, q_data_sent_time) > q_status.idle_timeout)
                        ) {
                                qlog(_("Connection IDLE timeout exceeded, closing...\n"));

                                /* Kill quicklearn script */
                                stop_quicklearn();

                                /* Kill running script */
                                script_stop();

                                if (Q_SERIAL_OPEN) {
#ifndef Q_NO_SERIAL
                                        /* Modem/serial */
                                        close_serial_port();
#endif /* Q_NO_SERIAL */
                                } else {
                                        /*
                                         * All other cases.  We send the kill now,
                                         * and the rest will be handled in
                                         * process_incoming_data() .
                                         */
                                        close_connection();
                                }
                        }
                }

                if (    (q_child_tty_fd != -1) &&
                        (q_keepalive_timeout > 0) &&
                        (q_program_state != Q_STATE_DIALER)
                ) {
                        /* See if keepalive timeout was specified */
                        time(&current_time);
                        if ((difftime(current_time, data_time) > q_keepalive_timeout) && (difftime(current_time, q_data_sent_time) > q_keepalive_timeout)) {
                                /* Send keepalive bytes */
                                if (q_keepalive_bytes_n > 0) {
                                        qodem_write(q_child_tty_fd, q_keepalive_bytes, q_keepalive_bytes_n, Q_TRUE);
                                }
                        }
                }

                /*
                 * File transfer protocols, scripts, and host mode need to
                 * run all the time.
                 */
                if ((q_program_state == Q_STATE_DOWNLOAD) ||
                        (q_program_state == Q_STATE_UPLOAD) ||
                        (q_program_state == Q_STATE_UPLOAD_BATCH) ||
                        (q_program_state == Q_STATE_DIALER) ||
                        (q_program_state == Q_STATE_SCRIPT_EXECUTE) ||
                        (q_program_state == Q_STATE_HOST)
#ifdef Q_LIBSSH2
                        || (    (q_status.dial_method == Q_DIAL_METHOD_SSH) &&
                                (net_is_connected() == Q_TRUE) &&
                                (is_readable(q_child_tty_fd)))
#endif /* Q_LIBSSH2 */
                        || (have_data == Q_TRUE)
                ) {

#ifndef Q_PDCURSES_WIN32
                        /*
                         * For scripts: this is timeout, so don't try to move
                         * data to the pty/pipe.
                         */
                        if (q_program_state == Q_STATE_SCRIPT_EXECUTE) {
                                q_running_script.stdout_readable = Q_FALSE;
                                q_running_script.stdin_writeable = Q_FALSE;

                        }
#endif /* Q_PDCURSES_WIN32 */

                        /* Process incoming data */
                        process_incoming_data();
                }
                break;
        default:

#ifdef DEBUG_IO
                fprintf(DEBUG_IO_HANDLE, "q_child_tty %s %s %s\n",
                        (FD_ISSET(q_child_tty_fd, &readfds) ? "READ" : ""),
                        (FD_ISSET(q_child_tty_fd, &writefds) ? "WRITE" : ""),
                        (FD_ISSET(q_child_tty_fd, &exceptfds) ? "EXCEPT" : ""));
#endif

                /*
                 * For scripts: see if stdout/stderr are readable and set
                 * flag as needed.
                 */
                if (q_program_state == Q_STATE_SCRIPT_EXECUTE) {
#ifndef Q_PDCURSES_WIN32
                        if (q_running_script.script_tty_fd != -1) {
                                if (FD_ISSET(q_running_script.script_tty_fd, &readfds)) {
                                        q_running_script.stdout_readable = Q_TRUE;
                                } else {
                                        q_running_script.stdout_readable = Q_FALSE;
                                }
                                if (FD_ISSET(q_running_script.script_tty_fd, &writefds)) {
                                        q_running_script.stdin_writeable = Q_TRUE;
                                } else {
                                        q_running_script.stdin_writeable = Q_FALSE;
                                }
                        }
#endif /* Q_PDCURSES_WIN32 */
                }
                if (    (net_connect_pending() == Q_TRUE) &&
                        (       (FD_ISSET(q_child_tty_fd, &readfds)) ||
                                (FD_ISSET(q_child_tty_fd, &writefds)))
                ) {
#ifdef DEBUG_IO
                        fprintf(DEBUG_IO_HANDLE, "net_connect_finish()\n");
#endif
                        /* Our connect() call has completed, go deal with it */
                        net_connect_finish();
                }

                /* Data is present somewhere, go process it */
                if (    ((q_child_tty_fd > 0) && (is_readable(q_child_tty_fd))) ||
                        ((q_child_tty_fd > 0) && (FD_ISSET(q_child_tty_fd, &writefds))) ||
                        (q_program_state == Q_STATE_SCRIPT_EXECUTE) ||
                        (q_program_state == Q_STATE_HOST)
                ) {
                        /* Process incoming data */
                        process_incoming_data();
                }

                /* Note that keyboard_handler() will be called from
                 main() anyway, so just break here. */

                break;
        }

} /* ---------------------------------------------------------------------- */

/*
 * open_workingdir_file - open a file in the working dir.  *new_filename
 *                        will point to a new string if filename pointed
 *                        to a relative path, else *new_filename will
 *                        equal filename.
 */
FILE * open_workingdir_file(const char * filename, char ** new_filename) {
        *new_filename = NULL;

        if (strlen(filename) == 0) {
                return NULL;
        }

        if (filename[0] != '/') {
                /* Relative path, prefix working directory */
                *new_filename = (char *)Xmalloc(strlen(filename) +
                        strlen(get_option(Q_OPTION_WORKING_DIR)) + 2, __FILE__, __LINE__);
                memset(*new_filename, 0, strlen(filename) + strlen(get_option(Q_OPTION_WORKING_DIR)) + 2);

                strncpy(*new_filename, get_option(Q_OPTION_WORKING_DIR), strlen(get_option(Q_OPTION_WORKING_DIR)));
                (*new_filename)[strlen(*new_filename)] = '/';
                strncpy(*new_filename + strlen(*new_filename), filename, strlen(filename));
        } else {
                /* Duplicate the passed-in filename */
                *new_filename = Xstrdup(filename, __FILE__, __LINE__);
        }

        return fopen(*new_filename, "a");
} /* ---------------------------------------------------------------------- */

static char datadir_filename[FILENAME_SIZE];

/*
 * get_datadir_filename - open a file in the data dir.  *new_filename
 *                        will point to a new string if filename pointed
 *                        to a relative path, else *new_filename will
 *                        equal filename.
 */
char * get_datadir_filename(const char * filename) {
        assert(q_home_directory != NULL);

        sprintf(datadir_filename, "%s/%s", q_home_directory, filename);

        return datadir_filename;
} /* ---------------------------------------------------------------------- */

/*
 * get_workingdir_filename - open a file in the working directory.  *new_filename
 *                        will point to a new string if filename pointed
 *                        to a relative path, else *new_filename will
 *                        equal filename.
 */
char * get_workingdir_filename(const char * filename) {
        sprintf(datadir_filename, "%s/%s",
                get_option(Q_OPTION_WORKING_DIR), filename);

        return datadir_filename;
} /* ---------------------------------------------------------------------- */

/*
 * get_scriptdir_filename - open a file in the data dir.  *new_filename
 *                        will point to a new string if filename pointed
 *                        to a relative path, else *new_filename will
 *                        equal filename.
 */
char * get_scriptdir_filename(const char * filename) {
        assert(q_home_directory != NULL);

#ifdef Q_PDCURSES_WIN32
        sprintf(datadir_filename, "%s\\scripts\\%s",
                get_option(Q_OPTION_WORKING_DIR), filename);
#else
        sprintf(datadir_filename, "%s/scripts/%s", q_home_directory, filename);
#endif /* Q_PDCURSES_WIN32 */

        return datadir_filename;
} /* ---------------------------------------------------------------------- */

/*
 * open_datadir_file - open a file in the data dir.  *new_filename
 *                     will point to a new string if filename pointed
 *                     to a relative path, else *new_filename will
 *                     equal filename.
 */
FILE * open_datadir_file(const char * filename, char ** new_filename, const char * mode) {
        assert(q_home_directory != NULL);

        *new_filename = NULL;

        if (strlen(filename) == 0) {
                return NULL;
        }

        if (filename[0] != '/') {
                /* Relative path, prefix data directory */
                *new_filename = (char *)Xmalloc(strlen(filename) +
                        strlen(q_home_directory) + 2, __FILE__, __LINE__);
                memset(*new_filename, 0, strlen(filename) + strlen(q_home_directory) + 2);

                strncpy(*new_filename, q_home_directory, strlen(q_home_directory));
                (*new_filename)[strlen(*new_filename)] = '/';
                strncpy(*new_filename + strlen(*new_filename), filename, strlen(filename));
        } else {
                /* Duplicate the passed-in filename */
                *new_filename = Xstrdup(filename, __FILE__, __LINE__);
        }

        return fopen(*new_filename, mode);
} /* ---------------------------------------------------------------------- */

/*
 * spawn_terminal - spawn a new terminal.  Different code based on whether
 *                  or not this is the X11 build.
 */
void spawn_terminal(const char * command) {
#ifdef Q_PDCURSES
        int i;
        char * substituted_string;
        substituted_string = substitute_string(get_option(Q_OPTION_X11_TERMINAL), "$COMMAND", command);

        /* Clear with background */
        for (i = 0; i < HEIGHT; i++) {
                screen_put_color_hline_yx(i, 0, ' ', WIDTH, Q_COLOR_CONSOLE);
        }
#ifdef Q_PDCURSES_WIN32
        char * wait_msg = _("Waiting On Command Shell To Exit...");
#else
        char * wait_msg = _("Waiting On X11 Terminal To Exit...");
#endif
        screen_put_color_str_yx(HEIGHT / 2, (WIDTH - strlen(wait_msg)) / 2, wait_msg, Q_COLOR_CONSOLE);
        screen_flush();

        system(substituted_string);
        Xfree(substituted_string, __FILE__, __LINE__);
        screen_clear();
        q_screen_dirty = Q_TRUE;
#else
        reset_shell_mode();
        system(command);
        reset_prog_mode();
        screen_really_clear();
        q_screen_dirty = Q_TRUE;
#endif
} /* ---------------------------------------------------------------------- */

/*
 * main
 */
int qodem_main(int argc, char * const argv[]) {
        int option_index = 0;           /* getopt_long() */
        int rc;                         /* getopt_long() */
        char * env_string;
        char * substituted_filename;
#ifndef Q_PDCURSES_WIN32
        wchar_t value_wchar[128];
        char * username;
#endif /* Q_PDCURSES_WIN32 */

#ifdef DEBUG_IO
        if (DEBUG_IO_HANDLE == NULL) {
                DEBUG_IO_HANDLE = fopen("debug_qodem.txt", "w");
        }
        fprintf(DEBUG_IO_HANDLE, "QODEM: START\n");
        fflush(DEBUG_IO_HANDLE);
#endif /* DEBUG_XMODEM */

        /* Internationalization */
        if (setlocale(LC_ALL, "") == NULL) {
                fprintf(stderr, "setlocale returned NULL: %s\n",
                        strerror(errno));
                exit(EXIT_ERROR_SETLOCALE);
        }

/*
 * Bug #3528357 - The "proper" solution is to add LIBINTL to LDFLAGS
 * and have configure build the intl directory.  But until we get
 * actual non-English translations it doesn't matter.  We may as well
 * just disable gettext().
 */
#if defined(ENABLE_NLS) && defined(HAVE_GETTEXT)
#ifdef Q_PDCURSES
        bindtextdomain("qodem-x11", LOCALEDIR);
        textdomain("qodem-x11");
#else
        bindtextdomain(PACKAGE, LOCALEDIR);
        textdomain(PACKAGE);
#endif /* Q_PDCURSES */

        /*
        fprintf(stderr, "LANG: %s\n", getenv("LANG"));
        fprintf(stderr, "%s\n", bindtextdomain(PACKAGE, LOCALEDIR));
        fprintf(stderr, "%s\n", textdomain(PACKAGE));
         */
#endif /* ENABLE_NLS && HAVE_GETTEXT */

        /* This is an UGLY HACK but it's somewhat important */
        rc = check_for_help(argc, argv);
        if (rc != 0) {
                exit(rc);
        }

        /* Initial call state */
        initial_call.method                     = Q_DIAL_METHOD_SSH;
        initial_call.address                    = NULL;
        initial_call.port                       = "22";

#ifdef Q_PDCURSES_WIN32
        /* Get username in Windows */
        TCHAR windows_user_name[65];
        DWORD windows_user_name_n;
        memset(windows_user_name, 0, sizeof(windows_user_name));
        windows_user_name_n = 64;

        if (!GetUserName(windows_user_name, &windows_user_name_n)) {
                /* Error getting username, just make it blank */
                initial_call.username = Xwcsdup(L"", __FILE__, __LINE__);
        } else {
                if (sizeof(TCHAR) == sizeof(char)) {
                        /* Convert from char to wchar_t */
                        initial_call.username = Xstring_to_wcsdup((char *)windows_user_name, __FILE__, __LINE__);
                } else {
                        /* TCHAR is wchar_t */
                        initial_call.username = Xwcsdup((wchar_t *)windows_user_name, __FILE__, __LINE__);
                }
        }
#else
        username = getpwuid(geteuid())->pw_name;
        mbstowcs(value_wchar, username, strlen(username) + 1);
        initial_call.username = Xwcsdup(value_wchar, __FILE__, __LINE__);
#endif /* Q_PDCURSES_WIN32 */
        initial_call.password                   = L"";
        initial_call.emulation                  = Q_EMUL_LINUX;
        initial_call.codepage                   = default_codepage(initial_call.emulation);
        initial_call.notes                      = NULL;
        initial_call.script_filename            = "";
        initial_call.keybindings_filename       = "";
        initial_call.capture_filename           = "";
        initial_call.doorway                    = Q_DOORWAY_CONFIG;
        initial_call.use_default_toggles        = Q_TRUE;
        dial_phonebook_entry_n                  = -1;

        /* Initial program state */
        q_program_state = Q_STATE_INITIALIZATION;
        /* Default to VT102 as the most common denominator */
        q_status.emulation              = Q_EMUL_VT102;
        q_status.codepage               = default_codepage(q_status.emulation);
        q_status.doorway_mode           = Q_DOORWAY_MODE_OFF;
        q_status.zmodem_autostart       = Q_TRUE;
        q_status.zmodem_escape_ctrl     = Q_FALSE;

        q_status.kermit_autostart               = Q_TRUE;
        q_status.kermit_robust_filename         = Q_FALSE;
        q_status.kermit_streaming               = Q_TRUE;
        q_status.kermit_long_packets            = Q_TRUE;
        q_status.kermit_uploads_force_binary    = Q_TRUE;
        q_status.kermit_downloads_convert_text  = Q_TRUE;

        q_status.external_telnet        = Q_FALSE;
        q_status.external_rlogin        = Q_TRUE;
        q_status.external_ssh           = Q_TRUE;
        q_status.xterm_double           = Q_TRUE;
        q_status.vt100_color            = Q_TRUE;
        q_status.vt52_color             = Q_TRUE;

        /*
         * Due to Avatar's ANSI fallback, in practice this flag does nothing
         * now.  Before ANSI fallback, it used to really control whether or
         * not Avatar would handle SGR.
         */
        q_status.avatar_color           = Q_TRUE;

#ifndef Q_NO_SERIAL
        q_status.serial_open            = Q_FALSE;
#endif /* Q_NO_SERIAL */
        q_status.online                 = Q_FALSE;
        q_status.split_screen           = Q_FALSE;
        q_status.sound                  = Q_FALSE;
        q_status.beeps                  = Q_FALSE;
        q_status.ansi_music             = Q_FALSE;
        q_status.strip_8th_bit          = Q_FALSE;
        q_status.full_duplex            = Q_TRUE;
        q_status.line_feed_on_cr        = Q_FALSE;
        q_status.guard_hangup           = Q_TRUE;
        q_status.capture                = Q_FALSE;
        q_status.capture_file           = NULL;
        q_status.capture_type           = Q_CAPTURE_TYPE_NORMAL;
        q_status.screen_dump_type       = Q_CAPTURE_TYPE_NORMAL;
        q_status.scrollback_save_type   = Q_CAPTURE_TYPE_NORMAL;
        q_status.capture_x              = 0;
        q_status.logging                = Q_FALSE;
        q_status.logging_file           = NULL;
        q_status.scrollback_enabled     = Q_TRUE;
        q_status.scrollback_lines       = 0;
        q_status.status_visible         = Q_TRUE;
        q_status.status_line_info       = Q_FALSE;
        q_status.hard_backspace         = Q_TRUE;
        /* Every console assumes line wrap, so turn it on by default */
        q_status.line_wrap              = Q_TRUE;
        /* BBS-like emulations usually assume 80 columns, so turn it on by default */
        q_status.assume_80_columns      = Q_TRUE;
        q_status.ansi_animate           = Q_FALSE;
        q_status.display_null           = Q_FALSE;
        q_status.reverse_video          = Q_FALSE;
        q_status.origin_mode            = Q_FALSE;
        q_status.insert_mode            = Q_FALSE;
        q_status.hold_screen_mode       = Q_FALSE;
        q_status.led_1                  = Q_FALSE;
        q_status.led_2                  = Q_FALSE;
        q_status.led_3                  = Q_FALSE;
        q_status.led_4                  = Q_FALSE;
        q_status.current_username       = NULL;
        q_status.current_password       = NULL;
        q_status.remote_address         = NULL;
        q_status.remote_port            = NULL;
        q_status.remote_phonebook_name  = NULL;
#ifndef Q_NO_SERIAL
        q_status.dial_method            = Q_DIAL_METHOD_MODEM;
#endif /* Q_NO_SERIAL */
        q_status.idle_timeout           = 0;
        q_status.quicklearn             = Q_FALSE;
        q_screensaver_timeout           = 0;
        q_keepalive_timeout             = 0;
        q_current_dial_entry            = NULL;
        q_status.exit_on_disconnect     = Q_FALSE;

        /* Initialize the music "engine" :-) */
        music_init();

        /*
         * Set q_home_directory.  load_options() will create the default key binding
         * files and needs to use open_datadir_file().
         */

        /* Sustitute for $HOME */
        env_string = get_home_directory();

        /* Store for global use */
#ifdef Q_PDCURSES_WIN32
        q_home_directory = substitute_string("$HOME\\qodem\\prefs", "$HOME", env_string);
#else
        q_home_directory = substitute_string("$HOME/.qodem", "$HOME", env_string);
#endif /* Q_PDCURSES_WIN32 */

        /* Load the options */
        load_options();

#ifndef QODEM_USE_SDL
#ifndef __linux
        /* Sound is not supported except on Linux console */
        q_status.sound = Q_FALSE;
#endif
#endif

        /* Setup MIXED mode doorway */
        setup_doorway_handling();

        /* Process options */
        for (;;) {
                rc = getopt_long(argc, argv, "h?", q_getopt_long_options, &option_index);
                if (rc == -1) {
                        /* Fall out of the for loop, we're done calling getopt_long */
                        break;
                }

                /* See which new option was specified */
                switch (rc) {
                case 0:
                        process_command_line_option(q_getopt_long_options[option_index].name, optarg);
                        break;
                case '?':
                case 'h':
                        printf("%s", usage_string());
                        q_program_state = Q_STATE_EXIT;
                        break;
                default:
                        break;
                }
        } /* for (;;) */

#if defined(Q_PDCURSES) || defined(Q_PDCURSES_WIN32)
        /* Initialize curses */
        screen_setup();

#else
        /*
         * Initialize the keyboard here.  It will newterm() each supported
         * emulation, but restore things before it leaves.
         */
        initialize_keyboard();

#endif

        /*
         * Load the translation table
         */
        load_translate_tables();

#ifndef Q_NO_SERIAL
        /*
         * Load the modem configuration
         */
        load_modem_config();
#endif /* Q_NO_SERIAL */

        /*
         * Setup the help system
         */
        setup_help();

        /* Initialize curses to setup stdscr */

        /*
         * We reduce ESCDELAY on the assumption that local console
         * is VERY fast.  However, if the user has already set ESCDELAY,
         * we don't want to change it.
         */
        if (getenv("ESCDELAY") == NULL) {
                putenv("ESCDELAY=20");
        }

#if !defined(Q_PDCURSES) && !defined(Q_PDCURSES_WIN32)
        /* Xterm: send the private sequence to select metaSendsEscape */
        fprintf(stdout, "\033[?1036h");
        fflush(stdout);

        /* Initialize curses */
        screen_setup();
#else
        /*
         * Initialize the keyboard here.  It will newterm() each supported
         * emulation, but restore things before it leaves.
         */
        initialize_keyboard();
#endif

        if (strcasecmp(get_option(Q_OPTION_CAPTURE), "true") == 0) {
                start_capture(get_option(Q_OPTION_CAPTURE_FILE));
        }

        if (strcasecmp(get_option(Q_OPTION_LOG), "true") == 0) {
                start_logging(get_option(Q_OPTION_LOG_FILE));
        }

        /* Default scrolling region needs HEIGHT which is set by curses. */
        q_status.scroll_region_top      = 0;
        q_status.scroll_region_bottom   = HEIGHT - STATUS_HEIGHT - 1;

#ifndef Q_PDCURSES_WIN32
        /* Ignore SIGPIPE */
        signal(SIGPIPE, SIG_IGN);
#endif /* Q_PDCURSES_WIN32 */

        /*
         * If anything else remains, turn it into a command line.
         */
        if (optind < argc) {

                if ((strlen(argv[optind]) == 2) && (strcmp(argv[optind], "--") == 0)) {
                        /*
                         * Special case:  strip out the "--" argument used to pass
                         * the remaining arguments to the connect program.
                         */
                        optind++;
                }

                if (initial_call.address != NULL) {
                        /* Error: --connect was specified along with a command line */
                        screen_put_color_str_yx(0, 0,
                                _("Error: The --connect argument cannot be used when a command"),  Q_COLOR_CONSOLE_TEXT);
                        screen_put_color_str_yx(1, 0,
                                _("line is also specified."),  Q_COLOR_CONSOLE_TEXT);
                        /*
                         * Some X11 emulators (like GNOME-Terminal) will drop directly
                         * down to a command-line without leaving the usage info up.
                         * Force a keystroke so the user can actually see it.
                         */
                        screen_put_color_str_yx(3, 0, _("Press any key to continue...\n"), Q_COLOR_CONSOLE_TEXT);
                        screen_flush();
                        discarding_getch();

                        q_exitrc = EXIT_ERROR_COMMANDLINE;
                        q_program_state = Q_STATE_EXIT;
                        /*
                         * I'll let it finish constructing initial_call,
                         * but it'll never be used.
                         */
                }
                /* Set the dial method */
                initial_call.method = Q_DIAL_METHOD_COMMANDLINE;

                /* Build the command line */
                initial_call.address = (char *)Xmalloc(sizeof(char) * 1, __FILE__, __LINE__);
                initial_call.address[0] = '\0';
                for (;optind < argc; optind++) {
                        /* Expand the buffer to include the next argument + one space +
                         null terminator. */
                        initial_call.address = (char *)Xrealloc(initial_call.address,
                                sizeof(char) * (strlen(initial_call.address) + strlen(argv[optind]) + 2),
                                __FILE__, __LINE__);
                        /* Zero out the new space */
                        memset(initial_call.address + strlen(initial_call.address), 0, strlen(argv[optind]) + 1);
                        /* Add the space before the next argument */
                        initial_call.address[strlen(initial_call.address)] = ' ';
                        /* Copy the next argument + the null terminator over */
                        memcpy(initial_call.address + strlen(initial_call.address), argv[optind], strlen(argv[optind]) + 1);
                }
                initial_call.name = Xstring_to_wcsdup(initial_call.address, __FILE__, __LINE__);
        } /* if (optind < argc) */

        /* See if we need to --play something */
        if (play_music_string != NULL) {
                play_ansi_music(play_music_string, strlen((char *)play_music_string), Q_TRUE);
                Xfree(play_music_string, __FILE__, __LINE__);
                play_music_string = NULL;
                if (play_music_exit == Q_TRUE) {
                        q_program_state = Q_STATE_EXIT;
                }
        }

        if (q_program_state != Q_STATE_EXIT) {

#if 0

                /* This will show the curses keystroke for an input key */
                for (;;) {
                        rc = getch();
                        if (rc != -1) {
                                printw("Key: %04o   %c\n", rc, (rc & 0xFF));
                        }
                        if (rc == 'q') {
                                break;
                        }
                }

#else

                /*
                 * Load the phonebook
                 */

                /* Sustitute for $HOME */
                env_string = get_home_directory();

                /* Check for the default phonebook */
#ifdef Q_PDCURSES_WIN32
                substituted_filename = substitute_string("$HOME\\qodem\\prefs\\" DEFAULT_PHONEBOOK, "$HOME", env_string);
                if (file_exists(substituted_filename) == Q_FALSE) {
#else
                substituted_filename = substitute_string("$HOME/.qodem/" DEFAULT_PHONEBOOK, "$HOME", env_string);
                if (access(substituted_filename, F_OK) != 0) {
#endif /* Q_PDCURSES_WIN32 */
                        FILE * file;
                        if ((file = fopen(substituted_filename, "w")) == NULL) {
                                screen_put_color_printf_yx(0, 0, Q_COLOR_CONSOLE_TEXT, _("Error creating file \"%s\": %s\n"),
                                        substituted_filename, strerror(errno));
                                screen_put_color_printf_yx(3, 0, Q_COLOR_CONSOLE_TEXT, _("Press any key to continue...\n"));
                                screen_flush();
                                discarding_getch();
                        } else {
                                fclose(file);
                                /* Create the default phonebook */
                                q_phonebook.filename = substituted_filename;
                                create_phonebook();
                        }
                }
                q_phonebook.filename = substituted_filename;

                /* Now load it */
                load_phonebook(Q_FALSE);

                /*
                 * Explicitly call console_refresh() so that the scrollback
                 * will be set up.
                 */
                console_refresh(Q_FALSE);

                /* Reset all emulations */
                reset_emulation();

                if (dial_phonebook_entry_n != -1) {
                        q_current_dial_entry = q_phonebook.entries;
                        while ((dial_phonebook_entry_n > 1) && (q_current_dial_entry != NULL)) {
                                q_current_dial_entry = q_current_dial_entry->next;
                                dial_phonebook_entry_n--;
                        }
                        if (q_current_dial_entry != NULL) {
                                q_phonebook.selected_entry = q_current_dial_entry;
                                phonebook_normalize();
                                do_dialer();
                        }
                } else if (initial_call.address != NULL) {
                        q_keyboard_blocks = Q_TRUE;
                        q_current_dial_entry = &initial_call;
                        do_dialer();
                } else if (strncmp(get_option(Q_OPTION_START_PHONEBOOK), "true", 4) == 0) {
                        switch_state(Q_STATE_PHONEBOOK);
                } else {
                        switch_state(Q_STATE_CONSOLE);
                }

#ifdef Q_LIBSSH2
                if (libssh2_init(0) < 0) {
                        screen_put_color_printf_yx(0, 0, Q_COLOR_CONSOLE_TEXT, _("Error initializing libssh2\n"));
                        screen_put_color_printf_yx(3, 0, Q_COLOR_CONSOLE_TEXT, _("Press any key to continue...\n"));
                        screen_flush();
                        discarding_getch();
                }
#endif /* Q_LIBSSH2 */

                /* Enter main loop */
                for (;;) {
                        /* Window size checks, refresh, etc. */
                        refresh_handler();

                        /* Grab data */
                        data_handler();

                        keyboard_handler();
                        if (q_program_state == Q_STATE_EXIT) {
                                break;
                        }
                }

#endif

        } /* if (q_program_state != Q_STATE_EXIT) */

        /* Close any open files */
        stop_capture();
        stop_quicklearn();
        script_stop();
#ifndef Q_NO_SERIAL
        if (Q_SERIAL_OPEN) {
                close_serial_port();
        }
#endif /* Q_NO_SERIAL */
        /* Log our exit */
        qlog(_("Qodem exiting...\n"));
        stop_logging();

        /* Clear the screen */
        screen_clear();

        /* Shutdown curses */
        screen_teardown();

        /* Shutdown the music "engine" :-) */
        music_teardown();

#ifdef Q_LIBSSH2
        libssh2_exit();
#endif /* Q_LIBSSH2 */

#ifdef DEBUG_IO
        fclose(DEBUG_IO_HANDLE);
#endif

#ifdef Q_PDCURSES_WIN32
        /* Shutdown winsock */
        stop_winsock();
#endif /*Q_PDCURSES_WIN32 */

        /* Exit */
        exit(q_exitrc);
} /* ---------------------------------------------------------------------- */

/*
 * main
 */
int main(int argc, char * const argv[]) {
        return qodem_main(argc, argv);
} /* ---------------------------------------------------------------------- */

#ifdef Q_PDCURSES_WIN32

/*
 * Windows main
 */
int APIENTRY WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance,
        LPSTR lpszCmdLine, int nCmdShow) {

        char *argv[30];
        int i;
        int argc = 1;

        argv[0] = "qodem";
        for (i = 0; lpszCmdLine[i]; i++) {
                if ((lpszCmdLine[i] != ' ') &&
                        (!i || lpszCmdLine[i - 1] == ' ')
                ) {
                        argv[argc++] = lpszCmdLine + i;
                }
        }

        for (i = 0; lpszCmdLine[i]; i++) {
                if (lpszCmdLine[i] == ' ') {
                        lpszCmdLine[i] = '\0';
                }
        }
        return qodem_main( argc, (char **)argv);
}

#endif