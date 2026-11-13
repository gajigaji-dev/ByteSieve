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

/* Cleaner 대상 종류(CleanerKind 순서): 이름·설명. */
static const UiText k_kind_name[CLEANER_KIND_COUNT] = {
    [CLEANER_USER_TEMP] = UI_T_K_NAME_USER_TEMP, [CLEANER_WER] = UI_T_K_NAME_WER,
    [CLEANER_RECYCLE_BIN] = UI_T_K_NAME_RECYCLE, [CLEANER_CHROME] = UI_T_K_NAME_CHROME,
    [CLEANER_EDGE] = UI_T_K_NAME_EDGE,           [CLEANER_FIREFOX] = UI_T_K_NAME_FIREFOX,
    [CLEANER_DEV_CACHE] = UI_T_K_NAME_DEV,       [CLEANER_VDISK] = UI_T_K_NAME_VDISK,
};
static const UiText k_kind_note[CLEANER_KIND_COUNT] = {
    [CLEANER_USER_TEMP] = UI_T_K_NOTE_USER_TEMP, [CLEANER_WER] = UI_T_K_NOTE_WER,
    [CLEANER_RECYCLE_BIN] = UI_T_K_NOTE_RECYCLE, [CLEANER_CHROME] = UI_T_K_NOTE_CHROME,
    [CLEANER_EDGE] = UI_T_K_NOTE_EDGE,           [CLEANER_FIREFOX] = UI_T_K_NOTE_FIREFOX,
    [CLEANER_DEV_CACHE] = UI_T_K_NOTE_DEV,       [CLEANER_VDISK] = UI_T_K_NOTE_VDISK,
};
static const UiText k_group[CLEANER_GROUP_COUNT] = {
    [CLEANER_GROUP_WINDOWS] = UI_T_K_GROUP_WINDOWS, [CLEANER_GROUP_RECYCLE] = UI_T_K_GROUP_RECYCLE,
    [CLEANER_GROUP_BROWSER] = UI_T_K_GROUP_BROWSER, [CLEANER_GROUP_DEV] = UI_T_K_GROUP_DEV,
    [CLEANER_GROUP_VDISK] = UI_T_K_GROUP_VDISK,
};
static const UiText k_cleaner_status[CLEANER_STATUS_COUNT] = {
    [CLEANER_READY] = UI_T_K_ST_READY,         [CLEANER_EMPTY] = UI_T_K_ST_EMPTY,
    [CLEANER_NOT_FOUND] = UI_T_K_ST_NOT_FOUND, [CLEANER_APP_RUNNING] = UI_T_K_ST_APP_RUNNING,
    [CLEANER_DENIED] = UI_T_K_ST_DENIED,       [CLEANER_ERROR] = UI_T_K_ST_ERROR,
    [CLEANER_CANCELLED] = UI_T_K_ST_CANCELLED, [CLEANER_INFO_ONLY] = UI_T_K_ST_INFO_ONLY,
};
static const UiText k_skip[CLEAN_SKIP_COUNT] = {
    [CLEAN_SKIP_NONE] = UI_T_SKIP_NONE,
    [CLEAN_SKIP_RECENT] = UI_T_SKIP_RECENT,
    [CLEAN_SKIP_APP_RUNNING] = UI_T_SKIP_APP_RUNNING,
};
/* 개발 캐시(DevCacheKind 순서): 캐시 이름·설명. */
static const UiText k_dev_label[DEV_CACHE_KIND_COUNT] = {
    [DEV_CACHE_NPM] = UI_T_D_LABEL_NPM,
    [DEV_CACHE_PIP] = UI_T_D_LABEL_PIP,
    [DEV_CACHE_GRADLE] = UI_T_D_LABEL_GRADLE,
    [DEV_CACHE_GRADLE_WRAPPER] = UI_T_D_LABEL_GRADLE_WRAPPER,
    [DEV_CACHE_CARGO_REGISTRY] = UI_T_D_LABEL_CARGO_REGISTRY,
    [DEV_CACHE_CARGO_GIT] = UI_T_D_LABEL_CARGO_GIT,
    [DEV_CACHE_NUGET_PACKAGES] = UI_T_D_LABEL_NUGET_PACKAGES,
    [DEV_CACHE_NUGET_HTTP] = UI_T_D_LABEL_NUGET_HTTP,
    [DEV_CACHE_PNPM] = UI_T_D_LABEL_PNPM,
    [DEV_CACHE_YARN] = UI_T_D_LABEL_YARN,
    [DEV_CACHE_YARN_BERRY] = UI_T_D_LABEL_YARN_BERRY,
    [DEV_CACHE_MAVEN] = UI_T_D_LABEL_MAVEN,
    [DEV_CACHE_GO_MOD] = UI_T_D_LABEL_GO_MOD,
    [DEV_CACHE_GO_BUILD] = UI_T_D_LABEL_GO_BUILD,
    [DEV_CACHE_CONDA] = UI_T_D_LABEL_CONDA,
};
static const UiText k_dev_note[DEV_CACHE_KIND_COUNT] = {
    [DEV_CACHE_NPM] = UI_T_D_NOTE_NPM,
    [DEV_CACHE_PIP] = UI_T_D_NOTE_PIP,
    [DEV_CACHE_GRADLE] = UI_T_D_NOTE_GRADLE,
    [DEV_CACHE_GRADLE_WRAPPER] = UI_T_D_NOTE_GRADLE_WRAPPER,
    [DEV_CACHE_CARGO_REGISTRY] = UI_T_D_NOTE_CARGO_REGISTRY,
    [DEV_CACHE_CARGO_GIT] = UI_T_D_NOTE_CARGO_GIT,
    [DEV_CACHE_NUGET_PACKAGES] = UI_T_D_NOTE_NUGET_PACKAGES,
    [DEV_CACHE_NUGET_HTTP] = UI_T_D_NOTE_NUGET_HTTP,
    [DEV_CACHE_PNPM] = UI_T_D_NOTE_PNPM,
    [DEV_CACHE_YARN] = UI_T_D_NOTE_YARN,
    [DEV_CACHE_YARN_BERRY] = UI_T_D_NOTE_YARN_BERRY,
    [DEV_CACHE_MAVEN] = UI_T_D_NOTE_MAVEN,
    [DEV_CACHE_GO_MOD] = UI_T_D_NOTE_GO_MOD,
    [DEV_CACHE_GO_BUILD] = UI_T_D_NOTE_GO_BUILD,
    [DEV_CACHE_CONDA] = UI_T_D_NOTE_CONDA,
};
static const UiText k_dev_status[DEV_CACHE_STATUS_COUNT] = {
    [DEV_CACHE_FOUND] = UI_T_D_ST_FOUND,
    [DEV_CACHE_NOT_FOUND] = UI_T_D_ST_NOT_FOUND,
    [DEV_CACHE_NOT_DIR] = UI_T_D_ST_NOT_DIR,
    [DEV_CACHE_LINK] = UI_T_D_ST_LINK,
    [DEV_CACHE_UNRECOGNIZED] = UI_T_D_ST_UNRECOGNIZED,
    [DEV_CACHE_NO_PATH] = UI_T_D_ST_NO_PATH,
};

