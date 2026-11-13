#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <process.h>
#include <shellapi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "gui_quarantine.h"
#include "gui_progress.h"
#include "gui_theme.h"
#include "path.h"
#include "quarantine.h"
#include "size_format.h"
#include "ui_lang.h"

#define QUARANTINE_PANEL_CLASS L"ByteSieveQuarantinePanel"
#define WM_APP_PURGE_DONE (WM_APP + 1)
#define WM_APP_PURGE_PROGRESS (WM_APP + 2)

#define PROGRESS_INTERVAL_MS 100
#define PROGRESS_RANGE 1000

/* 확인 창·결과 창에 나열하는 최대 항목 수(나머지는 "외 n개"). */
#define CONFIRM_LIST_MAX 12
#define FAILURE_LIST_MAX 10

enum {
    IDC_Q_REFRESH = 100,
    IDC_Q_RESTORE,
    IDC_Q_PURGE,
    IDC_Q_CANCEL,
    IDC_Q_OPEN,
    IDC_Q_PROGRESS,
    IDC_Q_SUMMARY,
    IDC_Q_INFO,
    IDC_Q_LIST,
    IDC_Q_STATUS
};

/* 목록 열. */
enum { COL_STATE, COL_SIZE, COL_ORIGINAL, COL_TIME, COL_LOCATION, COL_NOTE, COL_COUNT };

typedef struct {
    HFONT font;
    int dpi;
} PanelParams;

/*
 * 결과 열 메모. 문장을 저장하지 않고 무엇이었는지만 기억해 두었다가 그릴 때 지금 언어로 만든다(note_text).
 * result 면 "머리말: 상태 - 시스템 설명", 아니면 text 문구 하나.
 */
typedef struct {
    int set;              /* 0 이면 메모 없음 */
    UiText text;          /* 문구 또는 머리말 */
    int result;
    QuarantineStatus st;  /* result 일 때 */
    uint32_t code;        /* result 일 때 Windows 오류(0 이면 시스템 설명 없음) */
} QNote;

/* 상태 줄에 보인 내용의 종류(언어를 바꾸면 render_status 가 새 언어로 다시 쓴다). */
typedef enum {
    Q_STATUS_TEXT,   /* 인자 없는 문구(status_text, UI_T_COUNT 면 빈 줄) */
    Q_STATUS_COUNTS, /* %u 인자들을 넣는 문구(status_text, status_n) */
    Q_STATUS_PURGING /* 영구 삭제 진행(작업에서 읽음) */
} QStatusKind;

/* 다른 이름으로 복원할 때 붙는 접미사. quarantine_alternate_path 가 만드는 파일 이름 글자라 번역하지 않는다
 * (quarantine_panel_verify 가 core 결과와 같은지 확인). */
#define RESTORED_SUFFIX L" (복원됨)"

/*
 * 영구 삭제 작업(스레드가 끝날 때까지 entries 는 바뀌지 않는다). 진행 값은 작업 스레드가
 * 쓰고 UI 스레드가 읽는다. 취소는 지우고 있던 파일까지만 지우고 멈춘다.
 */
typedef struct {
    HANDLE thread;
    HWND hwnd;
    const QuarantineEntry *entries; /* 패널 목록의 항목들 */
    size_t *rows;
    size_t count;
    QuarantineStatus *status;
    uint32_t *code;
    uint64_t total_bytes; /* 고른 항목의 기록된 용량 합 */

    volatile LONG cancel;
    volatile LONG current;  /* 지우고 있는 항목(rows 의 위치) */
    volatile LONG done;     /* 끝난 항목 수 */
    volatile LONG64 removed; /* 지운 파일·폴더 수 */
    volatile LONG64 bytes;   /* 지운 파일 용량 */
    ULONGLONG last_notify;   /* 작업 스레드만 쓴다 */
} PurgeJob;

typedef struct {
    HWND hwnd;
    HWND refresh, restore, purge, cancel, open, summary, progress, info, list, status;
    HFONT font;
    int dpi;
    int progress_pos;
    int purge_finished; /* 마지막 영구 삭제가 취소 없이 끝났으면 1(자체 검사용) */

    wchar_t *root; /* NULL 이면 모든 드라이브 */
    QuarantineList entries;
    QNote *notes; /* 항목별 마지막 작업 결과(entries.len 개, NULL 가능) */
    size_t unreadable; /* 읽지 못한 드라이브 수 */
    size_t conflicts;  /* 마지막 복원에서 같은 이름 때문에 건너뛴 수 */

    /* 단추 폭(지금 글꼴로 잰 문구 폭, 예전 고정 폭이 최소): 새로 고침·복원·영구 삭제·취소·열기 */
    int w_refresh, w_restore, w_purge, w_cancel, w_open;

    /* 상태 줄(render_status) */
    QStatusKind status_kind;
    UiText status_text;
    unsigned status_n[4];

    PurgeJob *job;

    wchar_t cell[MAX_PATH + 512]; /* LVN_GETDISPINFO 용 */
} QuarantinePanel;

/* 작업 결과를 목록을 다시 읽은 뒤에도 붙이려고 항목 폴더 경로로 기억한다. */
typedef struct {
    wchar_t *entry;
    QNote note;
} PendingNote;

