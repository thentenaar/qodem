/*
 * kermit.c
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
 * Bugs in the Kermit protocol:
 *
 *     NONE so far
 *
 * Bugs noted in gkermit implementation:
 *
 * 1)  Doesn't send the file creation time in Attributes (though ckermit does)
 *
 * Bugs noted in ckermit implementation:
 *
 *     NONE so far
 *
 *
 * TODO:
 *     Expose block size in qodemrc
 *     Locking shift
 */

#include "qcurses.h"
#include "common.h"

#include <assert.h>
#ifdef __BORLANDC__
#include <strptime.h>
#else
#include <libgen.h>
#include <unistd.h>
#endif
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <utime.h>
#include "qodem.h"
#include "kermit.h"

/*
 * Note I want Kermit to be somewhat modular, but I do modify
 * q_transfer_stats directly.
 */
#include "protocols.h"
#include "music.h"

/* #define DEBUG_KERMIT 1 */
#undef DEBUG_KERMIT
/* #define DEBUG_KERMIT_VERBOSE 1 */
#undef DEBUG_KERMIT_VERBOSE
#ifdef DEBUG_KERMIT
static FILE * DEBUG_FILE_HANDLE = NULL;
#endif

/*
 * Technically, Kermit maxes at 900k bytes, but Qodem will top out at 1k
 * byte data packets while sending.
 */
#define KERMIT_BLOCK_SIZE 1024

/* Data types ----------------------------------------------- */

/* Packet types */
typedef enum {
        P_KSINIT,
        P_KACK,
        P_KNAK,
        P_KDATA,
        P_KFILE,
        P_KEOF,
        P_KBREAK,
        P_KERROR,
        P_KSERVINIT,
        P_KTEXT,
        P_KRINIT,
        P_KATTRIBUTES,
        P_KCOMMAND,
        P_KKERMIT_COMMAND,
        P_KGENERIC_COMMAND,
        P_KRESERVED1,
        P_KRESERVED2
} PACKET_TYPE;

struct packet_type_char {
        PACKET_TYPE type;
        char packet_char;
        char * description;
};

static struct packet_type_char packet_type_chars[] = {
        { P_KSINIT,             'S', "Send-Init" },
        { P_KACK,               'Y', "ACK Acknowledge" },
        { P_KNAK,               'N', "NAK Negative Acknowledge" },
        { P_KDATA,              'D', "File Data" },
        { P_KFILE,              'F', "File Header" },
        { P_KEOF,               'Z', "EOF End Of File" },
        { P_KBREAK,             'B', "EOT Break Transmission" },
        { P_KERROR,             'E', "Error" },
        { P_KSERVINIT,          'I', "Initialize Server" },
        { P_KTEXT,              'X', "Text Header" },
        { P_KRINIT,             'R', "Receive Initiate" },
        { P_KATTRIBUTES,        'A', "File Attributes" },
        { P_KCOMMAND,           'C', "Host Command" },
        { P_KKERMIT_COMMAND,    'K', "Kermit Command" },
        { P_KGENERIC_COMMAND,   'G', "Generic Kermit Command" },
        { P_KRESERVED1,         'T', "Reserved" },
        { P_KRESERVED2,         'Q', "Reserved" }
};

static PACKET_TYPE packet_type(char type_char) {
        int i;
        for (i = P_KSINIT; i <= P_KRESERVED2; i++) {
                if (packet_type_chars[i].packet_char == type_char) {
                        return packet_type_chars[i].type;
                }
        }
        return -1;
} /* ---------------------------------------------------------------------- */

#ifdef DEBUG_KERMIT
static char * packet_type_string(char type_char) {
        int i;
        for (i = P_KSINIT; i <= P_KRESERVED2; i++) {
                if (packet_type_chars[i].packet_char == type_char) {
                        return packet_type_chars[i].description;
                }
        }
        return "UNKNOWN PACKET TYPE";
} /* ---------------------------------------------------------------------- */

static char * packet_type_name(int type) {
        int i;
        for (i = P_KSINIT; i <= P_KRESERVED2; i++) {
                if (packet_type_chars[i].type == type) {
                        return packet_type_chars[i].description;
                }
        }
        return "UNKNOWN PACKET TYPE";
} /* ---------------------------------------------------------------------- */
#endif /* DEBUG_KERMIT */

/* The state of the protocol */
typedef enum {
        INIT,                   /* Before the first byte is sent */

        COMPLETE,               /* Transfer complete */

        ABORT,                  /* Transfer was aborted due to excessive
                                 * timeouts, user abort, or other error */

        /*
         * These states are taken directly from the Kermit Protocol
         * book.
         */
        KM_S,                   /* Send Send-Init packet */
        KM_SF,                  /* Send File-Header packet */
        KM_SA,                  /* Send Attributes packet */
        KM_SDW,                 /* Send File-Data packet (windowing) */
        KM_SZ,                  /* Send EOF packet */
        KM_SB,                  /* Send Break (EOT) packet */
        KM_R,                   /* Wait for Send-Init packet */
        KM_RF,                  /* Wait for File-Header packet */
        KM_RDW                  /* Wait for File-Data (windowing) */

} STATE;

struct session_parameters {
        unsigned char MARK;
        unsigned int MAXL;
        unsigned int TIME;
        unsigned int NPAD;
        unsigned char PADC;
        unsigned char EOL;
        unsigned char QCTL;
        unsigned char QBIN;
        unsigned char CHKT;
        unsigned char REPT;
        unsigned int CAPAS;
        unsigned int WINDO;
        unsigned int MAXLX1;
        unsigned int MAXLX2;
        unsigned int WHATAMI;
        Q_BOOL attributes;
        Q_BOOL windowing;
        Q_BOOL long_packets;
        Q_BOOL streaming;
        unsigned int WINDO_in;
        unsigned int WINDO_out;
};
static struct session_parameters local_parms;
static struct session_parameters remote_parms;
static struct session_parameters session_parms;

typedef enum {
        K_ACCESS_NEW,
        K_ACCESS_SUPERSEDE,
        K_ACCESS_APPEND,
        K_ACCESS_WARN
} K_ACCESS;

/* The local status variables for a single transferring file */
struct KERMIT_STATUS {
        STATE state;                    /* INIT, COMPLETE, ABORT, etc. */

        int check_type;                 /*
                                         * 1 : 6-bit checksum
                                         * 2 : 12-bit checksum
                                         * 3 : CRC16
                                         * 12 : 12-bit checksum (B)
                                         */

        unsigned long sequence_number;  /* Packet sequence number, NOT modulo */

        Q_BOOL sending;                 /* If true, we are the sender */

        char * file_name;               /* Current filename being transferred */
        unsigned int file_size;         /* Size of file in bytes */
        unsigned int file_size_k;       /* Size of file in k-bytes */
        time_t file_modtime;            /* Modification time of file */
        off_t file_position;            /* Current position */
        FILE * file_stream;             /* Stream pointer to current file */
        mode_t file_protection;         /* File protection */

        off_t outstanding_bytes;        /*
                                         * # bytes that have not yet been ACK'd
                                         * by remote
                                         */

        int block_size;                 /* Block size */
        K_ACCESS access;                /* File access (write) */


        time_t timeout_begin;           /*
                                         * The beginning time for the most
                                         * recent timeout cycle
                                         */

        int timeout_max;                /*
                                         * Total number of timeouts before
                                         * aborting is 5
                                         */

        int timeout_count;              /* Total number of timeouts so far */

        Q_BOOL first_R;                 /*
                                         * Receiving case - first time to enter
                                         * receive_R()
                                         */

        Q_BOOL first_S;                 /*
                                         * Sending case - first time to enter
                                         * send_S()
                                         */

        Q_BOOL first_SB;                /*
                                         * Sending case - first time to enter
                                         * send_SB()
                                         */

        Q_BOOL sent_nak;                /*
                                         * Send the first NAK to start things
                                         * off
                                         */

        Q_BOOL skip_file;               /*
                                         * Skip the current file using the
                                         * method on pg 37 of "The Kermit
                                         * Protocol"
                                         */

        Q_BOOL text_mode;               /* Convert text files to/from CRLF */

        Q_BOOL seven_bit_only;          /* If the channel is 7 bit */

        Q_BOOL do_resend;               /* Support RESEND */

        char file_fullname[FILENAME_SIZE];      /* Full pathname to file */

};
static struct KERMIT_STATUS status = {
        INIT,
        1,
        0,
        Q_FALSE,
        NULL,
        0,
        0,
        0,
        0,
        NULL,
        0,
        0,
        KERMIT_BLOCK_SIZE,
        K_ACCESS_WARN,
        0,
        5,
        0,
        Q_FALSE,
        Q_FALSE,
        Q_FALSE,
        Q_FALSE,
        Q_FALSE,
        Q_FALSE,
        Q_FALSE,
        Q_FALSE
};

/* The list of files to upload */
static struct file_info * upload_file_list;

/* The current entry in upload_file_list being sent */
static int upload_file_list_i;

/*
 * The path to download to.  Note download_path is Xstrdup'd TWICE:
 * once HERE and once more on the progress dialog.  The q_program_state
 * transition to Q_STATE_CONSOLE is what Xfree's the copy in the progress
 * dialog.  This copy is Xfree'd in kermit_stop().
 */
static char * download_path = NULL;

/*
 * Every bit of Kermit data goes out as packets.  So much nicer than
 * other protocols...
 */
struct kermit_packet {
        Q_BOOL parsed_ok;               /* Set to true if packet is OK */
        int seq;                        /* SEQ.  The SEQ for SEND-INIT is 0 */
        PACKET_TYPE type;               /* From packet_types[] */

        int length;                     /*
                                         * length from the LEN byte to the
                                         * first CRC byte, exclusive.
                                         */

        Q_BOOL long_packet;             /* If true, this is a long packet */

        unsigned char * data;
        unsigned int data_n;
        unsigned int data_max;
};
/* The currently-processing input and output packet */
static struct kermit_packet input_packet;
static struct kermit_packet output_packet;

/* Input buffer used to collect a complete packet before processing it */
static unsigned char packet_buffer[KERMIT_BLOCK_SIZE * 2];
static int packet_buffer_n;

/*
 * Full duplex sliding windows support.  EVERY transfer operates with a
 * window size of 1.  If windowing is negotiated, the window size may get
 * bigger.
 */
static unsigned int input_window_begin;
static unsigned int input_window_i;
static unsigned int input_window_n;
static unsigned int output_window_begin;
static unsigned int output_window_i;
static unsigned int output_window_n;

struct kermit_packet_serial {
        unsigned int seq;               /* SEQ */

        unsigned int try_count;         /*
                                         * # of times this packet has
                                         * been sent
                                         */

        Q_BOOL acked;                   /* Packet was sent/received OK */

        PACKET_TYPE type;               /* From packet_types[] */

        unsigned char * data;
        unsigned int data_n;
};
static struct kermit_packet_serial * input_window = NULL;
static struct kermit_packet_serial * output_window = NULL;

/* ------------------------------------------------------------------------ */
/* Defaults --------------------------------------------------------------- */
/* ------------------------------------------------------------------------ */

/*
 * Set the session parameters we normally go in with.
 */
static void set_default_session_parameters(struct session_parameters * parms) {
        parms->MARK = C_SOH;
        parms->MAXL = 80;
        parms->TIME = 5;
        parms->NPAD = 0;
        parms->PADC = 0x00;
        parms->EOL = C_CR;
        parms->QCTL = '#';
        if (status.seven_bit_only == Q_TRUE) {
                /*
                 * 7 bit channel: do 8th bit prefixing
                 */
                parms->QBIN = '&';
        } else {
                /*
                 * 8 bit channel: prefer no prefixing
                 */
                parms->QBIN = 'Y';
        }
        parms->CHKT = '3';
        parms->REPT = '~';              /* Generally '~' */

        /* 0x10 - Can do RESEND */
        /* 0x08 - Can accept Attribute packets */
        /* 0x02 - Can send/receive long packets */
        /* 0x04 - Can do sliding windows */
        parms->CAPAS = 0x10 | 0x08 | 0x04;
        parms->WINDO = 30;
        parms->WINDO_in = 1;
        parms->WINDO_out = 1;
        parms->MAXLX1 = KERMIT_BLOCK_SIZE / 95;
        parms->MAXLX2 = KERMIT_BLOCK_SIZE % 95;
        parms->attributes = Q_TRUE;
        parms->windowing = Q_TRUE;
        if (q_status.kermit_long_packets == Q_TRUE) {
                parms->long_packets = Q_TRUE;
                parms->CAPAS |= 0x02;
        } else {
                parms->long_packets = Q_FALSE;
        }
        if (q_status.kermit_streaming == Q_TRUE) {
                parms->streaming = Q_TRUE;
                parms->WHATAMI = 0x28;          /* Can do streaming */
        } else {
                parms->streaming = Q_FALSE;
                parms->WHATAMI = 0x00;          /* No streaming */
        }
} /* ---------------------------------------------------------------------- */

/* CRC16 CODE ------------------------------------------------------------- */

/*
 * KAL - This CRC16 routine is modeled after "The Working Programmer's Guide
 * To Serial Protocols" by Tim Kientzle, Coriolis Group Books.
 *
 * This function calculates the CRC used by the Kermit Protocol
 */
#define CRC16 0x8408 /* CRC polynomial */

static short crc_16_tab[256];

static void makecrc(void) {
        int i, j, crc;
        for (i = 0; i< 256; i++) {
                crc = i;
                for (j = 0; j < 8; j++) {
                        crc = (crc >> 1) ^ ((crc & 1) ? CRC16 : 0);
                }
                crc_16_tab[i] = crc & 0xFFFF;
        }
} /* ---------------------------------------------------------------------- */

static short compute_crc16(const unsigned char * ptr, int count) {
        unsigned char ch;
        int i;
        unsigned long crc = 0;
        for (i = 0; i < count; i++) {
                ch = ptr[i];
                if (status.seven_bit_only == Q_TRUE) {
                        ch &= 0x7F;
                }
#ifdef DEBUG_KERMIT
                /* fprintf(DEBUG_FILE_HANDLE, "CRC16 '%c' (%02x) CRC %04lx\n",
                        ch, ch, crc); */
#endif /* DEBUG_KERMIT */
                crc = crc_16_tab[(crc ^ ch) & 0xFF] ^ (crc >> 8);
                crc &= 0xFFFF;
        }
        return crc & 0xFFFF;
} /* ---------------------------------------------------------------------- */

#if 0

/*
 * This is the function from the Kermit docs.  It works fine, but I'll keep
 * the Serial Protocols book version.
 */
static short compute_crc16_2(const unsigned char * ptr, int count) {
        unsigned char ch;
        int i;
        unsigned long crc = 0;
        unsigned long q = 0;
        for (i = 0; i < count; i++) {
                ch = ptr[i];
                if (status.seven_bit_only == Q_TRUE) {
                        ch &= 0x7F;
                }
                q = (crc ^ ch) & 0x0F;
                crc = (crc >> 4) ^ (q * 4225);
                q = (crc ^ (ch >> 4)) & 0x0F;
                crc = (crc >> 4) ^ (q * 4225);
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "CRC16 '%c' (%02x) CRC %04lx\n",
                        ch, ch, (crc & 0xFFFF));
#endif /* DEBUG_KERMIT */
        }
        return crc & 0xFFFF;
} /* ---------------------------------------------------------------------- */

#endif

/* CRC16 CODE ------------------------------------------------------------- */

static unsigned char compute_checksum(const unsigned char * ptr, int count) {
        unsigned char sum = 0;
        int i;
        for (i = 0; i < count; i++) {
                if (status.seven_bit_only == Q_TRUE) {
                        sum += (ptr[i] & 0x7F);
                } else {
                        sum += ptr[i];
                }
        }
        return (sum + (sum & 0xC0)/0x40) & 0x3F;
} /* ---------------------------------------------------------------------- */

static short compute_checksum2(const unsigned char * ptr, int count) {
        short sum = 0;
        int i;
        for (i = 0; i < count; i++) {
                if (status.seven_bit_only == Q_TRUE) {
                        sum += (ptr[i] & 0x7F);
                } else {
                        sum += ptr[i];
                }
        }
        return (sum & 0x0FFF);
} /* ---------------------------------------------------------------------- */

/* ------------------------------------------------------------------------ */
/* Progress dialog -------------------------------------------------------- */
/* ------------------------------------------------------------------------ */

/*
 * Statistics: reset for a new file
 */
static void stats_new_file(const char * filename, const int filesize) {
        char * basename_arg;
        char * dirname_arg;

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "stats_new_file %s %d\n",
                filename, filesize);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        /* Clear per-file statistics */
        q_transfer_stats.blocks_transfer = 0;
        q_transfer_stats.bytes_transfer = 0;
        q_transfer_stats.error_count = 0;
        set_transfer_stats_last_message("");
        q_transfer_stats.bytes_total = filesize;
        q_transfer_stats.blocks = filesize / KERMIT_BLOCK_SIZE;
        if ((filesize % KERMIT_BLOCK_SIZE) > 0) {
                q_transfer_stats.blocks++;
        }

        /* Note that basename and dirname modify the arguments */
        basename_arg = Xstrdup(filename, __FILE__, __LINE__);
        dirname_arg = Xstrdup(filename, __FILE__, __LINE__);
        set_transfer_stats_filename(basename(basename_arg));
        set_transfer_stats_pathname(dirname(dirname_arg));
        Xfree(basename_arg, __FILE__, __LINE__);
        Xfree(dirname_arg, __FILE__, __LINE__);

        q_transfer_stats.state = Q_TRANSFER_STATE_TRANSFER;
        q_screen_dirty = Q_TRUE;
        time(&q_transfer_stats.file_start_time);

        /* Log it */
        if (status.sending == Q_TRUE) {
                qlog(_("UPLOAD: sending file %s/%s, %d bytes\n"),
                        q_transfer_stats.pathname,
                        q_transfer_stats.filename, filesize);
        } else {
                qlog(_("DOWNLOAD: receiving file %s/%s, %d bytes\n"),
                        q_transfer_stats.pathname,
                        q_transfer_stats.filename, filesize);
        }
} /* ---------------------------------------------------------------------- */

/*
 * Statistics: reset for a new file
 */
static void stats_increment_blocks() {
        q_transfer_stats.block_size = status.block_size;
        q_transfer_stats.blocks_transfer = status.file_position / session_parms.MAXL;
        q_transfer_stats.blocks = status.file_size / session_parms.MAXL;
        if ((status.file_position % session_parms.MAXL) > 0) {
                q_transfer_stats.blocks_transfer++;
        }

        q_screen_dirty = Q_TRUE;
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
} /* ---------------------------------------------------------------------- */

/* Needed by setup_for_next_file() */
static void send_file_header();
static void error_packet(char * message);

/*
 * Initialize a new file
 */
static Q_BOOL setup_for_next_file() {
        char * basename_arg;
        int i;
        int rc;
        char ch;

        /* Reset our dynamic variables */
        if (status.file_stream != NULL) {
                fclose(status.file_stream);
        }
        status.file_stream = NULL;
        if (status.file_name != NULL) {
                Xfree(status.file_name, __FILE__, __LINE__);
        }
        status.file_name = NULL;

        if (upload_file_list[upload_file_list_i].name == NULL) {
                /* Special case: the terminator block */
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: No more files (name='%s')\n",
                        upload_file_list[upload_file_list_i].name);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                /*
                 * Let's keep all the information the same, just increase the
                 * total bytes.
                 */
                q_transfer_stats.batch_bytes_transfer = q_transfer_stats.batch_bytes_total;
                q_screen_dirty = Q_TRUE;

                /* We're done */
                status.state = KM_SB;
                return Q_TRUE;
        }

        /* Get the file's modification time */
        status.file_modtime = upload_file_list[upload_file_list_i].fstats.st_mtime;
        status.file_size = upload_file_list[upload_file_list_i].fstats.st_size;

        /* Get the file's protection */
        status.file_protection = upload_file_list[upload_file_list_i].fstats.st_mode;

        /* Open the file */
        if ((status.file_stream = fopen(upload_file_list[upload_file_list_i].name, "rb")) == NULL) {
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "ERROR: Unable to open file %s: %s (%d)\n",
                        upload_file_list[upload_file_list_i].name,
                        strerror(errno), errno);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                status.state = ABORT;
                stop_file_transfer(Q_TRANSFER_STATE_ABORT);
                set_transfer_stats_last_message(_("DISK I/O ERROR"));
                error_packet("Disk I/O error");
                return Q_FALSE;
        }

        /* Text-mode checking */
        status.text_mode = Q_TRUE;
        if (q_status.kermit_uploads_force_binary == Q_TRUE) {
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "setup_for_next_file() force binary mode\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                status.text_mode = Q_FALSE;
        } else {
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "setup_for_next_file() check for binary file\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                /* Seek to the beginning */
                fseek(status.file_stream, 0, SEEK_SET);

                for (i = 0; i < 1024; i++) {
                        rc = fread(&ch, 1, 1, status.file_stream);
                        if (rc < 0) {
                                /* Uh-oh */
                                status.state = ABORT;
                                set_transfer_stats_last_message(_("DISK I/O ERROR"));
                                stop_file_transfer(Q_TRANSFER_STATE_ABORT);
                                error_packet("Disk I/O error");
                                return Q_FALSE;
                        } else if ((rc < 1) || (rc == 0)) {
                                /* Last byte */
                                break;
                        } else {
                                /* Read 1 byte successfully */
                        }
                        if ((ch & 0x80) != 0) {
                                /* Binary file */
                                status.text_mode = Q_FALSE;
                        }
                }

                /* Seek to the beginning */
                fseek(status.file_stream, 0, SEEK_SET);

#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "setup_for_next_file() ASCII FILE: %s\n",
                        (status.text_mode == Q_TRUE ? "true" : "false"));
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
        }

        /* Note that basename and dirname modify the arguments */
        basename_arg = Xstrdup(upload_file_list[upload_file_list_i].name, __FILE__, __LINE__);

        if (status.file_name != NULL) {
                Xfree(status.file_name, __FILE__, __LINE__);
        }
        status.file_name = Xstrdup(basename(basename_arg), __FILE__, __LINE__);

        /* Update the stats */
        stats_new_file(upload_file_list[upload_file_list_i].name,
                upload_file_list[upload_file_list_i].fstats.st_size);

        /* Free the copies passed to basename() and dirname() */
        Xfree(basename_arg, __FILE__, __LINE__);

        /* Reset the sent count */
        status.file_position = 0;

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "UPLOAD set up for new file %s (%lu bytes)...\n",
                upload_file_list[upload_file_list_i].name,
                (long int)upload_file_list[upload_file_list_i].fstats.st_size);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        /* Update stuff if this is the second file */
        if (status.state != ABORT) {
                /* Update main status state */
                q_transfer_stats.state = Q_TRANSFER_STATE_TRANSFER;

                /* Move to new state */
                set_transfer_stats_last_message(_("FILE HEADER"));
                send_file_header();
                status.state = KM_SF;
        }

        return Q_TRUE;
} /* ---------------------------------------------------------------------- */

/*
 * Reset timer
 */
static void reset_timer() {
        time(&status.timeout_begin);
} /* ---------------------------------------------------------------------- */

/*
 * Check for a timeout.
 */
static Q_BOOL check_timeout() {
        time_t now;
        time(&now);

        if (    (session_parms.streaming == Q_TRUE) &&
                ((status.state == KM_RDW) || (status.state == KM_SDW))
        ) {
                /*
                 * Do not do timeout processing during a streaming transfer.
                 */
                reset_timer();
                return Q_FALSE;
        }

        if (now - status.timeout_begin >= session_parms.TIME) {
                /* Timeout */
                status.timeout_count++;
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: Timeout #%d\n",
                        status.timeout_count);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                if (status.timeout_count >= status.timeout_max) {
                        /* ABORT */
                        stats_increment_errors(_("TOO MANY TIMEOUTS, TRANSFER CANCELLED"));
                        stop_file_transfer(Q_TRANSFER_STATE_ABORT);
                        status.state = ABORT;
                        error_packet("Too many timeouts");
                } else {
                        /* Timeout */
                        stats_increment_errors(_("TIMEOUT"));
                }

                /* Reset timeout */
                reset_timer();
                return Q_TRUE;
        }

        /* No timeout yet */
        return Q_FALSE;
} /* ---------------------------------------------------------------------- */

