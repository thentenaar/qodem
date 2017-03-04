Qodem Terminal Emulator
=======================


WHAT IS QODEM?
--------------

Qodem is a from-scratch clone implementation of the Qmodem
communications program made popular in the days when Bulletin Board
Systems ruled the night.  Qodem emulates the dialing directory and the
terminal screen features of Qmodem over both modem and Internet
connections.

The Qodem homepage, which includes an extensive archive of Qmodem(tm)
releases, a getting started guide, and Qodem binary downloads, is at:
http://qodem.sourceforge.net .  The Qodem source code is hosted at:
https://github.com/klamonte/qodem .

As for Qmodem(tm): Qmodem(tm) was originally written by John Friel III
in 1984, eventually being acquired by Mustang Software, Inc. (MSI) in
1991 and distributed until 2000.  Qmodem and QmodemPro were acquired
by Quintus when it purchased MSI, and their current copyright status
is abandonware.  Quintus went bankrupt shortly after the MSI
acquisition and members of both companies' Boards of Directors were
involved in a class-action lawsuit from Quintus shareholders.  The
lawsuit (C-00-4263 VRW in the Northern District of California, US
District Chief Judge Vaughn R Walker presiding) was settled on
December 5, 2006 for $10.1 million (with 11% to plaintiffs legal
fees).  (For those wanting more, Greg Hewgill collected many articles
and other media related to Mustang, including its acquisition of
Qmodem, in a PDF: http://hewgill.com/mustang/MustangHistory.pdf .  I
have a copy also at http://qodem.sourceforge.net/qmodem/MustangHistory.pdf .)



OBLIGATORY SCREENSHOTS
----------------------

![Zmodem upload showing single file and batch progress bars](/misc/screenshots/zmodem_upload1.gif?raw=true "Zmodem upload showing single file and batch progress bars")

![TradeWars 2002](/misc/screenshots/tradewars1.gif?raw=true "TradeWars 2002")



COPYRIGHT STATUS
----------------

To the extent possible under law, the author(s) of Qodem have
dedicated all copyright and related and neighboring rights to Qodem to
the public domain worldwide. This software is distributed without any
warranty.  The COPYING file describes this intent, and provides a
public license fallback for those jurisdictions that do not recognize
the public domain.

Qodem incorporates or links to software that is copyrighted and
licensed under BSD-like or GPL-like terms.  The CREDITS file describes
those pieces and their respective licenses.  The only Qodem source
file with such code is codepage.c, containing a UTF-8 decoding
function; all other such code is in the lib directory.

The effective license for the combined Qodem executable differs
depending on whether or not it was linked to cryptlib:

  * WITHOUT CRYPTLIB, the combined license terms are effectively the
    same as the BSD or MIT license: attribution in the source, source
    is not required to be shared, and there is no warranty.

  * WITH CRYPTLIB, the combined license terms are effectively the same
    as the GPLv3 license: attribution in the source, source for both
    cryptlib and Qodem IS required to be shared (even if somehow
    modified into a SaaS style architecture), and there is no
    warranty.

Qodem also ships an unmodified copy of the C64 TrueType fonts as
developed by Style available at http://style64.org/c64-truetype/ .
These fonts are permitted to be redistributed as part of a software
package "if said software package is freely provided to end users".
Entities wishing to ship packages that are not "freely provided to end
users" will need to either remove this font, or negotiate a separate
license agreement by contacting Style at
http://style64.org/contact-style .



INTENDED AUDIENCE
-----------------

Qodem is designed to help the following kinds of people:

* Console users who want a more capable terminal without having to
  resort to an X-based emulator.  Qodem provides a much longer
  scrollback, screen dump, capture, keyboard macros, very good VT100
  emulation, and many more functions.

* Windows and X11 desktop users who want a full-featured terminal with
  a keyboard-driven interface.  The X11 and Windows versions of Qodem
  combine the typical conveniences of modern terminals including
  scrollback, select-and-paste, Unicode support, and a resizable
  window, with more rare BBS-era features like Zmodem, Kermit, and
  keyboard macros.

* Users of telnet/ssh BBSes who would like to use BBS-era features
  such as: a phone book, Avatar and ANSI emulations (including ANSI
  music), file transfers, session logging, and many more functions.

* Those who use serial ports and modems to communicate with embedded
  devices, headless servers, and remote systems.  Qodem supports the
  serial port, has a modem dialer, and also a special "DEBUG"
  emulation mode that prints incoming and outgoing bytes in a
  programmer's hex dump format which can be quite useful in debugging
  communication with specialized devices.

* System administrators who need to manage a number of machines.
  Qodem can keep track of those machines in its phone book, including
  usernames and passwords, and it can be used to transfer files across
  a connection that spans multiple firewalls using Zmodem or Kermit.

* Fans of the original DOS-based Qmodem who knew and loved its unique
  approach to text-based communication.



BUILDING QODEM
--------------

Qodem can be built in several ways:

* An autoconf build is available: use `configure ; make` .

* A very simple barebones build is also provided via `make -f
  build/Makefile.generic`.

* Windows systems can use pre-made project files for Visual C++ 6 (in
  the vc6qodem, vc6libc, etc. directories) or Borland C++ 5 (files
  ending in .ide) to create a binary.  There is also a command-line
  make file for Borland C++ 5.

* Debian packages for `qodem` and `qodem-x11` are available in the
  build/deb directory.

* RPM spec files for `qodem` and `qodem-x11` are available in the
  build/rpm directory.

The INSTALL file has some additional details.



HOW TO USE QODEM
----------------

Qodem is driven by the keyboard.  It will listen for mouse events, but
only to send those to remote systems using the XTERM or X_UTF8
emulations.

Qodem has two main screens: the phone book and TERMINAL mode.

* The phone book contains a list of sites to access, with each site
  being customizable for username, password, emulation, toggles, and
  more.  This is the jumping off point for most connections, so by
  default Qodem starts in phone book mode.

* In TERMINAL mode keystrokes are passed directly to the "other side".

Qodem is exited by pressing Alt-X while in TERMINAL mode.  (If there
is no active connection, Ctrl-C will also bring up the exit dialog.)
Pressing 'y' or Enter at the exit prompt will exit Qodem.

Nearly all of the time pressing the ESCAPE key or the backtick (`)
will exit a dialog and the phone book screen.

Throughout Qodem, the bottom-most line of the screen is used to report
status and provide hints about what keystrokes are available.  In
TERMINAL mode the status line can be turned off with Alt--
(Alt-minus), or toggled between two different forms with Alt-7.



DIFFERENCES BETWEEN THE TEXT (NCURSES), X11, AND WINDOWS VERSIONS
-----------------------------------------------------------------

Version 1.0.0 introduces two new builds based on the PDCurses library:
an X11 version and a Windows version.  These versions in general work
the same as the text-based ncurses version, but have a few differences
due to the environments.  This section describes those differences.

----

X11 VERSION

The X11 version can be built by passing --enable-x11 to configure.
Due to how the curses libraries are linked, a single Qodem binary
cannot currently support both text ncurses and X11 PDCurses
interfaces, therefore the X11 binary is 'qodem-x11', and its man page
is accessed by 'man qodem-x11'.

The Fedora and Debian package is 'qodem-x11'.  It can be installed
entirely independently of the normal 'qodem' package.

When spawning other processes such as editors (Alt-L, Alt-N, Alt-V,
and editing files in the phone book), the mail reader (Alt-M), or
shelling to the OS (Alt-R), Qodem spawns them inside a separate
X11-based terminal window, and displays the message "Waiting On X11
Terminal To Exit..." until the other terminal closes.  The default
terminal program is 'x-terminal-emulator'; this can be changed in
qodemrc.

Mouse motion events do not work due to limitation in the PDCurses
mouse API.  Mouse clicks however do work.

WINDOWS VERSION

The Windows version can be built using either Borland C++ 5 or later,
or Microsoft Visual C++ 6 or later.

When spawning other processes such as editors (Alt-L, Alt-N, Alt-V,
and editing files in the phone book), the mail reader (Alt-M), or
shelling to the OS (Alt-R), Qodem waits for the program to exit.

Quicklearn scripts are written in Perl.  Strawberry Perl for Windows
is available at http://strawberryperl.com .

The Windows build uses Beep() rather than SDL for sounds.  This might
not work on Windows Vista and 64-bit XP systems.

SSH connections (client or host) using cryptlib do not work when
compiled with the Borland compiler.

Mouse motion events do not work due to limitation in the PDCurses
mouse API.  Mouse clicks however do work.



KNOWN ISSUES / DECISIONS
------------------------

In the development of Qodem some arbitrary design decisions had to be
made when either the obviously expected behavior did not happen or
when a specification was ambiguous.  This section describes such
issues.

----

The ncurses version of Qodem requires a Unicode-capable Linux console
or X emulator to look right.  For the Linux console, the default
settings for most Linux distributions should work well.  Under X11,
xterm, rxvt-unicode, and Konsole work well.

Most BBS programs assume a display with 80x24 dimensions.  Qodem by
default sets the right margin to column 80 for ANSI, Avatar, and TTY
emulations.  Changing "80_columns = true" to "80_columns = false" in
qodemrc will cause Qodem to use the real right margin.

The backspace key is always mapped to DEL (0x7F) in VT220 emulation to
match the keyboard of a real VT220.  You can send a true backspace
(0x08, ^H) by pressing Alt-\ 0 0 8 to use the Alt Code Key feature to
send backspace.

Function keys beyond F4 in VT100/VT102 emulation may not work as
expected.  Qodem uses a common convention that F5 is "{ESC} O t", F6
is "{ESC} O u", etc.  Some programs understand this convention.  Those
that don't will usually understand "{ESC} {number}", where {number} is
a number from 5 to 0, to mean F5 through F10.  You can get this effect
in Qodem by typing ESC {number}, or by switching to Doorway Mode and
typing Alt-{number} (or Meta-{number}).

In VT100, VT102, and LINUX emulations, some programs (like minicom and
Midnight Commander) send the DECCOLM sequence ({ESC} [ ? 3 l ) when
exiting, putting the emulation into 80-column mode.  Resetting the
emulation via Alt-G {pick emulation} {enter 'y'} will restore the
default right margin.

ASCII uploads may hang if the remote end can't keep up.  For instance,
using 'vi' to create a large file and ASCII uploading the contents may
hang after a few kilobytes.  'cat > filename' usually works fine.

ASCII downloads will process the TAB character (0x09) as a control
character, causing it to expand to the appropriate number of spaces.

Malformed escape sequences might "freeze" LINUX or VTxxx emulation.
(For example, receiving a 0x90 character causes VT102 to look for a
DCS sequence.  If the DCS sequence is not properly terminated the
emulation won't recover.)  Resetting the current emulation will
restore the console function.

KEY_SUSPEND is usually mapped to Ctrl-Z and used to suspend the local
program ('qodem').  If Qodem sees KEY_SUSPEND it will assume the user
typed Ctrl-Z and meant to pass that to the remote side.

On Ymodem downloads, if the file exists it will be appended to.

Xmodem and Ymodem downloads from hosts that use the rzsz package might
need to have stderr redirected to work correctly, for example 'sb
filename 2>/dev/null' .

Kermit receive mode by default handles file collisions by saving to a
new file (SET FILE COLLISION RENAME / WARN file access Attribute).  It
supports the APPEND file access Attribute but disregards the SUPERSEDE
file access Attribute.

When sending files via Zmodem to HyperTerminal, if the HyperTerminal
user clicks "Skip file" then the transfer will stall.  This appears to
be due to two separate bugs in HyperTerminal: 1) When the user clicks
"Skip File", HyperTerminal sends a ZRPOS with position==file size,
Qodem responds by terminating the data subpacket with ZCRCW, which
HyperTerminal responds to with ZACK, however the ZACK contains an
invalid file position.  2) Qodem ignores bug #1 and sends ZEOF, to
which HyperTerminal is supposed to respond with ZRINIT, however
HyperTerminal hangs presumably because it is expecting the ZEOF to
contain a particular file position, however the position it desires is
neither the true file size nor the value it returned in the ZACK.

Mark and space serial port parity are only supported for 7 data bits.
This is due to a limitation of the POSIX termios API.  Workarounds for
the other bit settings (5, 6, 8) are possible if there is user demand.

GNU Emacs may look wrong in ANSI emulation when Line Wrap is disabled.

Internal telnet and rlogin connections usually do not successfully
pass the LANG environment variable to the remote host.  (Qodem sends
the LANG variable and value, but most remote daemons do not listen for
it.)

The SSH server key fingerprint displayed in the Alt-I info screen is
unique, but does not match the key fingerprints reported by ssh-keygen
or the OpenSSH client.

The ssh library used by the host mode SSH server (cryptlib) has a
known issue accepting connections from ssh clients that request DH
keys larger than 4096 bits.  See
http://article.gmane.org/gmane.comp.encryption.cryptlib/2793 for some
discussion regarding this.  ssh clients to a Qodem host can pass the
'-m hmac-md5' command line argument to work around this.

The host mode SSH server does not care what username or password are
passed through the ssh client.  After the ssh connection is
established, the login sequence is identical to socket and telnet
connections.

Qodem manages its own known_hosts file for SSH connections.  This file
is stored in the ~/.qodem directory (or Documents\qodem\prefs on
Windows).

When using 'raw' mode for the capture file, host mode includes its
outgoing bytes in the capture file.



SCRIPT SUPPORT
--------------

Qodem has an entirely different method for supporting scripts than
Qmodem.  This section describes the Qodem scripting support.

----

Qodem does not have its own scripting language.  Instead, any program
that reads and writes to the standard input and output can be run as a
Qodem script:

* Characters sent from the remote connection are visible to the
  script in its standard input.

* Characters the script emits to its standard output are passed on
  the remote connection.

* Messages to the standard error are reported to the user and also
  recorded in the session log.

Since scripts are communicating with the remote system and not Qodem
itself, they are unable to script Qodem's behavior, e.g. change the
terminal emulation, hangup and dial another phone book entry, download
a file, etc.  However, they can be written in any language, and they
can be tested outside Qodem.

Scripts replace the user, and as such have similar constraints:

* Script standard input, output, and error must all be in UTF-8
  encoding.

* Scripts should send carriage return (0x0D, or \r) instead of new
  line (0x0A, or \n) to the remote side - the same as if a user
  pressed the Enter key.  They should expect to see either bare
  carriage return (0x0D, or \r) or carriage return followed by
  newline (0x0D 0x0A, or \r\n) from the remote side.

* Input and output translate byte translation (the Alt-A Translate
  Tables) are honored for scripts.

* While a script is running:
  - Zmodem and Kermit autostart are disabled.
  - Keyboard function key macros are disabled.
  - Qodem functions accessed through the Alt-character combinations
    and PgUp/PgDn are unavailable.
  - Pressing Alt-P will pause the script.

* While a script is paused:
  - The script will receive nothing on its standard input.
  - Anything in the script's standard output will be held until the
    script is resumed.
  - The script process will not be signaled; it may continue running
    in its own process.
  - The only Alt-character function recognized is Alt-P to resume the
    script.  All other Alt- keys will be ignored.
  - Keys pressed will be sent directly to the remote system.
  - Keyboard function key macros will work.

Scripts are launched in two ways:

* In TERMINAL mode, press Alt-F and enter the script filename.  The
  script will start immediately.

* In the phone book, add a script filename to a phone book entry.  The
  script will start once that entry is connected.

Script command-line arguments can be passed directly in both the Alt-F
script dialog and the phone book linked script field.  For example,
pressing Alt-F and entering "my_script.pl arg1" will launch
my_script.pl with its first command-line argument ($ARGV[0] in Perl)
set to "arg1".



TERMINAL EMULATION LIMITATIONS
------------------------------

This section describes known missing features in a Qodem emulation.

----

The following features are not supported for VT10x: smooth scrolling,
printing, keyboard locking, and tests.

132-column mode in VT100 is supported only within consoles/emulators
that have 132 (or more) columns available.  For instance, 132-column
VT100 output on a 128-column Linux console screen will result in
incorrect behavior.

VT52, VT10x, VT220, LINUX, and XTERM numeric/application keypad modes
do not work well in the text version.  This is due to Qodem's host
console translating the numeric keypad keys on its own before sending
the keystroke to the (n)curses library.  For example, the Linux
console will transmit the code corresponding to KEY_END when the
number pad "1 key" is pressed if NUMLOCK is off; if NUMLOCK is on the
console will transmit a "1" when the "1 key" is pressed.  Qodem thus
never actually sees the curses KEY_C1 code that would instruct Qodem
to transmit the appropriate string to the host system.  The only key
that appears to work right on most consoles is the number pad "5 key"
(KEY_B2).  The X11 version of Qodem supports the number pad correctly.

VT52 HOLD SCREEN mode is not supported in any emulation (VT52, VT10x,
LINUX, XTERM).

In VT52 graphics mode, the 3/, 5/, and 7/ characters (fraction
numerators) are not rendered correctly.

In addition to the VT100/VT102 limitations, the following features are
not supported for VT220: user-defined keys (DECUDK), downloadable
fonts (DECDLD), VT100/ANSI compatibility mode (DECSCL).  (Also,
because the VT220 emulation does not support DEC VT100/ANSI mode, it
will fail the last part of the vttest "Test of VT52 mode".)  The
unsupported commands are parsed to keep a clean display, but not used
otherwise.

VT220 discards all data meant for the 'printer' (CSI Pc ? i).

The ANSI.SYS screen mode switch sequence (ESC [ = Pn {h | l}) only
supports enable/disable line wrap (Pn = 7); the various screen mode
settings (e.g 40x25 mono, 640x480 16-color, etc.) are not supported.

XTERM (and X_UTF8) recognizes only a few more features than LINUX and
VT220.  It does not support most of the advanced features unique to
XTerm such as Tektronix 4014 mode, alternate screen buffer, and many
more.  It is intended to support XTerm applications that only use the
sequences in the 'xterm' terminfo entry.

PETSCII colors do not exactly match true Commodore colors.  Also,
uppercase/lowercase switches new incoming characters but does not
change the existing characters on the screen.



DEVIATIONS FROM QMODEM
----------------------

Qodem strives to be as faithful as possible to Qmodem, however
sometimes it must deviate due to modern system constraints or in order
to go beyond Qmodem with entirely new features.  This section
describes those changes.

----

The default emulation for raw serial and command line connections is
VT102 rather than ANSI.

Qodem will listen for mouse events and send them to the remote side
using the same wire protocol as xterm's X10, UTF8, or SGR encoding.
It supports the X10-, normal-, button-, and any-event-tracking modes.
This is only available for XTERM and X_UTF8 emulations.  Note that
ncurses will not report any-event mouse events to Qodem unles TERM is
"xterm-1003"; similarly button-event tracking requires TERM to be
"xterm-1002".

The IBM PC ALT and CTRL + {function key} combinations do not work
through the curses terminal library.  CTRL-Home, CTRL-End, CTRL-PgUp,
CTRL-PgDn, Shift-Tab, and ALT-Up have been given new key combinations.

The F2, F4 and F10 function keys are often co-opted by modern desktop
environments and unavailable for Qodem.  F2 and F10 are still
supported, but also have additional keys depending on function.  Most
of the time space bar can be used for F2 and the Enter key for F10.
The status bar will show the alternate keystrokes.  F4 is currently
only used to clear the Batch Entry Window and show/hide dotfiles in
the View Directory window, no alternative keystroke is provided.

The ESCAPE key can have a long delay (up to 1 second) under some
installations of curses.  It is still supported, but the backtick (`)
can also be used for faster response time.  See ESCDELAY in the curses
documentation.

The program settings are stored in a text file usually called
$HOME/.qodem/qodemrc.  They are hand-edited by the user rather than
another executable ala QINSTALL.EXE.  The Alt-N Configuration command
loads the file into an editor for convenience.

The batch entry window is a simple form permitting up to twenty
entries, each with a long filename.  Next to each entry is the file
size.  The Qmodem screen was limited to three directories each
containing up to twenty 8.3 DOS filenames, and did not report file
sizes.  The F3 "Last Found" function is not supported since many
systems use long filenames.

The upload window for Ymodem, Zmodem, and Kermit contains a second
progress indicator for the batch percentage complete.

Alt-X Exit has only two options yes and no.  Qmodem offers a third
(exit with DTR up) that cannot be implemented using Linux-ish termios.

External protocols are not yet supported.

Some functions are different in TERMINAL mode:

```
    Key        Qodem function         Qmodem function
    ----------------------------------------------------------
    Alt-K      Send BREAK             Change COM Port
    Alt-L      Log View               Change drive
    Alt-O      Modem Config           Change directory
    Alt-P      Capture File           COM Parameters
    Alt-Y      COM Parameters         Auto Answer
    Alt-Z      Terminal Mode Menu     -
    Alt-2      Backspace/Del Mode     80x25 (EGA/VGA)
    Alt-3      Line Wrap              Debug Status Info
    Alt-4      Display NULL           80x43/50 (EGA/VGA)
    Alt-9      Serial Port            Printer Echo
    Alt-+      CR/CRLF Mode           -
    Alt-,      ANSI Music             -
    Alt-\      Alt Code Key           -
    Alt-:      Colors                 -
    Alt-/      Scroll Back            -
    Alt-;      Codepage               -
    Home       -                      Terminal Mode Menu
    Alt-Up     -                      Scroll Back
    Ctrl-End   -                      Send BREAK
    Ctrl-Home  -                      Capture File
    Shift-Tab  -                      CR/CRLF Mode
```

The phone book stores an arbitrary number of entries, not the
hard-coded 200 of Qmodem.

The directory view popup window allows up to 20 characters for
filename, and the Unix file permissions are displayed in the rightmost
column.

The directory browse window behaves differently.  Scrolling occurs a
full page at a time and the first selected entry is the first entry
rather than the first file.  Also, F4 can be used to toggle between
showing and hiding "hidden files" (dotfiles) - by default dotfiles are
hidden.

The phone book displays the fully-qualified filename rather than the
base filename.

VT100 escape sequences may change terminal settings, such as line
wrap, local echo, and duplex.  The original settings are not restored
after leaving VT100 emulation.

DEBUG_ASCII and DEBUG_HEX emulations are not supported.  Qodem instead
offers a DEBUG emulation that resembles the output of a programmer's
hex editor: a byte offset, hexadecimal codes, and a region of
printable characters.

TTY emulation is actually a real emulation.  The following control
characters are recognized: ENQ, BEL, BS, HT, LF, VT, FF, CR.  Also,
underlines that would overwrite characters in a typical character cell
display will actually underline the characters.  For example, A^H_
('A' backspace underline) will draw an underlined 'A' on a console
that can render underlined characters.

ANSI emulation supports more codes than ANSI.SYS.  Specifically, it
responds to DSR 6 (Cursor Position) which many BBSes used to
"autodetect" ANSI, and it also supports the following ANSI X3.64
functions: ICH, DCH, IL, DL, VPA, CHA, CHT, and REP.  It detects and
discards the RIPScript auto-detection code (CSI !) to maintain a
cleaner display.

The "Tag Multiple" command in the phone book does not support the
"P{prefix}{number}{suffix}" form of tagging.  Number prefixes and
suffixes in general are not supported.  Also, text searching in both
"Tag Multiple" and "Find Text/Find Again" is case-insensitive.

The "Set Emulation" function has the ability to reset the current
emulation.  For example, if Qodem is in ANSI emulation, and you try to
change to ANSI emulation, a prompt will appear asking if you want to
reset the current emulation.  If you respond with 'Y' or 'y', the
emulation will be reset, otherwise nothing will change.  This is
particularly useful to recover from a flash.c-style of attack.

"WideView" mode in the function key editor is not supported.

"Status Line Info" changes the status line to show the online/offline
state, the name of the remote system (in the phone book), and the
current time.  Qmodem showed the name of the system, the phone number,
and the connect time.

The scripting language is entirely different.  Qodem has no plans to
support Qmodem or QmodemPro scripts.

Qmodem had several options to control Zmodem behavior: overwrite
files, crash recovery, etc.  Qodem does not expose these to the user;
Qodem's Zmodem implementation will always use crash recovery or rename
files to prevent overwrite when appropriate.

Qodem always prompts for a filename for capture, screen dump, saved
scrollback, and session log.  (Qmodem only prompts if the files do not
already exist.)  Exception: if session log is specified on a phone
book entry toggle, Qodem will not prompt for the filename but use the
default session log filename specified in qodemrc.

Qodem supports two kinds of DOORWAY mode: "Doorway FULL" and "Doorway
MIXED".  "Doorway FULL" matches the behavior of Qmodem's DOORWAY mode.
"Doorway MIXED" behaves like DOORWAY EXCEPT for a list of commands to
honor.  These commands are stored in the qodemrc
'doorway_mixed_mode_commands' option.  "Doorway MIXED" allows one to
use PgUp/PgDn and Alt-X (M-X) in Emacs yet still have
ALT-PgUp/ALT-PgDn, scrollback, capture, etc.

Qodem includes a Alt Code Key function (Alt-\\) for entering a raw
decimal byte value (0-255) or a 16-bit Unicode value (0-FFFF).

Capture, screen dump, and saving scrollback can be saved in several
formats (configured in qodemrc).  "normal" behaves like Qmodem: colors
and emulation commands are stripped out, leaving a UTF-8 encoded
black-and-white text file.  "html" saves in an HTML format that
includes colors.  For capture only, "raw" saves the raw incoming byte
stream before any UTF-8 decoding or emulation processing.  For all
save formats, "ask" will bring up a dialog to select the save format
every time the save is requested.  For phone book entries that specify
a capture file, if the capture type is "ask" it will be saved in
"normal" format.

Host mode behaves differently.  It uses simple ASCII menus rather than
CP437 menus, provides no "Optional Activities", and has fewer features
than Qmodem's Host Mode implementation.  However, in addition to
listening on the modem, it can also listen on TCP ports for raw
socket, telnet, and ssh connections; optionally the TCP port can be
exposed via UPnP to the general Internet.



TRANSLATE TABLES
----------------

Qodem has a slightly different method for translating bytes and
Unicode code points than Qmodem's Alt-A Translate Table function.
This section describes the Qodem Translate Tables.

----

The Alt-A Translate Table function has been renamed to Translate
Tables (plural), and encompasses both 8-bit and Unicode conversions.
The data flow is as follows:

  * Bytes received from the wire are converted according to the 8-bit
    INPUT table before any other processing.  Similarly, bytes are
    converted through the 8-bit OUTPUT table before being written to
    the wire.

  * Code points written to the screen are converted according to the
    Unicode INPUT table.  Code points read from the keyboard are
    converted through the Unicode OUTPUT table before being converted
    to UTF-8.

  * When using 8-bit codepages, Qodem attempts to convert code points
    read from the keyboard back to the correct 8-bit codepage value
    based on several strategies.  If no values can be found, '?' is
    sent instead.

  * Capture, scrollback, screen dump, and keyboard macro files are
    stored in untranslated formats where possible.  'raw' capture
    records bytes before the 8-bit tables are applied; 'normal'
    capture and other files record code points after 8-bit tables are
    applied but before Unicode tables are applied.

  * 8-bit and Unicode tables can be specified for each phonebook
    entry.

  * An EBCDIC-to-CP437 table is provided, but is largely untested.



DOCUMENTATION
-------------

Qodem has three sources of documentation:

* This README.

* Online help, accessed in most screens by pressing F1.

* The qodem and qodem-x11 man pages.



CONTRIBUTING
------------

Qodem is dedicated to the public domain.  Anyone is free to see and
modify the source code and release new versions under whatever license
terms they wish.

The official repository is hosted on github at
https://github.com/klamonte/qodem .  Pull requests are very much
welcomed.



ACKNOWLEDGMENTS
---------------

We'd like to thank the following individuals:

* John Friel III for writing Qmodem which was the inspiration for this
  project.

* Paul Williams, for his excellent work documenting the DEC VT
  terminals at http://www.vt100.net/emu/dec_ansi_parser .

* Thomas E. Dickey, for his work on the xterm emulator and the ncurses
  library.  Both Mr. Williams and Mr. Dickey have answered numerous
  questions over the years in comp.terminals that were archived and
  greatly aided the development of Qodem's emulation layer.

* Bjorn Larsson, William McBrine, and the many developers involved in
  PDCurses who dedicated their work to the public domain.

* Miquel van Smoorenburg and the many developers involved in minicom
  who licensed their work under the GNU General Public License.

* Thomas BERNARD and the developers involved in miniupnpc who licensed
  their work under a BSD-like license.

* Jeff Gustafson for creating the Fedora RPM build script.

* Martin Godisch for help in packaging for the deb build.

* Jason Scott for creating "BBS: The Documentary".

* Peter Gutmann for developing cryptlib and licensing it under an open
  source compatible license.

* Nathanael Culver for obtaining Qmodem 2.3, 4.2f, and QmodemPro 1.50.

* Tim Hentenaar for the original version of the ATASCII code.
