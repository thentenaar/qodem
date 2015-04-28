/*
 * xmodem.c
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
 * How Xmodem send works:
 *
 * xmodem_start()  --> state = INIT
 *
 * xmodem()
 *
 *  STATE == INIT:
 *   Check for ACK/'C'/'G'
 *      Got it     --> state = BLOCK
 *   Check for timeout
 *      Timeout    --> state = PURGE_INPUT
 *
 *  STATE == BLOCK:
 *   Check for timeout
 *      Timeout    --> state = PURGE_INPUT
 *   Send a block
 *      Last block?
 *         Yes     --> state = LAST_BLOCK
 *         No      --> state = BLOCK
 *
 *  STATE == LAST_BLOCK:
 *   Check for timeout
 *      Timeout    --> state = PURGE_INPUT
 *   Check for ACK
 *      Got it     --> state = EOT_ACK
 *
 *  STATE == EOT_ACK:
 *   Check for timeout
 *      Timeout    --> state = PURGE_INPUT
 *   Check for ACK
 *      Got it     --> state = COMPLETE
 *
 * xmodem_stop()
 *
 *
 *
 * How Xmodem receive works:
 *
 * xmodem_start()  --> state = INIT
 *
 * xmodem()
 *
 *  STATE == INIT:
 *   Normal: Send ACK        --> state = BLOCK
 *   Enhanced: Send 'C'/'G'  --> state = FIRST_BLOCK
 *
 *  STATE == FIRST_BLOCK:
 *   Got data?
 *      Yes            --> state = BLOCK
 *   Timeout?
 *      Yes            --> downgrade to X_NORMAL, state = BLOCK
 *
 *  STATE == BLOCK:
 *   Check for timeout
 *      Timeout        --> state = PURGE_INPUT
 *   Got a block?
 *      Yes
 *        verify_block()
 *          true       --> save, send ACK
 *          false      --> send NAK
 *      No
 *   EOT?
 *      Yes            --> state = COMPLETE
 *
 * xmodem_stop()
 *
 */

#include "qcurses.h"
#include "common.h"

#include <errno.h>
#include <assert.h>
#ifdef __BORLANDC__
#include <io.h>
#define ftruncate chsize
#else
#include <libgen.h>
#include <unistd.h>
#endif
#include <string.h>
#include <stdlib.h>
#include <utime.h>
#include "qodem.h"
#include "music.h"
#include "protocols.h"
#include "xmodem.h"

/* #define DEBUG_XMODEM 1 */
#undef DEBUG_XMODEM
#ifdef DEBUG_XMODEM
static FILE * DEBUG_FILE_HANDLE = NULL;
#endif

/* Filename to send or receive */
static char * filename = NULL;

/* File to send or receive */
static FILE * file = NULL;

/* An Xmodem block can have up to 1024 data bytes plus:
 * 1 byte HEADER
 * 1 byte block number
 * 1 byte inverted block number
 * 2 bytes CRC
 */
#define XMODEM_MAX_BLOCK_SIZE 1024 + 5

/* Current block to send or receive */
static unsigned char current_block[XMODEM_MAX_BLOCK_SIZE];

/* Size of current_block */
static int current_block_n = 0;

/* Sequence # of current_block.  Start with 1. */
static unsigned char current_block_sequence_i = 1;

/* Actual block # of current_block.  Start with 1. (Sequence # is what
 * is transmitted in the Xmodem block, block # is what we surface to
 * the user on the progress dialog.)
 */
static unsigned int current_block_number = 1;

/* The first byte to start Xmodem for this flavor. Default for X_NORMAL. */
static unsigned char first_byte = C_NAK;

/* Whether sending or receiving */
static Q_BOOL sending = Q_FALSE;

/* The state of the protocol */
typedef enum {
        INIT,           /* Before the first byte is sent */
        PURGE_INPUT,    /* Before a regular NAK is sent */
        FIRST_BLOCK,    /* Receiver: waiting for first block after 'C' or 'G' first NAK */
        BLOCK,          /* Collecting data for block */
        LAST_BLOCK,     /* Sender: waiting for ACK on final block before sending EOT */
        EOT_ACK,        /* Sender: waiting for final ACK to EOT */
        COMPLETE,       /* Transfer complete */
        ABORT,          /* Transfer was aborted due to excessive timeouts/errors */

        YMODEM_BLOCK0,  /* Receiver: looking for block 0 (file information)
                           Sender: got start, need to send block 0 */
        YMODEM_BLOCK0_ACK1,     /* Sender: sent block 0, waiting for ACK */
        YMODEM_BLOCK0_ACK2      /* Sender: got block 0 ACK, waiting for 'C'/'G' */

} STATE;

/* Transfer state */
static STATE state;

/* For PURGE_INPUT, state we came from */
static STATE prior_state;

/* Timeout normally lasts 10 seconds */
static int timeout_length = 10;

/* The beginning time for the most recent timeout cycle */
static time_t timeout_begin;

/* Total number of timeouts before aborting is 10 */
static int timeout_max = 10;

/* Total number of timeouts so far */
static int timeout_count;

/* Total number of errors before aborting is 15 */
static int errors_max = 15;

/* The flavor of Xmodem to use */
static XMODEM_FLAVOR flavor;

/* YMODEM ONLY ----------------------------------------------------- */

/* The list of files to upload */
static struct file_info * upload_file_list;

/* The current entry in upload_file_list being sent */
static int upload_file_list_i;

/* The path to download to.  Note download_path is Xstrdup'd TWICE:
 * once HERE and once more on the progress dialog.  The q_program_state
 * transition to Q_STATE_CONSOLE is what Xfree's the copy in the progress
 * dialog.  This copy is Xfree'd in ymodem_stop().
 */
static char * download_path = NULL;

/* The modification time of the current downloading file */
static time_t download_file_modtime;

/* Whether or not Ymodem block 0 has been seen */
static Q_BOOL block0_has_been_seen = Q_FALSE;

/* YMODEM ONLY ----------------------------------------------------- */

/*
 * Clear current_block
 */
static void clear_block() {
#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "clear_block()\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
        memset(current_block, 0, sizeof(current_block));
        current_block_n = 0;
} /* ---------------------------------------------------------------------- */

/*
 * Reset timer
 */
static void reset_timer() {
#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "reset_timer()\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

        /* Reset timer */
        time(&timeout_begin);
} /* ---------------------------------------------------------------------- */

/*
 * Check for a timeout.  Pass the output buffer because we
 * might send a CAN if timeout_max is exceeded.
 */
static Q_BOOL check_timeout(unsigned char * output, int * output_n) {
#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "check_timeout()\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
        time_t now;
        time(&now);

        /* Let the receive have one freebie */
        if ((sending == Q_TRUE) && (now - timeout_begin < 2 * timeout_length)) {
                return Q_FALSE;
        }

        if (now - timeout_begin >= timeout_length) {

                /* Timeout */
                timeout_count++;
#ifdef DEBUG_XMODEM
                fprintf(DEBUG_FILE_HANDLE, "XMODEM: Timeout #%d\n", timeout_count);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

                q_transfer_stats.error_count++;
                if (timeout_count >= timeout_max) {
                        /* ABORT */
                        set_transfer_stats_last_message(_("TOO MANY TIMEOUTS, TRANSFER CANCELLED"));
                        if (sending == Q_FALSE) {
                                output[0] = C_CAN;
                                *output_n = 1;
                        }
                        stop_file_transfer(Q_TRANSFER_STATE_ABORT);
                        state = ABORT;
                } else {
                        /* Timeout */
                        set_transfer_stats_last_message(_("TIMEOUT"));
                        prior_state = state;
                        state = PURGE_INPUT;
                }

                /* Reset timeout */
                reset_timer();
                return Q_TRUE;
        }

        return Q_FALSE;
} /* ---------------------------------------------------------------------- */

/*
 * Statistics: a block was sent out or received successfuly
 */
static void stats_increment_blocks(const unsigned char * input) {
        int new_block_size;
        int old_block_size;
        int bytes_left;

        old_block_size = q_transfer_stats.block_size;
        new_block_size = old_block_size;

        /*
         * The block increment is in its own check because Xmodem-1k
         * and 1K/G still don't get the full file size.
         */
        if ((sending == Q_FALSE) && (flavor != Y_NORMAL) && (flavor != Y_G)) {
                q_transfer_stats.blocks++;
        }

        q_transfer_stats.blocks_transfer++;
        if (((flavor == X_1K) || (flavor == X_1K_G) || (flavor == Y_NORMAL) || (flavor == Y_G)) && (input[0] == C_STX) && (sending == Q_FALSE)) {
                /*
                 * Receiver case: we got a 1024-byte block
                 */
                q_transfer_stats.bytes_transfer += 1024;
                if ((sending == Q_FALSE) && (flavor != Y_NORMAL) && (flavor != Y_G)) {
                        q_transfer_stats.bytes_total += 1024;
                }
                new_block_size = 1024;

        } else if ((current_block_n >= 1024) && (sending == Q_TRUE)) {
                /*
                 * Sender case: we sent a 1024-byte block
                 */
                q_transfer_stats.bytes_transfer += 1024;
                new_block_size = 1024;
        } else {
                /*
                 * Sender and receiver case: 128-byte block
                 */
                q_transfer_stats.bytes_transfer += 128;

                if ((sending == Q_FALSE) && (flavor != Y_NORMAL) && (flavor != Y_G)) {
                        /*
                         * Xmodem receive only: increment the number of bytes
                         * to report for the file because Xmodem doesn't send
                         * the file size.
                         */
                        q_transfer_stats.bytes_total += 128;
                }
                new_block_size = 128;
        }

        /*
         * Special check: If we're receiving via Ymodem, and we just incremented
         * q_transfer_stats.bytes_transfer by a full block size and went past
         * the known file size, then trim it back to the actual file size.
         */
        if ((sending == Q_FALSE) && ((flavor == Y_NORMAL) || (flavor == Y_G)) && (q_transfer_stats.bytes_transfer > q_transfer_stats.bytes_total)) {
                q_transfer_stats.bytes_transfer = q_transfer_stats.bytes_total;
        } else {
                /*
                 * Special check:  if we just changed block size, re-compute the
                 * number of blocks remaining based on the bytes left.
                 */
                if (new_block_size != old_block_size) {
                        if ((sending == Q_TRUE) || (flavor == Y_NORMAL) || (flavor == Y_G)) {
                                bytes_left = q_transfer_stats.bytes_total - q_transfer_stats.bytes_transfer;
                                if (bytes_left > 0) {
                                        q_transfer_stats.blocks = bytes_left / new_block_size;
                                        if ((bytes_left % new_block_size) > 0) {
                                                q_transfer_stats.blocks++;
                                        }
                                        q_transfer_stats.blocks += q_transfer_stats.blocks_transfer;
                                        q_transfer_stats.block_size = new_block_size;
                                }
                        }
                }
        }

        /* Update the progress dialog */
        q_screen_dirty = Q_TRUE;
} /* ---------------------------------------------------------------------- */