/* Forward declaration for open_receive_file() */
static void ack_packet_parm(char * parm, int parm_n);

/*
 * Delayed file open function.  We shouldn't open the file until we've seen
 * BOTH the File-Header and all Attributes packets if they are coming.
 *
 * Returns Q_TRUE if the file is opened and ready for writing.
 * Returns Q_FALSE and aborts transfer if there is an error.
 *
 * Note this function also calls ack_packet_parm() with different parameters
 * based on the do_resend option.
 */
static Q_BOOL open_receive_file() {
        struct stat fstats;
        int rc;
        int i;
        Q_BOOL file_exists = Q_FALSE;
        Q_BOOL need_new_file = Q_FALSE;
        unsigned int file_size = 0;
        char buffer[34];

        /* We only get here once. */
        assert(status.file_stream == NULL);

        /* If this a RESEND, we must be in binary mode */
        if (status.do_resend == Q_TRUE) {
                if (status.text_mode == Q_TRUE) {
                        ack_packet_parm("N+", 2);
                        return Q_FALSE;
                }
        }

        /* Open the file */
        sprintf(status.file_fullname, "%s/%s", download_path, status.file_name);
        rc = stat(status.file_fullname, &fstats);
        if (rc < 0) {
                if (errno == ENOENT) {
                        /* Creating the file */
                        status.file_position = 0;
                        set_transfer_stats_last_message(_("FILE HEADER"));
                } else {
                        /* Uh-oh */
                        status.state = ABORT;
                        set_transfer_stats_last_message(_("DISK I/O ERROR"));
                        stop_file_transfer(Q_TRANSFER_STATE_ABORT);
                        error_packet("Disk I/O error");
                        return Q_FALSE;
                }
        } else {
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "open_receive_file() file exists, checking ACCESS...\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                file_exists = Q_TRUE;
                if (status.file_size_k > 0) {
                        file_size = status.file_size_k * 1024;
                }
                if (status.file_size > 0) {
                        file_size = status.file_size;
                }

                if (status.access == K_ACCESS_NEW) {
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "open_receive_file() K_ACCESS_NEW\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                        /* New file */
                        need_new_file = Q_TRUE;

                } else if (status.access == K_ACCESS_SUPERSEDE) {
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "open_receive_file() K_ACCESS_SUPERSEDE\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                        /* Overwrite file */
                        /* Not supported */
                        need_new_file = Q_TRUE;
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "open_receive_file() refuse to SUPERSEDE\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                } else if (status.access == K_ACCESS_WARN) {
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "open_receive_file() K_ACCESS_WARN\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                        if (status.do_resend == Q_TRUE) {
#ifdef DEBUG_KERMIT
                                fprintf(DEBUG_FILE_HANDLE, "open_receive_file() RESEND\n");
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                                /* This is a crash recovery */

                                /* Append to end of file */
                                status.file_position = fstats.st_size;

                        } else {
#ifdef DEBUG_KERMIT
                                fprintf(DEBUG_FILE_HANDLE, "open_receive_file() rename file\n");
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                                /* Rename file */
                                need_new_file = Q_TRUE;
                        }

                } else if (status.access == K_ACCESS_APPEND) {
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "open_receive_file() K_ACCESS_APPEND\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                        /* Supposed to append */
                        status.file_position = fstats.st_size;

                        if (file_size < fstats.st_size) {
                                /*
                                 * Uh-oh, this is obviously a new file
                                 * because it is smaller than the file on
                                 * disk.
                                 */
                                need_new_file = Q_TRUE;
                                status.file_position = 0;
#ifdef DEBUG_KERMIT
                                fprintf(DEBUG_FILE_HANDLE, "open_receive_file() smaller file already exists\n");
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                        } else if (file_size > 0) {
                                /*
                                 * Looks like a crash recovery case
                                 */
#ifdef DEBUG_KERMIT
                                fprintf(DEBUG_FILE_HANDLE, "open_receive_file(): filename exists, might need crash recovery...\n");
#endif /* DEBUG_KERMIT */
                                set_transfer_stats_last_message(_("APPEND"));

                        } else {
                                /*
                                 * 0-length file or no Attributes
                                 */
#ifdef DEBUG_KERMIT
                                fprintf(DEBUG_FILE_HANDLE, "open_receive_file(): 0-length file or no Attributes\n");
#endif /* DEBUG_KERMIT */
                        }
                }
        }

        if (need_new_file == Q_TRUE) {
                /* Guarantee we get a new file */
                file_exists = Q_FALSE;

                for (i = 0 ; ; i++) {
                        /* Change the filename */
                        sprintf(status.file_fullname, "%s/%s.%04d",
                                download_path, status.file_name, i);

                        rc = stat(status.file_fullname, &fstats);
                        if (rc < 0) {
                                if (errno == ENOENT) {
                                        /* Creating the file */
                                        status.file_position = 0;

#ifdef DEBUG_KERMIT
                                        fprintf(DEBUG_FILE_HANDLE, "open_receive_file(): prevent overwrite, new filename = %s\n", status.file_fullname);
#endif /* DEBUG_KERMIT */
                                        break;
                                } else {
                                        /* Uh-oh */
                                        status.state = ABORT;
                                        set_transfer_stats_last_message(_("DISK I/O ERROR"));
                                        stop_file_transfer(Q_TRANSFER_STATE_ABORT);
                                        error_packet("Disk I/O error");
                                        return Q_FALSE;
                                }
                        }
                } /* for (i = 0 ; ; i++) */
        } /* if (need_new_file == Q_TRUE) */

        if (file_exists == Q_TRUE) {
                status.file_stream = fopen(status.file_fullname, "r+b");
        } else {
                status.file_stream = fopen(status.file_fullname, "w+b");
        }

        if (status.file_stream == NULL) {
                /* Uh-oh */
                status.state = ABORT;
                set_transfer_stats_last_message(_("CANNOT CREATE FILE"));
                stop_file_transfer(Q_TRANSFER_STATE_ABORT);
                error_packet("Disk I/O error: cannot create file");
                return Q_FALSE;
        }

        /* Seek to the end of the file.  We do this for every case... */
        fseek(status.file_stream, 0, SEEK_END);

        if (input_packet.type == P_KATTRIBUTES) {
                /*
                 * The sender sent a File Attributes packet.  If this is a
                 * RESEND case, seek to the end and tell the sender how much
                 * we have.
                 */
                if (status.do_resend == Q_TRUE) {
                        snprintf(buffer, sizeof(buffer) - 1, "1_%lu",
                                status.file_position);
                        buffer[1] = strlen(buffer) - 2 + 32;
                        ack_packet_parm(buffer, strlen(buffer));
                } else {
                        /* Accept the file */
                        ack_packet_parm("Y", 1);
                }
        } else {
                /* The sender did not send a File Attributes packet */

                /* Nothing to do here. */
        }

        /* Save time from remote server */
        if (status.file_modtime == -1) {
                /* Right now */
                time(&status.file_modtime);
        }

        /* Update progress display */
        if ((status.file_size_k > 0) && (status.file_size <= 0)) {
                /* Use file_size_k */
                stats_new_file(status.file_fullname, status.file_size_k * 1024);
        } else {
                stats_new_file(status.file_fullname, status.file_size);
        }
        q_transfer_stats.bytes_transfer = status.file_position;

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "open_receive_file(): all OK\n");
#endif /* DEBUG_KERMIT */

        /* All OK */
        return Q_TRUE;
} /* ---------------------------------------------------------------------- */

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */
/* Encoding layer --------------------------------------------------------- */
/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

static unsigned char kermit_tochar(unsigned char ch) {
        return (ch + 32);
} /* ---------------------------------------------------------------------- */

static unsigned char kermit_unchar(unsigned char ch) {
        return (ch - 32);
} /* ---------------------------------------------------------------------- */

static unsigned char kermit_ctl(unsigned char ch) {
        return (ch ^ 0x40);
} /* ---------------------------------------------------------------------- */

static Q_BOOL decode_data_field(PACKET_TYPE type, unsigned char * input,
        const unsigned int input_n, unsigned char ** output,
        unsigned int * output_n, unsigned int * output_max) {

        int i;
        unsigned int begin;
        unsigned int data_n = 0;
        unsigned char ch;
        Q_BOOL prefix_ctrl = Q_FALSE;
        Q_BOOL prefix_8bit = Q_FALSE;
        Q_BOOL prefix_rept = Q_FALSE;
        int repeat_count = 1;
        unsigned char output_ch = 0;
        Q_BOOL do_output_ch = Q_FALSE;

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "decode_data_field() %d %s input_n %d output_max %d\n", type, packet_type_chars[type].description, input_n, *output_max);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        if ((type == P_KDATA) && (status.state == KM_RDW)) {
                if (status.file_stream == NULL) {
                        /* Need to open the file */
                        open_receive_file();
                }
        }

        for (begin = 0; begin < input_n; begin++) {

                /* Output a previously-escaped character */
                if (do_output_ch == Q_TRUE) {
                        for (i = 0; i < repeat_count; i++) {
                                if ((type == P_KDATA) &&
                                        (status.state == KM_RDW) &&
                                        (status.text_mode == Q_TRUE) &&
                                        (output_ch == C_CR)
                                ) {
                                        /* Strip CR's */
                                } else {
                                        if (data_n == *output_max) {
                                                /*
                                                 * Grow the output buffer to
                                                 * handle what we've got.
                                                 */
                                                *output_max *= 2;
                                                *output = (unsigned char *)Xrealloc(*output,
                                                        *output_max,
                                                        __FILE__,
                                                        __LINE__);
#ifdef DEBUG_KERMIT
                                                fprintf(DEBUG_FILE_HANDLE, "decode_data_field() resize output_max to %d\n", *output_max);
                                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                                        }
                                        (*output)[data_n] = output_ch;
                                        data_n++;
                                }
                        }
                        repeat_count = 1;
                        do_output_ch = Q_FALSE;
                }

                /* Pull next character from input */
                ch = input[begin];

                if ((input_packet.seq == 0) &&
                        ((type == P_KACK) || (type == P_KSINIT))
                ) {
                        /*
                         * Special case: do not do any prefix handling for
                         * the Send-Init or its corresponding ACK packet.
                         */
#ifdef DEBUG_KERMIT_VERBOSE
                        fprintf(DEBUG_FILE_HANDLE, "SEND-INIT --> ch '%c' %02x\n", ch, ch);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT_VERBOSE */
                        if (data_n == *output_max) {
                                /*
                                 * Grow the output buffer to handle what
                                 * we've got.
                                 */
                                *output_max *= 2;
                                *output = (unsigned char *)Xrealloc(*output,
                                        *output_max,
                                        __FILE__,
                                        __LINE__);
#ifdef DEBUG_KERMIT
                                fprintf(DEBUG_FILE_HANDLE, "decode_data_field() resize output_max to %d\n", *output_max);
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                        }
                        (*output)[data_n] = ch;
                        data_n++;
                        continue;
                }

                if (type == P_KATTRIBUTES) {
                        /*
                         * Special case: do not do any prefix handling
                         * for the Attributes packet.
                         */
#ifdef DEBUG_KERMIT_VERBOSE
                        fprintf(DEBUG_FILE_HANDLE, "ATTRIBUTES --> ch '%c' %02x\n", ch, ch);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT_VERBOSE */
                        if (data_n == *output_max) {
                                /*
                                 * Grow the output buffer to handle what
                                 * we've got.
                                 */
                                *output_max *= 2;
                                *output = (unsigned char *)Xrealloc(*output,
                                        *output_max,
                                        __FILE__,
                                        __LINE__);
#ifdef DEBUG_KERMIT
                                fprintf(DEBUG_FILE_HANDLE, "decode_data_field() resize output_max to %d\n", *output_max);
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                        }
                        (*output)[data_n] = ch;
                        data_n++;
                        continue;
                }

#ifdef DEBUG_KERMIT_VERBOSE
                fprintf(DEBUG_FILE_HANDLE, "decode_data_field() ch '%c' %02x ctrl %d 8bit %d repeat %d repeat_count %d\n", ch, ch,
                        prefix_ctrl, prefix_8bit, prefix_rept, repeat_count);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT_VERBOSE */

                if ((session_parms.REPT != ' ') && (ch == session_parms.REPT)) {
                        if ((prefix_ctrl == Q_TRUE) && (prefix_8bit == Q_TRUE)) {
                                /* Escaped 8-bit REPT */
                                output_ch = session_parms.REPT | 0x80;
                                do_output_ch = Q_TRUE;

#ifdef DEBUG_KERMIT_VERBOSE
                                fprintf(DEBUG_FILE_HANDLE, " - escaped 8-bit REPT -\n");
                                fprintf(DEBUG_FILE_HANDLE, "    '%c' %02x --> ch '%c' %02x\n",
                                        input[begin], input[begin],
                                        session_parms.REPT | 0x80,
                                        session_parms.REPT | 0x80);
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT_VERBOSE */

                                prefix_ctrl = Q_FALSE;
                                prefix_8bit = Q_FALSE;
                                prefix_rept = Q_FALSE;
                                continue;
                        }

                        if (prefix_ctrl == Q_TRUE) {
                                /* Escaped REPT */
                                output_ch = session_parms.REPT;
                                do_output_ch = Q_TRUE;

#ifdef DEBUG_KERMIT_VERBOSE
                                fprintf(DEBUG_FILE_HANDLE, " - escaped REPT -\n");
                                fprintf(DEBUG_FILE_HANDLE, "    '%c' %02x --> ch '%c' %02x\n",
                                        input[begin], input[begin],
                                        session_parms.REPT, session_parms.REPT);
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT_VERBOSE */

                                prefix_ctrl = Q_FALSE;
                                prefix_rept = Q_FALSE;
                                continue;
                        }

                        if (prefix_rept == Q_TRUE) {
                                repeat_count = kermit_unchar(session_parms.REPT);
#ifdef DEBUG_KERMIT_VERBOSE
                                fprintf(DEBUG_FILE_HANDLE, " - REPT count %d \n", repeat_count);
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT_VERBOSE */
                                prefix_rept = Q_FALSE;
                                continue;
                        }

                        /* Flip rept bit */
                        prefix_rept = Q_TRUE;
                        do_output_ch = Q_FALSE;
                        continue;
                }

                if (prefix_rept == Q_TRUE) {
                        repeat_count = kermit_unchar(ch);
#ifdef DEBUG_KERMIT_VERBOSE
                        fprintf(DEBUG_FILE_HANDLE, " - REPT count %d \n",
                                repeat_count);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT_VERBOSE */
                        prefix_rept = Q_FALSE;
                        continue;
                }

                if (ch == remote_parms.QCTL) {
                        if ((prefix_8bit == Q_TRUE) &&
                                (prefix_ctrl == Q_TRUE)
                        ) {

                                /* 8-bit QCTL */
                                output_ch = remote_parms.QCTL | 0x80;
                                do_output_ch = Q_TRUE;

#ifdef DEBUG_KERMIT_VERBOSE
                                fprintf(DEBUG_FILE_HANDLE, " - 8-bit QCTL -\n");
                                fprintf(DEBUG_FILE_HANDLE, "    '%c' %02x --> ch '%c' %02x\n",
                                        input[begin], input[begin],
                                        remote_parms.QCTL | 0x80,
                                        remote_parms.QCTL | 0x80);
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT_VERBOSE */

                                prefix_ctrl = Q_FALSE;
                                prefix_8bit = Q_FALSE;
                                continue;
                        }

                        if (prefix_ctrl == Q_TRUE) {
                                /* Escaped QCTL */
                                output_ch = remote_parms.QCTL;
                                do_output_ch = Q_TRUE;

#ifdef DEBUG_KERMIT_VERBOSE
                                fprintf(DEBUG_FILE_HANDLE, " - escaped QCTL -\n");
                                fprintf(DEBUG_FILE_HANDLE, "    '%c' %02x --> ch '%c' %02x\n",
                                        input[begin], input[begin],
                                        remote_parms.QCTL, remote_parms.QCTL);
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT_VERBOSE */

                                prefix_ctrl = Q_FALSE;
                                continue;
                        }
                        /* Flip ctrl bit */
                        prefix_ctrl = Q_TRUE;
                        do_output_ch = Q_FALSE;
                        continue;
                }

                if ((session_parms.QBIN != ' ') && (ch == session_parms.QBIN)) {
                        if ((prefix_8bit == Q_TRUE) &&
                                (prefix_ctrl == Q_FALSE)
                        ) {

                                /* This is an error */
#ifdef DEBUG_KERMIT_VERBOSE
                                fprintf(DEBUG_FILE_HANDLE, " - ERROR QBIN QBIN -\n");
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT_VERBOSE */
                                return Q_FALSE;
                        }

                        if ((prefix_8bit == Q_TRUE) &&
                                (prefix_ctrl == Q_TRUE)
                        ) {
                                /* 8-bit QBIN */
                                output_ch = session_parms.QBIN | 0x80;
                                do_output_ch = Q_TRUE;

#ifdef DEBUG_KERMIT_VERBOSE
                                fprintf(DEBUG_FILE_HANDLE, " - 8bit QBIN -\n");
                                fprintf(DEBUG_FILE_HANDLE, "    '%c' %02x --> ch '%c' %02x\n",
                                        input[begin], input[begin],
                                        session_parms.QBIN | 0x80,
                                        session_parms.QBIN | 0x80);
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT_VERBOSE */
                                prefix_ctrl = Q_FALSE;
                                prefix_8bit = Q_FALSE;
                                continue;
                        }

                        if (prefix_ctrl == Q_TRUE) {
                                /* Escaped QBIN */
                                output_ch = session_parms.QBIN;
                                do_output_ch = Q_TRUE;

#ifdef DEBUG_KERMIT_VERBOSE
                                fprintf(DEBUG_FILE_HANDLE, " - escaped QBIN -\n");
                                fprintf(DEBUG_FILE_HANDLE, "    '%c' %02x --> ch '%c' %02x\n",
                                        input[begin], input[begin],
                                        session_parms.QBIN, session_parms.QBIN);
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT_VERBOSE */

                                prefix_ctrl = Q_FALSE;
                                continue;
                        }

                        /* Flip 8bit bit */
                        prefix_8bit = Q_TRUE;
                        do_output_ch = Q_FALSE;
                        continue;
                }

                /* Regular character */
                if (prefix_ctrl == Q_TRUE) {
                        /*
                         * Control prefix can quote anything, so make sure to
                         * UN-ctl only for control characters.
                         */
                        if (((kermit_ctl(ch) & 0x7F) < 0x20) ||
                                ((kermit_ctl(ch) & 0x7F) == 0x7F)
                        ) {
                                ch = kermit_ctl(ch);
                        }
                        prefix_ctrl = Q_FALSE;
                }
                if (prefix_8bit == Q_TRUE) {
                        ch |= 0x80;
                        prefix_8bit = Q_FALSE;
                }

#ifdef DEBUG_KERMIT_VERBOSE
                fprintf(DEBUG_FILE_HANDLE, "    '%c' %02x --> ch '%c' %02x\n",
                        input[begin], input[begin], ch, ch);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT_VERBOSE */

                for (i = 0; i < repeat_count; i++) {
                        if ((type == P_KDATA) &&
                                (status.state == KM_RDW) &&
                                (status.text_mode == Q_TRUE) &&
                                (ch == C_CR)
                        ) {
                                /* Strip CR's */
                        } else {
                                if (data_n == *output_max) {
                                        /*
                                         * Grow the output buffer to handle
                                         * what we've got.
                                         */
                                        *output_max *= 2;
                                        *output = (unsigned char *)Xrealloc(*output,
                                                *output_max,
                                                __FILE__,
                                                __LINE__);
#ifdef DEBUG_KERMIT
                                        fprintf(DEBUG_FILE_HANDLE, "decode_data_field() resize output_max to %d\n", *output_max);
                                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                                }
                                (*output)[data_n] = ch;
                                data_n++;
                        }
                }
                repeat_count = 1;
        } /* for (begin = 0; begin < input_n; begin++) */

        /* Output a previously-escaped character (boundary case) */
        if (do_output_ch == Q_TRUE) {
                for (i = 0; i < repeat_count; i++) {
                        if ((type == P_KDATA) &&
                                (status.state == KM_RDW) &&
                                (status.text_mode == Q_TRUE) &&
                                (output_ch == C_CR)
                        ) {
                                /* Strip CR's */
                        } else {
                                if (data_n == *output_max) {
                                        /*
                                         * Grow the output buffer to handle
                                         * what we've got.
                                         */
                                        *output_max *= 2;
                                        *output = (unsigned char *)Xrealloc(*output,
                                                *output_max,
                                                __FILE__,
                                                __LINE__);
#ifdef DEBUG_KERMIT
                                        fprintf(DEBUG_FILE_HANDLE, "decode_data_field() resize output_max to %d\n", *output_max);
                                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                                }
                                (*output)[data_n] = output_ch;
                                data_n++;
                        }
                }
                repeat_count = 1;
                do_output_ch = Q_FALSE;
        }

        /* Save final result */
        *output_n = data_n;
#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "decode_data_field() output_n = %d bytes:\n", *output_n);
        for (i = 0; i < *output_n; i++) {
                fprintf(DEBUG_FILE_HANDLE, "%02x ", ((*output)[i] & 0xFF));
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        /* Data was OK */
        return Q_TRUE;
} /* ---------------------------------------------------------------------- */

/*
 * Encode one character to output
 */
static int encode_one_byte(unsigned char ch, unsigned int repeat_count,
        unsigned char * output) {

        int i;
        int data_n = 0;
        unsigned char ch7bit = ch & 0x7F;
        Q_BOOL need_qbin = Q_FALSE;
        Q_BOOL need_qctl = Q_FALSE;
        Q_BOOL ch_is_ctl = Q_FALSE;
        unsigned char output_ch = ch;

        /* Repeat count */
        if ((repeat_count > 3) ||
                /* Also force repeat count on spaces with 'B' check type */
                ((status.check_type == 12) && (ch == ' '))) {

                output[data_n] = session_parms.REPT;
                data_n++;
                output[data_n] = kermit_tochar(repeat_count);
                data_n++;
                repeat_count = 1;
        }

        for (i = 0; i < repeat_count; i++) {
                ch7bit = ch & 0x7F;
                need_qbin = Q_FALSE;
                need_qctl = Q_FALSE;
                ch_is_ctl = Q_FALSE;
                output_ch = ch;

                if ((session_parms.QBIN != ' ') && ((ch & 0x80) != 0)) {
                        need_qbin = Q_TRUE;
                }
                if ((session_parms.REPT != ' ') && (ch7bit == session_parms.REPT)) {
                        /* Quoted REPT character */
                        need_qctl = Q_TRUE;
                } else if ((session_parms.QBIN != ' ') && (ch7bit == session_parms.QBIN)) {
                        /* Quoted QBIN character */
                        need_qctl = Q_TRUE;
                } else if (ch7bit == local_parms.QCTL) {
                        /* Quoted QCTL character */
                        need_qctl = Q_TRUE;
                } else if ((ch7bit < 0x20) || (ch7bit == 0x7F)) {
                        /* ctrl character */
                        need_qctl = Q_TRUE;
                        ch_is_ctl = Q_TRUE;
                }
                if (need_qbin == Q_TRUE) {
                        output[data_n] = session_parms.QBIN;
                        data_n++;
                        output_ch = ch7bit;
                }
                if (need_qctl == Q_TRUE) {
                        output[data_n] = local_parms.QCTL;
                        data_n++;
                }
                if (ch_is_ctl == Q_TRUE) {
                        /* Either 7-bit or 8-bit control character */
                        output[data_n] = kermit_ctl(output_ch);
                        data_n++;
                } else {
                        /* Regular character */
                        output[data_n] = output_ch;
                        data_n++;
                }
        }

        return data_n;
} /* ---------------------------------------------------------------------- */

