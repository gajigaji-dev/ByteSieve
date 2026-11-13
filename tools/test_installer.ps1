# 설치 프로그램 검증: 설치 → 업그레이드 → (앱 실행 중) 업그레이드 거부 → 제거를 조용한 모드로 실행하고 결과를 확인한다.
#
#   powershell -ExecutionPolicy Bypass -File tools\test_installer.ps1        # tools\package.ps1 을 먼저 실행(dist\stage 필요)
#
# 실제 설치본·설정을 건드리지 않게 installer\bytesieve.iss 를 검증용(/DTestMode)으로 컴파일해 쓴다:
#   다른 AppId·이름("ByteSieve Installer Test"), 언어 기록은 HKCU\Software\ByteSieveTest\installer, 설치 폴더는 임시 폴더,
#   바로 가기는 만들지 않음(/NOICONS). 실행 중 확인에 띄우는 앱도 설정·기록 위치를 테스트용으로 돌린다.
# 끝나면 실제 위치(%LOCALAPPDATA%\Programs\ByteSieve, HKCU\Software\ByteSieve, 시작 메뉴, 배포용 제거 항목)가
# 검증 전과 같은지 비교한다. 성공하면 "installer-test: ok" 를 출력하고 0 으로 끝난다.
param(
    [string]$Iscc = "",
    [string]$StageDir = "dist\stage",
    [string]$WorkDir = "",
    [switch]$KeepWork  # 성공해도 작업 폴더(설치 로그 포함)를 남긴다
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$TestAppId = "{59F2B501-42F4-4B26-92A9-7BC6242C2D27}"
$ProdAppId = "{76B7146F-FC3B-4E67-B142-1BA4B7B2CEBE}"
$TestKey = "HKCU:\Software\ByteSieveTest\installer"
$UninstallRoot = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall"
$TestUninstall = Join-Path $UninstallRoot "$($TestAppId)_is1"
$failures = 0

function Check([bool]$cond, [string]$what) {
    if ($cond) { Write-Host "  ok   $what" } else { Write-Host "  FAIL $what"; $script:failures++ }
}

# ---- 준비 ----
# ByteSieve 가 켜져 있으면 설치 프로그램이 뮤텍스로 감지해 멈추므로 검증할 수 없다. 사용자의 프로세스는 끄지 않는다.
$running = Get-Process bytesieve-gui -ErrorAction SilentlyContinue
if ($running) {
    throw "bytesieve-gui is running (PID $(($running | ForEach-Object Id) -join ', ')). Close it and run again."
}
if (-not $Iscc) {
    $Iscc = @((Get-Command iscc -ErrorAction SilentlyContinue).Source,
              "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
              "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
              "$env:ProgramFiles\Inno Setup 6\ISCC.exe") | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
}
if (-not $Iscc) { throw "Inno Setup (ISCC.exe) not found" }
$stage = (Resolve-Path $StageDir).Path
if (-not (Test-Path (Join-Path $stage "bytesieve-gui.exe"))) { throw "run tools\package.ps1 first ($stage)" }
if (-not $WorkDir) { $WorkDir = Join-Path ([IO.Path]::GetTempPath()) "ByteSieveInstallerTest" }
if (Test-Path $WorkDir) { Remove-Item $WorkDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
$app = Join-Path $WorkDir "app"

# 실제 위치의 상태(검증 뒤 비교)
function Snapshot {
    $startMenu = Join-Path ([Environment]::GetFolderPath("Programs")) ""
    [pscustomobject]@{
        ProgramDir   = Test-Path "$env:LOCALAPPDATA\Programs\ByteSieve"
        SettingsKey  = if (Test-Path "HKCU:\Software\ByteSieve") { (Get-ItemProperty "HKCU:\Software\ByteSieve" | Out-String) } else { "<none>" }
        Shortcuts    = (Get-ChildItem $startMenu -Filter "ByteSieve*" -ErrorAction SilentlyContinue | ForEach-Object Name) -join ","
        ProdUninst   = Test-Path (Join-Path $UninstallRoot "$($ProdAppId)_is1")
        Desktop      = Test-Path (Join-Path ([Environment]::GetFolderPath("Desktop")) "ByteSieve*.lnk")
    }
}
$before = Snapshot

# 앞선 실행이 남긴 검증용 설치가 있으면 먼저 지운다.
if (Test-Path $TestUninstall) {
    $old = (Get-ItemProperty $TestUninstall).UninstallString.Trim('"')
    if (Test-Path $old) { Start-Process $old -ArgumentList "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART" -Wait }
}
if (Test-Path $TestKey) { Remove-Item $TestKey -Recurse -Force }

# 검증용 설치 프로그램 두 버전
foreach ($v in "1.0.0", "1.0.1") {
    & $Iscc /Q /DTestMode /DAppVersion=$v "/DStageDir=$stage" "/DOutputDir=$WorkDir" (Join-Path $root "installer\bytesieve.iss")
    if ($LASTEXITCODE -ne 0) { throw "ISCC failed for $v" }
}
$setup100 = Join-Path $WorkDir "ByteSieve-1.0.0-installer-test.exe"
$setup101 = Join-Path $WorkDir "ByteSieve-1.0.1-installer-test.exe"

function RunSetup([string]$exe, [string]$lang, [string]$log) {
    $p = Start-Process $exe -PassThru -Wait -ArgumentList @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/NOICONS",
        "/LANG=$lang", "/DIR=`"$app`"", "/LOG=`"$(Join-Path $WorkDir $log)`"")
    return $p.ExitCode
}
function UninstallValue([string]$name) {
    if (Test-Path $TestUninstall) { return (Get-ItemProperty $TestUninstall).$name } return $null
}
function Lang { if (Test-Path $TestKey) { return (Get-ItemProperty $TestKey).Language } return $null }

# ---- 1. 새로 설치(한국어) ----
Write-Host "1. install 1.0.0 (korean)"
$rc = RunSetup $setup100 "korean" "install.log"
Check ($rc -eq 0) "exit code 0 (got $rc)"
foreach ($f in "bytesieve-gui.exe", "disk-scanner.exe", "LICENSE.txt", "README.txt", "unins000.exe",
             "terms\terms-ko.txt", "terms\terms-en.txt", "terms\terms-ja.txt", "terms\terms-zh-Hans.txt") {
    Check (Test-Path (Join-Path $app $f)) "installed $f"
}
# 설치한 LICENSE.txt 는 저장소의 LICENSE 원문과 바이트까지 같아야 한다(약관을 붙여도 원문은 그대로).
Check ((Get-FileHash (Join-Path $app "LICENSE.txt")).Hash -eq (Get-FileHash (Join-Path $stage "LICENSE.txt")).Hash) "LICENSE.txt is the original"
Check ((UninstallValue "DisplayVersion") -eq "1.0.0") "uninstall entry version 1.0.0"
Check ((Lang) -eq "ko") "language recorded as ko (got $(Lang))"
$ver = & (Join-Path $app "disk-scanner.exe") --version
Check ($ver -match "^disk-scanner ") "installed disk-scanner runs ($ver)"

# ---- 2. 업그레이드(영어 설치) — 사용자가 [설정]에서 고른 언어는 그대로 ----
Write-Host "2. upgrade to 1.0.1 (english), keep user language"
Set-ItemProperty $TestKey -Name Language -Value "ja"
$rc = RunSetup $setup101 "english" "upgrade.log"
Check ($rc -eq 0) "exit code 0 (got $rc)"
Check ((UninstallValue "DisplayVersion") -eq "1.0.1") "uninstall entry version 1.0.1"
Check ((Lang) -eq "ja") "user language kept (got $(Lang))"
Check (Test-Path (Join-Path $app "bytesieve-gui.exe")) "app still installed"
Check (-not (Test-Path (Join-Path $app "unins001.exe"))) "single uninstaller (same AppId)"

# ---- 3. 앱 실행 중이면 업그레이드하지 않음(AppMutex) ----
Write-Host "3. upgrade while the app is running is refused"
$fixture = Join-Path $WorkDir "fixture"
foreach ($d in "Temp", "Local", "Roaming", "Home", "history") { New-Item -ItemType Directory -Force -Path (Join-Path $fixture $d) | Out-Null }
$psi = New-Object Diagnostics.ProcessStartInfo (Join-Path $app "bytesieve-gui.exe")
$psi.UseShellExecute = $false
$psi.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
$psi.EnvironmentVariables["TEMP"] = Join-Path $fixture "Temp"
$psi.EnvironmentVariables["TMP"] = Join-Path $fixture "Temp"
$psi.EnvironmentVariables["LOCALAPPDATA"] = Join-Path $fixture "Local"
$psi.EnvironmentVariables["APPDATA"] = Join-Path $fixture "Roaming"
$psi.EnvironmentVariables["USERPROFILE"] = Join-Path $fixture "Home"
$psi.EnvironmentVariables["BYTESIEVE_HISTORY_DIR"] = Join-Path $fixture "history"
$psi.EnvironmentVariables["BYTESIEVE_QUARANTINE_ROOT"] = $fixture
$psi.EnvironmentVariables["BYTESIEVE_SETTINGS_KEY"] = "Software\ByteSieveTest\installer-app"
$gui = [Diagnostics.Process]::Start($psi)
Start-Sleep -Milliseconds 1500
try {
    $rc = RunSetup $setup100 "english" "running.log"
    Check ($rc -ne 0) "setup refused while running (exit $rc)"
    Check ((UninstallValue "DisplayVersion") -eq "1.0.1") "installed version unchanged"
} finally {
    if (-not $gui.HasExited) { $gui.Kill(); $gui.WaitForExit() }
    Remove-Item "HKCU:\Software\ByteSieveTest\installer-app" -Recurse -Force -ErrorAction SilentlyContinue
}

# ---- 4. 제거 — 프로그램 파일만 지우고 설정은 남김 ----
Write-Host "4. uninstall keeps user settings"
Start-Process (Join-Path $app "unins000.exe") -ArgumentList "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART" -Wait
# 제거 프로그램은 임시 폴더로 자신을 복사해 다시 실행하므로 끝날 때까지 기다린다.
for ($i = 0; $i -lt 60 -and ((Test-Path $TestUninstall) -or (Test-Path (Join-Path $app "unins000.exe"))); $i++) {
    Start-Sleep -Milliseconds 500
}
Check (-not (Test-Path $TestUninstall)) "uninstall entry removed"
Check (-not (Test-Path (Join-Path $app "bytesieve-gui.exe"))) "program files removed"
Check (-not (Test-Path (Join-Path $app "terms"))) "terms folder removed"
Check (-not (Test-Path (Join-Path $app "unins000.exe"))) "uninstaller removed"
Check ((Lang) -eq "ja") "language setting kept after uninstall (got $(Lang))"

# ---- 실제 위치는 그대로 ----
Write-Host "5. real install locations untouched"
$after = Snapshot
foreach ($prop in "ProgramDir", "SettingsKey", "Shortcuts", "ProdUninst", "Desktop") {
    Check ($before.$prop -eq $after.$prop) "$prop unchanged"
}

# ---- 정리 ----
if (Test-Path $TestKey) { Remove-Item $TestKey -Recurse -Force }
if ($failures -eq 0) {
    if (-not $KeepWork) { Remove-Item $WorkDir -Recurse -Force -ErrorAction SilentlyContinue }
    Write-Host "installer-test: ok"
    exit 0
}
Write-Host "installer-test: $failures failure(s). Logs: $WorkDir"
exit 1
