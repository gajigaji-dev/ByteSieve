#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "cleaner.h"
#include "cli_devenv.h"
#include "devenv_history.h"
#include "jsonl.h"
#include "scan_history.h"
#include "size_format.h"
#include "vdisk.h"

#define FT_DAY 864000000000ull

void cli_devenv_print_usage(FILE *out)
{
    fwprintf(out,
             L"Usage:\n"
             L"  disk-scanner devenv [--json | --jsonl] [--no-record]\n"
             L"  disk-scanner devenv -h | --help\n"
             L"\n"
             L"Shows developer caches (npm, pip, Gradle, Cargo, NuGet, pnpm, Yarn, Maven, Go, Conda)\n"
             L"and WSL2 / Docker virtual disks (.vhdx) with their size, the space deleting them\n"
             L"would free, and how much they grew since the last record. Read-only.\n"
             L"\n"
             L"Each run records the current sizes in devenv-history.tsv in the history folder\n"
             L"(BYTESIEVE_HISTORY_DIR, or %%LOCALAPPDATA%%\\ByteSieve\\history) unless --no-record\n"
             L"is given or history auto-save is turned off in the GUI.\n"
             L"\n"
             L"Virtual disks are never cleaned (that would delete the distribution or containers);\n"
             L"the commands to shrink them are shown instead. Clean caches with\n"
             L"'disk-scanner clean system --include <name>'.\n"
             L"\n"
             L"Exit codes: 0 done, 2 error.\n");
}

CliParseResult cli_devenv_parse_args(int argc, wchar_t **argv, CliDevenvOptions *opts,
                                     wchar_t *err, size_t err_len)
{
    int i;

    memset(opts, 0, sizeof(*opts));
    err[0] = L'\0';
    for (i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"-h") == 0 || wcscmp(argv[i], L"--help") == 0) {
            return CLI_PARSE_HELP;
        }
    }
    for (i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"--json") == 0) {
            opts->json = 1;
        } else if (wcscmp(argv[i], L"--jsonl") == 0) {
            opts->jsonl = 1;
        } else if (wcscmp(argv[i], L"--no-record") == 0) {
            opts->no_record = 1;
        } else {
            swprintf(err, err_len, L"unknown argument: %ls", argv[i]);
            return CLI_PARSE_ERROR;
        }
    }
    if (opts->json && opts->jsonl) {
        swprintf(err, err_len, L"--json and --jsonl cannot be used together");
        return CLI_PARSE_ERROR;
    }
    return CLI_PARSE_OK;
}

static uint64_t now_ft(void)
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

static uint64_t ft_to_unix(uint64_t ft)
{
    return ft > 116444736000000000ull ? (ft - 116444736000000000ull) / 10000000ull : 0;
}

/* "+200 MiB (3 days)", "no change (3 days)", "first record", 추적하지 않으면 "-". */
static void change_text(const CleanerTarget *t, uint64_t now, wchar_t *buf, size_t len)
{
    wchar_t size[32];
    uint64_t days;

    if (!cleaner_target_tracked(t)) {
        swprintf(buf, len, L"-");
        return;
    }
    if (!t->has_growth) {
        swprintf(buf, len, L"first record");
        return;
    }
    days = now > t->growth_since ? (now - t->growth_since) / FT_DAY : 0;
    if (t->growth == 0) {
        swprintf(buf, len, L"no change (%llu d)", (unsigned long long)days);
        return;
    }
    format_size((uint64_t)(t->growth < 0 ? -t->growth : t->growth), size, 32);
    swprintf(buf, len, L"%lc%ls (%llu d)", t->growth > 0 ? L'+' : L'-', size,
             (unsigned long long)days);
}

