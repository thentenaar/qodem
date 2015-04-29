/*
 * dialer.c
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

#ifdef Q_PDCURSES_WIN32
#include <windows.h>
#ifndef __BORLANDC__
#include <winsock2.h>
#endif
#else
/* Use forkpty() */
#ifdef __APPLE__
#include <util.h>
#else
#ifdef __FreeBSD__
#include <sys/types.h>
#include <libutil.h>
#else
#include <pty.h>
#endif
#endif /* __APPLE__ */

#include <termios.h>
#include <unistd.h>

#endif /* Q_PDCURSES_WIN32 */

#ifdef __linux
#include <sys/ioctl.h>
#endif /* __linux */

#include <string.h>                     /* strdup() */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <assert.h>
#include <fcntl.h>
#include "qodem.h"
#include "music.h"
#include "states.h"
#include "status.h"
#include "screen.h"
#include "options.h"
#include "script.h"
#include "netclient.h"
#include "dialer.h"

/* When we started dialing */
time_t q_dialer_start_time;

/* How much time is left (in seconds) on the cycle clock */
time_t q_dialer_cycle_time;

/* When the cycle clock started */
time_t q_dialer_cycle_start_time;

/* How many calls have been attempted */
int q_dialer_attempts;

/* The status line to report on the redialer screen */
char q_dialer_status_message[DIALOG_MESSAGE_SIZE];

/* The modem line to report on the redialer screen */
char q_dialer_modem_message[DIALOG_MESSAGE_SIZE];

/* Our current dialing state */
Q_DIAL_STATE q_dial_state;

/*
 * tokenize_command - Separate command into arguments.
 */
char ** tokenize_command(const char * argv) {
        int i, j;
        int argv_start;
        char ** new_argv;
        char * old_argv;
        int n;

        old_argv = Xstrdup(argv, __FILE__, __LINE__);
        n = strlen(argv);

        /* Trim beginning whitespace */
        argv_start = 0;
        while (isspace(argv[argv_start])) {
                argv_start++;
        }

        for (i = 0, j = argv_start; j < n; i++) {
                while ((j < n) && !isspace(argv[j])) {
                        j++;
                }
                while ((j < n) && isspace(argv[j])) {
                        j++;
                }
        }

        /* i is the # of tokens */
        new_argv = (char **)Xmalloc(sizeof(char *) * (i+1), __FILE__, __LINE__);
        for (i = 0, j = argv_start; j < n; i++) {
                new_argv[i] = old_argv + j;
                while ((j < n) && !isspace(old_argv[j])) {
                        j++;
                }
                while ((j < n) && isspace(old_argv[j])) {
                        old_argv[j] = 0;
                        j++;
                }
        }

        /*
         * Why can't I free this?  Because new_argv is pointers
         * into old_argv.  Freeing it kills the child process.
         */
        /* Xfree(old_argv, __FILE__, __LINE__); */

        new_argv[i] = NULL;
        return new_argv;
} /* ---------------------------------------------------------------------- */

/*
 * connect_command - construct the command line
 */
