Unicode true
!include "MUI2.nsh"
!include "x64.nsh"
!include "LogicLib.nsh"
!ifndef PACKAGE_DIR
  !error "Pass PACKAGE_DIR, BUILD_ID and OUTPUT_FILE to makensis."
!endif
Name "3D Thumbnail"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\3DThumbnail"
RequestExecutionLevel user
SetCompressor /SOLID zlib
SetOverwrite on
VIProductVersion "0.5.0.0"
VIAddVersionKey /LANG=1033 "ProductName" "3D Thumbnail"
VIAddVersionKey /LANG=1033 "FileDescription" "3D Thumbnail Setup"
VIAddVersionKey /LANG=1033 "FileVersion" "0.5.0"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Copyright (c) 2026 DMIND / dmind114@gmail.com"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TEXT "Windows 탐색기에 3D 파일 썸네일과 색상 설정을 설치합니다.$\r$\n$\r$\n지원 형식: 3MF, STL, STEP/STP, OBJ, GLB/glTF, PLY, IGES/IGS, FBX.$\r$\n$\r$\n현재 사용자 계정에 설치하며 기본 파일 열기 앱은 유지합니다. 모델 처리는 별도 작업자에서 실행합니다.$\r$\n$\r$\n0.5.0은 Windows 11 x64에서 검증 중인 시험 배포입니다."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${PACKAGE_DIR}\LICENSE"
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\versions\${BUILD_ID}\thumbnail-settings.exe"
!define MUI_FINISHPAGE_RUN_TEXT "색상 설정 열기"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH
!insertmacro MUI_LANGUAGE "Korean"
!insertmacro MUI_LANGUAGE "English"

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "Windows x64 is required."
    Abort
  ${EndIf}
  SetShellVarContext current
  SetRegView 64
FunctionEnd

Section "Install"
  SetOutPath "$INSTDIR"
  File "${PACKAGE_DIR}\LICENSE"
  File "${PACKAGE_DIR}\README.md"
  File "${PACKAGE_DIR}\THIRD_PARTY_NOTICES.md"
  File "${PACKAGE_DIR}\shell-formats.json"
  SetOutPath "$INSTDIR\licenses"
  File "${PACKAGE_DIR}\licenses\*"
  SetOutPath "$INSTDIR\scripts"
  File "${PACKAGE_DIR}\scripts\*.ps1"
  File "${PACKAGE_DIR}\scripts\*.psm1"
  SetOutPath "$INSTDIR\versions\${BUILD_ID}"
  SetOverwrite off
  File "${PACKAGE_DIR}\versions\${BUILD_ID}\*"
  SetOverwrite on
  CreateDirectory "$LOCALAPPDATA\3DThumbnail"
  nsExec::ExecToStack /TIMEOUT=120000 '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$INSTDIR\scripts\installer-registration.ps1" -Action Install -StatePath "$LOCALAPPDATA\3DThumbnail\registration.json" -RuntimeDirectory "$INSTDIR\versions\${BUILD_ID}"'
  Pop $0
  Pop $1
  FileOpen $2 "$LOCALAPPDATA\3DThumbnail\setup.log" a
  FileWrite $2 "Install ${BUILD_ID}: $0$\r$\n$1$\r$\n"
  FileClose $2
  DetailPrint "$1"
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "Thumbnail registration failed ($0). Recovery records were retained. See $LOCALAPPDATA\3DThumbnail\setup.log" /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\3D Thumbnail"
  CreateShortcut "$SMPROGRAMS\3D Thumbnail\색상 설정.lnk" "$INSTDIR\versions\${BUILD_ID}\thumbnail-settings.exe"
  CreateShortcut "$SMPROGRAMS\3D Thumbnail\사용 설명서.lnk" "$INSTDIR\README.md"
  CreateShortcut "$SMPROGRAMS\3D Thumbnail\제거.lnk" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\3DThumbnail" "DisplayName" "3D Thumbnail"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\3DThumbnail" "DisplayVersion" "0.5.0"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\3DThumbnail" "Publisher" "DMIND"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\3DThumbnail" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\3DThumbnail" "DisplayIcon" "$INSTDIR\versions\${BUILD_ID}\thumbnail-settings.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\3DThumbnail" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\3DThumbnail" "QuietUninstallString" '$\"$INSTDIR\Uninstall.exe$\" /S'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\3DThumbnail" "URLInfoAbout" "https://github.com/XomPM/3d-thumbnail"
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\3DThumbnail" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\3DThumbnail" "NoRepair" 1
SectionEnd

Section "Uninstall"
  SetShellVarContext current
  SetRegView 64
  ${If} $INSTDIR != "$LOCALAPPDATA\Programs\3DThumbnail"
    MessageBox MB_ICONSTOP "Unexpected installation path. Removal refused."
    Abort
  ${EndIf}
  nsExec::ExecToStack /TIMEOUT=120000 '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$INSTDIR\scripts\installer-registration.ps1" -Action Uninstall -StatePath "$LOCALAPPDATA\3DThumbnail\registration.json"'
  Pop $0
  Pop $1
  FileOpen $2 "$LOCALAPPDATA\3DThumbnail\setup.log" a
  FileWrite $2 "Uninstall: $0$\r$\n$1$\r$\n"
  FileClose $2
  DetailPrint "$1"
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "Registration recovery failed. Files and recovery records have been retained." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
  nsExec::ExecToStack /TIMEOUT=60000 '"$WINDIR\Sysnative\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$INSTDIR\scripts\installer-cleanup.ps1" -InstallRoot "$INSTDIR"'
  Pop $0
  Pop $1
  DetailPrint "$1"
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "Registration removed. File cleanup needs attention; files have been retained." /SD IDOK
    SetErrorLevel 1
    Abort
  ${EndIf}
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\3DThumbnail"
  Delete "$SMPROGRAMS\3D Thumbnail\색상 설정.lnk"
  Delete "$SMPROGRAMS\3D Thumbnail\사용 설명서.lnk"
  Delete "$SMPROGRAMS\3D Thumbnail\제거.lnk"
  RMDir "$SMPROGRAMS\3D Thumbnail"
  !include "${PACKAGE_DIR}\uninstall-files.nsh"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
SectionEnd
