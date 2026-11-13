; ByteSieve 설치 프로그램(Inno Setup 6). 설계와 결정 사항은 docs/release.md, docs/ui-design.md#설치-마법사.
;
;   ISCC /DAppVersion=1.0.0 /DStageDir=<cmake --install 결과 폴더> /DOutputDir=<출력 폴더> installer\bytesieve.iss
;
; 보통은 tools/package.ps1 이 Release 빌드·설치(stage)·포터블 ZIP·SHA256 과 함께 부른다.
;
; 원칙
;   - 사용자별 설치(관리자 권한 없음): %LOCALAPPDATA%\Programs\ByteSieve. HKCU 가 실제 사용자의 것이 된다.
;   - 설치 언어(한국어·English·日本語·简体中文)를 앱 표시 언어로 기록한다(HKCU\Software\ByteSieve\Language).
;     이미 값이 있으면(앱 [설정]에서 고른 언어, 이전 설치) 덮어쓰지 않는다.
;   - 라이선스 화면은 설치 언어의 이용약관(installer\terms\terms-<언어>.txt) 뒤에 [부록] 머리글(LicenseAppendix)과
;     설치하는 LICENSE.txt(PolyForm Noncommercial 1.0.0 원문)를 수정 없이 이어 붙여 보여 준다(InitializeWizard).
;     원문의 사본을 따로 두지 않으므로 LICENSE 를 고쳐도 화면과 어긋나지 않는다.
;     약관 네 언어는 설치 폴더의 terms\ 에도 넣어 설치한 뒤 다시 볼 수 있게 한다(README.txt 에 안내).
;   - 제거할 때 지우는 것은 설치한 프로그램 파일·바로 가기뿐이다. 테마·언어 설정, 스캔 기록,
;     각 드라이브의 격리 폴더(.bytesieve-trash)는 절대 지우지 않고 남아 있다고 알린다(격리 항목은 사용자 파일).
;   - 앱이 실행 중이면(뮤텍스 ByteSieve.Gui.Running, src/gui_main.c) 닫으라고 안내한다.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StageDir
  #define StageDir "..\build\stage"
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

; 검증용 빌드(/DTestMode, tools/test_installer.ps1): 실제 설치본·설정과 겹치지 않게 AppId·이름·설정 키를 바꾸고,
; 바로 가기를 만들지 않을 수 있게 한다(/NOICONS). 배포용은 아래 #else 값을 쓴다.
#ifdef TestMode
  #define AppIdValue "{{59F2B501-42F4-4B26-92A9-7BC6242C2D27}"
  #define AppTitle "ByteSieve Installer Test"
  #define SettingsKey "Software\ByteSieveTest\installer"
  #define SetupName "ByteSieve-" + AppVersion + "-installer-test"
#else
  #define AppIdValue "{{76B7146F-FC3B-4E67-B142-1BA4B7B2CEBE}"
  #define AppTitle "ByteSieve"
  #define SettingsKey "Software\ByteSieve"
  #define SetupName "ByteSieve-" + AppVersion + "-setup-x64"
#endif

; 중국어 간체 번역: 컴파일러에 있으면 그것을, 없으면(예전 Inno Setup) 저장소의 사본을 쓴다.
#if FileExists(AddBackslash(CompilerPath) + "Languages\ChineseSimplified.isl")
  #define ZhHansIsl "compiler:Languages\ChineseSimplified.isl"
#else
  #define ZhHansIsl "languages\ChineseSimplified.isl"
#endif