static char * connect_command(const struct q_phone_struct * target) {
        char connect_string[COMMAND_LINE_SIZE];
        char * substituted_string;

        switch (target->method) {

        case Q_DIAL_METHOD_SHELL:
                snprintf(connect_string, sizeof(connect_string), "%s", get_option(Q_OPTION_SHELL));
                break;
        case Q_DIAL_METHOD_SSH:
                if ((q_status.current_username != NULL) && (wcslen(q_status.current_username) > 0)) {
                        snprintf(connect_string, sizeof(connect_string), "%s", get_option(Q_OPTION_SSH_USER));
                } else {
                        snprintf(connect_string, sizeof(connect_string), "%s", get_option(Q_OPTION_SSH));
                }
                break;
        case Q_DIAL_METHOD_RLOGIN:
                if ((q_status.current_username != NULL) && (wcslen(q_status.current_username) > 0)) {
                        snprintf(connect_string, sizeof(connect_string), "%s", get_option(Q_OPTION_RLOGIN_USER));
                } else {
                        snprintf(connect_string, sizeof(connect_string), "%s", get_option(Q_OPTION_RLOGIN));
                }
                break;
        case Q_DIAL_METHOD_TELNET:
                snprintf(connect_string, sizeof(connect_string), "%s", get_option(Q_OPTION_TELNET));
                break;
        /*
        case Q_DIAL_METHOD_TN3270:
                snprintf(connect_string, sizeof(connect_string), "%s", get_option(Q_OPTION_TN3270));
                break;
         */
        case Q_DIAL_METHOD_COMMANDLINE:
                snprintf(connect_string, sizeof(connect_string), "%s", target->address);
                break;

#ifndef Q_NO_SERIAL
        case Q_DIAL_METHOD_MODEM:
#endif /* Q_NO_SERIAL */
        case Q_DIAL_METHOD_SOCKET:
                /* NOP */
                return NULL;
        }

        /* $USERNAME */
        if (q_status.current_username != NULL) {
                substituted_string = substitute_wcs_half(connect_string, "$USERNAME", q_status.current_username);
                strncpy(connect_string, substituted_string, sizeof(connect_string));
                Xfree(substituted_string, __FILE__, __LINE__);
        }

        /* $REMOTEHOST */
        if (q_status.remote_address != NULL) {
                substituted_string = substitute_string(connect_string, "$REMOTEHOST", q_status.remote_address);
                strncpy(connect_string, substituted_string, sizeof(connect_string));
                Xfree(substituted_string, __FILE__, __LINE__);
        }

        /* $REMOTEPORT */
        if (q_status.remote_port != NULL) {
                substituted_string = substitute_string(connect_string, "$REMOTEPORT", q_status.remote_port);
                strncpy(connect_string, substituted_string, sizeof(connect_string));
                Xfree(substituted_string, __FILE__, __LINE__);
        }

        return Xstrdup(connect_string, __FILE__, __LINE__);
} /* ---------------------------------------------------------------------- */

#if 0

#include <stdio.h>

#define termios_check_flag(A, B, C)             \
        if (term->A & B) { \
                fprintf(file, "%s ", C); \
        }

/*
 * Debugging the termios structure
 */
static void show_termios_info(const struct termios * term) {
        FILE * file = fopen("debug_termios.txt", "w");

        fprintf(file, "c_iflag: ");
        termios_check_flag(c_iflag, IGNBRK, "IGNBRK");
        termios_check_flag(c_iflag, IGNPAR, "IGNPAR");
        termios_check_flag(c_iflag, BRKINT, "BRKINT");
        termios_check_flag(c_iflag, PARMRK, "PARMRK");
        termios_check_flag(c_iflag, INPCK, "INPCK");
        termios_check_flag(c_iflag, ISTRIP, "ISTRIP");
        termios_check_flag(c_iflag, INLCR, "INLCR");
        termios_check_flag(c_iflag, IGNCR, "IGNCR");
        termios_check_flag(c_iflag, ICRNL, "ICRNL");
        termios_check_flag(c_iflag, IUCLC, "IUCLC");
        termios_check_flag(c_iflag, IXON, "IXON");
        termios_check_flag(c_iflag, INPCK, "IXANY");
        termios_check_flag(c_iflag, IXOFF, "IXOFF");
        termios_check_flag(c_iflag, IMAXBEL, "IMAXBEL");
        fprintf(file, "\n");

        fprintf(file, "c_lflag: ");
        termios_check_flag(c_lflag, ISIG, "ISIG");
        termios_check_flag(c_lflag, ICANON, "ICANON");

        fprintf(file, "\n");
        fclose(file);
} /* ---------------------------------------------------------------------- */

#endif

