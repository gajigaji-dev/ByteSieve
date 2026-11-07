# docs/screenshots 의 GUI 화면을 예제 데이터로 다시 찍는다(README·docs/gui.md 가 쓰는 그림).
#
#   powershell -ExecutionPolicy Bypass -File tools\screenshots.ps1          # build\Release 의 exe 로 촬영
#
# 개인 정보가 드러나지 않게:
#   - 예제 폴더(기본 C:\ByteSieveDemo)에 크기만 잡은 빈 파일과 지어낸 이름의 프로젝트·Git 저장소(작성자 "Demo")를 만든다.
#   - 사용자 폴더 환경 변수(TEMP·LOCALAPPDATA·APPDATA·USERPROFILE), 개발 도구 환경 변수, 설정 키(BYTESIEVE_SETTINGS_KEY),
#     기록 폴더(BYTESIEVE_HISTORY_DIR), 격리 루트(BYTESIEVE_QUARANTINE_ROOT), WSL 목록 키를 모두 예제 쪽으로 돌린다.
#   - 실제 휴지통 용량이 나오는 [시스템 정리]와 실제 프로세스·시작 프로그램이 나오는 [부스터]는 찍지 않는다.
#     [드라이브]·[모니터]는 이 PC 의 드라이브 용량·RAM/CPU 값만 보인다.
# 안전:
#   - 예제 폴더에 표식 파일(.bytesieve-demo)이 없으면(사용자 폴더일 수 있으므로) 지우지 않고 멈춘다.
#   - 정리는 누르지 않는다([모두 선택]까지만). 격리 폴더 화면용 항목은 CLI 로 예제 폴더 안에서만 옮긴다.
#   - 창은 활성화하지 않고(SW_SHOWNOACTIVATE) 화면 밖 좌표에 띄워 PrintWindow 로 찍는다. 화면 안에 띄우면 사용자가 다른
#     일을 하며 누른 마우스 클릭이 이 창에 들어가 페이지가 바뀔 수 있다(실제로 그래서 부스터 화면이 찍힌 적이 있다).
#   - 찍기 전에 페이지 제목이 기대한 페이지인지 확인하고, 아니면 저장하지 않고 멈춘다(실제 데이터 화면이 찍히지 않게).
param(
    [string]$Exe = "build\Release\bytesieve-gui.exe",
    [string]$Cli = "build\Release\disk-scanner.exe",
    [string]$Demo = "C:\ByteSieveDemo",
    [string]$Out = "docs\screenshots",
    [switch]$KeepDemo
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$Exe = (Resolve-Path $Exe).Path
$Cli = (Resolve-Path $Cli).Path
$Out = (Resolve-Path $Out).Path
$Marker = Join-Path $Demo ".bytesieve-demo"
$SettingsKey = "Software\ByteSieveTest\screenshots"

Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Text;
public static class Ui {
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    struct STARTUPINFO { public int cb; public string r, d, t; public int x,y,w,h,xc,yc,fill,flags; public short show, r2; public IntPtr r3, i, o, e; }
    [StructLayout(LayoutKind.Sequential)] struct PI { public IntPtr hp, ht; public int pid, tid; }
    [StructLayout(LayoutKind.Sequential)] struct RECT { public int l,t,r,b; }
    delegate bool EnumProc(IntPtr h, IntPtr lp);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern bool CreateProcessW(string app, StringBuilder cmd, IntPtr pa, IntPtr ta, bool inh, int flags, IntPtr env, string dir, ref STARTUPINFO si, out PI pi);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr h, IntPtr dc, int f);
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr FindWindowExW(IntPtr p, IntPtr a, string c, string w);
    [DllImport("user32.dll")] static extern int GetWindowThreadProcessId(IntPtr h, out int pid);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr h, EnumProc f, IntPtr lp);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr SendMessageW(IntPtr h, int m, IntPtr w, string l);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, int m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, int m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    public static int Start(string cmd) {
        STARTUPINFO si = new STARTUPINFO(); si.cb = Marshal.SizeOf(si);
        /* STARTF_USESHOWWINDOW | STARTF_USEPOSITION: 활성화하지 않고(SW_SHOWNOACTIVATE) 화면 밖에(마우스 클릭이 닿지 않게). */
        si.flags = 1 | 4; si.show = 4; si.x = 20000; si.y = 20000;
        PI pi;
        if (!CreateProcessW(null, new StringBuilder(cmd), IntPtr.Zero, IntPtr.Zero, false, 0, IntPtr.Zero, null, ref si, out pi))
            throw new Exception("CreateProcess " + Marshal.GetLastWin32Error());
        return pi.pid;
    }
    public static IntPtr MainOf(int pid) {
        IntPtr h = IntPtr.Zero;
        while ((h = FindWindowExW(IntPtr.Zero, h, "ByteSieveMain", null)) != IntPtr.Zero) {
            int p; GetWindowThreadProcessId(h, out p); if (p == pid) return h;
        }
        return IntPtr.Zero;
    }
    public static string Cls(IntPtr h) { StringBuilder s = new StringBuilder(64); GetClassNameW(h, s, 64); return s.ToString(); }
    public static string Text(IntPtr h) { StringBuilder s = new StringBuilder(512); GetWindowTextW(h, s, 512); return s.ToString(); }
    /* root 아래(자손 포함)에서 클래스가 cls 이고 (text 가 null 이 아니면) 글자가 text 인 index 번째 창. */
    public static IntPtr Find(IntPtr root, string cls, string text, int index) {
        List<IntPtr> found = new List<IntPtr>();
        EnumChildWindows(root, (h, lp) => {
            if (string.Equals(Cls(h), cls, StringComparison.OrdinalIgnoreCase) && (text == null || Text(h) == text)) found.Add(h);
            return true;
        }, IntPtr.Zero);
        return index < found.Count ? found[index] : IntPtr.Zero;
    }
    public static void SetText(IntPtr h, string text) { SendMessageW(h, 0x000C /* WM_SETTEXT */, IntPtr.Zero, text); }
    public static void Capture(IntPtr h, string file) {
        RECT r; GetWindowRect(h, out r);
        using (Bitmap b = new Bitmap(r.r - r.l, r.b - r.t)) {
            using (Graphics g = Graphics.FromImage(b)) { IntPtr dc = g.GetHdc(); PrintWindow(h, dc, 2); g.ReleaseHdc(dc); }
            b.Save(file, ImageFormat.Png);
        }
    }
}
"@
[Ui]::SetProcessDPIAware() | Out-Null

