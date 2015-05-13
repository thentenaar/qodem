/*
 * console.c
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

#include "common.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>
#include "screen.h"
#include "qodem.h"
#include "console.h"
#include "keyboard.h"
#include "protocols.h"
#include "translate.h"
#include "states.h"
#include "options.h"
#include "script.h"
#include "netclient.h"
#include "help.h"

/* Whether we need to render the entire console, or just update the
 * status line. */
Q_BOOL q_screen_dirty = Q_TRUE;

/* Whether we need to render the split-screen status line. */
Q_BOOL q_split_screen_dirty = Q_FALSE;

/* The emulation selected before we entered split-screen mode. */
static Q_EMULATION split_screen_emulation;

/* The split-screen keyboard buffer */
static char split_screen_buffer[254];
static int split_screen_buffer_n;
static int split_screen_x;
static int split_screen_y;

/* MIXED mode doorway keys */
static Q_BOOL doorway_mixed[256];
static Q_BOOL doorway_mixed_pgup;
static Q_BOOL doorway_mixed_pgdn;

/*
 * A flag to indicate a data flood on the console.  We need to not permit
 * download protocol autostarts during a flood.
 */
Q_BOOL q_console_flood = Q_FALSE;

/*
 * Setup MIXED mode doorway
 */
void setup_doorway_handling() {
        char * option;
        char * value;
        int i;
        for (i = 0; i < 256; i++) {
                doorway_mixed[i] = Q_FALSE;
        }
        doorway_mixed_pgup = Q_FALSE;
        doorway_mixed_pgdn = Q_FALSE;

        option = Xstrdup(get_option(Q_OPTION_DOORWAY_MIXED_KEYS),
                __FILE__, __LINE__);
        value = strtok(option, " ");
        while (value != NULL) {
                if (strlen(value) == 1) {
                        doorway_mixed[tolower(value[0])] = Q_TRUE;
                        doorway_mixed[toupper(value[0])] = Q_TRUE;
                }
                if (strcasecmp(value, "pgdn") == 0) {
                        doorway_mixed_pgdn = Q_TRUE;
                }
                if (strcasecmp(value, "pgup") == 0) {
                        doorway_mixed_pgup = Q_TRUE;
                }
                value = strtok(NULL, " ");
        }

        Xfree(option, __FILE__, __LINE__);
} /* ---------------------------------------------------------------------- */

/*
 * start_capture
 */
void start_capture(const char * filename) {
        char * new_filename;
        char time_string[TIME_STRING_LENGTH];
        time_t current_time;
        char notify_message[DIALOG_MESSAGE_SIZE];

        if ((filename != NULL) && (q_status.capture == Q_FALSE)) {
                q_status.capture_file = open_workingdir_file(filename, &new_filename);
                if (q_status.capture_file == NULL) {
                        snprintf(notify_message, sizeof(notify_message),
                                _("Error opening file \"%s\" for writing: %s"),
                                new_filename, strerror(errno));
                        notify_form(notify_message, 0);
                        q_cursor_on();
                } else {
                        qlog(_("Capture open to file '%s'\n"), filename);
                        time(&current_time);

                        if (q_status.capture_type == Q_CAPTURE_TYPE_HTML) {
                                /* HTML */
                                strftime(time_string, sizeof(time_string), _("Capture Generated %a, %d %b %Y %H:%M:%S %z"), localtime(&current_time));
                                fprintf(q_status.capture_file, "<html>\n\n");
                                fprintf(q_status.capture_file, "<!-- * - * Qodem " Q_VERSION " %s BEGIN * - * --> \n\n", time_string);
                                fprintf(q_status.capture_file, "<body bgcolor=\"black\">\n<pre {font-family: 'Courier New', monospace;}><code><font %s>", color_to_html(q_current_color));
                        } else {
                                strftime(time_string, sizeof(time_string), _("Capture Generated %a, %d %b %Y %H:%M:%S %z"), localtime(&current_time));
                                fprintf(q_status.capture_file, "* - * Qodem " Q_VERSION " %s BEGIN * - *\n\n", time_string);
                        }

                        q_status.capture = Q_TRUE;
                }
                if (new_filename != filename) {
                        Xfree(new_filename, __FILE__, __LINE__);
                }
        }
} /* ---------------------------------------------------------------------- */

/*
 * stop_capture
 */
void stop_capture() {
        char time_string[TIME_STRING_LENGTH];
        time_t current_time;

        if (q_status.capture == Q_FALSE) {
                return;
        }

        time(&current_time);

        if (q_status.capture_type == Q_CAPTURE_TYPE_HTML) {
                /* HTML */
                fprintf(q_status.capture_file, "</code></pre></font>\n</body>\n");
                strftime(time_string, sizeof(time_string), _("Capture Generated %a, %d %b %Y %H:%M:%S %z"), localtime(&current_time));
                fprintf(q_status.capture_file, "\n<!-- * - * Qodem " Q_VERSION " %s END * - * -->\n", time_string);
                fprintf(q_status.capture_file, "\n</html>\n");
        } else {
                strftime(time_string, sizeof(time_string), _("Capture Generated %a, %d %b %Y %H:%M:%S %z"), localtime(&current_time));
                fprintf(q_status.capture_file, "\n* - * Qodem " Q_VERSION " %s END * - *\n", time_string);
        }

        fclose(q_status.capture_file);
        q_status.capture = Q_FALSE;
        qlog(_("Capture close\n"));
} /* ---------------------------------------------------------------------- */

/*
 * start_logging
 */
void start_logging(const char * filename) {
        char * new_filename;
        char time_string[TIME_STRING_LENGTH];
        time_t current_time;
        char notify_message[DIALOG_MESSAGE_SIZE];

        if ((filename != NULL) && (q_status.logging == Q_FALSE)) {
                q_status.logging_file = open_workingdir_file(filename, &new_filename);
                if (q_status.logging_file == NULL) {
                        snprintf(notify_message, sizeof(notify_message), _("Error opening file \"%s\" for writing: %s"), new_filename, strerror(errno));
                        notify_form(notify_message, 0);
                        q_cursor_on();
                } else {
                        time(&current_time);
                        strftime(time_string, sizeof(time_string), _("Log Generated %a, %d %b %Y %H:%M:%S %z"), localtime(&current_time));
                        fprintf(q_status.logging_file, "* - * Qodem " Q_VERSION " %s BEGIN * - *\n\n", time_string);
                        q_status.logging = Q_TRUE;
                }
                if (new_filename != filename) {
                        Xfree(new_filename, __FILE__, __LINE__);
                }
        }
} /* ---------------------------------------------------------------------- */

/*
 * stop_logging
 */
void stop_logging() {
        char time_string[TIME_STRING_LENGTH];
        time_t current_time;

        if (q_status.logging == Q_FALSE) {
                return;
        }

        time(&current_time);
        strftime(time_string, sizeof(time_string), _("Log Generated %a, %d %b %Y %H:%M:%S %z"), localtime(&current_time));
        fprintf(q_status.logging_file, "\n* - * Qodem " Q_VERSION " %s END * - *\n", time_string);
        fclose(q_status.logging_file);
        q_status.logging = Q_FALSE;
} /* ---------------------------------------------------------------------- */

/* Zmodem autostart buffer */
static unsigned char zrqinit_buffer[32];
static int zrqinit_buffer_n;

/*
 * reset_zmodem_autostart
 */
static void reset_zmodem_autostart() {
        memset(zrqinit_buffer, 0, sizeof(zrqinit_buffer));
        zrqinit_buffer_n = 0;
} /* ---------------------------------------------------------------------- */

/*
 * check_zmodem_autostart
 */
static Q_BOOL check_zmodem_autostart(unsigned char from_modem) {

        if (q_console_flood == Q_TRUE) {
                /* No autostart during a console flood */
                return Q_FALSE;
        }

        if (q_status.zmodem_autostart == Q_FALSE) {
                return Q_FALSE;
        }

        /*
         * Note:  I may need to expand the number of matching strings
         * for this to work.
         */
        if ((ZRQINIT_STRING[zrqinit_buffer_n] == from_modem) ||
                (ZRQINIT_STRING[zrqinit_buffer_n] == '?')) {

                /* Hang onto it, even though we technically don't need it. */
                zrqinit_buffer[zrqinit_buffer_n] = from_modem;
                zrqinit_buffer_n++;
                if (zrqinit_buffer_n == strlen(ZRQINIT_STRING)) {
                        /* All ZRQINIT characters received, return true. */
                        return Q_TRUE;
                }

                /* More characters can come.  Return false but don't reset.*/
                return Q_FALSE;
        }

        /* It didn't match, so reset */
        reset_zmodem_autostart();
        return Q_FALSE;
} /* ---------------------------------------------------------------------- */

/* Kermit autostart buffer */
static char kermit_autostart_buffer[32];
static int kermit_autostart_buffer_n;

/*
 * reset_kermit_autostart
 */
static void reset_kermit_autostart() {
        memset(kermit_autostart_buffer, 0, sizeof(kermit_autostart_buffer));
        kermit_autostart_buffer_n = 0;
} /* ---------------------------------------------------------------------- */

/*
 * check_kermit_autostart
 */
static Q_BOOL check_kermit_autostart(unsigned char from_modem) {

        if (q_console_flood == Q_TRUE) {
                /* No autostart during a console flood */
                return Q_FALSE;
        }

        if (q_status.kermit_autostart == Q_FALSE) {
                return Q_FALSE;
        }

        /*
         * Note:  I may need to expand the number of matching strings
         * for this to work.
         */
        if ((KERMIT_AUTOSTART_STRING[kermit_autostart_buffer_n] == from_modem) ||
                (KERMIT_AUTOSTART_STRING[kermit_autostart_buffer_n] == '?')) {

                /* Hang onto it, even though we technically don't need it. */
                kermit_autostart_buffer[kermit_autostart_buffer_n] = from_modem;
                kermit_autostart_buffer_n++;
                if (kermit_autostart_buffer_n == strlen(KERMIT_AUTOSTART_STRING)) {
                        /* All KERMIT_AUTOSTART characters received,
                         * return true. */
                        return Q_TRUE;
                }

                /* More characters can come.  Return false but don't reset.*/
                return Q_FALSE;
        }

        /* It didn't match, so reset */
        reset_kermit_autostart();
        return Q_FALSE;
} /* ---------------------------------------------------------------------- */

/* Quicklearn buffer */
static wchar_t quicklearn_buffer[32];
static int quicklearn_buffer_n;
static unsigned char quicklearn_send_buffer[32];
static int quicklearn_send_buffer_n;

FILE * quicklearn_file = NULL;

/*
 * start_quicklearn - begin writing waitfor/send blocks to file
 */
void start_quicklearn(const char * filename) {
        char time_string[TIME_STRING_LENGTH];
        time_t current_time;
        char notify_message[DIALOG_MESSAGE_SIZE];

        assert(quicklearn_file == NULL);

        qlog(_("QuickLearn writing to %s\n"), filename);

        if ((filename != NULL) && (q_status.quicklearn == Q_FALSE)) {
                quicklearn_file = fopen(filename, "w");
                if (quicklearn_file == NULL) {
                        snprintf(notify_message, sizeof(notify_message), _("Error opening file \"%s\" for writing: %s"), filename, strerror(errno));
                        notify_form(notify_message, 0);
                        q_cursor_on();
                } else {
                        time(&current_time);
                        strftime(time_string, sizeof(time_string), _("QuickLearn Script Generated %a, %d %b %Y %H:%M:%S %z"), localtime(&current_time));
                        fprintf(quicklearn_file, "#!/usr/bin/perl -w\n");
                        fprintf(quicklearn_file, "# * - * Qodem " Q_VERSION " %s BEGIN * - *\n\n", time_string);
                        fprintf(quicklearn_file, "use strict;\nuse utf8;\n\n");
                        fprintf(quicklearn_file, "\n");

                        /* Flush stdout/stderr */
                        fprintf(quicklearn_file, "# Flush stdout and stderr by default\n");
                        fprintf(quicklearn_file, "select(STDERR); $| = 1;\n");
                        fprintf(quicklearn_file, "select(STDOUT); $| = 1;\n");
                        fprintf(quicklearn_file, "\n");

                        /* UTF-8 stdin*/
                        fprintf(quicklearn_file, "# Set stdin and stdout to utf8\n");
                        fprintf(quicklearn_file, "binmode STDIN, \":encoding(utf8)\";\n");
                        fprintf(quicklearn_file, "binmode STDOUT, \":encoding(utf8)\";\n");
                        fprintf(quicklearn_file, "\n");

                        /* waitfor */
                        fprintf(quicklearn_file, "# waitfor() - wait for specific string to appear in stdin\n");
                        fprintf(quicklearn_file, "sub waitfor {\n");
                        fprintf(quicklearn_file, "    my @args     = @_;\n");
                        fprintf(quicklearn_file, "    my $string   = $args[0];\n");
                        fprintf(quicklearn_file, "\n");
                        fprintf(quicklearn_file, "    my $chars = \"\";\n");
                        fprintf(quicklearn_file, "    my $rc = 1;\n");
                        fprintf(quicklearn_file, "    while ($rc != 0) {\n");
                        fprintf(quicklearn_file, "        # Read the next character to the end of $chars\n");
                        fprintf(quicklearn_file, "        if (length($chars) > 0) {\n");
                        fprintf(quicklearn_file, "            $rc = read(STDIN, $chars, 1, length($chars));\n");
                        fprintf(quicklearn_file, "        } else {\n");
                        fprintf(quicklearn_file, "            $rc = read(STDIN, $chars, 1, 0);\n");
                        fprintf(quicklearn_file, "        }\n");
                        fprintf(quicklearn_file, "        if (!defined($rc)) {\n");
                        fprintf(quicklearn_file, "            # Error reading\n");
                        fprintf(quicklearn_file, "            print STDERR \"Error waiting for \\\"$string\\\": $^E $!\\n\";\n");
                        fprintf(quicklearn_file, "            die \"Error waiting for \\\"$string\\\": $^E $!\";\n");
                        fprintf(quicklearn_file, "        }\n");
                        fprintf(quicklearn_file, "        if (length($chars) >= length($string)) {\n");
                        fprintf(quicklearn_file, "            $chars = substr($chars, length($chars) - length($string));\n");
                        fprintf(quicklearn_file, "        }\n");
                        fprintf(quicklearn_file, "        if ($string eq $chars) {\n");
                        fprintf(quicklearn_file, "            # Match\n");
                        fprintf(quicklearn_file, "            return;\n");
                        fprintf(quicklearn_file, "        }\n");
                        fprintf(quicklearn_file, "    }\n");
                        fprintf(quicklearn_file, "}\n");
                        fprintf(quicklearn_file, "\n");

                        /* sendkeys */
                        fprintf(quicklearn_file, "# sendkeys() - send specific string to stdout as though typed on the keyboard\n");
                        fprintf(quicklearn_file, "sub sendkeys {\n");
                        fprintf(quicklearn_file, "    my @args     = @_;\n");
                        fprintf(quicklearn_file, "    my $string   = $args[0];\n");
                        fprintf(quicklearn_file, "    print STDOUT $string;\n");
                        fprintf(quicklearn_file, "}\n");
                        fprintf(quicklearn_file, "\n");
                        fprintf(quicklearn_file, "# ---- Main loop below ----\n\n");

                        /* All is OK for QuickLearn */
                        q_status.quicklearn = Q_TRUE;

#ifndef Q_PDCURSES_WIN32
                        /* Make it executable */
                        fchmod(fileno(quicklearn_file), S_IRUSR | S_IWUSR | S_IXUSR);
#endif /* Q_PDCURSES_WIN32 */

                        /* Reset quicklearn buffers */
                        quicklearn_buffer_n = 0;
                        quicklearn_send_buffer_n = 0;

                        /* Turn off other incompatible features */
                        q_status.doorway_mode = Q_DOORWAY_MODE_OFF;
                        q_status.split_screen = Q_FALSE;
                        set_status_line(Q_TRUE);
                } /* if (quicklearn_file == NULL) */
        } /* if ((filename != NULL) && (q_status.quicklearn == Q_FALSE)) */
} /* ---------------------------------------------------------------------- */