/*
 * Returns the appropriate TERM string for this connection
 */
const char * dialer_get_term() {
        /* Set the TERM variable */
        switch (q_status.emulation) {
        case Q_EMUL_ANSI:
                return "ansi";
        case Q_EMUL_AVATAR:
                return "avatar";
        case Q_EMUL_VT52:
                return "vt52";
        case Q_EMUL_VT100:
                return "vt100";
        case Q_EMUL_VT102:
                return "vt102";
        case Q_EMUL_VT220:
                return "vt220";
        case Q_EMUL_TTY:
                return "dumb";
        case Q_EMUL_LINUX:
        case Q_EMUL_LINUX_UTF8:
                return "linux";
        case Q_EMUL_XTERM:
        case Q_EMUL_XTERM_UTF8:
                return "xterm";
        case Q_EMUL_DEBUG:
        default:
                /* No default terminal setting */
                return "";
        }

} /* ---------------------------------------------------------------------- */

/*
 * Returns the appropriate LANG string for this connection
 */
const char * dialer_get_lang() {
        switch (q_status.emulation) {
        case Q_EMUL_XTERM_UTF8:
        case Q_EMUL_LINUX_UTF8:
                return get_option(Q_OPTION_UTF8_LANG);
        default:
                return get_option(Q_OPTION_ISO8859_LANG);
        }
} /* ---------------------------------------------------------------------- */

#ifdef Q_PDCURSES_WIN32
/*
 * Win32 case: we have to create pipes that are connected to the child
 * process' streams.  These are initialized in spawn_process().
 */
HANDLE q_child_stdin = NULL;
HANDLE q_child_stdout = NULL;
HANDLE q_child_process = NULL;
HANDLE q_child_thread = NULL;
#endif /* Q_PDCURSES_WIN32 */

/*
 * Spawn a sub-process.  command_line is explicit free()'d in this function.
 *
 * NOTE: ANY CHANGES TO BEHAVIOR HERE MUST BE CHECKED IN
 * script_start() ALSO!!!
 */
