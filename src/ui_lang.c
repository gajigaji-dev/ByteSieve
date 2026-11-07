#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <wctype.h>

#include "ui_lang.h"

/* 레지스트리 위치: 설치 프로그램이 설치 언어를 여기에 쓴다. */
#define UI_LANG_REG_KEY L"Software\\ByteSieve"
#define UI_LANG_REG_VALUE L"Language"
#define UI_LANG_ENV L"BYTESIEVE_LANG"
/* 설정 키를 바꾸는 환경 변수(테스트가 사용자의 실제 설정을 건드리지 않게). */
#define UI_SETTINGS_KEY_ENV L"BYTESIEVE_SETTINGS_KEY"
#define UI_SETTINGS_KEY_MAX 256
/* 언어 태그 최대 길이(널 포함). "zh-Hans-CN" 같은 태그도 들어간다. */
#define UI_LANG_TAG_MAX 32

static UiLang g_lang = UI_LANG_KO;

/*
 * 번역 표: [문자열 번호][언어]. 한국어가 원문이다.
 * 일본어·중국어는 괄호를 전각(（）)으로 쓴다(Windows 현지화 관례).
 * 영어는 Windows 11 처럼 문장형 대문자(첫 글자만 대문자)를 쓴다.
 */
static const wchar_t *const k_text[UI_T_COUNT][UI_LANG_COUNT] = {
    [UI_T_NAV_SCAN] = {L"폴더 스캔", L"Folder scan", L"フォルダースキャン", L"文件夹扫描"},
    [UI_T_NAV_DRIVES] = {L"드라이브", L"Drives", L"ドライブ", L"驱动器"},
    [UI_T_NAV_CLEANUP] = {L"프로젝트 정리", L"Project cleanup", L"プロジェクト整理", L"项目清理"},
    [UI_T_NAV_QUARANTINE] = {L"격리 폴더", L"Quarantine", L"隔離フォルダー", L"隔离文件夹"},
    [UI_T_NAV_MONITOR] = {L"모니터", L"Monitor", L"モニター", L"监视器"},
    [UI_T_NAV_CLEANER] = {L"시스템 정리", L"System cleanup", L"システム整理", L"系统清理"},
    [UI_T_NAV_BOOSTER] = {L"부스터", L"Booster", L"ブースター", L"加速"},
    [UI_T_NAV_CACHE] = {L"개발 캐시", L"Dev caches", L"開発キャッシュ", L"开发缓存"},
    [UI_T_NAV_SETTINGS] = {L"설정", L"Settings", L"設定", L"设置"},

    [UI_T_GROUP_ANALYSIS] = {L"프로젝트 분석", L"Project analysis", L"プロジェクト分析", L"项目分析"},
    [UI_T_GROUP_CLEANUP] = {L"정리", L"Cleanup", L"整理", L"清理"},
    [UI_T_GROUP_CACHE] = {L"캐시", L"Caches", L"キャッシュ", L"缓存"},
    [UI_T_GROUP_QUARANTINE] = {L"격리·복원", L"Quarantine & restore", L"隔離・復元", L"隔离与恢复"},
    [UI_T_GROUP_SYSTEM] = {L"시스템", L"System", L"システム", L"系统"},

    [UI_T_SCAN_BROWSE] = {L"찾아보기...", L"Browse...", L"参照...", L"浏览..."},
    [UI_T_SCAN_START] = {L"스캔", L"Scan", L"スキャン", L"扫描"},
    [UI_T_SCAN_CANCEL] = {L"취소", L"Cancel", L"キャンセル", L"取消"},
    [UI_T_SCAN_PRECISE] = {L"정밀 스캔(하드 링크)", L"Precise scan (hard links)",
                           L"精密スキャン（ハードリンク）", L"精确扫描（硬链接）"},
    [UI_T_SCAN_EXPORT_CSV] = {L"CSV 내보내기...", L"Export CSV...", L"CSV エクスポート...",
                              L"导出 CSV..."},
    [UI_T_SCAN_EXPORT_HTML] = {L"HTML 보고서...", L"HTML report...", L"HTML レポート...",
                               L"HTML 报告..."},
    [UI_T_SCAN_AUTOSAVE] = {L"스캔 기록 자동 저장", L"Auto-save scan history",
                            L"スキャン履歴を自動保存", L"自动保存扫描记录"},
    [UI_T_SCAN_CLEAR_HISTORY] = {L"기록 삭제...", L"Clear history...", L"履歴を削除...",
                                 L"清除记录..."},
    [UI_T_SCAN_PATH_CUE] = {L"스캔할 폴더 경로", L"Folder path to scan",
                            L"スキャンするフォルダーのパス", L"要扫描的文件夹路径"},
    [UI_T_SCAN_READY] = {L"폴더를 선택하고 [스캔]을 누르세요.",
                         L"Choose a folder and select [Scan].",
                         L"フォルダーを選んで [スキャン] を押してください。",
                         L"请选择文件夹，然后点击 [扫描]。"},

    [UI_T_VIEW_SIZE] = {L"용량 순위", L"Largest folders", L"サイズ順", L"按大小排序"},
    [UI_T_VIEW_ALLOC] = {L"디스크 사용 순위", L"Largest on disk", L"ディスク使用量順",
                         L"按磁盘占用排序"},
    [UI_T_VIEW_CHANGE] = {L"변화 순위(직전 스캔 대비)", L"Biggest changes (vs. last scan)",
                          L"変化の大きい順（前回スキャン比）", L"按变化排序（与上次扫描相比）"},
    [UI_T_VIEW_UNIQUE] = {L"중복 제외 용량 순위(정밀 스캔)",
                          L"Largest without duplicates (precise scan)",
                          L"重複除外サイズ順（精密スキャン）", L"按去重后大小排序（精确扫描）"},
    [UI_T_VIEW_DUP] = {L"하드 링크 중복 순위(정밀 스캔)",
                       L"Most hard-link duplicates (precise scan)",
                       L"ハードリンク重複順（精密スキャン）", L"按硬链接重复排序（精确扫描）"},
    [UI_T_VIEW_TREEMAP] = {L"트리맵(폴더 용량)", L"Treemap (folder size)",
                           L"ツリーマップ（フォルダーサイズ）", L"树状图（文件夹大小）"},

    [UI_T_COL_RANK] = {L"순위", L"Rank", L"順位", L"排名"},
    [UI_T_COL_FOLDER] = {L"폴더", L"Folder", L"フォルダー", L"文件夹"},
    [UI_T_COL_SIZE] = {L"용량", L"Size", L"サイズ", L"大小"},
    [UI_T_COL_UNIQUE] = {L"중복 제외", L"Unique", L"重複除外", L"去重后"},
    [UI_T_COL_DUP] = {L"하드 링크 중복", L"Hard-link dup.", L"ハードリンク重複", L"硬链接重复"},
    [UI_T_COL_CHANGE] = {L"변화", L"Change", L"変化", L"变化"},
    [UI_T_COL_ALLOC] = {L"디스크 사용", L"On disk", L"ディスク使用量", L"磁盘占用"},
    [UI_T_COL_FILES] = {L"파일 수", L"Files", L"ファイル数", L"文件数"},
    [UI_T_COL_SHARE] = {L"비율", L"Share", L"割合", L"占比"},

    [UI_T_FLAG_FAILED] = {L" [읽기 실패]", L" [read failed]", L" [読み取り失敗]", L" [读取失败]"},
    [UI_T_FLAG_CANCELLED] = {L" [취소됨]", L" [cancelled]", L" [キャンセル済み]", L" [已取消]"},
    [UI_T_FLAG_INCOMPLETE] = {L" [일부 누락]", L" [incomplete]", L" [一部欠落]", L" [部分缺失]"},
    [UI_T_FLAG_REPARSE] = {L" [링크]", L" [link]", L" [リンク]", L" [链接]"},
    [UI_T_FLAG_DEPTH_LIMIT] = {L" [깊이 제한]", L" [depth limit]", L" [深さ制限]",
                               L" [深度限制]"},

    [UI_T_SET_APPEARANCE] = {L"모양", L"Appearance", L"外観", L"外观"},
    [UI_T_SET_THEME] = {L"테마", L"Theme", L"テーマ", L"主题"},
    [UI_T_THEME_SYSTEM] = {L"시스템 설정 따르기", L"Use system setting", L"システム設定に従う",
                           L"跟随系统设置"},
    [UI_T_THEME_LIGHT] = {L"밝게", L"Light", L"ライト", L"浅色"},
    [UI_T_THEME_DARK] = {L"어둡게", L"Dark", L"ダーク", L"深色"},
    [UI_T_SET_THEME_HC] = {L"고대비 테마가 켜져 있어 어둡게 표시하지 않습니다.",
                           L"High contrast is on, so dark mode isn't applied.",
                           L"ハイコントラストがオンのため、ダーク モードは適用されません。",
                           L"已打开高对比度，因此不应用深色模式。"},
    [UI_T_SET_LANGUAGE] = {L"표시 언어", L"Display language", L"表示言語", L"显示语言"},
    [UI_T_SET_LANG_LABEL] = {L"언어", L"Language", L"言語", L"语言"},
    [UI_T_LANG_SYSTEM] = {L"시스템 설정 따르기(%ls)", L"Use system setting (%ls)",
                          L"システム設定に従う（%ls）", L"跟随系统设置（%ls）"},
    [UI_T_SET_LANG_RESTART] = {L"ByteSieve를 다시 시작하면 적용됩니다.",
                               L"Restart ByteSieve to apply.",
                               L"ByteSieve を再起動すると適用されます。",
                               L"重新启动 ByteSieve 后生效。"},
    [UI_T_SET_LANG_ENV] = {L"환경 변수 BYTESIEVE_LANG(%ls)이 이 설정보다 먼저 적용됩니다.",
                           L"The BYTESIEVE_LANG environment variable (%ls) takes precedence.",
                           L"環境変数 BYTESIEVE_LANG（%ls）がこの設定より優先されます。",
                           L"环境变量 BYTESIEVE_LANG（%ls）优先于此设置。"},
    [UI_T_SET_HISTORY] = {L"스캔 기록", L"Scan history", L"スキャン履歴", L"扫描记录"},
    [UI_T_SET_HISTORY_DIR] = {L"기록 폴더: %ls", L"History folder: %ls", L"履歴フォルダー: %ls",
                              L"记录文件夹：%ls"},
    [UI_T_SET_OPEN_DIR] = {L"폴더 열기", L"Open folder", L"フォルダーを開く", L"打开文件夹"},
    [UI_T_SET_SAVE_FAILED] = {L"설정을 저장하지 못했습니다(오류 %lu). 이번 실행에만 적용됩니다.",
                              L"Couldn't save the setting (error %lu). It applies to this session only.",
                              L"設定を保存できませんでした（エラー %lu）。今回の実行にのみ適用されます。",
                              L"无法保存设置（错误 %lu）。仅对本次运行有效。"},
    [UI_T_SET_AUTOSAVE_FAILED] = {L"자동 저장 설정을 저장하지 못했습니다. 설정은 바뀌지 않았습니다.",
                                  L"Couldn't save the auto-save setting. Nothing was changed.",
                                  L"自動保存の設定を保存できませんでした。設定は変更されていません。",
                                  L"无法保存自动保存设置。设置未更改。"},
    [UI_T_SET_ABOUT] = {L"정보", L"About", L"バージョン情報", L"关于"},
    [UI_T_SET_VERSION] = {L"ByteSieve %ls · PolyForm Noncommercial 1.0.0 라이선스",
                          L"ByteSieve %ls · PolyForm Noncommercial 1.0.0 license",
                          L"ByteSieve %ls · PolyForm Noncommercial 1.0.0 ライセンス",
                          L"ByteSieve %ls · PolyForm Noncommercial 1.0.0 许可证"},
};