/*
 * stop_quicklearn - complete quicklearn and close out file
 */
void stop_quicklearn() {
        char time_string[TIME_STRING_LENGTH];
        time_t current_time;

        if (q_status.quicklearn == Q_FALSE) {
                return;
        }

        assert(quicklearn_file != NULL);

        time(&current_time);
        strftime(time_string, sizeof(time_string), _("QuickLearn Script Generated %a, %d %b %Y %H:%M:%S %z"), localtime(&current_time));
        fprintf(quicklearn_file, "\n# * - * Qodem " Q_VERSION " %s END * - *\n", time_string);
        fclose(quicklearn_file);
        quicklearn_file = NULL;
        q_status.quicklearn = Q_FALSE;

        qlog(_("QuickLearn finished.\n"));
} /* ---------------------------------------------------------------------- */

static void quicklearn_save_sendto() {
        int i;
        assert(quicklearn_file != NULL);

        fprintf(quicklearn_file, "sendkeys(\"");
        for (i = 0; i < quicklearn_send_buffer_n; i++) {
                if (quicklearn_send_buffer[i] == 0x0D) {
                        /* CR */
                        fprintf(quicklearn_file, "\\r");
                } else if (quicklearn_send_buffer[i] == 0x0A) {
                        /* NL - should be rare to send out */
                        fprintf(quicklearn_file, "\\x0A");
                } else if (quicklearn_send_buffer[i] < 0x20) {
                        /* Other control character */
                        fprintf(quicklearn_file, "\\x%02x", quicklearn_send_buffer[i]);
                } else if (quicklearn_send_buffer[i] == '@') {
                        /* Perl - escape out @ */
                        fprintf(quicklearn_file, "\\@");
                } else if (quicklearn_send_buffer[i] == '$') {
                        /* Perl - escape out $ */
                        fprintf(quicklearn_file, "\\$");
                } else {
                        fprintf(quicklearn_file, "%c", quicklearn_send_buffer[i]);
                }
        }
        fprintf(quicklearn_file, "\");\n");
        quicklearn_send_buffer_n = 0;
} /* ---------------------------------------------------------------------- */

static void quicklearn_save_waitfor() {
        int i;
        assert(quicklearn_file != NULL);

        fprintf(quicklearn_file, "waitfor(\"");
        for (i = 0; i < quicklearn_buffer_n; i++) {
                if (quicklearn_buffer[i] == '@') {
                        /* Perl - escape out @ */
                        fprintf(quicklearn_file, "\\@");
                } else if (quicklearn_buffer[i] == '$') {
                        /* Perl - escape out $ */
                        fprintf(quicklearn_file, "\\$");
                } else {
                        fprintf(quicklearn_file, "%lc", (wint_t)quicklearn_buffer[i]);
                }
        }
        fprintf(quicklearn_file, "\");\n");
        quicklearn_buffer_n = 0;
} /* ---------------------------------------------------------------------- */

/*
 * Save one character in the waitfor quicklearn buffer
 */
void quicklearn_print_character(const wchar_t ch) {
        assert(quicklearn_file != NULL);

        if ((ch == '\r') || (ch == '\n')) {
                if (quicklearn_send_buffer_n > 0) {
                        quicklearn_save_sendto();
                }
                /* Clear on newline */
                quicklearn_buffer_n = 0;
                return;
        }
        /* Append */
        if (quicklearn_buffer_n == 32) {
                wmemmove(quicklearn_buffer, quicklearn_buffer + 1,
                        (quicklearn_buffer_n - 1));
                quicklearn_buffer[quicklearn_buffer_n - 1] = ch;
                return;
        } else {
                quicklearn_buffer_n++;
                quicklearn_buffer[quicklearn_buffer_n - 1] = ch;
        }
} /* ---------------------------------------------------------------------- */

/*
 * Save one outgoing byte in the sendto quicklearn buffer
 */
void quicklearn_send_byte(const unsigned char ch) {

        /* Save prompt */
        if ((quicklearn_buffer_n > 0) && (quicklearn_send_buffer_n == 0)) {
                quicklearn_save_waitfor();
        }

        /* Append */
        if (quicklearn_send_buffer_n == 32) {
                memmove(quicklearn_send_buffer, quicklearn_send_buffer + 1,
                        (quicklearn_send_buffer_n - 1));
                quicklearn_send_buffer[quicklearn_send_buffer_n - 1] = ch;
        } else {
                quicklearn_send_buffer_n++;
                quicklearn_send_buffer[quicklearn_send_buffer_n - 1] = ch;
        }

        if ((ch == '\r') || (ch == '\n')) {
                quicklearn_save_sendto();

                /* Clear waitfor on newline output */
                quicklearn_buffer_n = 0;
                return;
        }

} /* ---------------------------------------------------------------------- */

/*
 * console_quicklearn_keyboard_handler
 */
void console_quicklearn_keyboard_handler(int keystroke, int flags) {
        int new_keystroke;

        switch (keystroke) {

        case 'Q':
        case 'q':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-Q QuickLearn */
                        assert(q_status.quicklearn == Q_TRUE);
                        stop_quicklearn();
                        return;
                }
                break;

        default:
                break;

        }

        switch (keystroke) {

        case '\\':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-\ Compose key */
                        if ((q_status.emulation == Q_EMUL_LINUX_UTF8) || (q_status.emulation == Q_EMUL_XTERM_UTF8)) {
                                new_keystroke = compose_key(Q_TRUE);
                        } else {
                                new_keystroke = compose_key(Q_FALSE);
                        }
                        if (new_keystroke != -1) {
                                keystroke = new_keystroke;
                                flags &= ~KEY_FLAG_ALT;
                                if ((q_status.emulation == Q_EMUL_LINUX_UTF8) || (q_status.emulation == Q_EMUL_XTERM_UTF8)) {
                                        flags |= KEY_FLAG_UNICODE;
                                }
                        } else {
                                return;
                        }
                }
                break;

        default:
                break;
        }

        /* Pass keystroke */
        assert(q_status.split_screen == Q_FALSE);
        new_keystroke = keystroke;
        if (((new_keystroke <= 0xFF) && ((flags & KEY_FLAG_UNICODE) == 0)) ||
                ((new_keystroke <= 0x7F) && ((flags & KEY_FLAG_UNICODE) != 0))) {
                /*
                 * Run regular keystrokes through the output
                 * translation table.  Note that Unicode keys
                 * greater than 0x7F will not get translated.
                 */
                new_keystroke = q_translate_table_output.map_to[new_keystroke];
        }

        post_keystroke(new_keystroke, flags);
        return;
} /* ---------------------------------------------------------------------- */

/*
 * set_status_line - called by set_dial_out_toggles and console_keyboard_handler
 */
void set_status_line(Q_BOOL make_visible) {
        if (make_visible == Q_FALSE) {
                /* Increase the scrolling region */
                if (q_status.scroll_region_bottom == HEIGHT - STATUS_HEIGHT - 1) {
                        q_status.scroll_region_bottom = HEIGHT - 1;
                }
                /* Eliminate the status line */
                q_status.status_visible = Q_FALSE;
                STATUS_HEIGHT = 0;
                if (q_status.scrollback_lines >= HEIGHT) {
                        q_status.cursor_y++;
                }
                screen_clear();
                q_screen_dirty = Q_TRUE;
        } else {
                q_status.status_visible = Q_TRUE;
                STATUS_HEIGHT = 1;
                /* Decrease the scrolling region */
                if (q_status.scroll_region_bottom == HEIGHT - 1) {
                        q_status.scroll_region_bottom = HEIGHT - STATUS_HEIGHT - 1;
                }
                if (q_status.cursor_y == HEIGHT - 1) {
                        /* On the last line, pull back before */
                        q_status.cursor_y -= STATUS_HEIGHT;
                } else if (q_status.cursor_y == HEIGHT - 1 - STATUS_HEIGHT) {
                        /* On what is about to be the last line */
                        if (q_status.scrollback_lines >= HEIGHT) {
                                /* Pull back because more lines are present in
                                 the scrollback buffer. */
                                q_status.cursor_y--;
                        }
                }
                q_screen_dirty = Q_TRUE;
        }
} /* ---------------------------------------------------------------------- */

/*
 * console_keyboard_handler
 */