static QuarantinePanel *panel_of(HWND hwnd)
{
    return (QuarantinePanel *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
}

static int pscale(const QuarantinePanel *p, int px)
{
    return MulDiv(px, p->dpi, 96);
}

/* ---- 표시 문자열 ---- */

static const wchar_t *state_label(QuarantineEntryState state)
{
    switch (state) {
    case QUARANTINE_ENTRY_OK:
        return ui_text(UI_T_Q_STATE_OK);
    case QUARANTINE_ENTRY_MISSING:
        return ui_text(UI_T_Q_STATE_MISSING);
    default:
        return ui_text(UI_T_Q_STATE_ORPHAN);
    }
}

/* 작업 결과 문구(QuarantineStatus 순서). 분류는 core 의 quarantine_status_text 와 같다. */
static const UiText k_status_text[QUARANTINE_STATUS_COUNT] = {
    UI_T_Q_ST_OK,       UI_T_Q_ST_MISSING,  UI_T_Q_ST_CONFLICT,     UI_T_Q_ST_ACCESS_DENIED,
    UI_T_Q_ST_IN_USE,   UI_T_Q_ST_NO_SPACE, UI_T_Q_ST_BAD_STORE,    UI_T_Q_ST_OTHER_VOLUME,
    UI_T_Q_ST_FAILED,   UI_T_Q_ST_CANCELLED};

static const wchar_t *status_text(QuarantineStatus st)
{
    return (unsigned)st < (unsigned)QUARANTINE_STATUS_COUNT ? ui_text(k_status_text[st]) : L"";
}

static void time_text(uint64_t ft, wchar_t *buf, size_t len)
{
    FILETIME utc, local;
    SYSTEMTIME st;

    buf[0] = L'\0';
    if (ft == 0) {
        return;
    }
    utc.dwLowDateTime = (DWORD)ft;
    utc.dwHighDateTime = (DWORD)(ft >> 32);
    if (FileTimeToLocalFileTime(&utc, &local) && FileTimeToSystemTime(&local, &st)) {
        swprintf(buf, len, L"%04u-%02u-%02u %02u:%02u:%02u", st.wYear, st.wMonth, st.wDay,
                 st.wHour, st.wMinute, st.wSecond);
    }
}

/* 결과 메모: "머리말: 상태 - 시스템 설명"(그릴 때 note_text 가 문장을 만든다). */
static QNote result_note(UiText head, QuarantineStatus st, uint32_t code)
{
    QNote n;

    memset(&n, 0, sizeof(n));
    n.set = 1;
    n.text = head;
    n.result = 1;
    n.st = st;
    n.code = code;
    return n;
}

static QNote text_note(UiText id)
{
    QNote n;

    memset(&n, 0, sizeof(n));
    n.set = 1;
    n.text = id;
    return n;
}

/*
 * 메모를 지금 언어로 buf 에 쓴다: "복원하지 못함: 접근 거부 ... - (시스템 설명)". 시스템 설명은 앱 언어로
 * 먼저 찾고 그 언어 리소스가 없으면 Windows 기본 언어로 쓴다. with_system 이 0 이면 시스템 설명을 뺀다
 * (자체 검사가 번역 문구만 확인할 때).
 */
static void note_text(const QNote *note, int with_system, wchar_t *buf, size_t len)
{
    wchar_t why[160];
    DWORD n = 0;

    buf[0] = L'\0';
    if (note == NULL || !note->set) {
        return;
    }
    if (!note->result) {
        swprintf(buf, len, L"%ls", ui_text(note->text));
        return;
    }
    if (with_system && note->code != 0 && note->st != QUARANTINE_CONFLICT) {
        n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, note->code,
                           ui_lang_langid(ui_lang_current()), why, 160, NULL);
        if (n == 0) {
            n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                               note->code, 0, why, 160, NULL);
        }
    }
    while (n > 0 && (why[n - 1] == L'\r' || why[n - 1] == L'\n' || why[n - 1] == L'.' ||
                     why[n - 1] == L' ')) {
        n--;
    }
    why[n] = L'\0';
    if (n > 0) {
        swprintf(buf, len, L"%ls: %ls - %ls", ui_text(note->text), status_text(note->st), why);
    } else {
        swprintf(buf, len, L"%ls: %ls", ui_text(note->text), status_text(note->st));
    }
}

/* ---- 상태 줄 ---- */

/* 영구 삭제 진행 문구(Q_STATUS_PURGING). 작업이 없으면 빈 문자열. */
static void purge_text(const QuarantinePanel *p, wchar_t *buf, size_t len)
{
    const PurgeJob *job = p->job;
    const QuarantineEntry *e;
    wchar_t size[32];
    size_t current, done;

    buf[0] = L'\0';
    if (job == NULL || job->count == 0) {
        return;
    }
    done = (size_t)job->done;
    current = (size_t)job->current;
    e = &job->entries[job->rows[current < job->count ? current : job->count - 1]];
    format_size((uint64_t)job->bytes, size, 32);
    swprintf(buf, len, ui_text(UI_T_Q_PURGING),
             (unsigned)(done < job->count ? done + 1 : job->count), (unsigned)job->count,
             (unsigned long long)job->removed, size, e->original != NULL ? e->original : e->item);
}

/* 상태 줄 문구를 기억해 둔 종류·값과 지금 언어로 만든다. */
static void status_line(const QuarantinePanel *p, wchar_t *buf, size_t len)
{
    buf[0] = L'\0';
    switch (p->status_kind) {
    case Q_STATUS_TEXT:
        if (p->status_text < UI_T_COUNT) {
            swprintf(buf, len, L"%ls", ui_text(p->status_text));
        }
        break;
    case Q_STATUS_COUNTS:
        /* 문구마다 쓰는 %u 수가 다르다(남는 인자는 쓰이지 않음). */
        swprintf(buf, len, ui_text(p->status_text), p->status_n[0], p->status_n[1], p->status_n[2],
                 p->status_n[3]);
        break;
    case Q_STATUS_PURGING:
        purge_text(p, buf, len);
        break;
    }
}

static void render_status(QuarantinePanel *p)
{
    wchar_t text[MAX_PATH + 512];

    status_line(p, text, sizeof(text) / sizeof(text[0]));
    SetWindowTextW(p->status, text);
}

/* 인자 없는 문구를 상태 줄에 보인다(UI_T_COUNT 면 비운다). */
static void show_text(QuarantinePanel *p, UiText id)
{
    p->status_kind = Q_STATUS_TEXT;
    p->status_text = id;
    render_status(p);
}

/* %u 인자(최대 4개)를 넣는 문구를 상태 줄에 보인다. */
static void show_counts(QuarantinePanel *p, UiText id, size_t a, size_t b, size_t c, size_t d)
{
    p->status_kind = Q_STATUS_COUNTS;
    p->status_text = id;
    p->status_n[0] = (unsigned)a;
    p->status_n[1] = (unsigned)b;
    p->status_n[2] = (unsigned)c;
    p->status_n[3] = (unsigned)d;
    render_status(p);
}

/* ---- 목록 ---- */

static void free_notes(QuarantinePanel *p)
{
    free(p->notes);
    p->notes = NULL;
}

static size_t selected_count(const QuarantinePanel *p)
{
    return (size_t)ListView_GetSelectedCount(p->list);
}

static void update_buttons(QuarantinePanel *p)
{
    int busy = p->job != NULL, any = selected_count(p) > 0;

    EnableWindow(p->refresh, !busy);
    EnableWindow(p->restore, !busy && any);
    EnableWindow(p->purge, !busy && any);
    EnableWindow(p->cancel, busy && !p->job->cancel);
    EnableWindow(p->open, p->entries.len > 0);
}

static void update_summary(QuarantinePanel *p)
{
    wchar_t text[256], size[32];
    uint64_t bytes = 0;
    size_t i, ok = 0, other = 0;

    for (i = 0; i < p->entries.len; i++) {
        if (p->entries.items[i].state == QUARANTINE_ENTRY_OK) {
            ok++;
            bytes += p->entries.items[i].bytes;
        } else {
            other++;
        }
    }
    format_size(bytes, size, 32);
    if (other > 0) {
        swprintf(text, sizeof(text) / sizeof(text[0]), ui_text(UI_T_Q_SUMMARY_CHECK), (unsigned)ok,
                 size, (unsigned)other);
    } else {
        swprintf(text, sizeof(text) / sizeof(text[0]), ui_text(UI_T_Q_SUMMARY), (unsigned)ok, size);
    }
    SetWindowTextW(p->summary, text);
}

/* 목록을 다시 읽고, 기억해 둔 작업 결과를 항목 폴더 경로로 다시 붙인다. */
static void reload(QuarantinePanel *p, PendingNote *pending, size_t pending_len)
{
    size_t i, k;

    ListView_SetItemCountEx(p->list, 0, 0);
    free_notes(p);
    if (p->root != NULL) {
        quarantine_list_free(&p->entries);
        p->unreadable = quarantine_list_volume(p->root, &p->entries) == QUARANTINE_OK ? 0 : 1;
    } else {
        p->unreadable = quarantine_list_system(&p->entries);
    }
    if (p->entries.len > 0) {
        p->notes = calloc(p->entries.len, sizeof(*p->notes));
    }
    for (k = 0; k < pending_len && p->notes != NULL; k++) {
        for (i = 0; i < p->entries.len; i++) {
            if (pending[k].note.set && pending[k].entry != NULL &&
                _wcsicmp(p->entries.items[i].entry, pending[k].entry) == 0) {
                p->notes[i] = pending[k].note;
                break;
            }
        }
    }
    ListView_SetItemCountEx(p->list, (int)p->entries.len, 0);
    InvalidateRect(p->list, NULL, TRUE);
    update_summary(p);
    update_buttons(p);
}