void spawn_process(char * command_line, Q_EMULATION emulation) {

#ifdef Q_PDCURSES_WIN32
        /*
         * Modeled after example on MSDN:
         * http://msdn.microsoft.com/en-us/library/ms682499%28v=VS.85%29.aspx
         */
        PROCESS_INFORMATION process_info;
        STARTUPINFOA startup_info;
        HANDLE q_child_stdin_2 = NULL;
        HANDLE q_child_stdout_2 = NULL;
        SECURITY_ATTRIBUTES security_attr;
        char buffer[32];
        int columns;

        security_attr.nLength = sizeof(SECURITY_ATTRIBUTES);
        security_attr.bInheritHandle = TRUE;
        security_attr.lpSecurityDescriptor = NULL;

        /* Create pipes as needed to communicate with child process */
        if (!CreatePipe(&q_child_stdout, &q_child_stdout_2,
                        &security_attr, 0)) {
#if 0
                fprintf(stderr, "CreatePipe() 1 failed: %d %sn",
                        GetLastError(), strerror(GetLastError()));
                fflush(stderr);
#endif
                /* Error, bail out */
                return;
        }
        if (!SetHandleInformation(q_child_stdout, HANDLE_FLAG_INHERIT, 0)) {
                /* Error, bail out */
                CloseHandle(q_child_stdout);
                CloseHandle(q_child_stdout_2);
                q_child_stdout = NULL;
                q_child_stdout_2 = NULL;
#if 0
                fprintf(stderr, "SetHandleInformation() 1 failed: %d %sn",
                        GetLastError(), strerror(GetLastError()));
                fflush(stderr);
#endif
                return;
        }
        if (!CreatePipe(&q_child_stdin_2, &q_child_stdin,
                        &security_attr, 0)) {
#if 0
                fprintf(stderr, "CreatePipe() 2 failed: %d %sn",
                        GetLastError(), strerror(GetLastError()));
                fflush(stderr);
#endif
                /* Error, bail out */
                return;
        }
        if (!SetHandleInformation(q_child_stdin, HANDLE_FLAG_INHERIT, 0)) {
                /* Error, bail out */
                CloseHandle(q_child_stdout);
                CloseHandle(q_child_stdout_2);
                q_child_stdout = NULL;
                q_child_stdout_2 = NULL;
                CloseHandle(q_child_stdin);
                CloseHandle(q_child_stdin_2);
                q_child_stdin = NULL;
                q_child_stdin_2 = NULL;
#if 0
                fprintf(stderr, "SetHandleInformation() 2 failed: %d %sn",
                        GetLastError(), strerror(GetLastError()));
                fflush(stderr);
#endif
                return;
        }

        /* Set my TERM variable */
        if (strlen(dialer_get_term()) > 0) {
                SetEnvironmentVariableA("TERM", dialer_get_term());
        }

        /* Set LINES and COLUMNS */
        snprintf(buffer, sizeof(buffer), "%u", HEIGHT - STATUS_HEIGHT);
        SetEnvironmentVariableA("LINES", buffer);

        switch (emulation) {
        case Q_EMUL_ANSI:
        case Q_EMUL_AVATAR:
        case Q_EMUL_TTY:
                /* BBS-ish emulations:  check the assume_80_columns flag */
                if (q_status.assume_80_columns == Q_TRUE) {
                        columns = 80;
                } else {
                        columns = WIDTH;
                }
                break;
        default:
                columns = WIDTH;
                break;
        }

        snprintf(buffer, sizeof(buffer), "%u", columns);
        SetEnvironmentVariableA("COLUMNS", buffer);

        /* Set the LANG */
        SetEnvironmentVariableA("LANG", dialer_get_lang());

        /* Create child process itself */
        memset(&process_info, 0, sizeof(PROCESS_INFORMATION));
        memset(&startup_info, 0, sizeof(STARTUPINFO));
        startup_info.cb = sizeof(STARTUPINFO);
        startup_info.hStdInput = q_child_stdin_2;
        startup_info.hStdOutput = q_child_stdout_2;
        startup_info.hStdError = q_child_stdout_2;
        startup_info.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        startup_info.wShowWindow = SW_HIDE;
        if (!CreateProcessA(NULL,               /* Use command line */

                           command_line,        /* Command line */

                           NULL,                /*
                                                 * No inherited security
                                                 * attributes for process
                                                 */

                           NULL,                /*
                                                 * No inherited security
                                                 * attributes for thread
                                                 */

                           TRUE,                /* Inherit handles */

                           0,                   /* No special creation flags */

                           NULL,                /* Inherit environment block */

                           NULL,                /* Inherit starting directory */

                           &startup_info,       /* STARTUPINFO to fill */

                           &process_info        /* PROCESS_INFORMATION to fill */
                    )) {
                /* Error, bail out */
#if 0
                fprintf(stderr, "CreateProcess() failed: %d %sn",
                        GetLastError(), strerror(GetLastError()));
                fflush(stderr);
#endif
                CloseHandle(process_info.hProcess);
                CloseHandle(process_info.hThread);
                CloseHandle(q_child_stdout);
                CloseHandle(q_child_stdout_2);
                q_child_stdout = NULL;
                q_child_stdout_2 = NULL;
                CloseHandle(q_child_stdin);
                CloseHandle(q_child_stdin_2);
                q_child_stdin = NULL;
                q_child_stdin_2 = NULL;
                return;
        }
        q_child_process = process_info.hProcess;
        q_child_thread = process_info.hThread;

        /* The child has these, not us, so close them */
        CloseHandle(q_child_stdin_2);
        CloseHandle(q_child_stdout_2);

        /*
         * At this point, we should have a running child process that writes
         * to the other ends of q_child_stdout and q_child_stderr, and reads
         * from the other end of q_child_stdin.  We have to use ReadFile()
         * and WriteFile() on our end of these handles.
         */
#if 0
        fprintf(stderr, "spawn_process() OK: PID %d TID %d\n",
                process_info.dwProcessId, process_info.dwThreadId);
        fflush(stderr);
#endif
        return;

#else
        /* POSIX case: fork and execute */

        /* Fork and put the child on a new tty */
        char ** target_argv;
        char ttyname_buffer[FILENAME_SIZE];
        pid_t child_pid = forkpty(&q_child_tty_fd, ttyname_buffer, NULL, NULL);
        q_child_ttyname = Xstrdup(ttyname_buffer, __FILE__, __LINE__);

        if (child_pid == 0) {
                /*
                 * Child process, will become the connection program
                 */

                /*
                 * NOTE: ANY CHANGES TO BEHAVIOR HERE MUST BE CHECKED IN
                 * script_start() ALSO!!!
                 */

                struct q_scrolline_struct * line;
                struct q_scrolline_struct * line_next;
                /* This just has to be long enough for LINES=blah and COLUMNS=blah */
                char buffer[32];
                int columns;

                /* Restore signal handlers */
                signal(SIGPIPE, SIG_DFL);

                /* Free scrollback memory */
                line = q_scrollback_buffer;
                while (line != NULL) {
                        line_next = line->next;
                        Xfree(line, __FILE__, __LINE__);
                        line = line_next;
                }

                /* Set my TERM variable */
                if (strlen(dialer_get_term()) == 0) {
                        unsetenv("TERM");
                } else {
                        setenv("TERM", dialer_get_term(), 1);
                }

                /* Set LINES and COLUMNS */
                memset(buffer, 0, sizeof(buffer));
                snprintf(buffer, sizeof(buffer), "LINES=%u", HEIGHT - STATUS_HEIGHT);
                putenv(strdup(buffer));
                memset(buffer, 0, sizeof(buffer));

                switch (emulation) {
                case Q_EMUL_ANSI:
                case Q_EMUL_AVATAR:
                case Q_EMUL_TTY:
                        /* BBS-ish emulations:  check the assume_80_columns flag */
                        if (q_status.assume_80_columns == Q_TRUE) {
                                columns = 80;
                        } else {
                                columns = WIDTH;
                        }
                        break;
                default:
                        columns = WIDTH;
                        break;
                }

                snprintf(buffer, sizeof(buffer), "COLUMNS=%u", columns);
                putenv(strdup(buffer));

#ifdef __linux

                /*
                 * Some programs won't forward the environment
                 * variables anyway, so let's tell the kernel to reset
                 * cols and rows and maybe that will make it across.
                 *
                 * We use perror() here because it will make its way
                 * back to the parent (I hope).  We don't have control
                 * of the terminal anymore.
                 */
                struct winsize console_size;
                if (ioctl(STDIN_FILENO, TIOCGWINSZ, &console_size) < 0) {
                        perror("ioctl(TIOCGWINSZ)");
                } else {
                        console_size.ws_row = HEIGHT - STATUS_HEIGHT;
                        console_size.ws_col = columns;
                        if (ioctl(STDIN_FILENO, TIOCSWINSZ, &console_size) < 0) {
                                perror("ioctl(TIOCSWINSZ)");
                        }
                }

#endif /* __linux */

                /* Set the LANG */
                snprintf(buffer, sizeof(buffer), "LANG=%s", dialer_get_lang());
                putenv(strdup(buffer));

                /* Separate target into arguments */
                target_argv = tokenize_command(command_line);
                Xfree(command_line, __FILE__, __LINE__);
                execvp(target_argv[0], target_argv);
                /* Error: couldn't run the other side so crap out */
                perror("execvp()");
                exit(-1);
        } else {

                /* Free leak */
                Xfree(command_line, __FILE__, __LINE__);

        } /* if (child_pid == 0) */

        if (q_child_tty_fd != -1) {
                q_child_pid = child_pid;
        }

#endif /* Q_PDCURSES_WIN32 */

} /* ---------------------------------------------------------------------- */