void console_keyboard_handler(int keystroke, int flags) {
        char * filename;
        char notify_message[DIALOG_MESSAGE_SIZE];
        char command_line[COMMAND_LINE_SIZE];
        int i;
        struct file_info * batch_upload_file_list = NULL;
        int new_keystroke;
        Q_HOST_TYPE host_type;
        char * port;

        if (q_status.doorway_mode == Q_DOORWAY_MODE_FULL) {
                if ((keystroke == '=') && (flags & KEY_FLAG_ALT)) {
                        /* Alt-= Doorway mode */
                        q_status.doorway_mode = Q_DOORWAY_MODE_OFF;
                        notify_form(_("Doorway OFF"), 1.5);
                        q_cursor_on();
                } else {
                        /* Pass keystroke */
                        post_keystroke(keystroke, flags);
                }
                return;
        }

        if (q_status.doorway_mode == Q_DOORWAY_MODE_MIXED) {
                if ((keystroke == '=') && (flags & KEY_FLAG_ALT)) {
                        /* Alt-= Doorway mode */
                        q_status.doorway_mode = Q_DOORWAY_MODE_FULL;
                        notify_form(_("Doorway FULL"), 1.5);
                        q_cursor_on();
                        return;
                } else {
                        if (q_key_code_yes(keystroke)) {
                                /* See if this is PgUp or PgDn */
                                if ((keystroke == Q_KEY_NPAGE) && (flags == 0) && (doorway_mixed_pgdn == Q_TRUE)) {
                                        /* Raw PgDn, pass it on */
                                        post_keystroke(keystroke, flags);
                                        return;
                                }
                                if ((keystroke == Q_KEY_PPAGE) && (flags == 0) && (doorway_mixed_pgup == Q_TRUE)) {
                                        /* Raw PgUp, pass it on */
                                        post_keystroke(keystroke, flags);
                                        return;
                                }
                                if ((keystroke == Q_KEY_NPAGE) || (keystroke == Q_KEY_PPAGE)) {
                                        /* Modified PgUp/PgDn, handle it below */
                                } else {
                                        /* Some other function key, pass it on */
                                        post_keystroke(keystroke, flags);
                                        return;
                                }
                        } else if ( (doorway_mixed[keystroke] == Q_TRUE) ||
                                (keystroke == 0x03) ||
                                (keystroke == 0x00)
                        ) {
                                /* This keystroke is supposed to be
                                 * handled, so honor it.  We pass
                                 * Ctrl-C and Ctrl-Space to the block
                                 * below so that MIXED mode has the
                                 * same easy exit when not connected.
                                 */
                        } else {
                                /* Pass keystroke */
                                post_keystroke(keystroke, flags);
                                return;
                        }
                }
        }

        if ((keystroke == 0x03) || (keystroke == 0x00)) {
                /* Special check for Ctrl-C */
                if (!Q_SERIAL_OPEN && (q_status.online == Q_FALSE)) {
                        /* Behave just like Alt-X Exit */
                        new_keystroke = tolower(notify_prompt_form(
                                            _("Exit Qodem"),
                                            _(" Are you sure? [Y/n] "),
                                            _(" Y-Exit Qodem   N-Return to TERMINAL Mode "),
                                            Q_TRUE,
                                            0.0, "YyNn\r"));

                        if ((new_keystroke == 'y') || (new_keystroke == C_CR)) {
                                switch_state(Q_STATE_EXIT);
                                return;
                        }
                }
        }

        switch (keystroke) {

        case '0':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-0 Session log */
                        if (q_status.logging == Q_FALSE) {
                                filename = save_form(_("Log Filename"), get_option(Q_OPTION_LOG_FILE), Q_FALSE, Q_FALSE);
                                if (filename != NULL) {
                                        start_logging(filename);
                                        Xfree(filename, __FILE__, __LINE__);
                                }
                        } else {
                                stop_logging();
                                notify_form(_("Logging OFF"), 1.5);
                                q_cursor_on();
                        }
                        return;
                }
                break;

#ifndef Q_NO_SERIAL
        case '1':
                /* Alt-1 XON/XOFF Flow Control */
                if (flags & KEY_FLAG_ALT) {

                        if (q_serial_port.xonxoff == Q_TRUE) {
                                q_serial_port.xonxoff = Q_FALSE;
                                notify_form(_("XON/XOFF OFF"), 1.5);
                        } else {
                                q_serial_port.xonxoff = Q_TRUE;
                                notify_form(_("XON/XOFF ON"), 1.5);
                        }
                        q_cursor_on();

                        /* We edited something, reconfigure the port if it's open */
                        if (Q_SERIAL_OPEN) {
                                if (configure_serial_port() == Q_FALSE) {
                                        /* notify_form() just turned off the cursor */
                                        q_cursor_on();
                                }
                        }
                        return;
                }
                break;
#endif /* Q_NO_SERIAL */

        case '2':
                /* Alt-2 Backspace/Del Mode */
                if (flags & KEY_FLAG_ALT) {
                        if (q_status.hard_backspace == Q_TRUE) {
                                q_status.hard_backspace = Q_FALSE;
                                notify_form(_("Backspace is DEL"), 1.5);
                        } else {
                                q_status.hard_backspace = Q_TRUE;
                                notify_form(_("Backspace is ^H"), 1.5);
                        }
                        q_cursor_on();
                        return;
                }
                break;

        case '3':
                /* Alt-3 Line Wrap */
                if (flags & KEY_FLAG_ALT) {
                        if (q_status.line_wrap == Q_TRUE) {
                                q_status.line_wrap = Q_FALSE;
                                notify_form(_("Line Wrap OFF"), 1.5);
                        } else {
                                q_status.line_wrap = Q_TRUE;
                                notify_form(_("Line Wrap ON"), 1.5);
                        }
                        q_cursor_on();
                        return;
                }
                break;

        case '4':
                /* Alt-4 Display NULL */
                if (flags & KEY_FLAG_ALT) {
                        if (q_status.display_null == Q_TRUE) {
                                q_status.display_null = Q_FALSE;
                                notify_form(_("Display NULL OFF"), 1.5);
                        } else {
                                q_status.display_null = Q_TRUE;
                                notify_form(_("Display NULL ON"), 1.5);
                        }
                        q_cursor_on();
                        return;
                }
                break;

        case '5':
                /* Alt-5 Host Mode */
                if (flags & KEY_FLAG_ALT) {
                        /* Don't allow host if already online */
                        if ((q_status.online == Q_TRUE) || Q_SERIAL_OPEN) {
                                break;
                        }

                        if (ask_host_type(&host_type) == Q_TRUE) {
                                /* User asked for host mode */

                                /* Check port option */
                                switch (host_type) {
                                case Q_HOST_TYPE_SOCKET:
                                case Q_HOST_TYPE_TELNETD:
                                        /* Get a port selection */
                                        if (prompt_listen_port(&port) == Q_FALSE) {
                                                /*
                                                 * User cancelled on
                                                 * the port dialog
                                                 */
                                                q_screen_dirty = Q_TRUE;
                                                return;
                                        }

                                        break;
#ifndef Q_NO_SERIAL
                                case Q_HOST_TYPE_MODEM:
                                case Q_HOST_TYPE_SERIAL:
                                        /* Do nothing, host mode will
                                         * handle it */
                                        break;
#endif /* Q_NO_SERIAL */
                                }

                                /*
                                 * Switch state first, because
                                 * host_start() might switch back
                                 * immediately if it fails to bind.
                                 */
                                switch_state(Q_STATE_HOST);
                                host_start(host_type, port);
                                return;
                        }
                }
                break;

        case '6':
                /* Alt-6 Batch Entry Window */
                if (flags & KEY_FLAG_ALT) {
                        batch_upload_file_list = batch_entry_window(get_option(Q_OPTION_UPLOAD_DIR), Q_FALSE);
                        if (batch_upload_file_list != NULL) {
                                for (i = 0; batch_upload_file_list[i].name != NULL; i++) {
                                        Xfree(batch_upload_file_list[i].name, __FILE__, __LINE__);
                                }
                                Xfree(batch_upload_file_list, __FILE__, __LINE__);
                        }
                        /* Refresh screen */
                        q_screen_dirty = Q_TRUE;
                        return;
                }
                break;

        case '7':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-7 Status line info */
                        if (q_status.status_line_info == Q_TRUE) {
                                q_status.status_line_info = Q_FALSE;
                        } else {
                                q_status.status_line_info = Q_TRUE;
                        }
                        return;
                }
                break;

        case '8':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-8 8th bit strip */
                        if (q_status.strip_8th_bit == Q_TRUE) {
                                q_status.strip_8th_bit = Q_FALSE;
                                notify_form(_("Strip 8th OFF"), 1.5);
                        } else {
                                q_status.strip_8th_bit = Q_TRUE;
                                notify_form(_("Strip 8th ON"), 1.5);
                        }
                        q_cursor_on();
                        return;
                }
                break;

#ifndef Q_NO_SERIAL
        case '9':
                /* Alt-9 Serial Port */
                if (flags & KEY_FLAG_ALT) {
                        if (!Q_SERIAL_OPEN && (q_status.online == Q_FALSE)) {
                                if (open_serial_port() == Q_FALSE) {
                                        /* notify_form() just turned off the cursor */
                                        q_cursor_on();
                                }
                        } else if (!Q_SERIAL_OPEN && (q_status.online == Q_TRUE)) {
                                /* You can't do serial port while connected somewhere else */
                                snprintf(notify_message, sizeof(notify_message), _("Cannot open serial port while connected to non-serial host."));
                                notify_form(notify_message, 0);
                                q_cursor_on();
                        } else if (Q_SERIAL_OPEN && (q_status.online == Q_TRUE)) {
                                /* Hangup first */
                                if (q_status.guard_hangup == Q_TRUE) {
                                        new_keystroke = tolower(notify_prompt_form(
                                                            _("Hangup"),
                                                            _("Hangup Modem? [Y/n] "),
                                                            _(" Y-Hang Up the Connection   N-Exit "),
                                                            Q_TRUE,
                                                            0.0, "YyNn\r"));
                                } else {
                                        new_keystroke = 'y';
                                }
                                if ((new_keystroke == 'y') || (new_keystroke == C_CR)) {
                                        notify_form(_("Sending Hang-Up command"), 1.5);
                                        hangup_modem();
                                        if (close_serial_port() == Q_FALSE) {
                                                /* notify_form() just turned off the cursor */
                                                q_cursor_on();
                                        }
                                }
                                q_cursor_on();
                        } else if (Q_SERIAL_OPEN && (q_status.online == Q_FALSE)) {
                                /* Close port */
                                if (close_serial_port() == Q_FALSE) {
                                        /* notify_form() just turned off the cursor */
                                        q_cursor_on();
                                }

                        }
                        return;
                }
                break;
#endif /* Q_NO_SERIAL */

        case '-':
                /* Alt-- Status Lines */
                if (flags & KEY_FLAG_ALT) {
                        if (q_status.status_visible == Q_TRUE) {
                                set_status_line(Q_FALSE);
                        } else {
                                set_status_line(Q_TRUE);
                        }
                        return;
                }
                break;

        case '+':
                /* Alt-+ CR/CRLF */
                if (flags & KEY_FLAG_ALT) {
                        if (q_status.line_feed_on_cr == Q_TRUE) {
                                q_status.line_feed_on_cr = Q_FALSE;
                                notify_form(_("Add LF OFF"), 1.5);
                        } else {
                                q_status.line_feed_on_cr = Q_TRUE;
                                notify_form(_("Add LF ON"), 1.5);
                        }
                        q_cursor_on();
                        return;
                }
                break;

        case '=':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-= Doorway mode */
                        q_status.doorway_mode = Q_DOORWAY_MODE_MIXED;
                        notify_form(_("Doorway MIXED"), 1.5);
                        q_cursor_on();
                        return;
                }
                break;

        case ',':
                /* Alt-, ANSI Music */
                if (flags & KEY_FLAG_ALT) {
                        if (q_status.ansi_music == Q_TRUE) {
                                q_status.ansi_music = Q_FALSE;
                                notify_form(_("ANSI Music OFF"), 1.5);
                        } else {
                                if (q_status.sound == Q_TRUE) {
                                        q_status.ansi_music = Q_TRUE;
                                        notify_form(_("ANSI Music ON"), 1.5);
                                }
                        }
                        q_cursor_on();
                        return;
                }
                break;

        case 'A':
        case 'a':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-A Translate table */
                        switch_state(Q_STATE_TRANSLATE_MENU);
                        return;
                }
                break;

        case 'B':
        case 'b':
                /* Alt-B Beeps and bells */
                if (flags & KEY_FLAG_ALT) {
                        if (q_status.beeps == Q_TRUE) {
                                q_status.beeps = Q_FALSE;
                                notify_form(_("Beeps & Bells OFF"), 1.5);
                        } else {
                                if (q_status.sound == Q_TRUE) {
                                        q_status.beeps = Q_TRUE;
                                        notify_form(_("Beeps & Bells ON"), 1.5);
                                }
                        }
                        q_cursor_on();
                        return;
                }
                break;

        case 'C':
        case 'c':
                if (flags & KEY_FLAG_ALT) {
                        if (q_status.emulation != Q_EMUL_DEBUG) {
                                /* Alt-C Clear screen */
                                cursor_formfeed();

                                /* Switch back to default color */
                                q_current_color = Q_A_NORMAL | scrollback_full_attr(Q_COLOR_CONSOLE_TEXT);
                                q_screen_dirty = Q_TRUE;
                                return;
                        } else {
                                /* DEBUG emulation, don't pass the keystroke, kill it here */
                                return;
                        }
                }
                break;

        case 'D':
        case 'd':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-D Phonebook */
                        switch_state(Q_STATE_PHONEBOOK);
                        return;
                }
                break;

        case 'E':
        case 'e':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-E Half/full duplex */
                        if ((q_status.full_duplex == Q_TRUE) && (q_status.emulation != Q_EMUL_DEBUG)) {
                                q_status.full_duplex = Q_FALSE;
                                notify_form(_("Half Duplex"), 1.5);
                        } else {
                                q_status.full_duplex = Q_TRUE;
                                notify_form(_("Full Duplex"), 1.5);
                        }
                        q_cursor_on();
                        return;
                }
                break;


        case 'F':
        case 'f':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-F Execute script */

                        /* Prompt for script filename */
                        filename = save_form(_("Execute Script"), "", Q_FALSE, Q_FALSE);
                        if (filename != NULL) {
                                if (strlen(filename) > 0) {
                                        /* script_start() will call switch_state() if all was OK. */
                                        script_start(filename);
                                }
                                /* No leak */
                                Xfree(filename, __FILE__, __LINE__);
                        }
                        return;
                }
                break;

        case 'G':
        case 'g':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-G Term Emulation */
                        if (q_status.split_screen == Q_FALSE) {
                                switch_state(Q_STATE_EMULATION_MENU);
                        }
                        return;
                }
                break;

        case 'H':
        case 'h':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-H Hangup modem */
                        if ((q_status.online == Q_TRUE) || Q_SERIAL_OPEN) {
                                if (q_status.guard_hangup == Q_TRUE) {
                                        if (Q_SERIAL_OPEN) {
                                                new_keystroke = tolower(notify_prompt_form(
                                                                _("Hangup"),
                                                                _("Hangup Modem? [Y/n] "),
                                                                _(" Y-Hang Up the Connection   N-Exit "),
                                                                Q_TRUE,
                                                                0.0, "YyNn\r"));
                                        } else {
                                                new_keystroke = tolower(notify_prompt_form(
                                                                _("Close"),
                                                                _("Close Connection? [Y/n] "),
                                                                _(" Y-Close Connection   N-Exit "),
                                                                Q_TRUE,
                                                                0.0, "YyNn\r"));
                                        }
                                } else {
                                        new_keystroke = 'y';
                                }
                                if ((new_keystroke == 'y') || (new_keystroke == C_CR)) {
                                        if (Q_SERIAL_OPEN) {
                                                notify_form(_("Sending Hang-Up Command"), 1.5);
                                                qlog(_("Sending Hang-up Command\n"));
                                        } else {
                                                notify_form(_("Closing Connection"), 1.5);
                                                qlog(_("Closing Connection\n"));
                                        }
                                        q_cursor_on();
                                        q_status.hanging_up = Q_TRUE;
                                        if (!Q_SERIAL_OPEN) {
                                                close_connection();
                                        } else {
#ifndef Q_NO_SERIAL
                                                hangup_modem();
                                                if (close_serial_port() == Q_FALSE) {
                                                        /* notify_form() just turned off the cursor */
                                                        q_cursor_on();
                                                }
#endif /* Q_NO_SERIAL */
                                        }
                                }
                        }
                        return;
                }
                break;

        case 'I':
        case 'i':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-I Information */
                        switch_state(Q_STATE_INFO);
                        return;
                }
                break;

        case 'J':
        case 'j':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-J Function Keys */
                        switch_state(Q_STATE_FUNCTION_KEY_EDITOR);
                        return;
                }
                break;