/*
 * Downgrade to vanilla Xmodem
 */
static void downgrade_to_vanilla_xmodem() {
        set_transfer_stats_protocol_name(_("Xmodem"));
        q_transfer_stats.block_size = 128;
        if ((flavor == X_1K) || (flavor == X_1K_G)) {
                q_transfer_stats.blocks = q_transfer_stats.bytes_total / 128;
                if ((q_transfer_stats.bytes_total % 128) > 0) {
                        q_transfer_stats.blocks++;
                }
        }
        flavor = X_NORMAL;
} /* ---------------------------------------------------------------------- */

/*
 * Statistics: an error was encountered
 */
static void stats_increment_errors(const char * format, ...) {
        char outbuf[DIALOG_MESSAGE_SIZE];
        va_list arglist;
        memset(outbuf, 0, sizeof(outbuf));
        va_start(arglist, format);
        vsprintf((char *)(outbuf+strlen(outbuf)), format, arglist);
        va_end(arglist);
        set_transfer_stats_last_message(outbuf);

        q_transfer_stats.error_count++;

        if (q_transfer_stats.error_count >= errors_max) {
                /* ABORT */
                set_transfer_stats_last_message(_("TRANSFER_MAX_ERRORS"));
                stop_file_transfer(Q_TRANSFER_STATE_ABORT);
                state = ABORT;
        }
} /* ---------------------------------------------------------------------- */

/*
 * Statistics: a file is complete
 */
static void stats_file_complete_ok() {
        set_transfer_stats_last_message(_("SUCCESS"));
        q_transfer_stats.bytes_transfer = q_transfer_stats.bytes_total;
        state = COMPLETE;
        stop_file_transfer(Q_TRANSFER_STATE_END);
        time(&q_transfer_stats.end_time);

        /* Play music */
        if (sending == Q_TRUE) {
                play_sequence(Q_MUSIC_UPLOAD);
        } else {
                play_sequence(Q_MUSIC_DOWNLOAD);
        }
} /* ---------------------------------------------------------------------- */

/*
 * Statistics: transfer was cancelled
 */
static void stats_file_cancelled(const char * format, ...) {
        char outbuf[DIALOG_MESSAGE_SIZE];
        va_list arglist;
        memset(outbuf, 0, sizeof(outbuf));
        va_start(arglist, format);
        vsprintf((char *)(outbuf+strlen(outbuf)), format, arglist);
        va_end(arglist);
        set_transfer_stats_last_message(outbuf);

        stop_file_transfer(Q_TRANSFER_STATE_ABORT);
        state = ABORT;
} /* ---------------------------------------------------------------------- */

/*
 * Statistics: reset for a new file
 */
static void stats_new_file(const char * filename, const char * pathname, const int filesize, const int blocks) {
        /* Clear per-file statistics */
        q_transfer_stats.batch_bytes_transfer += q_transfer_stats.bytes_transfer;
        q_transfer_stats.blocks_transfer = 0;
        q_transfer_stats.bytes_transfer = 0;
        q_transfer_stats.error_count = 0;
        set_transfer_stats_last_message("");
        set_transfer_stats_filename(filename);
        set_transfer_stats_pathname(pathname);
        q_transfer_stats.bytes_total = filesize;
        q_transfer_stats.blocks = blocks;

        /* Reset block size */
        if ((flavor == X_1K) || (flavor == X_1K_G) || (flavor == Y_NORMAL) || (flavor == Y_G)) {
                q_transfer_stats.block_size = 1024;
        } else {
                q_transfer_stats.block_size = 128;
        }

        q_transfer_stats.state = Q_TRANSFER_STATE_TRANSFER;
        time(&q_transfer_stats.file_start_time);

        /* Log it */
        if (sending == Q_TRUE) {
                qlog(_("UPLOAD: sending file %s/%s, %d bytes\n"), pathname, filename, filesize);
        } else {
                qlog(_("DOWNLOAD: receiving file %s/%s, %d bytes\n"), pathname, filename, filesize);
        }

} /* ---------------------------------------------------------------------- */

/*
 * Initialize a new file
 */
static Q_BOOL setup_for_next_file() {
        char * basename_arg;
        char * dirname_arg;
        int blocks;

        /* Reset our dynamic variables */
        if (file != NULL) {
                fclose(file);
        }
        file = NULL;
        if (filename != NULL) {
                Xfree(filename, __FILE__, __LINE__);
        }
        filename = NULL;

        if (upload_file_list[upload_file_list_i].name == NULL) {
                /* Special case: the terminator block */
#ifdef DEBUG_XMODEM
                fprintf(DEBUG_FILE_HANDLE, "YMODEM: Terminator block (name='%s')\n", upload_file_list[upload_file_list_i].name);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

                /* Let's keep all the information the same, just increase the total bytes */
                q_transfer_stats.batch_bytes_transfer += q_transfer_stats.bytes_transfer;
                return Q_TRUE;
        }

        /* Open the file */
        if ((file = fopen(upload_file_list[upload_file_list_i].name, "rb")) == NULL) {
#ifdef DEBUG_XMODEM
                fprintf(DEBUG_FILE_HANDLE, "Unable to open file %s, returning false\n", upload_file_list[upload_file_list_i].name);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                return Q_FALSE;
        }

        /* Initialize timer for the first timeout */
        reset_timer();

        /* Note that basename and dirname modify the arguments */
        basename_arg = Xstrdup(upload_file_list[upload_file_list_i].name, __FILE__, __LINE__);
        dirname_arg = Xstrdup(upload_file_list[upload_file_list_i].name, __FILE__, __LINE__);

        if (filename != NULL) {
                Xfree(filename, __FILE__, __LINE__);
        }
        filename = Xstrdup(basename(basename_arg), __FILE__, __LINE__);

        /* Determine total blocks */
        blocks = upload_file_list[upload_file_list_i].fstats.st_size / 1024;
        if ((upload_file_list[upload_file_list_i].fstats.st_size % 1024) > 0) {
                blocks++;
        }

        /* Update the stats */
        stats_new_file(Xstrdup(filename, __FILE__, __LINE__),
                       Xstrdup(dirname(dirname_arg), __FILE__, __LINE__),
                       upload_file_list[upload_file_list_i].fstats.st_size,
                       blocks);

        /* Free the copies passed to basename() and dirname() */
        Xfree(basename_arg, __FILE__, __LINE__);
        Xfree(dirname_arg, __FILE__, __LINE__);

#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "Set up for %s, returning true...\n", upload_file_list[upload_file_list_i].name);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
        return Q_TRUE;
} /* ---------------------------------------------------------------------- */

/*
 * KAL - This CRC routine was taken verbatim from XYMODEM.DOC.
 *
 * This function calculates the CRC used by the XMODEM/CRC Protocol
 * The first argument is a pointer to the message block.
 * The second argument is the number of bytes in the message block.
 * The function returns an integer which contains the CRC.
 * The low order 16 bits are the coefficients of the CRC.
 */
static int calcrc(unsigned char * ptr, int count)
{
    int crc, i;

    crc = 0;
    while (--count >= 0) {
        crc = crc ^ ((int)*ptr++ << 8);
        for (i = 0; i < 8; ++i)
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc = crc << 1;
        }
    return (crc & 0xFFFF);
}

/*
 * Read from file and construct a block in current_block
 */
