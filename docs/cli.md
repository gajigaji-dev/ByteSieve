# CLI 사용법 (`disk-scanner`)

```
disk-scanner [--max-depth N] [--jsonl] [--track [--history-dir DIR]] [--threads N] <directory>
disk-scanner -h | --help
disk-scanner find <directory> ...        # 정리 후보 찾기: 아래 [find](#정리-후보-찾기-disk-scanner-find) 참고
disk-scanner clean project|system ...   # 정리: 아래 [clean](#정리-disk-scanner-clean) 참고
```

| 옵션 | 설명 |
|---|---|
| `--max-depth N` | 대상 폴더 아래 N단계까지만 탐색(0 = 대상 폴더만, 기본: 제한 없음) |
| `--jsonl` | 표준 출력에 UTF-8 JSON Lines 이벤트 출력 |
| `--track` | 직전 `--track` 스캔과 폴더별 용량을 비교하고 새 스냅숏 저장 |
| `--history-dir DIR` | 스냅숏 폴더(`--track`과 함께만 사용). 기본: 환경 변수 `BYTESIEVE_HISTORY_DIR`, 없으면 `%LOCALAPPDATA%\ByteSieve\history` |
| `--threads N` | 폴더 열거 작업 스레드 수(0 = 자동, 1 = 병렬 없음, 최대 8) |
| `-h`, `--help` | 도움말(다른 인자보다 우선) |
| `--` | 다음 인자를 대상 폴더로 취급 |

옵션과 대상의 순서는 상관없고 `--max-depth=2`처럼 `=` 형식도 받습니다.
정리한 항목을 옮겨 둔 격리 폴더(`.bytesieve-trash`)는 GUI와 같이 들어가지 않고 용량에서 뺍니다(폴더 수에는 그 폴더 하나만 셉니다).
**종료 코드**: `0` 모든 폴더를 읽음, `1` 일부 폴더를 읽지 못함, `2` 대상 폴더를 스캔할 수 없거나 인자 오류.

```powershell
build\Debug\disk-scanner.exe C:\Users\me\Projects
build\Debug\disk-scanner.exe --max-depth 0 C:\Users\me\Projects
build\Debug\disk-scanner.exe --jsonl C:\Users\me\Projects > scan.jsonl
build\Debug\disk-scanner.exe --track C:\Users\me\Projects
build\Debug\disk-scanner.exe --threads 1 C:\Users\me\Projects   # 병렬 없이(메모리를 덜 씀)
```

## 텍스트 출력

```
Target:         readme_demo
Max depth:      unlimited
Files:          3
Directories:    4
Total size:     12.21 KiB (12503 bytes)
On disk:        12.50 KiB (12800 bytes)
  Slack:        +297 bytes (cluster size 4.00 KiB)
Reparse points: 0 (not descended)
Errors:         0
Skipped dirs:   0
Aborted:        no
Status:         complete (all directories scanned)
```

`On disk:` 아래에는 0이 아닌 원인(`Slack`·`Compressed`·`Sparse`·`Cloud-only`·`Other`·`Unknown`)만 표시합니다.
오류가 있으면 표준 오류에 경로·종류·Windows 오류 코드를 최대 20개까지 나열합니다.

## 변화 추적 (`--track`)

두 번째 스캔부터 직전 스캔 대비 변화를 보여 줍니다(첫 스캔은 `Changes: no previous snapshot (baseline)`).
변화량이 큰 폴더를 최대 20개 나열하며, 기록 저장 실패는 경고만 하고 종료 코드에는 영향을 주지 않습니다.

```
Changes since 2026-10-31 02:37: -8.01 KiB (12.2 KiB -> 4.20 KiB)
  grown 1, shrunk 1, added 1, removed 1
     -11.7 KiB  shrunk   app
     -11.7 KiB  removed  app\node_modules
     +2.93 KiB  grown    logs
        +800 B  added    dist
History:        saved to C:\Users\me\AppData\Local\ByteSieve\history\6f56c5d88fb9907f.snap
```

## JSON Lines 출력 (`--jsonl`)

이벤트 순서는 `start` → `progress`(1,000개 항목마다) → `result` → `complete`입니다.

