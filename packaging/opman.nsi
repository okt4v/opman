; opman Windows installer (NSIS 3, built by packaging/build-windows.sh; makensis runs on Linux).
; Per-user install to %LOCALAPPDATA%\Programs\opman: no admin rights / UAC prompt.
; makensis -DVERSION=x.y.z -DSTAGE=<dir with opman.exe, assets\, LICENSE> -DOUTFILE=<setup.exe> opman.nsi
; Silent: setup.exe /S [/D=C:\path]; uninstall: Uninstall.exe /S. Settings in %APPDATA%\opman and your notes
; (Documents\opman) are kept unless "Remove my opman settings" is ticked; notes are never removed.

!ifndef VERSION | STAGE | OUTFILE
  !error "pass -DVERSION=... -DSTAGE=... -DOUTFILE=..."
!endif
!if ! /FileExists "${STAGE}\opman.exe"
  !error "${STAGE}\opman.exe not found"
!endif

Unicode true
SetCompressor /SOLID lzma
ManifestDPIAware true
RequestExecutionLevel user

!define APP "opman"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP}"

Name "${APP}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\${APP}"
InstallDirRegKey HKCU "Software\${APP}" "InstallDir"
BrandingText "${APP} ${VERSION}"

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${APP}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "${APP} installer"
VIAddVersionKey "LegalCopyright" "GPL-3.0-or-later"

!include "MUI2.nsh"
!include "x64.nsh"

!define MUI_ICON "opman.ico"  ; makensis runs in the script's directory
!define MUI_UNICON "opman.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\opman.exe"
!define MUI_COMPONENTSPAGE_NODESC

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${STAGE}\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_COMPONENTS
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "opman needs 64-bit Windows."
    Abort
  ${EndIf}
FunctionEnd

Section "opman" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  RMDir /r "$INSTDIR\assets"  ; drop files a previous version had but this one doesn't
  File /r "${STAGE}\*"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateShortcut "$SMPROGRAMS\${APP}.lnk" "$INSTDIR\opman.exe"
  WriteRegStr HKCU "Software\${APP}" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "${APP}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\opman.exe,0"
  WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKCU "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
SectionEnd

Section "Desktop shortcut" SecDesktop
  CreateShortcut "$DESKTOP\${APP}.lnk" "$INSTDIR\opman.exe"
SectionEnd

Section "un.opman" UnSecMain
  SectionIn RO
  Delete "$INSTDIR\opman.exe"
  Delete "$INSTDIR\LICENSE"
  RMDir /r "$INSTDIR\assets"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"  ; only if empty: never removes files we didn't put there
  Delete "$SMPROGRAMS\${APP}.lnk"
  Delete "$DESKTOP\${APP}.lnk"
  DeleteRegKey HKCU "${UNINST_KEY}"
  DeleteRegKey HKCU "Software\${APP}"
SectionEnd

; off by default: settings only, the notes folder is the user's
Section /o "un.Remove my opman settings" UnSecData
  RMDir /r "$APPDATA\${APP}"
SectionEnd