#ifndef Q_NO_SERIAL
        case 'K':
        case 'k':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-K Send Break */
                        if (Q_SERIAL_OPEN) {
                                /*
                                 * For linux, break value is in 'jiffies' -- apparently
                                 * 1 jiffie = 1/100 seconds
                                 *
                                 * On all architectures:
                                 *
                                 * 0 means 0.25 secs <= duration <= 0.50 secs
                                 */
                                if (tcsendbreak(q_child_tty_fd, 0) < 0) {
                                        /* Error */
                                        snprintf(notify_message, sizeof(notify_message), _("Error sending BREAK to \"%s\": %s"), q_modem_config.dev_name, strerror(errno));
                                        notify_form(notify_message, 0);
                                        q_cursor_on();
                                } else {
                                        qlog(_("Sent BREAK\n"));
                                }
                        }
                        return;
                }
                break;
#endif /* Q_NO_SERIAL */

        case 'L':
        case 'l':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-L Log View */
                        screen_clear();
                        screen_put_str_yx(0, 0, _("Spawning editor...\n\n"), Q_A_NORMAL, 0);
                        screen_flush();
                        sprintf(command_line, "%s %s", get_option(Q_OPTION_EDITOR),
                                get_workingdir_filename(get_option(Q_OPTION_LOG_FILE)));
                        spawn_terminal(command_line);
                        return;
                }
                break;

        case 'M':
        case 'm':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-M Mail reader */
                        if (strlen(get_option(Q_OPTION_MAIL_READER)) > 0) {
                                qlog(_("Spawning mail reader with command line '%s'...\n"), get_option(Q_OPTION_MAIL_READER));
                                screen_clear();
                                screen_put_str_yx(0, 0, _("Spawning mail reader...\n\n"), Q_A_NORMAL, 0);
                                screen_flush();
                                spawn_terminal(get_option(Q_OPTION_MAIL_READER));
                        }
                        return;
                }
                break;

        case 'N':
        case 'n':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-N Configuration */
                        screen_clear();
                        screen_put_str_yx(0, 0, _("Spawning editor...\n\n"), Q_A_NORMAL, 0);
                        screen_flush();
                        sprintf(command_line, "%s %s", get_option(Q_OPTION_EDITOR), get_options_filename());
                        spawn_terminal(command_line);

                        /* Reload options from file */
                        load_options();

                        return;
                }
                break;

        case ':':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-: Colors */
                        screen_clear();
                        screen_put_str_yx(0, 0, _("Spawning editor...\n\n"), Q_A_NORMAL, 0);
                        screen_flush();
                        sprintf(command_line, "%s %s", get_option(Q_OPTION_EDITOR), get_colors_filename());
                        spawn_terminal(command_line);

                        /* Reload colors from file */
                        load_colors();

                        return;
                }
                break;

#ifndef Q_NO_SERIAL
        case 'O':
        case 'o':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-O Modem Config */
                        switch_state(Q_STATE_MODEM_CONFIG);
                        return;
                }
                break;
#endif /* Q_NO_SERIAL */

        case 'P':
        case 'p':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-P Capture File */
                        if (q_status.capture == Q_FALSE) {
                                reset_capture_type();
                                if (q_status.capture_type == Q_CAPTURE_TYPE_ASK) {
                                        q_status.capture_type = ask_capture_type();
                                        q_screen_dirty = Q_TRUE;
                                        console_refresh(Q_FALSE);
                                }
                                if (q_status.capture_type != Q_CAPTURE_TYPE_ASK) {
                                        filename = save_form(_("Capture Filename"),
                                                get_option(Q_OPTION_CAPTURE_FILE), Q_FALSE, Q_FALSE);
                                        if (filename != NULL) {
                                                start_capture(filename);
                                                Xfree(filename, __FILE__, __LINE__);
                                        }
                                }
                        } else {
                                stop_capture();
                                notify_form(_("Capture OFF"), 1.5);
                                q_cursor_on();
                        }
                        return;
                }
                break;

        case 'Q':
        case 'q':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-Q QuickLearn */
                        if (q_status.quicklearn == Q_FALSE) {
                                filename = save_form(_("Quicklearn Filename"), get_scriptdir_filename(""), Q_FALSE, Q_FALSE);
                                if (filename != NULL) {
                                        if (file_exists(filename) == Q_TRUE) {
                                                /* Prompt to overwrite */
                                                new_keystroke = tolower(notify_prompt_form(
                                                        _("Script File Already Exists"),
                                                        _(" Overwrite File? [Y/n] "),
                                                        _(" Y-Overwrite Script File   N-Abort Quicklearn "),
                                                        Q_TRUE,
                                                        0.0, "YyNn\r"));
                                                if ((new_keystroke == 'y') || (new_keystroke == C_CR)) {
                                                        start_quicklearn(filename);
                                                }
                                        } else {
                                                start_quicklearn(filename);
                                        }
                                        Xfree(filename, __FILE__, __LINE__);
                                }
                        }
                        return;
                }
                break;

        case 'R':
        case 'r':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-R OS shell */
                        qlog(_("Spawning system shell with command line '%s'...\n"), get_option(Q_OPTION_SHELL));
                        screen_clear();
                        screen_put_str_yx(0, 0, _("Spawning system shell...\n\n"), Q_A_NORMAL, 0);
                        screen_flush();
                        spawn_terminal(get_option(Q_OPTION_SHELL));
                        return;
                }
                break;

        case 'S':
        case 's':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-S Split Screen */
                        if (q_status.split_screen == Q_FALSE) {
                                q_status.split_screen = Q_TRUE;
                                split_screen_emulation = q_status.emulation;
                                memset(split_screen_buffer, 0, sizeof(split_screen_buffer));
                                split_screen_buffer_n = 0;
                                split_screen_x = 0;
                                split_screen_y = HEIGHT - 1 - STATUS_HEIGHT - 4;
                                q_split_screen_dirty = Q_TRUE;
                        } else {
                                q_status.split_screen = Q_FALSE;
                                q_status.emulation = split_screen_emulation;
                                split_screen_x = 0;
                                split_screen_y = 0;
                        }
                        cursor_formfeed();
                        reset_emulation();
                        /* To avoid the "disappearing line" let's move the
                         * "real" cursor to (6,0) so it'll be the top corner
                         * of the split-screen screen.
                         */
                        if (q_status.split_screen == Q_TRUE) {
                                cursor_position(6, 0);
                        }
                        q_screen_dirty = Q_TRUE;
                        return;
                }
                break;

        case 'T':
        case 't':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-T Screen dump */
                        reset_screen_dump_type();
                        if (q_status.screen_dump_type == Q_CAPTURE_TYPE_ASK) {
                                q_status.screen_dump_type = ask_save_type();
                                q_screen_dirty = Q_TRUE;
                                console_refresh(Q_FALSE);
                        }
                        if (q_status.screen_dump_type != Q_CAPTURE_TYPE_ASK) {
                                filename = save_form(_("Screen Dump Filename"), _("screen_dump.txt"), Q_FALSE, Q_FALSE);
                                if (filename != NULL) {
                                        qlog(_("Screen dump to file '%s'\n"), filename);
                                        if (screen_dump(filename) == Q_FALSE) {
                                                snprintf(notify_message, sizeof(notify_message), _("Error saving to file \"%s\""), filename);
                                                notify_form(notify_message, 0);
                                                q_cursor_on();
                                        }
                                        Xfree(filename, __FILE__, __LINE__);
                                }
                        }
                        return;
                }
                break;

        case 'U':
        case 'u':
                /* Alt-U Scrollback */
                if (flags & KEY_FLAG_ALT) {
                        if (q_status.scrollback_enabled == Q_TRUE) {
                                q_status.scrollback_enabled = Q_FALSE;
                                notify_form(_("Scrollback OFF"), 1.5);
                        } else {
                                q_status.scrollback_enabled = Q_TRUE;
                                notify_form(_("Scrollback ON"), 1.5);
                        }
                        q_cursor_on();
                        return;
                }
                break;

        case 'V':
        case 'v':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-V View a File */
                        filename = save_form(_("View File"), get_option(Q_OPTION_WORKING_DIR), Q_FALSE, Q_FALSE);
                        if (filename != NULL) {
                                screen_clear();
                                screen_put_str_yx(0, 0, _("Spawning editor...\n\n"), Q_A_NORMAL, 0);
                                screen_flush();
                                sprintf(command_line, "%s %s", get_option(Q_OPTION_EDITOR), filename);
                                spawn_terminal(command_line);
                                Xfree(filename, __FILE__, __LINE__);
                        }
                        return;
                }
                break;

        case 'W':
        case 'w':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-W View Directory */
                        filename = save_form(_("List Directory"), get_option(Q_OPTION_WORKING_DIR), Q_TRUE, Q_FALSE);
                        if (filename != NULL) {
                                q_cursor_off();
                                view_directory(filename, "*");
                                q_cursor_on();
                                q_screen_dirty = Q_TRUE;
                                Xfree(filename, __FILE__, __LINE__);
                        }
                        return;
                }
                break;

        case 'X':
        case 'x':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-X Exit */
                        new_keystroke = tolower(notify_prompt_form(
                                            _("Exit Qodem"),
                                            _(" Are you sure? [Y/n] "),
                                            _(" Y-Exit Qodem   N-Return to TERMINAL Mode "),
                                            Q_TRUE,
                                            0.0, "YyNn\r"));

                        if ((new_keystroke == 'y') || (new_keystroke == C_CR)) {
                                switch_state(Q_STATE_EXIT);
                        }
                        return;
                }
                break;

#ifndef Q_NO_SERIAL
        case 'Y':
        case 'y':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-Y COM Parameters */

                        /* Use the comm_settings_form to get the values */
                        if (comm_settings_form(_("Serial Port Settings"), &q_serial_port.baud, &q_serial_port.data_bits, &q_serial_port.parity, &q_serial_port.stop_bits, &q_serial_port.xonxoff, &q_serial_port.rtscts) == Q_TRUE) {
                                /* We edited something, reconfigure the port if it's open */
                                if (Q_SERIAL_OPEN) {
                                        if (configure_serial_port() == Q_FALSE) {
                                                /* notify_form() just turned off the cursor */
                                                q_cursor_on();
                                        }
                                }
                        }
                        return;
                }
                break;
#endif /* Q_NO_SERIAL */

        case 'Z':
        case 'z':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-Z Menu */
                        switch_state(Q_STATE_CONSOLE_MENU);
                        return;
                }
                break;

        default:
                break;

        }

        switch (keystroke) {

        case Q_KEY_PPAGE:
                if ((flags & KEY_FLAG_UNICODE) == 0) {
                        /* PgUp Upload */
                        switch_state(Q_STATE_UPLOAD_MENU);
                }
                return;

        case Q_KEY_NPAGE:
                if ((flags & KEY_FLAG_UNICODE) == 0) {
                        /* PgUp Download */
                        switch_state(Q_STATE_DOWNLOAD_MENU);
                }
                return;

        case '/':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-/ Scrollback */
                        switch_state(Q_STATE_SCROLLBACK);
                        return;
                }
                break;

        case '\\':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-\ Compose key */
                        if ((q_status.emulation == Q_EMUL_LINUX_UTF8) || (q_status.emulation == Q_EMUL_XTERM_UTF8)) {
                                new_keystroke = compose_key(Q_TRUE);
                        } else {
                                new_keystroke = compose_key(Q_FALSE);
                        }
                        if (new_keystroke != -1) {
                                keystroke = new_keystroke;
                                flags &= ~KEY_FLAG_ALT;
                                if ((q_status.emulation == Q_EMUL_LINUX_UTF8) || (q_status.emulation == Q_EMUL_XTERM_UTF8)) {
                                        flags |= KEY_FLAG_UNICODE;
                                }
                        } else {
                                return;
                        }
                }
                break;

        case ';':
                if (flags & KEY_FLAG_ALT) {
                        /* Alt-; Codepage */
                        if (q_status.split_screen == Q_FALSE) {
                                switch_state(Q_STATE_CODEPAGE);
                        }
                        return;
                }
                break;

        default:
                break;
        }

        /* Pass keystroke */
        if (q_status.split_screen == Q_FALSE) {
                new_keystroke = keystroke;
                if (((new_keystroke <= 0xFF) && ((flags & KEY_FLAG_UNICODE) == 0)) ||
                        ((new_keystroke <= 0x7F) && ((flags & KEY_FLAG_UNICODE) != 0))) {
                        /*
                         * Run regular keystrokes through the output
                         * translation table.  Note that Unicode keys
                         * greater than 0x7F will not get translated.
                         */
                        new_keystroke = q_translate_table_output.map_to[new_keystroke];
                }

                post_keystroke(new_keystroke, flags);
                return;
        }

        /* Split screen: save character UNLESS it's an ENTER */
        if ((keystroke == Q_KEY_ENTER) || (keystroke == C_CR)) {
                /* Transmit entire buffer */
                for (i=0; i<split_screen_buffer_n; i++) {
                        if (    (split_screen_buffer[i] == '^') &&
                                (i+1 < split_screen_buffer_n) &&
                                (split_screen_buffer[i+1] == 'M')
                        ) {
                                post_keystroke(C_CR, 0);
                                i++;
                                continue;
                        }
                        post_keystroke(split_screen_buffer[i], 0);
                }
                memset(split_screen_buffer, 0, sizeof(split_screen_buffer));
                split_screen_buffer_n = 0;
                q_screen_dirty = Q_TRUE;
                q_split_screen_dirty = Q_TRUE;
                split_screen_x = 0;
                split_screen_y = HEIGHT - 1 - STATUS_HEIGHT - 4;
                return;
        }

        /* Split screen: BACKSPACE */
        if ((keystroke == Q_KEY_BACKSPACE) || (keystroke == 0x08) || (keystroke == 0x7F)) {
                if (split_screen_buffer_n > 0) {

                        split_screen_x--;
                        screen_put_color_char_yx(split_screen_y, split_screen_x, ' ', Q_COLOR_CONSOLE_TEXT);
                        screen_move_yx(split_screen_y, split_screen_x);
                        if (split_screen_x < 0) {
                                split_screen_y--;
                                split_screen_x = 0;
                        }
                        q_split_screen_dirty = Q_TRUE;
                        split_screen_buffer[split_screen_buffer_n] = '\0';
                        split_screen_buffer_n--;
                }
                return;
        }

        if (split_screen_buffer_n < sizeof(split_screen_buffer)) {
                /* Append keystroke to buffer */
                split_screen_buffer[split_screen_buffer_n] = keystroke;
                split_screen_buffer_n++;
                screen_put_color_char(keystroke & 0xFF, Q_COLOR_CONSOLE_TEXT);
                split_screen_x++;
                if (split_screen_x == WIDTH) {
                        split_screen_x = 0;
                        split_screen_y++;
                }
                q_split_screen_dirty = Q_TRUE;
                return;
        }
} /* ---------------------------------------------------------------------- */

