# Microsoft Developer Studio Project File - Name="vc6miniupnpc" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Static Library" 0x0104

CFG=vc6miniupnpc - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "vc6miniupnpc.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "vc6miniupnpc.mak" CFG="vc6miniupnpc - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "vc6miniupnpc - Win32 Release" (based on "Win32 (x86) Static Library")
!MESSAGE "vc6miniupnpc - Win32 Debug" (based on "Win32 (x86) Static Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "vc6miniupnpc - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "objs"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_MBCS" /D "_LIB" /YX /FD /c
# ADD CPP /nologo /G3 /MT /W3 /GX /O2 /I "..\..\..\include" /D "NDEBUG" /D "WIN32" /D "_UNICODE" /D "_LIB" /D "MINIUPNP_STATICLIB" /YX /FD /c
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo

!ELSEIF  "$(CFG)" == "vc6miniupnpc - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "objs"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_MBCS" /D "_LIB" /YX /FD /GZ /c
# ADD CPP /nologo /G3 /MTd /W3 /Gm /GX /ZI /Od /I "..\..\..\include" /D "_DEBUG" /D "WIN32" /D "_UNICODE" /D "_LIB" /D "MINIUPNP_STATICLIB" /D "STATIC_LIB" /YX /FD /GZ /c
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

# Name "vc6miniupnpc - Win32 Release"
# Name "vc6miniupnpc - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=..\connecthostport.c
# ADD CPP /G3 /MT
# End Source File
# Begin Source File

SOURCE=..\igd_desc_parse.c
# ADD CPP /G3 /MT
# End Source File
# Begin Source File

SOURCE=..\minisoap.c
# ADD CPP /G3 /MT
# End Source File
# Begin Source File

SOURCE=..\minissdpc.c
# ADD CPP /G3 /MT
# End Source File
# Begin Source File

SOURCE=..\miniupnpc.c
# ADD CPP /G3 /MT
# End Source File
# Begin Source File

SOURCE=..\miniwget.c
# ADD CPP /G3 /MT
# End Source File
# Begin Source File

SOURCE=..\minixml.c
# ADD CPP /G3 /MT
# End Source File
# Begin Source File

SOURCE=..\portlistingparse.c
# ADD CPP /G3 /MT
# End Source File
# Begin Source File

SOURCE=..\receivedata.c
# ADD CPP /G3 /MT
# End Source File
# Begin Source File

SOURCE=..\upnpcommands.c
# ADD CPP /G3 /MT
# End Source File
# Begin Source File

SOURCE=..\upnperrors.c
# ADD CPP /G3 /MT
# End Source File
# Begin Source File

SOURCE=..\upnpreplyparse.c
# ADD CPP /G3 /MT
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=..\codelength.h
# End Source File
# Begin Source File

SOURCE=..\connecthostport.h
# End Source File
# Begin Source File

SOURCE=..\igd_desc_parse.h
# End Source File
# Begin Source File

SOURCE=..\minisoap.h
# End Source File
# Begin Source File

SOURCE=..\minissdpc.h
# End Source File
# Begin Source File

SOURCE=..\miniupnpc.h
# End Source File
# Begin Source File

SOURCE=..\miniupnpc_declspec.h
# End Source File
# Begin Source File

SOURCE=..\miniupnpcstrings.h
# End Source File
# Begin Source File

SOURCE=..\miniupnpctypes.h
# End Source File
# Begin Source File

SOURCE=..\miniwget.h
# End Source File
# Begin Source File

SOURCE=..\minixml.h
# End Source File
# Begin Source File

SOURCE=..\portlistingparse.h
# End Source File
# Begin Source File

SOURCE=..\receivedata.h
# End Source File
# Begin Source File

SOURCE=..\upnpcommands.h
# End Source File
# Begin Source File

SOURCE=..\upnperrors.h
# End Source File
# Begin Source File

SOURCE=..\upnpreplyparse.h
# End Source File
# End Group
# End Target
# End Project
