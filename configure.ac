AC_INIT([qodem], [1.0.1])
AC_CONFIG_SRCDIR([source/qodem.h])
AM_CONFIG_HEADER([source/config.h])
AM_INIT_AUTOMAKE([foreign subdir-objects])

dnl Checks for programs
AC_PROG_CC
AC_PROG_INSTALL

dnl Set Q_NO_NEWTERM by default so that Arch Linux works out of the box.
CFLAGS="$CFLAGS -DQ_NO_NEWTERM"

dnl Checks for header files.
AC_CHECK_HEADERS(sys/select.h)
AC_CHECK_HEADERS(pty.h)
AC_CHECK_HEADERS(libutil.h)
AC_CHECK_HEADERS(util.h)
AC_CHECK_HEADERS(getopt.h)
AC_CHECK_HEADERS(ncursesw/curses.h)
AC_CHECK_HEADERS(miniupnpc/miniupnpc.h)

dnl Checks for libraries
AC_SEARCH_LIBS(forkpty, util)
AC_CHECK_FUNCS(forkpty)
AC_CHECK_LIB([ncursesw], [mvwadd_wch], Q_HAS_NCURSES="yes", Q_HAS_NCURSES="no")
AC_CHECK_LIB([ncurses], [mvwadd_wch], Q_HAS_NCURSES_APPLE="yes", Q_HAS_NCURSES_APPLE="no")
AC_CHECK_LIB([miniupnpc], [upnpDiscover], Q_USE_SYSTEM_UPNP="yes", Q_USE_SYSTEM_UPNP="no")
AC_CHECK_LIB([gpm], [Gpm_Open], Q_HAS_LIBGPM="yes", Q_HAS_LIBGPM="no")

