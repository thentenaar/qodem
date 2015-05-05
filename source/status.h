/*
 * status.h
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

#ifndef __STATUS_H__
#define __STATUS_H__

/* Includes --------------------------------------------------------------- */

#include <time.h>                       /* time_t */
#include <stdio.h>                      /* FILE */
#include "emulation.h"                  /* Q_EMULATION */
#include "codepage.h"                   /* Q_CODEPAGE */
#include "phonebook.h"                  /* Q_DIAL_METHOD */
#include "common.h"

/* Defines ---------------------------------------------------------------- */

/*
 * Available capture types.
 */
typedef enum Q_CAPTURE_TYPES {
        Q_CAPTURE_TYPE_NORMAL,          /* normal */
        Q_CAPTURE_TYPE_RAW,             /* raw */
        Q_CAPTURE_TYPE_HTML,            /* html */
        Q_CAPTURE_TYPE_ASK              /* ask - prompt every time */
} Q_CAPTURE_TYPE;

/*
 * Available doorway modes
 */
typedef enum Q_DOORWAY_MODE {
        Q_DOORWAY_MODE_OFF,             /* No doorway */
        Q_DOORWAY_MODE_MIXED,           /* Mixed mode */
        Q_DOORWAY_MODE_FULL             /* Full doorway */
} Q_DOORWAY_MODE;

#ifndef Q_NO_SERIAL
#define Q_SERIAL_OPEN (q_status.serial_open == Q_TRUE)
#else
#define Q_SERIAL_OPEN (Q_FALSE)
#endif /* Q_NO_SERIAL */

struct q_status_struct {
        Q_EMULATION emulation;          /* Current emulation mode */

        Q_CODEPAGE codepage;            /* Current codepage */

        Q_DOORWAY_MODE doorway_mode;    /* Doorway mode */

        Q_BOOL online;                  /* true  = online
                                           false = offline */

        Q_BOOL hanging_up;              /* true  = user is requesting hangup
                                           false = normal operation */

#ifndef Q_NO_SERIAL
        Q_BOOL serial_open;             /* true  = serial port is open
                                           false = serial port is closed */
#endif /* Q_NO_SERIAL */

        Q_BOOL split_screen;            /* true  = split screen mode
                                           false = normal screen mode */

        time_t connect_time;            /* the moment online became true */

        Q_BOOL beeps;                   /* true  = beeps on
                                           false = beeps off */

        Q_BOOL sound;                   /* true  = sound on
                                           false = sound off */

        Q_BOOL ansi_music;              /* true  = ANSI music on
                                           false = ANSI music off */

        Q_BOOL capture;                 /* true  = capture on
                                           false = capture off */
        FILE * capture_file;            /* Capture file */

        Q_CAPTURE_TYPE capture_type;    /* Capture type (normal/raw/html/ask) */

        Q_CAPTURE_TYPE screen_dump_type;        /* Screen dump type (normal/html/ask) */

        Q_CAPTURE_TYPE scrollback_save_type;    /* Scrollback save type (normal/html/ask) */

        time_t capture_flush_time;      /* When we last fflush()'d the capture file */

        int capture_x;                  /* The current column number for the capture file */

        Q_BOOL logging;                 /* true  = log enabled
                                           false = log disabled */
        FILE * logging_file;            /* Logging file */

        unsigned int scrollback_lines;  /* # of lines in scrollback buffer */

        int cursor_x, cursor_y;         /* Current cursor position */

        Q_BOOL strip_8th_bit;           /* true  = strip high bit
                                           false = no strip */

        Q_BOOL full_duplex;             /* true  = full duplex
                                           false = half duplex (local echo) */

        Q_BOOL line_feed_on_cr;         /* true  = add a linefeed for every CR
                                           false = no extra linefeeds */

        Q_BOOL guard_hangup;            /* true  = prompt before permitting Alt-H hangup
                                           false = no prompt, immediately hangup */

        Q_BOOL scrollback_enabled;      /* true  = lines recorded to scrollback
                                           false = no new lines in scrollback */

        Q_BOOL status_visible;          /* true  = status line(s) is visible
                                           false = status line(s) is not visible */

        Q_BOOL status_line_info;        /* true  = alternate info line (address, current time)
                                           false = regular info line (online, flags, connect time) */

        Q_BOOL hard_backspace;          /* true  = Backspace is ^H
                                           false = Backspace is DEL */

        Q_BOOL line_wrap;               /* true  = Wrap lines at right-most column
                                           false = Do not wrap */