static const wchar_t *const k_lang_name[UI_LANG_COUNT] = {L"ko", L"en", L"ja", L"zh-Hans"};
static const wchar_t *const k_native_name[UI_LANG_COUNT] = {L"한국어", L"English", L"日本語",
                                                           L"简体中文"};

void ui_lang_set(UiLang lang)
{
    if ((int)lang >= 0 && lang < UI_LANG_COUNT) {
        g_lang = lang;
    }
}

UiLang ui_lang_current(void)
{
    return g_lang;
}

const wchar_t *ui_text_in(UiLang lang, UiText id)
{
    const wchar_t *s;

    if ((int)lang < 0 || lang >= UI_LANG_COUNT || (int)id < 0 || id >= UI_T_COUNT) {
        return L"";
    }
    s = k_text[id][lang];
    return s != NULL ? s : L"";
}

const wchar_t *ui_text(UiText id)
{
    return ui_text_in(g_lang, id);
}

const wchar_t *ui_lang_name(UiLang lang)
{
    return (int)lang >= 0 && lang < UI_LANG_COUNT ? k_lang_name[lang] : k_lang_name[UI_LANG_EN];
}

const wchar_t *ui_lang_native_name(UiLang lang)
{
    return (int)lang >= 0 && lang < UI_LANG_COUNT ? k_native_name[lang]
                                                  : k_native_name[UI_LANG_EN];
}