static void json_change(JsonBuf *b, const CleanerTarget *t)
{
    jsonl_field_key(b, "change");
    if (!t->has_growth) {
        jsonl_raw(b, "null");
        return;
    }
    {
        char num[32];
        jsonl_raw(b, "{\"bytes\":");
        snprintf(num, sizeof(num), "%lld", (long long)t->growth);
        jsonl_raw(b, num);
        jsonl_field_u64(b, "since", ft_to_unix(t->growth_since));
        jsonl_raw(b, "}");
    }
}

static void json_cache_fields(JsonBuf *b, const CleanerTarget *t)
{
    const DevCacheKindInfo *k = dev_cache_kind_info(t->dev_kind);
    jsonl_field_string(b, "id", k != NULL ? k->id : L"");
    jsonl_field_string(b, "tool", k != NULL ? k->tool : L"");
    jsonl_field_string(b, "label", t->label);
    jsonl_field_string(b, "location", t->location);
    jsonl_field_string(b, "state", t->dev_status != DEV_CACHE_FOUND
                                       ? dev_cache_status_name(t->dev_status)
                                       : cleaner_status_name(t->status));
    jsonl_field_string(b, "source", t->dev_source == DEV_CACHE_SRC_ENV      ? L"env"
                                    : t->dev_source == DEV_CACHE_SRC_CONFIG ? L"config"
                                                                            : L"default");
    jsonl_field_string(b, "origin", t->dev_origin);
    jsonl_field_u64(b, "bytes", cleaner_target_tracked_bytes(t));
    jsonl_field_u64(b, "reclaim", t->reclaim_bytes);
    jsonl_field_u64(b, "shared", t->shared_bytes);
    jsonl_field_u64(b, "files", t->files);
    jsonl_field_bool(b, "caution", cleaner_target_caution(t));
    jsonl_field_bool(b, "cleanable", cleaner_target_runnable(t));
    json_change(b, t);
}

static void json_vdisk_fields(JsonBuf *b, const CleanerTarget *t)
{
    Vdisk v;
    wchar_t hint[1100];

    memset(&v, 0, sizeof(v));
    v.kind = t->vdisk_kind;
    v.path = t->location;
    v.sparse = t->vdisk_sparse;
    vdisk_hint(&v, hint, sizeof(hint) / sizeof(hint[0]));
    jsonl_field_string(b, "name", t->label);
    jsonl_field_string(b, "kind", vdisk_kind_name(t->vdisk_kind));
    jsonl_field_string(b, "path", t->location);
    jsonl_field_u64(b, "size", t->bytes);
    jsonl_field_u64(b, "alloc", t->vdisk_alloc);
    jsonl_field_bool(b, "sparse", t->vdisk_sparse);
    jsonl_field_u64(b, "modified", ft_to_unix(t->modified));
    json_change(b, t);
    jsonl_field_string(b, "hint", hint);
}