static Q_BOOL encode_data_field(PACKET_TYPE type, unsigned char * input,
        const unsigned int input_n, unsigned char * output,
        unsigned int * output_n) {

#ifdef DEBUG_KERMIT
        int i;
#endif /* DEBUG_KERMIT */
        int rc;
        unsigned int begin = 0;
        unsigned int data_n = 0;
        unsigned char ch;
        unsigned char last_ch = 0;
        int repeat_count = 0;
        Q_BOOL first = Q_TRUE;
        Q_BOOL crlf = Q_FALSE;
        unsigned int data_max = 0;

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "encode_data_field() %d %s %d\n", type, packet_type_chars[type].description, input_n);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        if ((type == P_KDATA) && (status.state == KM_SDW)) {
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "encode_data_field() seek to %lu\n", status.file_position);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                /* Seek to the current file position */
                fseek(status.file_stream, status.file_position, SEEK_SET);
                status.outstanding_bytes = 0;
        }

        for (;;) {

                /* Pull next character from either input or file */

                /* Check for enough space for the next character */
                if (output_packet.long_packet == Q_TRUE) {
                        data_max = session_parms.MAXLX1 * 95 + session_parms.MAXLX2;
                        data_max -= 9;
                } else {
                        data_max = session_parms.MAXL;
                }

                if (data_n >= data_max - 5) {
                        /* No more room in destination */
                        break;
                }

                /*
                 * Check for enough space for the next character -
                 * include extra for the LF -> CRLF conversion.
                 */
                if ((status.text_mode == Q_TRUE) &&
                        (data_n >= session_parms.MAXL - 5 - 2)
                ) {
                        /* No more room in destination */
                        break;
                }

                if (crlf == Q_TRUE) {
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "    INSERT LF\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                        ch = C_LF;
                } else {

                        if ((type == P_KDATA) && (status.state == KM_SDW)) {
                                rc = fread(&ch, 1, 1, status.file_stream);
                                if (rc < 0) {
                                        /* Uh-oh */
                                        status.state = ABORT;
                                        set_transfer_stats_last_message(_("DISK I/O ERROR"));
                                        stop_file_transfer(Q_TRANSFER_STATE_ABORT);
                                        error_packet("Disk I/O error");
                                        return Q_FALSE;
                                } else if ((rc < 1) || (rc == 0)) {
                                        /* Last packet */
#ifdef DEBUG_KERMIT
                                        fprintf(DEBUG_FILE_HANDLE, "      - EOF -\n");
                                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                                        break;
                                } else {
                                        /* Read 1 byte successfully */
                                }
                        } else {
                                if (begin == input_n) {
#ifdef DEBUG_KERMIT
                                        fprintf(DEBUG_FILE_HANDLE, "      - end of input, break -\n");
                                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                                        /* No more characters to read */
                                        break;
                                }
                                ch = input[begin];
                                begin++;
                        }
                        status.outstanding_bytes++;
                }

                if (    (output_packet.type == P_KSINIT) ||
                        (       (status.sequence_number == 0) &&
                                (output_packet.type == P_KACK))
                        ) {
                        /*
                         * Special case: do not do any prefix handling
                         * for the Send-Init or its ACK packet.
                         */
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "SEND-INIT --> ch '%c' %02x\n", ch, ch);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                        output[data_n] = ch;
                        data_n++;
                        continue;
                }

                if (output_packet.type == P_KATTRIBUTES) {
                        /*
                         * Special case: do not do any prefix handling
                         * for the ATTRIBUTES packet.
                         */
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "ATTRIBUTES --> ch '%c' %02x\n", ch, ch);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                        output[data_n] = ch;
                        data_n++;
                        continue;
                }

                /*
                 * Text files: strip any CR's, and replace LF's with CRLF.
                 */
                if ((status.text_mode == Q_TRUE) && (ch == C_CR)) {
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "    STRIP CR\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                        continue;
                }
                if ((status.text_mode == Q_TRUE) && (ch == C_LF)) {
                        if (crlf == Q_FALSE) {
#ifdef DEBUG_KERMIT
                                fprintf(DEBUG_FILE_HANDLE, "    SUB LF -> CR\n");
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                                crlf = Q_TRUE;
                                ch = C_CR;
                        } else {
                                crlf = Q_FALSE;
                        }
                }


                if (first == Q_TRUE) {
                        /* Special case: first character to read */
                        last_ch = ch;
                        first = Q_FALSE;
                        repeat_count = 0;
                }

                /* Normal case: do repeat count and prefixing */
                if ((last_ch == ch) && (repeat_count < 94)) {
                        repeat_count++;
                } else {
#ifdef DEBUG_KERMIT_VERBOSE
                        fprintf(DEBUG_FILE_HANDLE, "   encode ch '%c' %02x repeat %d\n",
                                last_ch, last_ch, repeat_count);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT_VERBOSE */
                        data_n += encode_one_byte(last_ch, repeat_count,
                                output + data_n);

                        repeat_count = 1;
                        last_ch = ch;
                }

        } /* for (;;) */

#ifdef DEBUG_KERMIT_VERBOSE
        fprintf(DEBUG_FILE_HANDLE, "   last_ch '%c' %02x repeat %d\n",
                last_ch, last_ch, repeat_count);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT_VERBOSE */

        if (repeat_count > 0) {
#ifdef DEBUG_KERMIT_VERBOSE
                fprintf(DEBUG_FILE_HANDLE, "   LAST encode ch '%c' %02x repeat %d\n", last_ch, last_ch, repeat_count);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT_VERBOSE */
                data_n += encode_one_byte(last_ch, repeat_count, output + data_n);
        }
        if ((status.text_mode == Q_TRUE) && (crlf == Q_TRUE)) {
                /* Terminating LF */
#ifdef DEBUG_KERMIT_VERBOSE
                fprintf(DEBUG_FILE_HANDLE, "   LAST TERMINATING LF\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT_VERBOSE */
                data_n += encode_one_byte(C_LF, 1, output + data_n);
        }

        /* Save output bytes */
        *output_n = data_n;

        /* Update block size on send */
        if ((type == P_KDATA) && (status.state == KM_SDW)) {
                status.block_size = data_n;
        }

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "encode_data_field() output_n = %d bytes:\n", *output_n);
        for (i = 0; i < *output_n; i++) {
                fprintf(DEBUG_FILE_HANDLE, "%02x ", (output[i] & 0xFF));
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        /* Data was OK */
        return Q_TRUE;
} /* ---------------------------------------------------------------------- */

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */
/* Packet layer ----------------------------------------------------------- */
/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

/*
 * Process the Send-Init packet
 */
static Q_BOOL process_send_init() {
        struct session_parameters parms;
        unsigned char capas;
        unsigned char whatami;
        unsigned char id_length;
#ifdef DEBUG_KERMIT
        unsigned char buffer[KERMIT_BLOCK_SIZE];
        int j;
#endif /* DEBUG_KERMIT */
        int capas_i = 9;

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "process_send_init()\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        /* This sets MY default parameters */
        set_default_session_parameters(&parms);

        /* Now reset to bare Kermit defaults */
        parms.MARK = C_SOH;
        parms.MAXL = 80;
        parms.TIME = 5;
        parms.NPAD = 0;
        parms.PADC = 0;
        parms.EOL = C_CR;
        parms.QCTL = '#';
        parms.QBIN = ' ';
        parms.CHKT = '1';
        parms.REPT = ' ';
        parms.CAPAS = 0x00;
        parms.WHATAMI = 0x00;
        parms.WINDO = 0;
        parms.MAXLX1 = 0;
        parms.MAXLX2 = 0;
        parms.attributes = Q_FALSE;
        parms.windowing = Q_FALSE;
        parms.long_packets = Q_FALSE;
        parms.streaming = Q_FALSE;

        if ((input_packet.data_n >= 1) && (input_packet.data[0] != ' ')) {
                /* Byte 1: MAXL */
                parms.MAXL = kermit_unchar(input_packet.data[0]);
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "    MAXL: '%c' %d\n",
                        input_packet.data[0], parms.MAXL);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                if (parms.MAXL > 94) {
                        /* Error: invalid maximum packet length */
                        return Q_FALSE;
                }
        }

        if ((input_packet.data_n >= 2) && (input_packet.data[1] != ' ')) {
                /* Byte 2: TIME */
                parms.TIME = kermit_unchar(input_packet.data[1]);
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "    TIME: '%c' %d\n",
                        input_packet.data[1], parms.TIME);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
        }

        if ((input_packet.data_n >= 3) && (input_packet.data[2] != ' ')) {
                /* Byte 3: NPAD */
                parms.NPAD = kermit_unchar(input_packet.data[2]);
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "    NPAD: '%c' %d\n",
                        input_packet.data[2], parms.NPAD);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
        }

        if ((input_packet.data_n >= 4) && (input_packet.data[3] != ' ')) {
                /* Byte 4: PADC - ctl */
                parms.PADC = kermit_ctl(input_packet.data[3]);
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "    PADC: '%c' %02x\n",
                        input_packet.data[3], parms.PADC);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
        }

        if ((input_packet.data_n >= 5) && (input_packet.data[4] != ' ')) {
                /* Byte 5: EOL */
                parms.EOL = kermit_unchar(input_packet.data[4]);
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "    EOL:  '%c' %02x\n",
                        input_packet.data[4], parms.EOL);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
        }

        if ((input_packet.data_n >= 6) && (input_packet.data[5] != ' ')) {
                /* Byte 6: QCTL - verbatim */
                parms.QCTL = input_packet.data[5];
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "    QCTL: '%c' %02x\n",
                        input_packet.data[5], parms.QCTL);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
        }

        if ((input_packet.data_n >= 7) && (input_packet.data[6] != ' ')) {
                /* Byte 7: QBIN - verbatim */
                parms.QBIN = input_packet.data[6];
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "    QBIN: '%c' %02x\n",
                        input_packet.data[6], parms.QBIN);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
        }

        if ((input_packet.data_n >= 8) && (input_packet.data[7] != ' ')) {
                /* Byte 8: CHKT - verbatim */
                parms.CHKT = input_packet.data[7];
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "    CHKT: '%c' %02x\n",
                        input_packet.data[7], parms.CHKT);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
        }

        if ((input_packet.data_n >= 9) && (input_packet.data[8] != ' ')) {
                /* Byte 9: REPT - verbatim */
                parms.REPT = input_packet.data[8];
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "    REPT: '%c' %02x\n",
                        input_packet.data[8], parms.REPT);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
        }

        if (input_packet.data_n >= 10) {
                while (input_packet.data_n > capas_i) {
                        /* Byte 10-?: CAPAS */
                        capas = kermit_unchar(input_packet.data[capas_i]);
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "    CAPAS %d: '%c' %02x\n",
                                (capas_i - 9), input_packet.data[capas_i],
                                capas);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                        if (capas_i == 9) {
                                parms.CAPAS = capas;
                                if (capas & 0x10) {
                                        /* Ability to support RESEND */
#ifdef DEBUG_KERMIT
                                        fprintf(DEBUG_FILE_HANDLE, "    CAPAS %d: Can do RESEND\n", (capas_i - 9));
                                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                                }

                                if (capas & 0x08) {
                                        /* Ability to accept "A" packets */
#ifdef DEBUG_KERMIT
                                        fprintf(DEBUG_FILE_HANDLE, "    CAPAS %d: Can accept A packets\n", (capas_i - 9));
                                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                                        parms.attributes = Q_TRUE;
                                }

                                if (capas & 0x04) {
                                        /* Ability to do full duplex sliding window */
#ifdef DEBUG_KERMIT
                                        fprintf(DEBUG_FILE_HANDLE, "    CAPAS %d: Can do full duplex sliding windows\n", (capas_i - 9));
                                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                                        parms.windowing = Q_TRUE;
                                }

                                if (capas & 0x02) {
                                        /* Ability to transmit and receive extended-length packets */
#ifdef DEBUG_KERMIT
                                        fprintf(DEBUG_FILE_HANDLE, "    CAPAS %d: Can do extended-length packets\n", (capas_i - 9));
                                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                                        parms.long_packets = Q_TRUE;
                                }
                        }

                        /* Point to next byte */
                        capas_i++;

                        if ((capas & 0x01) == 0) {
                                /* Last capas byte */
                                break;
                        }
                }

                if (input_packet.data_n >= capas_i + 1) {
                        /* WINDO */
                        parms.WINDO = kermit_unchar(input_packet.data[capas_i]);
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "    WINDO:  '%c' %02x %d\n",
                                input_packet.data[capas_i],
                                parms.WINDO, parms.WINDO);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                        capas_i++;
                }

                if (input_packet.data_n >= capas_i + 1) {
                        /* MAXLX1 */
                        parms.MAXLX1 = kermit_unchar(input_packet.data[capas_i]);
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "    MAXLX1: '%c' %02x %d\n",
                                input_packet.data[capas_i],
                                parms.MAXLX1, parms.MAXLX1);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                        capas_i++;
                }

                if (input_packet.data_n >= capas_i + 1) {
                        /* MAXLX2 */
                        parms.MAXLX2 = kermit_unchar(input_packet.data[capas_i]);
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "    MAXLX2: '%c' %02x %d\n",
                                input_packet.data[capas_i],
                                parms.MAXLX2, parms.MAXLX2);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                        capas_i++;
                }

                if (input_packet.data_n >= capas_i + 1) {
                        /* CHECKPOINT1 - Discard */
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "    CHECKPOINT 1: '%c' %02x %d\n",
                                input_packet.data[capas_i],
                                input_packet.data[capas_i],
                                input_packet.data[capas_i]);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                        capas_i++;
                }

                if (input_packet.data_n >= capas_i + 1) {
                        /* CHECKPOINT2 - Discard */
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "    CHECKPOINT 2: '%c' %02x %d\n",
                                input_packet.data[capas_i],
                                input_packet.data[capas_i],
                                input_packet.data[capas_i]);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                        capas_i++;
                }

                if (input_packet.data_n >= capas_i + 1) {
                        /* CHECKPOINT3 - Discard */
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "    CHECKPOINT 3: '%c' %02x %d\n",
                                input_packet.data[capas_i],
                                input_packet.data[capas_i],
                                input_packet.data[capas_i]);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                        capas_i++;
                }

                if (input_packet.data_n >= capas_i + 1) {
                        /* CHECKPOINT4 - Discard */
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "    CHECKPOINT 4: '%c' %02x %d\n",
                                input_packet.data[capas_i],
                                input_packet.data[capas_i],
                                input_packet.data[capas_i]);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                        capas_i++;
                }

                if (input_packet.data_n >= capas_i + 1) {
                        /* WHATAMI */
                        whatami = kermit_unchar(input_packet.data[capas_i]);
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "    WHATAMI: '%c' %02x %d\n",
                                input_packet.data[capas_i],
                                input_packet.data[capas_i],
                                input_packet.data[capas_i]);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                        if (whatami & 0x08) {
                                /* Ability to stream */
#ifdef DEBUG_KERMIT
                                fprintf(DEBUG_FILE_HANDLE, "    WHATAMI: Can stream\n");
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                                parms.streaming = Q_TRUE;
                        }

                        capas_i++;
                }

                if (input_packet.data_n >= capas_i + 1) {
                        /* System type - Length */
                        id_length = kermit_unchar(input_packet.data[capas_i]);
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "    System ID length: '%c' %02x %d\n",
                                input_packet.data[capas_i],
                                input_packet.data[capas_i],
                                input_packet.data[capas_i]);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                        if (input_packet.data_n >= capas_i + 1 + id_length) {
#ifdef DEBUG_KERMIT
                                for (j = 0; j < id_length; j++) {
                                        buffer[j] = input_packet.data[capas_i + 1 + j];
                                }
                                buffer[id_length] = 0;
                                fprintf(DEBUG_FILE_HANDLE, "        System ID: \"%s\"\n", buffer);
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                                capas_i += id_length;
                        }
                        capas_i++;
                }

                if (input_packet.data_n >= capas_i + 1) {
                        /* WHATAMI2 */
                        whatami = kermit_unchar(input_packet.data[capas_i]);
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "    WHATAMI2: '%c' %02x %d\n",
                                input_packet.data[capas_i],
                                input_packet.data[capas_i],
                                input_packet.data[capas_i]);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                        capas_i++;
                }
        }

        /*
         * If long packets are supported, but MAXLX1 and MAXLX2 were not
         * provided, there is a default of 500.
         */
        if (parms.long_packets == Q_TRUE) {
                if ((parms.MAXLX1 == 0) && (parms.MAXLX2 == 0)) {
                        parms.MAXLX1 = 500 / 95;
                        parms.MAXLX2 = 500 % 95;
                }
                if (((parms.MAXLX1 * 95) + parms.MAXLX2) > KERMIT_BLOCK_SIZE) {
                        parms.MAXLX1 = KERMIT_BLOCK_SIZE / 95;
                        parms.MAXLX2 = KERMIT_BLOCK_SIZE % 95;
                }
        }

        /* Save remote parameters */
        memcpy(&remote_parms, &parms, sizeof(struct session_parameters));

        /* All OK */
        return Q_TRUE;
} /* ---------------------------------------------------------------------- */

/*
 * Process the File-Header packet.  Returns false if the filename is invalid
 * or malformed.
 */
static Q_BOOL process_file_header() {
        int i;
        Q_BOOL lower_filename = Q_TRUE;

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "process_file_header()\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "   %d input bytes (hex):  ",
                input_packet.data_n);
        for (i = 0; i < input_packet.data_n; i++) {
                fprintf(DEBUG_FILE_HANDLE, "%02x ",
                        (input_packet.data[i] & 0xFF));
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        fprintf(DEBUG_FILE_HANDLE, "   %d input bytes (ASCII):  ",
                input_packet.data_n);
        for (i = 0; i < input_packet.data_n; i++) {
                fprintf(DEBUG_FILE_HANDLE, "%c ",
                        (input_packet.data[i] & 0xFF));
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        /* Terminate filename */
        input_packet.data[input_packet.data_n] = 0;

        /*
         * Apply gkermit heuristics:
         *
         *    1) All uppercase -> all lowercase
         *    2) Any lowercase -> no change
         */
        for (i = 0; i < input_packet.data_n; i++) {
                if (islower(input_packet.data[i])) {
                        lower_filename = Q_FALSE;
                }
        }
        if (lower_filename == Q_TRUE) {
                for (i = 0; i < input_packet.data_n; i++) {
                        input_packet.data[i] = tolower(input_packet.data[i]);
                }
        }

        /* Use filename */
        status.file_name = Xstrdup((char *)input_packet.data,
                __FILE__, __LINE__);

        /* Set default file size */
        status.file_size = 0;
        status.file_size_k = 0;

        /* Unset protection */
        status.file_protection = 0xFFFF;

        /* Unset mod_time */
        status.file_modtime = -1;

        /* All OK */
        return Q_TRUE;
} /* ---------------------------------------------------------------------- */

/*
 * Process the Attributes packet.  Returns false on malformed data.
 */
static Q_BOOL process_attributes() {
        int i;
        int j;
        int size_k = -1;
        int protection = -1;
        int kermit_protection = -1;
        long size_bytes = -1;
        unsigned char length;
        unsigned char type;
        char buffer[KERMIT_BLOCK_SIZE];
        struct tm file_time;
        Q_BOOL got_file_time = Q_FALSE;

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "process_attributes()\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "   %d input bytes (hex):  ",
                input_packet.data_n);
        for (i = 0; i < input_packet.data_n; i++) {
                fprintf(DEBUG_FILE_HANDLE, "%02x ",
                        (input_packet.data[i] & 0xFF));
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        fprintf(DEBUG_FILE_HANDLE, "   %d input bytes (ASCII):  ",
                input_packet.data_n);
        for (i = 0; i < input_packet.data_n; i++) {
                fprintf(DEBUG_FILE_HANDLE, "%c ",
                        (input_packet.data[i] & 0xFF));
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        /* Terminate attributes */
        input_packet.data[input_packet.data_n] = 0;

        for (i = 0; i + 1 < input_packet.data_n;) {
                type = input_packet.data[i];
                i++;
                length = kermit_unchar(input_packet.data[i]);
                i++;

#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "   ATTRIBUTE TYPE '%c' LENGTH %d i %d data_n %d\n",
                        type, length, i, input_packet.data_n);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                if (i + length > input_packet.data_n) {
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "   ERROR SHORT ATTRIBUTE PACKET\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                        /* Sender isn't Kermit compliant, abort. */
                        set_transfer_stats_last_message(_("ERROR PARSING PACKET"));
                        status.state = ABORT;
                        stop_file_transfer(Q_TRANSFER_STATE_ABORT);
                        error_packet("Error parsing packet");
                        return Q_FALSE;
                }

                switch (type) {
                case '!':
                        /* File size in k-bytes */
                        for (j = 0; j < length; j++) {
                                buffer[j] = input_packet.data[i + j];
                        }
                        buffer[length] = 0;
                        size_k = atoi(buffer);
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "   file size (K) %u\n", size_k);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                        status.file_size_k = size_k;
                        break;
                case '\"':
                        /* File type */
                        for (j = 0; j < length; j++) {
                                buffer[j] = input_packet.data[i + j];
                        }
                        buffer[length] = 0;
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "   file type: \"%s\"\n", buffer);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                        /* See if ASCII */
                        if ((length > 0) && (buffer[0] == 'A')) {
#ifdef DEBUG_KERMIT
                                fprintf(DEBUG_FILE_HANDLE, "   file type: ASCII\n");
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                                /*
                                 * The Kermit Protocol book allows for
                                 * multiple ways to encode EOL, but also
                                 * specifies CRLF as the canonical
                                 * standard.  Qodem will always assume
                                 * ASCII files are CRLF format.
                                 *
                                 * Actually, all Qodem does is strip CR's
                                 * in the input, even if they aren't
                                 * paired with LF.
                                 */
                                if (q_status.kermit_downloads_convert_text == Q_TRUE) {
                                        status.text_mode = Q_TRUE;
#ifdef DEBUG_KERMIT
                                        fprintf(DEBUG_FILE_HANDLE, "       - WILL do CRLF conversion -\n");
                                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                                } else {
#ifdef DEBUG_KERMIT
                                        fprintf(DEBUG_FILE_HANDLE, "       - will NOT do CRLF conversion -\n");
                                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                                }
                        }

                        break;
                case '#':
                        /* Creation date */
                        for (j = 0; j < length; j++) {
                                buffer[j] = input_packet.data[i + j];
                        }
                        buffer[length] = 0;
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "   creation date %s\n",
                                buffer);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                        if (strptime(buffer, "%C%y%m%d %H:%M:%S", &file_time) != NULL) {
#ifdef DEBUG_KERMIT
                                fprintf(DEBUG_FILE_HANDLE, "   creation date YYYYMMDD HHMMSS\n");
#endif /* DEBUG_KERMIT */
                                /* YYYYMMDD HHmmss */
                                got_file_time = Q_TRUE;
                        } else if (strptime(buffer, "%y%m%d %H:%M:%S", &file_time) != NULL) {
#ifdef DEBUG_KERMIT
                                fprintf(DEBUG_FILE_HANDLE, "   creation date YYMMDD HHMMSS\n");
#endif /* DEBUG_KERMIT */
                                /* YYMMDD HHmmss */
                                got_file_time = Q_TRUE;
                        } else if (strptime(buffer, "%C%y%m%d %H:%M", &file_time) != NULL) {
#ifdef DEBUG_KERMIT
                                fprintf(DEBUG_FILE_HANDLE, "   creation date YYYYMMDD HHMM\n");
#endif /* DEBUG_KERMIT */
                                /* YYYYMMDD HHmm */
                                got_file_time = Q_TRUE;
                        } else if (strptime(buffer, "%y%m%d %H:%M", &file_time) != NULL) {
#ifdef DEBUG_KERMIT
                                fprintf(DEBUG_FILE_HANDLE, "   creation date YYMMDD HHMM\n");
#endif /* DEBUG_KERMIT */
                                /* YYMMDD HHmm */
                                got_file_time = Q_TRUE;
                        } else if (strptime(buffer, "%C%y%m%d", &file_time) != NULL) {
#ifdef DEBUG_KERMIT
                                fprintf(DEBUG_FILE_HANDLE, "   creation date YYYYMMDD\n");
#endif /* DEBUG_KERMIT */
                                /* YYYYMMDD */
                                got_file_time = Q_TRUE;
                        } else if (strptime(buffer, "%y%m%d", &file_time) != NULL) {
#ifdef DEBUG_KERMIT
                                fprintf(DEBUG_FILE_HANDLE, "   creation date YYMMDD\n");
#endif /* DEBUG_KERMIT */
                                /* YYMMDD */
                                got_file_time = Q_TRUE;
                        } else {
                                got_file_time = Q_FALSE;
                        }

                        if (got_file_time == Q_TRUE) {
                                file_time.tm_isdst = -1;
                                status.file_modtime = mktime(&file_time);
                        } else {
                                status.file_modtime = time(NULL);
                        }

#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "   creation date %s\n",
                                ctime(&status.file_modtime));
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                        break;
                case '$':
                        /* Creator ID - skip */
                        break;
                case '%':
                        /* Charge account - skip */
                        break;
                case '&':
                        /* Area to store the file - skip */
                        break;
                case '\'':
                        /* Area storage password - skip */
                        break;
                case '(':
                        /* Block size - skip */
                        break;
                case ')':
                        /* Access */
                        switch (input_packet.data[i]) {
                        case 'N':
                                /* Create a new file */
#ifdef DEBUG_KERMIT
                                fprintf(DEBUG_FILE_HANDLE, "   CREATE NEW FILE\n");
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                                status.access = K_ACCESS_NEW;
                                break;
                        case 'S':
                                /* Supersede - overwrite if a file already exists */
#ifdef DEBUG_KERMIT
                                fprintf(DEBUG_FILE_HANDLE, "   SUPERSEDE\n");
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                                status.access = K_ACCESS_SUPERSEDE;
                                break;
                        case 'A':
                                /* Append to file */
#ifdef DEBUG_KERMIT
                                fprintf(DEBUG_FILE_HANDLE, "   APPEND TO FILE\n");
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                                status.access = K_ACCESS_APPEND;
                                break;
                        case 'W':
                                /* Warn - rename if file exists */
#ifdef DEBUG_KERMIT
                                fprintf(DEBUG_FILE_HANDLE, "   WARN AND RENAME IF FILE EXISTS\n");
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                                status.access = K_ACCESS_WARN;
                                break;
                        }
                        break;
                case '*':
                        /* Encoding */
                        switch (input_packet.data[i]) {
                        case 'A':
                                /* ASCII */
                                break;
                        case 'H':
                                /* Hex "nibble" encoding */
                                break;
                        case 'E':
                                /* EBCDIC */
                                break;
                        case 'X':
                                /* Encrypted */
                                break;
                        case 'Q':
                                /* Huffman encoding */
                                break;
                        }
                        break;
                case '+':
                        /* Disposition */
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "   disposition: %c\n",
                                input_packet.data[i]);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                        switch (input_packet.data[i]) {
                        case 'R':
                                /* RESEND option */
#ifdef DEBUG_KERMIT
                                fprintf(DEBUG_FILE_HANDLE, "       RESEND\n");
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                                status.do_resend = Q_TRUE;
                                break;
                        case 'M':
                                /* Send as Mail to user */
                                break;
                        case 'O':
                                /* Send as lOng terminal message */
                                break;
                        case 'S':
                                /* Submit as batch job */
                                break;
                        case 'P':
                                /* Print on system printer */
                                break;
                        case 'T':
                                /* Type the file on screen */
                                break;
                        case 'L':
                                /* Load into memory at given address */
                                break;
                        case 'X':
                                /* Load into memory at given address and eXecute */
                                break;
                        case 'A':
                                /* Archive the file */
                                break;
                        }
                        break;
                case ',':
                        /* Protection in receiver format */
                        for (j = 0; j < length; j++) {
                                buffer[j] = input_packet.data[i + j];
                        }
                        buffer[length] = 0;
                        /* It will be in octal */
                        protection = strtol(buffer, NULL, 8);
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "   protection %u %o\n",
                                protection, protection);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                        status.file_protection = protection;
                        break;
                case '-':
                        /* Protection in Kermit format */
                        kermit_protection = kermit_unchar(input_packet.data[i]);
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "   protection (kermit format) %02x\n", kermit_protection);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                        break;
                case '.':
                        /* Machine and OS of origin - skip */
                        break;
                case '/':
                        /* Format of data within file - skip */
                        break;
                case 'O':
                        /* System-dependant parameters for storing file - skip */
                        break;
                case '1':
                        /* File size in bytes */
                        for (j = 0; j < length; j++) {
                                buffer[j] = input_packet.data[i + j];
                        }
                        buffer[length] = 0;
                        size_bytes = atoi(buffer);
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "   file size (bytes) %lu\n", size_bytes);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                        status.file_size = size_bytes;
                        break;
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
                case '9':
                case ':':
                case ';':
                case '<':
                case '=':
                case '>':
                case '?':
                case '@':
                        /* Reserved - discard */
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "   UNKNOWN ATTRIBUTE\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                        break;
                }
                i += length;
        }

        if ((input_packet.data_n - i) != 0) {
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "   ERROR LONG ATTRIBUTE PACKET\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                /* Sender isn't Kermit compliant, abort. */
                set_transfer_stats_last_message(_("ERROR PARSING ATTRIBUTE PACKET"));
                status.state = ABORT;
                stop_file_transfer(Q_TRANSFER_STATE_ABORT);
                error_packet("Error parsing packet");
                return Q_FALSE;
        }

        /* Use kermit_protection if file_protection wasn't specified */
        if ((status.file_protection == 0xFFFF) && (kermit_protection != -1)) {
                /* Start with rw------- */
                status.file_protection = 0600;
                if (kermit_protection & 0x01) {
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "       kermit_protection: world read\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                        /* Add r--r--r-- */
                        status.file_protection |= 044;
                }
                if (kermit_protection & 0x02) {
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "       kermit_protection: world write\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                        /* Add -w--w--w- */
                        status.file_protection |= 022;
                }
                if (kermit_protection & 0x01) {
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "       kermit_protection: world execute\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                        /* Add --x--x--x */
                        status.file_protection |= 0111;
                }
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "translated from kermit_protection: %u %o\n",
                        status.file_protection, status.file_protection);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
        }

        /* All OK */
        return Q_TRUE;
} /* ---------------------------------------------------------------------- */