#ifdef Q_PDCURSES_WIN32

/*
 * Make the socket non-blocking
 */
Q_BOOL set_nonblock(const int fd) {
        u_long non_block_mode = 1;

        if (    (net_is_connected() == Q_FALSE) &&
                (net_connect_pending == Q_FALSE) &&
                (net_is_listening == Q_FALSE)
        ) {
                /* Assume success for a not-socket case. */
                return Q_TRUE;
        }

        if (ioctlsocket(fd, FIONBIO, &non_block_mode) == 0) {
                return Q_TRUE;
        }
        return Q_FALSE;
} /* ---------------------------------------------------------------------- */

#else

/*
 * Make the file/socket non-blocking
 */
Q_BOOL set_nonblock(const int fd) {
        int flags;

        flags = fcntl(fd, F_GETFL);
        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
                /* Error */
                return Q_FALSE;
        }

        /* All OK */
        return Q_TRUE;
} /* ---------------------------------------------------------------------- */

/*
 * Perform a cfmakeraw() on the TTY
 */
Q_BOOL set_raw_termios(const int tty_fd) {
        struct termios old_termios;
        struct termios new_termios;

        if (tcgetattr(tty_fd, &old_termios) < 0) {
                /* Error */
                return Q_FALSE;
        }
        memcpy(&new_termios, &old_termios, sizeof(struct termios));
        cfmakeraw(&new_termios);
        if (tcsetattr(tty_fd, TCSANOW, &new_termios) < 0) {
                /* Error */
                return Q_FALSE;
        }

        /* All OK */
        return Q_TRUE;
} /* ---------------------------------------------------------------------- */