static void free_pending(PendingNote *pending, size_t len)
{
    size_t i;

    if (pending == NULL) {
        return;
    }
    for (i = 0; i < len; i++) {
        free(pending[i].entry);
    }
    free(pending);
}

/* 선택한 행 번호들(malloc). */
static size_t *selected_rows(const QuarantinePanel *p, size_t *count)
{
    size_t n = selected_count(p), k = 0;
    size_t *rows = n > 0 ? malloc(n * sizeof(*rows)) : NULL;
    int row;

    *count = 0;
    if (rows == NULL) {
        return NULL;
    }
    for (row = ListView_GetNextItem(p->list, -1, LVNI_SELECTED); row >= 0 && k < n;
         row = ListView_GetNextItem(p->list, row, LVNI_SELECTED)) {
        if ((size_t)row < p->entries.len) {
            rows[k++] = (size_t)row;
        }
    }
    *count = k;
    return rows;
}

/* ---- 복원 ---- */

static void show_box(QuarantinePanel *p, const wchar_t *text, const wchar_t *title, UINT icon)
{
    MessageBoxW(GetAncestor(p->hwnd, GA_ROOT), text, title, MB_OK | icon);
}

/*
 * 같은 이름 때문에 복원하지 못한 항목에 대해 처리 방법을 안내하고, 다른 이름으로 복원할지 묻는다.
 * [예]면 1.
 */