/*
 * Process the Error packet.
 */
static void process_error_packet() {
#ifdef DEBUG_KERMIT
        int i;
#endif /* DEBUG_KERMIT */

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "process_error_packet()\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "   %d input bytes (hex):  ",
                input_packet.data_n);
        for (i = 0; i < input_packet.data_n; i++) {
                fprintf(DEBUG_FILE_HANDLE, "%02x ",
                        (input_packet.data[i] & 0xFF));
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        fprintf(DEBUG_FILE_HANDLE, "   %d input bytes (ASCII):  ",
                input_packet.data_n);
        for (i = 0; i < input_packet.data_n; i++) {
                fprintf(DEBUG_FILE_HANDLE, "%c ",
                        (input_packet.data[i] & 0xFF));
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        /* Terminate string */
        input_packet.data[input_packet.data_n] = 0;

        status.state = ABORT;
        set_transfer_stats_last_message((char *)input_packet.data);
        stop_file_transfer(Q_TRANSFER_STATE_ABORT);
} /* ---------------------------------------------------------------------- */

/*
 * Create the File-Header packet
 */
static void send_file_header() {
        int i;
        char ch;
        int last_period = -1;

        output_packet.parsed_ok = Q_TRUE;
        output_packet.type = P_KFILE;
        output_packet.seq = (status.sequence_number % 64);

        for (i = 0; i < strlen(status.file_name); i++) {
                ch = status.file_name[i];
                if (q_status.kermit_robust_filename == Q_TRUE) {
                        /* Convert to "common form" */
                        output_packet.data[i] = ch;
                        if (ch == '.') {
                                output_packet.data[i] = '_';
                                last_period = i;
                        }
                        if (!isalnum(ch)) {
                                output_packet.data[i] = '_';
                        }
                        if (islower(ch)) {
                                output_packet.data[i] = toupper(ch);
                        }
                } else {
                        /* Use the literal filename */
                        output_packet.data[i] = ch;
                }
        }
        if (last_period != -1) {
                output_packet.data[last_period] = '.';
        }
        output_packet.data_n = i;

        if (q_status.kermit_robust_filename == Q_TRUE) {
                /* Cannot begin with a dot */
                if (output_packet.data[0] == '.') {
                        memmove(output_packet.data,
                                output_packet.data + 1,
                                output_packet.data_n - 1);
                        output_packet.data_n--;
                }

                /* Cannot end with a dot */
                if (output_packet.data[output_packet.data_n - 1] == '.') {
                        output_packet.data[output_packet.data_n - 1] = 0;
                        output_packet.data_n--;
                }
        }
} /* ---------------------------------------------------------------------- */

/*
 * Create the Attributes packet
 */
static void send_file_attributes() {
        int i;
        char buffer[KERMIT_BLOCK_SIZE];

        output_packet.parsed_ok = Q_TRUE;
        output_packet.type = P_KATTRIBUTES;
        output_packet.seq = (status.sequence_number % 64);
        i = 0;

        output_packet.data[i] = '\"';
        i++;
        if (status.text_mode == Q_TRUE) {
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "send_file_attributes() - ASCII FILE\n");
#endif /* DEBUG_KERMIT */
                /* File type AMJ */
                output_packet.data[i] = kermit_tochar(1);
                i++;
                output_packet.data[i] = 'A';
                i++;
        } else {
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "send_file_attributes() - BINARY FILE\n");
#endif /* DEBUG_KERMIT */
                /* File type B8 */
                output_packet.data[i] = kermit_tochar(2);
                i++;
                output_packet.data[i] = 'B';
                i++;
                output_packet.data[i] = '8';
                i++;
        }

        /* File size in bytes */
        sprintf(buffer, "%u", status.file_size);
#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "send_file_attributes() - file size %s\n",
                buffer);
#endif /* DEBUG_KERMIT */
        output_packet.data[i] = '1';
        i++;
        output_packet.data[i] = kermit_tochar(strlen(buffer));
        i++;
        memcpy(output_packet.data + i, buffer, strlen(buffer));
        i += strlen(buffer);

        /* File modification time */
        strftime(buffer, sizeof(buffer) - 1, "%Y%m%d %H:%M:%S",
                localtime(&status.file_modtime));
#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "send_file_attributes() - file time %s\n",
                buffer);
#endif /* DEBUG_KERMIT */
        output_packet.data[i] = '#';
        i++;
        output_packet.data[i] = kermit_tochar(strlen(buffer));
        i++;
        memcpy(output_packet.data + i, buffer, strlen(buffer));
        i += strlen(buffer);

        /* Protection - native, only include the bottom 9 bits */
        sprintf(buffer, "%o", status.file_protection & 0x1FF);
#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "send_file_attributes() - protection %s\n",
                buffer);
#endif /* DEBUG_KERMIT */
        output_packet.data[i] = ',';
        i++;
        output_packet.data[i] = kermit_tochar(strlen(buffer));
        i++;
        memcpy(output_packet.data + i, buffer, strlen(buffer));
        i += strlen(buffer);

        /* Protection - kermit, only look at bottom 3 bits */
        buffer[0] = 0;
        if (status.file_protection & 0x01) {
                buffer[0] |= 0x04;
        }
        if (status.file_protection & 0x02) {
                buffer[0] |= 0x02;
        }
        if (status.file_protection & 0x04) {
                buffer[0] |= 0x01;
        }
#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "send_file_attributes() - kermit_protection %c %02x\n",
                buffer[0], buffer[0]);
#endif /* DEBUG_KERMIT */
        output_packet.data[i] = '-';
        i++;
        output_packet.data[i] = kermit_tochar(1);
        i++;
        output_packet.data[i] = kermit_tochar(buffer[0]);
        i++;

        /* Resend */
        if (    ((session_parms.CAPAS & 0x10) != 0) &&
                (q_status.kermit_resend == Q_TRUE)
        ) {
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "send_file_attributes() - RESEND\n");
#endif /* DEBUG_KERMIT */
                output_packet.data[i] = '+';
                i++;
                output_packet.data[i] = kermit_tochar(1);
                i++;
                output_packet.data[i] = 'R';
                i++;
                status.do_resend = Q_TRUE;
        }

        output_packet.data_n = i;
} /* ---------------------------------------------------------------------- */

/*
 * Create the File-Data packet - this is a special case as encode_data_field()
 * does the actual file reading.
 */
static Q_BOOL send_file_data() {

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "KERMIT: send_file_data()\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        if (feof(status.file_stream)) {
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: send_file_data() EOF\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                return Q_FALSE;
        }
        output_packet.parsed_ok = Q_TRUE;
        output_packet.type = P_KDATA;
        output_packet.seq = (status.sequence_number % 64);
        output_packet.data_n = 0;

        return Q_TRUE;
} /* ---------------------------------------------------------------------- */

/*
 * Create the EOF packet
 */
static void send_eof() {
        output_packet.parsed_ok = Q_TRUE;
        output_packet.type = P_KEOF;
        output_packet.seq = (status.sequence_number % 64);
        if (status.skip_file == Q_TRUE) {
                /* Don't do it twice */
                status.skip_file = Q_FALSE;

                output_packet.data[0] = 'D';
                output_packet.data_n = 1;
        } else {
                output_packet.data_n = 0;
        }
} /* ---------------------------------------------------------------------- */

/*
 * Create the EOT packet
 */
static void send_eot() {
        output_packet.parsed_ok = Q_TRUE;
        output_packet.type = P_KBREAK;
        output_packet.seq = (status.sequence_number % 64);
        output_packet.data_n = 0;
} /* ---------------------------------------------------------------------- */

/*
 * Create the Send-Init (or its ACK) packet
 */
static void ack_send_init() {
        status.sequence_number = 0;

        output_packet.parsed_ok = Q_TRUE;
        output_packet.type = P_KACK;
        output_packet.seq = (status.sequence_number % 64);
        output_packet.data[0] = kermit_tochar(session_parms.MAXL);
        output_packet.data[1] = kermit_tochar(session_parms.TIME);
        output_packet.data[2] = kermit_tochar(local_parms.NPAD);
        output_packet.data[3] = kermit_ctl(local_parms.PADC);
        output_packet.data[4] = kermit_tochar(local_parms.EOL);
        output_packet.data[5] = local_parms.QCTL;
        output_packet.data[6] = session_parms.QBIN;
        output_packet.data[7] = session_parms.CHKT;
        output_packet.data[8] = session_parms.REPT;
        output_packet.data[9] = kermit_tochar(session_parms.CAPAS);
        /* Long packets */
        output_packet.data[10] = kermit_tochar(session_parms.WINDO);
        output_packet.data[11] = kermit_tochar(session_parms.MAXLX1);
        output_packet.data[12] = kermit_tochar(session_parms.MAXLX2);
        /* Checkpointing - never implemented in the protocol */
        output_packet.data[13] = '0';
        output_packet.data[14] = '_';
        output_packet.data[15] = '_';
        output_packet.data[16] = '_';
        output_packet.data[17] = kermit_tochar(session_parms.WHATAMI);
        output_packet.data_n = 18;
} /* ---------------------------------------------------------------------- */

/*
 * Negotiate the two sides of the Send-Init packet
 */
static void negotiate_send_init() {

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "negotiate_send_init() - local side:\n");
        fprintf(DEBUG_FILE_HANDLE, "    MAXL: '%c' %d\n", local_parms.MAXL, local_parms.MAXL);
        fprintf(DEBUG_FILE_HANDLE, "    TIME: '%c' %d\n", local_parms.TIME, local_parms.TIME);
        fprintf(DEBUG_FILE_HANDLE, "    NPAD: '%c' %d\n", local_parms.NPAD, local_parms.NPAD);
        fprintf(DEBUG_FILE_HANDLE, "    PADC: '%c' %02x\n", local_parms.PADC, local_parms.PADC);
        fprintf(DEBUG_FILE_HANDLE, "    EOL:  '%c' %02x\n", local_parms.EOL, local_parms.EOL);
        fprintf(DEBUG_FILE_HANDLE, "    QCTL: '%c' %02x\n", local_parms.QCTL, local_parms.QCTL);
        fprintf(DEBUG_FILE_HANDLE, "    QBIN: '%c' %02x\n", local_parms.QBIN, local_parms.QBIN);
        fprintf(DEBUG_FILE_HANDLE, "    CHKT: '%c' %02x\n", local_parms.CHKT, local_parms.CHKT);
        fprintf(DEBUG_FILE_HANDLE, "    REPT: '%c' %02x\n", local_parms.REPT, local_parms.REPT);
        fprintf(DEBUG_FILE_HANDLE, "    CAPAS: '%c' %02x\n", local_parms.CAPAS, local_parms.CAPAS);
        fprintf(DEBUG_FILE_HANDLE, "    WHATAMI: '%c' %02x\n", local_parms.WHATAMI, local_parms.WHATAMI);
        fprintf(DEBUG_FILE_HANDLE, "    attributes: '%s'\n", (local_parms.attributes == Q_TRUE ? "true" : "false"));
        fprintf(DEBUG_FILE_HANDLE, "    windowing: '%s'\n", (local_parms.windowing == Q_TRUE ? "true" : "false"));
        fprintf(DEBUG_FILE_HANDLE, "    long_packets: '%s'\n", (local_parms.long_packets == Q_TRUE ? "true" : "false"));
        fprintf(DEBUG_FILE_HANDLE, "    streaming: '%s'\n", (local_parms.streaming == Q_TRUE ? "true" : "false"));
        fprintf(DEBUG_FILE_HANDLE, "    WINDO: '%c' %d\n", local_parms.WINDO, local_parms.WINDO);
        fprintf(DEBUG_FILE_HANDLE, "    MAXLX1: '%c' %d\n", local_parms.MAXLX1, local_parms.MAXLX1);
        fprintf(DEBUG_FILE_HANDLE, "    MAXLX2: '%c' %d\n", local_parms.MAXLX2, local_parms.MAXLX2);

        fprintf(DEBUG_FILE_HANDLE, "negotiate_send_init() - remote side:\n");
        fprintf(DEBUG_FILE_HANDLE, "    MAXL: '%c' %d\n", remote_parms.MAXL, remote_parms.MAXL);
        fprintf(DEBUG_FILE_HANDLE, "    TIME: '%c' %d\n", remote_parms.TIME, remote_parms.TIME);
        fprintf(DEBUG_FILE_HANDLE, "    NPAD: '%c' %d\n", remote_parms.NPAD, remote_parms.NPAD);
        fprintf(DEBUG_FILE_HANDLE, "    PADC: '%c' %02x\n", remote_parms.PADC, remote_parms.PADC);
        fprintf(DEBUG_FILE_HANDLE, "    EOL:  '%c' %02x\n", remote_parms.EOL, remote_parms.EOL);
        fprintf(DEBUG_FILE_HANDLE, "    QCTL: '%c' %02x\n", remote_parms.QCTL, remote_parms.QCTL);
        fprintf(DEBUG_FILE_HANDLE, "    QBIN: '%c' %02x\n", remote_parms.QBIN, remote_parms.QBIN);
        fprintf(DEBUG_FILE_HANDLE, "    CHKT: '%c' %02x\n", remote_parms.CHKT, remote_parms.CHKT);
        fprintf(DEBUG_FILE_HANDLE, "    REPT: '%c' %02x\n", remote_parms.REPT, remote_parms.REPT);
        fprintf(DEBUG_FILE_HANDLE, "    CAPAS: '%c' %02x\n", remote_parms.CAPAS, remote_parms.CAPAS);
        fprintf(DEBUG_FILE_HANDLE, "    WHATAMI: '%c' %02x\n", remote_parms.WHATAMI, remote_parms.WHATAMI);
        fprintf(DEBUG_FILE_HANDLE, "    attributes: '%s'\n", (remote_parms.attributes == Q_TRUE ? "true" : "false"));
        fprintf(DEBUG_FILE_HANDLE, "    windowing: '%s'\n", (remote_parms.windowing == Q_TRUE ? "true" : "false"));
        fprintf(DEBUG_FILE_HANDLE, "    long_packets: '%s'\n", (remote_parms.long_packets == Q_TRUE ? "true" : "false"));
        fprintf(DEBUG_FILE_HANDLE, "    streaming: '%s'\n", (remote_parms.streaming == Q_TRUE ? "true" : "false"));
        fprintf(DEBUG_FILE_HANDLE, "    WINDO: '%c' %d\n", remote_parms.WINDO, remote_parms.WINDO);
        fprintf(DEBUG_FILE_HANDLE, "    MAXLX1: '%c' %d\n", remote_parms.MAXLX1, remote_parms.MAXLX1);
        fprintf(DEBUG_FILE_HANDLE, "    MAXLX2: '%c' %d\n", remote_parms.MAXLX2, remote_parms.MAXLX2);