#endif /* Q_PDCURSES_WIN32 */

static void setup_dial_screen() {
        time(&q_dialer_cycle_start_time);
        q_dialer_cycle_time = atoi(get_option(Q_OPTION_DIAL_CONNECT_TIME));
        q_dialer_attempts++;
        q_dial_state = Q_DIAL_DIALING;

        /* Set the message */
        sprintf(q_dialer_status_message, _("%-3d Seconds remain until Cycle"), (int)q_dialer_cycle_time);

        switch_state(Q_STATE_DIALER);
        refresh_handler();
} /* ---------------------------------------------------------------------- */

/* Fixup stats for a successful connection */
void dial_success() {
        q_dial_state = Q_DIAL_CONNECTED;
        time(&q_dialer_cycle_start_time);
        time(&q_status.connect_time);
        q_status.online = Q_TRUE;

        /* Switch to doorway mode if requested */
        if (q_current_dial_entry->doorway == Q_DOORWAY_CONFIG) {
                if (strcasecmp(get_option(Q_OPTION_CONNECT_DOORWAY), "doorway") == 0) {
                        q_status.doorway_mode = Q_DOORWAY_MODE_FULL;
                }
                if (strcasecmp(get_option(Q_OPTION_CONNECT_DOORWAY), "mixed") == 0) {
                        q_status.doorway_mode = Q_DOORWAY_MODE_MIXED;
                }
        } else if (q_current_dial_entry->doorway == Q_DOORWAY_ALWAYS_DOORWAY) {
                q_status.doorway_mode = Q_DOORWAY_MODE_FULL;
        } else if (q_current_dial_entry->doorway == Q_DOORWAY_ALWAYS_MIXED) {
                q_status.doorway_mode = Q_DOORWAY_MODE_MIXED;
        } else if (q_current_dial_entry->doorway == Q_DOORWAY_NEVER) {
                q_status.doorway_mode = Q_DOORWAY_MODE_OFF;
        }
        /* Set remaining toggles */
        if (q_current_dial_entry->use_default_toggles == Q_FALSE) {
                set_dial_out_toggles(q_current_dial_entry->toggles);
        }

        /*
         * Untag it in the phonebook on the assumption
         * we connected
         */
        if (q_current_dial_entry->tagged == Q_TRUE) {
                q_current_dial_entry->tagged = Q_FALSE;
                q_phonebook.tagged--;
        }

#ifndef Q_PDCURSES_WIN32
        if (q_current_dial_entry->method != Q_DIAL_METHOD_MODEM) {
#endif /* Q_PDCURSES_WIN32 */
                /* Log */
                qlog(_("CONNECTION ESTABLISHED: %ls\n"), q_current_dial_entry->name);

                /* Play connect sequence */
                if (q_status.beeps == Q_TRUE) {
                        play_sequence(Q_MUSIC_CONNECT);
                }

                if (net_connect_pending() == Q_FALSE) {

                        /* Go immediately to console */
                        switch_state(Q_STATE_CONSOLE);

                        /* Execute script if supplied */
                        if (q_current_dial_entry->script_filename != NULL) {
                                if (strlen(q_current_dial_entry->script_filename) > 0) {
                                        if (q_status.quicklearn == Q_FALSE) {
                                                /*
                                                 * We're not quicklearning,
                                                 * start the script
                                                 */
                                                script_start(q_current_dial_entry->script_filename);
                                        }
                                }
                        }
                        screen_flush();
                }
#ifndef Q_PDCURSES_WIN32
        }
#endif /* Q_PDCURSES_WIN32 */
} /* ---------------------------------------------------------------------- */

