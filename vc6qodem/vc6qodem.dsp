# Microsoft Developer Studio Project File - Name="vc6qodem" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Application" 0x0101

CFG=vc6qodem - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "vc6qodem.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "vc6qodem.mak" CFG="vc6qodem - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "vc6qodem - Win32 Release" (based on "Win32 (x86) Application")
!MESSAGE "vc6qodem - Win32 Debug" (based on "Win32 (x86) Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "vc6qodem - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "../bin"
# PROP Intermediate_Dir "../objs"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /YX /FD /c
# ADD CPP /nologo /G3 /MT /W3 /GX /O2 /I "../lib/cryptlib" /I "../include" /I "../lib/pdcurses/include" /D "NDEBUG" /D "Q_PDCURSES" /D "Q_PDCURSES_WIN32" /D "UNICODE" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "Q_SSH_CRYPTLIB" /D "STATIC_LIB" /YX /FD /Zm1000 /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:windows /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib ws2_32.lib netapi32.lib shlwapi.lib /nologo /subsystem:windows /machine:I386 /nodefaultlib:"libcd"
# SUBTRACT LINK32 /pdb:none /nodefaultlib

!ELSEIF  "$(CFG)" == "vc6qodem - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "../bin"
# PROP Intermediate_Dir "../objs"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /YX /FD /GZ /c
# ADD CPP /nologo /G3 /MTd /W3 /Gm /GX /ZI /Od /I "../include" /I "../lib/pdcurses/include" /I "../lib/cryptlib" /I "../lib/upnp" /D "_DEBUG" /D "Q_PDCURSES" /D "Q_PDCURSES_WIN32" /D "Q_UPNP" /D "UNICODE" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "Q_SSH_CRYPTLIB" /D "STATIC_LIB" /D "MINIUPNP_STATICLIB" /YX /FD /Zm1000 /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:windows /debug /machine:I386 /pdbtype:sept
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib ws2_32.lib netapi32.lib shlwapi.lib /nologo /subsystem:windows /debug /machine:I386 /pdbtype:sept
# SUBTRACT LINK32 /pdb:none

!ENDIF 

# Begin Target

# Name "vc6qodem - Win32 Release"
# Name "vc6qodem - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=..\source\ansi.c
# End Source File
# Begin Source File

SOURCE=..\source\atascii.c
# End Source File
# Begin Source File

SOURCE=..\source\avatar.c
# End Source File
# Begin Source File

SOURCE=..\source\codepage.c
# End Source File
# Begin Source File

SOURCE=..\source\colors.c
# End Source File
# Begin Source File

SOURCE=..\source\common.c
# End Source File
# Begin Source File

SOURCE=..\source\console.c
# End Source File
# Begin Source File

SOURCE=..\source\dialer.c
# End Source File
# Begin Source File

SOURCE=..\source\emulation.c
# End Source File
# Begin Source File

SOURCE=..\source\field.c
# End Source File
# Begin Source File

SOURCE=..\source\forms.c
# End Source File
# Begin Source File

SOURCE=..\source\help.c
# End Source File
# Begin Source File

SOURCE=..\source\host.c
# End Source File
# Begin Source File

SOURCE=..\source\input.c
# End Source File
# Begin Source File

SOURCE=..\source\kermit.c
# End Source File
# Begin Source File

SOURCE=..\source\keyboard.c
# End Source File
# Begin Source File

SOURCE=..\source\modem.c
# End Source File
# Begin Source File

SOURCE=..\source\music.c
# End Source File
# Begin Source File

SOURCE=..\source\netclient.c
# End Source File
# Begin Source File

SOURCE=..\source\options.c
# End Source File
# Begin Source File

SOURCE=..\source\petscii.c
# End Source File
# Begin Source File

SOURCE=..\source\phonebook.c
# End Source File
# Begin Source File

SOURCE=..\source\protocols.c
# End Source File
# Begin Source File

SOURCE=..\source\qodem.c
# End Source File
# Begin Source File

SOURCE=..\build\win32\resources.rc
# End Source File
# Begin Source File

SOURCE=..\source\screen.c
# End Source File
# Begin Source File

SOURCE=..\source\script.c
# End Source File
# Begin Source File

SOURCE=..\source\scrollback.c
# End Source File
# Begin Source File

SOURCE=..\source\states.c
# End Source File
# Begin Source File

SOURCE=..\source\translate.c
# End Source File
# Begin Source File

SOURCE=..\source\vt100.c
# End Source File
# Begin Source File

SOURCE=..\source\vt52.c
# End Source File
# Begin Source File

SOURCE=..\source\xmodem.c
# End Source File
# Begin Source File

SOURCE=..\source\zmodem.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=..\source\ansi.h
# End Source File
# Begin Source File

SOURCE=..\source\atascii.h
# End Source File
# Begin Source File

SOURCE=..\source\avatar.h
# End Source File
# Begin Source File

SOURCE=..\source\codepage.h
# End Source File
# Begin Source File

SOURCE=..\source\colors.h
# End Source File
# Begin Source File

SOURCE=..\source\common.h
# End Source File
# Begin Source File

SOURCE=..\source\console.h
# End Source File
# Begin Source File

SOURCE=..\source\dialer.h
# End Source File
# Begin Source File

SOURCE=..\source\emulation.h
# End Source File
# Begin Source File

SOURCE=..\source\field.h
# End Source File
# Begin Source File

SOURCE=..\source\forms.h
# End Source File
# Begin Source File

SOURCE=..\source\help.h
# End Source File
# Begin Source File

SOURCE=..\source\host.h
# End Source File
# Begin Source File

SOURCE=..\source\input.h
# End Source File
# Begin Source File

SOURCE=..\source\kermit.h
# End Source File
# Begin Source File

SOURCE=..\source\keyboard.h
# End Source File
# Begin Source File

SOURCE=..\source\modem.h
# End Source File
# Begin Source File

SOURCE=..\source\music.h
# End Source File
# Begin Source File

SOURCE=..\source\netclient.h
# End Source File
# Begin Source File

SOURCE=..\source\options.h
# End Source File
# Begin Source File

SOURCE=..\source\petscii.h
# End Source File
# Begin Source File

SOURCE=..\source\phonebook.h
# End Source File
# Begin Source File

SOURCE=..\source\protocols.h
# End Source File
# Begin Source File

SOURCE=..\source\qcurses.h
# End Source File
# Begin Source File

SOURCE=..\source\qodem.h
# End Source File
# Begin Source File

SOURCE=..\source\screen.h
# End Source File
# Begin Source File

SOURCE=..\source\script.h
# End Source File
# Begin Source File

SOURCE=..\source\scrollback.h
# End Source File
# Begin Source File

SOURCE=..\source\states.h
# End Source File
# Begin Source File

SOURCE=..\source\translate.h
# End Source File
# Begin Source File

SOURCE=..\source\vt100.h
# End Source File
# Begin Source File

SOURCE=..\source\vt52.h
# End Source File
# Begin Source File

SOURCE=..\source\xmodem.h
# End Source File
# Begin Source File

SOURCE=..\source\zmodem.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# Begin Source File

SOURCE=..\lib\c\vc6libc\Release\vc6libc.lib
# End Source File
# Begin Source File

SOURCE=..\lib\upnp\vc6miniupnpc\Release\vc6miniupnpc.lib
# End Source File
# Begin Source File

SOURCE=..\lib\pdcurses\vc6pdcurses\Release\vc6pdcurses.lib
# End Source File
# Begin Source File

SOURCE=..\lib\cryptlib\Release\crypt32static.lib
# End Source File
# End Target
# End Project