static void print_text(FILE *out, const CleanerList *l, uint64_t now, const wchar_t *file,
                       int recorded, const wchar_t *why_not)
{
    size_t i, caches = 0, disks = 0;
    uint64_t cache_bytes = 0, cache_reclaim = 0, disk_bytes = 0;
    wchar_t a[32], b[32], c[64];

    for (i = 0; i < l->len; i++) {
        const CleanerTarget *t = &l->items[i];
        if (t->kind == CLEANER_DEV_CACHE) {
            caches++;
            cache_bytes += cleaner_target_tracked_bytes(t);
            cache_reclaim += t->reclaim_bytes;
        } else if (t->kind == CLEANER_VDISK) {
            disks++;
            disk_bytes += t->bytes;
        }
    }
    format_size(cache_bytes, a, 32);
    format_size(cache_reclaim, b, 32);
    fwprintf(out, L"Developer caches: %u found, %ls (reclaim %ls)\n", (unsigned)caches, a, b);
    if (caches > 0) {
        fwprintf(out, L"  %-15ls %-7ls %-10ls %-10ls %-20ls %-14ls %ls\n", L"Cache", L"Tool",
                 L"Size", L"Reclaim", L"Change", L"State", L"Location");
    }
    for (i = 0; i < l->len; i++) {
        const CleanerTarget *t = &l->items[i];
        const DevCacheKindInfo *k;
        if (t->kind != CLEANER_DEV_CACHE) {
            continue;
        }
        k = dev_cache_kind_info(t->dev_kind);
        format_size(cleaner_target_tracked_bytes(t), a, 32);
        format_size(t->reclaim_bytes, b, 32);
        change_text(t, now, c, 64);
        fwprintf(out, L"  %-15ls %-7ls %-10ls %-10ls %-20ls %-14ls %ls%ls\n",
                 k != NULL ? k->id : L"", k != NULL ? k->tool : L"",
                 t->dev_status == DEV_CACHE_FOUND ? a : L"-",
                 t->dev_status == DEV_CACHE_FOUND ? b : L"-", c,
                 t->dev_status != DEV_CACHE_FOUND ? dev_cache_status_name(t->dev_status)
                                                  : cleaner_status_name(t->status),
                 t->location != NULL ? t->location : L"",
                 cleaner_target_caution(t) ? L"  (caution)" : L"");
    }
    format_size(disk_bytes, a, 32);
    fwprintf(out, L"Virtual disks (WSL2 / Docker): %u, %ls\n", (unsigned)disks, a);
    for (i = 0; i < l->len; i++) {
        const CleanerTarget *t = &l->items[i];
        Vdisk v;
        wchar_t hint[1100], *line, *ctx = NULL;
        if (t->kind != CLEANER_VDISK) {
            continue;
        }
        format_size(t->bytes, a, 32);
        format_size(t->vdisk_alloc, b, 32);
        change_text(t, now, c, 64);
        fwprintf(out, L"  %ls [%ls]  size %ls, on disk %ls%ls, change %ls\n    %ls\n",
                 t->label != NULL ? t->label : L"", vdisk_kind_name(t->vdisk_kind), a, b,
                 t->vdisk_sparse ? L" (sparse)" : L"", c, t->location);
        memset(&v, 0, sizeof(v));
        v.kind = t->vdisk_kind;
        v.path = t->location;
        v.sparse = t->vdisk_sparse;
        vdisk_hint(&v, hint, sizeof(hint) / sizeof(hint[0]));
        fwprintf(out, L"    How to shrink (stop WSL/Docker first; some commands need admin):\n");
        for (line = wcstok_s(hint, L"\n", &ctx); line != NULL; line = wcstok_s(NULL, L"\n", &ctx)) {
            fwprintf(out, L"      %ls\n", line);
        }
    }
    if (recorded) {
        fwprintf(out, L"History: recorded to %ls\n", file);
    } else {
        fwprintf(out, L"History: not recorded (%ls)%ls%ls\n", why_not, file != NULL ? L"; compared with " : L"",
                 file != NULL ? file : L"");
    }
    fwprintf(out, L"Read-only: nothing was changed. Clean caches with "
                  L"'disk-scanner clean system --include <name>'.\n");
}