/*
 * dial_out - dial out to a target system
 *
 * NOTE: ANY CHANGES TO BEHAVIOR HERE MUST BE CHECKED IN
 * script_start() ALSO!!!
 */
int dial_out(struct q_phone_struct * target) {
        char * command = NULL;
        Q_BOOL do_network_connect = Q_FALSE;

        /* Assert that the connection does not already exist. */
        assert(q_child_tty_fd == -1);

        /* Reset clock for keepalive/idle timeouts */
        time(&q_data_sent_time);

        /* Reset the terminal emulation state */
        q_status.emulation = target->emulation;
        q_status.codepage = target->codepage;
        reset_emulation();

#ifndef Q_NO_SERIAL
        if (target->method == Q_DIAL_METHOD_MODEM) {
                /*
                 * Modem dialup:  just open the serial port and switch
                 * to Q_STATE_DIALER.
                 */
                qlog(_("Dialing %ls (%s)...\n"), target->name, target->address);
                if (open_serial_port() == Q_TRUE) {
                        /* Set the modem setting based on the target settings */
                        if (target->use_modem_cfg == Q_TRUE) {
                                q_serial_port.rtscts            = q_modem_config.rtscts;
                                q_serial_port.xonxoff           = q_modem_config.xonxoff;
                                q_serial_port.baud              = q_modem_config.default_baud;
                                q_serial_port.data_bits         = q_modem_config.default_data_bits;
                                q_serial_port.stop_bits         = q_modem_config.default_stop_bits;
                                q_serial_port.parity            = q_modem_config.default_parity;
                                q_serial_port.lock_dte_baud     = q_modem_config.lock_dte_baud;
                        } else {
                                q_serial_port.rtscts            = target->rtscts;
                                q_serial_port.xonxoff           = target->xonxoff;
                                q_serial_port.baud              = target->baud;
                                q_serial_port.data_bits         = target->data_bits;
                                q_serial_port.stop_bits         = target->stop_bits;
                                q_serial_port.parity            = target->parity;
                                q_serial_port.lock_dte_baud     = target->lock_dte_baud;
                        }
                        configure_serial_port();

                        setup_dial_screen();
                }

        }
#endif /* Q_NO_SERIAL */

        if ((target->method == Q_DIAL_METHOD_TELNET) &&
                (q_status.external_telnet == Q_FALSE)) {

                /* Telnet: direct connect */
                do_network_connect = Q_TRUE;
                setup_dial_screen();
                q_child_tty_fd = net_connect_start(target->address,
                        target->port);
        } else if ((target->method == Q_DIAL_METHOD_RLOGIN) &&
                (q_status.external_rlogin == Q_FALSE)) {
                /* Rlogin: direct connect.  Rlogin is always connected to port 513. */
                do_network_connect = Q_TRUE;
                setup_dial_screen();
                q_child_tty_fd = net_connect_start(target->address, "513");
        } else if (target->method == Q_DIAL_METHOD_SOCKET) {
                /* Socket: direct connect */
                do_network_connect = Q_TRUE;
                setup_dial_screen();
                q_child_tty_fd = net_connect_start(target->address,
                        target->port);
#ifdef Q_LIBSSH2
        } else if ((target->method == Q_DIAL_METHOD_SSH) &&
                (q_status.external_ssh == Q_FALSE)) {
                /* SSH: direct connect */
                do_network_connect = Q_TRUE;
                setup_dial_screen();
                q_child_tty_fd = net_connect_start(target->address,
                        target->port);
#endif /* Q_LIBSSH2 */
#ifdef Q_NO_SERIAL
        } else {
#else
        } else if (target->method != Q_DIAL_METHOD_MODEM) {
#endif /* Q_NO_SERIAL */

                /*
                 * Other connection methods:  fork and execute
                 */

                /*
                 * Push all the data out to clear the soon-to-be child process's
                 * output buffer.
                 */
                screen_flush();

                command = connect_command(target);

                /*
                 * Log the command line.  This should be in the child branch
                 * but putting it there corrupts the session log output (two
                 * file handles pointing to the same file).
                 */
                qlog(_("[child] Connecting with command line '%s'...\n"), command);

                spawn_process(command, target->emulation);

        } /* if (target->method != Q_DIAL_METHOD_MODEM) */

        /*
         * Parent qodem process
         */

        /* Reset the bytes connection bytes counter */
        q_connection_bytes_received = 0;

#ifdef Q_PDCURSES_WIN32
        if (    (       (q_child_tty_fd != -1) &&
                        (do_network_connect == Q_FALSE)
                ) || (q_child_stdout != NULL)
        ) {
#else
        if (    (q_child_tty_fd != -1) &&
                (do_network_connect == Q_FALSE)
        ) {
#endif /* Q_PDCURSES_WIN32 */
                /* Connected OK */

                /* Don't block the port */
                set_nonblock(q_child_tty_fd);

                /* Set to raw mode */
                /* set_raw_termios(q_child_tty_fd); */
        }

#ifndef Q_NO_SERIAL
#ifdef Q_PDCURSES_WIN32
        if (    (target->method != Q_DIAL_METHOD_MODEM) &&
                (do_network_connect == Q_FALSE) &&
                (       (q_child_tty_fd != -1) ||
                        (q_child_stdout != NULL))
        ) {
#else
        if (    (target->method != Q_DIAL_METHOD_MODEM) &&
                (do_network_connect == Q_FALSE) &&
                (q_child_tty_fd != -1)
        ) {
#endif /* Q_PDCURSES_WIN32 */
#else
#ifdef Q_PDCURSES_WIN32
        if (    (       (q_child_tty_fd != -1) &&
                        (do_network_connect == Q_FALSE)
                ) || (q_child_stdout != NULL)
        ) {
#else
        if (    (q_child_tty_fd != -1) &&
                (do_network_connect == Q_FALSE)
        ) {
#endif /* Q_PDCURSES_WIN32 */
#endif /* Q_NO_SERIAL */

                /* Immediate connection */
                dial_success();
        }

        return q_child_tty_fd;
} /* ---------------------------------------------------------------------- */