```json
{"event":"start","target":"readme_demo"}
{"event":"result","target":"readme_demo","files":3,"dirs":4,"bytes":12503,"alloc_bytes":12800,"disk":{"slack":297,"compressed_saved":0,"sparse_saved":0,"cloud_saved":0,"other_saved":0,"unknown_files":0},"reparse_points":0,"error_count":0,"errors":[]}
{"event":"complete","status":"complete","exit_code":0}
```

`--jsonl --track`이면 `progress` 대신 `complete` 앞에 `changes` 이벤트가 붙습니다(`items`는 최대 100개, 나머지는 `more`).

```json
{"event":"changes","previous":true,"since":134378554553163683,"old_bytes":4303,"new_bytes":4303,"items":[],"more":0,"saved_to":"C:\\Users\\me\\AppData\\Local\\ByteSieve\\history\\6f56c5d88fb9907f.snap"}
```

`items`의 각 항목은 `{"path","kind":"grown|shrunk|added|removed","old","new","delta","uncertain"}`이고, `since`는 직전 스캔 시각(FILETIME, UTC)입니다.

## 정리 후보 찾기 (`disk-scanner find`)

프로젝트 정리 후보를 찾아 보여 주기만 합니다(읽기 전용, 아무것도 바꾸지 않음). GUI [프로젝트 정리]와 같은 탐지·판정(보호 경로·앱 데이터 폴더 제외, 실제 확보 용량)을 씁니다.

```
disk-scanner find <directory> [--category LIST] [--eco LIST] [--dormant-days N] [--no-git]
                  [--json | --jsonl]
disk-scanner find -h | --help
```

| 옵션 | 설명 |
|---|---|
| `--category LIST` | 이 유형만(`node_modules`, `build`, `cache`, `temp`, `log`) |
| `--eco LIST` | 이 개발 환경만(`node`, `python`, `rust`, `java`, `dotnet`, `cpp`, `go`, `generic`) |
| `--json` | JSON 문서 하나(UTF-8)를 stdout에 쓴다 |
| `--jsonl` | JSON Lines 이벤트(UTF-8)를 stdout에 쓴다 |

후보는 파일 크기가 큰 것부터 나옵니다. 보호 경로 안의 후보도 `protected:<이유>`와 함께 보이며 `cleanable`이 아닙니다.

```
Cleanup candidates under D:\work: 3 found, 3 cleanable, reclaim 20.0 KiB (size 14.0 KiB)
  #    Reclaim    Size       Type          Eco      Note                   Path
  1    12.0 KiB   8.79 KiB   node_modules  node     confirmed              D:\work\web\node_modules
  2    8.00 KiB   4.88 KiB   build         node     confirmed              D:\work\web\dist
  3    0 B        302 B      build         generic  guessed                D:\work\loose\dist
Nothing was changed. Use 'disk-scanner clean project' to see the cleanup plan and move them to the Recycle Bin.
```

`Note`: `confirmed`(같은 폴더에 프로젝트 파일이 있음) 또는 `guessed`(이름으로만 추정), `incomplete`(일부를 읽지 못해 용량이 더 클 수 있음), `hardlinks`(하드 링크로 다른 곳과 나누는 파일이 있어 실제 확보에서 뺌), `protected:<이유>`.

```json
{"version":1,"command":"find","root":"D:\\work","status":"complete",
 "summary":{"candidates":3,"cleanable":3,"bytes":14306,"reclaim":20480},
 "candidates":[{"index":1,"category":"node_modules","eco":"node","path":"D:\\work\\web\\node_modules",
   "bytes":9002,"reclaim":12288,"shared":0,"files":1,"dir":true,"confirmed":true,"guessed":false,
   "incomplete":false,"cleanable":true,"state":"ready"}, ...],
 "exit_code":0}
```

(실제 출력은 한 줄입니다.) `--jsonl`은 `find_start` → 후보마다 `find_candidate`(위 `candidates` 항목과 같은 필드) → `complete`(`status`·`candidates`·`cleanable`·`bytes`·`reclaim`·`exit_code`) 순서입니다.

## 정리 (`disk-scanner clean`)

GUI의 [프로젝트 정리]·[시스템 정리] 탭과 **같은 탐지·계획·실행 코드**(`clean_plan`)를 씁니다. 기본은 `--dry-run`(계획과 예상 결과만 출력)이며, `--execute`를 줘야만 옮깁니다.