#endif /* DEBUG_KERMIT */

        /* Negotiate session parameters */
        /* MAXL - Use the minimum value */
        if (local_parms.MAXL < remote_parms.MAXL) {
                session_parms.MAXL = local_parms.MAXL;
        } else {
                session_parms.MAXL = remote_parms.MAXL;
        }

        /* TIME - Just use mine */
        session_parms.TIME = local_parms.TIME;

        /* NPAD - use theirs */
        session_parms.NPAD = remote_parms.NPAD;

        /* PADC - use theirs */
        session_parms.PADC = remote_parms.PADC;

        /* EOL - use theirs */
        session_parms.EOL = remote_parms.EOL;

        /* QCTL - use mine */
        session_parms.QCTL = local_parms.QCTL;

        /* QBIN - see what they ask for */
        if (remote_parms.QBIN == 'Y') {
                if (((local_parms.QBIN >= 33) && (local_parms.QBIN <= 62)) ||
                        ((local_parms.QBIN >= 96) && (local_parms.QBIN <= 126))
                ) {
                        /* Got a valid local QBIN */
                        session_parms.QBIN = local_parms.QBIN;
                }
        } else if (remote_parms.QBIN == 'N') {
                session_parms.QBIN = ' ';
        } else if (((remote_parms.QBIN >= 33) && (remote_parms.QBIN <= 62)) ||
                ((remote_parms.QBIN >= 96) && (remote_parms.QBIN <= 126))) {
                /* Got a valid remote QBIN */
                session_parms.QBIN = remote_parms.QBIN;
        }
        if (session_parms.QBIN == 'Y') {
                /* We both offered but don't need to */
                session_parms.QBIN = ' ';
        }
        if (remote_parms.QBIN == session_parms.QCTL) {
                /* Can't use QCTL as QBIN too */
                session_parms.QBIN = ' ';
        }

        /* CHKT - if in agreement, use theirs, else use '1' */
        if (local_parms.CHKT == remote_parms.CHKT) {
                session_parms.CHKT = remote_parms.CHKT;
        } else {
                session_parms.CHKT = '1';
        }
        if (session_parms.CHKT == 'B') {
                status.check_type = 12;
        } else {
                status.check_type = session_parms.CHKT - '0';
        }

        /* REPT - if in agreement, use theirs, else use ' ' */
        if (local_parms.REPT == remote_parms.REPT) {
                if (((local_parms.REPT >= 33) && (local_parms.REPT <= 62)) ||
                        ((local_parms.REPT >= 96) && (local_parms.REPT <= 126))
                ) {
                        /* Got a valid local REPT */
                        session_parms.REPT = local_parms.REPT;
                }
                session_parms.REPT = remote_parms.REPT;
        } else {
                session_parms.REPT = ' ';
        }
        if ((remote_parms.REPT == session_parms.QCTL) ||
                (remote_parms.REPT == session_parms.QBIN)
        ) {
                /* Can't use QCTL or QBIN as REPT too */
                session_parms.REPT = ' ';
        }

        /* Attributes - if in agreement, use theirs */
        if (local_parms.attributes == remote_parms.attributes) {
                session_parms.attributes = local_parms.attributes;
                session_parms.CAPAS = 0x10 | 0x08;
        } else {
                session_parms.attributes = Q_FALSE;
                session_parms.CAPAS = 0;
        }

        /* Check RESEND flag */
        if ((session_parms.CAPAS & 0x10) != 0) {
                status.do_resend = Q_TRUE;
        }

        /* Long packets - if in agreement, use theirs */
        if (local_parms.long_packets == remote_parms.long_packets) {
                session_parms.long_packets = local_parms.long_packets;
                if (local_parms.long_packets == Q_TRUE) {
                        session_parms.CAPAS |= 0x02;
                }
        } else {
                session_parms.long_packets = Q_FALSE;
        }
        /* Streaming - if in agreement, use theirs */
        if (local_parms.streaming == remote_parms.streaming) {
                session_parms.streaming = local_parms.streaming;
                if (session_parms.streaming == Q_TRUE) {
                        session_parms.WHATAMI = 0x28;
                }
        } else {
                session_parms.streaming = Q_FALSE;
                session_parms.WHATAMI = 0;
        }
        /* Windowing - if in agreement, use theirs */
        if (local_parms.windowing == remote_parms.windowing) {
                if (remote_parms.WINDO < local_parms.WINDO) {
                        session_parms.WINDO = remote_parms.WINDO;
                } else {
                        session_parms.WINDO = local_parms.WINDO;
                }
                if (session_parms.WINDO < 2) {
                        /* Disable windowing for windows of 1 packet */
                        session_parms.WINDO = 0;
                        session_parms.windowing = Q_FALSE;
                        session_parms.WINDO_out = 1;
                } else {
                        session_parms.WINDO_in = session_parms.WINDO;
                        session_parms.WINDO_out = session_parms.WINDO;
                }

                /*
                 * Streaming overrides sliding windows.  If we're both
                 * able to stream, don't do windows.
                 */
                if (session_parms.streaming == Q_TRUE) {
                        session_parms.windowing = Q_FALSE;
                } else {
                        session_parms.windowing = local_parms.windowing;
                        if (local_parms.windowing == Q_TRUE) {
                                session_parms.CAPAS |= 0x04;
                                /* Allocate the two windows */
                                assert(input_window != NULL);
                                input_window = (struct kermit_packet_serial *)Xrealloc(input_window, session_parms.WINDO_in * sizeof(struct kermit_packet_serial), __FILE__, __LINE__);
                                memset(input_window, 0, session_parms.WINDO_in * sizeof(struct kermit_packet_serial));
                                assert(output_window != NULL);
                                output_window = (struct kermit_packet_serial *)Xrealloc(output_window, session_parms.WINDO_out * sizeof(struct kermit_packet_serial), __FILE__, __LINE__);
                                memset(output_window, 0, session_parms.WINDO_out * sizeof(struct kermit_packet_serial));
                                /* Reset input_window */
                                input_window_n = 0;
                                input_window_i = 0;
                                input_window_begin = 0;
                        }
                }
                /*
                 * Final sanity check: if windowing is off, stick to 1 slot
                 * on each side.
                 */
                if (session_parms.windowing == Q_FALSE) {
                        session_parms.WINDO_in = 1;
                        session_parms.WINDO_out = 1;
                }
        } else {
                session_parms.windowing = Q_FALSE;
        }

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "negotiate_send_init() - negotiated values:\n");
        fprintf(DEBUG_FILE_HANDLE, "    MAXL: '%c' %d\n", session_parms.MAXL, session_parms.MAXL);
        fprintf(DEBUG_FILE_HANDLE, "    TIME: '%c' %d\n", session_parms.TIME, session_parms.TIME);
        fprintf(DEBUG_FILE_HANDLE, "    NPAD: '%c' %d\n", session_parms.NPAD, session_parms.NPAD);
        fprintf(DEBUG_FILE_HANDLE, "    PADC: '%c' %02x\n", session_parms.PADC, session_parms.PADC);
        fprintf(DEBUG_FILE_HANDLE, "    EOL:  '%c' %02x\n", session_parms.EOL, session_parms.EOL);
        fprintf(DEBUG_FILE_HANDLE, "    QCTL: '%c' %02x\n", session_parms.QCTL, session_parms.QCTL);
        fprintf(DEBUG_FILE_HANDLE, "    QBIN: '%c' %02x\n", session_parms.QBIN, session_parms.QBIN);
        fprintf(DEBUG_FILE_HANDLE, "    CHKT: '%c' %02x\n", session_parms.CHKT, session_parms.CHKT);
        fprintf(DEBUG_FILE_HANDLE, "    REPT: '%c' %02x\n", session_parms.REPT, session_parms.REPT);
        fprintf(DEBUG_FILE_HANDLE, "    CAPAS: '%c' %02x\n", session_parms.CAPAS, session_parms.CAPAS);
        fprintf(DEBUG_FILE_HANDLE, "    WHATAMI: '%c' %02x\n", session_parms.WHATAMI, session_parms.WHATAMI);
        fprintf(DEBUG_FILE_HANDLE, "    attributes: '%s'\n", (session_parms.attributes == Q_TRUE ? "true" : "false"));
        fprintf(DEBUG_FILE_HANDLE, "    windowing: '%s'\n", (session_parms.windowing == Q_TRUE ? "true" : "false"));
        fprintf(DEBUG_FILE_HANDLE, "    long_packets: '%s'\n", (session_parms.long_packets == Q_TRUE ? "true" : "false"));
        fprintf(DEBUG_FILE_HANDLE, "    streaming: '%s'\n", (session_parms.streaming == Q_TRUE ? "true" : "false"));
        fprintf(DEBUG_FILE_HANDLE, "    resend: '%s'\n", (status.do_resend == Q_TRUE ? "true" : "false"));
        fprintf(DEBUG_FILE_HANDLE, "    WINDO: '%c' %d\n", session_parms.WINDO, session_parms.WINDO);
        fprintf(DEBUG_FILE_HANDLE, "    MAXLX1: '%c' %d\n", session_parms.MAXLX1, session_parms.MAXLX1);
        fprintf(DEBUG_FILE_HANDLE, "    MAXLX2: '%c' %d\n", session_parms.MAXLX2, session_parms.MAXLX2);
#endif /* DEBUG_KERMIT */

} /* ---------------------------------------------------------------------- */

/*
 * Generic ACK
 */
static void ack_packet(Q_BOOL really) {
#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "ack_packet() SEQ %d\n",
                input_packet.seq);
#endif /* DEBUG_KERMIT */

        /* Only the receiver can ACK */
        assert(status.sending == Q_FALSE);

        if (status.skip_file == Q_TRUE) {
                /* Don't do it twice */
                status.skip_file = Q_FALSE;

                /* Build a skip request */
                output_packet.parsed_ok = Q_TRUE;
                output_packet.type = P_KACK;
                output_packet.seq = input_packet.seq;
                output_packet.data[0] = 'X';
                output_packet.data_n = 1;
                return;
        }

        if ((session_parms.streaming == Q_FALSE) || (really == Q_TRUE)) {
                output_packet.parsed_ok = Q_TRUE;
                output_packet.type = P_KACK;
                output_packet.seq = input_packet.seq;
                output_packet.data_n = 0;
        }
} /* ---------------------------------------------------------------------- */

/*
 * Generic ACK with a parameter
 */
static void ack_packet_parm(char * parm, int parm_n) {
#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "ack_packet_parm() SEQ %d %s\n",
                input_packet.seq, parm);
#endif /* DEBUG_KERMIT */

        /* Only the receiver can ACK */
        assert(status.sending == Q_FALSE);

        output_packet.parsed_ok = Q_TRUE;
        output_packet.type = P_KACK;
        output_packet.seq = input_packet.seq;
        memcpy(output_packet.data, parm, parm_n);
        output_packet.data_n = parm_n;
} /* ---------------------------------------------------------------------- */

/*
 * Send ERROR packet to remote side
 */
static void error_packet(char * message) {
#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "error_packet() SEQ %lu (%lu) %s\n",
                (status.sequence_number % 64), status.sequence_number, message);
#endif /* DEBUG_KERMIT */
        output_packet.parsed_ok = Q_TRUE;
        output_packet.type = P_KERROR;
        output_packet.seq = (status.sequence_number % 64);
        memcpy(output_packet.data, message, strlen(message));
        output_packet.data_n = strlen(message);
} /* ---------------------------------------------------------------------- */

/*
 * Generic ACK for FILE-HEADER packet
 */
static void ack_file_packet() {
#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "ack_file_packet() SEQ %d, filename = %s\n",
                input_packet.seq,
                status.file_name);
#endif /* DEBUG_KERMIT */

        output_packet.parsed_ok = Q_TRUE;
        output_packet.type = P_KACK;
        output_packet.seq = input_packet.seq;
        memcpy(output_packet.data, status.file_name, strlen(status.file_name));
        output_packet.data_n = strlen(status.file_name);
} /* ---------------------------------------------------------------------- */

/* nak_packet() needs to know if it can safely append to the window */
static Q_BOOL window_next_packet_seq(const int seq);

/*
 * Generic NAK
 */
static void nak_packet() {
        int i = -1;
        int seq = input_packet.seq;
        Q_BOOL found_right_nak = Q_FALSE;
        int seq_end_i;

        /* Only the receiver can NAK */
        assert(status.sending == Q_FALSE);

        if (input_window_n > 0) {
                i = input_window_begin;
                do {
                        if (input_window[i].acked == Q_FALSE) {
                                /* NAK the oldest un-ACK'd packet */
                                seq = input_window[i].seq;
                                found_right_nak = Q_TRUE;
                                break;
                        }
                        /* This must be at the end of the loop. */
                        i++;
                        i %= session_parms.WINDO_in;
                } while (i != input_window_i);
                if (found_right_nak == Q_FALSE) {
                        /*
                         * Did not find anything to NAK within the window, so
                         * NAK the next expected packet.
                         */
                        seq_end_i = input_window_i - 1;
                        if (seq_end_i < 0) {
                                seq_end_i = input_window_n - 1;
                        }
                        seq = input_window[seq_end_i].seq + 1;
                }
        } else {
                /* The no-window case. */
                seq = status.sequence_number + 1;
        }
        /* Very first NAK packet. */
        if ((status.sequence_number == 0) && (input_packet.seq == 0)) {
                seq = 0;
        }

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "nak_packet() SEQ %u\n", seq);
#endif /* DEBUG_KERMIT */

        output_packet.parsed_ok = Q_TRUE;
        output_packet.type = P_KNAK;
        output_packet.seq = seq;
        output_packet.data_n = 0;

        /* Save errors */
        stats_increment_errors(_("NAK - SEQ %u"), seq);

        /*
         * Save to the input window.
         */
        if (session_parms.windowing == Q_TRUE) {

                if (window_next_packet_seq(input_packet.seq) == Q_FALSE) {
#if 0
                        /*
                         * Another case: if we're trying to NAK an
                         * already-ACK'd packet, don't send anything out.
                         */
                        if (window_within(input_packet.seq) == Q_TRUE) {
#ifdef DEBUG_KERMIT
                                fprintf(DEBUG_FILE_HANDLE, "nak_packet() NOT SENDING NAK - packet got here earlier - error in SEQ byte\n");
#endif /* DEBUG_KERMIT */
                                output_packet.parsed_ok = Q_FALSE;
                                return;
                        }
#endif
                        /*
                         * Do NOT add this to the window - it would create a
                         * gap or repeat in the window.
                         */
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "nak_packet() NOT APPENDING TO WINDOW - would create gap/loop\n");
#endif /* DEBUG_KERMIT */
                        return;
                }

                if ((input_window_n == session_parms.WINDO_in) &&
                        (input_window[input_window_begin].acked == Q_FALSE)) {
                        /* The window cannot grow, make this a NOP */
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "nak_packet() NOT APPENDING TO WINDOW - FULL - STALL\n");
#endif /* DEBUG_KERMIT */
                        output_packet.parsed_ok = Q_FALSE;
                        return;
                }

#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "nak_packet() adding to window slot %d with SEQ %u\n", input_window_i, input_packet.seq);
#endif /* DEBUG_KERMIT */

                assert(session_parms.WINDO_in > 0);

                /* Roll off the bottom if needed */
                if ((input_window[input_window_begin].acked == Q_TRUE) &&
                        (input_window_n == session_parms.WINDO_in)) {

                        if (input_window[input_window_begin].type == P_KDATA) {
#ifdef DEBUG_KERMIT
                                fprintf(DEBUG_FILE_HANDLE, "nak_packet() write %d bytes to file\n", input_window[input_window_begin].data_n);
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                                fwrite(input_window[input_window_begin].data, 1,
                                        input_window[input_window_begin].data_n,
                                        status.file_stream);
                                status.file_position += input_window[input_window_begin].data_n;
                                q_transfer_stats.bytes_transfer = status.file_position;
                                stats_increment_blocks();
                        }
                        Xfree(input_window[input_window_begin].data, __FILE__, __LINE__);
                        input_window[input_window_begin].data = NULL;
                        input_window_begin++;
                        input_window_begin %= session_parms.WINDO_in;
                        input_window_n--;

                        input_window[input_window_i].type = input_packet.type;
                        input_window[input_window_i].seq = input_packet.seq;
                        input_window[input_window_i].try_count = 1;
                        input_window[input_window_i].acked = Q_FALSE;
                        assert(input_window[input_window_i].data == NULL);
                        input_window[input_window_i].data_n = 0;
                        input_window_i++;
                        input_window_i %= session_parms.WINDO_in;
                        input_window_n++;
                } else {
                        /*
                         * We just sent the NAK for this one, so don't add
                         * another.
                         */
                }
        }

} /* ---------------------------------------------------------------------- */

/*
 * Read bytes from input, decode into input_packet.  Returns true if a packet
 * got taken out of input (even if the CRC check failed).
 */
static Q_BOOL decode_input_bytes(unsigned char * input,
        const unsigned int input_n, unsigned int * discard) {

        unsigned char * check_begin;
        unsigned int begin = 0;
        unsigned int mark_begin = 0;
        unsigned char checksum;
        unsigned short checksum2;
        unsigned short crc;
        unsigned char type_char;
        unsigned int data_length;
        unsigned int data_check_diff;
        unsigned check_type;
        unsigned check_type_length;
        unsigned short lenx1;
        unsigned short lenx2;
        unsigned short len;
        unsigned short hcheck_given = -1;
        unsigned short hcheck_computed = 0;

#ifdef DEBUG_KERMIT
        int i;
        fprintf(DEBUG_FILE_HANDLE, "decode_input_bytes() %d check_type = %d\n",
                input_n, status.check_type);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        if (input_n < 5) {
                /* Nothing to do */
                *discard = 0;
                return Q_FALSE;
        }

        /* Clear packet */
        input_packet.parsed_ok = Q_FALSE;
        input_packet.seq = 0;
        input_packet.type = 0;
        input_packet.length = 0;
        input_packet.long_packet = Q_FALSE;
        memset(input_packet.data, 0, input_packet.data_max);
        input_packet.data_n = 0;

        /* Find the start of the packet */
        while (input[begin] != session_parms.MARK) {
                /* Strip non-mark characters */
                begin++;
                if (begin >= input_n) {
                        /*
                         * Throw away what's here, we're still looking
                         * for a packet beginning.
                         */
                        *discard = begin;
                        return Q_FALSE;
                }
        }
        /*
         * We found the MARK, hang onto that location in case we need to
         * reparse.
         */
        mark_begin = begin;
#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "decode_input_bytes(): mark_begin %d\n", mark_begin);
#endif /* DEBUG_KERMIT */

        /* MARK - ignore */
        begin++;

        /* LEN */
        len = kermit_unchar(input[begin]);
        input_packet.length = len;
        begin++;

        if (input_packet.length == 0) {
                /*
                 * LEN is 0.  This is either an error or an
                 * extended-length packet.
                 */
                if (session_parms.long_packets == Q_TRUE) {
                        /* Extended-length packet. */
                        input_packet.long_packet = Q_TRUE;
                } else {
                        /* Tell the other side to re-send */
                        if (status.sending == Q_FALSE) {
                                nak_packet();
                        }

                        /* Discard everything in the input buffer */
                        *discard = input_n;
                        return Q_TRUE;
                }
        } else if ((input_packet.length == 1) || (input_packet.length == 2)) {
                /* This is definitely an error. */
                /* Tell the other side to re-send */
                if (status.sending == Q_FALSE) {
                        nak_packet();
                }

                /* Discard everything in the input buffer */
                *discard = input_n;
                return Q_TRUE;
        }
        /* Sanity check the length field */
        if (input_packet.long_packet == Q_FALSE) {
                if (input_packet.length > session_parms.MAXL) {
                        /* Bad LEN field */
                        /* Tell the other side to re-send */
                        if (status.sending == Q_FALSE) {
                                nak_packet();
                        }

                        /* Discard everything in the input buffer */
                        *discard = input_n;
                        return Q_TRUE;
                }
        }

        if (input_packet.long_packet == Q_FALSE) {
                /*
                 * We have the packet length, look for all the bytes to be
                 * here before trying to read it all.
                 */
                if ((input_n - begin) < input_packet.length) {
                        /* Still waiting for the rest of the packet */
                        *discard = mark_begin;
                        return Q_FALSE;
                }
        } else {
                /*
                 * We need at least 5 more bytes before we can look to see
                 * if the whole packet is here.
                 */
                if ((input_n - begin) < 5) {
                        /* Still waiting for the extended header */
                        *discard = mark_begin;
                        return Q_FALSE;
                }
        }
        check_begin = input + begin - 1;

        /* SEQ */
        input_packet.seq = kermit_unchar(input[begin]);
        begin++;
        if ((input_packet.seq < 0) || (input_packet.seq > 63)) {
                /* Bam, bad packet */
                /* Tell the other side to re-send */
                if (status.sending == Q_FALSE) {
                        nak_packet();
                }

                /* Discard everything in the input buffer */
                *discard = input_n;
                return Q_TRUE;
        }

        /* TYPE */
        type_char = input[begin];
        input_packet.type = packet_type(type_char);
        begin++;

        if (input_packet.long_packet == Q_TRUE) {
                /* LENX1, LENX2, HCHECK */
                lenx1 = kermit_unchar(input[begin]);
                begin++;
                lenx2 = kermit_unchar(input[begin]);
                begin++;
                input_packet.length = lenx1 * 95 + lenx2;

                /* Sanity check the length field */
                if (input_packet.length > session_parms.MAXLX1 * 95 + session_parms.MAXLX2) {
                        /* Bad length field */
                        /* Tell the other side to re-send */
                        if (status.sending == Q_FALSE) {
                                nak_packet();
                        }

                        /* Discard everything in the input buffer */
                        *discard = input_n;
                        return Q_TRUE;
                }

                /*
                 * To make the two packet lengths mean the same thing,
                 * include the extended header, SEQ, and TYPE in the length.
                 */
                input_packet.length += 5;

                /* Grab and compute the extended header checksum */
                hcheck_given = kermit_unchar(input[begin]);
                begin++;
                assert(begin >= 6);

                hcheck_computed = input[begin - 6] + input[begin - 5] + input[begin - 4] + input[begin - 3] + input[begin - 2];
                hcheck_computed = (hcheck_computed + ((hcheck_computed & 192)/64)) & 63;

                /* Sanity check the HCHECK field */
                if (hcheck_given != hcheck_computed) {
                        /* Bad extended header */
                        /* Tell the other side to re-send */
                        if (status.sending == Q_FALSE) {
                                nak_packet();
                        }

                        /* Discard everything in the input buffer */
                        *discard = input_n;
                        return Q_TRUE;
                }
        }

        if (input_packet.long_packet == Q_TRUE) {

#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "decode_input_bytes(): got EXTENDED packet. HCHECK %04x %04x extended-length %u begin %u remaining %u\n",
                        hcheck_given, hcheck_computed, input_packet.length,
                        begin,
                        input_n - begin);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                if ((input_n - begin) < input_packet.length - 5) {
                        /*
                         * Still waiting for the extended packet data to get
                         * here.
                         */
                        *discard = mark_begin;
                        return Q_FALSE;
                }
        }

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "decode_input_bytes(): %d packet bytes (hex): ", input_packet.length);
        for (i = 0; i < input_packet.length + 2; i++) {
                fprintf(DEBUG_FILE_HANDLE, "%02x ",
                        (*(check_begin + i - 1) & 0xFF));
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        fprintf(DEBUG_FILE_HANDLE, "decode_input_bytes(): %d packet bytes (ASCII): ", input_packet.length);
        for (i = 0; i < input_packet.length + 2; i++) {
                fprintf(DEBUG_FILE_HANDLE, "%c ",
                        (*(check_begin + i - 1) & 0xFF));
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        switch (input_packet.type) {
        case P_KSINIT:
                check_type = 1;
                break;
        case P_KNAK:
                check_type = len - 2;
                if ((check_type < 1) || (check_type > 3)) {
                        check_type = 1;
                }
                break;
        default:
                check_type = status.check_type;
                break;
        }
        if (check_type != 12) {
                check_type_length = check_type;
        } else {
                check_type_length = 2;
        }
        if (input_packet.long_packet == Q_TRUE) {
                data_length = input_packet.length - 5 - check_type_length;
        } else {
                data_length = input_packet.length - 2 - check_type_length;
        }

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "decode_input_bytes(): got packet. LEN %d SEQ %d (status %lu) TYPE %c (%s)\n",
                input_packet.length,
                input_packet.seq,
                status.sequence_number,
                type_char,
                packet_type_string(type_char));
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        if (input_packet.long_packet == Q_TRUE) {
                data_check_diff = 6;
        } else {
                data_check_diff = 3;
        }

        /* Check the checksum */
        if (check_type == 1) {
                checksum = kermit_tochar(compute_checksum(check_begin,
                                data_length + data_check_diff));
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "decode_input_bytes(): type 1 checksum: %c (%02x)\n",
                        checksum, checksum);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                if (checksum == check_begin[data_length + data_check_diff]) {
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "decode_input_bytes(): type 1 checksum OK\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                } else {
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "decode_input_bytes(): type 1 checksum FAIL\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                        /* Tell the other side to re-send */
                        if (status.sending == Q_FALSE) {
                                nak_packet();
                        }

                        /* Discard everything in the input buffer */
                        *discard = input_n;
                        return Q_TRUE;
                }
        }

        if (check_type == 2) {
                checksum2 = compute_checksum2(check_begin,
                        data_length + data_check_diff);
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "decode_input_bytes(): type 2 checksum: %c %c (%04x)\n",
                        kermit_tochar((checksum2 >> 6) & 0x3F),
                        kermit_tochar(checksum2 & 0x3F),
                        checksum2);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                if (checksum2 == ((kermit_unchar(check_begin[data_length + data_check_diff]) << 6) |
                                kermit_unchar(check_begin[data_length + data_check_diff + 1]))
                ) {

#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "decode_input_bytes(): type 2 checksum OK\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                } else {
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "decode_input_bytes(): type 2 checksum FAIL\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                        /* Tell the other side to re-send */
                        if (status.sending == Q_FALSE) {
                                nak_packet();
                        }

                        /* Discard everything in the input buffer */
                        *discard = input_n;
                        return Q_TRUE;
                }
        }

        if (check_type == 12) {
                checksum2 = compute_checksum2(check_begin,
                        data_length + data_check_diff);
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "decode_input_bytes(): type B checksum: %c %c (%04x)\n",
                        kermit_tochar(((checksum2 >> 6) & 0x3F) + 1),
                        kermit_tochar((checksum2 & 0x3F) + 1),
                        checksum2);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                if (checksum2 == (
                        ((kermit_unchar(check_begin[data_length + data_check_diff]) - 1) << 6) |
                        (kermit_unchar(check_begin[data_length + data_check_diff + 1]) - 1))
                ) {

#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "decode_input_bytes(): type B checksum OK\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                } else {
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "decode_input_bytes(): type B checksum FAIL\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                        /* Tell the other side to re-send */
                        if (status.sending == Q_FALSE) {
                                nak_packet();
                        }

                        /* Discard everything in the input buffer */
                        *discard = input_n;
                        return Q_TRUE;
                }
        }

        if (check_type == 3) {
                crc = compute_crc16(check_begin, data_length + data_check_diff);
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "decode_input_bytes(): type 3 CRC16: %c %c %c (%04x)\n",
                        kermit_tochar((crc >> 12) & 0x0F),
                        kermit_tochar((crc >> 6) & 0x3F),
                        kermit_tochar(crc & 0x3F),
                        crc);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                if (crc == ((kermit_unchar(check_begin[data_length + data_check_diff]) << 12) |
                                (kermit_unchar(check_begin[data_length + data_check_diff + 1]) << 6) |
                                kermit_unchar(check_begin[data_length + data_check_diff + 2]))
                ) {
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "decode_input_bytes(): type 3 CRC16 OK\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                } else {
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "decode_input_bytes(): type 3 CRC16 FAIL\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                        /* Tell the other side to re-send */
                        if (status.sending == Q_FALSE) {
                                nak_packet();
                        }

                        /* Discard everything in the input buffer */
                        *discard = input_n;
                        return Q_TRUE;
                }
        }

        /* Handle prefixing and such */
        if (decode_data_field(input_packet.type, input + begin,
                        (input_packet.long_packet == Q_TRUE ?
                                input_packet.length - 5 - check_type_length :
                                input_packet.length - 2 - check_type_length),
                        &input_packet.data, &input_packet.data_n,
                        &input_packet.data_max) == Q_FALSE) {

                /* This packet has an error */
                input_packet.parsed_ok = Q_FALSE;

                /* Tell the other side to re-send */
                if (status.sending == Q_FALSE) {
                        nak_packet();
                }

                /* Discard everything in the input buffer */
                *discard = input_n;
                return Q_TRUE;
        }

        /* The packet layer is OK, now process the data payload */
        input_packet.parsed_ok = Q_TRUE;

        switch (input_packet.type) {
        case P_KSINIT:
                /* Send-Init */
                input_packet.parsed_ok = process_send_init();
                break;
        case P_KFILE:
                /* File-Header */
                input_packet.parsed_ok = process_file_header();
                break;
        case P_KATTRIBUTES:
                /* Attributes */
                input_packet.parsed_ok = process_attributes();
                break;
        case P_KERROR:
                /* Error */
                process_error_packet();
                break;
        case P_KRESERVED1:
        case P_KRESERVED2:
                /* Sender isn't Kermit compliant, abort. */
                set_transfer_stats_last_message(_("ERROR - WRONG PACKET TYPE"));
                status.state = ABORT;
                stop_file_transfer(Q_TRANSFER_STATE_ABORT);
                input_packet.parsed_ok = Q_FALSE;
                error_packet("Improper packet type");
                break;
        case P_KNAK:
                /*
                 * If we're streaming, this is always an error during the
                 * data transfer portion.
                 */
                if ((status.sending == Q_TRUE) &&
                        (session_parms.streaming == Q_TRUE) &&
                        (       (status.state == KM_SDW) ||
                                (status.state == KM_SZ))
                ) {
                        set_transfer_stats_last_message(_("ERROR - NAK WHILE STREAMING"));
                        status.state = ABORT;
                        stop_file_transfer(Q_TRANSFER_STATE_ABORT);
                        input_packet.parsed_ok = Q_FALSE;
                        error_packet("NAK while streaming");
                }
                break;
        case P_KACK:
        case P_KEOF:
        case P_KBREAK:
        case P_KDATA:
                /* Don't need any special payload processing */
                break;
        case P_KSERVINIT:
        case P_KRINIT:
        case P_KTEXT:
        case P_KCOMMAND:
        case P_KKERMIT_COMMAND:
        case P_KGENERIC_COMMAND:
                /* Will not support */
                break;
        }

        /* Discard what's been processed */
        if (input_packet.long_packet == Q_TRUE) {
                *discard = begin + input_packet.length - 3 - 2;
        } else {
                *discard = begin + input_packet.length - 2;
        }

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "decode_input_bytes(): input_packet.parsed_ok = %s\n",
                (input_packet.parsed_ok == Q_TRUE ? "true" : "false"));
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        /* All OK */
        return Q_TRUE;
} /* ---------------------------------------------------------------------- */