/* 앞 부분 태그(소문자)가 word 와 같고 그 뒤가 끝이거나 구분자면 1. */
static int subtag_is(const wchar_t *s, const wchar_t *word)
{
    while (*word != L'\0') {
        if (*s != *word) {
            return 0;
        }
        s++;
        word++;
    }
    return *s == L'\0' || *s == L'-';
}

int ui_lang_from_tag(const wchar_t *tag, UiLang *out)
{
    wchar_t t[UI_LANG_TAG_MAX];
    const wchar_t *rest;
    size_t i;

    if (tag == NULL || out == NULL) {
        return 0;
    }
    /* 소문자로, '_' 는 '-' 로 바꿔 둔다. 너무 긴 태그는 지원 언어가 아니다. */
    for (i = 0; tag[i] != L'\0'; i++) {
        if (i + 1 >= UI_LANG_TAG_MAX) {
            return 0;
        }
        t[i] = tag[i] == L'_' ? L'-' : (wchar_t)towlower(tag[i]);
    }
    t[i] = L'\0';

    if (subtag_is(t, L"ko")) {
        *out = UI_LANG_KO;
        return 1;
    }
    if (subtag_is(t, L"en")) {
        *out = UI_LANG_EN;
        return 1;
    }
    if (subtag_is(t, L"ja")) {
        *out = UI_LANG_JA;
        return 1;
    }
    if (subtag_is(t, L"zh")) {
        /* 간체: 표기 없음, Hans, 중국 본토·싱가포르. 번체(Hant·대만·홍콩·마카오)는 지원하지 않는다. */
        rest = t + 2;
        if (*rest == L'\0' || subtag_is(rest + 1, L"hans") || subtag_is(rest + 1, L"cn") ||
            subtag_is(rest + 1, L"sg")) {
            *out = UI_LANG_ZH_HANS;
            return 1;
        }
    }
    return 0;
}

