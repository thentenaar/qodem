# Microsoft Developer Studio Project File - Name="vc6libc" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Static Library" 0x0104

CFG=vc6libc - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "vc6libc.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "vc6libc.mak" CFG="vc6libc - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "vc6libc - Win32 Release" (based on "Win32 (x86) Static Library")
!MESSAGE "vc6libc - Win32 Debug" (based on "Win32 (x86) Static Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "vc6libc - Win32 Release"

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
# ADD CPP /nologo /G3 /MT /W3 /GX /O2 /I "../../../include" /D "WIN32" /D "NDEBUG" /D "_MBCS" /D "_LIB" /YX /FD /c
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo

!ELSEIF  "$(CFG)" == "vc6libc - Win32 Debug"

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
# ADD CPP /nologo /G3 /MTd /W3 /Gm /GX /ZI /Od /I "../../../include" /D "WIN32" /D "_DEBUG" /D "_MBCS" /D "_LIB" /YX /FD /GZ /c
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

# Name "vc6libc - Win32 Release"
# Name "vc6libc - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=..\basename.c
# End Source File
# Begin Source File

SOURCE=..\dirent.c
# End Source File
# Begin Source File

SOURCE=..\dirname.c
# End Source File
# Begin Source File

SOURCE=..\fnmatch.c
# End Source File
# Begin Source File

SOURCE=..\getopt.c
# End Source File
# Begin Source File

SOURCE=..\getopt1.c
# End Source File
# Begin Source File

SOURCE=..\snprintf.c
# End Source File
# Begin Source File

SOURCE=..\strptime.c
# End Source File
# Begin Source File

SOURCE=..\sys\time.c
# End Source File
# Begin Source File

SOURCE=..\wspiapi.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=..\..\..\include\dirent.h
# End Source File
# Begin Source File

SOURCE=..\..\..\include\fnmatch.h
# End Source File
# Begin Source File

SOURCE=..\..\..\include\getopt.h
# End Source File
# Begin Source File

SOURCE=..\..\..\include\libgen.h
# End Source File
# Begin Source File

SOURCE=..\..\..\include\snprintf.h
# End Source File
# Begin Source File

SOURCE=..\..\..\include\stdint.h
# End Source File
# Begin Source File

SOURCE=..\..\..\include\strptime.h
# End Source File
# Begin Source File

SOURCE=..\..\..\include\sys\time.h
# End Source File
# Begin Source File

SOURCE=..\..\..\include\wchar.h
# End Source File
# Begin Source File

SOURCE=..\..\..\include\wspiapi.h
# End Source File
# End Group
# End Target
# End Project