/*
 * console_process_incoming_data
 */
void console_process_incoming_data(unsigned char * buffer, const int n, int * remaining) {
        int i;
        wchar_t emulated_char;
        Q_EMULATION_STATUS emulation_rc;

#if 0
        qlog("console.c: buffer_full %s buffer_empty %s running %s paused %s\n",
                (q_running_script.print_buffer_full == Q_TRUE ? "true" : "false"),
                (q_running_script.print_buffer_empty == Q_TRUE ? "true" : "false"),
                (q_running_script.running == Q_TRUE ? "true" : "false"),
                (q_running_script.paused == Q_TRUE ? "true" : "false"));
#endif

        for (i = 0; i < n; i++) {
                if (q_program_state == Q_STATE_SCRIPT_EXECUTE) {
                        if (    (q_running_script.print_buffer_full == Q_TRUE) &&
                                (q_running_script.running == Q_TRUE) &&
                                (q_running_script.paused == Q_FALSE)
                        ) {
                                /*
                                 * Stop processing new characters when the
                                 * script print buffer is full and we're
                                 * waiting on the script process to do
                                 * something with it.
                                 */
                                break;
                        }
                }

                /* DEBUG */
                /* fprintf(stderr, "buffer[i]: %02x ('%c') remaining: %d\n", buffer[i], buffer[i], *remaining); */

                /* Strip 8th bit processing */
                if (q_status.strip_8th_bit == Q_TRUE) {
                        buffer[i] &= 0x7F;
                }

                /* Only do Zmodem and Kermit autostart when not executing a script */
                if (q_program_state != Q_STATE_SCRIPT_EXECUTE) {

                        /* Check for Zmodem autostart*/
                        if (check_zmodem_autostart(buffer[i]) == Q_TRUE) {
                                if (q_download_location == NULL) {
                                        q_download_location = save_form(_("Download Directory"), get_option(Q_OPTION_DOWNLOAD_DIR), Q_TRUE, Q_FALSE);
                                }

                                if (q_download_location != NULL) {
                                        q_transfer_stats.protocol = Q_PROTOCOL_ZMODEM;
                                        switch_state(Q_STATE_DOWNLOAD);
                                        start_file_transfer();
                                }

                                /* Reset check for Zmodem autostart */
                                reset_zmodem_autostart();

                                /* Get out of here */
                                return;
                        }

                        /* Check for Kermit autostart*/
                        if (check_kermit_autostart(buffer[i]) == Q_TRUE) {
                                if (q_download_location == NULL) {
                                        q_download_location = save_form(_("Download Directory"), get_option(Q_OPTION_DOWNLOAD_DIR), Q_TRUE, Q_FALSE);
                                }

                                if (q_download_location != NULL) {
                                        q_transfer_stats.protocol = Q_PROTOCOL_KERMIT;
                                        switch_state(Q_STATE_DOWNLOAD);
                                        start_file_transfer();
                                }

                                /* Reset check for Kermit autostart */
                                reset_kermit_autostart();

                                /* Get out of here */
                                return;
                        }

                } /* if (q_status.state != Q_STATE_SCRIPT_EXECUTE) */

                /* Capture */
                if (q_status.capture == Q_TRUE) {
                        if (q_status.capture_type == Q_CAPTURE_TYPE_RAW) {
                                /* Raw */
                                fprintf(q_status.capture_file, "%c", buffer[i]);
                                if (q_status.capture_flush_time < time(NULL)) {
                                        fflush(q_status.capture_file);
                                        q_status.capture_flush_time = time(NULL);
                                }
                        }
                }

                /*
                 * Run received characters through the input translation table.
                 */
                switch (q_status.emulation) {
                case Q_EMUL_LINUX_UTF8:
                case Q_EMUL_XTERM_UTF8:
                        if (buffer[i] <= 0x7F) {
                                buffer[i] = q_translate_table_input.map_to[buffer[i]];
                        }
                        break;
                default:
                        buffer[i] = q_translate_table_input.map_to[buffer[i]];
                        break;
                }

                /* Normal character -- pass it through emulator */
                emulation_rc = terminal_emulator(buffer[i], &emulated_char);
                *remaining -= 1;

                for (;;) {

                        if (emulation_rc == Q_EMUL_FSM_ONE_CHAR) {

                                /* Print this character */
                                print_character(emulated_char);

                                /* We grabbed the one character, get out */
                                break;
                        } else if (emulation_rc == Q_EMUL_FSM_NO_CHAR_YET) {

                                /* No more characters, break out */
                                break;
                        } else {
                                /* Q_EMUL_FSM_MANY_CHARS */

                                /* Print this character */
                                print_character(emulated_char);

                                /* ...and continue pulling more characters */
                                emulation_rc = terminal_emulator( -1 , &emulated_char);
                        }

                } /* for (;;) */

        } /* for (i = 0; i < n; i++) */

        q_screen_dirty = Q_TRUE;
        if (q_status.split_screen == Q_TRUE) {
                q_split_screen_dirty = Q_TRUE;
        }
} /* ---------------------------------------------------------------------- */

/*
 * console_refresh
 */
