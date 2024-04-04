# name the installer
OutFile "MixerInstaller.exe"
 
# define the directory to install to, the desktop in this case as specified  
# by the predefined $DESKTOP variable
InstallDir $PROGRAMFILES64\VolumeMixer\

# default section
Section

; CopyFiles /SILENT "..\MixerClient\x64\Release\MixerClient.exe" "$InstDir\MixerClient.exe"

# define the output path for this file
SetOutPath $INSTDIR

File "..\MixerClient\x64\Release\MixerClient.exe"

CreateShortCut "$SMPROGRAMS\Startup\VolumeMixer.lnk" "$INSTDIR\MixerClient.exe" \
  "" "$INSTDIR\MixerClient.exe" 2 SW_SHOWNORMAL \
  ALT|CTRL|SHIFT|F5 "VolumeMixer"

Exec '"$INSTDIR\MixerClient.exe"'

# default section end
SectionEnd