/*
 * protocols.h
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

#ifndef __PROTOCOLS_H__
#define __PROTOCOLS_H__

/* Includes --------------------------------------------------------------- */

#include <time.h>
#include <sys/types.h>
#include "forms.h"

/* Defines ---------------------------------------------------------------- */

/*
 * The maximum size a single block might need on the wire.  We use this to
 * increase the throughput of uploads.
 */
#define Q_PROTOCOL_MAX_BLOCK_SIZE       2048

/* The supported download protocols. */
typedef enum Q_PROTOCOLS {
        Q_PROTOCOL_ASCII,               /* ASCII */
        Q_PROTOCOL_KERMIT,              /* Kermit */
        Q_PROTOCOL_XMODEM,              /* Xmodem */
        Q_PROTOCOL_XMODEM_CRC,          /* Xmodem CRC */
        Q_PROTOCOL_XMODEM_RELAXED,      /* Xmodem Relaxed */
        Q_PROTOCOL_XMODEM_1K,           /* Xmodem-1K */
        Q_PROTOCOL_YMODEM,              /* Ymodem Batch */
        Q_PROTOCOL_ZMODEM,              /* Zmodem Batch */
        Q_PROTOCOL_XMODEM_1K_G,         /* Xmodem-1K/G */
        Q_PROTOCOL_YMODEM_G             /* Ymodem/G Batch */

} Q_PROTOCOL;

/* The transfer state, used by process_transfer_stats() */
typedef enum Q_TRANSFER_STATES {
        Q_TRANSFER_STATE_INIT,          /* Initial state */
        Q_TRANSFER_STATE_FILE_INFO,     /* Waiting for file information */
        Q_TRANSFER_STATE_TRANSFER,      /* Transferrring a file */

        Q_TRANSFER_STATE_FILE_DONE,     /*
                                         * Completed with a file, maybe
                                         * waiting for another file info
                                         */

        Q_TRANSFER_STATE_ABORT,         /* Transfer aborted, displaying completion screen */
        Q_TRANSFER_STATE_END            /* Displaying completion screen */
} Q_TRANSFER_STATE;

struct q_transfer_stats_struct {
        Q_TRANSFER_STATE state;
        Q_PROTOCOL protocol;
        char * protocol_name;
        char * filename;
        char * pathname;
        char * last_message;
        unsigned long bytes_total;
        unsigned long bytes_transfer;
        unsigned long blocks;
        unsigned long block_size;
        unsigned long blocks_transfer;
        unsigned long error_count;

        /* Batch counts */
        unsigned long batch_bytes_total;        /* The total bytes to send */
        unsigned long batch_bytes_transfer;     /* The amount of bytes sent so far */

        time_t batch_start_time;
        time_t file_start_time;
        time_t end_time;
};

/*
 * Convenience functions to handle the strings inside q_transfer_stats_struct.
 * To avoid memory leaks I Xstrdup() them in and then Xfree() them out when
 * I switch to Q_STATE_CONSOLE.
 */
extern void set_transfer_stats_protocol_name(const char * new_string);
extern void set_transfer_stats_filename(const char * new_string);
extern void set_transfer_stats_pathname(const char * new_string);
extern void set_transfer_stats_last_message(const char * format, ...);

/* Zmodem initiation code */
#define ZRQINIT_STRING          "\x2A\x2A?\x42\x30\x30\x30\x30\x30\x30\x30\x30\x30\x30"

/*
 * 2A2A             ZPAD
 * 18               ZDLE
 * ^^----- but I saw 01 from lrzsz
 * 42               Format type
 * 3030             ZRQINIT (Zmodem hex 1 byte)
 * 3030303030303030 Flags   (Zmodem hex 4 bytes)
 * ????????         CRC check bytes (Zmodem hex 2 bytes)
 * <CR><LF><XON>    End of packet
 *
 * In ASCII this looks like:
 * "**<CAN>B0000000000????<CR><LF><XON>"
 */

/* Kermit initiation code */
#define KERMIT_AUTOSTART_STRING         "\x01?\x20\x53???\x40\x2d\x23"

/*
 * 01               MARK
 * ??               LEN
 * 20               SEQ
 * 'S'              TYPE
 * ??               MAXL
 * ??               TIME
 * ??               NPAD
 * 00               PADC
 * 0d               EOL
 * 23               QCTL
 * ??               QBIN
 */

/* Globals ---------------------------------------------------------------- */

/* Download location file or directory, stored in protocols.c */
extern char * q_download_location;

/* Transfer statistics, stored in protocols.c, access by console.c for Zmodem autostart */
extern struct q_transfer_stats_struct q_transfer_stats;

/* Functions -------------------------------------------------------------- */

/* Called to end a file transfer. */
extern void stop_file_transfer(const Q_TRANSFER_STATE new_state);

/* Called by console.c for Zmodem autostart. */
extern void start_file_transfer();

/* Called by host mode to perform an upload ("download" to the remote side) */
extern void set_batch_upload(struct file_info * upload);

extern void protocol_menu_keyboard_handler(const int keystroke, const int flags);
extern void protocol_menu_refresh();

extern void protocol_pathdialog_keyboard_handler(const int keystroke, const int flags);
extern void protocol_pathdialog_refresh();

extern void protocol_transfer_keyboard_handler(const int keystroke, const int flags);
extern void protocol_transfer_refresh();

/* Analogous to console_process_incoming_data() */
extern void protocol_process_data(unsigned char * input, const int input_n, int * remaining, unsigned char * output, int * output_n, const int output_max);

#endif /* __PROTOCOLS_H__ */