static int ask_alternate(QuarantinePanel *p, const size_t *rows, size_t count)
{
    size_t cap = 2048 + (size_t)CONFIRM_LIST_MAX * (MAX_PATH + 16), used = 0, i;
    wchar_t *text = malloc(cap * sizeof(*text));
    int n, answer;

    if (text == NULL) {
        return 0;
    }
    n = swprintf(text, cap, ui_text(UI_T_Q_CONFLICT_HEAD), (unsigned)count);
    used = n > 0 ? (size_t)n : 0;
    for (i = 0; i < count && i < CONFIRM_LIST_MAX; i++) {
        n = swprintf(text + used, cap - used, L"%ls\n", p->entries.items[rows[i]].restore);
        used += n > 0 ? (size_t)n : 0;
    }
    if (count > CONFIRM_LIST_MAX) {
        n = swprintf(text + used, cap - used, ui_text(UI_T_Q_MORE), (unsigned)(count - CONFIRM_LIST_MAX));
        used += n > 0 ? (size_t)n : 0;
        n = swprintf(text + used, cap - used, L"\n");
        used += n > 0 ? (size_t)n : 0;
    }
    swprintf(text + used, cap - used, ui_text(UI_T_Q_CONFLICT_TAIL), RESTORED_SUFFIX);
    answer = MessageBoxW(GetAncestor(p->hwnd, GA_ROOT), text, ui_text(UI_T_Q_CONFLICT_TITLE),
                         MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
    free(text);
    return answer == IDYES;
}

static void show_failures(QuarantinePanel *p, UiText title, UiText head, PendingNote *pending,
                          size_t len, size_t failed)
{
    size_t cap = 1024 + (size_t)FAILURE_LIST_MAX * (MAX_PATH + 600), used = 0, i, shown = 0;
    wchar_t *text = malloc(cap * sizeof(*text)), note[512];
    int n;

    if (text == NULL) {
        return;
    }
    n = swprintf(text, cap, L"%ls\n\n", ui_text(head));
    used = n > 0 ? (size_t)n : 0;
    for (i = 0; i < len && shown < FAILURE_LIST_MAX; i++) {
        if (!pending[i].note.set || pending[i].entry == NULL) {
            continue;
        }
        note_text(&pending[i].note, 1, note, sizeof(note) / sizeof(note[0]));
        n = swprintf(text + used, cap - used, L"%ls\n    %ls\n", pending[i].entry, note);
        used += n > 0 ? (size_t)n : 0;
        shown++;
    }
    if (failed > shown) {
        swprintf(text + used, cap - used, ui_text(UI_T_Q_MORE), (unsigned)(failed - shown));
    }
    show_box(p, text, ui_text(title), MB_ICONWARNING);
    free(text);
}

size_t quarantine_panel_restore(HWND panel, int interactive)
{
    QuarantinePanel *p = panel_of(panel);
    size_t count = 0, i, restored = 0, failed = 0, conflicts = 0, skipped = 0, pending_len = 0;
    size_t *rows, *conflict_rows = NULL;
    PendingNote *pending = NULL;

    if (p == NULL || p->job != NULL) {
        return 0;
    }
    rows = selected_rows(p, &count);
    if (rows == NULL || count == 0) {
        free(rows);
        show_text(p, UI_T_Q_SELECT_RESTORE);
        return 0;
    }
    conflict_rows = malloc(count * sizeof(*conflict_rows));
    pending = calloc(count, sizeof(*pending));
    if (conflict_rows == NULL || pending == NULL) {
        free(rows);
        free(conflict_rows);
        free(pending);
        return 0;
    }
    for (i = 0; i < count; i++) {
        const QuarantineEntry *e = &p->entries.items[rows[i]];
        QuarantineStatus st;
        uint32_t code = 0;

        if (e->state != QUARANTINE_ENTRY_OK) {
            /* 기록이 없거나 파일이 없는 항목은 복원할 수 없다(그대로 둔다). */
            pending[pending_len].entry = path_dup(e->entry);
            pending[pending_len++].note = text_note(
                e->state == QUARANTINE_ENTRY_ORPHAN ? UI_T_Q_NOTE_NO_ORIGIN : UI_T_Q_NOTE_NO_FILE);
            skipped++;
            continue;
        }
        st = quarantine_restore(e, NULL, &code);
        if (st == QUARANTINE_OK) {
            restored++;
        } else if (st == QUARANTINE_CONFLICT) {
            conflict_rows[conflicts++] = rows[i];
        } else {
            pending[pending_len].entry = path_dup(e->entry);
            pending[pending_len++].note = result_note(UI_T_Q_HEAD_RESTORE_FAILED, st, code);
            failed++;
        }
    }

    /* 같은 이름: 안내하고, 원하면 다른 이름으로 복원한다. */
    if (conflicts > 0 && interactive && ask_alternate(p, conflict_rows, conflicts)) {
        for (i = 0; i < conflicts; i++) {
            const QuarantineEntry *e = &p->entries.items[conflict_rows[i]];
            wchar_t *alt = quarantine_alternate_path(e->restore, e->is_dir);
            uint32_t code = 0;
            QuarantineStatus st = alt != NULL ? quarantine_restore(e, alt, &code)
                                              : QUARANTINE_FAILED;
            free(alt);
            if (st == QUARANTINE_OK) {
                restored++;
            } else {
                pending[pending_len].entry = path_dup(e->entry);
                pending[pending_len++].note = result_note(UI_T_Q_HEAD_ALT_FAILED, st, code);
                failed++;
            }
        }
        conflicts = 0;
    }
    for (i = 0; i < conflicts; i++) {
        pending[pending_len].entry = path_dup(p->entries.items[conflict_rows[i]].entry);
        pending[pending_len++].note = text_note(UI_T_Q_NOTE_SKIPPED);
    }
    p->conflicts = conflicts;

    if (interactive && failed > 0) {
        show_failures(p, UI_T_Q_RESTORE_RESULT_TITLE, UI_T_Q_RESTORE_RESULT_HEAD, pending,
                      pending_len, failed + skipped);
    }
    reload(p, pending, pending_len);
    show_counts(p, UI_T_Q_RESTORED, restored, conflicts, failed, skipped);
    free_pending(pending, pending_len);
    free(conflict_rows);
    free(rows);
    return restored;
}

/* ---- 영구 삭제 ---- */

/* 지운 것을 세고, 최대 PROGRESS_INTERVAL_MS 마다 한 번 화면에 알린다. 취소했으면 1. */
static int on_purge_step(uint64_t bytes, void *ctx)
{
    PurgeJob *job = ctx;
    ULONGLONG now = GetTickCount64();

    InterlockedIncrement64(&job->removed);
    InterlockedAdd64(&job->bytes, (LONG64)bytes);
    if (now - job->last_notify >= PROGRESS_INTERVAL_MS) {
        job->last_notify = now;
        PostMessageW(job->hwnd, WM_APP_PURGE_PROGRESS, 0, 0);
    }
    return job->cancel != 0;
}

static unsigned __stdcall purge_thread(void *arg)
{
    PurgeJob *job = arg;
    size_t i;

    for (i = 0; i < job->count; i++) {
        if (job->cancel) {
            /* 시작하지 않은 항목은 그대로 둔다(code 0 = 아무것도 지우지 않음). */
            job->status[i] = QUARANTINE_CANCELLED;
            job->code[i] = 0;
            continue;
        }
        InterlockedExchange(&job->current, (LONG)i);
        PostMessageW(job->hwnd, WM_APP_PURGE_PROGRESS, 0, 0);
        job->status[i] = quarantine_purge_with(&job->entries[job->rows[i]], on_purge_step, job,
                                               &job->code[i]);
        InterlockedIncrement(&job->done);
    }
    PostMessageW(job->hwnd, WM_APP_PURGE_DONE, 0, 0);
    return 0;
}

/*
 * 진행바와 상태 줄: 끝난 항목 수와 기록된 용량 대비 지운 용량 중 큰 쪽으로 채운다. 기록된
 * 용량이 실제보다 작을 수 있으므로 용량 기준은 지우고 있는 항목의 구간 끝을 넘지 않는다.
 */
static void update_purge_progress(QuarantinePanel *p)
{
    PurgeJob *job = p->job;
    uint64_t bytes;
    size_t done;
    int pos;

    if (job == NULL || job->cancel) {
        return;
    }
    bytes = (uint64_t)job->bytes;
    done = (size_t)job->done;
    pos = (int)(done * PROGRESS_RANGE / job->count);
    if (job->total_bytes > 0 && done < job->count) {
        int limit = (int)((done + 1) * PROGRESS_RANGE / job->count) - 1;
        int by_bytes = bytes >= job->total_bytes
                           ? PROGRESS_RANGE
                           : (int)((double)bytes * PROGRESS_RANGE / (double)job->total_bytes);
        if (by_bytes > limit) {
            by_bytes = limit;
        }
        if (by_bytes > pos) {
            pos = by_bytes;
        }
    }
    if (pos > p->progress_pos) {
        p->progress_pos = pos;
        progress_set(p->progress, PROGRESS_RUNNING, pos, PROGRESS_RANGE);
    }
    p->status_kind = Q_STATUS_PURGING;
    render_status(p);
}

static void cancel_purge(QuarantinePanel *p)
{
    if (p->job == NULL || p->job->cancel) {
        return;
    }
    InterlockedExchange(&p->job->cancel, 1);
    update_buttons(p);
    show_text(p, UI_T_Q_STOPPING);
}

static void job_free(PurgeJob *job)
{
    if (job != NULL) {
        free(job->rows);
        free(job->status);
        free(job->code);
        free(job);
    }
}

/* 두 번 확인한다: 대상 목록·용량·복구 불가 안내 → 마지막 확인. 둘 다 [예]면 1. */
static int confirm_purge(QuarantinePanel *p, const size_t *rows, size_t count)
{
    size_t cap = 2048 + (size_t)CONFIRM_LIST_MAX * (MAX_PATH + 64), used = 0, i, orphans = 0;
    wchar_t *text = malloc(cap * sizeof(*text)), size[32];
    uint64_t bytes = 0;
    int n, answer;

    if (text == NULL) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        const QuarantineEntry *e = &p->entries.items[rows[i]];
        bytes += e->bytes;
        orphans += e->state == QUARANTINE_ENTRY_ORPHAN;
    }
    format_size(bytes, size, 32);
    n = swprintf(text, cap, ui_text(UI_T_Q_PURGE_CONFIRM_HEAD), (unsigned)count, size);
    used = n > 0 ? (size_t)n : 0;
    for (i = 0; i < count && i < CONFIRM_LIST_MAX; i++) {
        const QuarantineEntry *e = &p->entries.items[rows[i]];
        format_size(e->bytes, size, 32);
        n = swprintf(text + used, cap - used, L"%ls   %ls\n", size,
                     e->original != NULL ? e->original : e->item);
        used += n > 0 ? (size_t)n : 0;
    }
    if (count > CONFIRM_LIST_MAX) {
        n = swprintf(text + used, cap - used, ui_text(UI_T_Q_MORE), (unsigned)(count - CONFIRM_LIST_MAX));
        used += n > 0 ? (size_t)n : 0;
        n = swprintf(text + used, cap - used, L"\n");
        used += n > 0 ? (size_t)n : 0;
    }
    n = swprintf(text + used, cap - used, L"%ls", ui_text(UI_T_Q_PURGE_CONFIRM_WARN));
    used += n > 0 ? (size_t)n : 0;
    if (orphans > 0) {
        n = swprintf(text + used, cap - used, ui_text(UI_T_Q_PURGE_CONFIRM_ORPHANS), (unsigned)orphans);
        used += n > 0 ? (size_t)n : 0;
    }
    swprintf(text + used, cap - used, L"%ls", ui_text(UI_T_Q_PURGE_CONFIRM_ASK));
    answer = MessageBoxW(GetAncestor(p->hwnd, GA_ROOT), text, ui_text(UI_T_Q_PURGE_TITLE),
                         MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    if (answer == IDYES) {
        format_size(bytes, size, 32);
        swprintf(text, cap, ui_text(UI_T_Q_PURGE_FINAL), (unsigned)count, size);
        answer = MessageBoxW(GetAncestor(p->hwnd, GA_ROOT), text, ui_text(UI_T_Q_PURGE_FINAL_TITLE),
                             MB_YESNO | MB_ICONSTOP | MB_DEFBUTTON2);
    }
    free(text);
    return answer == IDYES;
}

int quarantine_panel_purge(HWND panel, int confirm)
{
    QuarantinePanel *p = panel_of(panel);
    PurgeJob *job;
    size_t count = 0, *rows;

    if (p == NULL || p->job != NULL) {
        return 0;
    }
    rows = selected_rows(p, &count);
    if (rows == NULL || count == 0) {
        free(rows);
        show_text(p, UI_T_Q_SELECT_PURGE);
        return 0;
    }
    if (confirm && !confirm_purge(p, rows, count)) {
        free(rows);
        show_text(p, UI_T_Q_PURGE_CANCELLED);
        return 0;
    }
    job = calloc(1, sizeof(*job));
    if (job != NULL) {
        size_t i;
        job->hwnd = panel;
        job->entries = p->entries.items;
        job->rows = rows;
        job->count = count;
        job->status = calloc(count, sizeof(*job->status));
        job->code = calloc(count, sizeof(*job->code));
        for (i = 0; i < count; i++) {
            job->total_bytes += p->entries.items[rows[i]].bytes;
        }
    }
    if (job == NULL || job->status == NULL || job->code == NULL) {
        if (job == NULL) {
            free(rows);
        }
        job_free(job);
        show_text(p, UI_T_Q_PURGE_NO_MEMORY);
        return 0;
    }
    job->thread = (HANDLE)_beginthreadex(NULL, 0, purge_thread, job, 0, NULL);
    if (job->thread == NULL) {
        job_free(job);
        show_text(p, UI_T_Q_PURGE_NO_THREAD);
        return 0;
    }
    p->job = job;
    p->purge_finished = 0;
    p->progress_pos = 0;
    progress_set(p->progress, PROGRESS_IDLE, 0, PROGRESS_RANGE);
    update_buttons(p);
    update_purge_progress(p);
    return 1;
}

static void finish_purge(QuarantinePanel *p, int interactive)
{
    PurgeJob *job = p->job;
    PendingNote *pending;
    size_t i, done = 0, failed = 0, cancelled = 0, len = 0;

    if (job == NULL) {
        return;
    }
    WaitForSingleObject(job->thread, INFINITE);
    CloseHandle(job->thread);
    p->job = NULL;
    pending = calloc(job->count > 0 ? job->count : 1, sizeof(*pending));
    /* 실패를 먼저 모은다(실패 창은 앞의 failed 개만 보여 준다). 취소는 결과 열에만 남긴다. */
    for (i = 0; i < job->count; i++) {
        QuarantineStatus st = job->status[i];
        if (st == QUARANTINE_OK) {
            done++;
        } else if (st == QUARANTINE_CANCELLED) {
            cancelled++;
        } else {
            failed++;
            if (pending != NULL) {
                pending[len].entry = path_dup(job->entries[job->rows[i]].entry);
                pending[len++].note = result_note(UI_T_Q_HEAD_PURGE_FAILED, st, job->code[i]);
            }
        }
    }
    for (i = 0; i < job->count && pending != NULL; i++) {
        if (job->status[i] == QUARANTINE_CANCELLED) {
            pending[len].entry = path_dup(job->entries[job->rows[i]].entry);
            pending[len++].note = text_note(job->code[i] != 0 ? UI_T_Q_NOTE_CANCEL_PARTIAL
                                                              : UI_T_Q_NOTE_CANCEL_NONE);
        }
    }
    p->purge_finished = cancelled == 0;
    /* 일부라도 실패하면 빨간색, 취소하면 멈춘 자리에서 노란색(예전에는 0 으로 되돌려 사라져 보였다). */
    progress_set(p->progress,
                 failed > 0 ? PROGRESS_FAILED : cancelled > 0 ? PROGRESS_CANCELLED : PROGRESS_DONE, 0,
                 PROGRESS_RANGE);
    p->progress_pos = (int)SendMessageW(p->progress, PBM_GETPOS, 0, 0);
    if (interactive && failed > 0 && pending != NULL) {
        show_failures(p, UI_T_Q_PURGE_RESULT_TITLE, UI_T_Q_PURGE_RESULT_HEAD, pending, failed, failed);
    }
    job_free(job);
    reload(p, pending, len);
    if (cancelled > 0) {
        show_counts(p, UI_T_Q_PURGE_STOPPED, done, cancelled, failed, 0);
    } else {
        show_counts(p, UI_T_Q_PURGED, done, failed, 0, 0);
    }
    free_pending(pending, len);
    SendMessageW(GetParent(p->hwnd), WM_COMMAND,
                 MAKEWPARAM(GetDlgCtrlID(p->hwnd), QPN_PURGED), (LPARAM)p->hwnd);
}

/* 선택한 항목(없으면 첫 항목)의 항목 폴더를 탐색기로 연다. */
static void open_location(QuarantinePanel *p)
{
    int row = ListView_GetNextItem(p->list, -1, LVNI_SELECTED);
    const QuarantineEntry *e;
    const wchar_t *target;

    if (p->entries.len == 0) {
        return;
    }
    e = &p->entries.items[row >= 0 && (size_t)row < p->entries.len ? (size_t)row : 0];
    target = e->state == QUARANTINE_ENTRY_ORPHAN && !e->is_dir ? e->store : e->entry;
    ShellExecuteW(GetAncestor(p->hwnd, GA_ROOT), L"open", target, NULL, NULL, SW_SHOWNORMAL);
}

/* ---- 가상 목록 ---- */

static void on_get_dispinfo(QuarantinePanel *p, NMLVDISPINFOW *di)
{
    const QuarantineEntry *e;
    size_t row = (size_t)di->item.iItem;
    size_t cell_len = sizeof(p->cell) / sizeof(p->cell[0]);

    if (row >= p->entries.len || !(di->item.mask & LVIF_TEXT)) {
        return;
    }
    e = &p->entries.items[row];
    switch (di->item.iSubItem) {
    case COL_STATE:
        di->item.pszText = (wchar_t *)state_label(e->state);
        break;
    case COL_SIZE:
        p->cell[0] = L'\0';
        if (e->state != QUARANTINE_ENTRY_ORPHAN || !e->is_dir) {
            format_size(e->bytes, p->cell, 64);
        }
        di->item.pszText = p->cell;
        break;
    case COL_ORIGINAL:
        di->item.pszText = e->restore != NULL ? e->restore : (wchar_t *)ui_text(UI_T_Q_UNKNOWN);
        break;
    case COL_TIME:
        time_text(e->time, p->cell, cell_len);
        di->item.pszText = p->cell;
        break;
    case COL_LOCATION:
        di->item.pszText = e->item != NULL ? e->item : e->entry;
        break;
    case COL_NOTE:
        note_text(p->notes != NULL ? &p->notes[row] : NULL, 1, p->cell, cell_len);
        di->item.pszText = p->cell;
        break;
    }
}

/* ---- 레이아웃·생성 ---- */

static void panel_layout(QuarantinePanel *p)
{
    RECT rc;
    int h = pscale(p, 24), gap = pscale(p, 6);
    int w, y, x, list_h;

    GetClientRect(p->hwnd, &rc);
    w = rc.right - rc.left;
    y = 0;
    x = 0;
    MoveWindow(p->refresh, x, y, p->w_refresh, h, TRUE);
    x += p->w_refresh + gap;
    MoveWindow(p->restore, x, y, p->w_restore, h, TRUE);
    x += p->w_restore + gap;
    MoveWindow(p->purge, x, y, p->w_purge, h, TRUE);
    x += p->w_purge + gap;
    MoveWindow(p->cancel, x, y, p->w_cancel, h, TRUE);
    x += p->w_cancel + gap;
    MoveWindow(p->open, x, y, p->w_open, h, TRUE);
    x += p->w_open + gap * 2;
    MoveWindow(p->summary, x, y + pscale(p, 4), w - x > 0 ? w - x : 0, pscale(p, 20), TRUE);
    y += h + gap;
    MoveWindow(p->progress, 0, y, w, pscale(p, 16), TRUE);
    y += pscale(p, 16) + gap;
    MoveWindow(p->info, 0, y, w, pscale(p, 20), TRUE);
    y += pscale(p, 20);
    MoveWindow(p->status, 0, y, w, pscale(p, 20), TRUE);
    y += pscale(p, 20) + gap;
    list_h = rc.bottom - y;
    MoveWindow(p->list, 0, y, w, list_h > 0 ? list_h : 0, TRUE);
}

static HWND panel_child(QuarantinePanel *p, const wchar_t *cls, const wchar_t *text, DWORD style,
                        DWORD ex, int id)
{
    HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, p->hwnd,
                             (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)p->font, FALSE);
    return h;
}

