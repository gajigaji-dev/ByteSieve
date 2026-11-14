# ByteSieve에 기여하기

ByteSieve에 관심을 가져 주셔서 고맙습니다. 버그 신고, 기능 제안, 번역 개선, 문서 수정, 코드 기여를 모두 환영합니다.
처음이라면 [처음 오셨나요?](#처음-오셨나요)와 [기여할 거리](#기여할-거리)부터 보세요.

이 프로젝트에 참여하는 모든 사람은 [행동 강령](CODE_OF_CONDUCT.md)을 지킵니다.

> English speakers: see [In English](#in-english) at the end. Issues and pull requests in English are welcome.

## 목차

- [처음 오셨나요?](#처음-오셨나요)
- [Issue 등록](#issue-등록)
- [Label](#label)
- [브랜치 전략](#브랜치-전략)
- [커밋 메시지](#커밋-메시지)
- [Pull Request](#pull-request)
- [개발 환경과 빌드·테스트](#개발-환경과-빌드테스트)
- [코드 규칙](#코드-규칙)
- [정리(삭제) 기능의 안전 원칙](#정리삭제-기능의-안전-원칙)
- [번역](#번역)
- [문서와 테스트 개수](#문서와-테스트-개수)
- [기여할 거리](#기여할-거리)
- [라이선스](#라이선스)

## 처음 오셨나요?

ByteSieve는 Windows용 디스크 사용량 분석·정리 도구입니다. C11과 Win32 API만으로 작성했고 외부 라이브러리가 없습니다.
CLI(`disk-scanner`)와 GUI(`bytesieve-gui`)가 같은 core 코드를 씁니다.

| 알고 싶은 것 | 문서 |
|---|---|
| 무엇을 하는 프로그램인지, 설치·사용법 | [README.md](README.md) |
| 코드 구조(core·GUI·CLI, 스캔·정리 흐름) | [docs/architecture.md](docs/architecture.md) |
| 정리 규칙과 안전 장치 | [docs/cleanup.md](docs/cleanup.md) |
| 테스트 목록과 GUI 자체 검사 | [docs/testing.md](docs/testing.md) |
| 화면·다국어 규칙 | [docs/ui-design.md](docs/ui-design.md) |
| 성능 측정 방법과 기록 | [docs/performance.md](docs/performance.md) |
| Windows 종속 코드와 다른 플랫폼 확장 방향 | [docs/platform.md](docs/platform.md) |
| 배포 절차 | [docs/release.md](docs/release.md) |

지금은 **Windows 전용**입니다. macOS·Linux는 공식 지원하지 않으며 배포 파일도 제공하지 않습니다. macOS·Linux 지원은 아직 계획 단계이며, 관련 기여는 [docs/platform.md](docs/platform.md)의
방향을 따르고 Windows 동작을 바꾸지 않아야 합니다.

## Issue 등록

[Issues](https://github.com/gajigaji-dev/ByteSieve/issues)에서 비슷한 Issue가 이미 있는지 찾아본 뒤
[새 Issue](https://github.com/gajigaji-dev/ByteSieve/issues/new/choose)에서 양식을 고릅니다.

| 양식 | 쓰는 경우 |
|---|---|
| 버그 신고 | 잘못 동작하거나 오류가 나는 문제 |
| 기능 제안 | 새 기능이나 개선 |
| 번역 오류·누락 | 고른 표시 언어로 나오지 않거나 잘못 번역된 문구 |
| 작업 | 릴리스·문서·저장소 관리 등(주로 관리자용) |

버그 신고에는 다음을 적어 주세요. 양식에 같은 칸이 있습니다.

- ByteSieve 버전: GUI [설정] 페이지의 버전 정보 또는 `disk-scanner --version`, 설치 프로그램인지 포터블인지
- Windows 버전(예: Windows 11 24H2)과 표시 언어
- 재현 순서, 기대한 결과와 실제 결과, 오류 문구
- 스크린샷이나 로그. **사용자 이름·폴더 이름 같은 개인정보는 가려 주세요.**

파일이 잘못 옮겨지거나 지워진 것 같다면 어느 페이지에서 정리했는지, 항목의 경로, 휴지통이나 그 드라이브의 격리 폴더
(`.bytesieve-trash`)에 남아 있는지를 함께 알려 주세요. 격리 폴더 항목은 [격리 폴더] 페이지에서 복원할 수 있습니다.

보안 문제(다른 사람의 파일을 지우게 만드는 방법 등)는 공개 Issue에 자세한 재현 방법을 쓰지 말고
README [라이선스](README.md#라이선스)에 있는 관리자 이메일로 먼저 알려 주세요.

## Label

Issue·PR에는 종류 Label 하나와, 해당하면 영역 Label을 붙입니다. 양식으로 만든 Issue에는 종류 Label과 `needs triage`가
자동으로 붙고, 관리자가 확인한 뒤 `needs triage`를 떼고 영역 Label을 붙입니다. 전체 목록과 색은 [.github/labels.yml](.github/labels.yml)에 있습니다.

| 분류 | Label |
|---|---|
| 종류 | `bug`, `enhancement`, `documentation`, `question` |
| 영역 | `scan`(폴더 스캔·기록), `cleanup`(정리·격리 폴더), `gui`, `cli`, `i18n`(번역), `installer`(설치·배포 파일), `release`, `performance`, `platform`(Windows 종속성·다른 플랫폼 준비), `accessibility` |
| 주의 | `safety`: 사용자 파일을 잘못 옮기거나 지울 수 있는 문제. 가장 먼저 봅니다 |
| 상태 | `needs triage`(아직 확인 전), `duplicate`, `invalid`, `wontfix` |
| 기여자용 | `good first issue`(처음 기여하기 좋은 작은 일), `help wanted`(도움이 필요한 일) |

## 브랜치 전략

개인 프로젝트이면서 외부 기여도 받을 수 있도록 단순하게 운영합니다.

- **`main`**: 항상 빌드·테스트가 통과하는 안정 브랜치이자 릴리스 기준입니다. 직접 push하지 않고 PR로만 바꿉니다.
  릴리스는 `main`에 `v<버전>` 태그를 붙여 만듭니다([docs/release.md](docs/release.md)).
- **작업 브랜치**: `main`에서 만들고, 끝나면 `main`으로 PR을 엽니다. 이름은 `종류/짧은-설명`(영어 소문자, 단어는 `-`로 연결)입니다.

  | 접두사 | 용도 | 예 |
  |---|---|---|
  | `feat/` | 새 기능 | `feat/cleanup-completion-ui` |
  | `fix/` | 버그 수정(번역 누락, 급한 수정 포함) | `fix/select-all` |
  | `docs/` | 문서만 바꿈 | `docs/readme-contributing-release` |
  | `refactor/` | 동작은 그대로 두고 구조만 바꿈 | `refactor/gui-core-integration` |
  | `perf/` | 성능 개선(측정 근거 포함) | `perf/gui-and-scan-optimization` |
  | `test/` | 테스트만 더하거나 고침 | |
  | `chore/` | 빌드·배포·저장소 관리, 릴리스 준비 | `chore/release-1.0.0` |

- 릴리스 준비(버전·CHANGELOG)는 `chore/release-<버전>` 브랜치에서 하고 `main`에 병합한 뒤 태그를 붙입니다.
- 급한 수정도 따로 `hotfix/`를 두지 않고 `fix/` 브랜치로 `main`에 PR을 엽니다.
- `dev`는 1.0.0 이전에 쓰던 통합 브랜치입니다. 지금은 새 작업을 `dev`로 보내지 않습니다.
- 외부 기여자는 저장소를 Fork해 같은 규칙으로 브랜치를 만들고, 원래 저장소의 `main`으로 PR을 엽니다.
- 병합한 작업 브랜치는 지워도 됩니다.

## 커밋 메시지

```
종류: 요약

(선택) 무엇을 왜 바꿨는지. 줄마다 하나씩 "- " 로 써도 됩니다.
```

- **종류**는 브랜치 접두사와 같은 낱말(`feat`, `fix`, `docs`, `refactor`, `perf`, `test`, `chore`)을 씁니다.
- **요약**은 한국어로, 무엇을 했는지 한 줄(대략 50자 안팎)로 씁니다. 끝에 마침표를 찍지 않습니다.
- 커밋 하나에는 한 가지 변경만 담습니다. 빌드가 깨진 중간 상태를 커밋하지 않도록 합니다.

```
fix: 격리 폴더 페이지 번역 및 언어 즉시 적용
feat: Git 기반 프로젝트 분석
perf: 비교·저장의 UI 멈춤 및 직전 기록 읽기 지연
```

## Pull Request

- **제목**: 커밋과 같은 `종류: 요약` 형식(한국어)입니다. 예: `fix: 모두 선택 오류 수정`
- **본문**: PR 양식의 구역을 채웁니다.
  - 무엇을 왜 바꿨는지, 관련 Issue(`Closes #번호` 또는 `Refs #번호`)
  - 어떻게 확인했는지(실행한 테스트, 직접 확인한 화면). 화면이 바뀌면 스크린샷(개인정보는 가림)
  - 확인 목록(경고 없음, 테스트 통과, 번역, 문서·CHANGELOG)
  - 정리(삭제) 코드를 바꿨다면 안전 확인 목록
  - 기존 테스트의 기대값을 바꿨다면 그 이유
- PR 하나에는 한 가지 목적만 담습니다. 큰 기능이나 구조 변경은 먼저 Issue에서 방향을 맞춘 뒤 시작하면 다시 작업하는 일을 줄일 수 있습니다.
- 아직 작업 중이면 Draft PR로 엽니다.
- PR을 열면 GitHub Actions(CI)가 Release 구성으로 빌드하고 전체 테스트를 돌립니다. **CI가 통과해야 병합합니다.** 실패하면 로그를 보고 고쳐 주세요.
- 관리자가 리뷰한 뒤 병합 커밋으로 `main`에 병합합니다.

## 개발 환경과 빌드·테스트

**필요한 것**

- Windows 10 이상(64비트)
- Visual Studio 또는 Build Tools: "C++를 사용한 데스크톱 개발" 워크로드(MSVC, Windows SDK, CMake 포함)
- CMake 3.21 이상(`cmake`가 PATH에 없으면 **Developer PowerShell for VS**에서 실행)
- 선택: Git(일부 CLI 검사가 Git 상태까지 확인, 없으면 그 부분은 건너뜀), [Inno Setup 6](https://jrsoftware.org/isinfo.php)(설치 프로그램을 만들 때만)

**빌드·테스트**

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

- 결과물: `build\Debug\` 아래 `bytesieve-gui.exe`(GUI), `disk-scanner.exe`(CLI), `bytesieve-bench.exe`(성능 측정)
- CI와 같은 조건으로 확인하려면 `--config Release`·`-C Release`로 빌드·테스트합니다.
- 테스트 하나만: `ctest --test-dir build -C Debug -R ui_lang --output-on-failure`
- 테스트 종류: 단위 테스트(`tests/test_*.c`), CLI 검사(`tests/*.cmake`), GUI 자체 검사(숨긴 창에서 실제 작업을 실행하고 화면 상태 확인).
  목록과 범위, GUI 자체 검사 옵션은 [docs/testing.md](docs/testing.md#gui-자체-검사-옵션)에 있습니다.
- 배포 파일(포터블 ZIP·설치 프로그램)까지 만들어 보려면
  `powershell -ExecutionPolicy Bypass -File tools\package.ps1 -NoInstaller`(Inno Setup이 있으면 `-NoInstaller` 없이)를 실행합니다.

**새 테스트 추가**: `tests/test_<모듈>.c`를 만들고 `CMakeLists.txt`에 다른 단위 테스트와 같은 방식
(`add_executable` → `target_link_libraries` → `bytesieve_configure` → `add_test`)으로 등록합니다.

코드 구조는 [docs/architecture.md](docs/architecture.md)를 먼저 읽으면 빠르게 파악할 수 있습니다.
core(스캔·정리 계획·휴지통 이동 등)와 화면(`gui_*`)·CLI(`cli*`)가 나뉘어 있고, GUI와 CLI는 같은 core 코드를 씁니다.

## 코드 규칙

- **C11 + Win32 API만** 씁니다. 외부 라이브러리를 추가하지 않습니다.
- MSVC `/W4 /utf-8 /std:c11`로 빌드합니다. **새 코드에서 경고가 나오지 않게** 합니다.
- 주변 코드의 스타일을 따릅니다.
  - 들여쓰기 4칸. 함수의 여는 중괄호는 다음 줄, `if`·`for`·`switch` 등은 같은 줄이며 한 줄짜리 본문에도 중괄호를 씁니다.
  - 주석은 한국어. 헤더에는 함수의 동작과 안전 조건(계약)을 적습니다.
  - 문자열·경로는 `wchar_t`(Win32 `W` API)
- 상태 enum을 추가하면 화면용 `*_status_text`와 식별자용 `*_status_name`도 함께 추가합니다.
- core 코드는 화면 문구(번역)에 기대지 않습니다. core의 한국어 상태 문구는 CLI·테스트용이고, GUI는
  `gui_status_text`에서 같은 상태 번호를 번역 표에 연결합니다.
- 새 Win32 호출은 가능하면 이미 그 일을 맡은 core 모듈 안에 둡니다. 화면 코드(`gui_*`)에서 파일 시스템·레지스트리를 직접 다루지 않습니다
  ([docs/platform.md](docs/platform.md)).
- 성능 개선은 측정 근거와 함께 냅니다([docs/performance.md](docs/performance.md)).

## 정리(삭제) 기능의 안전 원칙

ByteSieve는 사용자 파일을 옮기고 지우는 도구라서 아래 원칙은 예외 없이 지킵니다. 이 부분을 건드리는 PR은 특히 꼼꼼히 봅니다.

- **영구 삭제는 사용자가 명시적으로 확인한 경우에만** 합니다. 자동으로, 또는 실패를 처리하다가 영구 삭제가 일어나면 안 됩니다.
- 어떤 경로로 정리하든 `src/cleanup_trash.h`에 적힌 안전 장치(경로 판정 → 옮기기 직전 확인 → 옮기는 중 확인)를 그대로 거칩니다.
- 휴지통을 쓸 수 없는 드라이브는 같은 볼륨의 격리 폴더(`.bytesieve-trash`)로 옮깁니다. 격리 폴더의 기록과 실제 파일이
  어긋나도 파일을 지우지 않습니다.
- 정리 관련 변경에는 실패·충돌 상황(권한 부족, 사용 중인 파일, 같은 이름 등)의 회귀 테스트를 함께 넣어 주세요.
  테스트는 임시 폴더의 픽스처에서만 파일을 옮기게 하고, 실제 사용자 폴더·휴지통을 건드리지 않게 합니다.

자세한 규칙은 [docs/cleanup.md](docs/cleanup.md)에 있습니다.

## 번역

GUI는 한국어·영어·일본어·중국어 간체를 지원합니다. 번역 개선도 큰 도움이 됩니다.

- 화면 문구는 코드에 직접 쓰지 않고 번역 표 `src/ui_lang.c`(항목 이름은 `src/ui_lang.h`의 `UiText`)에 **네 언어를 모두** 넣습니다.
- 서식 지정자(`%ls`, `%u` 등)는 네 언어에서 같은 순서로 씁니다.
- `test_ui_lang`이 빠진 번역, 서식 지정자 불일치, 한국어가 아닌 문구에 섞인 한글을 잡습니다.
- 문구 길이가 언어마다 달라 단추 폭은 글자 폭에 맞춰 정합니다. 화면이 잘리지 않는지는 `gui_lang_*`·`gui_layout_*` 테스트가 확인합니다.
- 설치 프로그램 이용약관(`installer/terms/terms-*.txt`)의 일본어·중국어 간체는 **검토가 필요한 번역 초안**입니다.
  원어민 검토를 환영합니다. 약관과 라이선스 원문이 다르면 원문이 우선합니다.

규칙은 [docs/ui-design.md](docs/ui-design.md#다국어)에 있습니다.

## 문서와 테스트 개수

- 기능을 바꾸면 관련 `docs/*.md`와 README의 기능 표·향후 개선 계획을 함께 고칩니다.
- 테스트를 추가·삭제하면 README의 테스트 개수와 [docs/testing.md](docs/testing.md)를 함께 고칩니다.
- 사용자에게 보이는 변경은 [CHANGELOG.md](CHANGELOG.md)의 `[Unreleased]` 구역에 적습니다.
- UI가 바뀌면 `tools\screenshots.ps1`로 `docs/screenshots`를 다시 찍습니다(예제 데이터만 사용, [docs/gui.md](docs/gui.md#스크린샷)).
- 문서에는 직접 확인한 명령·경로·링크만 씁니다.

## 기여할 거리

- [`good first issue`](https://github.com/gajigaji-dev/ByteSieve/labels/good%20first%20issue) Label이 붙은 Issue는 처음 기여하기 좋은 작은 일입니다.
- README의 [향후 개선 계획](README.md#향후-개선-계획미구현)에 아직 구현하지 않은 기능과 개선점이 정리되어 있습니다.
- 일본어·중국어 간체 번역과 이용약관 검토
- 다양한 환경(이동식 드라이브, 네트워크 드라이브, 고배율 화면, 다크 모드)에서 찾은 버그 신고

## 라이선스

ByteSieve는 [PolyForm Noncommercial License 1.0.0](LICENSE)으로 배포됩니다. 기여한 코드도 저장소의 다른 코드와 함께
이 라이선스로 배포되므로, PR을 보내기 전에 라이선스를 확인해 주세요.

---

## In English

Thanks for your interest in ByteSieve, a Windows disk usage analyzer and safe cleaner written in C11 and the Win32 API.
Everyone taking part follows the [Code of Conduct](CODE_OF_CONDUCT.md).

- **Issues**: search [existing issues](https://github.com/gajigaji-dev/ByteSieve/issues) first, then pick a form (bug report,
  feature request, translation issue). Include the ByteSieve version (Settings page or `disk-scanner --version`), Windows
  version, display language and steps to reproduce. Please hide personal information (user names, folder names) in
  screenshots and logs. Report security problems privately by e-mail (see the License section of the README).
- **Branches**: `main` is the stable, release branch (releases are `v<version>` tags on `main`). Branch from `main` with a
  prefix such as `feat/`, `fix/`, `docs/`, `refactor/`, `perf/`, `test/` or `chore/`, and open the PR against `main`.
- **Commits and PR titles**: `type: summary` (the type matches the branch prefix). Link issues with `Closes #123`.
  CI builds and runs every test in the Release configuration and must pass before merging.
- **Build and test** (Visual Studio or Build Tools with the "Desktop development with C++" workload, CMake 3.21+):
  ```powershell
  cmake -S . -B build
  cmake --build build --config Debug
  ctest --test-dir build -C Debug --output-on-failure
  ```
- **Rules**: C11 + Win32 only, no external libraries, no new `/W4` warnings. Every UI string goes into `src/ui_lang.c` in all
  four languages (Korean, English, Japanese, Simplified Chinese). Code comments in this repository are written in Korean;
  English comments in a PR are fine and can be translated during review.
- **Safety**: permanent deletion happens only after explicit user confirmation, and every cleanup path goes through the
  checks in `src/cleanup_trash.h`. Changes to cleanup code need regression tests that only touch temporary fixtures.
- **License**: contributions are distributed under the [PolyForm Noncommercial License 1.0.0](LICENSE).
