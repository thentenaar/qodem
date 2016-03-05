/*
 * qcurses.h
 *
 * qodem - Qodem Terminal Emulator
 *
 * Written 2003-2016 by Kevin Lamonte
 *
 * To the extent possible under law, the author(s) have dedicated all
 * copyright and related and neighboring rights to this software to the
 * public domain worldwide. This software is distributed without any
 * warranty.
 *
 * You should have received a copy of the CC0 Public Domain Dedication along
 * with this software. If not, see
 * <http://creativecommons.org/publicdomain/zero/1.0/>.
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