/* 목록 열: 머리글 문구, 96 DPI 기준 기본 폭(머리글이 더 길면 넓힌다), 정렬. */
static const struct {
    UiText title;
    int width;
    int fmt;
} k_cols[COL_COUNT] = {
    {UI_T_Q_COL_STATE, 150, LVCFMT_LEFT},    {UI_T_Q_COL_SIZE, 90, LVCFMT_RIGHT},
    {UI_T_Q_COL_ORIGINAL, 360, LVCFMT_LEFT}, {UI_T_Q_COL_TIME, 140, LVCFMT_LEFT},
    {UI_T_Q_COL_LOCATION, 300, LVCFMT_LEFT}, {UI_T_Q_COL_RESULT, 300, LVCFMT_LEFT},
};

/* 단추와 문구(왼쪽부터). */
static void button_texts(QuarantinePanel *p, HWND *h, UiText *t)
{
    h[0] = p->refresh, t[0] = UI_T_Q_REFRESH;
    h[1] = p->restore, t[1] = UI_T_Q_RESTORE;
    h[2] = p->purge, t[2] = UI_T_Q_PURGE;
    h[3] = p->cancel, t[3] = UI_T_Q_CANCEL;
    h[4] = p->open, t[4] = UI_T_Q_OPEN;
}