static void ymodem_construct_block_0() {
        int i;
        int crc;
        char local_buffer[32];

#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "YMODEM: ymodem_construct_block0()\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

        assert((flavor == Y_NORMAL) || (flavor == Y_G));

        /* Clear out current block */
        memset(current_block, 0, sizeof(current_block));
        current_block_n = 3;

        if (upload_file_list[upload_file_list_i].name != NULL) {
                /* Filename */
                for (i=0; i<strlen(filename); i++) {
                        current_block[current_block_n] = filename[i];
                        current_block_n++;
                }
                /* Push past null terminator */
                current_block_n++;

                /* Length */
                snprintf(local_buffer, sizeof(local_buffer), "%lu",
                        upload_file_list[upload_file_list_i].fstats.st_size);
                for (i=0; i<strlen(local_buffer); i++) {
                        current_block[current_block_n] = local_buffer[i];
                        current_block_n++;
                }
                /* Push past ' ' terminator */
                current_block[current_block_n] = ' ';
                current_block_n++;

                /* Modification date */
                snprintf(local_buffer, sizeof(local_buffer), "%lo",
                        upload_file_list[upload_file_list_i].fstats.st_mtime);

                /*
                 * Grr.  I need to learn to read.
                 */
                for (i=0; i<strlen(local_buffer); i++) {
                        current_block[current_block_n] = local_buffer[i];
                        current_block_n++;
                }

                /* Push past null terminator */
                current_block_n++;
        }

        /* CRC */
        if (current_block_n > 128 + 5 - 2) {
                /* Long block */
                crc = calcrc(current_block + 3, 1024);
                current_block[1023 + 4] = (crc >> 8) & 0xFF;
                current_block[1023 + 5] = crc & 0xFF;
                current_block_n = 1024 + 5;
                /* STX */
                current_block[0] = C_STX;
        } else {
                crc = calcrc(current_block + 3, 128);
                current_block[127 + 4] = (crc >> 8) & 0xFF;
                current_block[127 + 5] = crc & 0xFF;
                current_block_n = 128 + 5;
                /* SOH */
                current_block[0] = C_SOH;
        }

        /* Sequence number */
        current_block[1] = current_block_sequence_i;
        current_block[2] = (0xFF - current_block_sequence_i) & 0xFF;
        current_block_sequence_i++;

#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "YMODEM: ymodem_construct_block0() current_block_n = %d\nBytes: ", current_block_n);
        for (i = 0; i < current_block_n; i++) {
                fprintf(DEBUG_FILE_HANDLE, "%02x ", (current_block[i] & 0xFF));
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
} /* ---------------------------------------------------------------------- */

/*
 * Decode the Ymodem block 0
 */
static Q_BOOL ymodem_decode_block_0() {
        int i;
        int crc;
        char full_filename[FILENAME_SIZE];
        char local_buffer[FILENAME_SIZE];
        int length;
        int blocks;

        assert((flavor == Y_NORMAL) || (flavor == Y_G));

#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "YMODEM: ymodem_decode_block0() checking block size, current_block_n = %d\nInput bytes: ", current_block_n);
        for (i = 0; i < current_block_n; i++) {
                fprintf(DEBUG_FILE_HANDLE, "%02x ", (current_block[i] & 0xFF));
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

        /* Verify the sequence # and CRC first */
        if ((current_block_n != 1024 + 5) && (current_block_n != 128 + 5)) {
                stats_increment_errors(_("SHORT/LONG BLOCK #%d"), current_block_number);
                return Q_FALSE;
        }

#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "YMODEM: ymodem_decode_block_0() checking byte 0\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

        /* Byte 0: SOH or STX */
        if ((current_block[0] != C_SOH) && (current_block[0] != C_STX)) {
                stats_increment_errors(_("HEADER ERROR IN BLOCK #%d"), current_block_number);
                return Q_FALSE;
        }

#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "YMODEM: ymodem_decode_block_0() checking sequence #\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

        /* Byte 1 and 2: current block counter */
        if (current_block_sequence_i != current_block[1]) {
                stats_increment_errors(_("BAD BLOCK NUMBER IN BLOCK #%d"), current_block_number);
                return Q_FALSE;
        }
        if ((current_block[1] & 0xFF) + (current_block[2] & 0xFF) != 0xFF) {
                stats_increment_errors(_("COMPLIMENT BYTE BAD IN BLOCK #%d"), current_block_number);
                return Q_FALSE;
        }

#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "YMODEM: ymodem_decode_block_0() checking CRC\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

        if (current_block[0] == C_SOH) {
                crc = calcrc(current_block + 3, 128);
                if ((((crc >> 8) & 0xFF) != current_block[127 + 4]) &&
                        ((crc & 0xFF) != (current_block[127 + 5] & 0xFF))) {
                        /* CRC didn't match */
                        stats_increment_errors(_("CRC ERROR IN BLOCK #%d"), current_block_number);
#ifdef DEBUG_XMODEM
                        fprintf(DEBUG_FILE_HANDLE, "YMODEM: ymodem_decode_block_0() crc error: %02x %02x\n", ((crc >> 8) & 0xFF), (crc & 0xFF));
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                        return Q_FALSE;
                }
        } else {
                crc = calcrc(current_block + 3, 1024);
                if ((((crc >> 8) & 0xFF) != (current_block[1023 + 4] & 0xFF)) &&
                        ((crc & 0xFF) != (current_block[1023 + 5] & 0xFF))) {
                        /* CRC didn't match */
                        stats_increment_errors(_("CRC ERROR IN BLOCK #%d"), current_block_number);
#ifdef DEBUG_XMODEM
                        fprintf(DEBUG_FILE_HANDLE, "YMODEM: ymodem_decode_block_0() crc error: %02x %02x\n", ((crc >> 8) & 0xFF), (crc & 0xFF));
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                        return Q_FALSE;
                }
        }

        /* Block is OK, read filename */
        current_block_n = 3;

        /* Filename */
        memset(local_buffer, 0, sizeof(local_buffer));
        for (i = 0; i < sizeof(local_buffer) - 1; i++) {
                local_buffer[i] = current_block[current_block_n];
                current_block_n++;
                if (current_block[current_block_n] == '\0') {
                        break;
                }
        }
        /* Push past null terminator */
        current_block_n++;

        /* Save filename*/
        if (filename != NULL) {
                Xfree(filename, __FILE__, __LINE__);
        }
        filename = Xstrdup(local_buffer, __FILE__, __LINE__);

#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "YMODEM: ymodem_decode_block_0() filename: %s\n", filename);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

        /* Return immediately on the terminator block */
        if (strlen(filename) == 0) {
                return Q_TRUE;
        }

        /* Open file */
        sprintf(full_filename, "%s/%s", download_path, filename);
        if ((file = fopen(full_filename, "w+b")) == NULL) {
#ifdef DEBUG_XMODEM
                fprintf(DEBUG_FILE_HANDLE, "Unable to open file for writing\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                stats_increment_errors(_("FILE OPEN ERROR"));
                return Q_FALSE;
        }

        /* Length */
        memset(local_buffer, 0, sizeof(local_buffer));
        for (i = 0; i < sizeof(local_buffer) - 1; i++) {
                local_buffer[i] = current_block[current_block_n];
                current_block_n++;
                if ((current_block[current_block_n] == '\0') || (current_block[current_block_n] == ' ')) {
                        break;
                }
        }
        /* Push past ' ' terminator */
        current_block[current_block_n] = ' ';
        current_block_n++;

        /* Convert length to integer */
        length = atol(local_buffer);
        blocks = length / 1024;
        if ((length % 1024) > 0) {
                blocks++;
        }

#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "YMODEM: ymodem_decode_block_0() length: %d\n", length);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

        /* File modification time */
        memset(local_buffer, 0, sizeof(local_buffer));
        for (i = 0; i < sizeof(local_buffer) - 1; i++) {
                local_buffer[i] = current_block[current_block_n];
                current_block_n++;
                if ((current_block[current_block_n] == '\0') || (current_block[current_block_n] == ' ')) {
                        break;
                }
        }

        /* Convert mod time to binary */
        sscanf(local_buffer, "%lo", &download_file_modtime);

#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "YMODEM: ymodem_decode_block_0() mod time: %s\n", ctime(&download_file_modtime));
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

        /* Finally, call stats_new_file() to initialize the progress dialog */
        stats_new_file(filename, q_transfer_stats.pathname, length, blocks);

        /* Sequence number */
        current_block_sequence_i++;
        return Q_TRUE;
} /* ---------------------------------------------------------------------- */

/*
 * Read from file and construct a block in current_block
 */
static Q_BOOL construct_block() {
        int i;
        unsigned char checksum;
        int crc;
        int rc;
        char notify_message[DIALOG_MESSAGE_SIZE];

        /* First, verify block size */
#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "XMODEM: construct_block() crc16 = %s length = ",
                ((flavor == X_CRC) || ((flavor != X_RELAXED) && (flavor != X_NORMAL))) ? "true" : "false");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

        if ((flavor == X_RELAXED) || (flavor == X_NORMAL) || (flavor == X_CRC)) {
                /* 128-byte */
#ifdef DEBUG_XMODEM
                fprintf(DEBUG_FILE_HANDLE, "128\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                rc = fread(current_block + 3, 1, 128, file);
                if (ferror(file)) {
                        snprintf(notify_message, sizeof(notify_message), _("Error reading from file \"%s\": %s"), filename, strerror(errno));
                        notify_form(notify_message, 0);
                        /* ABORT transfer */
                        stats_file_cancelled(_("DISK READ ERROR"));
                        return Q_FALSE;
                }
                if (feof(file)) {
#ifdef DEBUG_XMODEM
                        fprintf(DEBUG_FILE_HANDLE, "LAST BLOCK\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                        /* This is the last block */
                        state = LAST_BLOCK;
                }
                for (i=rc+1; i<=128; i++) {
                        /* Pad the remaining space with SUB */
                        current_block[i + 2] = C_SUB;
                }
                if (flavor == X_CRC) {
                        /* CRC */
                        crc = calcrc(current_block + 3, 128);
                        current_block[127 + 4] = (crc >> 8) & 0xFF;
                        current_block[127 + 5] = crc & 0xFF;
                        current_block_n = 128 + 5;
                } else {
                        /* Checksum */
                        checksum = 0;
                        for (i=3; i < 127 + 4; i++) {
                                checksum += current_block[i];
                        }
                        current_block[127 + 4] = checksum;
                        current_block_n = 128 + 4;
                }
                /* SOH */
                current_block[0] = C_SOH;
        } else {
                /* 1024-byte, CRC */
#ifdef DEBUG_XMODEM
                fprintf(DEBUG_FILE_HANDLE, "1024\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                rc = fread(current_block + 3, 1, 1024, file);
                if (ferror(file)) {
                        snprintf(notify_message, sizeof(notify_message), _("Error reading from file \"%s\": %s"), filename, strerror(errno));
                        notify_form(notify_message, 0);
                        /* ABORT transfer */
                        stats_file_cancelled(_("DISK READ ERROR"));
                        return Q_FALSE;
                }
                if (feof(file)) {
#ifdef DEBUG_XMODEM
                        fprintf(DEBUG_FILE_HANDLE, "LAST BLOCK\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                        /* This is the last block */
                        state = LAST_BLOCK;
                }
                for (i=rc+1; i<=1024; i++) {
                        /* Pad the remaining space with SUB */
                        current_block[i + 2] = C_SUB;
                }
                /* CRC */
                if (rc <= 128) {
                        /* Use a small terminating block */
                        crc = calcrc(current_block + 3, 128);
                        current_block[127 + 4] = (crc >> 8) & 0xFF;
                        current_block[127 + 5] = crc & 0xFF;
                        current_block_n = 128 + 5;
                        /* SOH */
                        current_block[0] = C_SOH;
                } else {
                        /* Normal 1K block */
                        crc = calcrc(current_block + 3, 1024);
                        current_block[1023 + 4] = (crc >> 8) & 0xFF;
                        current_block[1023 + 5] = crc & 0xFF;
                        current_block_n = 1024 + 5;
                        /* STX */
                        current_block[0] = C_STX;
                }
        }

        /* Sequence number */
        current_block[1] = current_block_sequence_i;
        current_block[2] = 0xFF - current_block_sequence_i;
        current_block_sequence_i++;
        current_block_number++;
        return Q_TRUE;
} /* ---------------------------------------------------------------------- */

/*
 * Verify that the block in current_block is valid and write to file if so
 */
static Q_BOOL verify_block() {
        int i;
        unsigned char checksum;
        int crc;
        int rc;
        unsigned char ch;
        unsigned char ch2;

        /* First, verify block size */
#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "XMODEM: verify_block() checking block size, current_block_n = %d\nInput bytes: ", current_block_n);
        for (i = 0; i < current_block_n; i++) {
                fprintf(DEBUG_FILE_HANDLE, "%02x ", (current_block[i] & 0xFF));
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

        switch (flavor) {
        case X_RELAXED:
        case X_NORMAL:
                if (current_block_n != 128 + 4) {
                        stats_increment_errors(_("SHORT/LONG BLOCK #%d"), current_block_number);
                        return Q_FALSE;
                }
                break;
        case X_CRC:
                if (current_block_n != 128 + 5) {
                        stats_increment_errors(_("SHORT/LONG BLOCK #%d"), current_block_number);
                        return Q_FALSE;
                }
                break;
        case X_1K:
        case X_1K_G:
        case Y_NORMAL:
        case Y_G:
                if ((current_block_n != 1024 + 5) && (current_block_n != 128 + 5)) {
                        stats_increment_errors(_("SHORT/LONG BLOCK #%d"), current_block_number);
                        return Q_FALSE;
                }
                break;
        }

#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "XMODEM: verify_block() checking header for SOH/STX\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

        /* Byte 0: SOH or STX */
        ch = current_block[0] & 0xFF;
        switch (flavor) {
        case X_RELAXED:
        case X_NORMAL:
        case X_CRC:
                if (ch != C_SOH) {
                        stats_increment_errors(_("HEADER ERROR IN BLOCK #%d"), current_block_number);
                        return Q_FALSE;
                }
                break;
        case X_1K:
        case X_1K_G:
        case Y_NORMAL:
        case Y_G:
                if ((ch != C_SOH) && (ch != C_STX)) {
                        stats_increment_errors(_("HEADER ERROR IN BLOCK #%d"), current_block_number);
                        return Q_FALSE;
                }
                break;
        }

#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "XMODEM: verify_block() checking sequence #, current_block_sequence_i = %d\n", current_block_sequence_i);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

        /* Byte 1 and 2: current block counter */
        ch = current_block[1];
        ch2 = current_block[2];
        if (current_block_sequence_i != ch) {
                stats_increment_errors(_("BAD BLOCK NUMBER IN BLOCK #%d"), current_block_number);
                return Q_FALSE;
        }
        if (ch + ch2 != 0xFF) {
                stats_increment_errors(_("COMPLIMENT BYTE BAD IN BLOCK #%d"), current_block_number);
                return Q_FALSE;
        }

#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "XMODEM: verify_block() checking CRC/checksum\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

        /* Finally, check the checksum or the CRC */
        if ((flavor == X_NORMAL) || (flavor == X_RELAXED)) {
                checksum = 0;
                for (i=3; i < 127 + 4; i++) {
                        ch = current_block[i];
                        checksum += ch;
                }
                if (checksum != (current_block[127 + 4] & 0xFF)) {
                        stats_increment_errors(_("CHECKSUM ERROR IN BLOCK #%d"), current_block_number);

#ifdef DEBUG_XMODEM
                        fprintf(DEBUG_FILE_HANDLE, "XMODEM: verify_block() bad checksum: %02x\n", checksum);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                        return Q_FALSE;
                }
        } else if (flavor == X_CRC) {
                /* X_CRC: fixed-length blocks */
                crc = calcrc(current_block + 3, 128);
                if ((((crc >> 8) & 0xFF) != (current_block[127 + 4] & 0xFF)) &&
                        ((crc & 0xFF) != (current_block[127 + 5] & 0xFF))) {
                        /* CRC didn't match */
                        stats_increment_errors(_("CRC ERROR IN BLOCK #%d"), current_block_number);

#ifdef DEBUG_XMODEM
                        fprintf(DEBUG_FILE_HANDLE, "XMODEM: verify_block() crc error: %02x %02x\n", ((crc >> 8) & 0xFF), (crc & 0xFF));
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                        return Q_FALSE;
                }
        } else {
                /* X_1K or X_1K_G or Y_NORMAL or Y_G -- variable length blocks */
                if (current_block[0] == C_SOH) {
                        crc = calcrc(current_block + 3, 128);
                        if ((((crc >> 8) & 0xFF) != (current_block[127 + 4] & 0xFF)) &&
                        ((crc & 0xFF) != (current_block[127 + 5] & 0xFF))) {
                                /* CRC didn't match */
                                stats_increment_errors(_("CRC ERROR IN BLOCK #%d"), current_block_number);

#ifdef DEBUG_XMODEM
                                fprintf(DEBUG_FILE_HANDLE, "XMODEM: verify_block() crc error: %02x %02x\n", ((crc >> 8) & 0xFF), (crc & 0xFF));
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                                return Q_FALSE;
                        }
                } else {
                        crc = calcrc(current_block + 3, 1024);
                        if ((((crc >> 8) & 0xFF) != (current_block[1023 + 4] & 0xFF)) &&
                        ((crc & 0xFF) != (current_block[1023 + 5] & 0xFF))) {
                                /* CRC didn't match */
                                stats_increment_errors(_("CRC ERROR IN BLOCK #%d"), current_block_number);
#ifdef DEBUG_XMODEM
                                fprintf(DEBUG_FILE_HANDLE, "XMODEM: verify_block() crc error: %02x %02x\n", ((crc >> 8) & 0xFF), (crc & 0xFF));
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                                return Q_FALSE;
                        }
                }
        }

#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "XMODEM: verify_block() block OK, writing to file\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

        /* Check for duplicate */
        if ((current_block[1] & 0xFF) == current_block_sequence_i - 1) {
                /* Duplicate block */
                stats_increment_errors(_("DUPLICATE BLOCK #%d"), current_block_number);
                return Q_FALSE;
        }

        /* Block is OK, so append to file */
        if (current_block[0] == C_SOH) {
                /* 128 byte block */
                rc = fwrite(current_block + 3, 1, 128, file);
                assert (rc == 128);
        } else {
                /* 1024 byte block */
                rc = fwrite(current_block + 3, 1, 1024, file);
                assert (rc == 1024);
        }
        fflush(file);

        /* Increment sequence # */
        current_block_sequence_i++;
        current_block_number++;

        /* Block OK */
#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "XMODEM: verify_block() returning true, file size is now %ld\n", q_transfer_stats.bytes_transfer);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
        return Q_TRUE;
} /* ---------------------------------------------------------------------- */

/*
 * Receive a file via the Xmodem protocol from input.
 */
static void xmodem_receive(unsigned char * input, int * input_n, unsigned char * output, int * output_n) {

#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "xmodem_receive() input_n = %d\n", *input_n);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

        time_t now;
        int i;
        int rc;
        int filesize;
        unsigned char last_byte;
        char full_filename[FILENAME_SIZE];
        struct utimbuf utime_buffer;
        char notify_message[DIALOG_MESSAGE_SIZE];

        /*
         * INIT begins the entire transfer.  We send first_byte and
         * immediately switch to BLOCK to await the data.
         *
         * Enhanced Xmodem modes will switch to FIRST_BLOCK and begin
         * awaiting the data.  If a data block doesn't come in within
         * the timeout period, FIRST_BLOCK will downgrade to regular
         * Xmodem, re-send the initial ACK, and then switch to BLOCK
         * just as regular Xmodem would have done.
         */
        if (state == INIT) {

#ifdef DEBUG_XMODEM
                fprintf(DEBUG_FILE_HANDLE, "XMODEM: sending initial byte to begin transfer: ");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

                /* Send the first byte */
                output[0] = first_byte;
                *output_n = 1;
                if ((flavor == X_NORMAL) || (flavor == X_RELAXED) || (flavor == Y_NORMAL) || (flavor == Y_G)) {
                        /* Initial state for normal is BLOCK */
                        state = BLOCK;
                } else {
                        /* Any others go to FIRST_BLOCK so they can fallback */
                        state = FIRST_BLOCK;
                }

                /* Reset timer */
                reset_timer();

#ifdef DEBUG_XMODEM
                fprintf(DEBUG_FILE_HANDLE, "'%c'\n", output[0]);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

                /* Clear input */
                *input_n = 0;
                return;
        }

        /*
         * FIRST_BLOCK is a special check for enhanced Xmodem support by the
         * sender.
         */
        if (state == FIRST_BLOCK) {

#ifdef DEBUG_XMODEM
                fprintf(DEBUG_FILE_HANDLE, "XMODEM: waiting for verification of sender extended Xmodem mode\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

                if (*input_n == 0) {
                        /* Special-case timeout processing.  We try to
                         * send the enhanced Xmodem first_byte ('C' or
                         * 'G') five times, with a three-second
                         * timeout between each attempt.  If we still
                         * have no transfer, we downgrade to regular
                         * Xmodem.
                         */
                        time(&now);
                        if (now - timeout_begin > 3) {
                                /* Timeout */
                                timeout_count++;
#ifdef DEBUG_XMODEM
                                fprintf(DEBUG_FILE_HANDLE, "XMODEM: First block timeout #%d\n", timeout_count);
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

                                if (timeout_count >= 5) {
#ifdef DEBUG_XMODEM
                                        fprintf(DEBUG_FILE_HANDLE, "XMODEM: fallback to vanilla Xmodem\n");
                                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

                                        stats_increment_errors(_("FALLBACK TO NORMAL XMODEM"));

                                        /* Send NAK */
                                        output[0] = C_NAK;
                                        *output_n = 1;
                                        prior_state = BLOCK;
                                        state = PURGE_INPUT;

                                        /* Downgrade to plain Xmodem */
                                        downgrade_to_vanilla_xmodem();
                                } else {
                                        stats_increment_errors(_("TIMEOUT"));
                                }

                                /* Reset timer */
                                reset_timer();

                                /* Re-send the NAK */
                                output[0] = first_byte;
                                *output_n = 1;
                        }

                        return;
                }

#ifdef DEBUG_XMODEM
                fprintf(DEBUG_FILE_HANDLE, "XMODEM: sender appears to be OK, switching to data mode\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

                /*
                 * We got some data using the enhanced Xmodem
                 * first_byte, so go into the block processing.
                 */
                state = BLOCK;
        }

        /*
         * BLOCK is the main receive data path.  We look for a data
         * block from the sender, decode it, write it to disk, and
         * then send an ACK when all of that works.
         */
        if (state == BLOCK) {
                /* See if data has yet arrived.  It might not be here
                 * yet because xmodem() is called as soon as we can
                 * write data out.
                 */
                if (*input_n == 0) {
#ifdef DEBUG_XMODEM
                        fprintf(DEBUG_FILE_HANDLE, "XMODEM: No block yet\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

                        /*
                         * No data has arrived yet.  See if the
                         * timeout has been reached.
                         */
                        if (check_timeout(output, output_n) == Q_TRUE) {
                                if (state != ABORT) {
                                        /* Send NAK */
                                        output[0] = first_byte;
                                        *output_n = 1;
                                        set_transfer_stats_last_message(_("SENDING NAK #%d"), current_block_number);
                                        /* Special case: for the first block NEVER go to PURGE_INPUT state */
                                        if (current_block_number == 1) {
                                                state = prior_state;
                                        }
                                }
                        }
                        return;
                }

                /* Reset timer */
                reset_timer();

                /* Data has indeed arrived.  See what it is. */
                if ((current_block_n + *input_n > XMODEM_MAX_BLOCK_SIZE) && (flavor != X_1K_G) && (flavor != Y_G)) {
                        /* Too much data was sent and this isn't 1K/G.
                         * Only Xmodem-1K/G streams blocks, so if we
                         * got more than XMODEM_MAX_BLOCK_SIZE we must
                         * have encountered line noise.  Wait for the
                         * input queue to clear and then have the
                         * PURGE_INPUT state send a NAK to continue.
                         */
                        prior_state = BLOCK;
                        state = PURGE_INPUT;

                        /* Clear input */
                        *input_n = 0;
                        return;

                } else if (((flavor == X_1K_G) || (flavor == Y_G)) && (!((current_block_sequence_i == 0) && (current_block_number == 1)))) {
                        /* Xmodem - 1K/G case: pull in just enough to
                         * make a complete block, process it, and come
                         * back for more.
                         */
                        int n = XMODEM_MAX_BLOCK_SIZE - current_block_n;
                        if (*input_n < n) {
                                /* Copy only what is here */
                                n = *input_n;
                        }
#ifdef DEBUG_XMODEM
                        fprintf(DEBUG_FILE_HANDLE, "XMODEM-G: adding %d bytes of data\n", n);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                        if (current_block_n + n > sizeof(current_block)) {
                                /* We are lost */
                                /* In G land this is a fatal error, ABORT */
                                clear_block();
                                stats_file_cancelled(_("Xmodem 1K/G error"));

                                /* Clear input */
                                *input_n = 0;
                                return;
                        }

                        memcpy(current_block + current_block_n, input, n);
                        current_block_n += n;

                        if (((current_block[0] == C_STX) && (current_block_n == XMODEM_MAX_BLOCK_SIZE)) ||
                                ((current_block[0] == C_SOH) && (current_block_n == 128 + 5))
                        ) {

                                /* Verify the block */
                                if (verify_block() == Q_FALSE) {
                                        /* In G land this is a fatal error, ABORT */
                                        clear_block();
                                        stats_file_cancelled(_("Xmodem 1K/G error"));

                                        /* Clear input */
                                        *input_n = 0;
                                        return;
                                }

#ifdef DEBUG_XMODEM
                                fprintf(DEBUG_FILE_HANDLE, "XMODEM: block OK, 1K/G NOT sending ACK\n");
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

                                stats_increment_blocks(current_block);
                                clear_block();
                        }

                        /* Save the remainder off the input into
                         * current_block and return */
                        memmove(input, input + n, *input_n - n);
                        *input_n -= n;
                        if ((current_block_n == 1) && (current_block[0] == C_EOT) && (*input_n == 0)) {
                                /* EOT, handle below */
                        } else {
                                /* Awaiting more data */
                                return;
                        }
                }

                /* We've got data than can fit inside current_block.
                 * Append it to current_block.
                 */
                if (current_block_n + *input_n > sizeof(current_block)) {
                        /* We are lost */

                        /* Throw the block away and request it again */
                        clear_block();
                        prior_state = BLOCK;
                        state = PURGE_INPUT;
                        /* Clear input */
                        *input_n = 0;
                        return;
                }

                memcpy(current_block + current_block_n, input, *input_n);
                current_block_n += *input_n;

                /*
                 * Special case: EOT means the last block received
                 * ended the file.
                 */
                if ((current_block_n == 1) && (current_block[0] == C_EOT)) {
#ifdef DEBUG_XMODEM
                        fprintf(DEBUG_FILE_HANDLE, "XMODEM: EOT, saving file...\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

                        /* Clear out current_block */
                        clear_block();

                        /* Xmodem pads the file with SUBs.  We
                         * generally don't want these SUBs to be in
                         * the final file image, as that leads to a
                         * corrupt file.  So eliminate the SUB's in
                         * the tail.  Note we do NOT do this for
                         * Ymodem.
                         */
                        for (;(flavor != Y_NORMAL) && (flavor != Y_G);) {
                                filesize = ftell(file);
                                rc = fseek(file, -1, SEEK_END);
                                if (rc != 0) {
                                        snprintf(notify_message, sizeof(notify_message), _("Error seeking in file \"%s\": %s"), filename, strerror(errno));
                                        notify_form(notify_message, 0);
                                }
                                i = fread(&last_byte, 1, 1, file);
                                if (i != 1) {
                                        snprintf(notify_message, sizeof(notify_message), _("Error reading from file \"%s\": %s"), filename, strerror(errno));
                                        notify_form(notify_message, 0);
                                }
                                if (last_byte == C_SUB) {
                                        rc = ftruncate(fileno(file), filesize - 1);
                                        if (rc != 0) {
                                                snprintf(notify_message, sizeof(notify_message), _("Error truncating file \"%s\": %s"), filename, strerror(errno));
                                                notify_form(notify_message, 0);
                                        }
                                        /* Special case: decrement the total bytes
                                           as we save the file. */
                                        q_transfer_stats.bytes_transfer--;
                                        q_transfer_stats.bytes_total--;
                                } else {
                                        /* Done */
                                        /* Send the ACK to end the transfer */
                                        output[0] = C_ACK;
                                        *output_n = 1;

                                        /* Set the final transfer state. */
                                        stats_file_complete_ok();
                                        break;
                                }
                        }

                        if ((flavor == Y_NORMAL) || (flavor == Y_G)) {

                                /*
                                 * For Ymodem, we already have the
                                 * file size from Block 0, so we can
                                 * just truncate the file to correct
                                 * size.
                                 */
                                ftruncate(fileno(file), q_transfer_stats.bytes_total);

                                /* The file is fully written, so close it. */
                                fclose(file);
                                file = NULL;

                                /* Modify the file's times to reflect
                                 * what was sent */
                                sprintf(full_filename, "%s/%s", download_path, filename);
                                utime_buffer.actime  = download_file_modtime; /* access time */
                                utime_buffer.modtime = download_file_modtime; /* modification time */
                                utime(full_filename, &utime_buffer);

#ifdef DEBUG_XMODEM
                                fprintf(DEBUG_FILE_HANDLE, "YMODEM: Sending EOT ACK and first_byte\n");
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

                                /* Not translated since it isn't a sentence */
                                set_transfer_stats_last_message("EOF");

                                /* Set the appropriate transfer stats state */
                                q_transfer_stats.state = Q_TRANSFER_STATE_FILE_DONE;

                                /* Send the ACK and the first byte again */
                                output[0] = C_ACK;
                                output[1] = first_byte;
                                *output_n = 2;

                                /* Reset the Block 0 check flag */
                                block0_has_been_seen = Q_FALSE;
                                current_block_sequence_i = 0;
                                current_block_number = 1;
                        }

                        /* Clear input */
                        *input_n = 0;
                        return;
                }


                if (    ((flavor == Y_NORMAL) || (flavor == Y_G)) &&
                        (current_block_sequence_i == 0) &&
                        (current_block_number == 1) &&
                        (block0_has_been_seen == Q_FALSE)
                ) {

                        if (    (       (current_block[0] == C_STX) &&
                                        (current_block_n >= XMODEM_MAX_BLOCK_SIZE)
                                ) ||
                                (       (current_block[0] == C_SOH) &&
                                        (current_block_n >= 128 + 5)
                                )
                        ) {


#ifdef DEBUG_XMODEM
                                fprintf(DEBUG_FILE_HANDLE, "YMODEM: block 0 received, calling ymodem_decode_block0()...\n");
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

                                /* Ymodem:  Block 0 */
                                if (ymodem_decode_block_0() == Q_TRUE) {
                                        if (flavor != Y_G) {
                                                /* Send the ACK and
                                                 * first_byte again to
                                                 * start the
                                                 * transfer */
                                                output[0] = C_ACK;
                                                output[1] = first_byte;
                                                *output_n = 2;
                                        } else {
                                                /* Send only the
                                                 * first_byte again to
                                                 * start the
                                                 * transfer */
                                                output[0] = first_byte;
                                                *output_n = 1;
                                        }

                                        block0_has_been_seen = Q_TRUE;
                                        /* Clear the block */
                                        clear_block();
                                } else {
                                        /* Throw the block away and
                                         * request it again
                                         */
                                        clear_block();
                                        prior_state = BLOCK;
                                        state = PURGE_INPUT;
                                        /* Clear input */
                                        *input_n = 0;
                                        return;
                                }

                                /* See if this is the terminator block */
                                if (strlen(filename) == 0) {
#ifdef DEBUG_XMODEM
                                        fprintf(DEBUG_FILE_HANDLE, "YMODEM: terminator block received\n");
                                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                                        /* Send ACK and end */
                                        output[0] = C_ACK;
                                        *output_n = 1;

                                        /* Set the final transfer state. */
                                        stats_file_complete_ok();
                                }

                        } else {
#ifdef DEBUG_XMODEM
                                fprintf(DEBUG_FILE_HANDLE, "YMODEM: need more for block0\n");
                                fprintf(DEBUG_FILE_HANDLE, "   current_block[0] == %02x\n",
                                        current_block[0]);
                                fprintf(DEBUG_FILE_HANDLE, "   current_block_n %d\n",
                                        current_block_n);
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

                        }

                        /* Clear input */
                        *input_n = 0;
                        return;
                }

                /* See if enough is in current_block to process it */
                switch (flavor) {
                case Y_NORMAL:
                case Y_G:
                case X_1K:
                case X_1K_G:
                        /* Block size is 1024 + 1 + 4 */
                        if ((current_block[0] == C_STX) && (current_block_n < XMODEM_MAX_BLOCK_SIZE)) {
                                /* Waiting for more data */
#ifdef DEBUG_XMODEM
                                fprintf(DEBUG_FILE_HANDLE, "XMODEM: waiting for more data (1k block)...\n");
#endif /* DEBUG_XMODEM */
                                /* Clear input */
                                *input_n = 0;
                                return;
                        }
                        /* Fall through ... */
                case X_CRC:
                        if (current_block_n < 128 + 5) {
                                /* Waiting for more data */
#ifdef DEBUG_XMODEM
                                fprintf(DEBUG_FILE_HANDLE, "XMODEM: waiting for more data (128 byte block with CRC)...\n");
#endif /* DEBUG_XMODEM */
                                /* Clear input */
                                *input_n = 0;
                                return;
                        }
                        break;
                default:
                        if (current_block_n < 128 + 4) {
                                /* Waiting for more data */
#ifdef DEBUG_XMODEM
                                fprintf(DEBUG_FILE_HANDLE, "XMODEM: waiting for more data (128 byte block)...\n");
#endif /* DEBUG_XMODEM */
                                /* Clear input */
                                *input_n = 0;
                                return;
                        }
                        break;
                }

#ifdef DEBUG_XMODEM
                fprintf(DEBUG_FILE_HANDLE, "XMODEM: block received, calling verify_block()...\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

                /*
                 * Normal case: a data block came in.  We verify the
                 * block data first with verify_block().
                 */
                if (verify_block() == Q_FALSE) {
                        /*
                         * verify_block() has already posted the appropriate
                         * error message to the progress dialog.
                         */
                        if (state == ABORT) {
                                /* Clear input */
                                *input_n = 0;
                                return;
                        }

                        /* Throw the block away and request it again */
                        clear_block();
                        prior_state = BLOCK;
                        state = PURGE_INPUT;
                        /* Clear input */
                        *input_n = 0;
                        return;
                }

#ifdef DEBUG_XMODEM
                fprintf(DEBUG_FILE_HANDLE, "XMODEM: block OK, sending ACK\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

                /* The data block was fine, so send an ACK and keep going... */
                output[0] = C_ACK;
                *output_n = 1;
                stats_increment_blocks(current_block);
                clear_block();

                /* Clear input */
                *input_n = 0;
                return;
        }

        /*
         * This state is used to wait until the input buffer is clear,
         * then send a NAK to request whatever was sent to be re-sent.
         */
        if (state == PURGE_INPUT) {
#ifdef DEBUG_XMODEM
                fprintf(DEBUG_FILE_HANDLE, "xmodem_receive PURGE INPUT\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                if (*input_n == 0) {
                        /* Send the NAK */
                        output[0] = C_NAK;
                        *output_n = 1;
                        state = prior_state;
                        set_transfer_stats_last_message(_("SENDING NAK #%d"), current_block_number);
                }
        }

#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "xmodem_receive returning at the end()\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

        /* Clear input */
        *input_n = 0;
        return;
} /* ---------------------------------------------------------------------- */

/*
 * Send a file via the Xmodem protocol to output.
 */
static void xmodem_send(unsigned char * input, int * input_n, unsigned char * output, int * output_n) {

#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "xmodem_send() STATE = %d input_n = %d\n", state, *input_n);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

        if ((*input_n > 0) && (input[0] == C_CAN)) {
                stats_file_cancelled(_("TRANSFER CANCELLED BY RECEIVER"));
                /* Clear input */
                *input_n = 0;
                return;
        }

        /*
         * This state is where everyone begins.  The receiver is going to
         * send first_byte, we're just marking time until we see it.
         */
        if (state == INIT) {

#ifdef DEBUG_XMODEM
                fprintf(DEBUG_FILE_HANDLE, "XMODEM: waiting for receiver to initiate transfer...\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

                set_transfer_stats_last_message(_("WAITING FOR NAK"));

                /* Do timeout processing */
                if (*input_n == 0) {
                        check_timeout(output, output_n);
                        return;
                }

                /* We've got some data, check it out */
                if (*input_n >= 1) {
#ifdef DEBUG_XMODEM
                        fprintf(DEBUG_FILE_HANDLE, "XMODEM: received char: '%c'\n", input[0]);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

                        /*
                         * It would be nice to just look for
                         * first_byte and zip off to BLOCK state.  But
                         * we need to see if the receiver is using the
                         * same kind of Xmodem enhancement we expect.
                         * If not, we need to downgrade.  So we have a
                         * switch for the various flavor downgrade
                         * options.
                         */
                        switch (flavor) {
                        case X_RELAXED:
                        case X_NORMAL:
                                if (input[0] == first_byte) {
                                        /* We're good to go. */
                                        state = BLOCK;

                                        /* Clear the last message */
                                        set_transfer_stats_last_message("");

                                        /* Put an ACK here so the if
                                         * (state == BLOCK) case can
                                         * construct the first block.
                                         */
                                        input[0] = C_ACK;
                                } else {
                                        /*
                                         * Error.  Wait and see if the
                                         * receiver will downgrade.
                                         */

                                        /* Clear input */
                                        *input_n = 0;
                                        return;
                                }
                                break;
                        case X_CRC:
                        case X_1K:
                        case X_1K_G:
                                if (input[0] == first_byte) {
                                        /* We're good to go. */
                                        state = BLOCK;
                                        /* Put an ACK here so the if
                                         * (state == BLOCK) case can
                                         * construct the first block.
                                         */

                                        /* Clear the last message */
                                        set_transfer_stats_last_message("");

                                        input[0] = C_ACK;
                                } else if (input[0] == C_NAK) {
#ifdef DEBUG_XMODEM
                                        fprintf(DEBUG_FILE_HANDLE, "XMODEM: fallback to vanilla Xmodem\n");
                                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

                                        /* Clear the last message */
                                        set_transfer_stats_last_message("");

                                        /* Downgrade to plain Xmodem */
                                        downgrade_to_vanilla_xmodem();
                                        state = BLOCK;
                                } else {
                                        /* Error, proceed to timeout
                                         * case.  Just return and the
                                         * next xmodem_send() will do
                                         * timeout checks.
                                         */

                                        /* Clear input */
                                        *input_n = 0;
                                        return;
                                }
                                break;
                        case Y_NORMAL:
                        case Y_G:
                                if (input[0] == first_byte) {
                                        /* We're good to go. */
                                        state = YMODEM_BLOCK0;
                                } else {
                                        /* Error, proceed to timeout
                                         * case.  Just return and the
                                         * next xmodem_send() will do
                                         * timeout checks.
                                         */
                                }
                                break;
                        } /* switch (flavor) */
                } /* if (*input_n == 1) */

                /* At this point, we've either gotten the first_byte
                 * we expect, or we've downgraded to vanilla Xmodem,
                 * OR we've seen complete garbage from the receiver.
                 * In the first two cases, we've already switched
                 * state to BLOCK and we have a NAK waiting on the
                 * input queue.  In the last case, we're still in INIT
                 * state.
                 *
                 * Since we've got NAK/first_byte, we need to fall
                 * through to the BLOCK state and begin sending data,
                 * so we DON'T put a return here.
                 *
                 * Finally, for Ymodem, when we saw first_byte we
                 * switched to YMODEM_BLOCK0 state.
                 */
        }

        /*
         * Ymodem has a weird startup sequence:
         *
         * 1) Wait for 'C' or 'G'.
         * 2) Send block 0
         * 3) Wait for ACK
         * 4) Wait for 'C' or 'G' AGAIN.
         * 5) Send data...
         *
         * It's that step 4 that creates this mess of YMODEM_BLOCK0
         * states.  We might get ACK then 'C'/'G' as two separated
         * calls to xmodem(), OR we might get ACK + 'C'/'G' as one
         * call.
         *
         * Our state machine goes like this:
         *   INIT
         *     Got first_byte       --> YMODEM_BLOCK0
         *   YMODEM_BLOCK0
         *     Send block 0         --> YMODEM_BLOCK0_ACK1
         *   YMODEM_BLOCK0_ACK1
         *     See ACK alone        --> YMODEM_BLOCK0_ACK2
         *     See ACK + first_byte --> BLOCK
         *   YMODEM_BLOCK0_ACK2
         *     See first_byte       --> BLOCK
         */
        if (state == YMODEM_BLOCK0) {
                /* Send block 0 */
#ifdef DEBUG_XMODEM
                fprintf(DEBUG_FILE_HANDLE, "YMODEM: Sending block 0\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                ymodem_construct_block_0();
                assert(*output_n == 0);
                memcpy(output + *output_n, current_block, current_block_n);
                *output_n += current_block_n;

                /*
                 * Tell the user, but only if we're really sending another file
                 */
                if (filename != NULL) {
                        set_transfer_stats_last_message(_("SENDING HEADER"));
                }

                /* Switch state */
                state = YMODEM_BLOCK0_ACK1;

                /* Reset timer */
                reset_timer();

                /* Clear input */
                *input_n = 0;
                return;
        }

        if (state == YMODEM_BLOCK0_ACK1) {

                if (flavor == Y_G) {
#ifdef DEBUG_XMODEM
                        fprintf(DEBUG_FILE_HANDLE, "YMODEM-G: Block 0 out, looking for 'G'\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                        /*
                         * Special case: we can dump out immediately
                         * after the last file.
                         */
                        if (filename == NULL) {
#ifdef DEBUG_XMODEM
                                fprintf(DEBUG_FILE_HANDLE, "YMODEM-G: DONE\n");
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                                /* Set the final transfer state. */
                                stats_file_complete_ok();

                                /* Clear input */
                                *input_n = 0;
                                return;
                        }
                } else {
#ifdef DEBUG_XMODEM
                        fprintf(DEBUG_FILE_HANDLE, "YMODEM: Block 0 out, looking for ACK\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                }

                if (*input_n == 0) {
                        check_timeout(output, output_n);
                        return;
                }

                if ((((*input_n >= 1)) && ((input[0] == C_ACK) && (flavor == Y_NORMAL))) || (((input[0] == 'G') && (flavor == Y_G)))) {

                        if (flavor == Y_G) {
#ifdef DEBUG_XMODEM
                                fprintf(DEBUG_FILE_HANDLE, "YMODEM-G: Block 0 'G' received\n");
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                        } else {
#ifdef DEBUG_XMODEM
                                fprintf(DEBUG_FILE_HANDLE, "YMODEM: Block 0 ACK received\n");
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                        }

                        if (filename == NULL) {
#ifdef DEBUG_XMODEM
                                fprintf(DEBUG_FILE_HANDLE, "YMODEM: DONE\n");
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                                /* Set the final transfer state. */
                                stats_file_complete_ok();

                                /* Clear input */
                                *input_n = 0;
                                return;
                        }

                        if (flavor == Y_NORMAL) {

                                /* ACK received */
                                state = YMODEM_BLOCK0_ACK2;

                                /* Check for 'C' or 'G' */
                                if (*input_n == 2) {
                                        if (input[1] == first_byte) {
#ifdef DEBUG_XMODEM
                                                fprintf(DEBUG_FILE_HANDLE, "YMODEM: block 0 '%c' received\n", first_byte);
                                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                                                state = BLOCK;
                                                /* Put an ACK here so the if (state == BLOCK) case
                                                 * can construct the first block.
                                                 */
                                                input[0] = C_ACK;
                                                *input_n = 1;
                                        }
                                }
                        } else {
                                /* Ymodem-G go straight to BLOCK */
                                state = BLOCK;

                                /* Toss input */
                                *input_n = 0;
                        }
                }

                if ((*input_n == 1) && (input[0] == C_NAK)) {
#ifdef DEBUG_XMODEM
                        fprintf(DEBUG_FILE_HANDLE, "YMODEM: NAK on block 0, re-sending\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                        state = YMODEM_BLOCK0;
                        /* Reset the sequence number */
                        current_block_sequence_i = 0;

                        /* Clear input */
                        *input_n = 0;
                        return;
                }
                /* Like the exit point of INIT, we might be ready for
                 * BLOCK if both ACK and first_byte were seen.  So
                 * don't return, fall through to send the first
                 * block */

                /* Clear the last message */
                set_transfer_stats_last_message("");
        }

        if (state == YMODEM_BLOCK0_ACK2) {
#ifdef DEBUG_XMODEM
                fprintf(DEBUG_FILE_HANDLE, "YMODEM: Block 0 out, looking for 'C' or 'G'\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                if (*input_n == 0) {
                        check_timeout(output, output_n);
                        return;
                }
                if ((*input_n == 1) && (input[0] == first_byte)) {
#ifdef DEBUG_XMODEM
                        fprintf(DEBUG_FILE_HANDLE, "YMODEM: block 0 '%c' received\n", first_byte);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                        /* Good to go */
                        state = BLOCK;
                        /* Put an ACK here so the if (state == BLOCK)
                         * case can construct the first block.
                         */
                        input[0] = C_ACK;
                }
                /* Like the exit point of INIT, we might be ready for
                 * BLOCK if the first_byte was seen.  So don't return,
                 * fall through to send the first block */

                /* Clear the last message */
                set_transfer_stats_last_message("");
        }

        /*
         * This is the meat of send.  We make sure that an ACK is
         * waiting in input first to let us know that the previous
         * block was OK.  Then we construct and send out the next
         * block.
         */
        if (state == BLOCK) {
#ifdef DEBUG_XMODEM
                fprintf(DEBUG_FILE_HANDLE, "XMODEM: looking for ACK\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

                /* See if ACK is here */
                if (((*input_n == 1) && (input[0] == C_ACK)) || (flavor == X_1K_G) || (flavor == Y_G)) {
                        /*
                         * The receiver sent an ACK, so we can send a new block.
                         */

                        /* Reset timer */
                        reset_timer();

#ifdef DEBUG_XMODEM
                        if ((flavor == X_1K_G) || (flavor == Y_G)) {
                                fprintf(DEBUG_FILE_HANDLE, "XMODEM: -G no ACK needed, keep going\n");
                        } else {
                                fprintf(DEBUG_FILE_HANDLE, "XMODEM: ACK found, constructing block %d\n", current_block_sequence_i);
                        }
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

                        /* Construct block.  Note that if this is the last block
                         * state will be LAST_BLOCK.
                         */
                        clear_block();
                        if (construct_block() == Q_FALSE) {
#ifdef DEBUG_XMODEM
                                fprintf(DEBUG_FILE_HANDLE, "XMODEM: ABORT, file error\n");
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                                /* construct_block() has already cancelled the transfer
                                 * if it encountered an error with local I/O.
                                 */
                                /* Clear input */
                                *input_n = 0;
                                return;
                        }

#ifdef DEBUG_XMODEM
                        fprintf(DEBUG_FILE_HANDLE, "XMODEM: Delivering block\n");
                        fprintf(DEBUG_FILE_HANDLE, "XMODEM: output_n %d current_block_n %d\n", *output_n, current_block_n);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

                        /* Send the block out. */
                        if ((flavor != X_1K_G) && (flavor != Y_G)) {
                                assert(*output_n == 0);
                        }

                        memcpy(output + *output_n, current_block, current_block_n);
                        *output_n += current_block_n;

                        /* Update stats on the prior block */
                        if (state == LAST_BLOCK) {
#ifdef DEBUG_XMODEM
                                fprintf(DEBUG_FILE_HANDLE, "XMODEM: Sending last block...\n");
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                                q_transfer_stats.bytes_transfer = q_transfer_stats.bytes_total;
                                q_screen_dirty = Q_TRUE;
                        } else {
                                stats_increment_blocks(input);
                        }

                        /* Clear input */
                        *input_n = 0;
                        return;

                } else if ((*input_n == 1) && (input[0] == C_NAK)) {
                        /*
                         * The receiver sent a NAK, so we have to re-send the current
                         * block.
                         */
#ifdef DEBUG_XMODEM
                        fprintf(DEBUG_FILE_HANDLE, "XMODEM: NAK found, resending block\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                        prior_state = BLOCK;
                        state = PURGE_INPUT;
                        /* Not translated since it isn't a real sentence */
                        stats_increment_errors("NAK");

                        /* Clear input */
                        *input_n = 0;
                        return;
                } else if (*input_n == 0) {
                        /*
                         * Still nothing from the receiver, so do timeout processing.
                         */
                        if (check_timeout(output, output_n) == Q_TRUE) {
                                /*
                                 * Re-send the block just in case.
                                 */
                                prior_state = BLOCK;
                                state = PURGE_INPUT;
                        }
                        return;
                } else if (*input_n > 0) {
                        /* The receiver sent me some garbage, re-send the
                         * block.  But first purge whatever else he sent */
                        prior_state = BLOCK;
                        state = PURGE_INPUT;
                        stats_increment_errors(_("LINE NOISE, !@#&*%U"));
                        /* Clear input */
                        *input_n = 0;
                        return;
                }
        }

        /*
         * The only other state using PURGE_INPUT is BLOCK and LAST_BLOCK.
         * We get here when the receiver sent us garbage or NAK instead
         * of a clear ACK.
         */
        if (state == PURGE_INPUT) {
                /* Reset timer */
                reset_timer();
#ifdef DEBUG_XMODEM
                fprintf(DEBUG_FILE_HANDLE, "XMODEM: Waiting for input buffer to clear\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                if (*input_n == 0) {
#ifdef DEBUG_XMODEM
                        fprintf(DEBUG_FILE_HANDLE, "XMODEM: Re-sending current block\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                        if ((prior_state == BLOCK) || (prior_state == LAST_BLOCK)) {
                                /* Re-send the current block */
                                assert(*output_n == 0);
                                memcpy(output + *output_n, current_block, current_block_n);
                                *output_n += current_block_n;
                        }
                        state = prior_state;
                }

                /* Clear input */
                *input_n = 0;
                return;
        }

        /*
         * This is the special case for when the EOT is ready to be
         * transmitted.  construct_block() changed our state to LAST_BLOCK
         * when it encountered EOF.
         */
        if (state == LAST_BLOCK) {
                /* See if the receiver ACK'd the last block. */
                if (((*input_n == 1) && (input[0] == C_ACK)) || (flavor == X_1K_G) || (flavor == Y_G)) {
                        /*
                         * The receiver ACK'd the last block.
                         * Send EOT to end the transfer.
                         */
#ifdef DEBUG_XMODEM
                        fprintf(DEBUG_FILE_HANDLE, "XMODEM: Sending EOT\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                        output[*output_n] = C_EOT;
                        *output_n += 1;
                        state = EOT_ACK;
                        set_transfer_stats_last_message(_("SENDING EOT"));

                        /* Increment on the last block now that it's ACK'd */
                        q_transfer_stats.blocks_transfer++;
                        q_transfer_stats.bytes_transfer = q_transfer_stats.bytes_total;
                        q_screen_dirty = Q_TRUE;

                        /* Reset timer */
                        reset_timer();
                        /* Clear input */
                        *input_n = 0;
                        return;
                } else if ((*input_n == 1) && (input[0] == C_NAK)) {
                        /*
                         * Oops!  The receiver said the last block was bad.
                         * Re-send the last block.
                         */
#ifdef DEBUG_XMODEM
                        fprintf(DEBUG_FILE_HANDLE, "XMODEM: NAK on LAST BLOCK, resending...\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                        prior_state = LAST_BLOCK;
                        state = PURGE_INPUT;
                        /* Not translated since it isn't a real sentence */
                        stats_increment_errors("NAK");
                        /* Clear input */
                        *input_n = 0;
                        return;
                } else if (*input_n == 0) {
                        /*
                         * Do timeout checks
                         */
                        check_timeout(output, output_n);
                        return;
                } else {
                        /* The receiver sent me some garbage, re-send the
                         * block.  But first purge whatever else he sent */
                        prior_state = LAST_BLOCK;
                        state = PURGE_INPUT;
                        stats_increment_errors(_("LINE NOISE, !@#&*%U"));
                        return;
                }
        }

        /*
         * Finally!  We are waiting to see the receiver ACK the EOT.
         */
        if (state == EOT_ACK) {
#ifdef DEBUG_XMODEM
                fprintf(DEBUG_FILE_HANDLE, "XMODEM: Waiting for ACK to EOT\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

                if (*input_n == 0) {
                        if (check_timeout(output, output_n) == Q_TRUE) {
                                /* We got a timeout so re-send the EOT */
                                output[*output_n] = C_EOT;
                                *output_n += 1;
                        }
                        return;
                }

                if ((*input_n >= 1) && (input[0] == C_ACK)) {
                        /* DONE */
                        fclose(file);
                        file = NULL;

#ifdef DEBUG_XMODEM
                        fprintf(DEBUG_FILE_HANDLE, "XMODEM: Received EOT ACK\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

                        if ((flavor == Y_NORMAL) || (flavor == Y_G)) {
                                /* Ymodem special case:
                                 * This was just the first file!  Get it
                                 * ready for the next file.
                                 */
                                /* Not translated since it isn't a sentence */
                                set_transfer_stats_last_message("EOF");
                                q_transfer_stats.bytes_transfer = q_transfer_stats.bytes_total;

                                /* Setup for the next file */
                                upload_file_list_i++;
                                setup_for_next_file();
                                current_block_sequence_i = 0;
                                current_block_number = 1;
                                timeout_count = 0;
                                clear_block();

                                /* Switch state */
                                state = INIT;

                                /* Reset timer */
                                reset_timer();

                                /* Shift input down 1 byte */
                                if (*input_n > 1) {
                                        int n = *input_n - 1;
                                        memmove(input, input + 1, n);
                                        *input_n = n;
                                }
#ifdef DEBUG_XMODEM
                                fprintf(DEBUG_FILE_HANDLE, "YMODEM: Back to INIT\n");
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

                                return;
                        }

                        /* Normal Xmodem case */

                        /* Set the final transfer state. */
                        stats_file_complete_ok();
                }
        }

#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "xmodem_send returning at the end()\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

        /* Clear input */
        *input_n = 0;
        return;
} /* ---------------------------------------------------------------------- */

/*
 * Perform the Xmodem protocol against input and output.
 */
void xmodem(unsigned char * input, const int input_n, int * remaining, unsigned char * output, int * output_n, const int output_max) {

#ifdef DEBUG_XMODEM
        int i;
#endif /* DEBUG_XMODEM */

        /* Check my input arguments */
        assert(input_n >= 0);
        assert(input != NULL);
        assert(output != NULL);
        assert(*output_n >= 0);
        assert(output_max > XMODEM_MAX_BLOCK_SIZE);

        /*
         * It's amazing how little documentation exists for Xmodem
         * and Ymodem in paper form.  My local university library
         * only had one book with enough detail in it to actually
         * implement bare-bones Xmodem.  I've got another on order
         * that supposedly has lots of great detail.
         *
         * OTOH, in electronic form I find Chuck Forsberg's "Tower
         * of Babel" document that describes to byte-level detail
         * exactly how Xmodem (checksum, CRC, and 1K) work along
         * with Ymodem.
         *
         * The (X/Y/Z)modem protocols really are a product of the
         * early online culture.  Today's computer bookstore or
         * library has NO real information about this most
         * fundamental operation that almost every modem program
         * in the world has implemented.  But they've got hundreds
         * of books about Cisco routers, Oracle databases, and
         * artificial intelligence -- none of which are present on
         * a typical home system.  <sigh>
         *
         */

        if (state == ABORT) {
                return;
        }

#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "XMODEM: state = %d current_block_n = %d\n", state, current_block_n);
        if (flavor == X_NORMAL) {
                fprintf(DEBUG_FILE_HANDLE, "XMODEM: flavor = X_NORMAL\n");
        }
        if (flavor == X_CRC) {
                fprintf(DEBUG_FILE_HANDLE, "XMODEM: flavor = X_CRC\n");
        }
        if (flavor == X_RELAXED) {
                fprintf(DEBUG_FILE_HANDLE, "XMODEM: flavor = X_RELAXED\n");
        }
        if (flavor == X_1K) {
                fprintf(DEBUG_FILE_HANDLE, "XMODEM: flavor = X_1K\n");
        }
        if (flavor == X_1K_G) {
                fprintf(DEBUG_FILE_HANDLE, "XMODEM: flavor = X_1K_G\n");
        }
        if (flavor == Y_NORMAL) {
                fprintf(DEBUG_FILE_HANDLE, "XMODEM: flavor = Y_NORMAL\n");
        }
        if (flavor == Y_G) {
                fprintf(DEBUG_FILE_HANDLE, "XMODEM: flavor = Y_G\n");
        }

        fprintf(DEBUG_FILE_HANDLE, "XMODEM: %d input bytes: ", input_n);
        for (i=0; i<input_n; i++) {
                fprintf(DEBUG_FILE_HANDLE, "%02x ", (input[i] & 0xFF));
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

        if (sending == Q_FALSE) {
                /* -G protocols might see multiple packets, so loop this */
                int n = input_n;
                do {
                        xmodem_receive(input, &n, output, output_n);
                } while (n > 0);
        } else {
                int n = input_n;
                if (output_max - *output_n < XMODEM_MAX_BLOCK_SIZE) {
                        /* Don't send unless there is enough room for a full block */
                        return;
                }
                do {
                        xmodem_send(input, &n, output, output_n);
                } while (n > 0);
        }

        /* All of the input was consumed */
        *remaining = 0;

#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "XMODEM: cleared input queue\n");
        fprintf(DEBUG_FILE_HANDLE, "XMODEM: %d output bytes: ", *output_n);
        for (i = 0; i < *output_n; i++) {
                fprintf(DEBUG_FILE_HANDLE, "%02x ", (output[i] & 0xFF));
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

} /* ---------------------------------------------------------------------- */

/*
 * Setup the Xmodem protocol for a file transfer
 */
Q_BOOL xmodem_start(const char * in_filename, const Q_BOOL send, const XMODEM_FLAVOR in_flavor) {
        struct stat fstats;
        char notify_message[DIALOG_MESSAGE_SIZE];

        /* Assume we don't start up successfully */
        state = ABORT;

#ifdef DEBUG_XMODEM
        if (DEBUG_FILE_HANDLE == NULL) {
                DEBUG_FILE_HANDLE = fopen("debug_xmodem.txt", "w");
        }
        fprintf(DEBUG_FILE_HANDLE, "XMODEM: START flavor = %d ", in_flavor);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

        if (send == Q_TRUE) {
                /* Pull the file size */
                if (stat(in_filename, &fstats) < 0) {
                        return Q_FALSE;
                }

                if ((file = fopen(in_filename, "rb")) == NULL) {
#ifdef DEBUG_XMODEM
                        fprintf(DEBUG_FILE_HANDLE, "false\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                        return Q_FALSE;
                }
                /* Initialize timer for the first timeout */
                reset_timer();
        } else {
                if ((file = fopen(in_filename, "w+b")) == NULL) {
#ifdef DEBUG_XMODEM
                        fprintf(DEBUG_FILE_HANDLE, "false\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
                        return Q_FALSE;
                }
        }

        filename = Xstrdup(in_filename, __FILE__, __LINE__);
        sending = send;
        flavor = in_flavor;
        state = INIT;
        current_block_sequence_i = 1;
        current_block_number = 1;
        timeout_count = 0;
        timeout_max = 10;
        clear_block();

        if (sending == Q_TRUE) {
                if (stat(filename, &fstats) < 0) {
                        snprintf(notify_message, sizeof(notify_message), _("Error stat()'ing file \"%s\": %s"), filename, strerror(errno));
                        notify_form(notify_message, 0);
                        return Q_FALSE;
                }

                switch (flavor) {

                case X_RELAXED:
                case X_NORMAL:
                case X_CRC:
                        if (sending == Q_TRUE) {
                                q_transfer_stats.bytes_total = fstats.st_size;
                                q_transfer_stats.blocks = fstats.st_size / 128;
                        }
                        if ((q_transfer_stats.blocks % 128) > 0) {
                                q_transfer_stats.blocks++;
                        }
                        break;
                case X_1K:
                case X_1K_G:
                        if (sending == Q_TRUE) {
                                q_transfer_stats.bytes_total = fstats.st_size;
                                q_transfer_stats.blocks = fstats.st_size / 1024;
                        }
                        if ((q_transfer_stats.blocks % 1024) > 0) {
                                q_transfer_stats.blocks++;
                        }
                        break;
                default:
                        /* Should never get here */
                        assert(1 == 0);
                }
        }

        /* Set block_size */
        if ((flavor == X_1K) || (flavor == X_1K_G)) {
                q_transfer_stats.block_size = 1024;
        } else {
                q_transfer_stats.block_size = 128;
        }

        /* Set first byte */
        switch (flavor) {
        case X_RELAXED:
                timeout_max *= 10;
                /* Fall through */
        case X_NORMAL:
                first_byte = C_NAK;
                break;
        case X_CRC:
        case X_1K:
                first_byte = 'C';
                break;
        case X_1K_G:
                first_byte = 'G';
                break;
        default:
                /* Should never get here */
                assert(1 == 0);
        }

        /* Clear the last message */
        set_transfer_stats_last_message("");

#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "true\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
        return Q_TRUE;
} /* ---------------------------------------------------------------------- */

/*
 * End an Xmodem transfer
 */
void xmodem_stop(const Q_BOOL save_partial) {
        char notify_message[DIALOG_MESSAGE_SIZE];

#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "XMODEM: STOP\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
        if ((save_partial == Q_TRUE) || (sending == Q_TRUE)) {
                if (file != NULL) {
                        fflush(file);
                        fclose(file);
                }
        } else {
                if (file != NULL) {
                        fclose(file);
                        if (unlink(filename) < 0) {
                                snprintf(notify_message, sizeof(notify_message), _("Error deleting file \"%s\": %s"), filename, strerror(errno));
                                notify_form(notify_message, 0);
                        }
                }
        }
        file = NULL;
        if (filename != NULL) {
                Xfree(filename, __FILE__, __LINE__);
        }
        filename = NULL;
} /* ---------------------------------------------------------------------- */

/*
 * Setup the Ymodem protocol for a file transfer
 */
Q_BOOL ymodem_start(struct file_info * file_list, const char * pathname, const Q_BOOL send, const XMODEM_FLAVOR in_flavor) {
        /*
         * If I got here, then I know that all the files in file_list exist.
         * forms.c makes sure the files are all readable by me.
         */

        /* Assume we don't start up successfully */
        state = ABORT;

        upload_file_list = file_list;
        upload_file_list_i = 0;

#ifdef DEBUG_XMODEM
        if (DEBUG_FILE_HANDLE == NULL) {
                DEBUG_FILE_HANDLE = fopen("debug_xmodem.txt", "w");
        }
        fprintf(DEBUG_FILE_HANDLE, "YMODEM: START flavor = %d ", in_flavor);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */

        if (send == Q_TRUE) {
                /* Set up for first file */
                if (setup_for_next_file() == Q_FALSE) {
                        return Q_FALSE;
                }
        } else {
                /* Save download path */
                download_path = Xstrdup(pathname, __FILE__, __LINE__);
                set_transfer_stats_filename("");
                set_transfer_stats_pathname(pathname);
        }

        sending = send;
        flavor = in_flavor;
        state = INIT;
        current_block_sequence_i = 0;
        current_block_number = 1;
        block0_has_been_seen = Q_FALSE;
        timeout_count = 0;
        timeout_max = 10;
        clear_block();

        /* Set block size */
        q_transfer_stats.block_size = 1024;

        /* Set first byte */
        if (flavor == Y_NORMAL) {
                first_byte = 'C';
        } else {
                first_byte = 'G';
        }

        /* Clear the last message */
        set_transfer_stats_last_message("");

#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "true\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
        return Q_TRUE;
} /* ---------------------------------------------------------------------- */

/*
 * End a Ymodem transfer
 */
void ymodem_stop(Q_BOOL save_partial) {
        char notify_message[DIALOG_MESSAGE_SIZE];

#ifdef DEBUG_XMODEM
        fprintf(DEBUG_FILE_HANDLE, "YMODEM: STOP\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_XMODEM */
        if ((save_partial == Q_TRUE) || (sending == Q_TRUE)) {
                if (file != NULL) {
                        fflush(file);
                        fclose(file);
                }
        } else {
                if (file != NULL) {
                        fclose(file);
                        if (unlink(filename) < 0) {
                                snprintf(notify_message, sizeof(notify_message), _("Error deleting file \"%s\": %s"), filename, strerror(errno));
                                notify_form(notify_message, 0);
                        }
                }
        }
        file = NULL;
        if (filename != NULL) {
                Xfree(filename, __FILE__, __LINE__);
        }
        filename = NULL;
        if (download_path != NULL) {
                Xfree(download_path, __FILE__, __LINE__);
        }
        download_path = NULL;
} /* ---------------------------------------------------------------------- */