/*
 * Encode output_packet into bytes
 */
static void encode_output_packet(unsigned char * output, int * output_n,
        const int output_max) {

        unsigned char checksum;
        short checksum2;
        short crc;
        unsigned int data_length = 0;
        unsigned int data_check_diff = 3;
        int packet_length;
        unsigned char type_char;
        unsigned char * check_begin;
        unsigned short hcheck_computed = 0;
        int check_type;
        int check_type_length;
        int my_output_n = 0;

        if (output_packet.parsed_ok == Q_FALSE) {
                return;
        }

        type_char = packet_type_chars[output_packet.type].packet_char;

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "encode_output_bytes(): SEQ %d TYPE %c (%s) data_n %d\n",
                output_packet.seq, type_char,
                packet_type_string(type_char),
                output_packet.data_n);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        /* MARK */
        output[0] = session_parms.MARK;

        /* LEN - do later */

        /* SEQ */
        output[2] = kermit_tochar(output_packet.seq);

        /* TYPE */
        output[3] = type_char;

        /* Default: do not use a long packet */
        output_packet.long_packet = Q_FALSE;
        switch (output_packet.type) {
        case P_KSINIT:
        case P_KNAK:
                check_type = 1;
                break;
        case P_KACK:
                /*
                 * Special case: use the type 1 check for the ACK to a
                 * SEND-INIT.
                 */
                if (status.sequence_number == 0) {
                        check_type = 1;
                } else {
                        check_type = status.check_type;
                }
                break;
        case P_KDATA:
                if (session_parms.long_packets == Q_TRUE) {
                        output_packet.long_packet = Q_TRUE;
                        data_check_diff = 6;
                } else {
                        output_packet.long_packet = Q_FALSE;
                }
                check_type = status.check_type;
                break;
        default:
                check_type = status.check_type;
                break;
        }
        if (check_type == 12) {
                check_type_length = 2;
        } else {
                check_type_length = check_type;
        }

        /* Encode the data field */
        encode_data_field(output_packet.type, output_packet.data,
                output_packet.data_n, output + data_check_diff + 1,
                &data_length);
        packet_length = data_length + data_check_diff - 1 + check_type_length;
        if (output_packet.long_packet == Q_TRUE) {
                output[1] = kermit_tochar(0);
                /* LENX1 and LENX2 */
                output[4] = kermit_tochar((data_length + 3) / 95);
                output[5] = kermit_tochar((data_length + 3) % 95);
                /* HCHECK */
                hcheck_computed = output[1] + output[2] + output[3] + output[4] + output[5];
                hcheck_computed = (hcheck_computed + ((hcheck_computed & 192)/64)) & 63;
                output[6] = kermit_tochar(hcheck_computed);
        } else {
                output[1] = kermit_tochar(packet_length);
        }
#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "encode_output_bytes(): long_packet %s packet_length %d data_length %d check_type %d status.check_type %d\n",
                (output_packet.long_packet == Q_TRUE ? "true" : "false"),
                packet_length, data_length, check_type, status.check_type);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        /* Create the checksum */
        check_begin = output + 1;

        if (check_type == 1) {
                checksum = kermit_tochar(compute_checksum(check_begin,
                                data_length + data_check_diff));
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "encode_output_bytes(): type 1 checksum: %c (%02x)\n",
                        checksum, checksum);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                check_begin[data_length + data_check_diff] = checksum;
        }

        if (check_type == 2) {
                checksum2 = compute_checksum2(check_begin,
                        data_length + data_check_diff);
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "encode_output_bytes(): type 2 checksum: %c %c (%04x)\n",
                        kermit_tochar((checksum2 >> 6) & 0x3F),
                        kermit_tochar(checksum2 & 0x3F),
                        checksum2);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                check_begin[data_length + data_check_diff] = kermit_tochar((checksum2 >> 6) & 0x3F);
                check_begin[data_length + data_check_diff + 1] = kermit_tochar(checksum2 & 0x3F);
        }

        if (check_type == 12) {
                checksum2 = compute_checksum2(check_begin,
                        data_length + data_check_diff);
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "encode_output_bytes(): type B checksum: %c %c (%04x)\n",
                        kermit_tochar(((checksum2 >> 6) & 0x3F) + 1),
                        kermit_tochar((checksum2 & 0x3F) + 1),
                        checksum2);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                check_begin[data_length + data_check_diff] = kermit_tochar(((checksum2 >> 6) & 0x3F) + 1);
                check_begin[data_length + data_check_diff + 1] = kermit_tochar((checksum2 & 0x3F) + 1);
        }

        if (check_type == 3) {
                crc = compute_crc16(check_begin, data_length + data_check_diff);
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "encode_output_bytes(): type 3 CRC16: %c %c %c (%04x)\n",
                        kermit_tochar((crc >> 12) & 0x0F),
                        kermit_tochar((crc >> 6) & 0x3F),
                        kermit_tochar(crc & 0x3F),
                        crc);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                check_begin[data_length + data_check_diff] = kermit_tochar((crc >> 12) & 0x0F);
                check_begin[data_length + data_check_diff + 1] = kermit_tochar((crc >> 6) & 0x3F);
                check_begin[data_length + data_check_diff + 2] = kermit_tochar(crc & 0x3F);
        }

        output[packet_length + 2] = session_parms.EOL;
        my_output_n = packet_length + 3;

        /* Do not repeat */
        output_packet.parsed_ok = Q_FALSE;

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "encode_output_bytes(): %d bytes\n", my_output_n);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        if (    (       (session_parms.streaming == Q_TRUE) ||
                        (session_parms.windowing == Q_TRUE)) &&
                (output_packet.type == P_KDATA)
        ) {
                /* Assume everything delivers OK */
                status.file_position += status.outstanding_bytes;
                q_transfer_stats.bytes_transfer = status.file_position;
                stats_increment_blocks();

#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "encode_output_packet() file_position %lu file_size %u outstanding_bytes %lu\n",
                        status.file_position, status.file_size,
                        status.outstanding_bytes);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
        }

        *output_n += my_output_n;
} /* ---------------------------------------------------------------------- */

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */
/* Top-level states ------------------------------------------------------- */
/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

/*
 * Receive:  R
 */
static Q_BOOL receive_R() {
#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "KERMIT: receive_R()\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        if (status.first_R == Q_TRUE) {
                set_transfer_stats_last_message(_("WAITING FOR SEND-INIT..."));
                status.first_R = Q_FALSE;
        }

        /* See if a packet is here */
        if (input_packet.parsed_ok == Q_FALSE) {
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: receive_R() no data\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                /* No packet yet */
                return Q_TRUE;
        }

        switch (input_packet.type) {
        case P_KSINIT:
                /* Got Send-Init */
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: receive_R() got Send-Init\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                set_transfer_stats_last_message(_("ACK SEND-INIT"));
                negotiate_send_init();

                ack_send_init();
                /* Remove input packet from processing */
                input_packet.parsed_ok = Q_FALSE;

                /* Move to new state */
                set_transfer_stats_last_message(_("WAITING FOR FILE HEADER..."));
                status.state = KM_RF;
                return Q_TRUE;
        default:
                /* Sender isn't Kermit compliant, abort. */
                set_transfer_stats_last_message(_("PACKET SEQUENCE ERROR"));
                status.state = ABORT;
                stop_file_transfer(Q_TRANSFER_STATE_ABORT);
                error_packet("Wrong packet in sequence");
                return Q_TRUE;
        }

        /* Process through the new state */
        return Q_FALSE;
} /* ---------------------------------------------------------------------- */

/*
 * Receive:  RF
 */
static Q_BOOL receive_RF() {
#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "KERMIT: receive_RF()\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        /* See if a packet is here */
        if (input_packet.parsed_ok == Q_FALSE) {
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: receive_RF() no data\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                /* No packet yet */
                return Q_TRUE;
        }

        switch (input_packet.type) {

        case P_KFILE:
                /* File-Header */
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: receive_RF() got File-Header\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                set_transfer_stats_last_message(_("FILE HEADER"));

                ack_file_packet();
                /* Remove input packet from processing */
                input_packet.parsed_ok = Q_FALSE;

                /* Move to new state */
                set_transfer_stats_last_message(_("WAITING FOR ATTRIBUTES OR FILE DATA..."));
                status.state = KM_RDW;
                return Q_TRUE;

        case P_KBREAK:
                /* Break */
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: receive_RF() got EOT (BREAK)\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                /* Remove input packet from processing */
                input_packet.parsed_ok = Q_FALSE;

                set_transfer_stats_last_message(_("END OF TRANSMISSION"));

                /* We send the ACK, but don't care if the remote side gets it */
                ack_packet(Q_TRUE);

                /* Move to new state */
                status.state = COMPLETE;

                /* Update transfer stats */
                set_transfer_stats_last_message(_("SUCCESS"));
                stop_file_transfer(Q_TRANSFER_STATE_END);
                time(&q_transfer_stats.end_time);

                /* Play music */
                play_sequence(Q_MUSIC_DOWNLOAD);
                return Q_TRUE;

        default:
                /* Sender isn't Kermit compliant, abort. */
                set_transfer_stats_last_message(_("PACKET SEQUENCE ERROR"));
                status.state = ABORT;
                stop_file_transfer(Q_TRANSFER_STATE_ABORT);
                error_packet("Wrong packet in sequence");
                return Q_TRUE;
        }

        /* Process through the new state */
        return Q_FALSE;
} /* ---------------------------------------------------------------------- */

static Q_BOOL window_save_all();

/*
 * Receive:  RDW
 */
static Q_BOOL receive_RDW() {
        struct utimbuf utime_buffer;

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "KERMIT: receive_RDW()\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        /* See if a packet is here */
        if (input_packet.parsed_ok == Q_FALSE) {
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: receive_RDW() no data\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                /* No packet yet */
                return Q_TRUE;
        }

        switch (input_packet.type) {
        case P_KEOF:
                /* EOF */
                if ((input_packet.data_n > 0) &&
                        (input_packet.data[0] == 'D')
                ) {

                        /* Remote side skipped this file */
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "KERMIT: receive_RF() got EOF (SKIP)\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                        set_transfer_stats_last_message(_("SKIP FILE"));

#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "KERMIT: receive_RF() PARTIAL (SKIPPED) file download complete: %sF\n", status.file_name);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                        /* Log it */
                        qlog(_("DOWNLOAD FILE COMPLETE (PARTIAL): protocol %s, filename %s, filesize %d\n"),
                                q_transfer_stats.protocol_name,
                                q_transfer_stats.filename,
                                status.file_position);

                } else {
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "KERMIT: receive_RF() got EOF\n");
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */


                        if (session_parms.windowing == Q_TRUE) {
                                if (window_save_all() != Q_TRUE) {
                                        /*
                                         * We still have some outstanding
                                         * packets in the window, we're not
                                         * done yet.
                                         */
                                        nak_packet();

                                        /*
                                         * Remove input packet from
                                         * processing.
                                         */
                                        input_packet.parsed_ok = Q_FALSE;
                                        return Q_FALSE;
                                }
                        }

                        set_transfer_stats_last_message(_("EOF"));

#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "KERMIT: receive_RF() file download complete: %s\n", status.file_name);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                        /* Log it */
                        qlog(_("DOWNLOAD FILE COMPLETE: protocol %s, filename %s, filesize %d\n"),
                                q_transfer_stats.protocol_name,
                                q_transfer_stats.filename,
                                status.file_position);

                }

                q_transfer_stats.state = Q_TRANSFER_STATE_FILE_DONE;

                /* Close file */
                if (status.file_protection != 0xFFFF) {
#ifndef Q_PDCURSES_WIN32
                        fchmod(fileno(status.file_stream),
                                status.file_protection);
#endif /* Q_PDCURSES_WIN32 */
                }
                fclose(status.file_stream);

                /* Set access time */
                utime_buffer.actime  = status.file_modtime;
                /* Set modification time */
                utime_buffer.modtime = status.file_modtime;
                utime(status.file_fullname, &utime_buffer);

                /* Clean up */
                assert(status.file_name != NULL);
                Xfree(status.file_name, __FILE__, __LINE__);
                status.file_name = NULL;
                status.file_stream = NULL;

                ack_packet(Q_TRUE);

                /* Remove input packet from processing */
                input_packet.parsed_ok = Q_FALSE;

                /* Move to new state */
                set_transfer_stats_last_message(_("WAITING FOR FILE HEADER..."));
                status.state = KM_RF;
                return Q_FALSE;

        case P_KDATA:
                /* File-Data */
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: receive_RF() got File-Data\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                set_transfer_stats_last_message(_("DATA"));

                /* Increment count */
                status.block_size = input_packet.length;
                q_transfer_stats.bytes_transfer = status.file_position;
                stats_increment_blocks();

                ack_packet(Q_FALSE);

                /* Remove input packet from processing */
                input_packet.parsed_ok = Q_FALSE;
                return Q_TRUE;

        case P_KATTRIBUTES:
                /* Attributes */
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: receive_RF() got Attributes\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                set_transfer_stats_last_message(_("ATTRIBUTES"));

                if (status.file_stream == NULL) {
                        /* Need to open the file */
                        open_receive_file();
                }

                /* Remove input packet from processing */
                input_packet.parsed_ok = Q_FALSE;
                return Q_TRUE;
        default:
                /* Sender isn't Kermit compliant, abort. */
                set_transfer_stats_last_message(_("PACKET SEQUENCE ERROR"));
                status.state = ABORT;
                stop_file_transfer(Q_TRANSFER_STATE_ABORT);
                error_packet("Wrong packet in sequence");
                return Q_TRUE;
        }

        /* Process through the new state */
        return Q_FALSE;
} /* ---------------------------------------------------------------------- */

/*
 * Receive a file via the Kermit protocol from input.
 */
static Q_BOOL kermit_receive() {
        Q_BOOL done;

        done = Q_FALSE;
        while (done == Q_FALSE) {
                switch (status.state) {
                case INIT:
                        /*
                         * This state is where everyone begins.  Start by
                         * waiting for the send-init packet.
                         */
                        status.state = KM_R;
                        status.text_mode = Q_FALSE;
                        break;

                case KM_R:
                        done = receive_R();
                        break;

                case KM_RF:
                        done = receive_RF();
                        break;

                case KM_RDW:
                        done = receive_RDW();
                        break;

                case KM_S:
                case KM_SF:
                case KM_SA:
                case KM_SDW:
                case KM_SZ:
                case KM_SB:
                        /* Send states, this is a programming bug */
                        abort();

                case ABORT:
                case COMPLETE:
                        /*
                         * NOP
                         */
                        done = Q_TRUE;
                        break;
                }
        }
        return done;
} /* ---------------------------------------------------------------------- */

/*
 * Send:  S
 */
static Q_BOOL send_S() {
#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "KERMIT: send_S()\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        if (status.first_S == Q_TRUE) {
                set_transfer_stats_last_message(_("SENDING SEND-INIT..."));
                /* Just like the ACK, but make it SEND-INIT instead */
                ack_send_init();
                output_packet.type = P_KSINIT;
                status.first_S = Q_FALSE;
        }

        /* See if a packet is here */
        if (input_packet.parsed_ok == Q_FALSE) {
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: send_S() no data\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                /* No packet yet */
                return Q_TRUE;
        }

        switch (input_packet.type) {
        case P_KNAK:
                /* NAK */
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: send_S() got NAK\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                /* We need to re-send our Send-Init*/
                /* Just like the ACK, but make it SEND-INIT instead */
                ack_send_init();
                output_packet.type = P_KSINIT;

                /* Remove input packet from processing */
                input_packet.parsed_ok = Q_FALSE;

                /* No packet yet */
                return Q_TRUE;
        case P_KACK:
                /* Ack */
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: send_S() got Ack\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                /*
                 * This is a special case: the ACK to a SEND-INIT must
                 * look like a SEND-INIT.
                 */
                process_send_init();
                negotiate_send_init();

                /* Remove input packet from processing */
                input_packet.parsed_ok = Q_FALSE;

                /* Increment sequence number */
                status.sequence_number++;

                /* Move to new state */
                set_transfer_stats_last_message(_("FILE HEADER"));
                send_file_header();
                status.state = KM_SF;
                return Q_FALSE;
        default:
                /* Receiver isn't Kermit compliant, abort. */
                set_transfer_stats_last_message(_("PACKET SEQUENCE ERROR"));
                status.state = ABORT;
                stop_file_transfer(Q_TRANSFER_STATE_ABORT);
                error_packet("Wrong packet in sequence");
                return Q_TRUE;
        }

        /* Process through the new state */
        return Q_FALSE;
} /* ---------------------------------------------------------------------- */

/*
 * Send:  SF
 */
static Q_BOOL send_SF() {
#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "KERMIT: send_SF()\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        /* See if a packet is here */
        if (input_packet.parsed_ok == Q_FALSE) {
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: send_SF() no data\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                /* No packet yet */
                return Q_TRUE;
        }

        switch (input_packet.type) {
        case P_KACK:
                /* Ack */
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: send_SF() got Ack\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                /* Remove input packet from processing */
                input_packet.parsed_ok = Q_FALSE;

                /* Increment sequence number */
                status.sequence_number++;

                /* Move to new state */
                if (session_parms.attributes == Q_TRUE) {
                        set_transfer_stats_last_message(_("ATTRIBUTES"));
                        send_file_attributes();
                        status.state = KM_SA;
                } else {
                        set_transfer_stats_last_message(_("DATA"));
                        /* Get more data */
                        if (send_file_data() == Q_FALSE) {
                                /* EOF */
                                /* Move to new state */
                                set_transfer_stats_last_message(_("EOF"));
                                send_eof();
                                status.state = KM_SZ;
                        } else {

                                status.state = KM_SDW;
                        }
                }
                return Q_FALSE;
        default:
                /* Receiver isn't Kermit compliant, abort. */
                set_transfer_stats_last_message(_("PACKET SEQUENCE ERROR"));
                status.state = ABORT;
                stop_file_transfer(Q_TRANSFER_STATE_ABORT);
                error_packet("Wrong packet in sequence");
                return Q_TRUE;
        }

        /* Process through the new state */
        return Q_FALSE;
} /* ---------------------------------------------------------------------- */

/*
 * Send:  SA
 */
static Q_BOOL send_SA() {
#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "KERMIT: send_SA()\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        /* See if a packet is here */
        if (input_packet.parsed_ok == Q_FALSE) {
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: send_SA() no data\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                /* No packet yet */
                return Q_TRUE;
        }

        switch (input_packet.type) {
        case P_KACK:
                /* Ack */
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: send_SA() got Ack\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                /* Remove input packet from processing */
                input_packet.parsed_ok = Q_FALSE;

                /* Clear outbound for first data packet */
                output_packet.parsed_ok = Q_FALSE;

#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "do_resend %s data_n %u\n",
                        (status.do_resend == Q_TRUE ? "true" : "false"),
                        input_packet.data_n);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                /* RESEND support */
                if ((status.do_resend == Q_TRUE) && (input_packet.data_n > 0)) {
                        /*
                         * Check the data payload to see if the receiver
                         * wants us to seek ahead.
                         */
                        if (input_packet.data[0] == '1') {
                                status.file_position = atol((char *)input_packet.data + 2);
                                if (status.file_position < 0) {
                                        status.file_position = 0;
                                }
                                fseek(status.file_stream, status.file_position,
                                        SEEK_SET);
                                status.outstanding_bytes = 0;

#ifdef DEBUG_KERMIT
                                fprintf(DEBUG_FILE_HANDLE, "RESEND %d \'%s\'\n",
                                        input_packet.data[1] - 32,
                                        input_packet.data + 2);
                                fprintf(DEBUG_FILE_HANDLE, "RESEND seek to %lu\n",
                                        status.file_position);
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                        }
                }

                if ((session_parms.streaming == Q_TRUE) ||
                        (session_parms.windowing == Q_TRUE)
                ) {
                        /*
                         * Streaming and windowing increment SEQ in
                         * send_SD_next_packet(), called by send_SDW().
                         */
                } else {
                        /* Increment sequence number */
                        status.sequence_number++;
                }

                /* Move to new state */
                set_transfer_stats_last_message(_("DATA"));
                status.state = KM_SDW;
                return Q_FALSE;
        default:
                /* Receiver isn't Kermit compliant, abort. */
                set_transfer_stats_last_message(_("PACKET SEQUENCE ERROR"));
                status.state = ABORT;
                stop_file_transfer(Q_TRANSFER_STATE_ABORT);
                error_packet("Wrong packet in sequence");
                return Q_TRUE;
        }

        /* Process through the new state */
        return Q_FALSE;
} /* ---------------------------------------------------------------------- */