/* 열 폭: 기본 폭과, 머리글 문구가 잘리지 않는 폭 중 큰 값. */
static int column_width(QuarantinePanel *p, int col)
{
    int need = theme_text_width(p->list, p->font, ui_text(k_cols[col].title)) + pscale(p, 16);
    return pscale(p, k_cols[col].width) > need ? pscale(p, k_cols[col].width) : need;
}

/* 단추 폭: 예전 고정 폭(84·120)을 최소로 두고(한국어 화면은 그대로), 지금 글꼴로 잰 문구가 더 길면 넓힌다. */
static void measure_buttons(QuarantinePanel *p)
{
    p->w_refresh = theme_button_width(p->hwnd, p->font, ui_text(UI_T_Q_REFRESH), p->dpi, pscale(p, 84));
    p->w_restore = theme_button_width(p->hwnd, p->font, ui_text(UI_T_Q_RESTORE), p->dpi, pscale(p, 84));
    p->w_purge = theme_button_width(p->hwnd, p->font, ui_text(UI_T_Q_PURGE), p->dpi, pscale(p, 120));
    p->w_cancel = theme_button_width(p->hwnd, p->font, ui_text(UI_T_Q_CANCEL), p->dpi, pscale(p, 84));
    p->w_open = theme_button_width(p->hwnd, p->font, ui_text(UI_T_Q_OPEN), p->dpi, pscale(p, 120));
}

