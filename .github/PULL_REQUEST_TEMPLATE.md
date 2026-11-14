<!-- 기여 안내: CONTRIBUTING.md / See CONTRIBUTING.md -->
<!-- PR 제목은 커밋과 같은 `종류: 요약` 형식으로 씁니다(예: `fix: 격리 폴더 페이지 번역 및 언어 즉시 적용`). -->
<!-- Title format: `type: summary` (feat / fix / docs / refactor / test / chore). -->

## 무엇을 왜 바꿨나요 / What and why

<!-- 관련 Issue가 있으면 적어 주세요(예: Closes #123). / Link related issues, e.g. "Closes #123". -->

## 변경 종류 / Type

- [ ] feat: 새 기능 / New feature
- [ ] fix: 버그 수정(번역 누락 포함) / Bug fix (incl. missing translations)
- [ ] docs: 문서만 / Docs only
- [ ] refactor: 동작 변화 없는 구조 변경 / Refactor (no behavior change)
- [ ] test: 테스트만 / Tests only
- [ ] chore: 빌드·배포·저장소 관리 / Build, release, maintenance

## 어떻게 확인했나요 / How was this tested

<!-- 실행한 테스트, 직접 확인한 화면·명령. 화면이 바뀌면 스크린샷을 붙여 주세요(개인정보는 가림). -->
<!-- Tests you ran and what you checked by hand. Attach screenshots for UI changes (hide personal information). -->

## 확인 목록 / Checklist

- [ ] `cmake --build build --config Debug`에서 새 경고가 없다 / No new build warnings (`/W4`)
- [ ] `ctest --test-dir build -C Debug --output-on-failure`가 모두 통과한다 / All tests pass
- [ ] 외부 라이브러리를 추가하지 않았다(C11 + Win32 API만) / No external libraries added
- [ ] 화면 문구는 `src/ui_lang.c`에 네 언어(한국어·영어·일본어·중국어 간체)로 넣었다(해당 시) / UI strings added in all four languages (if any)
- [ ] 상태 enum을 추가했다면 `*_status_text`·`*_status_name`도 함께 추가했다(해당 시) / New status enums come with `*_status_text` and `*_status_name` (if any)
- [ ] 관련 문서(`docs/*.md`, README 기능 표·향후 계획·테스트 개수, `docs/testing.md`)와 `CHANGELOG.md`의 `[Unreleased]`를 고쳤다(해당 시) / Docs and CHANGELOG updated (if needed)
- [ ] 기존 테스트의 기대값을 바꿨다면 아래에 이유를 적었다(해당 시) / Reason given below if existing test expectations changed

## 정리(삭제) 기능을 바꿨다면 / If cleanup or deletion code changed

<!-- 해당하지 않으면 이 구역을 지워 주세요. / Delete this section if not applicable. -->

- [ ] 영구 삭제는 사용자가 명시적으로 확인한 경우에만 하며, 자동으로나 실패 처리의 결과로 일어나지 않는다 / Permanent deletion only after explicit user confirmation, never automatically or as a failure fallback
- [ ] `cleanup_trash.h`의 안전 장치(경로 판정 → 옮기기 직전 확인 → 옮기는 중 확인)를 그대로 거친다 / Goes through every safety check in `cleanup_trash.h`
- [ ] 메타데이터와 실제 파일이 어긋나도 파일을 지우지 않는다(격리 폴더) / Never deletes files on metadata mismatch (quarantine)
- [ ] 회귀 테스트를 추가했다 / Regression tests added

## 기존 테스트 기대값 변경 이유 / Why existing test expectations changed

<!-- 해당하지 않으면 지워 주세요. / Delete if not applicable. -->
