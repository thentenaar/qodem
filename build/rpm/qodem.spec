Name:           qodem
Version:        1.0.0
Release:        1%{?dist}
Summary:        Qodem terminal emulator and communications package.

Group:          Applications/Communications
License:        Public Domain
URL:            http://qodem.sourceforge.net/
Source0:        https://downloads.sourceforge.net/project/qodem/qodem/1.0.0/qodem-1.0.0.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

Requires:  ncurses
BuildRequires:  ncurses-devel


%description
Qodem is an open-source re-implementation of the Qmodem(tm)
shareware communications package, updated for more modern uses.
Major features include:
    * Unicode display: translation of CP437 (PC VGA), VT100 DEC
      Special Graphics characters, VT220 National Replacement
      Character sets, etc., to Unicode
    * Terminal interface conveniences: scrollback buffer, capture
      file, screen dump, dialing directory, keyboard macros, script
      support
    * Connection methods: serial, local shell, command line, telnet,
      ssh, rlogin, raw socket
    * Emulations: ANSI.SYS (including "ANSI music"), Avatar, VT52,
      VT100/102, VT220, Linux, and XTerm
    * Transfer protocols: Xmodem, Ymodem, Zmodem, and Kermit
    * External script support.  Any program that reads stdin and
      writes to stdout and stderr can be run as a script.
    * Host mode that provides a micro-BBS with messages, files, and
      sysop chat.
This version is built for text consoles using ncurses.

%prep
%setup -q


%build
%configure --disable-sdl --disable-ssh
make %{?_smp_mflags}


%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT%{_mandir}/man1
cp docs/qodem.1 docs/xqodem.1 $RPM_BUILD_ROOT%{_mandir}/man1/


%clean
rm -rf $RPM_BUILD_ROOT


%files
%defattr(-,root,root,-)
%{_bindir}/qodem
%{_bindir}/xqodem
%{_mandir}/man1/qodem.1.gz
%{_mandir}/man1/xqodem.1.gz
%doc ChangeLog COPYING CREDITS README.md docs/TODO.md


%changelog
* Mon Jul 04 2016 Kevin Lamonte <lamonte at, users.sourceforge.net> - 1.0.0-1
* Wed Apr 27 2016 Kevin Lamonte <lamonte at, users.sourceforge.net> - 1.0beta-1
* Sat May 19 2012 Kevin Lamonte <lamonte at, users.sourceforge.net> - 1.0alpha-1
* Sun Nov 30 2008 Jeff Gustafson <jeffgus at, fedoraproject.org> - 0.1.2-1
- Initial package creation