dnl SDL is an argument
disable_sdl=no
AC_ARG_ENABLE(sdl, [  --disable-sdl           Don't use libSDL for beeps/music])
AC_CHECK_LIB([SDL], [SDL_Init], AS_VAR_SET([Q_HAVE_LIBSDL], [yes]))
if test "x$enable_sdl" = "xyes" ; then
    disable_sdl=no
else
    if test "x$enable_sdl" = "x" ; then
        disable_sdl=no
    else
        disable_sdl=yes
    fi
fi
if test "x$disable_sdl" = "xyes"; then
    AC_MSG_NOTICE([ *** Disable SDL *** ])
else
    if test "x$Q_HAVE_LIBSDL" = "x"; then
        AC_MSG_NOTICE([ *** SDL unavailable *** ])
    else
        AC_MSG_NOTICE([ *** Enable SDL *** ])
        CFLAGS="$CFLAGS -DQ_SOUND_SDL"
        LIBS="-lSDL -lm $LIBS"
    fi
fi

dnl serial is an argument
disable_serial=no
AC_ARG_ENABLE(serial, [  --disable-serial        Disable serial port/modem])
if test "x$enable_serial" = "xyes" ; then
    disable_serial=no
else
    if test "x$enable_serial" = "x" ; then
        disable_serial=no
    else
        disable_serial=yes
    fi
fi
if test "x$disable_serial" = "xyes"; then
    AC_MSG_NOTICE([ *** Disable serial port / modem support *** ])
    CFLAGS="$CFLAGS -DQ_NO_SERIAL"
fi

dnl X11 build option
AC_ARG_ENABLE(x11, [  --enable-x11            Enable PDCurses-based X11 build])
if test "x$enable_x11" = "xyes"; then
    subdirs="lib/pdcurses $subdirs"
    LIBS="$LIBS lib/pdcurses/libXCurses.a -lXaw -lXmu -lXt -lX11 -lXpm"
    CFLAGS="$CFLAGS -Ilib/pdcurses/include -DQ_PDCURSES"
fi
AC_SUBST(subdirs)
if test "x$enable_x11" = "xyes"; then
    AC_MSG_NOTICE([ *** Using PDCurses X11 build instead of ncurses *** ])
    AC_SUBST(QODEMX11, "yes")
else
    if test "x$Q_HAS_NCURSES" = "xyes"; then
        AC_MSG_NOTICE([ *** ncurses found *** ])
        LIBS="-lncursesw $LIBS"
    else
        if test "x$Q_HAS_NCURSES_APPLE" = "xyes"; then
            AC_MSG_NOTICE([ *** ncurses found *** ])
            LIBS="-lncurses $LIBS"
        else
            AC_MSG_ERROR([ *** Error: the ncurses development library appears to be unavailable.  Please install the wide-char ncurses development library.  This is often called libncursesw5-dev, libncurses5w-dev, or ncurses-devel. ])
        fi
    fi
fi
AM_CONDITIONAL(Q_X11_BUILD, [test "x$enable_x11" = "xyes"])

dnl cryptlib support option - default is NO
disable_ssh=yes
AC_ARG_ENABLE(ssh, [  --enable-ssh            Enable internal ssh (via cryptlib)])
if test "x$enable_ssh" = "xyes" ; then
    AC_MSG_NOTICE([ --- Cryptlib requested --- ])
    disable_ssh=no
else
    AC_MSG_NOTICE([ --- Cryptlib default: disable --- ])
    disable_ssh=yes
fi
if test "x$disable_ssh" = "xyes"; then
    AC_MSG_NOTICE([ *** Disable cryptlib *** ])
else
    AC_MSG_NOTICE([ *** Enable cryptlib *** ])
    subdirs="lib/cryptlib $subdirs"
    LIBS="$LIBS lib/cryptlib/libcl.a -lpthread"
    CFLAGS="$CFLAGS -DQ_SSH_CRYPTLIB -Ilib/cryptlib"
fi
AC_SUBST(subdirs)

dnl UPnP support option - default is YES
disable_upnp=no
if test "x$enable_upnp" = "xyes" ; then
    disable_upnp=no
else
    if test "x$enable_upnp" = "x" ; then
        disable_upnp=no
    else
        disable_upnp=yes
    fi
fi
AC_ARG_ENABLE(upnp, [  --disable-upnp          Disable UPnP (via libminiupnpc)])
if test "x$disable_upnp" = "xyes"; then
    AC_MSG_NOTICE([ *** Disable UPnP *** ])
else
    AC_MSG_NOTICE([ *** Enable UPnP *** ])
    if test "x$Q_USE_SYSTEM_UPNP" = "xyes"; then
        AC_MSG_NOTICE([ *** Using system-provided UPnP library *** ])
        LIBS="$LIBS -lminiupnpc"
        CFLAGS="$CFLAGS -DQ_UPNP -DQ_USE_SYSTEM_UPNP"
    else
        subdirs="lib/upnp $subdirs"
        AC_MSG_NOTICE([ *** Using local UPnP library *** ])
        LIBS="$LIBS lib/upnp/libminiupnpc.a"
        CFLAGS="$CFLAGS -DQ_UPNP -Ilib/upnp"
    fi
fi

dnl Look for libgpm, and if present enable GPM support
disable_keymacros=no
AC_ARG_ENABLE(gpm, [  --disable-gpm           Disable Linux console GPM support])
if test "x$enable_gpm" = "xyes" ; then
    disable_gpm=no
else
    if test "x$enable_gpm" = "x" ; then
        disable_gpm=no
    else
        disable_gpm=yes
    fi
fi
if test "x$enable_x11" = "xyes" ; then
    AC_MSG_NOTICE([ *** Disable Linux console GPM support due to X11 build *** ])
    disable_gpm=yes
fi
if test "x$disable_gpm" = "xyes"; then
    AC_MSG_NOTICE([ *** Disable Linux console GPM support *** ])
else
    if test "x$Q_HAS_LIBGPM" = "xyes"; then
        AC_MSG_NOTICE([ *** libgpm found *** ])
        LIBS="$LIBS -lgpm"
        CFLAGS="$CFLAGS -DQ_ENABLE_GPM"
    else
        AC_MSG_ERROR([ *** libgpm NOT found, no Linux console GPM support *** ])
    fi
fi

AC_SUBST(subdirs)
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