/* 표 table[n] 의 i 번째 문구(범위 밖·빈 칸은 빈 문자열). */
static const wchar_t *table_text(const UiText *table, int n, int i)
{
    return i >= 0 && i < n && table[i] != 0 ? ui_text(table[i]) : L"";
}

const wchar_t *gui_cleaner_kind_name(CleanerKind kind)
{
    return table_text(k_kind_name, CLEANER_KIND_COUNT, (int)kind);
}

const wchar_t *gui_cleaner_kind_note(CleanerKind kind)
{
    return table_text(k_kind_note, CLEANER_KIND_COUNT, (int)kind);
}

const wchar_t *gui_cleaner_group_text(CleanerGroup group)
{
    return table_text(k_group, CLEANER_GROUP_COUNT, (int)group);
}

const wchar_t *gui_cleaner_status_text(CleanerStatus status)
{
    return table_text(k_cleaner_status, CLEANER_STATUS_COUNT, (int)status);
}

const wchar_t *gui_clean_skip_text(CleanSkip skip)
{
    return table_text(k_skip, CLEAN_SKIP_COUNT, (int)skip);
}

const wchar_t *gui_dev_cache_label(DevCacheKind kind)
{
    return table_text(k_dev_label, DEV_CACHE_KIND_COUNT, (int)kind);
}