UiLang ui_lang_from_langid(unsigned short langid)
{
    switch (PRIMARYLANGID(langid)) {
    case LANG_KOREAN:
        return UI_LANG_KO;
    case LANG_JAPANESE:
        return UI_LANG_JA;
    case LANG_CHINESE:
        if (SUBLANGID(langid) == SUBLANG_CHINESE_SIMPLIFIED ||
            SUBLANGID(langid) == SUBLANG_CHINESE_SINGAPORE) {
            return UI_LANG_ZH_HANS;
        }
        return UI_LANG_EN;
    default:
        return UI_LANG_EN;
    }
}

/* 설정 키: 환경 변수 BYTESIEVE_SETTINGS_KEY 가 있으면 그 키(테스트용), 없으면 기본 키.
 * app_settings 의 settings_reg_key 와 같은 규칙이다(test_app_settings 가 확인). */
static const wchar_t *settings_key(void)
{
    static wchar_t key[UI_SETTINGS_KEY_MAX];
    DWORD n = GetEnvironmentVariableW(UI_SETTINGS_KEY_ENV, key, UI_SETTINGS_KEY_MAX);

    return n > 0 && n < UI_SETTINGS_KEY_MAX ? key : UI_LANG_REG_KEY;
}

UiLang ui_lang_detect(void)
{
    wchar_t buf[UI_LANG_TAG_MAX];
    DWORD n, size;
    UiLang lang;

    n = GetEnvironmentVariableW(UI_LANG_ENV, buf, UI_LANG_TAG_MAX);
    if (n > 0 && n < UI_LANG_TAG_MAX && ui_lang_from_tag(buf, &lang)) {
        return lang;
    }
    size = (DWORD)sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, settings_key(), UI_LANG_REG_VALUE, RRF_RT_REG_SZ, NULL,
                     buf, &size) == ERROR_SUCCESS &&
        ui_lang_from_tag(buf, &lang)) {
        return lang;
    }
    return ui_lang_from_langid(GetUserDefaultUILanguage());
}
