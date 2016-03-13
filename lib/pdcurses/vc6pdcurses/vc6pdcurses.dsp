# Microsoft Developer Studio Project File - Name="vc6pdcurses" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Static Library" 0x0104

CFG=vc6pdcurses - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "vc6pdcurses.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "vc6pdcurses.mak" CFG="vc6pdcurses - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "vc6pdcurses - Win32 Release" (based on "Win32 (x86) Static Library")
!MESSAGE "vc6pdcurses - Win32 Debug" (based on "Win32 (x86) Static Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "vc6pdcurses - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_MBCS" /D "_LIB" /YX /FD /c
# ADD CPP /nologo /G3 /MT /W3 /GX /O2 /I "../include" /I "../../../include" /D "WIN32" /D "NDEBUG" /D "_MBCS" /D "_LIB" /D "PDC_WIDE" /YX /FD /c
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo

!ELSEIF  "$(CFG)" == "vc6pdcurses - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_MBCS" /D "_LIB" /YX /FD /GZ /c
# ADD CPP /nologo /G3 /MTd /W3 /Gm /GX /ZI /Od /I "../include" /I "../../../include" /D "WIN32" /D "_DEBUG" /D "_MBCS" /D "_LIB" /D "PDC_WIDE" /YX /FD /GZ /c
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo

!ENDIF 

# Begin Target

# Name "vc6pdcurses - Win32 Release"
# Name "vc6pdcurses - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=..\pdcurses\addch.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\addchstr.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\addstr.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\attr.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\beep.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\bkgd.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\border.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\clear.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\color.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\debug.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\delch.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\deleteln.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\deprec.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\getch.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\getstr.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\getyx.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\inch.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\inchstr.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\initscr.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\inopts.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\insch.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\insstr.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\instr.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\kernel.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\keyname.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\mouse.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\move.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\outopts.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\overlay.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\pad.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\panel.c
# End Source File
# Begin Source File

SOURCE=..\win32a\pdcclip.c
# End Source File
# Begin Source File

SOURCE=..\win32a\pdcdisp.c
# End Source File
# Begin Source File

SOURCE=..\win32a\pdcgetsc.c
# End Source File
# Begin Source File

SOURCE=..\win32a\pdckbd.c
# End Source File
# Begin Source File

SOURCE=..\win32a\pdcscrn.c
# End Source File
# Begin Source File

SOURCE=..\win32a\pdcsetsc.c
# End Source File
# Begin Source File

SOURCE=..\win32a\pdcurses.rc
# End Source File
# Begin Source File

SOURCE=..\win32a\pdcutil.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\printw.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\refresh.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\scanw.c
# End Source File
# Begin Source File

SOURCE=..\win32a\scr2html.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\scr_dump.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\scroll.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\slk.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\termattr.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\terminfo.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\touch.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\util.c
# End Source File
# Begin Source File

SOURCE=..\pdcurses\window.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=..\include\curses.h
# End Source File
# Begin Source File

SOURCE=..\include\curspriv.h
# End Source File
# Begin Source File

SOURCE=..\include\panel.h
# End Source File
# Begin Source File

SOURCE=..\include\pdcwin.h
# End Source File
# Begin Source File

SOURCE=..\include\term.h
# End Source File
# End Group
# End Target
# End Project