        Q_BOOL display_null;            /* true  = Display NULL as ' '
                                           false = Strip NULL from input */

        Q_BOOL zmodem_autostart;        /* true  = Autostart Zmodem when ZRQINIT is seen
                                           false = Do nothing when ZRQINIT is seen */

        Q_BOOL zmodem_escape_ctrl;      /* true  = Escape control characters in Zmodem
                                           false = Do not escape control characters in Zmodem */

        Q_BOOL zmodem_zchallenge;       /* true  = Issue ZCHALLENGE
                                           false = Do not issue ZCHALLENGE */

        Q_BOOL kermit_autostart;        /* true  = Autostart Kermit when SEND-INIT is seen
                                           false = Do nothing when SEND-INIT is seen */

        Q_BOOL kermit_robust_filename;  /* true  = squish filenames to "common form"
                                           false = keep literal filenames */

        Q_BOOL kermit_streaming;        /* true  = use streaming (don't send NAKs)
                                           false = don't use streaming */

        Q_BOOL kermit_uploads_force_binary;     /* true  = force binary uploads
                                                   false = text uploads on text files */

        Q_BOOL kermit_downloads_convert_text;   /* true  = convert CRLF -> LF on text files
                                                   false = treat text files like binary */

        Q_BOOL kermit_resend;           /* true  = Kermit always uses RESEND on uploads
                                           false = Kermit uses SEND on uploads */

        Q_BOOL kermit_long_packets;     /* true  = use long packets
                                           false = use short packets only */

        Q_BOOL external_telnet;         /* true  = use external telnet
                                           false = use netclient.c code */

        Q_BOOL external_rlogin;         /* true  = use external rlogin
                                           false = use netclient.c code */

        Q_BOOL external_ssh;            /* true  = use external ssh
                                           false = use netclient.c code */

        Q_BOOL xterm_double;            /* true  = use double-width chars under xterm
                                           false = use spaces */

        Q_BOOL vt100_color;             /* true  = support color ANSI codes
                                           false = bare-bones VT10x */

        Q_BOOL vt52_color;              /* true  = support color ANSI codes
                                           false = bare-bones VT52 */

        Q_BOOL avatar_color;            /* true  = support color ANSI codes
                                           false = bare-bones Avatar */


        /* VT100 modes */
        Q_BOOL origin_mode;             /* true  = cursor position is relative to
                                                   scrolling region
                                           false = cursor position is relative to
                                                   entire screen */

        Q_BOOL insert_mode;             /* true  = New printed characters shift row to right
                                           false = New printed characters overwrite row */

        int scroll_region_top;          /* Top margin of the scrolling region */

        int scroll_region_bottom;       /* Bottom margin of the scrolling region */

        Q_BOOL reverse_video;           /* true  = Video attributes are reversed
                                         false = Video attributes are normal */

        /* DECLL leds */
        Q_BOOL led_1;
        Q_BOOL led_2;
        Q_BOOL led_3;
        Q_BOOL led_4;

        /* LINUX/VT220 modes */
        Q_BOOL visible_cursor;          /* true  = Cursor is visible in terminal mode
                                           false = Cursor is invisible in terminal mode */

        /* VT52 modes */
        Q_BOOL hold_screen_mode;        /* true  = perform hold-screen logic on the bottom line
                                           false = normal */

        /* ANSI modes */
        Q_BOOL ansi_animate;            /* true  = flush screen to show ANSI animation ASAP
                                           false = buffer as normal, even if ANSI animates oddly */

        /* ANSI, Avatar, TTY */
        Q_BOOL assume_80_columns;       /* true  = wrap at column 80
                                           false = wrap at the right margin */

        /* The amount of time to wait before disconnecting. */
        int idle_timeout;

        /* Session variables */
        wchar_t * current_username;
        wchar_t * current_password;
        char * remote_address;
        char * remote_port;
        wchar_t * remote_phonebook_name;
        Q_DIAL_METHOD dial_method;

        /* Miscellaneous flags */
        Q_BOOL exit_on_disconnect;      /* true  = exit on next disconnect
                                           false = normal */


        /* Quicklearn */
        Q_BOOL quicklearn;              /* true  = in quicklearn mode
                                           false = normal */

};

/* Globals ---------------------------------------------------------------- */

/* Global status struct, stored in qodem.c */
extern struct q_status_struct q_status;

/* Functions -------------------------------------------------------------- */

#endif /* __STATUS_H__ */
