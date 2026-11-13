ByteSieve - Windows disk usage analyzer and cleaner
====================================================

[한국어]
ByteSieve는 폴더·드라이브의 디스크 사용량을 분석하고, 프로젝트 빌드 결과물·캐시·임시 파일을
안전하게 정리하는 Windows 프로그램입니다(Windows 10/11, 64비트).

- bytesieve-gui.exe : 프로그램(창). 왼쪽 사이드바에서 페이지를 고릅니다.
- disk-scanner.exe  : 명령줄 도구. "disk-scanner --help" 로 사용법을 봅니다.

안전 장치: 정리한 항목은 휴지통(휴지통이 없는 드라이브는 같은 드라이브의 격리 폴더 .bytesieve-trash)으로
옮기며, 영구 삭제는 직접 고르고 확인한 경우에만 합니다.

데이터 위치(설치본·포터블 모두 같음)
- 설정(테마·언어): HKCU\Software\ByteSieve
- 스캔 기록: %LOCALAPPDATA%\ByteSieve\history
- 격리 폴더: 각 드라이브의 \.bytesieve-trash
제거해도 위 데이터는 지우지 않습니다.

이 프로그램은 코드 서명이 없어 처음 실행할 때 Windows SmartScreen 경고가 나올 수 있습니다.
다운로드한 파일이 맞는지는 릴리스의 SHA256SUMS.txt 로 확인하세요.

이용약관: 이 폴더의 terms\terms-ko.txt (English: terms-en.txt, 日本語: terms-ja.txt, 简体中文: terms-zh-Hans.txt)
라이선스: PolyForm Noncommercial License 1.0.0 (LICENSE.txt). 약관과 라이선스 원문이 다르면 원문이 우선합니다.
일본어·중국어 약관은 검토가 필요한 번역 초안입니다.

[English]
ByteSieve analyzes disk usage of folders and drives and safely cleans up build outputs, caches and
temporary files (Windows 10/11, 64-bit).

- bytesieve-gui.exe : the app (window). Pick a page from the left sidebar.
- disk-scanner.exe  : command-line tool. Run "disk-scanner --help".

Safety: cleaned items go to the Recycle Bin (or, on drives without one, to the quarantine folder
.bytesieve-trash on the same drive). Permanent deletion only happens for items you pick and confirm.

Data locations (same for installer and portable): settings in HKCU\Software\ByteSieve,
scan history in %LOCALAPPDATA%\ByteSieve\history, quarantine in \.bytesieve-trash on each drive.
Uninstalling does not delete this data.

The binaries are not code-signed, so Windows SmartScreen may warn on first launch.
Verify downloads with SHA256SUMS.txt from the release page.

Terms of Use: terms\terms-en.txt in this folder (also terms-ko.txt, terms-ja.txt, terms-zh-Hans.txt;
the Japanese and Chinese versions are draft translations that need review).
License: PolyForm Noncommercial License 1.0.0 (see LICENSE.txt). If the Terms and the License differ, the License prevails.
Project: https://github.com/gajigaji-dev/ByteSieve