/*
 * Send:  SD - when the next packet makes it to the destination
 */
static void send_SD_next_packet() {
#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "KERMIT: send_SD_next_packet() SEQ %lu\n", status.sequence_number);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        if (    (session_parms.streaming == Q_TRUE) &&
                (output_packet.parsed_ok == Q_TRUE)
        ) {
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: send_SD_next_packet() outbound packet already present\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                /* There's already an outbound packet, NOP */
                return;
        }

        if ((session_parms.streaming == Q_TRUE) ||
                (session_parms.windowing == Q_TRUE)
        ) {
                /* Streaming/windowing: increment SEQ and go on. */
                status.sequence_number++;
        }

        if ((status.file_position == status.file_size) ||
                (status.skip_file == Q_TRUE)
        ) {
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: send_SD_next_packet() EOF\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                /* EOF */

                /* Move to new state */
                set_transfer_stats_last_message(_("EOF"));
                send_eof();
                status.state = KM_SZ;
        } else {
                /* Get more data */
                if (send_file_data() == Q_FALSE) {
                        /* EOF */
                        /* Move to new state */
                        set_transfer_stats_last_message(_("EOF"));
                        send_eof();
                        status.state = KM_SZ;
                }
        }
} /* ---------------------------------------------------------------------- */

/*
 * Send:  SDW
 */
static Q_BOOL send_SDW() {
#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "KERMIT: send_SDW()\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        /* See if a packet is here */
        if (input_packet.parsed_ok == Q_FALSE) {
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: send_SDW() no data\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                /* Streaming support */
                if ((session_parms.streaming == Q_TRUE) ||
                        (session_parms.windowing == Q_TRUE)) {
                        send_SD_next_packet();
                }

                /* No packet yet */
                return Q_TRUE;
        }

        switch (input_packet.type) {
        case P_KACK:
                /* Ack */
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: send_SDW() got Ack\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                /* Remove input packet from processing */
                input_packet.parsed_ok = Q_FALSE;

                if ((session_parms.windowing == Q_TRUE) &&
                        (output_window != NULL)) {
                        /*
                         * We are windowing, and received an ACK.  Just
                         * send the next out, whatever it is.  If we're
                         * at EOF, send_SD_next_packet() will switch
                         * state to KM_SZ.
                         */
                        send_SD_next_packet();
                        return Q_TRUE;
                }

                if ((session_parms.streaming == Q_TRUE) ||
                        (session_parms.windowing == Q_TRUE)
                ) {
                        /*
                         * Streaming and windowing increment SEQ in
                         * send_SD_next_packet(), called by send_SDW().
                         */
                } else {
                        /* Increment sequence number */
                        status.sequence_number++;

                        status.file_position += status.outstanding_bytes;
                        q_transfer_stats.bytes_transfer = status.file_position;
                        stats_increment_blocks();

#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "KERMIT: send_SD() file_position %lu file_size %u outstanding_bytes %lu\n",
                                status.file_position,
                                status.file_size,
                                status.outstanding_bytes);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                }

                send_SD_next_packet();
                return Q_FALSE;
        default:
                /* Receiver isn't Kermit compliant, abort. */
                set_transfer_stats_last_message(_("PACKET SEQUENCE ERROR"));
                status.state = ABORT;
                stop_file_transfer(Q_TRANSFER_STATE_ABORT);
                error_packet("Wrong packet in sequence");
                return Q_TRUE;
        }

        /* Process through the new state */
        return Q_FALSE;
} /* ---------------------------------------------------------------------- */

/*
 * Send:  SZ
 */
static Q_BOOL send_SZ() {
#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "KERMIT: send_SZ()\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        /* See if a packet is here */
        if (input_packet.parsed_ok == Q_FALSE) {
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: send_SZ() no data\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                /* No packet yet */
                return Q_TRUE;
        }

        switch (input_packet.type) {
        case P_KACK:
                /* Ack */
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: send_SZ() got Ack\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                /* Remove input packet from processing */
                input_packet.parsed_ok = Q_FALSE;

                if ((session_parms.windowing == Q_TRUE) &&
                        (output_window != NULL) &&
                        (output_window_n > 0)
                ) {
                        /*
                         * We're waiting on another ACK somewhere down
                         * the line.
                         */
                        return Q_TRUE;
                }

                /* Increment sequence number */
                status.sequence_number++;

#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: send_SZ() UPLOAD COMPLETE \n");
#endif /* DEBUG_KERMIT */

                /* Increase the total batch transfer */
                q_transfer_stats.batch_bytes_transfer += status.file_size;

                q_transfer_stats.state = Q_TRANSFER_STATE_FILE_DONE;
                fclose(status.file_stream);

                /* Log it */
                qlog(_("UPLOAD FILE COMPLETE: protocol %s, filename %s, filesize %d\n"),
                        q_transfer_stats.protocol_name,
                        q_transfer_stats.filename,
                        status.file_size);

                assert(status.file_name != NULL);
                Xfree(status.file_name, __FILE__, __LINE__);
                status.file_name = NULL;
                status.file_stream = NULL;

                /* Setup for the next file. */
                upload_file_list_i++;

                /*
                 * Move to new state - setup_for_next_file() will switch
                 * to KM_SB if necessary.
                 */
                setup_for_next_file();
                return Q_FALSE;
        default:
                /* Receiver isn't Kermit compliant, abort. */
                set_transfer_stats_last_message(_("PACKET SEQUENCE ERROR"));
                status.state = ABORT;
                stop_file_transfer(Q_TRANSFER_STATE_ABORT);
                error_packet("Wrong packet in sequence");
                return Q_TRUE;
        }

        /* Process through the new state */
        return Q_FALSE;
} /* ---------------------------------------------------------------------- */

/*
 * Send:  SB
 */
static Q_BOOL send_SB() {
#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "KERMIT: send_SB()\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        if (status.first_SB == Q_TRUE) {
                set_transfer_stats_last_message(_("SENDING EOT..."));
                send_eot();
                status.first_SB = Q_FALSE;
        }

        /* See if a packet is here */
        if (input_packet.parsed_ok == Q_FALSE) {
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: send_SB() no data\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                /* No packet yet */
                return Q_TRUE;
        }

        switch (input_packet.type) {
        case P_KACK:
                /* Ack */
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: send_SB() got Ack\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                /* Remove input packet from processing */
                input_packet.parsed_ok = Q_FALSE;

                /* Now switch to COMPLETE */
                status.state = COMPLETE;
                set_transfer_stats_last_message(_("SUCCESS"));
                stop_file_transfer(Q_TRANSFER_STATE_END);
                time(&q_transfer_stats.end_time);

                /* Play music */
                play_sequence(Q_MUSIC_UPLOAD);
                return Q_FALSE;
        default:
                /* Receiver isn't Kermit compliant, abort. */
                set_transfer_stats_last_message(_("PACKET SEQUENCE ERROR"));
                status.state = ABORT;
                stop_file_transfer(Q_TRANSFER_STATE_ABORT);
                error_packet("Wrong packet in sequence");
                return Q_TRUE;
        }

        /* Process through the new state */
        return Q_FALSE;
} /* ---------------------------------------------------------------------- */

/*
 * Send a file via the Kermit protocol to output.
 */
static Q_BOOL kermit_send() {
        Q_BOOL done;

        done = Q_FALSE;
        while (done == Q_FALSE) {
                switch (status.state) {
                case INIT:
                        /*
                         * This state is where everyone begins.  Start by
                         * sending the send-init packet.
                         */
                        status.state = KM_S;
                        break;

                case KM_S:
                        done = send_S();
                        break;

                case KM_SF:
                        done = send_SF();
                        break;

                case KM_SA:
                        done = send_SA();
                        break;

                case KM_SDW:
                        done = send_SDW();
                        break;

                case KM_SZ:
                        done = send_SZ();
                        break;

                case KM_SB:
                        done = send_SB();
                        break;

                case KM_R:
                case KM_RF:
                case KM_RDW:
                        /* Receive states, this is a programming bug */
                        abort();

                case ABORT:
                case COMPLETE:
                        /*
                         * NOP
                         */
                        done = Q_TRUE;
                        break;
                }
        }

        return done;
} /* ---------------------------------------------------------------------- */

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */
/* Windowing -------------------------------------------------------------- */
/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

/*
 * Returns Q_TRUE if the sequence is 1 past the window.
 *
 * This function implements Case 1 of the logic on p. 55 of "The Kermit
 * Protocol".
 */
static Q_BOOL window_next_packet_seq(const int seq) {
        int seq_end_i = -1;
        int seq_end = -1;

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "window_next_packet_seq() check SEQ %d\n", seq);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        /* If the window is empty, this is easy */
        if (input_window_n == 0) {
                return Q_TRUE;
        }

        /* Get the SEQs at the beginning and ending of the window */
        seq_end_i = input_window_i - 1;
        if (seq_end_i < 0) {
                seq_end_i = session_parms.WINDO_in - 1;
        }
        seq_end = input_window[seq_end_i].seq;

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "window_next_packet_seq() seq_end %d seq_end_i %d input_window_begin %d input_window_i %d input_window_n %d\n",
                seq_end, seq_end_i,
                input_window_begin, input_window_i, input_window_n);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        if (seq == (seq_end + 1) % 64) {

#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "window_next_packet_seq() TRUE Case 1\n");
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                /* Case 1: The usual case. */
                return Q_TRUE;
        }
        /*
         * Any other case: this will either create a gap, or is already
         * inside the window somewhere.
         */
        return Q_FALSE;
} /* ---------------------------------------------------------------------- */

/*
 * Find the slot in the input window that either matches input_packet's
 * SEQ (where it should go) or is the next slot to append data to.
 *
 * Returns -1 if the packet should be ignored.
 *
 * This function implements the logic on p. 55 of "The Kermit Protocol".
 */
static int find_input_slot() {
        int i;
        int seq_end_i = -1;
        int seq_end = -1;
        int seq_end_ws = -1;
        Q_BOOL lost_packet = Q_FALSE;

        assert(input_packet.parsed_ok == Q_TRUE);

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "find_input_slot() SEQ %d input_window_n %d\n", input_packet.seq, input_window_n);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        /* If the window is empty, this is easy */
        if (input_window_n == 0) {
                return input_window_i;
        }

        /* Get the SEQs at the ending of the window */
        seq_end_i = input_window_i - 1;
        if (seq_end_i < 0) {
                seq_end_i = session_parms.WINDO_in - 1;
        }
        seq_end = input_window[seq_end_i].seq;
        seq_end_ws = (seq_end + session_parms.WINDO_in) % 64;

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "find_input_slot() seq_end %d seq_end_i %d seq_end_ws %d input_window_begin %d input_window_i %d input_window_n %d\n",
                seq_end, seq_end_i, seq_end_ws,
                input_window_begin, input_window_i, input_window_n);
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        if (input_packet.seq == (seq_end + 1) % 64) {
                /* Case 1: The usual case. */

                /*
                 * If input_window_begin is a file data packet, write it to
                 * disk.
                 */
                if ((input_window[input_window_begin].type == P_KDATA) &&
                        (input_window[input_window_begin].acked == Q_TRUE)
                ) {
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "find_input_slot() write %d bytes to file\n", input_window[input_window_begin].data_n);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                        fwrite(input_window[input_window_begin].data, 1,
                                input_window[input_window_begin].data_n,
                                status.file_stream);
                        status.file_position += input_window[input_window_begin].data_n;
                        q_transfer_stats.bytes_transfer = status.file_position;
                        stats_increment_blocks();
                }
                /* Roll off the back of the input window */
                if ((input_window[input_window_begin].acked == Q_TRUE) &&
                        (input_window_n == session_parms.WINDO_in)) {
                        Xfree(input_window[input_window_begin].data, __FILE__, __LINE__);
                        input_window[input_window_begin].data = NULL;
                        input_window_begin++;
                        input_window_begin %= session_parms.WINDO_in;
                        input_window_n--;
                }

#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "find_input_slot() Case 1 %d\n",
                        input_window_i);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                return input_window_i;
        }

        /*
         * Case 2: A packet was lost.  We need to look for the range
         * (seq_end + 2) to (seq_end + WINDO_in).  Due to modulo 64,
         * there are a few different cases that match this.
         */
        if ((seq_end_ws > seq_end + 2) &&
                ((seq_end + 2) <= input_packet.seq) &&
                (input_packet.seq <= seq_end_ws)
        ) {
                /*
                 * Case 2: lost packet.  seq_end and seq_end_ws are not
                 * modulo'd.
                 */
                lost_packet = Q_TRUE;
        }
        if ((seq_end_ws < seq_end + 2) &&
                ((input_packet.seq >= (seq_end + 2)) ||
                        (input_packet.seq <= seq_end_ws))
        ) {
                /*
                 * Case 2: lost packet.  seq_end_ws is modulo'd so it is
                 * less than seq_end.
                 */
                lost_packet = Q_TRUE;
        }
        if (lost_packet == Q_TRUE) {
                /*
                 * We lost a packet along the way somewhere.  NAK the next
                 * one we want.
                 */
                seq_end++;
                seq_end %= 64;

#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "find_input_slot() Case 2: looking for %d\n", seq_end);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                i = input_packet.seq;
                input_packet.seq = seq_end;
                nak_packet();
                input_packet.seq = i;

                /*
                 * Let's go ahead and save everything we have currently, make
                 * gaps, and then save this packet where it belongs.
                 */
                window_save_all();

                /* Recompute seq_end et al*/
                seq_end_i = input_window_i - 1;
                if (seq_end_i < 0) {
                        seq_end_i = session_parms.WINDO_in - 1;
                }
                seq_end = input_window[seq_end_i].seq;
                seq_end_ws = (seq_end + session_parms.WINDO_in) % 64;
                seq_end++;
                seq_end %= 64;

                while ((seq_end != input_packet.seq) &&
                        (input_window_n < session_parms.WINDO_in)
                ) {
                        input_window[input_window_i].seq = seq_end;
                        input_window[input_window_i].acked = Q_FALSE;
                        assert(input_window[input_window_i].data == NULL);
                        input_window[input_window_i].data_n = 0;
                        input_window_i++;
                        input_window_i %= session_parms.WINDO_in;
                        input_window_n++;
                        seq_end++;
                        seq_end %= 64;
                }
                /*
                 * At this point input_window contains NAKs up to the current
                 * good packet, or it's full.
                 */
                if (input_window_n < session_parms.WINDO_in) {
                        /* Save the current packet */
                        input_window[input_window_i].seq = input_packet.seq;
                        input_window[input_window_i].type = input_packet.type;
                        input_window[input_window_i].acked = Q_TRUE;
                        assert(input_window[input_window_i].data == NULL);
                        input_window[input_window_i].data_n = input_packet.data_n;
                        input_window[input_window_i].data = (unsigned char *)Xmalloc(input_packet.data_n, __FILE__, __LINE__);
                        memcpy(input_window[input_window_i].data, input_packet.data, input_packet.data_n);
                        input_window_i++;
                        input_window_i %= session_parms.WINDO_in;
                        input_window_n++;
                }
                return -1;
        }

        /*
         * Case 3: A bad packet got retransmitted and is finally here.
         * Save it.
         */
        if (input_window_n > 0) {
                i = input_window_begin;
                do {
                        if (input_window[i].seq == input_packet.seq) {
#ifdef DEBUG_KERMIT
                                fprintf(DEBUG_FILE_HANDLE, "find_input_slot() Case 3 %d\n",
                                        i);
                                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
                                return i;
                        }
                        i++;
                        i %= session_parms.WINDO_in;
                } while (i != input_window_i);
        }

        /*
         * Case 4: A packet outside the sliding window: ignore it.
         */
#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "find_input_slot() Case 4 -1\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
        return -1;
} /* ---------------------------------------------------------------------- */


/*
 * Find the slot in the output window that matches input_packet's SEQ.
 * Returns -1 if it is outside the window.
 */
static int find_output_slot() {
        int i;

        assert(input_packet.parsed_ok == Q_TRUE);
        if (output_window_n > 1) {
                i = output_window_begin;
                do {
                        if (output_window[i].seq == input_packet.seq) {
                                return i;
                        }
                        i++;
                        i %= session_parms.WINDO_out;
                } while (i != output_window_i);
        } else if (output_window_n == 1) {
                if (output_window[output_window_begin].seq == input_packet.seq) {
                        return output_window_begin;
                }
        }

        /* Not found */
        return -1;
} /* ---------------------------------------------------------------------- */

/*
 * Check for repeated packets from the remote side.
 */
static void check_for_repeat(unsigned char * output, int * output_n,
        const int output_max) {

        int i = -1;
        Q_BOOL resend = Q_FALSE;
        Q_BOOL sequence_error = Q_FALSE;

        /* See if a packet is here */
        if (input_packet.parsed_ok == Q_FALSE) {
                /* Nothing came in, no need to repeat anything. */
                return;
        }

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "check_for_repeat() check SEQ %u\n",
                input_packet.seq);
#endif /* DEBUG_KERMIT */

        /* During streaming, do not do this in RDW or SDW states */
        if (session_parms.streaming == Q_TRUE) {
                if ((status.state == KM_RDW) || (status.state == KM_SDW)) {
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "check_for_repeat() STREAMING\n");
#endif /* DEBUG_KERMIT */
                        return;
                }
        }

        i = find_output_slot();
#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "check_for_repeat() i = %d\n", i);
#endif /* DEBUG_KERMIT */

        if ((i == -1) && (status.sending == Q_TRUE)) {
                /*
                 * NAK outside window.  Special case if this NAK is one
                 * past sequence_number.  The receiver is trying to
                 * "unstick" the transfer.  Clear the entire output
                 * window to make room for the next packet, and turn
                 * this NAK(n+1) into an empty ACK(n).
                 */
                if ((input_packet.seq == (status.sequence_number + 1) % 64) &&
                        (input_packet.type == P_KNAK)
                ) {
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "check_for_repeat() NAK(n+1)\n");
#endif /* DEBUG_KERMIT */
                        if (output_window_n > 0) {
                                i = output_window_begin;
                                do {
                                        assert(output_window[i].data != NULL);
                                        Xfree(output_window[i].data,
                                                __FILE__, __LINE__);
                                        output_window[i].data = NULL;
                                        i++;
                                        i %= session_parms.WINDO_out;
                                        output_window_n--;
                                } while (i != output_window_i);
                        }
                        output_window_i = 0;
                        output_window_begin = 0;

                        input_packet.type = P_KACK;
                        input_packet.seq = status.sequence_number % 64;
                        input_packet.data_n = 0;
                        return;
                }
        }

        if (i != -1) {
                if (status.sending == Q_FALSE) {
                        /*
                         * We're receiving and the sender has repeated
                         * something.  Re-send what we sent last time in
                         * response.
                         */
                        resend = Q_TRUE;
                        assert(output_window[i].seq == input_packet.seq);
                } else {
                        /*
                         * We're sending and the receiver has responded
                         * to something:
                         *
                         * - If it's ACK, we just set a flag.
                         *
                         * - If it's NAK, we resend what we had before.
                         *
                         * - Anything else, the receiver isn't Kermit
                         *   compliant.
                         */
                        switch (input_packet.type) {
                        case P_KACK:
#ifdef DEBUG_KERMIT
                                fprintf(DEBUG_FILE_HANDLE, "check_for_repeat() ACK slot %d\n", i);
#endif /* DEBUG_KERMIT */
                                output_window[i].acked = Q_TRUE;
                                break;
                        case P_KNAK:
                                /* Save errors */
                                stats_increment_errors(_("NAK - SEQ %u"),
                                        input_packet.seq);
                                resend = Q_TRUE;
                                break;
                        default:
                                sequence_error = Q_TRUE;
                                break;
                        }

                }

        } /* if (i == -1) */

        if (resend == Q_TRUE) {
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "check_for_repeat() RESEND SEQ %u in slot %d: %s\n",
                        input_packet.seq, i,
                        packet_type_name(output_window[i].type));
#endif /* DEBUG_KERMIT */
                assert(output_window[i].seq == input_packet.seq);

                memcpy(output + *output_n,
                        output_window[i].data,
                        output_window[i].data_n);
                output_window[i].try_count++;
                *output_n += output_window[i].data_n;

                /* Do not handle this packet again. */
                input_packet.parsed_ok = Q_FALSE;
        }
        if (sequence_error == Q_TRUE) {
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "check_for_repeat() PACKET SEQUENCE ERROR SEQ %u in slot %d: %s\n",
                        input_packet.seq, i,
                        packet_type_name(output_window[i].type));
#endif /* DEBUG_KERMIT */

                /* Receiver isn't Kermit compliant, abort. */
                set_transfer_stats_last_message(_("PACKET SEQUENCE ERROR"));
                status.state = ABORT;
                stop_file_transfer(Q_TRANSFER_STATE_ABORT);
                error_packet("Wrong packet in sequence");

                /* Do not handle this packet again. */
                input_packet.parsed_ok = Q_FALSE;
        }
} /* ---------------------------------------------------------------------- */

#ifdef DEBUG_KERMIT
/*
 * Display the packets in the sliding windows.
 */
static void debug_sliding_windows() {
        int i;
        fprintf(DEBUG_FILE_HANDLE, "%%%%%% INPUT WINDOW: %d slots %%%%%%\n",
                input_window_n);

        if (input_window_n > 0) {
                i = input_window_begin;
                do {
                        fprintf(DEBUG_FILE_HANDLE, "    SLOT %02d SEQ %02d %s %s\n",
                                i, input_window[i].seq,
                                (input_window[i].acked == Q_TRUE ? "ACK" : "NAK"),
                                packet_type_name(input_window[i].type)
                        );

                        /* This MUST be at the end of the loop. */
                        i++;
                        i %= session_parms.WINDO_in;
                } while (i != input_window_i);
        }

        fprintf(DEBUG_FILE_HANDLE, "%%%%%% OUTPUT WINDOW: %d slots %%%%%%\n",
                output_window_n);

        if (output_window_n > 0) {
                i = output_window_begin;
                do {
                        fprintf(DEBUG_FILE_HANDLE, "    SLOT %02d SEQ %02d %s %s\n",
                                i, output_window[i].seq,
                                (output_window[i].acked == Q_TRUE ? "ACKed" : "NAK"),
                                packet_type_name(output_window[i].type)
                        );

                        /* This MUST be at the end of the loop. */
                        i++;
                        i %= session_parms.WINDO_out;
                } while ((i != output_window_i) && (output_window_n > 1));
        }

} /* ---------------------------------------------------------------------- */
#endif /* DEBUG_KERMIT */

/*
 * Save the current packet to the input window.
 */
static void save_input_packet() {
        int i;

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "save_input_packet(): begin %d i %d n %d WINDOW_in %d SEQ %d sequence_number %lu\n",
                input_window_begin,
                input_window_i,
                input_window_n,
                session_parms.WINDO_in,
                input_packet.seq,
                status.sequence_number % 64
        );
#endif /* DEBUG_KERMIT */

        /* See if a packet is here */
        if (input_packet.parsed_ok == Q_FALSE) {
                /* Nothing came in, no need to save anything. */
                return;
        }

        /* Don't save input for sending */
        if (status.sending == Q_TRUE) {
                return;
        }

        /* Save to the input window slot */
        i = find_input_slot();
        if (i == -1) {
                /* Ignore this packet. */
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "save_input_packet(): IGNORE PACKET\n");
#endif /* DEBUG_KERMIT */
                input_packet.parsed_ok = Q_FALSE;
        } else {
                if (input_window[i].data != NULL) {
                        Xfree(input_window[i].data, __FILE__, __LINE__);
                }
                input_window[i].data = (unsigned char *)Xmalloc(input_packet.data_n, __FILE__, __LINE__);
                memcpy(input_window[i].data,
                        input_packet.data,
                        input_packet.data_n);
                input_window[i].data_n = input_packet.data_n;
                input_window[i].seq = input_packet.seq;
                input_window[i].type = input_packet.type;
                input_window[i].acked = Q_TRUE;
                input_window[i].try_count = 0;

#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "save_input_packet(): saved %d bytes to input_window slot %d (%d total before this)\n",
                        input_window[i].data_n, i, input_window_n);
