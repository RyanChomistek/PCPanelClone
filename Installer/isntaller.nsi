!include "LogicLib.nsh"
!include "nsProcess.nsh"

# name the installer
OutFile "MixerInstaller.exe"
 
# define the directory to install to, the desktop in this case as specified  
# by the predefined $DESKTOP variable
InstallDir $PROGRAMFILES64\VolumeMixer\

Section ""
  StrCpy $1 "MixerClient.exe"

  nsProcess::_FindProcess "$1"
  Pop $R0
  ${If} $R0 = 0
    nsProcess::_KillProcess "$1"
    Pop $R0

    Sleep 500
  ${EndIf}

SectionEnd

# default section
Section

# define the output path for this file
SetOutPath $INSTDIR

File "..\MixerClient\x64\Release\MixerClient.exe"

CreateShortCut "$SMPROGRAMS\Startup\VolumeMixer.lnk" "$INSTDIR\MixerClient.exe" \
  "" "$INSTDIR\MixerClient.exe" 2 SW_SHOWNORMAL \
  ALT|CTRL|SHIFT|F5 "VolumeMixer"

Exec '"$INSTDIR\MixerClient.exe"'

# default section end
SectionEnd