# ---- 예제 데이터 ----
function New-DemoFile([string]$rel, [long]$bytes, [int]$daysAgo = 0) {
    $path = Join-Path $Demo $rel
    New-Item -ItemType Directory -Force -Path (Split-Path $path) | Out-Null
    $fs = [IO.File]::Create($path); $fs.SetLength($bytes); $fs.Close()
    if ($daysAgo -gt 0) { $t = (Get-Date).AddDays(-$daysAgo); (Get-Item $path).CreationTime = $t; (Get-Item $path).LastWriteTime = $t }
}
function Set-Old([string]$rel, [int]$daysAgo) {
    $i = Get-Item (Join-Path $Demo $rel) -Force; $t = (Get-Date).AddDays(-$daysAgo); $i.CreationTime = $t; $i.LastWriteTime = $t
}
function New-DemoRepo([string]$rel, [string]$branch, [string]$date, [string[]]$files) {
    $dir = Join-Path $Demo $rel
    $env:GIT_AUTHOR_NAME = "Demo"; $env:GIT_AUTHOR_EMAIL = "demo@example.com"
    $env:GIT_COMMITTER_NAME = "Demo"; $env:GIT_COMMITTER_EMAIL = "demo@example.com"
    $env:GIT_AUTHOR_DATE = $date; $env:GIT_COMMITTER_DATE = $date
    git -C $dir -c init.defaultBranch=$branch init -q
    git -C $dir add -- $files
    git -C $dir -c commit.gpgsign=false commit -q -m "Initial import"
    foreach ($v in "GIT_AUTHOR_NAME", "GIT_AUTHOR_EMAIL", "GIT_COMMITTER_NAME", "GIT_COMMITTER_EMAIL", "GIT_AUTHOR_DATE", "GIT_COMMITTER_DATE") {
        Remove-Item "Env:$v" -ErrorAction SilentlyContinue
    }
}

