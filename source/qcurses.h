/*
 * qcurses.h
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

/*
 * This loads the header for the correct curses library (ncursesw or PDCurses
 * widechar).  It is a bit finicky because of conflicts between ncurses.h,
 * PDCurses curses.h, and windows.h.
 */

/* Support wide char ncurses */
#ifdef Q_PDCURSES_WIN32
#  define PDC_WIDE
#  include "curses.h"
/*
 * MOUSE_MOVED has a conflict with windows.h, so redefine it to
 * Q_MOUSE_MOVED.
 */
#  undef MOUSE_MOVED
#  define Q_MOUSE_MOVED         (Mouse_status.changes & PDC_MOUSE_MOVED)
#  include "term.h"
#else
#  ifdef Q_PDCURSES
#    define PDC_WIDE
#    define XCURSES
#    include "curses.h"
#    include "term.h"
#  else
#    if defined(__FreeBSD__) || defined(__APPLE__) || defined(__HAIKU__)
#      include <curses.h>
#    else
#      define _GNU_SOURCE
#      include <ncursesw/curses.h>
#    endif /* __FreeBSD__ __APPLE__ __HAIKU__ */
#  endif /* Q_PDCURSES */
#endif /* Q_PDCURSES_WIN32 */
