# ByteSieve 릴리스 패키지를 만든다: Release 빌드 → 테스트 → 설치 파일 모으기(cmake --install)
# → 포터블 ZIP → 설치 프로그램(Inno Setup) → SHA256SUMS.txt. 로컬과 CI(.github/workflows/release.yml)가 같이 쓴다.
#
#   powershell -ExecutionPolicy Bypass -File tools\package.ps1                 # 전체
#   powershell -ExecutionPolicy Bypass -File tools\package.ps1 -SkipTests      # 테스트 생략
#   powershell -ExecutionPolicy Bypass -File tools\package.ps1 -NoInstaller    # ZIP 만(Inno Setup 없을 때)
#
# 결과: dist\ByteSieve-<버전>-setup-x64.exe, dist\ByteSieve-<버전>-portable-x64.zip, dist\SHA256SUMS.txt
# 버전은 CMakeLists.txt 의 project(ByteSieve VERSION ...) 하나에서 온다. 태그 빌드(GITHUB_REF_NAME=v*)면
# 태그와 버전이 다를 때 실패한다(잘못된 이름으로 릴리스하지 않게).
param(
    [string]$BuildDir = "build",
    [string]$DistDir = "dist",
    [string]$Iscc = "",
    [switch]$SkipTests,
    [switch]$NoInstaller
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

function Run([string]$exe, [string[]]$argv) {
    & $exe @argv
    if ($LASTEXITCODE -ne 0) { throw "$exe failed ($LASTEXITCODE): $($argv -join ' ')" }
}

# 1. Release 빌드
Run "cmake" @("-S", ".", "-B", $BuildDir)
Run "cmake" @("--build", $BuildDir, "--config", "Release")

# 2. 테스트(배포하는 Release 구성으로)
if (-not $SkipTests) {
    Run "ctest" @("--test-dir", $BuildDir, "-C", "Release", "--output-on-failure")
}

# 3. 버전
$header = Get-Content (Join-Path $BuildDir "generated\version.h") -Raw
if ($header -notmatch 'BYTESIEVE_VERSION_STRING "([0-9]+\.[0-9]+\.[0-9]+)"') { throw "version not found in version.h" }
$version = $Matches[1]
if ($env:GITHUB_REF_NAME -and $env:GITHUB_REF_NAME.StartsWith("v") -and $env:GITHUB_REF_NAME -ne "v$version") {
    throw "tag $($env:GITHUB_REF_NAME) does not match CMake project version $version"
}
Write-Host "ByteSieve $version"

# 4. 배포 파일 모으기
$dist = Join-Path $root $DistDir
$stage = Join-Path $dist "stage"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Run "cmake" @("--install", $BuildDir, "--config", "Release", "--prefix", $stage)
foreach ($f in "bytesieve-gui.exe", "disk-scanner.exe", "LICENSE.txt", "README.txt") {
    if (-not (Test-Path (Join-Path $stage $f))) { throw "missing $f in stage" }
}
$exeVersion = (Get-Item (Join-Path $stage "bytesieve-gui.exe")).VersionInfo.ProductVersion
if ($exeVersion -ne $version) { throw "exe version $exeVersion does not match $version" }

# 5. 포터블 ZIP(폴더 하나에 담아 풀었을 때 흩어지지 않게)
$portableName = "ByteSieve-$version-portable-x64"
$zip = Join-Path $dist "$portableName.zip"
$zipRoot = Join-Path $dist $portableName
if (Test-Path $zipRoot) { Remove-Item $zipRoot -Recurse -Force }
if (Test-Path $zip) { Remove-Item $zip -Force }
Copy-Item $stage $zipRoot -Recurse
# PowerShell 5.1(.NET Framework)의 Compress-Archive·CreateFromDirectory 는 항목 이름에 '\'를 써서 일부 압축 프로그램이
# 폴더를 풀지 못한다. 항목 이름을 '/'로 직접 정해 만든다(ZIP 규격).
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::Open($zip, [IO.Compression.ZipArchiveMode]::Create)
try {
    Get-ChildItem $zipRoot -Recurse -File | ForEach-Object {
        $rel = $_.FullName.Substring($zipRoot.Length + 1).Replace('\', '/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $_.FullName, "$portableName/$rel",
                                                               [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally {
    $archive.Dispose()
}
Remove-Item $zipRoot -Recurse -Force
$artifacts = @($zip)

# 6. 설치 프로그램
if (-not $NoInstaller) {
    if (-not $Iscc) {
        $cands = @((Get-Command iscc -ErrorAction SilentlyContinue).Source,
                   "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
                   "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
                   "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe")
        $Iscc = $cands | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
    }
    if (-not $Iscc) { throw "Inno Setup (ISCC.exe) not found. Install Inno Setup 6 or use -NoInstaller." }
    Run $Iscc @("/Q", "/DAppVersion=$version", "/DStageDir=$stage", "/DOutputDir=$dist",
                (Join-Path $root "installer\bytesieve.iss"))
    $setup = Join-Path $dist "ByteSieve-$version-setup-x64.exe"
    if (-not (Test-Path $setup)) { throw "installer was not created: $setup" }
    $artifacts = @($setup) + $artifacts
}

# 7. 릴리스 노트: CHANGELOG.md 의 "## [<버전>]" 구역(다음 "## [" 전까지). 태그 빌드인데 없으면 실패한다.
$changelog = Get-Content (Join-Path $root "CHANGELOG.md") -Encoding UTF8
$start = -1
for ($i = 0; $i -lt $changelog.Count; $i++) {
    if ($changelog[$i] -match "^## \[$([regex]::Escape($version))\]") { $start = $i; break }
}
if ($start -ge 0) {
    $end = $changelog.Count
    for ($i = $start + 1; $i -lt $changelog.Count; $i++) {
        if ($changelog[$i] -match '^## \[') { $end = $i; break }
    }
    $notes = $changelog[($start + 1)..($end - 1)]
    [IO.File]::WriteAllLines((Join-Path $dist "RELEASE_NOTES.md"), [string[]]$notes, (New-Object Text.UTF8Encoding $false))
} elseif ($env:GITHUB_REF_NAME -and $env:GITHUB_REF_NAME.StartsWith("v")) {
    throw "CHANGELOG.md has no section for $version"
} else {
    Write-Warning "CHANGELOG.md has no section for $version (release notes not written)"
}

# 8. SHA256(서명이 없으므로 사용자가 내려받은 파일을 확인할 수 있게)
$sums = foreach ($a in $artifacts) {
    "{0}  {1}" -f (Get-FileHash $a -Algorithm SHA256).Hash.ToLowerInvariant(), (Split-Path $a -Leaf)
}
$sumFile = Join-Path $dist "SHA256SUMS.txt"
[IO.File]::WriteAllLines($sumFile, [string[]]$sums)
$artifacts += $sumFile

Write-Host ""
Write-Host "Artifacts:"
$artifacts | ForEach-Object { Write-Host ("  {0}  ({1:N0} KB)" -f $_, ((Get-Item $_).Length / 1KB)) }
