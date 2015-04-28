/*
 * common.h
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

#ifndef __COMMON_H__
#define __COMMON_H__

/* Hans-Boehm garbage collector integration ------------------------------- */
#if defined(QODEM_USE_GC) && defined(HAVE_LIBGC)
#include <gc.h>

#ifdef DEBUG
#define Xmalloc(X, Y, Z)                GC_debug_malloc(X, Y, Z)
#define Xcalloc(W, X, Y, Z)             GC_debug_calloc(W, X, Y, Z)
#define Xrealloc(W, X, Y, Z)            GC_debug_realloc(W, X, Y, Z)
#define Xfree(X, Y, Z)                  GC_debug_free(X)
#else
#define Xmalloc(X, Y, Z)                GC_malloc(X)
#define Xcalloc(W, X, Y, Z)             GC_calloc(W, X)
#define Xrealloc(W, X, Y, Z)            GC_realloc(W, X)
#define Xfree(X, Y, Z)                  GC_free(X)
#endif /* DEBUG */
#else
#define Xmalloc(X, Y, Z)                malloc(X)
#define Xcalloc(W, X, Y, Z)             calloc(W, X)
#define Xrealloc(W, X, Y, Z)            realloc(W, X)
#define Xfree(X, Y, Z)                  free(X)
#endif /* QODEM_USE_GC */

/* Includes --------------------------------------------------------------- */

#define _GNU_SOURCE
#include <wchar.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Defines ---------------------------------------------------------------- */
#define RELEASE
/* #undef RELEASE */

#define Q_VERSION               "1.0beta"
#define Q_AUTHOR                "Kevin Lamonte"

#ifdef RELEASE
#define Q_VERSION_BRANCH        "Production"
#else
#define Q_VERSION_BRANCH        "Development"
#endif /* RELEASE */

/* Exit codes */
#define EXIT_ERROR_SETLOCALE            12
#define EXIT_ERROR_SELECT_FAILED        20
#define EXIT_ERROR_COMMANDLINE          30
#define EXIT_HELP                       1
#define EXIT_VERSION                    2
#define EXIT_OK                         0

#define TIME_STRING_LENGTH      64

/* HH:MM:SS */
#define SHORT_TIME_SIZE         9

/* Length of a command line string */
#define COMMAND_LINE_SIZE       1024

/* Maximum length of any filename */
#define FILENAME_SIZE           256

/* Maximum length of a message generated for a dialog box (e.g. file transfer dialog) */
#define DIALOG_MESSAGE_SIZE     128

/* Maximum length of a message generated for the session log */
#define SESSION_LOG_LINE_SIZE   512

/* Maximum line length in the options file */
#define OPTIONS_LINE_SIZE       128

/* Localization / Internationalization */
#include <locale.h>

/*
 * Bug #3528357 - The "proper" solution is to add LIBINTL to LDFLAGS
 * and have configure built the intl directory.  But until we get
 * actual non-English translations it doesn't matter.  We may as well
 * just disable gettext().
 */
#if defined(ENABLE_NLS) && defined(HAVE_GETTEXT)
#include <libintl.h>
#define _(String) gettext(String)
#else
#define _(String) (String)
#endif

/* boolean support */
#define Q_BOOL int
#define Q_FALSE 0
#define Q_TRUE 1

/* Globals ---------------------------------------------------------------- */

/* Functions -------------------------------------------------------------- */

extern char * Xstrdup(const char * ptr, const char * file, const int line);
extern wchar_t * Xwcsdup(const wchar_t * ptr, const char * file, const int line);
extern wchar_t * Xstring_to_wcsdup(const char * ptr, const char * file, const int line);

/* Function to clean out any characters waiting in stdin */
extern void purge_stdin();

/* Returns the home directory where ~/.qodem and ~/qodem are stored */
extern char * get_home_directory();

/* Returns Q_TRUE if the named file already exists */
extern Q_BOOL file_exists(const char * filename);

/* Returns Q_TRUE if path exists and is a directory. */
extern Q_BOOL directory_exists(const char * path);


/* Borland C 5.02 support ------------------------------------------------- */

#ifdef __BORLANDC__
typedef int pid_t;

#define strcasecmp strcmpi
#define strncasecmp strncmpi
#define wmemset _wmemset
#define wmemcpy _wmemcpy

// TODO: implement these
extern int snprintf(char *buffer, const int n, const char *format, ...);
extern int strncasecmp(const char *s1, const char *s2, const int n);
extern char * dirname(const char *s);
extern char * basename(const char *s);
extern wchar_t * wmemmove(wchar_t * dest, const wchar_t * src, size_t n);

#endif /* __BORLANDC__ */

#endif /* __COMMON_H__ */