int cli_devenv_run(const CliDevenvOptions *opts, FILE *out, FILE *err)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CleanerList list;
    DevHistory hist;
    JsonBuf buf;
    wchar_t dir[MAX_PATH * 2], *file = NULL;
    const wchar_t *why_not = L"";
    uint64_t now = now_ft();
    int code = EXIT_COMPLETE, recorded = 0, record = 0;
    size_t i, prev_records = 0;

    cleaner_list_init(&list);
    devhist_init(&hist);
    jsonl_buf_init(&buf);
    if (cleaner_analyze(&list, NULL, NULL, NULL, NULL, NULL) != 0) {
        fwprintf(err, L"error: analysis failed (out of memory)\n");
        code = EXIT_FATAL;
        goto done;
    }
    /* 증가 추적: 직전 기록과 비교하고, 기록하도록 돼 있으면 지금 값을 더한다. */
    if (history_default_dir(dir, sizeof(dir) / sizeof(dir[0])) != 0) {
        why_not = L"history folder unknown";
    } else {
        file = devhist_file_in(dir);
        if (opts->no_record) {
            why_not = L"--no-record";
        } else if (!history_autosave_enabled(dir)) {
            why_not = L"history auto-save is off";
        } else {
            record = 1;
        }
        if (file != NULL && devhist_load(&hist, file) != 0) {
            fwprintf(err, L"warning: cannot read %ls\n", file);
        }
        prev_records = hist.len;
        if (cleaner_apply_growth(&list, &hist, now, record) > 0 && record) {
            unsigned long e = 0;
            if (history_ensure_dir(dir) == 0 && devhist_save(&hist, file, &e) == 0) {
                recorded = 1;
            } else {
                fwprintf(err, L"warning: cannot save %ls (error %lu)\n", file, e);
                why_not = L"save failed";
            }
        } else if (record) {
            why_not = L"nothing to record";
        }
    }
    if (!opts->json && !opts->jsonl) {
        print_text(out, &list, now, file, recorded, why_not);
        goto done;
    }
    if (opts->json) {
        int first = 1;
        jsonl_raw(&buf, "{\"version\":1");
        jsonl_field_string(&buf, "command", L"devenv");
        jsonl_field_key(&buf, "caches");
        jsonl_raw(&buf, "[");
        for (i = 0; i < list.len; i++) {
            if (list.items[i].kind == CLEANER_DEV_CACHE) {
                jsonl_raw(&buf, first ? "{" : ",{");
                first = 0;
                json_cache_fields(&buf, &list.items[i]);
                jsonl_raw(&buf, "}");
            }
        }
        jsonl_raw(&buf, "]");
        jsonl_field_key(&buf, "vdisks");
        jsonl_raw(&buf, "[");
        first = 1;
        for (i = 0; i < list.len; i++) {
            if (list.items[i].kind == CLEANER_VDISK) {
                jsonl_raw(&buf, first ? "{" : ",{");
                first = 0;
                json_vdisk_fields(&buf, &list.items[i]);
                jsonl_raw(&buf, "}");
            }
        }
        jsonl_raw(&buf, "]");
        jsonl_field_key(&buf, "history");
        jsonl_raw(&buf, "{");
        jsonl_field_string(&buf, "file", file);
        jsonl_field_bool(&buf, "recorded", recorded);
        jsonl_field_u64(&buf, "previous_records", prev_records);
        jsonl_raw(&buf, "}");
        jsonl_field_u64(&buf, "exit_code", (uint64_t)code);
        jsonl_raw(&buf, "}\n");
    } else {
        for (i = 0; i < list.len; i++) {
            const CleanerTarget *t = &list.items[i];
            if (t->kind == CLEANER_DEV_CACHE) {
                jsonl_begin_event(&buf, "devenv_cache");
                json_cache_fields(&buf, t);
                jsonl_end_event(&buf);
            } else if (t->kind == CLEANER_VDISK) {
                jsonl_begin_event(&buf, "devenv_vdisk");
                json_vdisk_fields(&buf, t);
                jsonl_end_event(&buf);
            }
        }
        jsonl_begin_event(&buf, "complete");
        jsonl_field_string(&buf, "history_file", file);
        jsonl_field_bool(&buf, "recorded", recorded);
        jsonl_field_u64(&buf, "exit_code", (uint64_t)code);
        jsonl_end_event(&buf);
    }
    if (!jsonl_buf_flush(&buf, out)) {
        fwprintf(err, L"error: failed to write JSON output\n");
        code = EXIT_FATAL;
    }
done:
    jsonl_buf_free(&buf);
    devhist_free(&hist);
    cleaner_list_free(&list);
    free(file);
    if (SUCCEEDED(hr)) {
        CoUninitialize();
    }
    return code;
}
