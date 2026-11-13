#include "gui_status_text.h"

#include <stdio.h>

#include "ui_lang.h"

/* CleanupTrashStatus 순서(PENDING 은 문구 없음). */
static const UiText k_trash[CLEANUP_TRASH_STATUS_COUNT] = {
    [CLEANUP_TRASH_PENDING] = UI_T_COUNT,
    [CLEANUP_TRASH_MOVED] = UI_T_TRASH_MOVED,
    [CLEANUP_TRASH_QUARANTINED] = UI_T_TRASH_QUARANTINED,
    [CLEANUP_TRASH_DENY_PATH] = UI_T_TRASH_DENY_PATH,
    [CLEANUP_TRASH_DENY_OUTSIDE] = UI_T_TRASH_DENY_OUTSIDE,
    [CLEANUP_TRASH_DENY_PROTECTED] = UI_T_TRASH_DENY_PROTECTED,
    [CLEANUP_TRASH_DENY_CRITICAL] = UI_T_TRASH_DENY_CRITICAL,
    [CLEANUP_TRASH_DENY_INCOMPLETE] = UI_T_TRASH_DENY_INCOMPLETE,
    [CLEANUP_TRASH_DENY_DRIVE] = UI_T_TRASH_DENY_DRIVE,
    [CLEANUP_TRASH_MISSING] = UI_T_TRASH_MISSING,
    [CLEANUP_TRASH_CHANGED] = UI_T_TRASH_CHANGED,
    [CLEANUP_TRASH_NO_RECYCLE] = UI_T_TRASH_NO_RECYCLE,
    [CLEANUP_TRASH_FAILED] = UI_T_TRASH_FAILED,
    [CLEANUP_TRASH_QUARANTINE_FAILED] = UI_T_TRASH_QUARANTINE_FAILED,
    [CLEANUP_TRASH_CANCELLED] = UI_T_TRASH_CANCELLED,
};

/* ProjectRecovery 순서(빠진 값은 0 이 되어 gui_status_text_verify 가 잡는다). */
static const UiText k_recovery[PROJECT_RECOVER_COUNT] = {
    [PROJECT_RECOVER_NONE] = UI_T_RECOVER_NONE,
    [PROJECT_RECOVER_REBUILD] = UI_T_RECOVER_REBUILD,
    [PROJECT_RECOVER_LOCKED] = UI_T_RECOVER_LOCKED,
    [PROJECT_RECOVER_UNLOCKED] = UI_T_RECOVER_UNLOCKED,
};
#define RECOVERY_COUNT PROJECT_RECOVER_COUNT

const wchar_t *gui_trash_status_text(CleanupTrashStatus status)
{
    if ((unsigned)status >= (unsigned)CLEANUP_TRASH_STATUS_COUNT || k_trash[status] == UI_T_COUNT) {
        return L"";
    }
    return ui_text(k_trash[status]);
}

const wchar_t *gui_recovery_text(ProjectRecovery recovery)
{
    return (unsigned)recovery < (unsigned)RECOVERY_COUNT ? ui_text(k_recovery[recovery]) : L"";
}

int gui_status_text_verify(void)
{
    int failures = 0, i, ko = ui_lang_current() == UI_LANG_KO;

    for (i = 0; i < CLEANUP_TRASH_STATUS_COUNT; i++) {
        const wchar_t *s = gui_trash_status_text((CleanupTrashStatus)i);
        if (i != CLEANUP_TRASH_PENDING && (k_trash[i] == 0 || s[0] == L'\0')) {
            fwprintf(stderr, L"status-text self-test failed: trash status %d has no text\n", i);
            failures++;
        }
        if (ko && wcscmp(s, cleanup_trash_status_text((CleanupTrashStatus)i)) != 0) {
            fwprintf(stderr, L"status-text self-test failed: trash status %d differs from core\n", i);
            failures++;
        }
    }
    for (i = 0; i < (int)RECOVERY_COUNT; i++) {
        const wchar_t *s = gui_recovery_text((ProjectRecovery)i);
        if (k_recovery[i] == 0 || s[0] == L'\0' || (ko && wcscmp(s, project_recovery_text((ProjectRecovery)i)) != 0)) {
            fwprintf(stderr, L"status-text self-test failed: recovery %d\n", i);
            failures++;
        }
    }
    return failures;
}