[Setup]
; AppId 는 업그레이드·제거를 같은 앱으로 알아보는 값이다. 배포용 값({76B7146F-...})은 절대 바꾸지 않는다.
AppId={#AppIdValue}
AppName={#AppTitle}
AppVersion={#AppVersion}
AppVerName={#AppTitle} {#AppVersion}
AppPublisher=gajigaji04
AppPublisherURL=https://github.com/gajigaji-dev/ByteSieve
AppSupportURL=https://github.com/gajigaji-dev/ByteSieve/issues
AppUpdatesURL=https://github.com/gajigaji-dev/ByteSieve/releases
VersionInfoVersion={#AppVersion}
PrivilegesRequired=lowest
DefaultDirName={autopf}\{#AppTitle}
DefaultGroupName={#AppTitle}
DisableProgramGroupPage=yes
#ifdef TestMode
AllowNoIcons=yes
#endif
LicenseFile={#StageDir}\LICENSE.txt
SetupIconFile=..\res\bytesieve.ico
UninstallDisplayIcon={app}\bytesieve-gui.exe
UninstallDisplayName={#AppTitle}
OutputDir={#OutputDir}
OutputBaseFilename={#SetupName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
AppMutex=ByteSieve.Gui.Running
CloseApplications=yes
ShowLanguageDialog=yes
UsePreviousLanguage=no

[Languages]
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"; LicenseFile: "{#StageDir}\terms\terms-ko.txt"
Name: "english"; MessagesFile: "compiler:Default.isl"; LicenseFile: "{#StageDir}\terms\terms-en.txt"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"; LicenseFile: "{#StageDir}\terms\terms-ja.txt"
Name: "chinesesimplified"; MessagesFile: "{#ZhHansIsl}"; LicenseFile: "{#StageDir}\terms\terms-zh-Hans.txt"

[CustomMessages]
korean.LicenseAppendix=%n%n==========================================================================%n[부록] 라이선스 원문 - PolyForm Noncommercial License 1.0.0%n아래는 라이선스 원문을 수정하지 않고 그대로 실은 것입니다(영문).%n==========================================================================%n
english.LicenseAppendix=%n%n==========================================================================%n[Appendix] License text - PolyForm Noncommercial License 1.0.0%nThe License text below is reproduced without modification.%n==========================================================================%n
japanese.LicenseAppendix=%n%n==========================================================================%n[付録] ライセンス原文 - PolyForm Noncommercial License 1.0.0%n以下は、ライセンス原文を変更せずにそのまま掲載したものです(英文)。%n==========================================================================%n
chinesesimplified.LicenseAppendix=%n%n==========================================================================%n[附录] 许可证原文 - PolyForm Noncommercial License 1.0.0%n以下为未经修改、按原样收录的许可证原文（英文）。%n==========================================================================%n
korean.ComponentMain=ByteSieve (필수)
english.ComponentMain=ByteSieve (required)
japanese.ComponentMain=ByteSieve（必須）
chinesesimplified.ComponentMain=ByteSieve（必需）
korean.ComponentCli=명령줄 도구 disk-scanner
english.ComponentCli=Command-line tool disk-scanner
japanese.ComponentCli=コマンドライン ツール disk-scanner
chinesesimplified.ComponentCli=命令行工具 disk-scanner
korean.KeptData=ByteSieve를 제거했습니다.%n%n다음은 사용자 데이터라서 지우지 않고 남겨 두었습니다:%n  • 테마·언어 설정 (HKCU\Software\ByteSieve)%n  • 스캔 기록 (%1)%n  • 각 드라이브의 격리 폴더(.bytesieve-trash)에 있는 항목%n%n격리 폴더 항목은 정리 중에 옮긴 사용자 파일입니다. 필요 없으면 직접 확인한 뒤 지우세요.
english.KeptData=ByteSieve has been removed.%n%nThe following user data was kept:%n  • Theme and language settings (HKCU\Software\ByteSieve)%n  • Scan history (%1)%n  • Items in the quarantine folder (.bytesieve-trash) on each drive%n%nQuarantined items are your files moved during cleanup. Check them yourself before deleting.
japanese.KeptData=ByteSieve を削除しました。%n%n次のユーザー データは削除せずに残しています:%n  • テーマ・言語の設定 (HKCU\Software\ByteSieve)%n  • スキャン履歴 (%1)%n  • 各ドライブの隔離フォルダー(.bytesieve-trash)の項目%n%n隔離項目は整理中に移動したユーザーのファイルです。不要な場合はご自身で確認してから削除してください。
chinesesimplified.KeptData=已卸载 ByteSieve。%n%n以下用户数据未删除，仍然保留：%n  • 主题和语言设置 (HKCU\Software\ByteSieve)%n  • 扫描记录 (%1)%n  • 各驱动器隔离文件夹(.bytesieve-trash)中的项目%n%n隔离项目是清理时移动的用户文件。如不需要，请自行确认后再删除。

[Types]
Name: "full"; Description: "{code:FullTypeName}"
Name: "custom"; Description: "{code:CustomTypeName}"; Flags: iscustom

[Components]
Name: "main"; Description: "{cm:ComponentMain}"; Types: full custom; Flags: fixed
Name: "cli"; Description: "{cm:ComponentCli}"; Types: full

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#StageDir}\bytesieve-gui.exe"; DestDir: "{app}"; Components: main; Flags: ignoreversion
Source: "{#StageDir}\disk-scanner.exe"; DestDir: "{app}"; Components: cli; Flags: ignoreversion
Source: "{#StageDir}\LICENSE.txt"; DestDir: "{app}"; Components: main; Flags: ignoreversion
Source: "{#StageDir}\README.txt"; DestDir: "{app}"; Components: main; Flags: ignoreversion
Source: "{#StageDir}\terms\terms-*.txt"; DestDir: "{app}\terms"; Components: main; Flags: ignoreversion

[Icons]
Name: "{group}\ByteSieve"; Filename: "{app}\bytesieve-gui.exe"
Name: "{autodesktop}\ByteSieve"; Filename: "{app}\bytesieve-gui.exe"; Tasks: desktopicon

[Registry]
; 설치 언어를 앱 표시 언어로. 이미 값이 있으면 그대로 둔다(createvalueifdoesntexist). 제거할 때도 지우지 않는다.
Root: HKCU; Subkey: "{#SettingsKey}"; ValueType: string; ValueName: "Language"; ValueData: "ko"; Languages: korean; Flags: createvalueifdoesntexist
Root: HKCU; Subkey: "{#SettingsKey}"; ValueType: string; ValueName: "Language"; ValueData: "en"; Languages: english; Flags: createvalueifdoesntexist
Root: HKCU; Subkey: "{#SettingsKey}"; ValueType: string; ValueName: "Language"; ValueData: "ja"; Languages: japanese; Flags: createvalueifdoesntexist
Root: HKCU; Subkey: "{#SettingsKey}"; ValueType: string; ValueName: "Language"; ValueData: "zh-Hans"; Languages: chinesesimplified; Flags: createvalueifdoesntexist

[Run]
Filename: "{app}\bytesieve-gui.exe"; Description: "{cm:LaunchProgram,ByteSieve}"; Flags: nowait postinstall skipifsilent

; [UninstallDelete] 는 일부러 두지 않는다: 설정·기록·격리 폴더는 제거해도 남긴다.

[Code]
function FullTypeName(Param: String): String;
begin
  Result := SetupMessage(msgFullInstallation);
end;

function CustomTypeName(Param: String): String;
begin
  Result := SetupMessage(msgCustomInstallation);
end;

procedure InitializeWizard;
var
  License: AnsiString;
begin
  { 이용약관(LicenseFile) 뒤에 설치 언어의 [부록] 머리글과 라이선스 원문을 그대로 붙인다. 원문은 ASCII 다. }
  ExtractTemporaryFile('LICENSE.txt');
  { 원문을 읽지 못하면 원문 없이 동의받지 않도록 설치를 멈춘다. }
  if not LoadStringFromFile(ExpandConstant('{tmp}\LICENSE.txt'), License) then
    RaiseException('LICENSE.txt could not be read.');
  WizardForm.LicenseMemo.Text := WizardForm.LicenseMemo.Text + FmtMessage(CustomMessage('LicenseAppendix'), []) + String(License);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  { 제거가 끝나면 남겨 둔 사용자 데이터를 알린다(조용한 제거에서는 띄우지 않음). }
  if (CurUninstallStep = usPostUninstall) and not UninstallSilent then
    MsgBox(FmtMessage(CustomMessage('KeptData'), [ExpandConstant('{localappdata}\ByteSieve\history')]),
           mbInformation, MB_OK);
end;