static void panel_create_controls(QuarantinePanel *p)
{
    size_t i;

    p->refresh = panel_child(p, L"BUTTON", ui_text(UI_T_Q_REFRESH), BS_PUSHBUTTON | WS_TABSTOP, 0,
                             IDC_Q_REFRESH);
    p->restore = panel_child(p, L"BUTTON", ui_text(UI_T_Q_RESTORE), BS_PUSHBUTTON | WS_TABSTOP, 0,
                             IDC_Q_RESTORE);
    p->purge = panel_child(p, L"BUTTON", ui_text(UI_T_Q_PURGE), BS_PUSHBUTTON | WS_TABSTOP, 0,
                           IDC_Q_PURGE);
    p->cancel = panel_child(p, L"BUTTON", ui_text(UI_T_Q_CANCEL), BS_PUSHBUTTON | WS_TABSTOP, 0,
                            IDC_Q_CANCEL);
    p->open = panel_child(p, L"BUTTON", ui_text(UI_T_Q_OPEN), BS_PUSHBUTTON | WS_TABSTOP, 0,
                          IDC_Q_OPEN);
    p->summary = panel_child(p, L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, 0,
                             IDC_Q_SUMMARY);
    p->progress = panel_child(p, PROGRESS_CLASSW, L"", PBS_SMOOTH, 0, IDC_Q_PROGRESS);
    SendMessageW(p->progress, PBM_SETRANGE32, 0, PROGRESS_RANGE);
    p->info = panel_child(p, L"STATIC", ui_text(UI_T_Q_INFO), SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, 0,
                          IDC_Q_INFO);
    p->status = panel_child(p, L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, 0,
                            IDC_Q_STATUS);
    p->list = panel_child(p, WC_LISTVIEWW, L"",
                          LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS | WS_TABSTOP,
                          WS_EX_CLIENTEDGE, IDC_Q_LIST);
    ListView_SetExtendedListViewStyle(p->list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    for (i = 0; i < COL_COUNT; i++) {
        LVCOLUMNW col;
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        col.pszText = (wchar_t *)ui_text(k_cols[i].title);
        col.cx = column_width(p, (int)i);
        col.fmt = k_cols[i].fmt;
        SendMessageW(p->list, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&col);
    }
    measure_buttons(p);
    update_buttons(p);
}

static LRESULT CALLBACK panel_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    QuarantinePanel *p = panel_of(hwnd);

    switch (msg) {
    case WM_NCCREATE: {
        /* 상태는 창과 수명을 같이 한다(WM_NCDESTROY 에서 해제). */
        const PanelParams *params = ((CREATESTRUCTW *)lp)->lpCreateParams;
        p = calloc(1, sizeof(*p));
        if (p == NULL) {
            return FALSE;
        }
        p->hwnd = hwnd;
        p->font = params->font;
        p->dpi = params->dpi;
        p->status_kind = Q_STATUS_TEXT;
        p->status_text = UI_T_COUNT; /* 처음에는 빈 상태 줄 */
        quarantine_list_init(&p->entries);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)p);
        break;
    }
    case WM_CREATE:
        panel_create_controls(p);
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
        /* 입력 칸·콤보 상자 목록(어둡게일 때 SURFACE 색). */
        return theme_ctlcolor(hwnd, msg, wp, lp);
    case WM_CTLCOLORSTATIC:
        /* 정적 글자·체크 상자 배경을 창 배경색(theme BG)에 맞춘다. */
        return theme_ctlcolor_static((HDC)wp);
    case WM_SIZE:
        panel_layout(p);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_Q_REFRESH:
            quarantine_panel_refresh(hwnd);
            return 0;
        case IDC_Q_RESTORE:
            quarantine_panel_restore(hwnd, 1);
            return 0;
        case IDC_Q_PURGE:
            quarantine_panel_purge(hwnd, 1);
            return 0;
        case IDC_Q_CANCEL:
            cancel_purge(p);
            return 0;
        case IDC_Q_OPEN:
            open_location(p);
            return 0;
        }
        break;
    case WM_NOTIFY: {
        NMHDR *hdr = (NMHDR *)lp;
        if (p == NULL || hdr->idFrom != IDC_Q_LIST) {
            break;
        }
        if (hdr->code == LVN_GETDISPINFOW) {
            on_get_dispinfo(p, (NMLVDISPINFOW *)lp);
            return 0;
        }
        if (hdr->code == LVN_ITEMCHANGED || hdr->code == LVN_ODSTATECHANGED) {
            update_buttons(p);
            return 0;
        }
        break;
    }
    case WM_APP_PURGE_PROGRESS:
        if (p != NULL) {
            update_purge_progress(p);
        }
        return 0;
    case WM_APP_PURGE_DONE:
        if (p != NULL) {
            finish_purge(p, IsWindowVisible(hwnd));
        }
        return 0;
    case WM_DESTROY:
        /*
         * 진행 중인 영구 삭제는 멈추고, 지우고 있던 파일까지만 끝나기를 기다린다. 남은 파일과
         * 기록은 격리 폴더에 그대로 남아 다음에 다시 영구 삭제할 수 있다. (끝까지 기다리면 큰
         * 폴더에서 창이 응답 없음이 된다.)
         */
        if (p != NULL && p->job != NULL) {
            InterlockedExchange(&p->job->cancel, 1);
            WaitForSingleObject(p->job->thread, INFINITE);
            CloseHandle(p->job->thread);
            job_free(p->job);
            p->job = NULL;
        }
        return 0;
    case WM_NCDESTROY:
        if (p != NULL) {
            free_notes(p);
            quarantine_list_free(&p->entries);
            free(p->root);
            free(p);
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---- 공개 함수 ---- */

int quarantine_panel_register(HINSTANCE inst)
{
    WNDCLASSEXW wc;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = panel_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = theme_bg_brush();
    wc.lpszClassName = QUARANTINE_PANEL_CLASS;
    return RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS ? 0 : -1;
}

HWND quarantine_panel_create(HWND parent, int id, HFONT font, int dpi)
{
    PanelParams params;

    params.font = font;
    params.dpi = dpi > 0 ? dpi : 96;
    return CreateWindowExW(WS_EX_CONTROLPARENT, QUARANTINE_PANEL_CLASS, L"",
                           WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, 0, 0, parent,
                           (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), &params);
}

int quarantine_panel_refresh(HWND panel)
{
    QuarantinePanel *p = panel_of(panel);

    if (p == NULL || p->job != NULL) {
        return 0;
    }
    reload(p, NULL, 0);
    if (p->unreadable > 0) {
        show_counts(p, UI_T_Q_UNREADABLE, p->unreadable, 0, 0, 0);
    } else {
        show_text(p, p->entries.len > 0 ? UI_T_Q_SELECT_ITEMS : UI_T_Q_EMPTY);
    }
    return 1;
}

void quarantine_panel_set_root(HWND panel, const wchar_t *volume_root)
{
    QuarantinePanel *p = panel_of(panel);

    if (p == NULL || p->job != NULL) {
        return;
    }
    free(p->root);
    p->root = volume_root != NULL ? path_dup(volume_root) : NULL;
    quarantine_panel_refresh(panel);
}

int quarantine_panel_busy(HWND panel)
{
    QuarantinePanel *p = panel_of(panel);

    return p != NULL && p->job != NULL;
}

size_t quarantine_panel_select_all(HWND panel)
{
    QuarantinePanel *p = panel_of(panel);

    if (p == NULL) {
        return 0;
    }
    ListView_SetItemState(p->list, -1, LVIS_SELECTED, LVIS_SELECTED);
    update_buttons(p);
    return selected_count(p);
}

size_t quarantine_panel_conflicts(HWND panel)
{
    QuarantinePanel *p = panel_of(panel);

    return p != NULL ? p->conflicts : 0;
}

void quarantine_panel_apply_language(HWND panel, HFONT font)
{
    QuarantinePanel *p = panel_of(panel);
    HWND child, h[5];
    UiText t[5];
    int i;

    if (p == NULL) {
        return;
    }
    if (font != NULL) {
        p->font = font;
        for (child = GetWindow(p->hwnd, GW_CHILD); child != NULL; child = GetWindow(child, GW_HWNDNEXT)) {
            SendMessageW(child, WM_SETFONT, (WPARAM)font, FALSE);
        }
    }
    button_texts(p, h, t);
    for (i = 0; i < 5; i++) {
        SetWindowTextW(h[i], ui_text(t[i]));
    }
    SetWindowTextW(p->info, ui_text(UI_T_Q_INFO));
    for (i = 0; i < COL_COUNT; i++) {
        LVCOLUMNW col;
        int need = column_width(p, i);
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT;
        col.pszText = (wchar_t *)ui_text(k_cols[i].title);
        SendMessageW(p->list, LVM_SETCOLUMNW, (WPARAM)i, (LPARAM)&col);
        /* 새 머리글이 잘리지 않게 넓힌다(사용자가 넓힌 폭은 줄이지 않음). */
        if (ListView_GetColumnWidth(p->list, i) < need) {
            ListView_SetColumnWidth(p->list, i, need);
        }
    }
    measure_buttons(p);
    update_summary(p);
    render_status(p);
    panel_layout(p);
    /* 상태·결과 열은 그릴 때 지금 언어로 만든다(가상 목록). */
    InvalidateRect(p->list, NULL, TRUE);
    InvalidateRect(p->hwnd, NULL, TRUE);
}

int quarantine_panel_min_width(HWND panel)
{
    QuarantinePanel *p = panel_of(panel);

    if (p == NULL) {
        return 0;
    }
    /* 단추 다섯 개와 간격(요약은 남는 폭만 쓴다). */
    return p->w_refresh + p->w_restore + p->w_purge + p->w_cancel + p->w_open + pscale(p, 6) * 4;
}

#define PANEL_CHECK(cond)                                                            \
    do {                                                                             \
        if (!(cond)) {                                                               \
            fwprintf(stderr, L"quarantine self-test failed: %hs (line %d)\n", #cond, \
                     __LINE__);                                                      \
            failures++;                                                              \
        }                                                                            \
    } while (0)

int quarantine_panel_verify(HWND panel, size_t expect_rows, size_t expect_ok)
{
    QuarantinePanel *p = panel_of(panel);
    wchar_t buf[MAX_PATH + 64];
    size_t i, ok = 0;
    int failures = 0;

    PANEL_CHECK(p != NULL);
    if (p == NULL) {
        return failures;
    }
    PANEL_CHECK(p->job == NULL && IsWindowEnabled(p->refresh));
    PANEL_CHECK(p->entries.len == expect_rows);
    PANEL_CHECK(ListView_GetItemCount(p->list) == (int)expect_rows);
    for (i = 0; i < p->entries.len; i++) {
        const QuarantineEntry *e = &p->entries.items[i];
        ok += e->state == QUARANTINE_ENTRY_OK;
        ListView_GetItemText(p->list, (int)i, COL_STATE, buf, MAX_PATH + 64);
        PANEL_CHECK(wcscmp(buf, state_label(e->state)) == 0);
        if (e->state == QUARANTINE_ENTRY_OK) {
            ListView_GetItemText(p->list, (int)i, COL_ORIGINAL, buf, MAX_PATH + 64);
            PANEL_CHECK(e->restore != NULL && wcscmp(buf, e->restore) == 0);
            PANEL_CHECK(GetFileAttributesW(e->item) != INVALID_FILE_ATTRIBUTES);
        }
    }
    PANEL_CHECK(ok == expect_ok);
    GetWindowTextW(p->summary, buf, MAX_PATH + 64);
    {
        /* 요약은 지금 언어의 "격리 항목 n개 · 용량"(확인 필요가 있으면 그 수까지) */
        wchar_t want[MAX_PATH + 64], size[32];
        uint64_t bytes = 0;
        size_t other = 0;
        for (i = 0; i < p->entries.len; i++) {
            if (p->entries.items[i].state == QUARANTINE_ENTRY_OK) {
                bytes += p->entries.items[i].bytes;
            } else {
                other++;
            }
        }
        format_size(bytes, size, 32);
        if (other > 0) {
            swprintf(want, MAX_PATH + 64, ui_text(UI_T_Q_SUMMARY_CHECK), (unsigned)ok, size,
                     (unsigned)other);
        } else {
            swprintf(want, MAX_PATH + 64, ui_text(UI_T_Q_SUMMARY), (unsigned)ok, size);
        }
        PANEL_CHECK(wcscmp(buf, want) == 0);
    }
    /* 확인 창이 보여 주는 다른 이름 접미사가 core 가 실제로 만드는 이름과 같다. */
    {
        wchar_t *alt = quarantine_alternate_path(L"Z:\\bytesieve-no-such-dir\\x.txt", 0);
        PANEL_CHECK(alt != NULL && wcscmp(alt, L"Z:\\bytesieve-no-such-dir\\x" RESTORED_SUFFIX L".txt") == 0);
        free(alt);
    }
    PANEL_CHECK(IsWindowEnabled(p->open) == (p->entries.len > 0));
    PANEL_CHECK(!IsWindowEnabled(p->cancel));
    /* 영구 삭제가 끝났으면 진행바가 끝까지 찬다. */
    PANEL_CHECK(!p->purge_finished ||
                SendMessageW(p->progress, PBM_GETPOS, 0, 0) == PROGRESS_RANGE);
    PANEL_CHECK(!p->purge_finished || progress_state(p->progress) == PROGRESS_DONE ||
                progress_state(p->progress) == PROGRESS_FAILED);
    return failures;
}

/* 한글(가-힣)이 들어 있는지. */
static int has_hangul(const wchar_t *s)
{
    for (; *s != L'\0'; s++) {
        if (*s >= 0xAC00 && *s <= 0xD7A3) {
            return 1;
        }
    }
    return 0;
}

int quarantine_panel_verify_lang(HWND panel)
{
    QuarantinePanel *p = panel_of(panel);
    wchar_t buf[MAX_PATH + 512], want[MAX_PATH + 512];
    HWND h[5];
    UiText t[5];
    RECT r;
    size_t i;
    int failures = 0, other = ui_lang_current() != UI_LANG_KO;

    PANEL_CHECK(p != NULL);
    if (p == NULL) {
        return failures;
    }
    /* 단추: 지금 언어 문구이고 잘리지 않는다 */
    button_texts(p, h, t);
    for (i = 0; i < 5; i++) {
        GetWindowTextW(h[i], buf, (int)(sizeof(buf) / sizeof(buf[0])));
        PANEL_CHECK(wcscmp(buf, ui_text(t[i])) == 0);
        GetWindowRect(h[i], &r);
        PANEL_CHECK(r.right - r.left >= theme_button_width(p->hwnd, p->font, buf, p->dpi, 0));
    }
    GetWindowTextW(p->info, buf, (int)(sizeof(buf) / sizeof(buf[0])));
    PANEL_CHECK(wcscmp(buf, ui_text(UI_T_Q_INFO)) == 0);
    /* 열 머리글: 지금 언어이고 잘리지 않는다 */
    for (i = 0; i < COL_COUNT; i++) {
        LVCOLUMNW col;
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT;
        col.pszText = buf;
        col.cchTextMax = (int)(sizeof(buf) / sizeof(buf[0]));
        buf[0] = L'\0';
        SendMessageW(p->list, LVM_GETCOLUMNW, (WPARAM)i, (LPARAM)&col);
        PANEL_CHECK(wcscmp(buf, ui_text(k_cols[i].title)) == 0);
        PANEL_CHECK(ListView_GetColumnWidth(p->list, (int)i) > theme_text_width(p->list, p->font, buf));
    }
    /* 상태 줄: 기억해 둔 내용을 지금 언어로 쓴 것(영구 삭제 중에는 부르지 않음) */
    PANEL_CHECK(p->job == NULL);
    status_line(p, want, sizeof(want) / sizeof(want[0]));
    GetWindowTextW(p->status, buf, (int)(sizeof(buf) / sizeof(buf[0])));
    PANEL_CHECK(wcscmp(buf, want) == 0);
    if (other) {
        PANEL_CHECK(!has_hangul(buf));
    }
    /* 목록: 상태·결과 열이 지금 언어(결과 열의 Windows 오류 설명과 경로는 번역 대상이 아님) */
    for (i = 0; i < p->entries.len; i++) {
        ListView_GetItemText(p->list, (int)i, COL_STATE, buf, (int)(sizeof(buf) / sizeof(buf[0])));
        PANEL_CHECK(wcscmp(buf, state_label(p->entries.items[i].state)) == 0);
        ListView_GetItemText(p->list, (int)i, COL_NOTE, buf, (int)(sizeof(buf) / sizeof(buf[0])));
        note_text(p->notes != NULL ? &p->notes[i] : NULL, 1, want, sizeof(want) / sizeof(want[0]));
        PANEL_CHECK(wcscmp(buf, want) == 0);
        if (other) {
            note_text(p->notes != NULL ? &p->notes[i] : NULL, 0, want, sizeof(want) / sizeof(want[0]));
            PANEL_CHECK(!has_hangul(want) && !has_hangul(state_label(p->entries.items[i].state)));
        }
    }
    if (p->entries.len > 0) {
        GetWindowTextW(p->summary, buf, (int)(sizeof(buf) / sizeof(buf[0])));
        PANEL_CHECK(!other || !has_hangul(buf));
    }
    return failures;
}