const wchar_t *gui_dev_cache_note(DevCacheKind kind)
{
    return table_text(k_dev_note, DEV_CACHE_KIND_COUNT, (int)kind);
}

const wchar_t *gui_dev_cache_status_text(DevCacheStatus status)
{
    return table_text(k_dev_status, DEV_CACHE_STATUS_COUNT, (int)status);
}

void gui_dev_cache_source_text(DevCacheSource source, const wchar_t *origin, wchar_t *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return;
    }
    switch (source) {
    case DEV_CACHE_SRC_ENV:
        swprintf(buf, len, ui_text(UI_T_D_SRC_ENV), origin != NULL ? origin : L"");
        break;
    case DEV_CACHE_SRC_CONFIG:
        swprintf(buf, len, ui_text(UI_T_D_SRC_CONFIG), origin != NULL ? origin : L"");
        break;
    default:
        swprintf(buf, len, L"%ls", ui_text(UI_T_D_SRC_DEFAULT));
        break;
    }
}

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
        if (k_recovery[i] == 0 || s[0] == L'\0' ||
            (ko && wcscmp(s, project_recovery_text((ProjectRecovery)i)) != 0)) {
            fwprintf(stderr, L"status-text self-test failed: recovery %d\n", i);
            failures++;
        }
    }
    /* Cleaner·개발 캐시: 모든 값에 문구가 있고, 한국어는 core 문구와 같다. */
#define CHECK_SAME(what, i, s, core)                                                                     \
    do {                                                                                                 \
        if ((s)[0] == L'\0' || (ko && wcscmp((s), (core)) != 0)) {                                       \
            fwprintf(stderr, L"status-text self-test failed: %hs %d\n", (what), (i));                    \
            failures++;                                                                                  \
        }                                                                                                \
    } while (0)
    for (i = 0; i < CLEANER_KIND_COUNT; i++) {
        const CleanerKindInfo *k = cleaner_kind_info((CleanerKind)i);
        CHECK_SAME("kind name", i, gui_cleaner_kind_name((CleanerKind)i), k->name);
        CHECK_SAME("kind note", i, gui_cleaner_kind_note((CleanerKind)i), k->note);
    }
    for (i = 0; i < CLEANER_GROUP_COUNT; i++) {
        CHECK_SAME("group", i, gui_cleaner_group_text((CleanerGroup)i), cleaner_group_text((CleanerGroup)i));
    }
    for (i = 0; i < CLEANER_STATUS_COUNT; i++) {
        CHECK_SAME("cleaner status", i, gui_cleaner_status_text((CleanerStatus)i),
                   cleaner_status_text((CleanerStatus)i));
    }
    for (i = 0; i < CLEAN_SKIP_COUNT; i++) {
        CHECK_SAME("skip", i, gui_clean_skip_text((CleanSkip)i), clean_skip_text((CleanSkip)i));
    }
    for (i = 0; i < DEV_CACHE_KIND_COUNT; i++) {
        const DevCacheKindInfo *k = dev_cache_kind_info((DevCacheKind)i);
        CHECK_SAME("dev label", i, gui_dev_cache_label((DevCacheKind)i), k->label);
        CHECK_SAME("dev note", i, gui_dev_cache_note((DevCacheKind)i), k->note);
    }
    for (i = 0; i < DEV_CACHE_STATUS_COUNT; i++) {
        CHECK_SAME("dev status", i, gui_dev_cache_status_text((DevCacheStatus)i),
                   dev_cache_status_text((DevCacheStatus)i));
    }
    for (i = 0; i <= (int)DEV_CACHE_SRC_CONFIG; i++) {
        wchar_t got[128], core[128];
        gui_dev_cache_source_text((DevCacheSource)i, L"X", got, 128);
        dev_cache_source_text((DevCacheSource)i, L"X", core, 128);
        CHECK_SAME("dev source", i, got, core);
    }
#undef CHECK_SAME
    return failures;
}