void console_refresh(Q_BOOL status_line) {
        static Q_BOOL first = Q_TRUE;
        char * online_string;
        char time_string[32];
        time_t current_time;
        char dec_leds_string[6];

        /* Put the header on the console */
        if (first == Q_TRUE) {
                char header_string[Q_MAX_LINE_LENGTH];
                int i;

                first = Q_FALSE;

                new_scrollback_line();
                sprintf(header_string, _("%s Compiled %s"), "Qodem " Q_VERSION " " Q_VERSION_BRANCH, __DATE__);
                q_scrollback_last->length = strlen(header_string);
                for (i=0; i<q_scrollback_last->length; i++) {
                        q_scrollback_last->chars[i] = cp437_chars[(unsigned char)header_string[i]];
                        q_scrollback_last->colors[i] = scrollback_full_attr(Q_COLOR_CONSOLE);
                }

                new_scrollback_line();
                sprintf(header_string, _("Copyright (C) 2012 %s"), Q_AUTHOR);
                q_scrollback_last->length = strlen(header_string);
                for (i=0; i<q_scrollback_last->length; i++) {
                        q_scrollback_last->chars[i] = cp437_chars[(unsigned char)header_string[i]];
                        q_scrollback_last->colors[i] = scrollback_full_attr(Q_COLOR_CONSOLE);
                }

                /* Blank line */
                new_scrollback_line();

                new_scrollback_line();
                sprintf(header_string, _("You are now in TERMINAL mode"));
                q_scrollback_last->length = strlen(header_string);
                for (i=0; i<q_scrollback_last->length; i++) {
                        q_scrollback_last->chars[i] = cp437_chars[(unsigned char)header_string[i]];
                        q_scrollback_last->colors[i] = scrollback_full_attr(Q_COLOR_CONSOLE);
                }

                new_scrollback_line();
                q_scrollback_last->length = WIDTH;
                for (i=0; i<q_scrollback_last->length; i++) {
                        q_scrollback_last->chars[i] = cp437_chars[DOUBLE_BAR];
                        q_scrollback_last->colors[i] = scrollback_full_attr(Q_COLOR_CONSOLE);
                }

                /* Initial line */
                new_scrollback_line();
                q_scrollback_current = q_scrollback_last;
                q_scrollback_position = q_scrollback_current;

                q_status.cursor_y = 5;
                q_status.cursor_x = 0;
        }

        /* Render scrollback */
        if (q_screen_dirty == Q_TRUE) {

                if (q_status.split_screen == Q_TRUE) {
                        int i;

                        /* Steal 6 lines from the scrollback buffer display:
                         * 5 lines of split-screen area + 1 line for the
                         * split-screen status bar.
                         */
                        render_scrollback(6);

                        /* Clear the bottom lines */
                        /* Start 4 lines above the bottom. */
                        for (i = (HEIGHT - STATUS_HEIGHT - 1 - 4); i < (HEIGHT - STATUS_HEIGHT); i++) {
                                screen_put_color_hline_yx(i, 0, ' ', WIDTH, Q_COLOR_CONSOLE_TEXT);
                        }
                } else {
                        render_scrollback(0);
                }
                q_screen_dirty = Q_FALSE;
        }

        /* Render split screen */
        if (q_split_screen_dirty == Q_TRUE) {
                char * title;
                int left_stop;
                int i, row;

                /* Put the Split Screen line */
                title = _(" Split Screen ");
                left_stop = WIDTH - strlen(title);
                if (left_stop < 0) {
                        left_stop = 0;
                } else {
                        left_stop /= 2;
                }
                screen_put_color_hline_yx(HEIGHT - 1 - STATUS_HEIGHT - 5, 0,
                        cp437_chars[DOUBLE_BAR], WIDTH, Q_COLOR_WINDOW_BORDER);
                screen_put_color_char_yx(HEIGHT - 1 - STATUS_HEIGHT - 5, 3, '[', Q_COLOR_WINDOW_BORDER);
                screen_put_color_printf(Q_COLOR_MENU_COMMAND,
                        _(" Keystrokes Queued: %d "), split_screen_buffer_n);
                screen_put_color_char(']', Q_COLOR_WINDOW_BORDER);

                screen_put_color_char_yx(HEIGHT - 1 - STATUS_HEIGHT - 5, left_stop - 1, '[', Q_COLOR_WINDOW_BORDER);
                screen_put_color_str(title, Q_COLOR_MENU_TEXT);
                screen_put_color_char(']', Q_COLOR_WINDOW_BORDER);

                /* Render the characters in the keyboard buffer */
                row = HEIGHT - 1 - STATUS_HEIGHT - 4;
                screen_move_yx(row, 0);
                for (i=0; i<split_screen_buffer_n; i++) {
                        if ((i > 0) && ((i % WIDTH) == 0)) {
                                row++;
                                screen_move_yx(row, 0);
                        }
                        screen_put_color_char(split_screen_buffer[i] & 0xFF, Q_COLOR_CONSOLE_TEXT);
                }
                q_split_screen_dirty = Q_FALSE;
        }

        /* Render status line */
        if ((q_status.status_visible == Q_TRUE) &&
                (status_line == Q_TRUE) &&
                (q_program_state != Q_STATE_DOWNLOAD) &&
                (q_program_state != Q_STATE_UPLOAD) &&
                (q_program_state != Q_STATE_UPLOAD_BATCH) &&
                (q_status.quicklearn == Q_FALSE)
        ) {

                /* Put up the status line */
                screen_put_color_hline_yx(HEIGHT - 1, 0, ' ', WIDTH, Q_COLOR_STATUS);

                if (q_status.status_line_info == Q_TRUE) {

                        /*
                         Format on right side:

                         < > <Offline | Online > < > <Codepage> < > <Address> < > <VT100 LEDS> < > <Modem lines>

                         Format on left side:

                         YYYY-MM-DD HH:mm:ss <3 spaces>
                         */

                        if ((q_status.online == Q_TRUE) && (q_status.doorway_mode == Q_DOORWAY_MODE_OFF)) {
                                online_string = _("Online");
                        } else if ((q_status.online == Q_FALSE) && (q_status.doorway_mode == Q_DOORWAY_MODE_OFF)) {
                                online_string = _("Offline");
                        } else if ((q_status.online == Q_TRUE) && (q_status.doorway_mode == Q_DOORWAY_MODE_FULL)) {
                                online_string = _("DOORWAY");
                        } else if ((q_status.online == Q_FALSE) && (q_status.doorway_mode == Q_DOORWAY_MODE_FULL)) {
                                online_string = _("doorway");
                        } else if ((q_status.online == Q_TRUE) && (q_status.doorway_mode == Q_DOORWAY_MODE_MIXED)) {
                                online_string = _("MIXED");
                        } else if ((q_status.online == Q_FALSE) && (q_status.doorway_mode == Q_DOORWAY_MODE_MIXED)) {
                                online_string = _("mixed");
                        } else {
                                /* BUG */
                                abort();
                        }

                        screen_put_color_str_yx(HEIGHT - 1, 1, online_string, Q_COLOR_STATUS);

                        screen_put_color_str_yx(HEIGHT - 1, 9, codepage_string(q_status.codepage), Q_COLOR_STATUS);

                        if (q_status.online == Q_TRUE) {
                                /* Show remote phonebook entry name */
                                screen_put_color_printf_yx(HEIGHT - 1, 25, Q_COLOR_STATUS, "%-25.25ls", q_status.remote_phonebook_name);
                        }

                        /* DEC leds */
                        switch (q_status.emulation) {
                        case Q_EMUL_VT100:
                        case Q_EMUL_VT102:
                        case Q_EMUL_VT220:
                        case Q_EMUL_LINUX:
                        case Q_EMUL_XTERM:
                        case Q_EMUL_LINUX_UTF8:
                        case Q_EMUL_XTERM_UTF8:
                                sprintf(dec_leds_string, "L%c%c%c%c",
                                        (q_status.led_1 == Q_TRUE ? '1' : ' '),
                                        (q_status.led_2 == Q_TRUE ? '2' : ' '),
                                        (q_status.led_3 == Q_TRUE ? '3' : ' '),
                                        (q_status.led_4 == Q_TRUE ? '4' : ' '));
                                break;
                        default:
                                memset(dec_leds_string, 0, sizeof(dec_leds_string));
                                break;
                        }
                        screen_put_color_str_yx(HEIGHT - 1, 51, dec_leds_string, Q_COLOR_STATUS);

#ifndef Q_NO_SERIAL
                        /* Modem lines */
                        if (q_status.serial_open == Q_TRUE) {
                                query_serial_port();
                                if (q_serial_port.rs232.DCD == Q_TRUE) {
                                        screen_put_color_str_yx(HEIGHT - 1, 58,
                                                "CD", Q_COLOR_STATUS);
                                } else {
                                        screen_put_color_str_yx(HEIGHT - 1, 58,
                                                "CD", Q_COLOR_STATUS_DISABLED);
                                }
                                if (q_serial_port.rs232.DTR == Q_TRUE) {
                                        screen_put_color_str_yx(HEIGHT - 1, 61,
                                                "DTR", Q_COLOR_STATUS);
                                } else {
                                        screen_put_color_str_yx(HEIGHT - 1, 61,
                                                "DTR", Q_COLOR_STATUS_DISABLED);
                                }
                                if (q_serial_port.rs232.CTS == Q_TRUE) {
                                        screen_put_color_str_yx(HEIGHT - 1, 65,
                                                "CTS", Q_COLOR_STATUS);
                                } else {
                                        screen_put_color_str_yx(HEIGHT - 1, 65,
                                                "CTS", Q_COLOR_STATUS_DISABLED);
                                }
                                if (q_serial_port.rs232.RI == Q_TRUE) {
                                        screen_put_color_str_yx(HEIGHT - 1, 69,
                                                "RI", Q_COLOR_STATUS);
                                } else {
                                        screen_put_color_str_yx(HEIGHT - 1, 69,
                                                "RI", Q_COLOR_STATUS_DISABLED);
                                }
                        }
#endif /* Q_NO_SERIAL */


                        time(&current_time);
                        strftime(time_string, sizeof(time_string),
                                "%Y-%m-%d %H:%M:%S", localtime(&current_time));
                        screen_put_color_str_yx(HEIGHT - 1, WIDTH - strlen(time_string) - 3, time_string, Q_COLOR_STATUS);

                } else {

                        /*
                         Format on right side:

                         < > <EMULATION padded to 7> < > <Offline | Online > <baud padded to 6> < > <8N1> <3 spaces>
                         "[ALT-Z]-Menu" <8 spaces> "LF X " C_CR " CP LG " 0x18

                         Format on left side:

                         HH:mm:ss <3 spaces>
                         */

                        if ((q_status.online == Q_TRUE) && (q_status.doorway_mode == Q_DOORWAY_MODE_OFF)) {
                                online_string = _("Online");
                        } else if ((q_status.online == Q_FALSE) && (q_status.doorway_mode == Q_DOORWAY_MODE_OFF)) {
                                online_string = _("Offline");
                        } else if ((q_status.online == Q_TRUE) && (q_status.doorway_mode == Q_DOORWAY_MODE_FULL)) {
                                online_string = _("DOORWAY");
                        } else if ((q_status.online == Q_FALSE) && (q_status.doorway_mode == Q_DOORWAY_MODE_FULL)) {
                                online_string = _("doorway");
                        } else if ((q_status.online == Q_TRUE) && (q_status.doorway_mode == Q_DOORWAY_MODE_MIXED)) {
                                online_string = _("MIXED");
                        } else if ((q_status.online == Q_FALSE) && (q_status.doorway_mode == Q_DOORWAY_MODE_MIXED)) {
                                online_string = _("mixed");
                        } else {
                                /* BUG */
                                abort();
                        }

                        if (q_status.online == Q_TRUE) {
                                /* time_string needs to be hours/minutes/seconds ONLINE */
                                int hours, minutes, seconds;
                                double online_time;
                                time(&current_time);
                                online_time = difftime(current_time, q_status.connect_time);
                                hours = online_time / 3600;
                                minutes = ((int)online_time % 3600) / 60;
                                seconds = (int)online_time % 60;
                                snprintf(time_string, sizeof(time_string), "%02u:%02u:%02u", hours, minutes, seconds);
                        } else {
                                time(&current_time);
                                strftime(time_string, sizeof(time_string), "%H:%M:%S", localtime(&current_time));
                        }

                        screen_put_color_str_yx(HEIGHT - 1, 1, emulation_string(q_status.emulation), Q_COLOR_STATUS);
                        screen_put_color_str_yx(HEIGHT - 1, 9, online_string, Q_COLOR_STATUS);
                        screen_put_color_str_yx(HEIGHT - 1, 17, _("[ALT-Z]-Menu"), Q_COLOR_STATUS);

#ifndef Q_NO_SERIAL
                        if (Q_SERIAL_OPEN) {
                                /* Baud */
                                if (q_serial_port.dce_baud > 0) {
                                        screen_put_color_printf_yx(HEIGHT - 1, 30, Q_COLOR_STATUS, "%6d", q_serial_port.dce_baud);
                                } else {
                                        screen_put_color_printf_yx(HEIGHT - 1, 30, Q_COLOR_STATUS, "%6s", baud_string(q_serial_port.baud));
                                }
                                /* Data bits */
                                screen_put_color_str_yx(HEIGHT - 1, 37, data_bits_string(q_serial_port.data_bits), Q_COLOR_STATUS);
                                /* Parity */
                                screen_put_color_str_yx(HEIGHT - 1, 38, parity_string(q_serial_port.parity, Q_TRUE), Q_COLOR_STATUS);
                                /* Stop bits */
                                screen_put_color_str_yx(HEIGHT - 1, 39, stop_bits_string(q_serial_port.stop_bits), Q_COLOR_STATUS);
                        } else {
#endif /* Q_NO_SERIAL */
                                /* Show the connection method */
                                if (q_status.online == Q_TRUE) {
                                        if (q_current_dial_entry == NULL) {
                                                /* Command-line */
                                                screen_put_color_str_yx(HEIGHT - 1, 30, "CMDLINE", Q_COLOR_STATUS);
                                        } else {
                                                screen_put_color_str_yx(HEIGHT - 1, 30, method_string(q_current_dial_entry->method), Q_COLOR_STATUS);
                                        }
                                }
#ifndef Q_NO_SERIAL
                        }
#endif /* Q_NO_SERIAL */

                        if (q_status.full_duplex == Q_TRUE) {
                                screen_put_color_str_yx(HEIGHT - 1, 45, _("FDX"), Q_COLOR_STATUS);
                        } else {
                                screen_put_color_str_yx(HEIGHT - 1, 45, _("HDX"), Q_COLOR_STATUS);
                        }
                        if (q_status.strip_8th_bit == Q_TRUE) {
                                screen_put_color_str_yx(HEIGHT - 1, 49, "7", Q_COLOR_STATUS);
                        } else {
                                screen_put_color_str_yx(HEIGHT - 1, 49, "8", Q_COLOR_STATUS);
                        }

                        if (q_status.line_feed_on_cr == Q_FALSE) {
                                screen_put_color_str_yx(HEIGHT - 1, 51, _("LF"), Q_COLOR_STATUS_DISABLED);
                        } else {
                                screen_put_color_str_yx(HEIGHT - 1, 51, _("LF"), Q_COLOR_STATUS);
                        }

                        /* BEEPS/BELLS */
                        if (q_status.beeps == Q_TRUE) {
                                screen_put_color_char_yx(HEIGHT - 1, 54, cp437_chars[0x0D], Q_COLOR_STATUS);
                        } else {
                                screen_put_color_char_yx(HEIGHT - 1, 54, cp437_chars[0x0D], Q_COLOR_STATUS_DISABLED);
                        }

                        /* ANSI MUSIC */
                        if (q_status.ansi_music == Q_TRUE) {
                                screen_put_color_char_yx(HEIGHT - 1, 56, cp437_chars[0x0E], Q_COLOR_STATUS);
                        } else {
                                screen_put_color_char_yx(HEIGHT - 1, 56, cp437_chars[0x0E], Q_COLOR_STATUS_DISABLED);
                        }

                        if (q_status.capture == Q_FALSE) {
                                screen_put_color_str_yx(HEIGHT - 1, 58, _("CP"), Q_COLOR_STATUS_DISABLED);
                        } else {
                                screen_put_color_str_yx(HEIGHT - 1, 58, _("CP"), Q_COLOR_STATUS);
                        }

                        if (q_status.logging == Q_FALSE) {
                                screen_put_color_str_yx(HEIGHT - 1, 61, _("LG"), Q_COLOR_STATUS_DISABLED);
                        } else {
                                screen_put_color_str_yx(HEIGHT - 1, 61, _("LG"), Q_COLOR_STATUS);
                        }

                        if (q_status.scrollback_enabled == Q_FALSE) {
                                screen_put_color_char_yx(HEIGHT - 1, 64, cp437_chars[UPARROW], Q_COLOR_STATUS_DISABLED);
                        } else {
                                screen_put_color_char_yx(HEIGHT - 1, 64, cp437_chars[UPARROW], Q_COLOR_STATUS);
                        }
                        screen_put_color_str_yx(HEIGHT - 1, WIDTH - strlen(time_string) - 3, time_string, Q_COLOR_STATUS);

                } /* if (q_status.status_line_info == Q_TRUE) */

        } /* if (q_status.status_visible == Q_TRUE) */

        if (q_status.quicklearn == Q_TRUE) {
                char * status_string;
                int status_left_stop;

                /* Put up the status line */
                screen_put_color_hline_yx(HEIGHT - 1, 0, cp437_chars[HATCH], WIDTH, Q_COLOR_STATUS);

                status_string = _(" QuickLearn In Progress   Alt-\\-Compose Key   Alt-Q-Stop QuickLearn ");
                status_left_stop = WIDTH - strlen(status_string);
                if (status_left_stop <= 0) {
                        status_left_stop = 0;
                } else {
                        status_left_stop /= 2;
                }
                screen_put_color_str_yx(HEIGHT - 1, status_left_stop, status_string, Q_COLOR_STATUS);

        } /* if (q_status.quicklearn == Q_TRUE) */

        /* Position cursor */
        if (q_status.split_screen == Q_TRUE) {
                screen_move_yx(split_screen_y, split_screen_x);
        } else {
                if (q_scrollback_current->double_width == Q_TRUE) {
                        if (has_true_doublewidth() == Q_FALSE) {
                                screen_move_yx(q_status.cursor_y, (2 * q_status.cursor_x));
                        } else {
                                screen_move_yx(q_status.cursor_y, q_status.cursor_x);
                        }
                } else {
                        screen_move_yx(q_status.cursor_y, q_status.cursor_x);
                }
        }

        switch (q_program_state) {
        case Q_STATE_CONSOLE:
            screen_flush();
            break;
        case Q_STATE_SCRIPT_EXECUTE:
        case Q_STATE_HOST:
        case Q_STATE_CONSOLE_MENU:
        case Q_STATE_INFO:
        case Q_STATE_SCROLLBACK:
        case Q_STATE_DOWNLOAD_MENU:
        case Q_STATE_UPLOAD_MENU:
        case Q_STATE_DOWNLOAD_PATHDIALOG:
        case Q_STATE_UPLOAD_PATHDIALOG:
        case Q_STATE_UPLOAD_BATCH_DIALOG:
        case Q_STATE_UPLOAD:
        case Q_STATE_UPLOAD_BATCH:
        case Q_STATE_DOWNLOAD:
        case Q_STATE_EMULATION_MENU:
        case Q_STATE_TRANSLATE_MENU:
        case Q_STATE_TRANSLATE_EDITOR:
        case Q_STATE_PHONEBOOK:
        case Q_STATE_DIALER:
        case Q_STATE_SCREENSAVER:
        case Q_STATE_FUNCTION_KEY_EDITOR:
#ifndef Q_NO_SERIAL
        case Q_STATE_MODEM_CONFIG:
#endif
        case Q_STATE_CODEPAGE:
        case Q_STATE_INITIALIZATION:
        case Q_STATE_EXIT:
            /*
             * Don't flush.
             */
            break;
        }

} /* ---------------------------------------------------------------------- */

