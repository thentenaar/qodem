/*
 * zmodem.h
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

#ifndef __ZMODEM_H__
#define __ZMODEM_H__

/* Includes --------------------------------------------------------------- */

#include "common.h"             /* Q_BOOL */
#include "forms.h"              /* struct file_info */

/* Defines ---------------------------------------------------------------- */

/**
 * The flavors of Zmodem that are supported.
 */
typedef enum {
    Z_CRC16,                    /* Zmodem 16-bit CRC */
    Z_CRC32                     /* Zmodem 32-bit CRC */
} ZMODEM_FLAVOR;

/* Globals ---------------------------------------------------------------- */

/* Functions -------------------------------------------------------------- */

/**
 * Process raw bytes from the remote side through the transfer protocol.  See
 * also protocol_process_data().
 *
 * @param input the bytes from the remote side
 * @param input_n the number of bytes in input_n
 * @param output a buffer to contain the bytes to send to the remote side
 * @param output_n the number of bytes that this function wrote to output
 * @param output_max the maximum number of bytes this function may write to
 * output
 */
extern void zmodem(unsigned char * input, const int input_n,
                   unsigned char * output, int * output_n,
                   const int output_max);

/**
 * Setup for a new file transfer session.
 *
 * @param file_list list of files to upload, or NULL if this will be a
 * download.
 * @param pathname the path to save downloaded files to
 * @param send if true, this is an upload: file_list must be valid and
 * pathname is ignored.  If false, this is a download: file_list must be NULL
 * and pathname will be used.
 * @param in_flavor the type of Zmodem transfer to perform
 */
extern Q_BOOL zmodem_start(struct file_info * file_list, const char * pathname,
                           const Q_BOOL send, const ZMODEM_FLAVOR in_flavor);

/**
 * Stop the file transfer.  Note that this function is only called in
 * stop_file_transfer() and save_partial is always true.  However it is left
 * in for API completeness.
 *
 * @param save_partial if true, save any partially-downloaded files.
 */
extern void zmodem_stop(const Q_BOOL save_partial);

#endif /* __ZMODEM_H__ */