```
disk-scanner clean project <directory> [--category LIST] [--eco LIST] [--include-dirty]
                           [--dormant-days N] [--no-git]
                           [--dry-run | --execute [--yes]] [--json | --jsonl]
disk-scanner clean system [--include LIST] [--exclude LIST]
                          [--dry-run | --execute [--yes]] [--json | --jsonl]
disk-scanner clean -h | --help
```

| 옵션 | 설명 |
|---|---|
| `project <directory>` | 폴더 안의 `node_modules`·빌드 결과물·프로젝트 캐시·임시·로그(정리 후보). GUI [프로젝트 정리]와 같은 탐지(보호 경로·앱 데이터 폴더 제외) |
| `system` | 사용자 임시 파일·Windows 오류 보고서·브라우저 캐시·공용 개발 캐시(npm·pip·Gradle·Cargo·NuGet). GUI [시스템 정리]와 같은 분석 |
| `--category LIST` | project: 이 유형만(`node_modules`, `build`, `cache`, `temp`, `log`) |
| `--eco LIST` | project: 이 개발 환경만(`node`, `python`, `rust`, `java`, `dotnet`, `cpp`, `go`, `generic`) |
| `--include LIST` | system: 이 대상도 고른다. 공용 개발 캐시는 기본으로 고르지 않으므로 이름(`npm`, `pip`, `gradle`, `gradle_wrapper`, `cargo_registry`, `cargo_git`, `nuget_packages`, `nuget_http`)이나 `dev_cache`(전체)로 지정 |
| `--exclude LIST` | system: 이 대상은 고르지 않는다(`user_temp`, `wer`, `chrome`, `edge`, `firefox`, 개발 캐시 이름) |
| `--dry-run` | 계획과 예상 결과(옮길 항목 수, 휴지통을 비우면 확보될 용량)만 보인다. 기본 동작과 같다 |
| `--execute` | 계획을 보여 준 뒤 확인(`[y/N]`, 기본 아니요)을 받고 휴지통(없는 드라이브는 격리 폴더)으로 옮긴다. `--apply`는 예전 이름으로 계속 동작한다 |
| `--yes` | 확인을 묻지 않는다(`--execute`와 함께만). 표준 입력이 대화형이 아니면(파이프·파일) `--yes` 없이는 실행하지 않고 종료 코드 2 |
| `--json` | JSON 문서 하나(UTF-8)를 stdout에 쓴다(안내·확인 질문은 stderr) |
| `--jsonl` | JSON Lines 이벤트(UTF-8)를 stdout에 쓴다(안내·확인 질문은 stderr) |

`--dry-run`과 `--execute`, `--json`과 `--jsonl`은 함께 쓸 수 없습니다. 목록은 쉼표로 구분하고 `--include=npm,pip`처럼 `=` 형식도 받습니다.
이름이 `clean`·`find`인 폴더를 스캔하려면 `disk-scanner -- clean` 또는 `disk-scanner .\clean`을 쓰세요.

**안전 규칙**(GUI와 같음)

- 선택은 GUI [모두 선택]과 같습니다. 휴지통 비우기와 공용 개발 캐시는 고르지 않고, 보호 경로·실행 중인 브라우저의 캐시·다 읽지 못한 항목은 고를 수 없습니다.
- 옮기기는 항목마다 같은 안전 장치(경로 판정 → 옮기기 직전 확인 → 옮기는 중 확인)를 거치고, 임시 파일은 실행 직전에 최근 변경을 다시 확인합니다. 영구 삭제하지 않습니다.
- **휴지통 비우기(영구 삭제)는 CLI에서 지원하지 않습니다.** `--include recycle_bin`은 인자 오류입니다.
- 일부 항목이 실패해도 나머지를 계속하고 항목별 결과를 남깁니다.

```powershell
build\Debug\disk-scanner.exe clean project D:\work                           # dry-run(계획·예상 결과)
build\Debug\disk-scanner.exe clean project D:\work --category node_modules --execute
build\Debug\disk-scanner.exe clean system --include npm,pip --exclude chrome --dry-run
build\Debug\disk-scanner.exe clean system --include dev_cache --execute --yes --json  # 스크립트용
```

