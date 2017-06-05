Qodem TODO List
===============



Regression Checklist
--------------------

  Persistence
    Phonebook
    Keyboard editor
    Modem config
    Translate tables editor
    Edit options
      Reload options after edit
    Edit colors
      Reload colors after edit

  Online features
    Codepage
√   Serial port settings
    Keyboard macros
    Translate tables
      Input
        8-bit
        UTF-8
      Output
        8-bit
        UTF-8
    Split screen
    Scrollback
      View up/down
      Clear
      Save to file
        Normal
        Html
    Screen dump
      Normal
      Html
    List directory
    View file
    View log file
    Send break
√   Launch mail reader
√   Operating system shell
    Capture
      Raw
      Normal
      Html

  UI Conventions
    F10 -> Enter or Alt-Enter
    F2 -> Space

  Toggles
    Full duplex
    Strip 8th
    Beeps and bells
    Session Log
    Xon/Xoff
    Hard backspace
    Linewrap
    Display NUL
    Scrollback
    Status line visible
    Status line info
    Add CR
    ANSI music

  Serial port
    5-bit
    6-bit
    7-bit
    8-bit
    Mark / Space
    RTS/CTS
    XON/XOFF

  Alt Code key
    8-bit
    Unicode

  Uploads
    ASCII
    Xmodem
    Xmodem CRC
    Xmodem 1k
    Xmodem Relaxed
    Xmodem 1k-G
    Ymodem
    Ymodem G
√   Zmodem
    Kermit

  Downloads
    ASCII
    Xmodem
    Xmodem CRC
    Xmodem 1k
    Xmodem Relaxed
    Xmodem 1k-G
    Ymodem
    Ymodem G
√   Zmodem
    Kermit

  Scripts

  Host mode
    socket
    ssh
    telnet
√   serial
√   modem

  Phonebook:
    Find
    Find Again
    Manual Dial
    Delete Tagged
    Sort
    Tag
    Edit Notes
    Edit Script

  Emulations:
    VT52
    VT100
    VT102
    VT220
    LINUX
    XTERM
    L_UTF8
    X_UTF8
    ANSI
      ANSI music
    AVATAR
      ANSI fallback
        ANSI music
    DEBUG
    PETSCII
    ATASCII

  Command-line switches:
    --connect
    --connect-method
    --play
    --play-exit
    --dial
    --username
    --dotqodem-dir
    --config
    --create-config
    --capfile
    --logfile
    --keyfile
    --xl8file
    --xlufile
    --srcfile
    --read-only
    -x, --exit-on-completion
    --doorway
    --codepage
    --emulation
    --status-line
    --geometry
    --xterm
    --version
    -h, --help



Release Checklist
-----------------

Fix all marked TODOs in code

√ Eliminate DEBUG_*, // comments

√ Update ChangeLog

√ #define RELEASE common.h

Update Telnet BBS List --> put into setup

Check dates on web pages:
    index.html screenshots.html getting_started.html

Sync up text in help.c and man pages and README and www/index.html

√ Rebuild man page HTML:
    groff -mandoc `man -w docs/qodem.1` -T html > qodem.1.html

√ Sync qodem.1 and qodem-x11.1

√ Version in:
    FILE_ID.DIZ
    configure.ac
    common.h
    colors.c
    qodem.c
    build/win32/qodem-isetup.iss
    build/win32/resources.rc
    build/osx/build_dmg.bash
    build/osx/Qodem.app/Contents/Info.plist
    build/deb/qodem/changelog
    build/deb/qodem/control
    build/deb/qodem-x11/changelog
    build/deb/qodem-x11/control
    build/rpm/qodem.spec
    build/rpm/qodem-x11.spec

√ Update written by date to current year:
    All code headers
    qodem.c --version
    console.c terminal header

Verify .tar.gz source builds cleanly

Windows:
    sync_windows_build.bash
    Build all:
        UPnP
        PDCurses
        cryptlib
        libc
        qodem
    Test on Windows 2000
    Test on Windows XP
    Test on Windows 7

Binary builds:
    Fedora i386
    Fedora x86_64
    X11 Fedora i386
    X11 Fedora x86_64
    Debian stable i386
    Debian stable x86_64
    X11 Debian stable i386
    X11 Debian stable x86_64
    MacOS Sierra
    Win32 Win2k
    Win64 ???

Tag github

Upload to SF



1.1.0 Wishlist
--------------

Encrypted phonebook

Refactor:
  Move Q_STATE_DIALER from phonebook.c to dialer.c
  Rationalize use of globals vs q_status
  Change connection methods to objects with functions

Full recognition of XTERM sequences

Direct proxy support:
  HTTP
    NTLM
  SOCKS5

Use puttygen'd keys for ssh

Kermit locking shift

Send Wake-On-LAN packet to phonebook entry

Automatic virus scanning / post-processing of downloaded files
  UUDECODE/UUENCODE/yEnc/Base64 decoding in ASCII file transfers:
    yydecode
  Virus scan everything:
    clamav
  Generic post processor:
    Session log event: "Spawning file post-processor..."

VT220 printer support

VT52 HOLD SCREEN mode

External protocols

"Server Cmds" command menu: telnet, kermit server, PPP
  Be able to sever a Kermit server/PPP link
  Send the other telnet commands (IP, GA, ...)



Brainstorm Wishlist
-------------------

Pacing character support

Integrate with I2P

Kerberos

HS/Link protocol

Multi-line customizable status display

Encrypted telnet

True telnet NVT ASCII console (linemode option)
  Send the other telnet commands (IP, GA, ...)

.qwk downloads -> ~/mmail/down
  Detect in zmodem, ymodem, and kermit

IEMSI login and mail transfer

Other emulations:
  Wyse50
  Wyse60
  IBM 3270

Session log in w3c/IIS format (for standardized recording of file transfers)

Script library (see Intellicomm)
  Unix login
  Unix SOUP mail download/upload
  Wildcat BBS login, filelist
  SSH through firewall, transfer files, disconnect
  TradeWars 2002 scripts

True native (not PDCurses) port to X11:
  Draw VTxxx fonts graphically (including VT52 chars not in Unicode)
  Sixel graphics
  ReGIS graphics
  Tektronix

Quartz backend for PDCurses


Qodem support BBS:
  BBS with:
    www, ftp, telnet, ssh
    x/y/zmodem, kermit
    long filenames (?)
  File boards:
    Qodem Releases
    Qodem Scripting
    Qmodem(tm)
    Other Communication Programs
    Offline Mail Readers
    Terminal Emulation
    File Transfer Protocols
    Linux
    DOS
  Message bases:
    Qodem Support
  Link to Fidonet
  Link to DOVE-Net
  Regular BBS ad announcement




Bugs Noted In Other Programs
----------------------------

Bug on telnetd: set LANG

Wishlist on ncurses: expose double-width / double-height

Bug on xterm/ncurses: A_BOLD and ACS_VLINE

Bug on xterm: -fbx/+fbx doesn't seem to do anything

Bug on xterm: A_REVERSE + DECSCNM + A_BOLD