if (Test-Path $Demo) {
    if (-not (Test-Path $Marker)) { throw "$Demo exists but is not a ByteSieve demo folder (no .bytesieve-demo). Not touching it." }
    Remove-Item $Demo -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $Demo | Out-Null
Set-Content -Path $Marker -Value "ByteSieve screenshot demo data (tools/screenshots.ps1). Safe to delete." -Encoding ASCII
$MB = 1MB

# 스캔·프로젝트 정리 대상(Files)
$P = "Files\Projects"
New-DemoFile "$P\web-shop\package.json" 1200
New-DemoFile "$P\web-shop\package-lock.json" 180000
New-DemoFile "$P\web-shop\src\app.tsx" 42000
New-DemoFile "$P\web-shop\src\cart.tsx" 18000
New-DemoFile "$P\web-shop\node_modules\react-dom\cjs\react-dom.development.js" (14 * $MB)
New-DemoFile "$P\web-shop\node_modules\typescript\lib\typescript.js" (22 * $MB)
New-DemoFile "$P\web-shop\node_modules\lodash\lodash.js" (5 * $MB)
New-DemoFile "$P\web-shop\node_modules\.package-lock.json" 90000
New-DemoFile "$P\web-shop\dist\assets\index.js" (3 * $MB)
New-DemoFile "$P\web-shop\npm-debug.log" 220000
Set-Content "$Demo\$P\web-shop\.gitignore" "node_modules/`ndist/`n*.log" -Encoding ASCII
New-DemoRepo "$P\web-shop" "main" "2026-11-03T10:00:00+09:00" @("package.json", "package-lock.json", "src", ".gitignore")
New-DemoFile "$P\web-shop\src\checkout.tsx" 9000   # 커밋하지 않은 변경

New-DemoFile "$P\photo-tagger\requirements.txt" 300
New-DemoFile "$P\photo-tagger\src\tagger.py" 24000
New-DemoFile "$P\photo-tagger\src\__pycache__\tagger.cpython-312.pyc" 410000
New-DemoFile "$P\photo-tagger\.pytest_cache\v\cache\nodeids" 4000
New-DemoFile "$P\photo-tagger\.venv\Lib\site-packages\numpy\_core\_multiarray_umath.pyd" (28 * $MB)
New-DemoFile "$P\photo-tagger\.venv\pyvenv.cfg" 200
Set-Content "$Demo\$P\photo-tagger\.gitignore" "__pycache__/`n.venv/`n.pytest_cache/" -Encoding ASCII
New-DemoRepo "$P\photo-tagger" "main" "2025-03-14T21:00:00+09:00" @("requirements.txt", "src\tagger.py", ".gitignore")

New-DemoFile "$P\game-engine\CMakeLists.txt" 3000
New-DemoFile "$P\game-engine\src\renderer.cpp" 61000
New-DemoFile "$P\game-engine\build\Release\engine.exe" (19 * $MB)
New-DemoFile "$P\game-engine\build\Release\engine.pdb" (34 * $MB)
New-DemoFile "$P\game-engine\build\CMakeCache.txt" 40000
Set-Content "$Demo\$P\game-engine\.gitignore" "build/" -Encoding ASCII
New-DemoRepo "$P\game-engine" "feature/renderer" "2026-10-28T19:30:00+09:00" @("CMakeLists.txt", "src", ".gitignore")

New-DemoFile "$P\android-notes\build.gradle" 2000
New-DemoFile "$P\android-notes\settings.gradle" 300
New-DemoFile "$P\android-notes\app\src\main\AndroidManifest.xml" 1500
New-DemoFile "$P\android-notes\app\build\outputs\apk\debug\app-debug.apk" (31 * $MB)
New-DemoFile "$P\android-notes\.gradle\8.5\executionHistory\executionHistory.bin" (4 * $MB)

New-DemoFile "Files\Media\family-trip.mp4" (140 * $MB)
New-DemoFile "Files\Media\graduation.mov" (85 * $MB)
foreach ($n in 1..12) { New-DemoFile ("Files\Media\Photos\2024\IMG_{0:D4}.jpg" -f (1000 + $n)) ((3 + ($n % 4)) * $MB) }
New-DemoFile "Files\Documents\thesis-draft.docx" (2 * $MB)
New-DemoFile "Files\Documents\budget-2026.xlsx" 380000
New-DemoFile "Files\Documents\old-notes.txt" 60000
New-DemoFile "Files\Archive\backup-2023.zip" (72 * $MB)

# 사용자 폴더(시스템 정리·개발 캐시가 보는 곳)
$H = "Profile\Home"; $L = "Profile\Local"
New-Item -ItemType Directory -Force -Path "$Demo\Profile\Roaming", "$Demo\Profile\Temp", "$Demo\History" | Out-Null
New-DemoFile "$L\npm-cache\_cacache\content-v2\sha512\ab\cdef01" (46 * $MB)
New-DemoFile "$L\npm-cache\_cacache\index-v5\12\3456" 80000
New-DemoFile "$L\pip\Cache\http-v2\a\b\c\0123" (12 * $MB)
New-DemoFile "$H\.gradle\caches\modules-2\files-2.1\com.google.guava\guava.jar" (31 * $MB)
New-DemoFile "$H\.gradle\wrapper\dists\gradle-8.5-bin\5t9huq95ubn472n8rpzujfbqh\gradle-8.5-bin.zip" (24 * $MB)
New-DemoFile "$H\.gradle\gradle.properties" 200
New-DemoFile "$H\.cargo\registry\cache\index.crates.io-6f17d22bba15001f\serde-1.0.210.crate" (9 * $MB)
New-DemoFile "$H\.nuget\packages\newtonsoft.json\13.0.3\.nupkg.metadata" 300
New-DemoFile "$H\.nuget\packages\newtonsoft.json\13.0.3\lib\net6.0\Newtonsoft.Json.dll" (6 * $MB)
New-DemoFile "$L\Docker\wsl\disk\docker_data.vhdx" (64 * $MB)

# 개발 캐시 증감: 7일 전 기록(지금보다 작음)
$ft = [DateTime]::UtcNow.AddDays(-7).ToFileTimeUtc()
$hist = "# bytesieve devenv history v1`n$ft`tnpm`t$(30 * $MB)`t$Demo\$L\npm-cache`n$ft`tgradle`t$(22 * $MB)`t$Demo\$H\.gradle\caches`n"
[IO.File]::WriteAllText("$Demo\History\devenv-history.tsv", $hist, (New-Object Text.UTF8Encoding $false))

# ---- 환경: 모두 예제 쪽으로 ----
$env:TEMP = "$Demo\Profile\Temp"; $env:TMP = $env:TEMP
$env:LOCALAPPDATA = "$Demo\$L"; $env:APPDATA = "$Demo\Profile\Roaming"; $env:USERPROFILE = "$Demo\$H"
$env:BYTESIEVE_HISTORY_DIR = "$Demo\History"
$env:BYTESIEVE_QUARANTINE_ROOT = $Demo
$env:BYTESIEVE_SETTINGS_KEY = $SettingsKey
$env:BYTESIEVE_WSL_KEY = "Software\ByteSieveTest\screenshots-no-wsl"
foreach ($v in "BYTESIEVE_LANG", "npm_config_cache", "NPM_CONFIG_USERCONFIG", "PIP_CACHE_DIR", "PIP_CONFIG_FILE",
               "GRADLE_USER_HOME", "CARGO_HOME", "NUGET_PACKAGES", "NUGET_HTTP_CACHE_PATH", "npm_config_store_dir",
               "YARN_CACHE_FOLDER", "YARN_GLOBAL_FOLDER", "GOMODCACHE", "GOPATH", "GOCACHE", "GOENV", "CONDA_PKGS_DIRS") {
    Remove-Item "Env:$v" -ErrorAction SilentlyContinue
}

function Set-DemoTheme([string]$theme) {
    New-Item -Path "HKCU:\$SettingsKey" -Force | Out-Null
    Set-ItemProperty -Path "HKCU:\$SettingsKey" -Name Theme -Value $theme
    Set-ItemProperty -Path "HKCU:\$SettingsKey" -Name Language -Value "ko"
}

# 기준 스캔(변화 열·변화 요약을 보이려고): CLI --track 으로 기준을 남긴 뒤 파일을 바꾼다.
& $Cli --track --history-dir "$Demo\History" "$Demo\Files" | Out-Null
New-DemoFile "Files\Media\drone-footage.mp4" (96 * $MB)
Remove-Item "$Demo\Files\Documents\old-notes.txt"
New-DemoFile "Files\Archive\backup-2023.zip" (88 * $MB)

# ---- 촬영 ----
$NavUp = 0x26; $NavDown = 0x28; $NavHome = 0x24; $NavEnd = 0x23
$script:Opened = @()   # 띄운 창(도중에 멈춰도 finally 에서 닫는다)
function Open-Gui([string]$arg) {
    $cmd = if ($arg) { "`"$Exe`" `"$arg`"" } else { "`"$Exe`"" }
    $procId = [Ui]::Start($cmd)
    $script:Opened += $procId
    $h = [IntPtr]::Zero
    for ($i = 0; $i -lt 100 -and $h -eq [IntPtr]::Zero; $i++) { Start-Sleep -Milliseconds 100; $h = [Ui]::MainOf($procId) }
    if ($h -eq [IntPtr]::Zero) { throw "window not found" }
    return [pscustomobject]@{ Pid = $procId; Hwnd = $h; Nav = [Ui]::Find($h, "ByteSieveNav", $null, 0);
                              Title = [Ui]::GetDlgItem($h, 101) }   # 페이지 제목(src/gui_main.c IDC_TITLE)
}
# 페이지 제목이 expect 가 될 때까지 기다린다(최대 5초). 아니면 멈춘다.
function Wait-Page($g, [string]$expect) {
    for ($i = 0; $i -lt 50; $i++) {
        if ([Ui]::Text($g.Title) -eq $expect) { return }
        Start-Sleep -Milliseconds 100
    }
    throw "expected page '$expect' but the window shows '$([Ui]::Text($g.Title))'. Not capturing."
}
function Go($g, [int]$page) {
    [Ui]::PostMessageW($g.Nav, 0x0100, [IntPtr]$NavHome, [IntPtr]::Zero) | Out-Null
    for ($i = 0; $i -lt $page; $i++) { [Ui]::PostMessageW($g.Nav, 0x0100, [IntPtr]$NavDown, [IntPtr]::Zero) | Out-Null }
}
function Shot($g, [string]$name, [string]$expect) {
    Wait-Page $g $expect   # 다른 페이지(특히 실제 데이터가 나오는 부스터·시스템 정리)를 찍지 않게 확인
    [Ui]::Capture($g.Hwnd, (Join-Path $Out $name))
    if ([Ui]::Text($g.Title) -ne $expect) { Remove-Item (Join-Path $Out $name); throw "page changed while capturing $name" }
    Write-Host "  $name"
}
function Close-Gui($g) {
    [Ui]::PostMessageW($g.Hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 800
    Get-Process -Id $g.Pid -ErrorAction SilentlyContinue | Stop-Process -Force
}
# 페이지 번호(사이드바 순서): 0 폴더 스캔, 1 드라이브, 2 프로젝트 정리, 3 시스템 정리, 4 개발 캐시, 5 격리 폴더, 6 모니터, 7 부스터, 8 설정

try {
    Write-Host "light theme:"
    Set-DemoTheme "light"
    $g = Open-Gui "$Demo\Files"
    Start-Sleep -Seconds 3
    Shot $g "scan.png" "폴더 스캔"

    # 트리맵 보기: 폴더 스캔 패널의 보기 콤보 상자(하나뿐)에서 마지막 항목
    $panel = [Ui]::Find($g.Hwnd, "ByteSieveScanPanel", $null, 0)
    $combo = [Ui]::Find($panel, "ComboBox", $null, 0)
    $count = [int][Ui]::SendMessageW($combo, 0x0146 <# CB_GETCOUNT #>, [IntPtr]::Zero, [IntPtr]::Zero)
    [Ui]::SendMessageW($combo, 0x014E <# CB_SETCURSEL #>, [IntPtr]($count - 1), [IntPtr]::Zero) | Out-Null
    $wp = ([int]1 -shl 16) -bor [Ui]::GetDlgCtrlID($combo)   # MAKEWPARAM(id, CBN_SELCHANGE)
    [Ui]::SendMessageW($panel, 0x0111, [IntPtr]$wp, $combo) | Out-Null
    Start-Sleep -Milliseconds 800
    Shot $g "treemap.png" "폴더 스캔"

    Go $g 1; Wait-Page $g "드라이브"; Start-Sleep -Seconds 2; Shot $g "drives.png" "드라이브"

    # 프로젝트 정리: 경로를 넣고 Enter(탐지) → [모두 선택](정리는 누르지 않음)
    Go $g 2; Wait-Page $g "프로젝트 정리"
    $cpanel = [Ui]::Find($g.Hwnd, "ByteSieveCleanupPanel", $null, 0)
    [Ui]::SetText([Ui]::Find($cpanel, "Edit", $null, 0), "$Demo\Files\Projects")
    [Ui]::PostMessageW($g.Hwnd, 0x0111, [IntPtr]1 <# IDOK #>, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Seconds 6
    $checkAll = [Ui]::Find($cpanel, "Button", "모두 선택", 0)
    if ($checkAll -eq [IntPtr]::Zero) { throw "'모두 선택' button not found" }
    [Ui]::SendMessageW($checkAll, 0x00F5 <# BM_CLICK #>, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 500
    Shot $g "cleanup.png" "프로젝트 정리"

    Go $g 4; Wait-Page $g "개발 캐시"; Start-Sleep -Seconds 5; Shot $g "devcache.png" "개발 캐시"
    Go $g 6; Wait-Page $g "모니터"; Start-Sleep -Seconds 6; Shot $g "monitor.png" "모니터"
    [Ui]::PostMessageW($g.Nav, 0x0100, [IntPtr]$NavEnd, [IntPtr]::Zero) | Out-Null   # 설정(부스터를 거치지 않게 End)
    Start-Sleep -Milliseconds 800; Shot $g "settings.png" "설정"
    Close-Gui $g

    # 격리 폴더: 빌드 결과물을 예제 폴더 안의 격리 폴더로 옮기고(CLI, BYTESIEVE_QUARANTINE_ROOT), 하나는 기록만 남긴다.
    $env:BYTESIEVE_GIT = "none"
    & $Cli clean project "$Demo\Files\Projects" --category build --execute --yes | Out-Null
    Remove-Item Env:BYTESIEVE_GIT
    $entry = Get-ChildItem "$Demo\.bytesieve-trash" -Directory | Sort-Object Name | Select-Object -First 1
    if ($entry) { Get-ChildItem $entry.FullName -Directory | Remove-Item -Recurse -Force }
    $g = Open-Gui ""
    Go $g 5; Wait-Page $g "격리 폴더"; Start-Sleep -Seconds 2; Shot $g "quarantine.png" "격리 폴더"
    Close-Gui $g

    Write-Host "dark theme:"
    Set-DemoTheme "dark"
    $g = Open-Gui "$Demo\Files"
    Start-Sleep -Seconds 3
    Shot $g "scan-dark.png" "폴더 스캔"
    [Ui]::PostMessageW($g.Nav, 0x0100, [IntPtr]$NavEnd, [IntPtr]::Zero) | Out-Null
    Start-Sleep -Milliseconds 800; Shot $g "settings-dark.png" "설정"
    Close-Gui $g
} finally {
    # 멈춘 경우에도 화면 밖에 띄운 창을 남기지 않는다(남으면 뮤텍스를 잡아 설치 검증 등을 방해한다).
    foreach ($id in $script:Opened) { Get-Process -Id $id -ErrorAction SilentlyContinue | Stop-Process -Force }
    Remove-Item "HKCU:\$SettingsKey" -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item "HKCU:\Software\ByteSieveTest\screenshots-no-wsl" -Recurse -Force -ErrorAction SilentlyContinue
    if (-not $KeepDemo -and (Test-Path $Marker)) { Remove-Item $Demo -Recurse -Force }
}
Write-Host "screenshots: done"