```
Project cleanup plan: D:\work
  #    Sel  Reclaim    Size       State            Target                   Location
  1    [x]  8.00 KiB   4.88 KiB   ready            build                    D:\work\web\dist
  2    [x]  12.0 KiB   8.79 KiB   ready            node_modules             D:\work\web\node_modules
Selected 2 of 2 selectable target(s): reclaim 20.0 KiB (size 13.7 KiB)
Dry run - nothing was moved. Expected result: 2 item(s) in 2 target(s) would be moved to the Recycle Bin
(quarantine folder on drives without one), freeing about 20.0 KiB once it is emptied.
Add --execute to move them.
```

- `Sel`: `[x]` 고름, `[ ]` 고를 수 있음, `[-]` 고를 수 없음(이유는 `State`: `deny_protected`, `app_running`, `not_found` 등).
- `Reclaim`은 지웠을 때 실제로 비는 용량(디스크 할당 크기, 하드 링크로 대상 밖과 나누는 파일 제외)이고 `Size`는 파일 크기 합입니다.
  옮긴 항목은 휴지통·격리 폴더를 비워야 공간이 확보됩니다.
- 괄호 안 표시: `manual`(직접 골라야 함), `caution`(정리 뒤 인터넷 없이 빌드할 수 없음), `guessed`(프로젝트 파일 없이 이름으로 추정), `permanent`(영구 삭제, CLI에서 고를 수 없음).

**JSON 문서(`--json`)**: 실행이 끝난 뒤 한 번에 씁니다(실제 출력은 한 줄).

```json
{"version":1,"command":"clean","source":"project","mode":"execute","root":"D:\\work",
 "status":"complete","executed":true,
 "summary":{"selected":2,"selectable":2,"entries":2,"bytes":14004,"reclaim":20480,
            "caution":0,"manual":0,"guessed":0},
 "targets":[{"index":2,"id":"node_modules","label":"D:\\work\\web\\node_modules",
             "location":"D:\\work\\web\\node_modules","state":"ready","bytes":9002,"reclaim":12288,
             "shared":0,"files":1,"selectable":false,"selected":false,"manual":false,"caution":false,
             "guessed":false,"permanent":false,
             "result":{"moved":1,"quarantined":0,"failed":0,"skipped":0,"cancelled":0,"moved_bytes":9002,
                       "entries":[{"path":"D:\\work\\web\\node_modules","status":"moved","skip":"none",
                                   "code":0,"moved_to":"D:\\$RECYCLE.BIN\\...\\$R1A2B3C"}]}}, ...],
 "totals":{"targets":2,"moved":2,"quarantined":0,"failed":0,"skipped":0,"cancelled":0,"moved_bytes":14004},
 "note":null,"exit_code":0}
```

- `mode`: `dry-run` 또는 `execute`. `executed`가 `false`면(dry-run, 고른 것 없음, 확인 취소, `--yes` 없음) `totals`와 각 대상의 `result`는 `null`이고 `note`에 이유가 있습니다.
- `status`: 탐지 상태(`complete`, `partial` = 일부 폴더를 읽지 못함). 탐지 자체가 실패하면 `{"version":1,…,"status":"failed","error":"cannot scan …","exit_code":2}`만 씁니다.
- `result.entries[].status`는 휴지통 이동 결과 식별자(`moved`, `quarantined`, `failed`, `deny_protected`, `changed` 등), `skip`은 실행 직전 확인으로 건너뛴 이유(`none`, `recent`, `app_running`)입니다.

**JSON Lines(`--jsonl`)** 이벤트 순서는 `clean_plan` → 대상마다 `clean_target` → `clean_summary`(`mode` 포함, 예전 이름 `apply`도 남김) → (`--execute`면 실행한 대상마다 `clean_result`) → `complete`입니다.
`clean_target`·`clean_result`의 필드는 위 JSON 문서의 `targets` 항목·`result`와 같습니다.

### 프로젝트(Git) 정보

`find`와 `clean project`는 후보를 **프로젝트**별로 묶어 보입니다. 프로젝트는 후보 위쪽에서 가장 가까운 Git 저장소(`.git` 폴더나 워크트리의
`gitdir:` 파일이 있는 폴더)이고, 저장소가 아니면 후보가 들어 있는 폴더입니다.