#endif /* DEBUG_KERMIT */

                /*
                 * Sanity check: if we're full, begin and i are the same.
                 */
                if (input_window_n == session_parms.WINDO_in) {
                        assert(input_window_i == input_window_begin);
                }

                /*
                 * If we're appending, grow the window by 1.  If receiving,
                 * increment sequence number.
                 */
                if (i == input_window_i) {
                        if (input_window_n < session_parms.WINDO_in) {
                                input_window_n++;
                                input_window_i++;
                                input_window_i %= session_parms.WINDO_in;
                        } else {
                                /* Sanity check on circular buffer. */
                                assert(i == input_window_begin);
                        }
                        if (status.sending == Q_FALSE) {
                                status.sequence_number++;
                        }
                }
        }
#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "save_input_packet(): CURRENT SEQ %lu %lu input_window_begin %d input_window_i %d input_window_n %d\n",
                status.sequence_number % 64, status.sequence_number,
                input_window_begin, input_window_i, input_window_n);

        debug_sliding_windows();
#endif /* DEBUG_KERMIT */
} /* ---------------------------------------------------------------------- */

/*
 * Re-send the most recent packet to the other side, or drop a NAK to speed
 * things along.
 */
static void handle_timeout(unsigned char * output, int * output_n,
        const int output_max) {

        int i;
        Q_BOOL found_nak = Q_FALSE;

        if (status.sending == Q_FALSE) {
                if (input_window_n > 0) {
                        i = input_window_begin;
                        do {
                                if (input_window[i].acked == Q_FALSE) {
                                        found_nak = Q_TRUE;
                                        break;
                                }
                                i++;
                                i %= session_parms.WINDO_in;
                        } while (i != input_window_i);
                        if (found_nak == Q_TRUE) {
                                input_packet.seq = input_window[i].seq;
                        } else {
                                i = input_window_i;
                                i--;
                                if (i < 0) {
                                        i = session_parms.WINDO_in;
                                }
                                input_packet.seq = input_window[i].seq;
                        }
                } else {
                        input_packet.seq = status.sequence_number;
                }
                nak_packet();
        } else {
                if (session_parms.windowing == Q_TRUE) {
                        if (output_window_n > 0) {
                                i = output_window_begin;
                                do {
                                        if (output_window[i].acked == Q_FALSE) {
                                                found_nak = Q_TRUE;
                                                break;
                                        }
                                        i++;
                                        i %= session_parms.WINDO_out;
                                } while (i != output_window_i);
                                if (found_nak == Q_TRUE) {
                                        memcpy(output + *output_n,
                                                output_window[i].data,
                                                output_window[i].data_n);
                                        output_window[i].try_count++;
                                        *output_n += output_window[i].data_n;
                                } else {
                                        /* This should be a bug */
                                        assert(1 == 0);
                                }
                        }
                }
        }
} /* ---------------------------------------------------------------------- */

/*
 * Save everything in the window, clearing out all ACK'd packets from the
 * front.
 */
static Q_BOOL window_save_all() {
        while (input_window_n > 0) {
                if (input_window[input_window_begin].acked == Q_FALSE) {
                        /* Oops, still have a NAK in here somewhere */
                        return Q_FALSE;
                }

                /*
                 * If input_window_begin is a file data packet, write it to
                 * disk.
                 */
                if (input_window[input_window_begin].type == P_KDATA) {
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "window_save_all() write %d bytes to file\n", input_window[input_window_begin].data_n);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                        fwrite(input_window[input_window_begin].data, 1,
                                input_window[input_window_begin].data_n,
                                status.file_stream);
                        status.file_position += input_window[input_window_begin].data_n;
                        q_transfer_stats.bytes_transfer = status.file_position;
                        stats_increment_blocks();
                }
                /* Roll off the back of the input window */
                Xfree(input_window[input_window_begin].data, __FILE__, __LINE__);
                input_window[input_window_begin].data = NULL;
                input_window_begin++;
                input_window_begin %= session_parms.WINDO_in;
                input_window_n--;
        }

        /* All done */
        return Q_TRUE;
} /* ---------------------------------------------------------------------- */

/*
 * Move sliding window begin/end's as needed.  For senders, this means
 * removing old ACK'd packets, for receivers this means both removing old
 * ACK'd packets and writing to disk.
 */
static void move_windows() {
        if (status.sending == Q_TRUE) {
                /*
                 * Sending: remove ACK'd packets from the output window
                 * until either the window is empty or we have an
                 * un-ACK'd packet at the beginning.
                 */
                while ((output_window_n > 0) &&
                        (output_window[output_window_begin].acked == Q_TRUE)) {

                        output_window_n--;
                        Xfree(output_window[output_window_begin].data, __FILE__, __LINE__);
                        output_window[output_window_begin].data = NULL;
                        output_window_begin++;
                        output_window_begin %= session_parms.WINDO_out;
                }
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "move_windows(): output_window resize down to %d, next slot %d\n", output_window_n, output_window_i);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
        }
} /* ---------------------------------------------------------------------- */

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */
/* Main loop -------------------------------------------------------------- */
/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */


/*
 * Perform the Kermit protocol against input and output.
 */
void kermit(unsigned char * input, int input_n, unsigned char * output,
        int * output_n, const int output_max) {

        unsigned int discard = 0;
        Q_BOOL done = Q_FALSE;
        Q_BOOL toss_input_buffer = Q_FALSE;
        Q_BOOL had_some_input = Q_TRUE;
        unsigned int free_space_needed = 0;
        static int ctrl_c_count = 0;
        int i;
        unsigned int output_n_start;

        /* Check my input arguments */
        assert(input_n >= 0);
        assert(input != NULL);
        assert(output != NULL);
        assert(*output_n >= 0);
        assert(output_max > (KERMIT_BLOCK_SIZE * 2));

        /* Stop if we are done */
        if ((status.state == ABORT) || (status.state == COMPLETE)) {
                return;
        }

        /*
         * Determine the amount of free space needed for the next
         * outgoing packet.
         */
        if (session_parms.long_packets == Q_TRUE) {
                free_space_needed = session_parms.MAXLX1 * 95 + session_parms.MAXLX2;
        } else {
                free_space_needed = session_parms.MAXL;
        }
        free_space_needed += remote_parms.NPAD + 10;

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "*** KERMIT: sequence %lu (%lu) state = %d text_mode = %s input_n = %d output_n = %d ***\n",
                status.sequence_number % 64, status.sequence_number,
                status.state, (status.text_mode == Q_TRUE ? "true" : "false"),
                input_n, *output_n);

        debug_sliding_windows();

        fprintf(DEBUG_FILE_HANDLE, "KERMIT: %d input bytes (hex):  ", input_n);
        for (i = 0; i < input_n; i++) {
                fprintf(DEBUG_FILE_HANDLE, "%02x ", (input[i] & 0xFF));
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        fprintf(DEBUG_FILE_HANDLE, "KERMIT: %d input bytes (ASCII):  ", input_n);
        for (i = 0; i < input_n; i++) {
                fprintf(DEBUG_FILE_HANDLE, "%c ", (input[i] & 0xFF));
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        if ((status.sequence_number == 0) && (status.sent_nak == Q_FALSE)) {
                if ((status.state == INIT) && (status.sending == Q_FALSE)) {
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "KERMIT: SEND NAK\n");
#endif /* DEBUG_KERMIT */

                        /* Toss a NAK on the output to speed things up */
                        nak_packet();
                }

                /*
                 * Also, throw away any data already accumulated in input in
                 * case the other side has filled up with packets.
                 */
                toss_input_buffer = Q_TRUE;

                /* I'm actually using this as a general "first block" flag */
                status.sent_nak = Q_TRUE;
        }

        if (input_n > 0) {
                /* Something was sent to me, so reset timeout */
                reset_timer();
        } else {
                if (check_timeout() == Q_TRUE) {
                        handle_timeout(output, output_n, output_max);
                }
        }

        if (output_max - *output_n < free_space_needed) {
                /* No more room, break out */
                done = Q_TRUE;
        }

        /*
         * Make sure we can store at least one more packet in output window.
         */
        if ((output_window_n == session_parms.WINDO_out) &&
                (status.sending == Q_TRUE) &&
                (input_n == 0) &&
                (packet_buffer_n < 5) &&
                (session_parms.streaming == Q_FALSE)
        ) {
#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: output window full and not enough input data\n");
#endif /* DEBUG_KERMIT */

                /* No more room, break out */
                done = Q_TRUE;
        }

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "KERMIT: enter done = %s\n",
                (done == Q_TRUE ? "true" : "false"));
#endif /* DEBUG_KERMIT */

        while (done == Q_FALSE) {

#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: LOOP done = %s\n",
                        (done == Q_TRUE ? "true" : "false"));
#endif /* DEBUG_KERMIT */

                if (output_max - *output_n < free_space_needed) {
                        /* No more room, break out */
                        /* This will only occur for sending */
                        assert(status.sending == Q_TRUE);
                        done = Q_TRUE;
                        continue;
                }

                /*
                 * Make sure we can store at least one more packet in output
                 * window.
                 */
                if ((output_window_n == session_parms.WINDO_out) &&
                        (status.sending == Q_TRUE) &&
                        (input_n == 0) &&
                        (had_some_input == Q_FALSE) &&
                        (session_parms.streaming == Q_FALSE)
                ) {
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "KERMIT: output window full \n");
#endif /* DEBUG_KERMIT */

                        /* No more room, break out */
                        done = Q_TRUE;
                        continue;
                }

#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: input_n %d packet_buffer_n %d\n",
                        input_n, packet_buffer_n);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                /* Look for ^C's to interrupt if necessary */
                if (input_n < 10) {
                        for (i = 0; i < input_n; i++) {
                                if (input[i] == 0x03) {
                                        ctrl_c_count++;
                                } else {
                                        ctrl_c_count = 0;
                                }
                        }
                }
                if (ctrl_c_count >= 3) {
                        /* Remote user has aborted */
                        status.state = ABORT;
                        stop_file_transfer(Q_TRANSFER_STATE_ABORT);
                        set_transfer_stats_last_message(_("ABORTED BY REMOTE SIDE"));
                        error_packet("Aborted by remote side");
                }

                if (toss_input_buffer == Q_TRUE) {
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "KERMIT: TOSS INPUT BUFFER\n");
#endif /* DEBUG_KERMIT */
                        input_n = 0;
                }

                /* Add input_n to packet_buffer */
                if (input_n > sizeof(packet_buffer) - packet_buffer_n) {

#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "KERMIT: copy %ld input bytes to packet_buffer\n",
                                sizeof(packet_buffer) - packet_buffer_n);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                        memcpy(packet_buffer + packet_buffer_n,
                                input,
                                sizeof(packet_buffer) - packet_buffer_n);
                        memmove(input,
                                input + sizeof(packet_buffer) - packet_buffer_n,
                                input_n - (sizeof(packet_buffer) - packet_buffer_n));
                        input_n -= (sizeof(packet_buffer) - packet_buffer_n);
                        packet_buffer_n = sizeof(packet_buffer);

                } else {

#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "KERMIT: copy %d input bytes to packet_buffer\n",
                                input_n);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                        memcpy(packet_buffer + packet_buffer_n,
                                input,
                                input_n);

                        packet_buffer_n += input_n;
                        input_n = 0;
                }

#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: packet_buffer_n %d\n", packet_buffer_n);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                /* Decode received bytes into packets */
                had_some_input = decode_input_bytes(packet_buffer,
                        packet_buffer_n, &discard);

#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: packet_buffer_n %d discard %d\n",
                        packet_buffer_n, discard);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                /* Take the bytes off the stream */
                if (discard > 0) {
                        assert(discard <= packet_buffer_n);
                        if (discard == packet_buffer_n) {
                                packet_buffer_n = 0;
                        } else {
                                memmove(packet_buffer, packet_buffer + discard,
                                        packet_buffer_n - discard);
                                packet_buffer_n -= discard;
                        }
                }

                /* See if this is a repeat packet */
                check_for_repeat(output, output_n, output_max);

                /* If the packet is still here, save it */
                save_input_packet();

                /* Sliding windows - move window boundaries */
                move_windows();

                /*
                 * Make sure we can store at least one more packet in output
                 * window.
                 */
                if ((output_window_n == session_parms.WINDO_out) &&
                        (status.sending == Q_TRUE) &&
                        (session_parms.streaming == Q_FALSE)
                ) {
#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "KERMIT: output window full \n");
#endif /* DEBUG_KERMIT */

                        /* No more room, break out */
                        done = Q_TRUE;
                        continue;
                }

                if (status.sending == Q_FALSE) {
                        done = kermit_receive();
                } else {
                        done = kermit_send();
                }

                /* NPAD */
                if ((remote_parms.NPAD > 0) &&
                        (output_packet.parsed_ok == Q_TRUE)
                ) {

#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "KERMIT: output %p output_n %d output_max %d\n",
                                output, *output_n, output_max);
                        fprintf(DEBUG_FILE_HANDLE, "KERMIT: NPAD %d PADC '%c' %02x\n",
                                remote_parms.NPAD,
                                remote_parms.PADC,
                                remote_parms.PADC);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                        memset(output + *output_n, remote_parms.PADC,
                                remote_parms.NPAD);
                        *output_n += remote_parms.NPAD;
                }

#ifdef DEBUG_KERMIT
                fprintf(DEBUG_FILE_HANDLE, "KERMIT: output %p output_n %d output_max %d\n",
                        output, *output_n, output_max);
                fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */


                /* Encode generated packet into bytes */
                output_n_start = *output_n;
                encode_output_packet(output + *output_n,
                        output_n,
                        output_max - *output_n);

                /*
                 * Save the next outbound packet to the output window,
                 * but only if it is NOT a NAK.
                 */
                if ((output_n_start != *output_n) &&
                        (output_packet.type != P_KNAK)
                ) {
                        if (status.sending == Q_TRUE) {
                                assert(output_window_n < session_parms.WINDO_out);
                        }
                        if (output_window[output_window_i].data != NULL) {
                                Xfree(output_window[output_window_i].data,
                                        __FILE__, __LINE__);
                        }
                        output_window[output_window_i].data = (unsigned char *)Xmalloc(*output_n - output_n_start, __FILE__, __LINE__);
                        memcpy(output_window[output_window_i].data,
                                output + output_n_start,
                                *output_n - output_n_start);
                        output_window[output_window_i].data_n = *output_n - output_n_start;
                        output_window[output_window_i].seq = output_packet.seq;
                        output_window[output_window_i].type = output_packet.type;
                        output_window[output_window_i].acked = Q_FALSE;
                        output_window[output_window_i].try_count = 1;

#ifdef DEBUG_KERMIT
                        fprintf(DEBUG_FILE_HANDLE, "KERMIT: saved %d bytes to output_window slot %d (%d total before this)\n",
                                *output_n - output_n_start,
                                output_window_i, output_window_n);
                        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

                        if ((status.sending == Q_TRUE) &&
                                (session_parms.streaming == Q_FALSE)
                        ) {
                                /*
                                 * Rotate the output window.
                                 */
                                output_window_n++;
                                output_window_i++;
                                output_window_i %= session_parms.WINDO_out;
                        } else {
                                /*
                                 * Receiving (or streaming) case: hang onto
                                 * the last one sent packet.
                                 */
                                output_window[output_window_i].acked = Q_TRUE;
                                output_window_n = 1;
                        }
                }

                if ((input_n == 0) && (had_some_input == Q_FALSE)) {
                        /* No more data, definitely finished */
                        done = Q_TRUE;
                }

                if ((input_n > 0) || (had_some_input == Q_TRUE)) {
                        /* More data, keep going */
                        done = Q_FALSE;
                }
        } /* while (done == Q_FALSE) */

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "=== KERMIT: EXIT %d output bytes (hex): ",
                *output_n);
        for (i = 0; i < *output_n; i++) {
                fprintf(DEBUG_FILE_HANDLE, "%02x ", (output[i] & 0xFF));
        }
        fprintf(DEBUG_FILE_HANDLE, "=== \n");
        fprintf(DEBUG_FILE_HANDLE, "KERMIT: %d output bytes (ASCII): ",
                *output_n);
        for (i = 0; i < *output_n; i++) {
                fprintf(DEBUG_FILE_HANDLE, "%c ", (output[i] & 0xFF));
        }
        fprintf(DEBUG_FILE_HANDLE, "\n");

        debug_sliding_windows();

        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        /* Reset the timer if we sent something */
        if (*output_n > 0) {
                reset_timer();
        }

        /* Clear the input packet so it won't be seen again */
        input_packet.parsed_ok = Q_FALSE;
} /* ---------------------------------------------------------------------- */

/*
 * Setup the Kermit protocol for a file transfer
 */
Q_BOOL kermit_start(struct file_info * file_list, const char * pathname,
        const Q_BOOL send) {
        int i;

        /*
         * If I got here, then I know that all the files in file_list exist.
         * forms.c ensures the files are all readable by me.
         */

        /* Verify that file_list is set when send is Q_TRUE */
        if (send == Q_TRUE) {
                assert(file_list != NULL);
        } else {
                assert(file_list == NULL);
        }

        /* Assume we don't start up successfully */
        status.state = ABORT;

        upload_file_list = file_list;
        upload_file_list_i = 0;

#ifdef DEBUG_KERMIT
        if (DEBUG_FILE_HANDLE == NULL) {
                DEBUG_FILE_HANDLE = fopen("debug_kermit.txt", "w");
        }
        fprintf(DEBUG_FILE_HANDLE, "KERMIT: START sending = %s pathname = \'%s\'\n",
                (send == Q_TRUE ? "true" : "false"), pathname);
        fflush(DEBUG_FILE_HANDLE);

        if (upload_file_list != NULL) {
                for (i = 0; upload_file_list[i].name != NULL; i++) {
                        fprintf(DEBUG_FILE_HANDLE, "upload_file_list[%d] = '%s'\n", i, upload_file_list[i].name);
                        fflush(DEBUG_FILE_HANDLE);
                }
        }
#endif /* DEBUG_KERMIT */

        status.sending = send;

        if (send == Q_TRUE) {
                /* Set up for first file */
                if (setup_for_next_file() == Q_FALSE) {
                        return Q_FALSE;
                }
        } else {
                q_transfer_stats.bytes_total = 0;

                /* Save download path */
                download_path = Xstrdup(pathname, __FILE__, __LINE__);
                set_transfer_stats_filename("");
                set_transfer_stats_pathname(pathname);
        }

        /* Setup CRC table */
        makecrc();

        /* Initial state */
        status.state = INIT;
        status.check_type = 1;
        status.sequence_number = 0;
        status.first_R = Q_TRUE;
        status.first_S = Q_TRUE;
        status.first_SB = Q_TRUE;
        status.sent_nak = Q_FALSE;
        status.skip_file = Q_FALSE;
        status.seven_bit_only = Q_FALSE;
        status.do_resend = Q_FALSE;

#ifndef Q_NO_SERIAL
        /* Check for 7bit line */
        if ((q_status.serial_open == Q_TRUE) &&
                (q_serial_port.data_bits != DATA_BITS_8)) {
                status.seven_bit_only = Q_TRUE;
        }
#endif /* Q_NO_SERIAL */

        /* Sliding windows support */
        if (input_window != NULL) {
                if (input_window_n > 0) {
                        i = input_window_begin;
                        do {
                                Xfree(input_window[i].data, __FILE__, __LINE__);
                                input_window[i].data = NULL;

                                /* This must be at the end of the loop. */
                                i++;
                                i %= session_parms.WINDO_in;
                        } while (i != input_window_i);
                        Xfree(input_window, __FILE__, __LINE__);
                        input_window = NULL;
                }
                input_window = NULL;
        }
        if (output_window != NULL) {
                if (output_window_n > 0) {
                        i = output_window_begin;
                        do {
                                i %= session_parms.WINDO_out;
                                Xfree(output_window[i].data, __FILE__, __LINE__);
                                output_window[i].data = NULL;
                        } while (i != output_window_i);
                        Xfree(output_window, __FILE__, __LINE__);
                        output_window = NULL;
                }
                output_window = NULL;
        }
        input_window_begin = 0;
        input_window_i = 0;
        input_window_n = 0;
        output_window_begin = 0;
        output_window_i = 0;
        output_window_n = 0;
        assert(input_window == NULL);
        assert(output_window == NULL);
        input_window = (struct kermit_packet_serial *)Xmalloc(1 * sizeof(struct kermit_packet_serial), __FILE__, __LINE__);
        memset(input_window, 0, 1 * sizeof(struct kermit_packet_serial));
        output_window = (struct kermit_packet_serial *)Xmalloc(1 * sizeof(struct kermit_packet_serial), __FILE__, __LINE__);
        memset(output_window, 0, 1 * sizeof(struct kermit_packet_serial));

        /* Clear the last message */
        set_transfer_stats_last_message("");

        /* Clear the packet buffer */
        packet_buffer_n = 0;

        /* Setup packet buffers */
        if (input_packet.data != NULL) {
                Xfree(input_packet.data, __FILE__, __LINE__);
                input_packet.data = NULL;
                input_packet.data_n = 0;
        }
        if (output_packet.data != NULL) {
                Xfree(output_packet.data, __FILE__, __LINE__);
                output_packet.data = NULL;
                output_packet.data_n = 0;
        }
        memset(&input_packet, 0, sizeof(input_packet));
        memset(&output_packet, 0, sizeof(output_packet));
        input_packet.data_max = KERMIT_BLOCK_SIZE;
        output_packet.data_max = KERMIT_BLOCK_SIZE;
        input_packet.data = (unsigned char *)Xmalloc(input_packet.data_max, __FILE__, __LINE__);
        output_packet.data = (unsigned char *)Xmalloc(output_packet.data_max, __FILE__, __LINE__);

        /* Setup timer */
        reset_timer();
        status.timeout_count = 0;

        /* Initialize the default state */
        set_default_session_parameters(&local_parms);
        set_default_session_parameters(&session_parms);

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "KERMIT: START OK\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */
        return Q_TRUE;
} /* ---------------------------------------------------------------------- */

/*
 * End a Kermit transfer
 */
void kermit_stop(const Q_BOOL save_partial) {
        char notify_message[DIALOG_MESSAGE_SIZE];

#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "KERMIT: STOP\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        if ((save_partial == Q_TRUE) || (status.sending == Q_TRUE)) {
                if (status.file_stream != NULL) {
                        fflush(status.file_stream);
                        fclose(status.file_stream);
                }
        } else {
                if (status.file_stream != NULL) {
                        fclose(status.file_stream);
                        if (unlink(status.file_name) < 0) {
                                snprintf(notify_message, sizeof(notify_message), _("Error deleting file \"%s\": %s"), status.file_name, strerror(errno));
                                notify_form(notify_message, 0);
                        }
                }
        }
        status.file_stream = NULL;
        if (status.file_name != NULL) {
                Xfree(status.file_name, __FILE__, __LINE__);
        }
        status.file_name = NULL;
        if (download_path != NULL) {
                Xfree(download_path, __FILE__, __LINE__);
        }
        download_path = NULL;
} /* ---------------------------------------------------------------------- */

/*
 * Skip this file
 */
void kermit_skip_file() {
#ifdef DEBUG_KERMIT
        fprintf(DEBUG_FILE_HANDLE, "KERMIT: SKIP FILE\n");
        fflush(DEBUG_FILE_HANDLE);
#endif /* DEBUG_KERMIT */

        status.skip_file = Q_TRUE;
} /* ---------------------------------------------------------------------- */
