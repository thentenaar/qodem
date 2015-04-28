/*
 * options.h
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

#ifndef __OPTIONS_H__
#define __OPTIONS_H__

/* Includes --------------------------------------------------------------- */

#include <wchar.h>

/* Defines ---------------------------------------------------------------- */

/* The option types.  See option.c for descriptions. */
typedef enum Q_OPTIONS {
        Q_OPTION_NULL,
        Q_OPTION_WORKING_DIR,
        Q_OPTION_HOST_DIR,
        Q_OPTION_ISO8859_LANG,
        Q_OPTION_UTF8_LANG,
        Q_OPTION_SOUNDS_ENABLED,
        Q_OPTION_ANSI_MUSIC,
        Q_OPTION_ANSI_ANIMATE,
        Q_OPTION_EDITOR,
        Q_OPTION_X11_TERMINAL,
        Q_OPTION_XTERM_DOUBLE,
        Q_OPTION_SCROLLBACK_LINES,
        Q_OPTION_80_COLUMNS,
        Q_OPTION_START_PHONEBOOK,
        Q_OPTION_EXIT_ON_DISCONNECT,
        Q_OPTION_CONNECT_DOORWAY,
        Q_OPTION_DOORWAY_MIXED_KEYS,
        Q_OPTION_HOST_USERNAME,
        Q_OPTION_HOST_PASSWORD,
        Q_OPTION_SHELL,
        Q_OPTION_SSH_EXTERNAL,
        Q_OPTION_SSH,
        Q_OPTION_SSH_USER,
        Q_OPTION_SSH_KNOWNHOSTS,
        Q_OPTION_RLOGIN_EXTERNAL,
        Q_OPTION_RLOGIN,
        Q_OPTION_RLOGIN_USER,
        Q_OPTION_TELNET_EXTERNAL,
        Q_OPTION_TELNET,
        /* Q_OPTION_TN3270, */
        Q_OPTION_CAPTURE,
        Q_OPTION_CAPTURE_FILE,
        Q_OPTION_CAPTURE_TYPE,
        Q_OPTION_SCREEN_DUMP_TYPE,
        Q_OPTION_SCROLLBACK_SAVE_TYPE,
        Q_OPTION_LOG,
        Q_OPTION_LOG_FILE,
        Q_OPTION_DOWNLOAD_DIR,
        Q_OPTION_UPLOAD_DIR,
        Q_OPTION_BATCH_ENTRY_FILE,
        Q_OPTION_MAIL_READER,
        Q_OPTION_IDLE_TIMEOUT,
        Q_OPTION_KEEPALIVE_TIMEOUT,
        Q_OPTION_KEEPALIVE_BYTES,
        Q_OPTION_SCREENSAVER_TIMEOUT,
        Q_OPTION_SCREENSAVER_PASSWORD,
        Q_OPTION_ENQ_ANSWERBACK,
        Q_OPTION_VT52_COLOR,
        Q_OPTION_VT100_COLOR,
        Q_OPTION_AVATAR_COLOR,
        Q_OPTION_ASCII_UPLOAD_USE_TRANSLATE_TABLE,
        Q_OPTION_ASCII_UPLOAD_CR_POLICY,
        Q_OPTION_ASCII_UPLOAD_LF_POLICY,
        Q_OPTION_ASCII_DOWNLOAD_USE_TRANSLATE_TABLE,
        Q_OPTION_ASCII_DOWNLOAD_CR_POLICY,
        Q_OPTION_ASCII_DOWNLOAD_LF_POLICY,
        Q_OPTION_ZMODEM_AUTOSTART,
        Q_OPTION_ZMODEM_ZCHALLENGE,
        Q_OPTION_ZMODEM_ESCAPE_CTRL,
        Q_OPTION_KERMIT_AUTOSTART,
        Q_OPTION_KERMIT_ROBUST_FILENAME,
        Q_OPTION_KERMIT_STREAMING,
        Q_OPTION_KERMIT_UPLOADS_FORCE_BINARY,
        Q_OPTION_KERMIT_DOWNLOADS_CONVERT_TEXT,
        Q_OPTION_KERMIT_RESEND,
        Q_OPTION_KERMIT_LONG_PACKETS,
        Q_OPTION_DIAL_CONNECT_TIME,
        Q_OPTION_DIAL_BETWEEN_TIME,
        Q_OPTION_MUSIC_CONNECT,
        Q_OPTION_MUSIC_CONNECT_MODEM,
        Q_OPTION_MUSIC_UPLOAD,
        Q_OPTION_MUSIC_DOWNLOAD,
        Q_OPTION_MUSIC_PAGE_SYSOP,

        Q_OPTION_MAX
} Q_OPTION;

/* Globals ---------------------------------------------------------------- */

/* Functions -------------------------------------------------------------- */

extern char * get_option(const Q_OPTION option);
extern void load_options();
extern char * substitute_string(const char * original, const char * pattern, const char * new_string);
extern char * substitute_wcs_half(const char * original, const char * pattern, const wchar_t * new_string);
extern wchar_t * substitute_wcs(const wchar_t * original, const wchar_t * pattern, const wchar_t * new_string);

extern char * get_options_filename();

extern void reset_capture_type();
extern void reset_screen_dump_type();
extern void reset_scrollback_save_type();

extern const char * get_option_key(const Q_OPTION option);
extern const char * get_option_description(const Q_OPTION option);
extern const char * get_option_default(const Q_OPTION option);

#endif /* __OPTIONS_H__ */