/*
 * console_menu_refresh
 */
void console_menu_refresh() {
        char * status_string;
        int status_left_stop;
        int menu_left, menu_top;
        const int before_row = 1;
        const int during_row = 3;
        const int after_row = 11;
        const int setup_row = 15;
        const int os_row = 19;
        const int toggles_row = 1;

        if (q_screen_dirty == Q_FALSE) {
                return;
        }

        /* Clear screen for when it resizes */
        console_refresh(Q_FALSE);

        menu_left = (WIDTH - 80) / 2;
        menu_top = (HEIGHT - 24) / 2;

        /* The menu window border */
        screen_draw_box(menu_left, menu_top, menu_left + 80, menu_top + 24);

        /* Place the title */
        screen_put_color_str_yx(menu_top, menu_left + 33, _(" COMMAND MENU "), Q_COLOR_WINDOW_BORDER);

        /* Place the "F1 Help" text */
        screen_put_color_str_yx(menu_top + 24 - 1, menu_left + 80 - 11, _("F1 Help"), Q_COLOR_WINDOW_BORDER);

        /* Put up the status line */
        screen_put_color_hline_yx(HEIGHT - 1, 0, cp437_chars[HATCH], WIDTH, Q_COLOR_STATUS);

        status_string = _(" Select a Command    ESC/`-Return to TERMINAL Mode ");
        status_left_stop = WIDTH - strlen(status_string);
        if (status_left_stop <= 0) {
                status_left_stop = 0;
        } else {
                status_left_stop /= 2;
        }
        screen_put_color_str_yx(HEIGHT - 1, status_left_stop, status_string, Q_COLOR_STATUS);

        /* BEFORE */
        screen_put_color_hline_yx(menu_top + before_row, menu_left + 2, cp437_chars[SINGLE_BAR], 19, Q_COLOR_MENU_TEXT);
        screen_put_color_str_yx(menu_top + before_row, menu_left + 21, _(" BEFORE "), Q_COLOR_MENU_TEXT);
        screen_put_color_hline_yx(menu_top + before_row, menu_left + 29, cp437_chars[SINGLE_BAR], 19, Q_COLOR_MENU_TEXT);
        /* D Phonebook */
        screen_put_color_str_yx(menu_top + before_row + 1, menu_left + 2, _("Alt-D  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Phone Book"), Q_COLOR_MENU_TEXT);
        /* G Term emulation */
        screen_put_color_str_yx(menu_top + before_row + 1, menu_left + 27, _("Alt-G  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Term Emulation"), Q_COLOR_MENU_TEXT);

        /* DURING */
        screen_put_color_hline_yx(menu_top + during_row, menu_left + 2, cp437_chars[SINGLE_BAR], 19, Q_COLOR_MENU_TEXT);
        screen_put_color_str_yx(menu_top + during_row, menu_left + 21, _(" DURING "), Q_COLOR_MENU_TEXT);
        screen_put_color_hline_yx(menu_top + during_row, menu_left + 29, cp437_chars[SINGLE_BAR], 19, Q_COLOR_MENU_TEXT);
        /* C Clear screen */
        screen_put_color_str_yx(menu_top + during_row + 1, menu_left + 2, _("Alt-C  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Clear Screen"), Q_COLOR_MENU_TEXT);
        /* T Screen dump */
        screen_put_color_str_yx(menu_top + during_row + 1, menu_left + 27, _("Alt-T  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Screen Dump"), Q_COLOR_MENU_TEXT);

        /* F Execute script */
        screen_put_color_str_yx(menu_top + during_row + 2, menu_left + 2, _("Alt-F  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Execute Script"), Q_COLOR_MENU_TEXT);

#ifndef Q_NO_SERIAL
        /* Y COM parameters */
        screen_put_color_str_yx(menu_top + during_row + 2, menu_left + 27, _("Alt-Y  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("COM Parameters"), Q_COLOR_MENU_TEXT);
        /* K Send BREAK */
        screen_put_color_str_yx(menu_top + during_row + 3, menu_left + 2, _("Alt-K  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Send BREAK"), Q_COLOR_MENU_TEXT);
#endif /* Q_NO_SERIAL */
        /* PgUp Upload files */
        screen_put_color_str_yx(menu_top + during_row + 3, menu_left + 27, _(" PgUp  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Upload Files"), Q_COLOR_MENU_TEXT);
        /* P Capture file */
        screen_put_color_str_yx(menu_top + during_row + 4, menu_left + 2, _("Alt-P  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Capture File"), Q_COLOR_MENU_TEXT);
        /* PgDn Download files */
        screen_put_color_str_yx(menu_top + during_row + 4, menu_left + 27, _(" PgDn  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Download Files"), Q_COLOR_MENU_TEXT);

        /* Q Quicklearn */
        screen_put_color_str_yx(menu_top + during_row + 5, menu_left + 2, _("Alt-Q  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("QuickLearn"), Q_COLOR_MENU_TEXT);

        /* \ Compose Key  */
        screen_put_color_str_yx(menu_top + during_row + 5, menu_left + 27, _("Alt-\\  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Compose Key"), Q_COLOR_MENU_TEXT);
        /* S Split screen */
        screen_put_color_str_yx(menu_top + during_row + 6, menu_left + 2, _("Alt-S  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Split Screen"), Q_COLOR_MENU_TEXT);
        /* ; Codepage */
        screen_put_color_str_yx(menu_top + during_row + 6, menu_left + 27, _("Alt-;  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Codepage"), Q_COLOR_MENU_TEXT);
        /* / Scrollback */
        screen_put_color_str_yx(menu_top + during_row + 7, menu_left + 27, _("Alt-/  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Scroll Back"), Q_COLOR_MENU_TEXT);

        /* AFTER */
        screen_put_color_hline_yx(menu_top + after_row, menu_left + 2, cp437_chars[SINGLE_BAR], 19, Q_COLOR_MENU_TEXT);
        screen_put_color_str_yx(menu_top + after_row, menu_left + 21, _(" AFTER "), Q_COLOR_MENU_TEXT);
        screen_put_color_hline_yx(menu_top + after_row, menu_left + 28, cp437_chars[SINGLE_BAR], 20, Q_COLOR_MENU_TEXT);
        /* H Hangup modem */
        screen_put_color_str_yx(menu_top + after_row + 1, menu_left + 2, _("Alt-H  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Hangup/Close"), Q_COLOR_MENU_TEXT);
        /* M Mail reader */
        screen_put_color_str_yx(menu_top + after_row + 1, menu_left + 27, _("Alt-M  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Mail Reader"), Q_COLOR_MENU_TEXT);
        /* L Log view */
        screen_put_color_str_yx(menu_top + after_row + 2, menu_left + 2, _("Alt-L  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Log View"), Q_COLOR_MENU_TEXT);
        /* X Exit Qodem */
        screen_put_color_str_yx(menu_top + after_row + 2, menu_left + 27, _("Alt-X  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Exit Qodem"), Q_COLOR_MENU_TEXT);

        /* SETUP */
        screen_put_color_hline_yx(menu_top + setup_row, menu_left + 2, cp437_chars[SINGLE_BAR], 19, Q_COLOR_MENU_TEXT);
        screen_put_color_str_yx(menu_top + setup_row, menu_left + 21, _(" SETUP "), Q_COLOR_MENU_TEXT);
        screen_put_color_hline_yx(menu_top + setup_row, menu_left + 28, cp437_chars[SINGLE_BAR], 20, Q_COLOR_MENU_TEXT);
        /* A Translate table */
        screen_put_color_str_yx(menu_top + setup_row + 1, menu_left + 2, _("Alt-A  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Translate Table"), Q_COLOR_MENU_TEXT);
        /* N Configuration */
        screen_put_color_str_yx(menu_top + setup_row + 1, menu_left + 27, _("Alt-N  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Configuration"), Q_COLOR_MENU_TEXT);
        /* J Function keys */
        screen_put_color_str_yx(menu_top + setup_row + 2, menu_left + 2, _("Alt-J  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Function Keys"), Q_COLOR_MENU_TEXT);
        /* : Colors */
        screen_put_color_str_yx(menu_top + setup_row + 2, menu_left + 27, _("Alt-:  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Colors"), Q_COLOR_MENU_TEXT);

        /* OS */
        screen_put_color_hline_yx(menu_top + os_row, menu_left + 2, cp437_chars[SINGLE_BAR], 20, Q_COLOR_MENU_TEXT);
        screen_put_color_str_yx(menu_top + os_row, menu_left + 21, _(" OS "), Q_COLOR_MENU_TEXT);
        screen_put_color_hline_yx(menu_top + os_row, menu_left + 25, cp437_chars[SINGLE_BAR], 23, Q_COLOR_MENU_TEXT);
        /* O Modem config */
        screen_put_color_str_yx(menu_top + os_row + 1, menu_left + 2, _("Alt-O  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Modem Config"), Q_COLOR_MENU_TEXT);
        /* V View file */
        screen_put_color_str_yx(menu_top + os_row + 1, menu_left + 27, _("Alt-V  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("View a File"), Q_COLOR_MENU_TEXT);
        /* R OS shell */
        screen_put_color_str_yx(menu_top + os_row + 2, menu_left + 2, _("Alt-R  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("OS Shell"), Q_COLOR_MENU_TEXT);
        /* W List directory */
        screen_put_color_str_yx(menu_top + os_row + 2, menu_left + 27, _("Alt-W  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("List Directory"), Q_COLOR_MENU_TEXT);

        /* TOGGLES */
        screen_put_color_hline_yx(menu_top + toggles_row, menu_left + 52, cp437_chars[SINGLE_BAR], 8, Q_COLOR_MENU_TEXT);
        screen_put_color_str_yx(menu_top + toggles_row, menu_left + 60, _(" TOGGLES "), Q_COLOR_MENU_TEXT);
        screen_put_color_hline_yx(menu_top + toggles_row, menu_left + 69, cp437_chars[SINGLE_BAR], 9, Q_COLOR_MENU_TEXT);
        /* 0 Session log */
        screen_put_color_str_yx(menu_top + toggles_row + 1, menu_left + 52, _("Alt-0  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Session Log"), Q_COLOR_MENU_TEXT);
#ifndef Q_NO_SERIAL
        /* 1 XON/XOFF */
        screen_put_color_str_yx(menu_top + toggles_row + 2, menu_left + 52, _("Alt-1  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("XON/XOFF Flow Ctrl"), Q_COLOR_MENU_TEXT);
#endif /* Q_NO_SERIAL */
        /* 2 Backspace/Del mode */
        screen_put_color_str_yx(menu_top + toggles_row + 3, menu_left + 52, _("Alt-2  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Backspace/Del Mode"), Q_COLOR_MENU_TEXT);
        /* 3 Line wrap */
        screen_put_color_str_yx(menu_top + toggles_row + 4, menu_left + 52, _("Alt-3  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Line Wrap"), Q_COLOR_MENU_TEXT);
        /* 4 Display NULL */
        screen_put_color_str_yx(menu_top + toggles_row + 5, menu_left + 52, _("Alt-4  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Display NULL"), Q_COLOR_MENU_TEXT);
        /* 5 Host mode */
        screen_put_color_str_yx(menu_top + toggles_row + 6, menu_left + 52, _("Alt-5  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Host Mode"), Q_COLOR_MENU_TEXT);
        /* 6 Batch entry window */
        screen_put_color_str_yx(menu_top + toggles_row + 7, menu_left + 52, _("Alt-6  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Batch Entry Window"), Q_COLOR_MENU_TEXT);
        /* 7 Status line info */
        screen_put_color_str_yx(menu_top + toggles_row + 8, menu_left + 52, _("Alt-7  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Status Line Info"), Q_COLOR_MENU_TEXT);
        /* 8 Hi-bit strip */
        screen_put_color_str_yx(menu_top + toggles_row + 9, menu_left + 52, _("Alt-8  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Hi-Bit Strip"), Q_COLOR_MENU_TEXT);
#ifndef Q_NO_SERIAL
        /* 9 Serial port */
        screen_put_color_str_yx(menu_top + toggles_row + 10, menu_left + 52, _("Alt-9  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Serial Port"), Q_COLOR_MENU_TEXT);
#endif /* Q_NO_SERIAL */
        /* B Beeps and bells */
        screen_put_color_str_yx(menu_top + toggles_row + 11, menu_left + 52, _("Alt-B  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Beeps & Bells"), Q_COLOR_MENU_TEXT);
        /* E Half/full duplex */
        screen_put_color_str_yx(menu_top + toggles_row + 12, menu_left + 52, _("Alt-E  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Half/Full Duplex"), Q_COLOR_MENU_TEXT);
        /* I Qodem information */
        screen_put_color_str_yx(menu_top + toggles_row + 13, menu_left + 52, _("Alt-I  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Qodem Information"), Q_COLOR_MENU_TEXT);
        /* U Scrollback record */
        screen_put_color_str_yx(menu_top + toggles_row + 14, menu_left + 52, _("Alt-U  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Scrollback Record"), Q_COLOR_MENU_TEXT);
        /* = Doorway mode */
        screen_put_color_str_yx(menu_top + toggles_row + 15, menu_left + 52, _("Alt-=  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Doorway Mode"), Q_COLOR_MENU_TEXT);
        /* - Status lines */
        screen_put_color_str_yx(menu_top + toggles_row + 16, menu_left + 52, _("Alt--  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("Status Lines"), Q_COLOR_MENU_TEXT);
        /* + CR/CRLF mode */
        screen_put_color_str_yx(menu_top + toggles_row + 17, menu_left + 52, _("Alt-+  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("CR/CRLF Mode"), Q_COLOR_MENU_TEXT);
        /* , ANSI music */
        screen_put_color_str_yx(menu_top + toggles_row + 18, menu_left + 52, _("Alt-,  "), Q_COLOR_MENU_COMMAND);
        screen_put_color_str(_("ANSI Music"), Q_COLOR_MENU_TEXT);

        screen_put_color_str_yx(menu_top + toggles_row + 20, menu_left + 52, "Qodem " Q_VERSION " " Q_VERSION_BRANCH, Q_COLOR_MENU_COMMAND);
        screen_put_color_printf_yx(menu_top + toggles_row + 21, menu_left + 52, Q_COLOR_MENU_COMMAND, _("Compiled %s"), __DATE__);

        screen_flush();
        q_screen_dirty = Q_FALSE;
} /* ---------------------------------------------------------------------- */

/*
 * console_menu_keyboard_handler
 */
void console_menu_keyboard_handler(const int keystroke, const int flags) {

        /* Process valid keystrokes through the console handler */
        if (flags & KEY_FLAG_ALT) {

                switch (keystroke) {

                case '0':
#ifndef Q_NO_SERIAL
                case '1':
#endif /* Q_NO_SERIAL */
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
#ifndef Q_NO_SERIAL
                case '9':
#endif /* Q_NO_SERIAL */
                case '-':
                case '+':
                case '=':
                case ',':
                case 'a':
                case 'b':
                case 'c':
                case 'd':
                case 'e':
                case 'f':
                case 'g':
                case 'h':
                case 'i':
                case 'j':
#ifndef Q_NO_SERIAL
                case 'k':
#endif /* Q_NO_SERIAL */
                case 'l':
                case 'm':
                case 'n':
#ifndef Q_NO_SERIAL
                case 'o':
#endif /* Q_NO_SERIAL */
                case 'p':
                case 'q':
                case 'r':
                case 's':
                case 't':
                case 'u':
                case 'v':
                case 'w':
                case 'x':
#ifndef Q_NO_SERIAL
                case 'y':
#endif /* Q_NO_SERIAL */
                case 'A':
                case 'B':
                case 'C':
                case 'D':
                case 'E':
                case 'F':
                case 'G':
                case 'H':
                case 'I':
                case 'J':
#ifndef Q_NO_SERIAL
                case 'K':
#endif /* Q_NO_SERIAL */
                case 'L':
                case 'M':
                case 'N':
#ifndef Q_NO_SERIAL
                case 'O':
#endif /* Q_NO_SERIAL */
                case 'P':
                case 'Q':
                case 'R':
                case 'S':
                case 'T':
                case 'U':
                case 'V':
                case 'W':
                case 'X':
#ifndef Q_NO_SERIAL
                case 'Y':
#endif /* Q_NO_SERIAL */
                case '/':
                case '\\':
                case ';':
                case ':':
                        switch_state(Q_STATE_CONSOLE);
                        q_screen_dirty = Q_TRUE;
                        console_refresh(Q_TRUE);
                        console_keyboard_handler(keystroke, flags);
                        return;
                default:
                        break;

                }
        }

        /* Process valid keystrokes through the console handler */
        switch (keystroke) {

        case Q_KEY_PPAGE:
        case Q_KEY_NPAGE:
                switch_state(Q_STATE_CONSOLE);
                q_screen_dirty = Q_TRUE;
                console_refresh(Q_TRUE);
                console_keyboard_handler(keystroke, flags);
                return;

        default:
                break;
        }

        /* Help screen keystrokes */
        switch (keystroke) {

        case Q_KEY_F(1):
                launch_help(Q_HELP_CONSOLE_MENU);

                /*
                 * Refresh the whole screen.  This is one of the few screens
                 * that is "top level" where just a q_screen_dirty is enough.
                 */
                q_screen_dirty = Q_TRUE;
                break;

        case '`':
                /* Backtick works too */
        case KEY_ESCAPE:
                /* ESC return to TERMINAL mode */
                switch_state(Q_STATE_CONSOLE);
                break;
        default:
                /* Ignore keystroke */
                break;
        }
} /* ---------------------------------------------------------------------- */

/*
 * console_info_keyboard_handler
 */
void console_info_keyboard_handler(const int keystroke, const int flags) {

        /* Info screen keystrokes */
        switch (keystroke) {
                case '`':
                        /* Backtick works too */
                case KEY_ESCAPE:
                        /* ESC return to TERMINAL mode */
                        switch_state(Q_STATE_CONSOLE);
                        break;
                case Q_KEY_F(1):
                        break;
                default:
                        /* Ignore keystroke */
                        break;
        }


} /* ---------------------------------------------------------------------- */

/*
 * console_info_refresh
 */
void console_info_refresh() {
        char * status_string;
        static struct q_scrolline_struct * screen;
        static Q_BOOL first = Q_TRUE;
        struct q_scrolline_struct * line;
        int info_left, info_top;
        int status_left_stop;
        int row;
        int i;
        static int delay = 0;
        static Q_BOOL redeye_right = Q_TRUE;
        static int redeye_screen_x;
        static int redeye_pause = 0;

        delay++;

        if ((q_screen_dirty == Q_FALSE) && (delay < 2)) {
                return;
        }

        delay = 0;

        info_left = (WIDTH - 80) / 2;
        info_top = (HEIGHT - 24) / 2;

        if (q_screen_dirty == Q_TRUE) {

                if (first == Q_TRUE) {
                        first = Q_FALSE;
                        screen = (struct q_scrolline_struct *)Xmalloc(sizeof(struct q_scrolline_struct), __FILE__, __LINE__);
                        memset(screen, 0, sizeof(struct q_scrolline_struct));

                        convert_thedraw_screen(q_info_screen, 2 * 80 * 25, screen);
                }

                /* Put up the status line */
                screen_put_color_hline_yx(HEIGHT - 1, 0, cp437_chars[HATCH], WIDTH, Q_COLOR_STATUS);
                status_string = _(" ESC/`-Return to TERMINAL Mode ");
                status_left_stop = WIDTH - strlen(status_string);
                if (status_left_stop <= 0) {
                        status_left_stop = 0;
                } else {
                        status_left_stop /= 2;
                }
                screen_put_color_str_yx(HEIGHT - 1, status_left_stop, status_string, Q_COLOR_STATUS);

                /* Put up the info screen */
                for (row=0, line=screen; line!=NULL; line=line->next) {
                        for (i=0; i<line->length; i++) {
                                if (i >= WIDTH) {
                                        break;
                                }
                                screen_put_scrollback_char_yx(row + info_top, i + info_left, cp437_chars[line->chars[i] & 0xFF], line->colors[i]);
                        }
                        row++;
                }

                /* Put up the build info */
                screen_put_printf_yx(info_top + 4, info_left + 5,
                        Q_A_BOLD, (Q_COLOR_WHITE << 3) | Q_COLOR_CYAN,
                        _("  Qodem %-13s"), Q_VERSION);

                screen_put_str_yx(info_top + 5, info_left + 5,
                        "                     ",
                        Q_A_BOLD, (Q_COLOR_WHITE << 3) | Q_COLOR_CYAN);

                screen_put_str_yx(info_top + 5, info_left + 5,
                        "  "
#if defined(__linux)
                        "Linux"
#else
#  if defined(__APPLE__)
                        "OS X"
#  else
#    if defined(Q_PDCURSES_WIN32)
                        "Win32"
#    else
                        "Unknown"
#    endif
#  endif
#endif
                        " "
#if defined(__amd64)
                        "x86_64"
#else
#  if defined(__i386)
                        "i386"
#  else
                        "unknown"
#  endif
#endif
                        ,
                        Q_A_BOLD, (Q_COLOR_WHITE << 3) | Q_COLOR_CYAN);

                screen_put_printf_yx(info_top + 6, info_left + 5,
                        Q_A_BOLD, (Q_COLOR_WHITE << 3) | Q_COLOR_CYAN,
                        _("  Built %s  "), __DATE__);
                screen_put_printf_yx(info_top + 7, info_left + 5,
                        Q_A_BOLD, (Q_COLOR_WHITE << 3) | Q_COLOR_CYAN,
                        "                     ", __DATE__);

                /* Put up the remote system info */
                if (q_status.online == Q_TRUE) {
                        int box_top = info_top + 10;
                        int box_left = info_left + 4;
                        int box_width;
                        int box_height = 8;
                        char * box_title = _(" Current Connection ");
                        box_width = strlen(box_title) + 4;
                        if (box_width < 40) {
                                box_width = 64;
                        }

#ifndef Q_NO_SERIAL
                        if (q_status.dial_method != Q_DIAL_METHOD_MODEM) {
                                box_height++;
                        }
#else
                        box_height++;
#endif /* Q_NO_SERIAL */

                        screen_draw_box(box_left, box_top, box_left + box_width, box_top + box_height);
                        screen_put_color_str_yx(box_top, box_left + (box_width - strlen(box_title))/2,
                                box_title, Q_COLOR_WINDOW_BORDER);
                        /* System name */
                        screen_put_color_str_yx(box_top + 1, box_left + 2,
                                _("System"), Q_COLOR_MENU_TEXT);
                        screen_put_color_wcs_yx(box_top + 1, box_left + 14,
                                q_status.remote_phonebook_name, Q_COLOR_MENU_COMMAND);

                        /* System address / port */
#ifndef Q_NO_SERIAL
                        if (q_status.dial_method == Q_DIAL_METHOD_MODEM) {
                                screen_put_color_str_yx(box_top + 2, box_left + 2,
                                        _("Number"), Q_COLOR_MENU_TEXT);
                                screen_put_color_str_yx(box_top + 2, box_left + 14,
                                        q_status.remote_address, Q_COLOR_MENU_COMMAND);
                        } else {
#endif /* Q_NO_SERIAL */
                                /* Hostname */
                                screen_put_color_str_yx(box_top + 2, box_left + 2,
                                        _("Hostname"), Q_COLOR_MENU_TEXT);
                                screen_put_color_str_yx(box_top + 2, box_left + 14,
                                        q_status.remote_address, Q_COLOR_MENU_COMMAND);

                                screen_put_color_str_yx(box_top + 3, box_left + 2,
                                        _("IP Address"), Q_COLOR_MENU_TEXT);
                                if (net_is_connected() == Q_TRUE) {
                                        screen_put_color_str_yx(box_top + 3, box_left + 14,
                                                net_ip_address(), Q_COLOR_MENU_COMMAND);
                                } else {
                                        screen_put_color_str_yx(box_top + 3, box_left + 14,
                                                q_status.remote_address, Q_COLOR_MENU_COMMAND);
                                }
                                screen_put_color_str_yx(box_top + 4, box_left + 2,
                                        _("IP Port"), Q_COLOR_MENU_TEXT);
                                screen_put_color_str_yx(box_top + 4, box_left + 14,
                                        q_status.remote_port, Q_COLOR_MENU_COMMAND);
#ifndef Q_NO_SERIAL
                        }
#endif /* Q_NO_SERIAL */

#ifdef Q_LIBSSH2
                        if ((q_status.dial_method == Q_DIAL_METHOD_SSH) &&
                                (net_is_connected() == Q_TRUE)
                        ) {
                                /* Display the server key fingerprint */
                                screen_put_color_str_yx(box_top + 6, box_left + 2,
                                        _("Server Key"), Q_COLOR_MENU_TEXT);
                                screen_put_color_str_yx(box_top + 6, box_left + 14,
                                        ssh_server_key_str(), Q_COLOR_MENU_COMMAND);

                        }
#endif /* Q_LIBSSH2 */

                } /* if (q_status.online == Q_TRUE) */

                q_screen_dirty = Q_FALSE;
        }

        if (redeye_pause == 0) {

                /* Do the animation */
                screen_put_char_yx(info_top + 3, info_left + 54 + redeye_screen_x,
                        cp437_chars[0xF0],
                        Q_A_BOLD, (Q_COLOR_BLACK << 3) | Q_COLOR_BLACK);
                screen_put_char_yx(info_top + 4, info_left + 54 + redeye_screen_x,
                        cp437_chars[0xF0],
                        Q_A_BOLD, (Q_COLOR_BLACK << 3) | Q_COLOR_BLACK);

                if (redeye_right == Q_TRUE) {
                        redeye_screen_x++;
                        if (redeye_screen_x == 20) {
                                redeye_right = Q_FALSE;
                                redeye_pause = 10;
                        }
                } else {
                        redeye_screen_x--;
                        if (redeye_screen_x == 0) {
                                redeye_right = Q_TRUE;
                                redeye_pause = 10;
                        }
                }

                screen_put_char_yx(info_top + 3, info_left + 54 + redeye_screen_x,
                        cp437_chars[0xF4],
                        Q_A_BOLD, (Q_COLOR_RED << 3) | Q_COLOR_BLACK);
                screen_put_char_yx(info_top + 4, info_left + 54 + redeye_screen_x,
                        cp437_chars[0xF5],
                        Q_A_BOLD, (Q_COLOR_RED << 3) | Q_COLOR_BLACK);

                screen_flush();
        } else {
                redeye_pause--;
        }

} /* ---------------------------------------------------------------------- */