| 옵션 | 설명 |
|---|---|
| `--dormant-days N` | 마지막 활동(마지막 커밋과 파일 마지막 수정 중 늦은 쪽)이 N일보다 오래되면 휴면(기본 90, 1~36500) |
| `--no-git` | git.exe를 실행하지 않고 `.git`을 직접 읽는다(브랜치·HEAD가 마지막으로 움직인 시각·원격 ref 비교만, 미커밋 변경은 모름) |
| `--include-dirty` | clean project: 커밋하지 않은 변경이 있는 프로젝트의 후보도 고른다(기본으로는 고르지 않음) |

```
Projects: 2 (1 dormant after 90 days, 1 with uncommitted changes, 1 with unpushed commits)
  P1   D:\work\web
       git main | last activity 2020-01-15 (2487 days ago) | DORMANT | NO REMOTE: 1 commit(s) only on this PC
  P2   D:\work\api
       git main | last activity 2026-11-07 (0 days ago) | UNCOMMITTED CHANGES: 3
```

- Git 상태는 PATH의 git.exe(환경 변수 `BYTESIEVE_GIT`로 경로 지정, `none`이면 쓰지 않음)를 **읽기 전용**으로 실행해 얻습니다:
  `rev-parse`, `log -1`, `status --porcelain`, `for-each-ref refs/remotes`, `rev-list --count HEAD --not --remotes`.
  `--no-optional-locks`로 index를 다시 쓰지 않고, `-c core.fsmonitor=false`로 저장소 설정의 외부 프로그램이 실행되지 않게 합니다.
  git.exe가 없거나 실패하면(다른 사용자 소유라 거부 등) `.git`을 직접 읽습니다.
- **미커밋 변경**: 추적하지 않는 새 파일을 포함한 변경 수(`.gitignore`로 무시한 `node_modules` 등은 세지 않음).
  미커밋 변경이 있는 프로젝트의 후보는 `uncommitted`로 표시하고 기본으로 고르지 않습니다.
- **미푸시 커밋**: 어느 원격에도 없는 커밋 수. 원격이 없으면 모든 커밋이 이 PC에만 있다고 알립니다(`NO REMOTE`).
- **마지막 수정**: 프로젝트 안 파일의 가장 늦은 수정 시각(`.git`·정리 후보 이름의 폴더·링크·다른 프로젝트 제외, 20만 항목까지).
- 후보마다 **복구 가능성**(`Recovery`): `locked`(lockfile이 있어 같은 버전으로 재설치: `package-lock.json`·`yarn.lock`·`pnpm-lock.yaml`·`bun.lock(b)`·
  `npm-shrinkwrap.json`), `unlocked`(재설치는 되지만 버전이 바뀔 수 있음), `rebuild`(빌드하면 다시 생김), `none`(다시 생기지 않음: 임시 파일·로그).

JSON 문서에는 `projects` 배열(`index`, `root`, `git`, `git_source`(`git`·`direct`·`none`), `branch`, `last_commit`·`last_modified`·`last_activity`(유닉스 초, 모르면
`null`), `age_days`, `dormant`, `changes`·`unpushed`(모르면 `null`), `has_remote`, `remote_differs`, `modified_incomplete`, `candidates`)과 `dormant_days`·`git_available`이,
후보·대상마다 `project`(1부터, `projects`의 `index`), `recovery`, `dirty`, `unpushed`, `dormant`가 붙습니다. `--jsonl`은 프로젝트마다 `project` 이벤트를 냅니다.

## 종료 코드(`find`·`clean`)

| 코드 | 이름 | 뜻 |
|---|---|---|
| `0` | 완료 | 모든 폴더를 읽었고, `--execute`면 고른 항목을 모두 옮김. 고른 것이 없거나 확인에서 취소한 경우도 아무것도 바꾸지 않았으므로 0 |
| `1` | 일부 | 일부 폴더를 읽지 못했거나(용량이 더 클 수 있음), 실행에서 옮기지 못한 항목(실패·건너뜀·취소)이 있음 |
| `2` | 오류 | 인자 오류, 탐지 불가(폴더를 열지 못함), 메모리 부족·출력 실패, 확인할 수 없는데 `--yes` 없음 |

`--json`·`--jsonl` 출력의 `exit_code`는 프로세스 종료 코드와 같습니다.
