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

    /* ---- 폴더 스캔: 상태 줄 ---- */
    [UI_T_SCAN_NEED_PATH] = {L"스캔할 폴더를 입력하거나 [찾아보기]로 선택하세요.",
                             L"Enter a folder to scan, or choose one with [Browse...].",
                             L"スキャンするフォルダーを入力するか、[参照...] で選んでください。",
                             L"请输入要扫描的文件夹，或通过 [浏览...] 选择。"},
    [UI_T_SCAN_START_FAILED] = {L"스캔을 시작하지 못했습니다(메모리 또는 스레드 생성 실패).",
                                L"Couldn't start the scan (out of memory or couldn't create a thread).",
                                L"スキャンを開始できませんでした（メモリ不足またはスレッドを作成できません）。",
                                L"无法开始扫描（内存不足或无法创建线程）。"},
    [UI_T_SCAN_STARTING] = {L"스캔 중...", L"Scanning...", L"スキャン中...", L"正在扫描..."},
    [UI_T_SCAN_STARTING_PRECISE] = {L"정밀 스캔 중(하드 링크 확인)...",
                                    L"Precise scan in progress (checking hard links)...",
                                    L"精密スキャン中（ハードリンクを確認）...",
                                    L"正在精确扫描（检查硬链接）..."},
    [UI_T_SCAN_CANCELLING] = {L"취소하는 중...", L"Cancelling...", L"キャンセルしています...",
                              L"正在取消..."},
    [UI_T_SCAN_PROGRESS] = {L"스캔 중... 파일 %llu · 폴더 %llu · %ls · 오류 %llu  |  %ls",
                            L"Scanning... files %llu · folders %llu · %ls · errors %llu  |  %ls",
                            L"スキャン中... ファイル %llu · フォルダー %llu · %ls · エラー %llu  |  %ls",
                            L"正在扫描... 文件 %llu · 文件夹 %llu · %ls · 错误 %llu  |  %ls"},
    [UI_T_SCAN_PROGRESS_PRECISE] =
        {L"정밀 스캔 중... 파일 %llu · 폴더 %llu · %ls · 하드 링크 중복 %llu개(-%ls) · 오류 %llu  |  %ls",
         L"Precise scan... files %llu · folders %llu · %ls · hard-link duplicates %llu (-%ls) · "
         L"errors %llu  |  %ls",
         L"精密スキャン中... ファイル %llu · フォルダー %llu · %ls · ハードリンク重複 %llu 個（-%ls） · "
         L"エラー %llu  |  %ls",
         L"正在精确扫描... 文件 %llu · 文件夹 %llu · %ls · 硬链接重复 %llu 个（-%ls） · 错误 %llu  |  %ls"},
    [UI_T_SCAN_HEAD_DONE] = {L"완료", L"Done", L"完了", L"完成"},
    [UI_T_SCAN_HEAD_PARTIAL] = {L"완료(일부 폴더를 읽지 못함)", L"Done (some folders couldn't be read)",
                                L"完了（一部のフォルダーを読み取れませんでした）",
                                L"完成（部分文件夹无法读取）"},
    [UI_T_SCAN_HEAD_CANCELLED] = {L"취소됨(그때까지 집계)", L"Cancelled (totals up to that point)",
                                  L"キャンセル済み（その時点までの集計）", L"已取消（统计到取消时为止）"},
    [UI_T_SCAN_HEAD_FAILED] = {L"스캔 실패", L"Scan failed", L"スキャン失敗", L"扫描失败"},
    [UI_T_SCAN_FINISHED] =
        {L"%ls: 파일 %llu · 폴더 %llu · %ls · 오류 %llu · 건너뛴 폴더 %llu · %.1f초",
         L"%ls: files %llu · folders %llu · %ls · errors %llu · skipped folders %llu · %.1f s",
         L"%ls: ファイル %llu · フォルダー %llu · %ls · エラー %llu · スキップしたフォルダー %llu · %.1f 秒",
         L"%ls：文件 %llu · 文件夹 %llu · %ls · 错误 %llu · 跳过的文件夹 %llu · %.1f 秒"},
    [UI_T_SCAN_FINISHED_PRECISE] =
        {L"정밀 스캔 %ls: 파일 %llu · 폴더 %llu · %ls · 오류 %llu · 건너뛴 폴더 %llu · %.1f초",
         L"%ls (precise scan): files %llu · folders %llu · %ls · errors %llu · skipped folders %llu · "
         L"%.1f s",
         L"精密スキャン%ls: ファイル %llu · フォルダー %llu · %ls · エラー %llu · "
         L"スキップしたフォルダー %llu · %.1f 秒",
         L"精确扫描%ls：文件 %llu · 文件夹 %llu · %ls · 错误 %llu · 跳过的文件夹 %llu · %.1f 秒"},
    [UI_T_SCAN_FATAL] = {L"%ls: 폴더를 열 수 없습니다 (%ls).", L"%ls: couldn't open the folder (%ls).",
                         L"%ls: フォルダーを開けません（%ls）。", L"%ls：无法打开文件夹（%ls）。"},
    [UI_T_SCAN_AUTOSAVE_ON] =
        {L"스캔 기록 자동 저장을 켰습니다. 다음 스캔부터 결과를 기록합니다.",
         L"Auto-save for scan history is on. Results will be recorded from the next scan.",
         L"スキャン履歴の自動保存をオンにしました。次回のスキャンから結果を記録します。",
         L"已开启扫描记录自动保存。将从下次扫描开始记录结果。"},
    [UI_T_SCAN_AUTOSAVE_OFF] =
        {L"스캔 기록 자동 저장을 껐습니다. 이후 스캔 결과는 저장하지 않습니다"
         L"(이미 저장된 기록은 그대로 둡니다).",
         L"Auto-save for scan history is off. Later scan results won't be saved "
         L"(saved history is kept).",
         L"スキャン履歴の自動保存をオフにしました。今後のスキャン結果は保存しません"
         L"（保存済みの履歴はそのまま残ります）。",
         L"已关闭扫描记录自动保存。之后的扫描结果将不会保存（已保存的记录保持不变）。"},
    [UI_T_SCAN_AUTOSAVE_SAVE_FAILED] =
        {L"자동 저장 설정을 저장하지 못했습니다(오류 %lu). 설정은 바뀌지 않았습니다.",
         L"Couldn't save the auto-save setting (error %lu). The setting wasn't changed.",
         L"自動保存の設定を保存できませんでした（エラー %lu）。設定は変更されていません。",
         L"无法保存自动保存设置（错误 %lu）。设置未更改。"},
    [UI_T_SCAN_HISTORY_NO_DIR] = {L"기록 폴더를 정하지 못해 기록을 삭제할 수 없습니다.",
                                  L"Couldn't determine the history folder, so history can't be cleared.",
                                  L"履歴フォルダーを特定できないため、履歴を削除できません。",
                                  L"无法确定记录文件夹，因此无法清除记录。"},
    [UI_T_SCAN_HISTORY_EMPTY] = {L"삭제할 스캔 기록이 없습니다.", L"There's no scan history to clear.",
                                 L"削除するスキャン履歴はありません。", L"没有可清除的扫描记录。"},
    [UI_T_SCAN_HISTORY_CLEAR_CANCELLED] = {L"기록 삭제를 취소했습니다. 아무것도 바뀌지 않았습니다.",
                                           L"Clearing history was cancelled. Nothing was changed.",
                                           L"履歴の削除をキャンセルしました。何も変更されていません。",
                                           L"已取消清除记录。没有任何更改。"},
    [UI_T_SCAN_HISTORY_CLEARED] = {L"스캔 기록 %u개를 삭제했습니다.",
                                   L"Cleared %u scan history entries.",
                                   L"スキャン履歴を %u 件削除しました。", L"已清除 %u 条扫描记录。"},
    [UI_T_SCAN_HISTORY_READ_FAILED] =
        {L"기록 폴더를 읽지 못해 기록을 삭제하지 못했습니다(오류 %lu).",
         L"Couldn't read the history folder, so history wasn't cleared (error %lu).",
         L"履歴フォルダーを読み取れなかったため、履歴を削除できませんでした（エラー %lu）。",
         L"无法读取记录文件夹，因此未能清除记录（错误 %lu）。"},
    [UI_T_SCAN_HISTORY_PARTIAL] =
        {L"스캔 기록 %u개를 삭제했고 %u개는 삭제하지 못했습니다(오류 %lu: 사용 중이거나 "
         L"권한이 없을 수 있음). 다시 시도하세요.",
         L"Cleared %u scan history entries; %u couldn't be cleared (error %lu: they may be in use "
         L"or you may not have permission). Try again.",
         L"スキャン履歴を %u 件削除し、%u 件は削除できませんでした（エラー %lu: 使用中か、"
         L"権限がない可能性があります）。もう一度お試しください。",
         L"已清除 %u 条扫描记录，%u 条未能清除（错误 %lu：可能正在使用或没有权限）。请重试。"},
    [UI_T_SCAN_EXPORT_WHAT_HTML] = {L"HTML 보고서", L"HTML report", L"HTML レポート", L"HTML 报告"},
    [UI_T_SCAN_EXPORTED] = {L"%ls(으)로 내보냈습니다(폴더 %u개): %ls",
                            L"Exported as %ls (%u folders): %ls",
                            L"%ls としてエクスポートしました（フォルダー %u 個）: %ls",
                            L"已导出为 %ls（%u 个文件夹）：%ls"},
    [UI_T_SCAN_EXPORT_FAILED] = {L"%ls 내보내기 실패(오류 %lu): %ls",
                                 L"%ls export failed (error %lu): %ls",
                                 L"%ls のエクスポートに失敗しました（エラー %lu）: %ls",
                                 L"%ls 导出失败（错误 %lu）：%ls"},
    [UI_T_SCAN_NOTHING_TO_EXPORT] = {L"내보낼 스캔 결과가 없습니다. 먼저 폴더를 스캔하세요.",
                                     L"There are no scan results to export. Scan a folder first.",
                                     L"エクスポートするスキャン結果がありません。先にフォルダーをスキャンしてください。",
                                     L"没有可导出的扫描结果。请先扫描文件夹。"},
    [UI_T_SCAN_DIALOG_FAILED] = {L"저장 위치 대화 상자를 열지 못했습니다(오류 0x%lx).",
                                 L"Couldn't open the save dialog (error 0x%lx).",
                                 L"保存先のダイアログを開けませんでした（エラー 0x%lx）。",
                                 L"无法打开保存位置对话框（错误 0x%lx）。"},

    /* ---- 폴더 스캔: 확인·알림 창, 대화 상자 ---- */
    [UI_T_SCAN_CLEAR_TITLE] = {L"스캔 기록 삭제", L"Clear scan history", L"スキャン履歴の削除",
                               L"清除扫描记录"},
    [UI_T_SCAN_CLEAR_CONFIRM] =
        {L"저장된 스캔 기록 %u개를 삭제할까요?\n\n기록 폴더: %ls\n\n"
         L"삭제하면 직전 스캔과 비교한 변화를 더 볼 수 없고, 다음 스캔은 새 기준이 됩니다. "
         L"같은 폴더를 쓰는 CLI --track 기록도 함께 삭제됩니다.\n"
         L"스캔한 폴더와 파일은 건드리지 않으며, 자동 저장 설정은 그대로 둡니다.\n\n"
         L"삭제한 기록은 되돌릴 수 없습니다.",
         L"Clear %u saved scan history entries?\n\nHistory folder: %ls\n\n"
         L"After clearing, you can no longer see changes compared with the last scan, and the next "
         L"scan becomes the new baseline. CLI --track history in the same folder is cleared too.\n"
         L"Scanned folders and files aren't touched, and the auto-save setting stays as it is.\n\n"
         L"Cleared history can't be restored.",
         L"保存されたスキャン履歴 %u 件を削除しますか？\n\n履歴フォルダー: %ls\n\n"
         L"削除すると、前回のスキャンと比較した変化を確認できなくなり、次回のスキャンが新しい基準になります。"
         L"同じフォルダーを使う CLI --track の履歴も削除されます。\n"
         L"スキャンしたフォルダーやファイルには触れず、自動保存の設定もそのままです。\n\n"
         L"削除した履歴は元に戻せません。",
         L"要清除已保存的 %u 条扫描记录吗？\n\n记录文件夹：%ls\n\n"
         L"清除后将无法再查看与上次扫描相比的变化，下次扫描将成为新的基准。"
         L"使用同一文件夹的 CLI --track 记录也会一并清除。\n"
         L"不会改动已扫描的文件夹和文件，自动保存设置也保持不变。\n\n"
         L"清除的记录无法恢复。"},
    [UI_T_SCAN_EXPORT_FAILED_TITLE] = {L"내보내기 실패", L"Export failed", L"エクスポートの失敗",
                                       L"导出失败"},
    [UI_T_SCAN_EXPORT_FAILED_MSG] =
        {L"%ls(으)로 내보내지 못했습니다.\n\n%ls\n(오류 %lu)\n\n파일: %ls\n\n"
         L"이미 있던 파일은 바뀌지 않았습니다.",
         L"Couldn't export as %ls.\n\n%ls\n(error %lu)\n\nFile: %ls\n\n"
         L"Any existing file wasn't changed.",
         L"%ls としてエクスポートできませんでした。\n\n%ls\n（エラー %lu）\n\nファイル: %ls\n\n"
         L"既存のファイルは変更されていません。",
         L"无法导出为 %ls。\n\n%ls\n（错误 %lu）\n\n文件：%ls\n\n原有文件未被更改。"},
    [UI_T_SCAN_CSV_FILTER] = {L"CSV 파일 (*.csv)", L"CSV files (*.csv)", L"CSV ファイル (*.csv)",
                              L"CSV 文件 (*.csv)"},
    [UI_T_SCAN_HTML_FILTER] = {L"HTML 보고서 (*.html)", L"HTML reports (*.html)",
                               L"HTML レポート (*.html)", L"HTML 报告 (*.html)"},
    [UI_T_SCAN_ALL_FILTER] = {L"모든 파일 (*.*)", L"All files (*.*)", L"すべてのファイル (*.*)",
                              L"所有文件 (*.*)"},
    [UI_T_SCAN_CSV_TITLE] = {L"스캔 결과를 CSV로 내보내기", L"Export scan results as CSV",
                             L"スキャン結果を CSV にエクスポート", L"将扫描结果导出为 CSV"},
    [UI_T_SCAN_HTML_TITLE] = {L"스캔 결과를 HTML 보고서로 저장", L"Save scan results as an HTML report",
                              L"スキャン結果を HTML レポートとして保存", L"将扫描结果保存为 HTML 报告"},
    [UI_T_SCAN_BROWSE_TITLE] = {L"스캔할 폴더를 선택하세요.", L"Choose a folder to scan.",
                                L"スキャンするフォルダーを選んでください。", L"请选择要扫描的文件夹。"},

    /* ---- 내보내기 실패 이유 ---- */
    [UI_T_EXPORT_ERR_ACCESS] =
        {L"저장 위치에 쓸 권한이 없습니다(읽기 전용 파일이거나 보호된 폴더).",
         L"You don't have permission to write to this location (read-only file or protected folder).",
         L"保存先に書き込む権限がありません（読み取り専用のファイルか、保護されたフォルダーです）。",
         L"没有写入保存位置的权限（只读文件或受保护的文件夹）。"},
    [UI_T_EXPORT_ERR_IN_USE] =
        {L"다른 프로그램(Excel 등)이 파일을 사용 중입니다. 파일을 닫고 다시 시도하세요.",
         L"Another program (such as Excel) is using the file. Close the file and try again.",
         L"別のプログラム（Excel など）がファイルを使用中です。ファイルを閉じてもう一度お試しください。",
         L"其他程序（如 Excel）正在使用该文件。请关闭文件后重试。"},
    [UI_T_EXPORT_ERR_DISK_FULL] = {L"디스크 공간이 부족합니다.", L"There isn't enough disk space.",
                                   L"ディスクの空き容量が不足しています。", L"磁盘空间不足。"},
    [UI_T_EXPORT_ERR_PATH] = {L"저장 위치를 찾을 수 없거나 파일 이름이 올바르지 않습니다.",
                              L"The save location can't be found, or the file name isn't valid.",
                              L"保存先が見つからないか、ファイル名が正しくありません。",
                              L"找不到保存位置，或文件名无效。"},
    [UI_T_EXPORT_ERR_MEMORY] = {L"메모리가 부족합니다.", L"There isn't enough memory.",
                                L"メモリが不足しています。", L"内存不足。"},
    [UI_T_EXPORT_ERR_NO_RESULT] = {L"내보낼 스캔 결과가 없습니다.", L"There are no scan results to export.",
                                   L"エクスポートするスキャン結果がありません。", L"没有可导出的扫描结果。"},
    [UI_T_EXPORT_ERR_OTHER] = {L"파일을 쓰지 못했습니다.", L"Couldn't write the file.",
                               L"ファイルを書き込めませんでした。", L"无法写入文件。"},

    /* ---- 폴더 스캔: 분석 줄 ---- */
    [UI_T_AN_DISK] = {L"디스크 사용 %ls(논리 크기 대비 %ls)", L"On disk %ls (%ls vs. logical size)",
                      L"ディスク使用量 %ls（論理サイズ比 %ls）", L"磁盘占用 %ls（与逻辑大小相比 %ls）"},
    [UI_T_AN_PRECISE_NONE] = {L" · 정밀 스캔: 하드 링크가 없는 파일 시스템이라 보정할 것이 없음",
                              L" · Precise scan: nothing to correct (the file system has no hard links)",
                              L" · 精密スキャン: ハードリンクのないファイルシステムのため補正なし",
                              L" · 精确扫描：文件系统不支持硬链接，无需校正"},
    [UI_T_AN_PRECISE] = {L" · 정밀 스캔: 중복 제외 %ls(기본 %ls, 하드 링크 중복 %llu개 -%ls)",
                         L" · Precise scan: %ls without duplicates (basic %ls, %llu hard-link "
                         L"duplicates -%ls)",
                         L" · 精密スキャン: 重複除外 %ls（基本 %ls、ハードリンク重複 %llu 個 -%ls）",
                         L" · 精确扫描：去重后 %ls（基本 %ls，硬链接重复 %llu 个 -%ls）"},
    [UI_T_AN_REAL_DISK] = {L" · 실제 디스크 사용 %ls", L" · actual disk usage %ls",
                           L" · 実際のディスク使用量 %ls", L" · 实际磁盘占用 %ls"},
    [UI_T_AN_UNKNOWN_ID] = {L" · 파일 ID 모름 %llu개(경로마다 셈)",
                            L" · %llu files with unknown ID (counted per path)",
                            L" · ファイル ID 不明 %llu 個（パスごとに集計）",
                            L" · 文件 ID 未知 %llu 个（按路径计数）"},
    [UI_T_AN_SLACK] = {L" · 클러스터 낭비 +%ls", L" · cluster slack +%ls", L" · クラスターの無駄 +%ls",
                       L" · 簇浪费 +%ls"},
    [UI_T_AN_COMPRESSED] = {L" · 압축 -%ls(%llu개)", L" · compressed -%ls (%llu)",
                            L" · 圧縮 -%ls（%llu 個）", L" · 压缩 -%ls（%llu 个）"},
    [UI_T_AN_SPARSE] = {L" · 스파스 -%ls(%llu개)", L" · sparse -%ls (%llu)",
                        L" · スパース -%ls（%llu 個）", L" · 稀疏 -%ls（%llu 个）"},
    [UI_T_AN_CLOUD] = {L" · 클라우드 전용 -%ls(%llu개)", L" · cloud-only -%ls (%llu)",
                       L" · クラウドのみ -%ls（%llu 個）", L" · 仅限云端 -%ls（%llu 个）"},
    [UI_T_AN_OTHER] = {L" · 기타 -%ls(%llu개)", L" · other -%ls (%llu)", L" · その他 -%ls（%llu 個）",
                       L" · 其他 -%ls（%llu 个）"},
    [UI_T_AN_CHANGES] =
        {L"직전 스캔(%02u-%02u %02u:%02u) 대비 %ls(%ls → %ls) · 늘어남 %u · 줄어듦 %u · "
         L"새 폴더 %u · 삭제 %u",
         L"Since last scan (%02u-%02u %02u:%02u): %ls (%ls → %ls) · grown %u · shrunk %u · "
         L"new folders %u · deleted %u",
         L"前回のスキャン（%02u-%02u %02u:%02u）比 %ls（%ls → %ls） · 増加 %u · 減少 %u · "
         L"新しいフォルダー %u · 削除 %u",
         L"与上次扫描（%02u-%02u %02u:%02u）相比 %ls（%ls → %ls） · 增加 %u · 减少 %u · "
         L"新文件夹 %u · 删除 %u"},
    [UI_T_AN_HISTORY_CLEARED] =
        {L"저장된 스캔 기록을 삭제했습니다(직전 스캔과 비교한 변화 표시도 지움)",
         L"Saved scan history was cleared (changes compared with the last scan are no longer shown)",
         L"保存されたスキャン履歴を削除しました（前回のスキャンと比較した変化の表示も消去）",
         L"已清除保存的扫描记录（与上次扫描相比的变化显示也已清除）"},
    [UI_T_AN_BASELINE] = {L"첫 스캔: 기준으로 저장했습니다(다음 스캔부터 변화를 비교)",
                          L"First scan: saved as the baseline (changes are compared from the next scan)",
                          L"初回スキャン: 基準として保存しました（次回のスキャンから変化を比較）",
                          L"首次扫描：已保存为基准（从下次扫描开始比较变化）"},
    [UI_T_AN_CANCELLED] = {L"%ls취소되어 기록하지 않았습니다", L"%lsCancelled, so not recorded",
                           L"%lsキャンセルされたため記録していません", L"%ls已取消，因此未记录"},
    [UI_T_AN_SAVE_FAILED] = {L"%ls변화 기록을 저장하지 못했습니다(오류 %lu)",
                             L"%lsCouldn't save the change history (error %lu)",
                             L"%ls変化の記録を保存できませんでした（エラー %lu）",
                             L"%ls无法保存变化记录（错误 %lu）"},
    [UI_T_AN_AUTOSAVE_OFF] = {L"%ls자동 저장이 꺼져 있어 이번 스캔은 기록하지 않았습니다",
                              L"%lsAuto-save is off, so this scan wasn't recorded",
                              L"%ls自動保存がオフのため、今回のスキャンは記録していません",
                              L"%ls自动保存已关闭，因此未记录本次扫描"},

    /* ---- 폴더 스캔: 트리·순위 목록 칸 ---- */
    [UI_T_TREE_UNIQUE] = {L" · 중복 제외 %ls", L" · %ls without duplicates", L" · 重複除外 %ls",
                          L" · 去重后 %ls"},
    [UI_T_LIST_DUP] = {L"-%ls (%llu개)", L"-%ls (%llu)", L"-%ls（%llu 個）", L"-%ls（%llu 个）"},
    [UI_T_CHANGE_ADDED] = {L"새 폴더 %ls%ls", L"New %ls%ls", L"新規 %ls%ls", L"新文件夹 %ls%ls"},
    [UI_T_CHANGE_REMOVED] = {L"삭제됨 %ls%ls", L"Deleted %ls%ls", L"削除 %ls%ls", L"已删除 %ls%ls"},

    /* ---- 폴더 스캔: 트리맵 ---- */
    [UI_T_MAP_COLOR_DEPTH] = {L"깊이", L"Depth", L"深さ", L"深度"},
    [UI_T_MAP_COLOR_TOP] = {L"최상위 폴더", L"Top-level folder", L"最上位フォルダー", L"顶级文件夹"},
    [UI_T_MAP_COLOR_CLEANUP] = {L"정리 후보", L"Cleanup candidates", L"整理候補", L"清理候选"},
    [UI_T_MAP_CAT_BUILD] = {L"빌드 결과물", L"Build output", L"ビルド成果物", L"构建产物"},
    [UI_T_MAP_CAT_CACHE] = {L"캐시", L"Cache", L"キャッシュ", L"缓存"},
    [UI_T_MAP_CAT_TEMP] = {L"임시", L"Temp", L"一時", L"临时"},
    [UI_T_MAP_FILES] = {L"파일 %llu개", L"%llu files", L"ファイル %llu 個", L"%llu 个文件"},
    [UI_T_MAP_OTHER] = {L"그 밖의 폴더 %llu개", L"%llu other folders", L"その他のフォルダー %llu 個",
                        L"其他 %llu 个文件夹"},
    [UI_T_MAP_HINT] = {L"더블클릭: 확대 · Backspace: 상위 폴더 · 우클릭: 메뉴",
                       L"Double-click: zoom in · Backspace: parent folder · Right-click: menu",
                       L"ダブルクリック: 拡大 · Backspace: 上位フォルダー · 右クリック: メニュー",
                       L"双击：放大 · Backspace：上级文件夹 · 右键：菜单"},
    [UI_T_MAP_ROOT_INFO] = {L"  ·  %ls  ·  하위 폴더 %llu개", L"  ·  %ls  ·  %llu subfolders",
                            L"  ·  %ls  ·  サブフォルダー %llu 個", L"  ·  %ls  ·  %llu 个子文件夹"},
    [UI_T_MAP_EMPTY] = {L"스캔하면 폴더 용량을 사각형 크기로 보여 줍니다.",
                        L"After a scan, folder sizes are shown as rectangle sizes.",
                        L"スキャンすると、フォルダーのサイズを四角形の大きさで表示します。",
                        L"扫描后将以矩形大小显示文件夹大小。"},
    [UI_T_MAP_NO_CHILDREN] = {L"이 폴더에는 용량이 있는 하위 항목이 없습니다.",
                              L"This folder has no subitems that take up space.",
                              L"このフォルダーには容量のあるサブ項目がありません。",
                              L"此文件夹没有占用空间的子项。"},
    [UI_T_MAP_TIP_RATIO] = {L"%ls · 기준 폴더의 %.1f%% · 전체의 %.1f%%",
                            L"%ls · %.1f%% of the current folder · %.1f%% of the total",
                            L"%ls · 基準フォルダーの %.1f%% · 全体の %.1f%%",
                            L"%ls · 占当前文件夹的 %.1f%% · 占总量的 %.1f%%"},
    [UI_T_MAP_TIP_FILES] = {L"%ls 바로 아래 파일 %llu개\n%ls", L"Files directly in %ls: %llu\n%ls",
                            L"%ls 直下のファイル %llu 個\n%ls", L"%ls 下的直接文件 %llu 个\n%ls"},
    [UI_T_MAP_TIP_OTHER] = {L"%ls 안의 작은 폴더 %llu개(묶음)\n%ls",
                            L"Small folders in %ls: %llu (grouped)\n%ls",
                            L"%ls 内の小さなフォルダー %llu 個（まとめて表示）\n%ls",
                            L"%ls 中的小文件夹 %llu 个（合并显示）\n%ls"},
    [UI_T_MAP_TIP_NODE] = {L"%ls\n%ls\n하위 폴더 %llu개 · 파일 %llu개",
                           L"%ls\n%ls\nSubfolders %llu · files %llu",
                           L"%ls\n%ls\nサブフォルダー %llu 個 · ファイル %llu 個",
                           L"%ls\n%ls\n子文件夹 %llu 个 · 文件 %llu 个"},
    [UI_T_MAP_TIP_CLEANUP] = {L"\n정리 후보일 수 있음(이름 기준): %ls",
                              L"\nMay be a cleanup candidate (by name): %ls",
                              L"\n整理候補の可能性あり（名前で判定）: %ls",
                              L"\n可能是清理候选（按名称判断）：%ls"},
    [UI_T_MAP_TIP_INCOMPLETE] = {L"\n일부 하위 내용을 읽지 못해 집계에서 빠짐",
                                 L"\nSome contents couldn't be read and aren't included",
                                 L"\n一部の内容を読み取れず、集計に含まれていません",
                                 L"\n部分内容无法读取，未计入统计"},
    [UI_T_MAP_MENU_ZOOM] = {L"이 폴더로 확대(&Z)\tEnter", L"&Zoom to this folder\tEnter",
                            L"このフォルダーに拡大(&Z)\tEnter", L"放大到此文件夹(&Z)\tEnter"},
    [UI_T_MAP_MENU_UP] = {L"상위 폴더로(&U)\tBackspace", L"&Up to parent folder\tBackspace",
                          L"上位フォルダーへ(&U)\tBackspace", L"转到上级文件夹(&U)\tBackspace"},
    [UI_T_MAP_MENU_OPEN] = {L"탐색기에서 열기(&O)", L"&Open in File Explorer",
                            L"エクスプローラーで開く(&O)", L"在文件资源管理器中打开(&O)"},
    [UI_T_MAP_MENU_COPY] = {L"경로 복사(&C)", L"&Copy path", L"パスのコピー(&C)", L"复制路径(&C)"},
    [UI_T_MAP_MENU_COLOR] = {L"색 구분(&L)", L"Co&lor by", L"色分け(&L)", L"颜色区分(&L)"},

    /* ---- 격리 폴더: 단추·열 머리글·안내 ---- */
    [UI_T_Q_REFRESH] = {L"새로 고침", L"Refresh", L"最新の情報に更新", L"刷新"},
    [UI_T_Q_RESTORE] = {L"복원(&R)", L"&Restore", L"復元(&R)", L"恢复(&R)"},
    [UI_T_Q_PURGE] = {L"영구 삭제(&P)...", L"&Permanently delete...", L"完全に削除(&P)...",
                      L"永久删除(&P)..."},
    [UI_T_Q_CANCEL] = {L"취소", L"Cancel", L"キャンセル", L"取消"},
    [UI_T_Q_OPEN] = {L"격리 폴더 열기", L"Open quarantine folder", L"隔離フォルダーを開く",
                     L"打开隔离文件夹"},
    [UI_T_Q_INFO] = {L"휴지통이 없는 드라이브(USB 등)에서 정리한 항목을 같은 드라이브의 "
                     L"격리 폴더(.bytesieve-trash)에 보관합니다. 복원은 덮어쓰지 않으며, "
                     L"영구 삭제한 항목은 복구할 수 없습니다.",
                     L"Items cleaned up on drives without a Recycle Bin (such as USB drives) are kept in the "
                     L"quarantine folder (.bytesieve-trash) on the same drive. Restoring never overwrites, "
                     L"and permanently deleted items can't be recovered.",
                     L"ごみ箱のないドライブ（USB など）で整理した項目は、同じドライブの隔離フォルダー"
                     L"（.bytesieve-trash）に保管します。復元時に上書きはせず、完全に削除した項目は"
                     L"元に戻せません。",
                     L"在没有回收站的驱动器（如 USB）上清理的项目会保存在同一驱动器的隔离文件夹"
                     L"（.bytesieve-trash）中。恢复时不会覆盖，永久删除的项目无法恢复。"},
    [UI_T_Q_COL_STATE] = {L"상태", L"Status", L"状態", L"状态"},
    [UI_T_Q_COL_SIZE] = {L"용량", L"Size", L"サイズ", L"大小"},
    [UI_T_Q_COL_ORIGINAL] = {L"원래 위치", L"Original location", L"元の場所", L"原始位置"},
    [UI_T_Q_COL_TIME] = {L"격리한 시각", L"Quarantined", L"隔離した日時", L"隔离时间"},
    [UI_T_Q_COL_LOCATION] = {L"격리 위치", L"Quarantine location", L"隔離場所", L"隔离位置"},
    [UI_T_Q_COL_RESULT] = {L"결과", L"Result", L"結果", L"结果"},
    [UI_T_Q_STATE_OK] = {L"격리됨", L"Quarantined", L"隔離済み", L"已隔离"},
    [UI_T_Q_STATE_MISSING] = {L"기록만 있음(파일 없음)", L"Record only (file missing)",
                              L"記録のみ（ファイルなし）", L"仅有记录（文件不存在）"},
    [UI_T_Q_STATE_ORPHAN] = {L"기록 없음(원래 위치 모름)", L"No record (original location unknown)",
                             L"記録なし（元の場所が不明）", L"无记录（原始位置未知）"},
    [UI_T_Q_UNKNOWN] = {L"(알 수 없음)", L"(unknown)", L"（不明）", L"（未知）"},
    [UI_T_Q_SUMMARY] = {L"격리 항목 %u개 · %ls", L"%u quarantined items · %ls",
                        L"隔離項目 %u 個 · %ls", L"隔离项目 %u 个 · %ls"},
    [UI_T_Q_SUMMARY_CHECK] = {L"격리 항목 %u개 · %ls   /   확인 필요 %u개",
                              L"%u quarantined items · %ls   /   %u need attention",
                              L"隔離項目 %u 個 · %ls   /   要確認 %u 個",
                              L"隔离项目 %u 个 · %ls   /   需要确认 %u 个"},

    /* ---- 격리 폴더: 상태 줄 ---- */
    [UI_T_Q_UNREADABLE] = {L"격리 폴더를 읽지 못한 드라이브가 %u개 있습니다(링크로 바뀌었거나 접근 거부).",
                           L"Couldn't read the quarantine folder on %u drives (it became a link, or "
                           L"access was denied).",
                           L"隔離フォルダーを読み取れなかったドライブが %u 個あります（リンクに変わったか、"
                           L"アクセスが拒否されました）。",
                           L"有 %u 个驱动器的隔离文件夹无法读取（已变为链接或访问被拒绝）。"},
    [UI_T_Q_SELECT_ITEMS] = {L"복원하거나 영구 삭제할 항목을 선택하세요.",
                             L"Select items to restore or permanently delete.",
                             L"復元または完全に削除する項目を選んでください。",
                             L"请选择要恢复或永久删除的项目。"},
    [UI_T_Q_EMPTY] = {L"격리 폴더에 항목이 없습니다.", L"There are no items in the quarantine folder.",
                      L"隔離フォルダーに項目はありません。", L"隔离文件夹中没有项目。"},
    [UI_T_Q_SELECT_RESTORE] = {L"복원할 항목을 목록에서 선택하세요.",
                               L"Select the items to restore from the list.",
                               L"復元する項目を一覧から選んでください。", L"请从列表中选择要恢复的项目。"},
    [UI_T_Q_RESTORED] = {L"복원 완료: 성공 %u개 · 같은 이름이 있어 건너뜀 %u개 · 실패 %u개 · 복원할 수 없음 %u개",
                         L"Restore finished: succeeded %u · skipped (same name exists) %u · failed %u · "
                         L"can't be restored %u",
                         L"復元完了: 成功 %u 個 · 同じ名前があるためスキップ %u 個 · 失敗 %u 個 · "
                         L"復元できない %u 個",
                         L"恢复完成：成功 %u 个 · 因同名而跳过 %u 个 · 失败 %u 个 · 无法恢复 %u 个"},
    [UI_T_Q_SELECT_PURGE] = {L"영구 삭제할 항목을 목록에서 선택하세요.",
                             L"Select the items to permanently delete from the list.",
                             L"完全に削除する項目を一覧から選んでください。",
                             L"请从列表中选择要永久删除的项目。"},
    [UI_T_Q_PURGE_CANCELLED] = {L"영구 삭제를 취소했습니다. 아무것도 바뀌지 않았습니다.",
                                L"Permanent deletion was cancelled. Nothing was changed.",
                                L"完全な削除をキャンセルしました。何も変更されていません。",
                                L"已取消永久删除。没有任何更改。"},
    [UI_T_Q_PURGE_NO_MEMORY] = {L"영구 삭제를 시작하지 못했습니다(메모리 부족).",
                                L"Couldn't start permanent deletion (out of memory).",
                                L"完全な削除を開始できませんでした（メモリ不足）。",
                                L"无法开始永久删除（内存不足）。"},
    [UI_T_Q_PURGE_NO_THREAD] = {L"영구 삭제를 시작하지 못했습니다(스레드 생성 실패).",
                                L"Couldn't start permanent deletion (couldn't create a thread).",
                                L"完全な削除を開始できませんでした（スレッドを作成できません）。",
                                L"无法开始永久删除（无法创建线程）。"},
    [UI_T_Q_PURGING] = {L"영구 삭제하는 중... 항목 %u/%u · 지운 파일·폴더 %llu개(%ls) · %ls",
                        L"Permanently deleting... item %u/%u · deleted files and folders %llu (%ls) · %ls",
                        L"完全に削除しています... 項目 %u/%u · 削除したファイル・フォルダー %llu 個（%ls） · %ls",
                        L"正在永久删除... 项目 %u/%u · 已删除的文件和文件夹 %llu 个（%ls） · %ls"},
    [UI_T_Q_STOPPING] = {L"영구 삭제를 멈추는 중... 지우고 있던 파일까지만 지우고, 남은 "
                         L"파일은 격리 폴더에 그대로 둡니다.",
                         L"Stopping permanent deletion... Only the file being deleted is finished; the "
                         L"remaining files stay in the quarantine folder.",
                         L"完全な削除を停止しています... 削除中のファイルまでを削除し、残りのファイルは"
                         L"隔離フォルダーにそのまま残します。",
                         L"正在停止永久删除... 只删除完正在删除的文件，其余文件保留在隔离文件夹中。"},
    [UI_T_Q_PURGE_STOPPED] = {L"영구 삭제 취소: 성공 %u개 · 취소 %u개(남은 항목은 격리 폴더에 그대로) · "
                              L"실패 %u개",
                              L"Permanent deletion cancelled: succeeded %u · cancelled %u (remaining items "
                              L"stay in the quarantine folder) · failed %u",
                              L"完全な削除をキャンセル: 成功 %u 個 · キャンセル %u 個（残りの項目は隔離"
                              L"フォルダーにそのまま） · 失敗 %u 個",
                              L"永久删除已取消：成功 %u 个 · 取消 %u 个（其余项目保留在隔离文件夹中） · "
                              L"失败 %u 个"},
    [UI_T_Q_PURGED] = {L"영구 삭제 완료: 성공 %u개 · 실패 %u개",
                       L"Permanent deletion finished: succeeded %u · failed %u",
                       L"完全な削除が完了: 成功 %u 個 · 失敗 %u 個",
                       L"永久删除完成：成功 %u 个 · 失败 %u 个"},

    /* ---- 격리 폴더: 결과 열 ---- */
    [UI_T_Q_HEAD_RESTORE_FAILED] = {L"복원하지 못함", L"Couldn't restore", L"復元できません",
                                    L"无法恢复"},
    [UI_T_Q_HEAD_ALT_FAILED] = {L"다른 이름으로 복원하지 못함", L"Couldn't restore under another name",
                                L"別の名前で復元できません", L"无法以其他名称恢复"},
    [UI_T_Q_HEAD_PURGE_FAILED] = {L"영구 삭제하지 못함", L"Couldn't permanently delete",
                                  L"完全に削除できません", L"无法永久删除"},
    [UI_T_Q_NOTE_NO_ORIGIN] = {L"복원할 수 없음: 원래 위치 기록이 없습니다. [격리 폴더 열기]로 "
                               L"직접 확인하세요.",
                               L"Can't restore: there's no record of the original location. Check it "
                               L"yourself with [Open quarantine folder].",
                               L"復元できません: 元の場所の記録がありません。[隔離フォルダーを開く] で"
                               L"直接確認してください。",
                               L"无法恢复：没有原始位置的记录。请通过 [打开隔离文件夹] 自行确认。"},
    [UI_T_Q_NOTE_NO_FILE] = {L"복원할 수 없음: 격리된 파일이 없습니다(기록만 남음).",
                             L"Can't restore: the quarantined file is missing (only the record remains).",
                             L"復元できません: 隔離したファイルがありません（記録のみ残っています）。",
                             L"无法恢复：隔离的文件不存在（仅剩记录）。"},
    [UI_T_Q_NOTE_SKIPPED] = {L"건너뜀: 원래 위치에 같은 이름이 있음(덮어쓰지 않음). 기존 항목을 옮긴 뒤 "
                             L"다시 복원하세요.",
                             L"Skipped: an item with the same name exists at the original location (not "
                             L"overwritten). Move the existing item, then restore again.",
                             L"スキップ: 元の場所に同じ名前があります（上書きしません）。既存の項目を移動"
                             L"してから、もう一度復元してください。",
                             L"已跳过：原始位置存在同名项目（不覆盖）。请移走现有项目后再恢复。"},
    [UI_T_Q_NOTE_CANCEL_PARTIAL] = {L"취소함: 일부 파일만 지웠습니다. 남은 파일과 기록은 격리 폴더에 "
                                    L"그대로 있습니다.",
                                    L"Cancelled: only some files were deleted. The remaining files and "
                                    L"the record stay in the quarantine folder.",
                                    L"キャンセル: 一部のファイルのみ削除しました。残りのファイルと記録は"
                                    L"隔離フォルダーにそのまま残っています。",
                                    L"已取消：只删除了部分文件。其余文件和记录保留在隔离文件夹中。"},
    [UI_T_Q_NOTE_CANCEL_NONE] = {L"취소함: 지우지 않았습니다.", L"Cancelled: nothing was deleted.",
                                 L"キャンセル: 削除していません。", L"已取消：未删除。"},
    [UI_T_Q_ST_OK] = {L"완료", L"Done", L"完了", L"完成"},
    [UI_T_Q_ST_MISSING] = {L"항목이 없음", L"Item not found", L"項目がありません", L"项目不存在"},
    [UI_T_Q_ST_CONFLICT] = {L"같은 이름이 이미 있음", L"An item with the same name already exists",
                            L"同じ名前がすでにあります", L"已存在同名项目"},
    [UI_T_Q_ST_ACCESS_DENIED] = {L"접근 거부(권한 부족·쓰기 금지·사용 중일 수 있음)",
                                 L"Access denied (no permission, write-protected, or in use)",
                                 L"アクセス拒否（権限不足・書き込み禁止・使用中の可能性）",
                                 L"访问被拒绝（可能没有权限、写保护或正在使用）"},
    [UI_T_Q_ST_IN_USE] = {L"다른 프로그램이 사용 중", L"In use by another program",
                          L"別のプログラムが使用中", L"其他程序正在使用"},
    [UI_T_Q_ST_NO_SPACE] = {L"디스크 공간 부족", L"Not enough disk space", L"ディスクの空き容量不足",
                            L"磁盘空间不足"},
    [UI_T_Q_ST_BAD_STORE] = {L"격리 폴더를 쓸 수 없음(링크·파일이거나 만들 수 없음)",
                             L"Can't use the quarantine folder (it's a link or file, or can't be created)",
                             L"隔離フォルダーを使用できません（リンク・ファイルであるか、作成できません）",
                             L"无法使用隔离文件夹（是链接或文件，或无法创建）"},
    [UI_T_Q_ST_OTHER_VOLUME] = {L"다른 드라이브라 옮길 수 없음", L"Can't move to a different drive",
                                L"別のドライブのため移動できません", L"位于其他驱动器，无法移动"},
    [UI_T_Q_ST_FAILED] = {L"실패", L"Failed", L"失敗", L"失败"},
    [UI_T_Q_ST_CANCELLED] = {L"취소함", L"Cancelled", L"キャンセル", L"已取消"},

    /* ---- 격리 폴더: 확인·결과 창 ---- */
    [UI_T_Q_MORE] = {L"... 외 %u개", L"... and %u more", L"... ほか %u 個", L"... 另外 %u 个"},
    [UI_T_Q_CONFLICT_TITLE] = {L"같은 이름이 있음", L"Same name exists", L"同じ名前があります",
                               L"存在同名项目"},
    [UI_T_Q_CONFLICT_HEAD] = {L"원래 위치에 같은 이름의 항목이 이미 있어 %u개를 복원하지 않았습니다. 기존 "
                              L"항목은 덮어쓰지 않았습니다.\n\n",
                              L"%u items weren't restored because items with the same name already exist "
                              L"at the original location. The existing items weren't overwritten.\n\n",
                              L"元の場所に同じ名前の項目がすでにあるため、%u 個を復元しませんでした。既存の"
                              L"項目は上書きしていません。\n\n",
                              L"由于原始位置已存在同名项目，%u 个项目未恢复。现有项目未被覆盖。\n\n"},
    [UI_T_Q_CONFLICT_TAIL] = {L"\n[예]: '이름%ls'처럼 다른 이름으로 같은 폴더에 복원합니다.\n"
                              L"[아니요]: 격리 폴더에 그대로 둡니다. 원래 위치의 항목을 옮기거나 이름을 바꾼 "
                              L"뒤 다시 [복원]을 누르면 원래 이름으로 복원됩니다.",
                              L"\n[Yes]: Restore to the same folder under a different name, such as "
                              L"'name%ls'.\n"
                              L"[No]: Keep them in the quarantine folder. Move or rename the item at the "
                              L"original location, then select [Restore] again to restore with the original "
                              L"name.",
                              L"\n[はい]: '名前%ls' のように別の名前で同じフォルダーに復元します。\n"
                              L"[いいえ]: 隔離フォルダーにそのまま残します。元の場所の項目を移動するか名前を"
                              L"変更してから、もう一度 [復元] を押すと元の名前で復元されます。",
                              L"\n[是]：以 '名称%ls' 这样的其他名称恢复到同一文件夹。\n"
                              L"[否]：保留在隔离文件夹中。移动或重命名原始位置的项目后，再次点击 [恢复] "
                              L"即可以原名称恢复。"},
    [UI_T_Q_RESTORE_RESULT_TITLE] = {L"복원 결과", L"Restore results", L"復元の結果", L"恢复结果"},
    [UI_T_Q_RESTORE_RESULT_HEAD] = {L"일부 항목을 복원하지 못했습니다. 이 항목들은 격리 "
                                    L"폴더에 그대로 남아 있습니다.",
                                    L"Some items couldn't be restored. They stay in the quarantine folder.",
                                    L"一部の項目を復元できませんでした。これらの項目は隔離フォルダーに"
                                    L"そのまま残っています。",
                                    L"部分项目无法恢复。这些项目仍保留在隔离文件夹中。"},
    [UI_T_Q_PURGE_TITLE] = {L"영구 삭제", L"Permanently delete", L"完全に削除", L"永久删除"},
    [UI_T_Q_PURGE_CONFIRM_HEAD] = {L"선택한 %u개 항목을 영구 삭제합니다.\n기록된 용량: %ls\n\n",
                                   L"%u selected items will be permanently deleted.\nRecorded size: %ls\n\n",
                                   L"選択した %u 個の項目を完全に削除します。\n記録されたサイズ: %ls\n\n",
                                   L"将永久删除所选的 %u 个项目。\n记录的大小：%ls\n\n"},
    [UI_T_Q_PURGE_CONFIRM_WARN] = {L"\n영구 삭제한 항목은 휴지통을 거치지 않으며 ByteSieve나 Windows에서 "
                                   L"복구할 수 없습니다.\n",
                                   L"\nPermanently deleted items don't go to the Recycle Bin and can't be "
                                   L"recovered with ByteSieve or Windows.\n",
                                   L"\n完全に削除した項目はごみ箱を経由せず、ByteSieve や Windows で元に"
                                   L"戻すことはできません。\n",
                                   L"\n永久删除的项目不会进入回收站，也无法通过 ByteSieve 或 Windows 恢复。\n"},
    [UI_T_Q_PURGE_CONFIRM_ORPHANS] = {L"\n주의: 기록이 없는 항목 %u개는 원래 위치를 알 수 없으며, 격리 폴더 안의 "
                                      L"그 항목이 통째로 지워집니다.\n",
                                      L"\nCaution: the original location of %u items without a record is "
                                      L"unknown, and those items in the quarantine folder are deleted "
                                      L"entirely.\n",
                                      L"\n注意: 記録のない項目 %u 個は元の場所が不明で、隔離フォルダー内の"
                                      L"その項目がまるごと削除されます。\n",
                                      L"\n注意：%u 个无记录项目的原始位置未知，隔离文件夹中的这些项目将被"
                                      L"整个删除。\n"},
    [UI_T_Q_PURGE_CONFIRM_ASK] = {L"\n계속할까요?", L"\nDo you want to continue?", L"\n続行しますか？",
                                  L"\n要继续吗？"},
    [UI_T_Q_PURGE_FINAL_TITLE] = {L"영구 삭제 - 마지막 확인", L"Permanently delete - final confirmation",
                                  L"完全に削除 - 最終確認", L"永久删除 - 最终确认"},
    [UI_T_Q_PURGE_FINAL] = {L"정말로 %u개 항목(%ls)을 영구 삭제할까요?\n\n이 작업은 되돌릴 수 없습니다.",
                            L"Permanently delete %u items (%ls)?\n\nThis can't be undone.",
                            L"本当に %u 個の項目（%ls）を完全に削除しますか？\n\nこの操作は元に戻せません。",
                            L"确定要永久删除 %u 个项目（%ls）吗？\n\n此操作无法撤消。"},
    [UI_T_Q_PURGE_RESULT_TITLE] = {L"영구 삭제 결과", L"Permanent deletion results", L"完全な削除の結果",
                                   L"永久删除结果"},
    [UI_T_Q_PURGE_RESULT_HEAD] = {L"일부 항목을 영구 삭제하지 못했습니다. 지우지 못한 파일은 격리 폴더에 "
                                  L"남아 있습니다.",
                                  L"Some items couldn't be permanently deleted. Files that weren't deleted "
                                  L"remain in the quarantine folder.",
                                  L"一部の項目を完全に削除できませんでした。削除できなかったファイルは"
                                  L"隔離フォルダーに残っています。",
                                  L"部分项目无法永久删除。未删除的文件仍保留在隔离文件夹中。"},

    /* ---- core 상태: 휴지통 이동 결과 ---- */
    [UI_T_TRASH_MOVED] = {L"휴지통으로 이동됨", L"Moved to the Recycle Bin", L"ごみ箱に移動済み",
                          L"已移到回收站"},
    [UI_T_TRASH_QUARANTINED] = {L"격리 폴더로 이동됨", L"Moved to the quarantine folder",
                                L"隔離フォルダーに移動済み", L"已移到隔离文件夹"},
    [UI_T_TRASH_DENY_PATH] = {L"휴지통이 지원하지 않는 경로(네트워크·너무 긴 경로)",
                              L"Path not supported by the Recycle Bin (network or too long)",
                              L"ごみ箱が対応していないパス（ネットワーク・長すぎるパス）",
                              L"回收站不支持的路径（网络或路径过长）"},
    [UI_T_TRASH_DENY_OUTSIDE] = {L"탐지 경로 밖", L"Outside the scanned folder", L"検出したフォルダーの外",
                                 L"在检测路径之外"},
    [UI_T_TRASH_DENY_PROTECTED] = {L"시스템·프로그램 보호 경로", L"Protected system or program location",
                                   L"システム・プログラムの保護された場所", L"系统或程序的受保护路径"},
    [UI_T_TRASH_DENY_CRITICAL] = {L"중요 폴더이거나 그 상위", L"Important folder or one of its parents",
                                  L"重要なフォルダーまたはその上位", L"重要文件夹或其上级"},
    [UI_T_TRASH_DENY_INCOMPLETE] = {L"하위를 다 읽지 못한 항목", L"Contents couldn't be fully read",
                                    L"下位をすべて読み取れなかった項目", L"未能完整读取子项的项目"},
    [UI_T_TRASH_DENY_DRIVE] = {L"휴지통·격리 폴더를 쓸 수 없는 드라이브(네트워크·CD 등)",
                               L"Drive without a Recycle Bin or quarantine folder (network, CD, etc.)",
                               L"ごみ箱・隔離フォルダーを使えないドライブ（ネットワーク・CD など）",
                               L"无法使用回收站或隔离文件夹的驱动器（网络、CD 等）"},
    [UI_T_TRASH_MISSING] = {L"이미 없음", L"Already gone", L"すでにありません", L"已不存在"},
    [UI_T_TRASH_CHANGED] = {L"탐지 후 바뀜(링크·종류·실제 위치)",
                            L"Changed after detection (link, type, or actual location)",
                            L"検出後に変更（リンク・種類・実際の場所）", L"检测后已更改（链接、类型或实际位置）"},
    [UI_T_TRASH_NO_RECYCLE] = {L"휴지통에 넣을 수 없음(영구 삭제 방지)",
                               L"Can't go to the Recycle Bin (prevents permanent deletion)",
                               L"ごみ箱に入れられません（完全な削除を防止）", L"无法放入回收站（防止永久删除）"},
    [UI_T_TRASH_FAILED] = {L"이동 실패", L"Move failed", L"移動に失敗", L"移动失败"},
    [UI_T_TRASH_QUARANTINE_FAILED] = {L"격리 폴더로 옮기지 못함", L"Couldn't move to the quarantine folder",
                                      L"隔離フォルダーに移動できません", L"无法移到隔离文件夹"},
    [UI_T_TRASH_CANCELLED] = {L"취소됨", L"Cancelled", L"キャンセル済み", L"已取消"},
    [UI_T_RECOVER_NONE] = {L"다시 생기지 않음", L"Won't be recreated", L"再作成されません", L"不会重新生成"},
    [UI_T_RECOVER_REBUILD] = {L"빌드하면 다시 생김", L"Recreated by building", L"ビルドすると再作成",
                              L"构建后会重新生成"},
    [UI_T_RECOVER_LOCKED] = {L"lockfile로 같은 버전 재설치", L"Same versions reinstalled from the lockfile",
                             L"lockfile で同じバージョンを再インストール", L"可按 lockfile 重新安装相同版本"},
    [UI_T_RECOVER_UNLOCKED] = {L"재설치 가능(버전이 바뀔 수 있음)", L"Can be reinstalled (versions may change)",
                               L"再インストール可能（バージョンが変わる場合あり）",
                               L"可重新安装（版本可能会变化）"},

    /* ---- 프로젝트 정리: 단추·입력 칸·필터·열 머리글 ---- */
    [UI_T_C_DETECT] = {L"탐지", L"Detect", L"検出", L"检测"},
    [UI_T_C_PATH_CUE] = {L"정리 후보를 찾을 폴더 경로", L"Folder path to search for cleanup candidates",
                         L"整理候補を探すフォルダーのパス", L"要查找清理候选项的文件夹路径"},
    [UI_T_C_READY] = {L"폴더를 선택하고 [탐지]를 누르세요.", L"Choose a folder and select [Detect].",
                      L"フォルダーを選んで [検出] を押してください。", L"请选择文件夹，然后点击 [检测]。"},
    [UI_T_C_FILTER_LABEL] = {L"유형:", L"Type:", L"種類:", L"类型："},
    [UI_T_C_ECO_LABEL] = {L"환경:", L"Stack:", L"環境:", L"环境："},
    [UI_T_C_CHECK_ALL] = {L"모두 선택", L"Select all", L"すべて選択", L"全选"},
    [UI_T_C_CHECK_NONE] = {L"선택 해제", L"Clear selection", L"選択を解除", L"取消选择"},
    [UI_T_C_TRASH] = {L"휴지통으로 이동(&D)...", L"&Move to Recycle Bin...", L"ごみ箱に移動(&D)...",
                      L"移到回收站(&D)..."},
    [UI_T_C_ALL] = {L"전체", L"All", L"すべて", L"全部"},
    [UI_T_C_FILTER_BUILD] = {L"빌드 결과물 (dist·build·target·bin·obj 등)",
                             L"Build output (dist, build, target, bin, obj, etc.)",
                             L"ビルド成果物 (dist・build・target・bin・obj など)",
                             L"构建产物 (dist、build、target、bin、obj 等)"},
    [UI_T_C_FILTER_CACHE] = {L"개발 캐시 (.cache·.next·__pycache__ 등)",
                             L"Dev caches (.cache, .next, __pycache__, etc.)",
                             L"開発キャッシュ (.cache・.next・__pycache__ など)",
                             L"开发缓存 (.cache、.next、__pycache__ 等)"},
    [UI_T_C_FILTER_TEMP] = {L"임시 파일 (.tmp·.temp·*.tmp)", L"Temporary files (.tmp, .temp, *.tmp)",
                            L"一時ファイル (.tmp・.temp・*.tmp)", L"临时文件 (.tmp、.temp、*.tmp)"},
    [UI_T_C_FILTER_LOG] = {L"로그 (*.log)", L"Logs (*.log)", L"ログ (*.log)", L"日志 (*.log)"},
    [UI_T_C_CAT_BUILD] = {L"빌드 결과물", L"Build output", L"ビルド成果物", L"构建产物"},
    [UI_T_C_CAT_CACHE] = {L"개발 캐시", L"Dev cache", L"開発キャッシュ", L"开发缓存"},
    [UI_T_C_CAT_TEMP] = {L"임시 파일", L"Temporary files", L"一時ファイル", L"临时文件"},
    [UI_T_C_CAT_LOG] = {L"로그", L"Logs", L"ログ", L"日志"},
    [UI_T_C_ECO_GENERIC] = {L"일반", L"General", L"一般", L"通用"},
    [UI_T_C_COL_RECLAIM] = {L"실제 확보", L"Reclaimable", L"実際の解放量", L"实际释放"},
    [UI_T_C_COL_SIZE] = {L"파일 크기", L"File size", L"ファイルサイズ", L"文件大小"},
    [UI_T_C_COL_TYPE] = {L"유형", L"Type", L"種類", L"类型"},
    [UI_T_C_COL_ECO] = {L"개발 환경", L"Stack", L"開発環境", L"开发环境"},
    [UI_T_C_COL_PATH] = {L"경로", L"Path", L"パス", L"路径"},
    [UI_T_C_COL_NOTE] = {L"비고", L"Notes", L"備考", L"备注"},
    [UI_T_C_COL_RECOVERY] = {L"복구", L"Recovery", L"復旧", L"恢复"},
    [UI_T_C_COL_PROJECT] = {L"프로젝트", L"Project", L"プロジェクト", L"项目"},
    [UI_T_C_COL_ACTIVITY] = {L"마지막 활동", L"Last activity", L"最終アクティビティ", L"最近活动"},
    [UI_T_C_COL_CLEAN] = {L"정리", L"Cleanup", L"整理", L"清理"},

    /* ---- 프로젝트 정리: 목록 칸 ---- */
    [UI_T_C_NOTE_SYSTEM] = {L"시스템 경로", L"System location", L"システムの場所", L"系统路径"},
    [UI_T_C_NOTE_CONFIRMED] = {L"확인됨", L"Confirmed", L"確認済み", L"已确认"},
    [UI_T_C_NOTE_GUESSED] = {L"추정", L"Guessed", L"推定", L"推测"},
    [UI_T_C_NOTE_INCOMPLETE] = {L"일부 누락", L"Incomplete", L"一部欠落", L"部分缺失"},
    [UI_T_C_NOTE_SHARED] = {L"하드 링크 공유", L"Shares hard links", L"ハードリンク共有", L"共享硬链接"},
    [UI_T_C_NOTE_DIRTY] = {L"미커밋 변경", L"Uncommitted changes", L"未コミットの変更", L"未提交的更改"},
    [UI_T_C_NOTE_UNPUSHED] = {L"미푸시 커밋", L"Unpushed commits", L"未プッシュのコミット", L"未推送的提交"},
    [UI_T_C_NOTE_DORMANT] = {L"휴면", L"Dormant", L"休眠", L"休眠"},
    [UI_T_C_ACTIVITY_UNKNOWN] = {L"모름", L"Unknown", L"不明", L"未知"},
    [UI_T_C_ACTIVITY] = {L"%04u-%02u-%02u · %u일 전%ls", L"%04u-%02u-%02u · %u days ago%ls",
                         L"%04u-%02u-%02u · %u 日前%ls", L"%04u-%02u-%02u · %u 天前%ls"},
    [UI_T_C_CLEAN_FAILED] = {L"실패: %ls (0x%08X)", L"Failed: %ls (0x%08X)", L"失敗: %ls (0x%08X)",
                             L"失败：%ls (0x%08X)"},
    [UI_T_C_CLEAN_MOVE_FAILED] = {L"이동하지 못함", L"Couldn't move", L"移動できません", L"无法移动"},
    [UI_T_C_CLEAN_QFAILED] = {L"실패: %ls - %ls (0x%08X)", L"Failed: %ls - %ls (0x%08X)",
                              L"失敗: %ls - %ls (0x%08X)", L"失败：%ls - %ls (0x%08X)"},
    [UI_T_C_UNKNOWN_ERROR] = {L"알 수 없는 오류", L"Unknown error", L"不明なエラー", L"未知错误"},
    [UI_T_C_CLEAN_SKIPPED] = {L"건너뜀: %ls", L"Skipped: %ls", L"スキップ: %ls", L"已跳过：%ls"},
    [UI_T_C_CLEAN_PROTECTED] = {L"보호: %ls", L"Protected: %ls", L"保護: %ls", L"受保护：%ls"},

    /* ---- 프로젝트 정리: 요약·선택·경고 줄 ---- */
    [UI_T_C_SUMMARY] = {L"표시 %u개 · %ls   /   전체 %u개 · 실제 확보 %ls (파일 크기 %ls)",
                        L"Showing %u · %ls   /   total %u · reclaimable %ls (file size %ls)",
                        L"表示 %u 個 · %ls   /   全体 %u 個 · 実際の解放量 %ls（ファイルサイズ %ls）",
                        L"显示 %u 个 · %ls   /   全部 %u 个 · 实际释放 %ls（文件大小 %ls）"},
    [UI_T_C_SEL] = {L"선택 %u개 · 실제 확보 %ls", L"Selected %u · reclaimable %ls",
                    L"選択 %u 個 · 実際の解放量 %ls", L"已选 %u 个 · 实际释放 %ls"},
    [UI_T_C_SEL_MOVED] = {L"선택 %u개 · 실제 확보 %ls   /   휴지통으로 옮김 %u개 · %ls",
                          L"Selected %u · reclaimable %ls   /   moved to Recycle Bin %u · %ls",
                          L"選択 %u 個 · 実際の解放量 %ls   /   ごみ箱に移動 %u 個 · %ls",
                          L"已选 %u 个 · 实际释放 %ls   /   已移到回收站 %u 个 · %ls"},
    [UI_T_C_SEL_MOVED_Q] = {L"선택 %u개 · 실제 확보 %ls   /   휴지통으로 옮김 %u개 · %ls (격리 폴더 %u개)",
                            L"Selected %u · reclaimable %ls   /   moved to Recycle Bin %u · %ls "
                            L"(quarantine folder %u)",
                            L"選択 %u 個 · 実際の解放量 %ls   /   ごみ箱に移動 %u 個 · %ls（隔離フォルダー %u 個）",
                            L"已选 %u 个 · 实际释放 %ls   /   已移到回收站 %u 个 · %ls（隔离文件夹 %u 个）"},
    [UI_T_C_WARN_PROTECTED] = {L"경고: 시스템·프로그램 보호 경로 안입니다. 표시된 항목은 Windows나 "
                               L"프로그램에 필요할 수 있어 휴지통으로 옮길 수 없습니다.",
                               L"Warning: this is inside a protected system or program location. The listed "
                               L"items may be needed by Windows or programs, so they can't be moved to the "
                               L"Recycle Bin.",
                               L"警告: システム・プログラムの保護された場所の中です。表示された項目は Windows や"
                               L"プログラムに必要な場合があるため、ごみ箱に移動できません。",
                               L"警告：位于系统或程序的受保护路径内。列出的项目可能是 Windows 或程序所需的，"
                               L"因此无法移到回收站。"},
    [UI_T_C_WARN_INFO] = {L"탐지는 읽기 전용입니다. 체크한 항목만 확인을 거쳐 휴지통(없는 "
                          L"드라이브는 격리 폴더)으로 옮기며 영구 삭제하지 않습니다. "
                          L"시스템·중요 폴더는 보호합니다.",
                          L"Detection is read-only. Only checked items are moved, after confirmation, to the "
                          L"Recycle Bin (or the quarantine folder on drives without one); nothing is "
                          L"permanently deleted. System and important folders are protected.",
                          L"検出は読み取り専用です。チェックした項目のみ、確認後にごみ箱（ごみ箱のない"
                          L"ドライブは隔離フォルダー）に移動し、完全には削除しません。システム・重要な"
                          L"フォルダーは保護します。",
                          L"检测为只读操作。只有勾选的项目会在确认后移到回收站（没有回收站的驱动器移到隔离"
                          L"文件夹），不会永久删除。系统和重要文件夹受保护。"},

    /* ---- 프로젝트 정리: 상태 줄 ---- */
    [UI_T_C_NEED_PATH] = {L"탐지할 폴더를 입력하거나 [찾아보기]로 선택하세요.",
                          L"Enter a folder to detect in, or choose one with [Browse...].",
                          L"検出するフォルダーを入力するか、[参照...] で選んでください。",
                          L"请输入要检测的文件夹，或通过 [浏览...] 选择。"},
    [UI_T_C_START_FAILED] = {L"탐지를 시작하지 못했습니다(메모리 또는 스레드 생성 실패).",
                             L"Couldn't start detection (out of memory or couldn't create a thread).",
                             L"検出を開始できませんでした（メモリ不足またはスレッドを作成できません）。",
                             L"无法开始检测（内存不足或无法创建线程）。"},
    [UI_T_C_DETECTING] = {L"탐지 중...", L"Detecting...", L"検出中...", L"正在检测..."},
    [UI_T_C_PROGRESS] = {L"탐지 중... 폴더 %llu · 파일 %llu · 후보 %u개 · %ls · 오류 %llu  |  %ls",
                         L"Detecting... folders %llu · files %llu · candidates %u · %ls · errors %llu  |  %ls",
                         L"検出中... フォルダー %llu · ファイル %llu · 候補 %u 個 · %ls · エラー %llu  |  %ls",
                         L"正在检测... 文件夹 %llu · 文件 %llu · 候选 %u 个 · %ls · 错误 %llu  |  %ls"},
    [UI_T_C_GIT_PROGRESS] = {L"프로젝트(Git) 분석 중... %u/%u · 후보 %u개",
                             L"Analyzing projects (Git)... %u/%u · candidates %u",
                             L"プロジェクト（Git）を分析中... %u/%u · 候補 %u 個",
                             L"正在分析项目（Git）... %u/%u · 候选 %u 个"},
    [UI_T_C_HEAD_CANCELLED] = {L"취소됨(그때까지 찾은 결과)", L"Cancelled (results found so far)",
                               L"キャンセル済み（その時点までの結果）", L"已取消（截至取消时的结果）"},
    [UI_T_C_HEAD_FAILED] = {L"탐지 실패", L"Detection failed", L"検出に失敗", L"检测失败"},
    [UI_T_C_FINISHED] = {L"%ls: 폴더 %llu · 파일 %llu · 오류 %llu · 건너뛴 폴더 %llu · "
                         L"제외한 시스템 폴더 %llu · 링크 %llu · %.1f초",
                         L"%ls: folders %llu · files %llu · errors %llu · skipped folders %llu · "
                         L"excluded system folders %llu · links %llu · %.1f s",
                         L"%ls: フォルダー %llu · ファイル %llu · エラー %llu · スキップしたフォルダー %llu · "
                         L"除外したシステムフォルダー %llu · リンク %llu · %.1f 秒",
                         L"%ls：文件夹 %llu · 文件 %llu · 错误 %llu · 跳过的文件夹 %llu · "
                         L"排除的系统文件夹 %llu · 链接 %llu · %.1f 秒"},
    [UI_T_C_CANCELLING_TRASH] = {L"취소하는 중... 옮기고 있던 항목은 끝까지 옮깁니다.",
                                 L"Cancelling... The item being moved will be finished.",
                                 L"キャンセルしています... 移動中の項目は最後まで移動します。",
                                 L"正在取消... 正在移动的项目将完成移动。"},
    [UI_T_C_NOTHING_CHECKED] = {L"휴지통으로 옮길 항목을 목록에서 체크하세요.",
                                L"Check the items to move to the Recycle Bin in the list.",
                                L"ごみ箱に移動する項目を一覧でチェックしてください。",
                                L"请在列表中勾选要移到回收站的项目。"},
    [UI_T_C_TRASH_CANCELLED] = {L"휴지통 이동을 취소했습니다. 아무것도 바뀌지 않았습니다.",
                                L"Moving to the Recycle Bin was cancelled. Nothing was changed.",
                                L"ごみ箱への移動をキャンセルしました。何も変更されていません。",
                                L"已取消移到回收站。没有任何更改。"},
    [UI_T_C_TRASH_START_FAILED] = {L"휴지통 이동을 시작하지 못했습니다(메모리 또는 스레드 생성 실패).",
                                   L"Couldn't start moving to the Recycle Bin (out of memory or couldn't "
                                   L"create a thread).",
                                   L"ごみ箱への移動を開始できませんでした（メモリ不足またはスレッドを作成できません）。",
                                   L"无法开始移到回收站（内存不足或无法创建线程）。"},
    [UI_T_C_TRASH_PROGRESS] = {L"휴지통으로 옮기는 중... %u/%u · 이동 %u개(%ls, 격리 폴더 %u개) · 실패 %u개",
                               L"Moving to the Recycle Bin... %u/%u · moved %u (%ls, quarantine folder %u) · "
                               L"failed %u",
                               L"ごみ箱に移動しています... %u/%u · 移動 %u 個（%ls、隔離フォルダー %u 個） · "
                               L"失敗 %u 個",
                               L"正在移到回收站... %u/%u · 已移动 %u 个（%ls，隔离文件夹 %u 个） · 失败 %u 个"},
    [UI_T_C_TRASHED] = {L"휴지통으로 이동 완료: 성공 %u개(%ls 확보) · 실패 %u개 · 취소 %u개 · %.1f초",
                        L"Moved to the Recycle Bin: succeeded %u (%ls reclaimable) · failed %u · "
                        L"cancelled %u · %.1f s",
                        L"ごみ箱への移動が完了: 成功 %u 個（%ls 解放） · 失敗 %u 個 · キャンセル %u 個 · %.1f 秒",
                        L"移到回收站完成：成功 %u 个（释放 %ls） · 失败 %u 个 · 取消 %u 个 · %.1f 秒"},
    [UI_T_C_TRASHED_Q] = {L"휴지통으로 이동 완료: 성공 %u개(%ls 확보) · 실패 %u개 · 취소 %u개 · %.1f초"
                          L"  |  휴지통이 없어 격리 폴더로 옮김 %u개([격리 폴더] 페이지에서 복원·영구 삭제)",
                          L"Moved to the Recycle Bin: succeeded %u (%ls reclaimable) · failed %u · "
                          L"cancelled %u · %.1f s  |  moved to the quarantine folder (no Recycle Bin) %u "
                          L"(restore or permanently delete on the [Quarantine] page)",
                          L"ごみ箱への移動が完了: 成功 %u 個（%ls 解放） · 失敗 %u 個 · キャンセル %u 個 · %.1f 秒"
                          L"  |  ごみ箱がないため隔離フォルダーに移動 %u 個（[隔離フォルダー] ページで復元・完全に削除）",
                          L"移到回收站完成：成功 %u 个（释放 %ls） · 失败 %u 个 · 取消 %u 个 · %.1f 秒"
                          L"  |  因没有回收站而移到隔离文件夹 %u 个（可在 [隔离文件夹] 页面恢复或永久删除）"},

    /* ---- 프로젝트 정리: 확인·결과 창 ---- */
    [UI_T_C_CONFIRM_HEAD] = {L"선택한 %u개 항목을 Windows 휴지통으로 옮깁니다.\n실제 확보 용량: %ls"
                             L"(휴지통·격리 폴더를 비우면 확보)\n\n",
                             L"%u selected items will be moved to the Windows Recycle Bin.\nReclaimable space: "
                             L"%ls (freed when the Recycle Bin or quarantine folder is emptied)\n\n",
                             L"選択した %u 個の項目を Windows のごみ箱に移動します。\n実際の解放量: %ls"
                             L"（ごみ箱・隔離フォルダーを空にすると解放）\n\n",
                             L"将把所选的 %u 个项目移到 Windows 回收站。\n实际释放空间：%ls"
                             L"（清空回收站或隔离文件夹后释放）\n\n"},
    [UI_T_C_CONFIRM_INFO] = {L"\n영구 삭제하지 않으며 휴지통에서 복원할 수 있습니다. 옮기기 직전에 항목을 다시 "
                             L"확인해 바뀐 항목·보호 대상은 건너뜁니다.\n휴지통이 없는 드라이브(USB 등)이거나 "
                             L"휴지통에 넣을 수 없는 항목은 같은 드라이브의 격리 폴더(.bytesieve-trash)로 옮기며, "
                             L"[격리 폴더] 탭에서 복원하거나 영구 삭제할 수 있습니다.\n",
                             L"\nNothing is permanently deleted, and items can be restored from the Recycle Bin. "
                             L"Each item is checked again just before moving, and changed or protected items "
                             L"are skipped.\nItems on drives without a Recycle Bin (such as USB drives), or "
                             L"that can't go to the Recycle Bin, are moved to the quarantine folder "
                             L"(.bytesieve-trash) on the same drive, where you can restore or permanently "
                             L"delete them on the [Quarantine] tab.\n",
                             L"\n完全には削除せず、ごみ箱から復元できます。移動の直前に項目を再確認し、変更された"
                             L"項目・保護対象はスキップします。\nごみ箱のないドライブ（USB など）の項目や、ごみ箱に"
                             L"入れられない項目は同じドライブの隔離フォルダー（.bytesieve-trash）に移動し、"
                             L"[隔離フォルダー] タブで復元または完全に削除できます。\n",
                             L"\n不会永久删除，可从回收站恢复。移动前会再次检查项目，已更改的项目和受保护的"
                             L"项目将被跳过。\n没有回收站的驱动器（如 USB）上的项目或无法放入回收站的项目，"
                             L"会移到同一驱动器的隔离文件夹（.bytesieve-trash），可在 [隔离文件夹] 选项卡中"
                             L"恢复或永久删除。\n"},
    [UI_T_C_CONFIRM_GUESSED] = {L"\n주의: '추정' 항목 %u개는 같은 폴더에 프로젝트 파일이 없어 빌드 결과물이 "
                                L"아닐 수 있습니다.\n",
                                L"\nCaution: %u 'Guessed' items have no project file in the same folder and may "
                                L"not be build output.\n",
                                L"\n注意: 「推定」の項目 %u 個は同じフォルダーにプロジェクトファイルがないため、"
                                L"ビルド成果物ではない可能性があります。\n",
                                L"\n注意：%u 个“推测”项目所在文件夹中没有项目文件，可能不是构建产物。\n"},
    [UI_T_C_CONFIRM_DIRTY] = {L"\n주의: %u개는 커밋하지 않은 변경이 있는(작업 중인) 프로젝트의 항목입니다.\n",
                              L"\nCaution: %u items belong to projects with uncommitted changes (work in "
                              L"progress).\n",
                              L"\n注意: %u 個はコミットしていない変更がある（作業中の）プロジェクトの項目です。\n",
                              L"\n注意：%u 个项目属于有未提交更改的（正在进行中的）项目。\n"},
    [UI_T_C_CONFIRM_UNPUSHED] = {L"\n참고: %u개는 원격에 올리지 않은 커밋이 있는 프로젝트의 항목입니다(정리 항목만 "
                                 L"옮기며 소스와 커밋은 그대로입니다).\n",
                                 L"\nNote: %u items belong to projects with commits not pushed to a remote (only "
                                 L"the cleanup items are moved; sources and commits stay as they are).\n",
                                 L"\n参考: %u 個はリモートにプッシュしていないコミットがあるプロジェクトの項目です"
                                 L"（整理項目のみ移動し、ソースとコミットはそのままです）。\n",
                                 L"\n提示：%u 个项目属于有未推送到远程的提交的项目（只移动清理项目，源代码和"
                                 L"提交保持不变）。\n"},
    [UI_T_C_TRASH_TITLE] = {L"휴지통으로 이동", L"Move to Recycle Bin", L"ごみ箱に移動", L"移到回收站"},
    [UI_T_C_FAILURES_HEAD] = {L"%u개 항목을 휴지통으로 옮기지 못했습니다. 이 항목들은 그대로 "
                              L"남아 있습니다.\n\n",
                              L"%u items couldn't be moved to the Recycle Bin. They remain where they "
                              L"were.\n\n",
                              L"%u 個の項目をごみ箱に移動できませんでした。これらの項目はそのまま残っています。\n\n",
                              L"%u 个项目无法移到回收站。这些项目保持原样。\n\n"},
    [UI_T_C_FAILURES_TITLE] = {L"휴지통 이동 결과", L"Recycle Bin results", L"ごみ箱への移動の結果",
                               L"移到回收站的结果"},
    [UI_T_C_BROWSE_TITLE] = {L"정리 후보를 찾을 폴더를 선택하세요.",
                             L"Choose a folder to search for cleanup candidates.",
                             L"整理候補を探すフォルダーを選んでください。", L"请选择要查找清理候选项的文件夹。"},

    /* ---- core: Cleaner 대상 종류 이름·설명 ---- */
    [UI_T_K_NAME_USER_TEMP] = {L"사용자 임시 파일", L"User temporary files", L"ユーザーの一時ファイル",
                               L"用户临时文件"},
    [UI_T_K_NAME_WER] = {L"Windows 오류 보고서", L"Windows Error Reporting files", L"Windows エラー報告",
                         L"Windows 错误报告"},
    [UI_T_K_NAME_RECYCLE] = {L"휴지통", L"Recycle Bin", L"ごみ箱", L"回收站"},
    [UI_T_K_NAME_CHROME] = {L"Chrome 캐시", L"Chrome cache", L"Chrome キャッシュ", L"Chrome 缓存"},
    [UI_T_K_NAME_EDGE] = {L"Edge 캐시", L"Edge cache", L"Edge キャッシュ", L"Edge 缓存"},
    [UI_T_K_NAME_FIREFOX] = {L"Firefox 캐시", L"Firefox cache", L"Firefox キャッシュ", L"Firefox 缓存"},
    [UI_T_K_NAME_DEV] = {L"개발 도구 캐시", L"Developer tool caches", L"開発ツールのキャッシュ",
                         L"开发工具缓存"},
    [UI_T_K_NAME_VDISK] = {L"가상 디스크", L"Virtual disk", L"仮想ディスク", L"虚拟磁盘"},
    [UI_T_K_NOTE_USER_TEMP] =
        {L"프로그램이 작업 중에 만들고 지우지 않은 임시 파일(%TEMP%)입니다. 최근 24시간 안에 바뀐 "
         L"항목은 지금 쓰고 있을 수 있어 빼고, 사용 중이라 옮기지 못한 파일은 건너뜁니다.",
         L"Temporary files (%TEMP%) that programs created while working and didn't delete. Items "
         L"changed in the last 24 hours may be in use, so they're excluded, and files that can't be "
         L"moved because they're in use are skipped.",
         L"プログラムが作業中に作成して削除しなかった一時ファイル（%TEMP%）です。最近 24 時間以内に"
         L"変更された項目は使用中の可能性があるため除外し、使用中で移動できないファイルはスキップします。",
         L"程序在运行时创建但未删除的临时文件（%TEMP%）。最近 24 小时内更改过的项目可能正在使用，因此"
         L"将其排除；因正在使用而无法移动的文件会被跳过。"},
    [UI_T_K_NOTE_WER] =
        {L"프로그램이 비정상 종료될 때 Windows가 이 계정에 남긴 보고서입니다. 정리해도 프로그램 "
         L"동작에는 영향이 없지만, 아직 보내지 않은 보고서는 보내지 않게 됩니다.",
         L"Reports that Windows saved for this account when programs crashed. Cleaning them up doesn't "
         L"affect how programs work, but reports that haven't been sent yet won't be sent.",
         L"プログラムが異常終了したときに Windows がこのアカウントに残したレポートです。整理しても"
         L"プログラムの動作には影響しませんが、まだ送信していないレポートは送信されなくなります。",
         L"程序异常退出时 Windows 为此帐户留下的报告。清理不会影响程序运行，但尚未发送的报告将不再发送。"},
    [UI_T_K_NOTE_RECYCLE] =
        {L"모든 드라이브의 휴지통을 비웁니다. 휴지통의 파일은 영구 삭제되어 복원할 수 없습니다"
         L"(ByteSieve가 이전에 휴지통으로 옮긴 항목 포함). 다른 항목은 비운 뒤에 휴지통으로 옮기므로 "
         L"이번에 정리한 항목은 복원할 수 있습니다.",
         L"Empties the Recycle Bin on all drives. Files in the Recycle Bin are permanently deleted and "
         L"can't be restored (including items ByteSieve moved there earlier). Other items are moved to "
         L"the Recycle Bin after it's emptied, so items cleaned up this time can be restored.",
         L"すべてのドライブのごみ箱を空にします。ごみ箱のファイルは完全に削除され、復元できません"
         L"（ByteSieve が以前ごみ箱に移動した項目を含む）。ほかの項目は空にした後でごみ箱に移動するため、"
         L"今回整理した項目は復元できます。",
         L"清空所有驱动器的回收站。回收站中的文件将被永久删除且无法恢复（包括 ByteSieve 之前移到回收站的"
         L"项目）。其他项目会在清空后移到回收站，因此本次清理的项目可以恢复。"},
    [UI_T_K_NOTE_CHROME] =
        {L"웹 페이지·이미지·스크립트 캐시(Cache·Code Cache·GPUCache)입니다. 로그인·북마크·방문 기록은 "
         L"그대로이며, 다음 방문 때 다시 내려받아 처음에는 조금 느릴 수 있습니다. Chrome이 실행 중이면 "
         L"정리하지 않습니다.",
         L"Cache of web pages, images, and scripts (Cache, Code Cache, GPUCache). Sign-ins, bookmarks, "
         L"and history are kept; pages are downloaded again on your next visit, so they may be a little "
         L"slower at first. Not cleaned up while Chrome is running.",
         L"Web ページ・画像・スクリプトのキャッシュ（Cache・Code Cache・GPUCache）です。ログイン・"
         L"ブックマーク・閲覧履歴はそのままで、次回の訪問時に再ダウンロードするため最初は少し遅くなる"
         L"ことがあります。Chrome が実行中の場合は整理しません。",
         L"网页、图片和脚本缓存（Cache、Code Cache、GPUCache）。登录信息、书签和浏览记录保持不变，下次"
         L"访问时会重新下载，因此一开始可能稍慢。Chrome 正在运行时不清理。"},
    [UI_T_K_NOTE_EDGE] =
        {L"웹 페이지·이미지·스크립트 캐시(Cache·Code Cache·GPUCache)입니다. 로그인·즐겨찾기·검색 기록은 "
         L"그대로입니다. Edge는 창을 닫아도 백그라운드에서 실행될 수 있으며(시작 부스트), 실행 중이면 "
         L"정리하지 않습니다.",
         L"Cache of web pages, images, and scripts (Cache, Code Cache, GPUCache). Sign-ins, favorites, "
         L"and search history are kept. Edge can keep running in the background after its windows are "
         L"closed (startup boost); it isn't cleaned up while running.",
         L"Web ページ・画像・スクリプトのキャッシュ（Cache・Code Cache・GPUCache）です。ログイン・"
         L"お気に入り・検索履歴はそのままです。Edge はウィンドウを閉じてもバックグラウンドで実行される"
         L"ことがあり（スタートアップ ブースト）、実行中の場合は整理しません。",
         L"网页、图片和脚本缓存（Cache、Code Cache、GPUCache）。登录信息、收藏夹和搜索记录保持不变。"
         L"Edge 关闭窗口后仍可能在后台运行（启动增强），正在运行时不清理。"},
    [UI_T_K_NOTE_FIREFOX] =
        {L"웹 페이지 캐시(cache2)입니다. 로그인·북마크·방문 기록은 그대로입니다. Firefox가 실행 중이면 "
         L"정리하지 않습니다.",
         L"Web page cache (cache2). Sign-ins, bookmarks, and history are kept. Not cleaned up while "
         L"Firefox is running.",
         L"Web ページのキャッシュ（cache2）です。ログイン・ブックマーク・閲覧履歴はそのままです。"
         L"Firefox が実行中の場合は整理しません。",
         L"网页缓存（cache2）。登录信息、书签和浏览记录保持不变。Firefox 正在运行时不清理。"},
    [UI_T_K_NOTE_DEV] = {L"여러 프로젝트가 함께 쓰는 패키지 관리자·빌드 도구 캐시입니다. 정리하면 다음 빌드·설치 때 "
                         L"다시 내려받습니다.",
                         L"Package manager and build tool caches shared by multiple projects. After cleanup, "
                         L"they're downloaded again at the next build or install.",
                         L"複数のプロジェクトで共有するパッケージマネージャー・ビルドツールのキャッシュです。"
                         L"整理すると次回のビルド・インストール時に再ダウンロードします。",
                         L"多个项目共用的包管理器和构建工具缓存。清理后，下次构建或安装时会重新下载。"},
    [UI_T_K_NOTE_VDISK] =
        {L"WSL 배포판·Docker Desktop의 가상 디스크(.vhdx)입니다. 안에서 파일을 지워도 이 파일은 저절로 "
         L"줄지 않습니다. 지우면 배포판·컨테이너가 모두 사라지므로 ByteSieve는 정리하지 않고 줄이는 방법만 "
         L"알려 줍니다(WSL 을 끈 뒤 압축, 관리자 권한 필요).",
         L"Virtual disk (.vhdx) of a WSL distribution or Docker Desktop. Deleting files inside doesn't "
         L"shrink this file by itself. Deleting it would remove the whole distribution and its "
         L"containers, so ByteSieve doesn't clean it up and only shows how to shrink it (compact after "
         L"shutting down WSL; requires administrator rights).",
         L"WSL ディストリビューション・Docker Desktop の仮想ディスク（.vhdx）です。中のファイルを削除"
         L"してもこのファイルは自動的には小さくなりません。削除するとディストリビューション・コンテナーが"
         L"すべて失われるため、ByteSieve は整理せず、縮小する方法だけを案内します（WSL を停止してから"
         L"圧縮、管理者権限が必要）。",
         L"WSL 发行版或 Docker Desktop 的虚拟磁盘（.vhdx）。在其中删除文件不会让此文件自动变小。删除它会"
         L"丢失整个发行版和容器，因此 ByteSieve 不清理，只提供缩小的方法（关闭 WSL 后压缩，需要管理员权限）。"},
    [UI_T_K_GROUP_WINDOWS] = {L"Windows 임시 파일", L"Windows temporary files", L"Windows 一時ファイル",
                              L"Windows 临时文件"},
    [UI_T_K_GROUP_RECYCLE] = {L"휴지통", L"Recycle Bin", L"ごみ箱", L"回收站"},
    [UI_T_K_GROUP_BROWSER] = {L"브라우저 캐시", L"Browser caches", L"ブラウザーのキャッシュ", L"浏览器缓存"},
    [UI_T_K_GROUP_DEV] = {L"개발 도구 캐시", L"Developer tool caches", L"開発ツールのキャッシュ",
                          L"开发工具缓存"},
    [UI_T_K_GROUP_VDISK] = {L"가상 디스크(WSL·Docker)", L"Virtual disks (WSL, Docker)",
                            L"仮想ディスク（WSL・Docker）", L"虚拟磁盘（WSL、Docker）"},
    [UI_T_K_ST_READY] = {L"정리 가능", L"Can be cleaned up", L"整理可能", L"可清理"},
    [UI_T_K_ST_EMPTY] = {L"정리할 것 없음", L"Nothing to clean up", L"整理するものなし", L"无需清理"},
    [UI_T_K_ST_NOT_FOUND] = {L"없음", L"Not found", L"なし", L"不存在"},
    [UI_T_K_ST_APP_RUNNING] = {L"프로그램 실행 중", L"Program is running", L"プログラム実行中",
                               L"程序正在运行"},
    [UI_T_K_ST_DENIED] = {L"보호·확인 불가로 정리 안 함", L"Not cleaned up (protected or can't be checked)",
                          L"保護・確認不可のため整理しない", L"因受保护或无法确认而不清理"},
    [UI_T_K_ST_ERROR] = {L"조회 실패", L"Lookup failed", L"取得に失敗", L"查询失败"},
    [UI_T_K_ST_CANCELLED] = {L"분석 취소됨", L"Analysis cancelled", L"分析をキャンセル", L"分析已取消"},
    [UI_T_K_ST_INFO_ONLY] = {L"분석만(정리하지 않음)", L"Analysis only (not cleaned up)",
                             L"分析のみ（整理しない）", L"仅分析（不清理）"},
    [UI_T_K_PROFILES] = {L"프로필 %d개", L"%d profiles", L"プロファイル %d 個", L"%d 个配置文件"},
    [UI_T_K_ALL_DRIVES] = {L"모든 드라이브", L"All drives", L"すべてのドライブ", L"所有驱动器"},
    [UI_T_SKIP_NONE] = {L"건너뛰지 않음", L"Not skipped", L"スキップしない", L"未跳过"},
    [UI_T_SKIP_RECENT] = {L"분석 뒤 최근에 바뀌어 건너뜀", L"Skipped (changed after analysis)",
                          L"分析後に変更されたためスキップ", L"分析后有更改，已跳过"},
    [UI_T_SKIP_APP_RUNNING] = {L"관련 프로그램이 실행 중이라 건너뜀", L"Skipped (related program is running)",
                               L"関連プログラムが実行中のためスキップ", L"相关程序正在运行，已跳过"},

    /* ---- core: 개발 캐시 이름·설명·상태·근거 ---- */
    [UI_T_D_LABEL_NPM] = {L"패키지 캐시", L"package cache", L"パッケージキャッシュ", L"包缓存"},
    [UI_T_D_LABEL_PIP] = {L"패키지 캐시", L"package cache", L"パッケージキャッシュ", L"包缓存"},
    [UI_T_D_LABEL_GRADLE] = {L"의존성·빌드 캐시", L"dependency and build cache", L"依存関係・ビルドキャッシュ",
                             L"依赖项和构建缓存"},
    [UI_T_D_LABEL_GRADLE_WRAPPER] = {L"래퍼 배포판", L"wrapper distributions", L"ラッパー配布物",
                                     L"包装器发行版"},
    [UI_T_D_LABEL_CARGO_REGISTRY] = {L"레지스트리 캐시", L"registry cache", L"レジストリキャッシュ",
                                     L"注册表缓存"},
    [UI_T_D_LABEL_CARGO_GIT] = {L"git 의존성", L"git dependencies", L"git 依存関係", L"git 依赖项"},
    [UI_T_D_LABEL_NUGET_PACKAGES] = {L"전역 패키지", L"global packages", L"グローバルパッケージ", L"全局包"},
    [UI_T_D_LABEL_NUGET_HTTP] = {L"HTTP 캐시", L"HTTP cache", L"HTTP キャッシュ", L"HTTP 缓存"},
    [UI_T_D_LABEL_PNPM] = {L"패키지 저장소", L"package store", L"パッケージストア", L"包存储"},
    [UI_T_D_LABEL_YARN] = {L"패키지 캐시(v1)", L"package cache (v1)", L"パッケージキャッシュ（v1）",
                           L"包缓存（v1）"},
    [UI_T_D_LABEL_YARN_BERRY] = {L"전역 캐시(Berry)", L"global cache (Berry)", L"グローバルキャッシュ（Berry）",
                                 L"全局缓存（Berry）"},
    [UI_T_D_LABEL_MAVEN] = {L"로컬 저장소", L"local repository", L"ローカルリポジトリ", L"本地仓库"},
    [UI_T_D_LABEL_GO_MOD] = {L"모듈 캐시", L"module cache", L"モジュールキャッシュ", L"模块缓存"},
    [UI_T_D_LABEL_GO_BUILD] = {L"빌드 캐시", L"build cache", L"ビルドキャッシュ", L"构建缓存"},
    [UI_T_D_LABEL_CONDA] = {L"패키지 캐시", L"package cache", L"パッケージキャッシュ", L"包缓存"},
    [UI_T_D_NOTE_NPM] =
        {L"npm이 내려받은 패키지 캐시입니다. 다음 설치 때 다시 내려받습니다('npm cache clean "
         L"--force'와 같은 효과). npm·npx가 실행 중이면 끝난 뒤 정리하세요.",
         L"Package cache downloaded by npm. Packages are downloaded again at the next install (same "
         L"effect as 'npm cache clean --force'). If npm or npx is running, clean up after it finishes.",
         L"npm がダウンロードしたパッケージキャッシュです。次回のインストール時に再ダウンロードします"
         L"（'npm cache clean --force' と同じ効果）。npm・npx が実行中の場合は終了後に整理してください。",
         L"npm 下载的包缓存。下次安装时会重新下载（与 'npm cache clean --force' 效果相同）。如果 npm 或 "
         L"npx 正在运行，请在其结束后再清理。"},
    [UI_T_D_NOTE_PIP] =
        {L"pip이 내려받은 패키지·빌드한 휠 캐시입니다. 다음 설치 때 다시 내려받거나 빌드합니다"
         L"('pip cache purge'와 같은 효과). 휠을 직접 빌드하는 패키지는 설치가 느려질 수 있습니다.",
         L"Cache of packages downloaded and wheels built by pip. They're downloaded or built again at "
         L"the next install (same effect as 'pip cache purge'). Packages that build their own wheels "
         L"may install more slowly.",
         L"pip がダウンロードしたパッケージ・ビルドした wheel のキャッシュです。次回のインストール時に"
         L"再ダウンロードまたは再ビルドします（'pip cache purge' と同じ効果）。wheel を自分でビルドする"
         L"パッケージはインストールが遅くなる場合があります。",
         L"pip 下载的包和构建的 wheel 缓存。下次安装时会重新下载或构建（与 'pip cache purge' 效果相同）。"
         L"需要自行构建 wheel 的包安装可能会变慢。"},
    [UI_T_D_NOTE_GRADLE] =
        {L"Gradle이 내려받은 의존성과 빌드 캐시입니다. 다음 빌드 때 다시 내려받아 첫 빌드가 느려집니다. "
         L"Gradle 데몬이 실행 중이면 사용 중 오류가 날 수 있으니 'gradle --stop' 후 정리하세요. "
         L"설정(gradle.properties)은 건드리지 않습니다.",
         L"Dependencies and build cache downloaded by Gradle. They're downloaded again at the next build, "
         L"so the first build is slower. If the Gradle daemon is running, you may get in-use errors, so "
         L"run 'gradle --stop' before cleaning up. Settings (gradle.properties) aren't touched.",
         L"Gradle がダウンロードした依存関係とビルドキャッシュです。次回のビルド時に再ダウンロードする"
         L"ため、最初のビルドが遅くなります。Gradle デーモンが実行中の場合は使用中エラーになることが"
         L"あるため、'gradle --stop' の後に整理してください。設定（gradle.properties）には触れません。",
         L"Gradle 下载的依赖项和构建缓存。下次构建时会重新下载，因此首次构建会变慢。如果 Gradle 守护进程"
         L"正在运行，可能会出现正在使用的错误，请先运行 'gradle --stop' 再清理。不会改动设置"
         L"（gradle.properties）。"},
    [UI_T_D_NOTE_GRADLE_WRAPPER] =
        {L"Gradle 래퍼가 내려받은 Gradle 배포판입니다. 다음 빌드 때 배포판(수백 MB)을 다시 "
         L"내려받으므로 인터넷이 없으면 빌드할 수 없습니다.",
         L"Gradle distributions downloaded by the Gradle wrapper. The distribution (hundreds of MB) is "
         L"downloaded again at the next build, so you can't build without an internet connection.",
         L"Gradle ラッパーがダウンロードした Gradle 配布物です。次回のビルド時に配布物（数百 MB）を再"
         L"ダウンロードするため、インターネットがないとビルドできません。",
         L"Gradle 包装器下载的 Gradle 发行版。下次构建时会重新下载发行版（数百 MB），因此没有网络时无法"
         L"构建。"},
    [UI_T_D_NOTE_CARGO_REGISTRY] =
        {L"crates.io 색인·크레이트 압축 파일·풀어 둔 소스입니다. 다음 빌드 때 다시 내려받습니다. "
         L"cargo가 실행 중이면 끝난 뒤 정리하세요. 설치한 도구(.cargo\\bin)와 설정은 건드리지 "
         L"않습니다.",
         L"crates.io index, crate archives, and extracted sources. They're downloaded again at the next "
         L"build. If cargo is running, clean up after it finishes. Installed tools (.cargo\\bin) and "
         L"settings aren't touched.",
         L"crates.io のインデックス・クレートのアーカイブ・展開したソースです。次回のビルド時に再"
         L"ダウンロードします。cargo が実行中の場合は終了後に整理してください。インストールしたツール"
         L"（.cargo\\bin）と設定には触れません。",
         L"crates.io 索引、crate 压缩包和解压后的源代码。下次构建时会重新下载。如果 cargo 正在运行，请在"
         L"其结束后再清理。不会改动已安装的工具（.cargo\\bin）和设置。"},
    [UI_T_D_NOTE_CARGO_GIT] =
        {L"git 저장소에서 받은 의존성입니다. 다음 빌드 때 다시 받습니다. 설치한 도구(.cargo\\bin)와 "
         L"설정은 건드리지 않습니다.",
         L"Dependencies fetched from git repositories. They're fetched again at the next build. "
         L"Installed tools (.cargo\\bin) and settings aren't touched.",
         L"git リポジトリから取得した依存関係です。次回のビルド時に再取得します。インストールした"
         L"ツール（.cargo\\bin）と設定には触れません。",
         L"从 git 仓库获取的依赖项。下次构建时会重新获取。不会改动已安装的工具（.cargo\\bin）和设置。"},
    [UI_T_D_NOTE_NUGET_PACKAGES] =
        {L"NuGet 전역 패키지 폴더입니다. 다음 복원(restore) 때 모든 패키지를 다시 내려받으므로 "
         L"인터넷이 없으면 빌드할 수 없습니다. Visual Studio·dotnet이 실행 중이면 사용 중 오류가 날 "
         L"수 있습니다('dotnet nuget locals global-packages --clear'와 같은 효과).",
         L"NuGet global packages folder. All packages are downloaded again at the next restore, so you "
         L"can't build without an internet connection. If Visual Studio or dotnet is running, you may "
         L"get in-use errors (same effect as 'dotnet nuget locals global-packages --clear').",
         L"NuGet のグローバルパッケージフォルダーです。次回の復元（restore）時にすべてのパッケージを再"
         L"ダウンロードするため、インターネットがないとビルドできません。Visual Studio・dotnet が実行中"
         L"の場合は使用中エラーになることがあります（'dotnet nuget locals global-packages --clear' と"
         L"同じ効果）。",
         L"NuGet 全局包文件夹。下次还原（restore）时会重新下载所有包，因此没有网络时无法构建。如果 "
         L"Visual Studio 或 dotnet 正在运行，可能会出现正在使用的错误（与 'dotnet nuget locals "
         L"global-packages --clear' 效果相同）。"},
    [UI_T_D_NOTE_NUGET_HTTP] =
        {L"NuGet 피드 응답 캐시입니다. 다음 복원 때 다시 내려받습니다('dotnet nuget locals "
         L"http-cache --clear'와 같은 효과).",
         L"Cache of NuGet feed responses. Downloaded again at the next restore (same effect as "
         L"'dotnet nuget locals http-cache --clear').",
         L"NuGet フィードの応答キャッシュです。次回の復元時に再ダウンロードします（'dotnet nuget locals "
         L"http-cache --clear' と同じ効果）。",
         L"NuGet 源的响应缓存。下次还原时会重新下载（与 'dotnet nuget locals http-cache --clear' 效果"
         L"相同）。"},
    [UI_T_D_NOTE_PNPM] =
        {L"pnpm이 프로젝트의 node_modules에 하드 링크로 나눠 주는 패키지 저장소입니다. 프로젝트 쪽 파일은 남고 "
         L"다음 설치 때 다시 내려받습니다(쓰지 않는 패키지만 지우려면 'pnpm store prune'). 프로젝트와 링크를 "
         L"나누는 파일은 지워도 비지 않으므로 실제 확보는 그만큼 작습니다.",
         L"Package store that pnpm shares with projects' node_modules through hard links. The project "
         L"files remain, and packages are downloaded again at the next install (to remove only unused "
         L"packages, use 'pnpm store prune'). Files that share links with projects free no space when "
         L"deleted, so the reclaimable space is smaller by that amount.",
         L"pnpm がプロジェクトの node_modules にハードリンクで共有するパッケージストアです。プロジェクト"
         L"側のファイルは残り、次回のインストール時に再ダウンロードします（使っていないパッケージだけを"
         L"削除するには 'pnpm store prune'）。プロジェクトとリンクを共有するファイルは削除しても空き"
         L"容量が増えないため、実際の解放量はその分小さくなります。",
         L"pnpm 通过硬链接与项目的 node_modules 共享的包存储。项目中的文件会保留，下次安装时会重新下载"
         L"（只删除未使用的包请用 'pnpm store prune'）。与项目共享链接的文件删除后不会释放空间，因此实际"
         L"释放量会相应减少。"},
    [UI_T_D_NOTE_YARN] =
        {L"Yarn 1(classic)이 내려받은 패키지 캐시입니다. 다음 설치 때 다시 내려받습니다('yarn cache clean'과 "
         L"같은 효과).",
         L"Package cache downloaded by Yarn 1 (classic). Downloaded again at the next install (same "
         L"effect as 'yarn cache clean').",
         L"Yarn 1（classic）がダウンロードしたパッケージキャッシュです。次回のインストール時に再"
         L"ダウンロードします（'yarn cache clean' と同じ効果）。",
         L"Yarn 1（classic）下载的包缓存。下次安装时会重新下载（与 'yarn cache clean' 效果相同）。"},
    [UI_T_D_NOTE_YARN_BERRY] =
        {L"Yarn 2 이상(Berry)의 전역 캐시입니다. 다음 설치 때 다시 내려받습니다. 프로젝트 안의 .yarn\\cache"
         L"(zero-install)는 건드리지 않습니다.",
         L"Global cache of Yarn 2 or later (Berry). Downloaded again at the next install. The "
         L".yarn\\cache folder inside projects (zero-install) isn't touched.",
         L"Yarn 2 以降（Berry）のグローバルキャッシュです。次回のインストール時に再ダウンロードします。"
         L"プロジェクト内の .yarn\\cache（zero-install）には触れません。",
         L"Yarn 2 及以上版本（Berry）的全局缓存。下次安装时会重新下载。不会改动项目中的 .yarn\\cache"
         L"（zero-install）。"},
    [UI_T_D_NOTE_MAVEN] =
        {L"Maven 로컬 저장소입니다. 다음 빌드 때 의존성을 모두 다시 내려받으므로 인터넷이 없으면 빌드할 수 "
         L"없고, 'mvn install'로 직접 넣은 내 아티팩트도 사라집니다. settings.xml은 건드리지 않습니다.",
         L"Maven local repository. All dependencies are downloaded again at the next build, so you can't "
         L"build without an internet connection, and your own artifacts added with 'mvn install' are "
         L"also removed. settings.xml isn't touched.",
         L"Maven のローカルリポジトリです。次回のビルド時に依存関係をすべて再ダウンロードするため、"
         L"インターネットがないとビルドできず、'mvn install' で入れた自分のアーティファクトも失われます。"
         L"settings.xml には触れません。",
         L"Maven 本地仓库。下次构建时会重新下载所有依赖项，因此没有网络时无法构建，用 'mvn install' 安装的"
         L"自有构件也会丢失。不会改动 settings.xml。"},
    [UI_T_D_NOTE_GO_MOD] =
        {L"Go가 내려받은 모듈 캐시입니다. 다음 빌드 때 다시 내려받습니다('go clean -modcache'와 같은 효과). "
         L"파일이 읽기 전용이라 일부를 옮기지 못하면 'go clean -modcache'로 지우세요.",
         L"Module cache downloaded by Go. Downloaded again at the next build (same effect as 'go clean "
         L"-modcache'). If some files can't be moved because they're read-only, remove them with 'go "
         L"clean -modcache'.",
         L"Go がダウンロードしたモジュールキャッシュです。次回のビルド時に再ダウンロードします（'go clean "
         L"-modcache' と同じ効果）。ファイルが読み取り専用のため一部を移動できない場合は 'go clean "
         L"-modcache' で削除してください。",
         L"Go 下载的模块缓存。下次构建时会重新下载（与 'go clean -modcache' 效果相同）。如果文件为只读而"
         L"无法移动部分文件，请用 'go clean -modcache' 删除。"},
    [UI_T_D_NOTE_GO_BUILD] = {L"Go 빌드 결과 캐시입니다. 다음 빌드·테스트가 느려집니다('go clean -cache'와 같은 효과).",
                              L"Go build output cache. The next builds and tests will be slower (same effect "
                              L"as 'go clean -cache').",
                              L"Go のビルド結果キャッシュです。次回のビルド・テストが遅くなります（'go clean "
                              L"-cache' と同じ効果）。",
                              L"Go 构建结果缓存。下次构建和测试会变慢（与 'go clean -cache' 效果相同）。"},
    [UI_T_D_NOTE_CONDA] =
        {L"conda가 내려받은 패키지(pkgs)입니다. 이미 만든 환경은 하드 링크로 파일을 갖고 있어 그대로지만, 새 "
         L"환경을 만들거나 패키지를 바꿀 때 다시 내려받습니다. 쓰지 않는 것만 지우려면 'conda clean --all'을 "
         L"권합니다.",
         L"Packages (pkgs) downloaded by conda. Existing environments keep their files through hard "
         L"links and stay as they are, but packages are downloaded again when you create environments "
         L"or change packages. To remove only unused ones, 'conda clean --all' is recommended.",
         L"conda がダウンロードしたパッケージ（pkgs）です。作成済みの環境はハードリンクでファイルを"
         L"持っているためそのままですが、新しい環境の作成やパッケージの変更時に再ダウンロードします。"
         L"使っていないものだけを削除するには 'conda clean --all' をお勧めします。",
         L"conda 下载的包（pkgs）。已创建的环境通过硬链接保留文件，因此不受影响，但创建新环境或更改包时"
         L"会重新下载。只删除未使用的包建议使用 'conda clean --all'。"},
    [UI_T_D_ST_FOUND] = {L"있음", L"Found", L"あり", L"存在"},
    [UI_T_D_ST_NOT_FOUND] = {L"없음", L"Not found", L"なし", L"不存在"},
    [UI_T_D_ST_NOT_DIR] = {L"폴더가 아님", L"Not a folder", L"フォルダーではありません", L"不是文件夹"},
    [UI_T_D_ST_LINK] = {L"링크라 정리하지 않음", L"Not cleaned up (it's a link)", L"リンクのため整理しない",
                        L"是链接，不清理"},
    [UI_T_D_ST_UNRECOGNIZED] = {L"캐시 구조가 아니라 정리하지 않음",
                                L"Not cleaned up (doesn't look like a cache)",
                                L"キャッシュの構造ではないため整理しない", L"不是缓存结构，不清理"},
    [UI_T_D_ST_NO_PATH] = {L"위치를 정할 수 없음", L"Location can't be determined", L"場所を特定できません",
                           L"无法确定位置"},
    [UI_T_D_SRC_DEFAULT] = {L"기본 위치", L"Default location", L"既定の場所", L"默认位置"},
    [UI_T_D_SRC_ENV] = {L"환경 변수 %ls", L"Environment variable %ls", L"環境変数 %ls", L"环境变量 %ls"},
    [UI_T_D_SRC_CONFIG] = {L"설정 파일 %ls", L"Config file %ls", L"設定ファイル %ls", L"配置文件 %ls"},

    /* ---- Cleaner 패널: 단추·열 머리글·안내 ---- */
    [UI_T_CL_REFRESH] = {L"다시 분석", L"Analyze again", L"再分析", L"重新分析"},
    [UI_T_CL_RUN] = {L"선택 항목 정리...", L"Clean up selected...", L"選択した項目を整理...", L"清理所选项..."},
    [UI_T_CL_COL_TARGET] = {L"대상", L"Target", L"対象", L"对象"},
    [UI_T_CL_COL_GROUP] = {L"분류", L"Category", L"分類", L"分类"},
    [UI_T_CL_COL_GROWTH] = {L"증감", L"Change", L"増減", L"增减"},
    [UI_T_CL_COL_EXCLUDED] = {L"제외", L"Excluded", L"除外", L"已排除"},
    [UI_T_CL_COL_LOCATION] = {L"위치", L"Location", L"場所", L"位置"},
    [UI_T_CL_COL_SOURCE] = {L"위치 근거", L"Location source", L"場所の根拠", L"位置来源"},
    [UI_T_CL_INFO_SYSTEM] =
        {L"Windows 임시 파일·오류 보고서·휴지통·브라우저 캐시를 분석합니다. [실제 확보]는 디스크 "
         L"할당 크기 기준이며 하드 링크로 다른 곳과 나누는 파일은 뺍니다. 휴지통 비우기를 뺀 항목은 "
         L"휴지통(없는 드라이브는 격리 폴더)으로 옮기므로 그곳을 비워야 공간이 확보됩니다. "
         L"휴지통 비우기(영구 삭제)는 [모두 선택]에 들어가지 않습니다.",
         L"Analyzes Windows temporary files, error reports, the Recycle Bin, and browser caches. "
         L"[Reclaimable] is based on allocated disk size and excludes files that share hard links with "
         L"other places. Items other than emptying the Recycle Bin are moved to the Recycle Bin (the "
         L"quarantine folder on drives without one), so space is freed when you empty it. Emptying the "
         L"Recycle Bin (permanent deletion) isn't included in [Select all].",
         L"Windows の一時ファイル・エラー報告・ごみ箱・ブラウザーのキャッシュを分析します。[実際の"
         L"解放量] はディスクの割り当てサイズ基準で、ハードリンクでほかの場所と共有するファイルは除き"
         L"ます。ごみ箱を空にする以外の項目はごみ箱（ごみ箱のないドライブは隔離フォルダー）に移動する"
         L"ため、そこを空にすると容量が解放されます。ごみ箱を空にする（完全な削除）は [すべて選択] に"
         L"含まれません。",
         L"分析 Windows 临时文件、错误报告、回收站和浏览器缓存。[实际释放] 以磁盘分配大小为准，不包括"
         L"通过硬链接与其他位置共享的文件。除清空回收站外的项目会移到回收站（没有回收站的驱动器移到"
         L"隔离文件夹），清空那里后才会释放空间。清空回收站（永久删除）不包含在 [全选] 中。"},
    [UI_T_CL_INFO_CACHE] =
        {L"npm·pip·Gradle·Cargo·NuGet 등 여러 프로젝트가 함께 쓰는 개발 캐시와 WSL·Docker 가상 "
         L"디스크(분석만)를 봅니다. 개발 캐시는 지우면 다시 내려받아야 하므로 [모두 선택]에 들어가지 "
         L"않고 행을 직접 체크해야 합니다. 정리한 캐시는 휴지통(없는 드라이브는 격리 폴더)으로 옮깁니다.",
         L"Shows developer caches shared by multiple projects, such as npm, pip, Gradle, Cargo, and NuGet, "
         L"and WSL and Docker virtual disks (analysis only). Developer caches must be downloaded again "
         L"after deletion, so they aren't included in [Select all]; check their rows yourself. Cleaned-up "
         L"caches are moved to the Recycle Bin (the quarantine folder on drives without one).",
         L"npm・pip・Gradle・Cargo・NuGet など複数のプロジェクトで共有する開発キャッシュと、WSL・Docker の"
         L"仮想ディスク（分析のみ）を確認します。開発キャッシュは削除すると再ダウンロードが必要なため "
         L"[すべて選択] には含まれず、行を自分でチェックする必要があります。整理したキャッシュはごみ箱"
         L"（ごみ箱のないドライブは隔離フォルダー）に移動します。",
         L"查看 npm、pip、Gradle、Cargo、NuGet 等多个项目共用的开发缓存，以及 WSL 和 Docker 虚拟磁盘"
         L"（仅分析）。开发缓存删除后需要重新下载，因此不包含在 [全选] 中，需要手动勾选。清理的缓存会移到"
         L"回收站（没有回收站的驱动器移到隔离文件夹）。"},
    [UI_T_CL_INFO_ALL] =
        {L"Windows 임시 파일·오류 보고서·휴지통·브라우저 캐시와 npm·pip·Gradle·"
         L"Cargo·NuGet 공용 캐시를 분석합니다. [실제 확보]는 디스크 할당 크기 기준이며 "
         L"하드 링크로 다른 곳과 나누는 파일은 뺍니다. 휴지통 비우기를 뺀 항목은 "
         L"휴지통(없는 드라이브는 격리 폴더)으로 옮기므로 그곳을 비워야 공간이 확보됩니다. "
         L"휴지통 비우기(영구 삭제)와 개발 도구 캐시는 [모두 선택]에 들어가지 않습니다.",
         L"Analyzes Windows temporary files, error reports, the Recycle Bin, browser caches, and shared "
         L"npm, pip, Gradle, Cargo, and NuGet caches. [Reclaimable] is based on allocated disk size and "
         L"excludes files that share hard links with other places. Items other than emptying the "
         L"Recycle Bin are moved to the Recycle Bin (the quarantine folder on drives without one), so "
         L"space is freed when you empty it. Emptying the Recycle Bin (permanent deletion) and developer "
         L"tool caches aren't included in [Select all].",
         L"Windows の一時ファイル・エラー報告・ごみ箱・ブラウザーのキャッシュと npm・pip・Gradle・"
         L"Cargo・NuGet の共有キャッシュを分析します。[実際の解放量] はディスクの割り当てサイズ基準で、"
         L"ハードリンクでほかの場所と共有するファイルは除きます。ごみ箱を空にする以外の項目はごみ箱"
         L"（ごみ箱のないドライブは隔離フォルダー）に移動するため、そこを空にすると容量が解放されます。"
         L"ごみ箱を空にする（完全な削除）と開発ツールのキャッシュは [すべて選択] に含まれません。",
         L"分析 Windows 临时文件、错误报告、回收站、浏览器缓存以及 npm、pip、Gradle、Cargo、NuGet 共用"
         L"缓存。[实际释放] 以磁盘分配大小为准，不包括通过硬链接与其他位置共享的文件。除清空回收站外的"
         L"项目会移到回收站（没有回收站的驱动器移到隔离文件夹），清空那里后才会释放空间。清空回收站"
         L"（永久删除）和开发工具缓存不包含在 [全选] 中。"},

    /* ---- Cleaner: 목록 칸 ---- */
    [UI_T_CL_STATE_EMPTIED] = {L"비움(영구 삭제) · %ls", L"Emptied (permanently deleted) · %ls",
                               L"空にしました（完全に削除） · %ls", L"已清空（永久删除） · %ls"},
    [UI_T_CL_STATE_EMPTY_FAILED] = {L"비우지 못함: %ls (0x%08X)", L"Couldn't empty: %ls (0x%08X)",
                                    L"空にできません: %ls (0x%08X)", L"无法清空：%ls (0x%08X)"},
    [UI_T_CL_ERROR_WORD] = {L"오류", L"Error", L"エラー", L"错误"},
    [UI_T_CL_STATE_DONE] = {L"정리함 %u개 · %ls%ls%ls", L"Cleaned up %u · %ls%ls%ls",
                            L"整理済み %u 個 · %ls%ls%ls", L"已清理 %u 个 · %ls%ls%ls"},
    [UI_T_CL_PART_QUARANTINE] = {L" (일부 격리 폴더)", L" (some to the quarantine folder)",
                                 L"（一部は隔離フォルダー）", L"（部分移到隔离文件夹）"},
    [UI_T_CL_PART_SKIPPED] = {L" 건너뜀 %u", L" skipped %u", L" スキップ %u", L" 跳过 %u"},
    [UI_T_CL_PART_FAILED] = {L" 실패 %u", L" failed %u", L" 失敗 %u", L" 失败 %u"},
    [UI_T_CL_PART_CANCELLED] = {L" 취소 %u", L" cancelled %u", L" キャンセル %u", L" 取消 %u"},
    [UI_T_CL_STATE_PERMANENT] = {L"정리 가능 · 영구 삭제", L"Can be cleaned up · permanent deletion",
                                 L"整理可能 · 完全な削除", L"可清理 · 永久删除"},
    [UI_T_CL_STATE_CAUTION] = {L"정리 가능 · 주의", L"Can be cleaned up · caution", L"整理可能 · 注意",
                               L"可清理 · 注意"},
    [UI_T_CL_EXCL_RECENT] = {L"최근 24시간 안에 바뀐 %u개(%ls)", L"%u changed in the last 24 hours (%ls)",
                             L"最近 24 時間以内に変更 %u 個（%ls）", L"最近 24 小时内更改 %u 个（%ls）"},
    [UI_T_CL_EXCL_DENIED] = {L"%ls보호·사용 중·확인 불가 %u개", L"%lsprotected, in use, or unverifiable %u",
                             L"%ls保護・使用中・確認不可 %u 個", L"%ls受保护、正在使用或无法确认 %u 个"},
    [UI_T_CL_GROWTH_FIRST] = {L"첫 기록", L"First record", L"初回の記録", L"首次记录"},
    [UI_T_CL_GROWTH_SAME] = {L"변화 없음 (%llu일 전과 같음)", L"No change (same as %llu days ago)",
                             L"変化なし（%llu 日前と同じ）", L"无变化（与 %llu 天前相同）"},
    [UI_T_CL_GROWTH_DELTA] = {L"%lc%ls (%llu일 전보다)", L"%lc%ls (vs. %llu days ago)",
                              L"%lc%ls（%llu 日前より）", L"%lc%ls（与 %llu 天前相比）"},

    /* ---- Cleaner: 선택 줄·상세 설명 ---- */
    [UI_T_CL_SELECTION] = {L"선택 %u개 · 실제 확보 %ls (파일 크기 %ls)%ls",
                           L"Selected %u · reclaimable %ls (file size %ls)%ls",
                           L"選択 %u 個 · 実際の解放量 %ls（ファイルサイズ %ls）%ls",
                           L"已选 %u 个 · 实际释放 %ls（文件大小 %ls）%ls"},
    [UI_T_CL_SEL_RECYCLE] = {L" + 휴지통 비우기(영구 삭제)", L" + empty Recycle Bin (permanent deletion)",
                             L" + ごみ箱を空にする（完全な削除）", L" + 清空回收站（永久删除）"},
    [UI_T_CL_SEL_CAUTION] = {L" · 주의 %u개", L" · caution %u", L" · 注意 %u 個", L" · 注意 %u 个"},
    [UI_T_CL_DETAIL_HINT] = {L"항목을 고르면 무엇을 정리하는지와 주의 사항이 여기 표시됩니다.",
                             L"Select an item to see what will be cleaned up and any cautions here.",
                             L"項目を選ぶと、何を整理するかと注意事項がここに表示されます。",
                             L"选择项目后，将在此处显示要清理的内容和注意事项。"},
    [UI_T_CL_DETAIL_SOURCE] = {L" · 위치 근거: ", L" · location source: ", L" · 場所の根拠: ", L" · 位置来源："},
    [UI_T_CL_DETAIL_SHARED] = {L"\r\n하드 링크로 다른 곳과 나누는 %ls는 지워도 비지 않아 실제 확보에서 뺐습니다.",
                               L"\r\n%ls shared with other places through hard links frees no space when "
                               L"deleted, so it's excluded from the reclaimable space.",
                               L"\r\nハードリンクでほかの場所と共有する %ls は削除しても空かないため、実際の"
                               L"解放量から除きました。",
                               L"\r\n通过硬链接与其他位置共享的 %ls 删除后不会释放空间，已从实际释放中排除。"},
    [UI_T_CL_DETAIL_VDISK] = {L"%ls\r\n위치: %ls · 실제 점유 %ls%ls\r\n줄이는 방법: %ls",
                              L"%ls\r\nLocation: %ls · actually occupies %ls%ls\r\nHow to shrink: %ls",
                              L"%ls\r\n場所: %ls · 実際の占有 %ls%ls\r\n縮小する方法: %ls",
                              L"%ls\r\n位置：%ls · 实际占用 %ls%ls\r\n缩小方法：%ls"},
    [UI_T_CL_UNKNOWN] = {L"(모름)", L"(unknown)", L"（不明）", L"（未知）"},
    [UI_T_CL_SPARSE] = {L"(스파스)", L" (sparse)", L"（スパース）", L"（稀疏）"},
    [UI_T_CL_DETAIL] = {L"%ls%ls\r\n위치: %ls%ls · %ls%ls%ls%ls", L"%ls%ls\r\nLocation: %ls%ls · %ls%ls%ls%ls",
                        L"%ls%ls\r\n場所: %ls%ls · %ls%ls%ls%ls", L"%ls%ls\r\n位置：%ls%ls · %ls%ls%ls%ls"},
    [UI_T_CL_CAUTION_TAG] = {L"[주의] ", L"[Caution] ", L"[注意] ", L"[注意] "},
    [UI_T_CL_EXCL_LABEL] = {L" · 제외: ", L" · excluded: ", L" · 除外: ", L" · 已排除："},

    /* ---- Cleaner: 상태 줄 ---- */
    [UI_T_CL_START_FAILED] = {L"분석을 시작하지 못했습니다(메모리 또는 스레드 생성 실패).",
                              L"Couldn't start the analysis (out of memory or couldn't create a thread).",
                              L"分析を開始できませんでした（メモリ不足またはスレッドを作成できません）。",
                              L"无法开始分析（内存不足或无法创建线程）。"},
    [UI_T_CL_ANALYZING] = {L"분석하는 중...", L"Analyzing...", L"分析しています...", L"正在分析..."},
    [UI_T_CL_SCANNING] = {L"분석하는 중... %ls (%u/%u) · 지금까지 %ls", L"Analyzing... %ls (%u/%u) · %ls so far",
                          L"分析しています... %ls（%u/%u） · これまでに %ls", L"正在分析... %ls（%u/%u） · 目前 %ls"},
    [UI_T_CL_NO_MEMORY] = {L"분석 결과를 표시하지 못했습니다(메모리 부족).",
                           L"Couldn't show the analysis results (out of memory).",
                           L"分析結果を表示できませんでした（メモリ不足）。", L"无法显示分析结果（内存不足）。"},
    [UI_T_CL_SCANNED] = {L"%ls: 정리할 수 있는 대상 %u개 · 실제 확보 %ls(휴지통 제외) · %.1f초",
                         L"%ls: %u targets can be cleaned up · reclaimable %ls (excluding the Recycle Bin) · %.1f s",
                         L"%ls: 整理できる対象 %u 個 · 実際の解放量 %ls（ごみ箱を除く） · %.1f 秒",
                         L"%ls：可清理的对象 %u 个 · 实际释放 %ls（不含回收站） · %.1f 秒"},
    [UI_T_CL_SCAN_DONE_HEAD] = {L"분석 완료", L"Analysis complete", L"分析完了", L"分析完成"},
    [UI_T_CL_SCAN_CANCELLED_HEAD] = {L"분석 취소됨(다 세지 못한 대상은 정리할 수 없음)",
                                     L"Analysis cancelled (targets that weren't fully counted can't be cleaned up)",
                                     L"分析をキャンセル（数え終わらなかった対象は整理できません）",
                                     L"分析已取消（未统计完的对象无法清理）"},
    [UI_T_CL_CANCELLING_RUN] = {L"취소하는 중... 옮기던 항목과 휴지통 비우기는 끝까지 합니다.",
                                L"Cancelling... The item being moved and emptying the Recycle Bin will be finished.",
                                L"キャンセルしています... 移動中の項目とごみ箱を空にする処理は最後まで行います。",
                                L"正在取消... 正在移动的项目和清空回收站会完成。"},
    [UI_T_CL_NOTHING_CHECKED] = {L"정리할 대상을 목록에서 체크하세요.",
                                 L"Check the targets to clean up in the list.",
                                 L"整理する対象を一覧でチェックしてください。", L"请在列表中勾选要清理的对象。"},
    [UI_T_CL_RUN_CANCELLED] = {L"정리를 취소했습니다. 아무것도 바뀌지 않았습니다.",
                               L"Cleanup was cancelled. Nothing was changed.",
                               L"整理をキャンセルしました。何も変更されていません。", L"已取消清理。没有任何更改。"},
    [UI_T_CL_NOTHING_TO_RUN] = {L"정리할 대상이 없습니다. 아무것도 바뀌지 않았습니다.",
                                L"There's nothing to clean up. Nothing was changed.",
                                L"整理する対象がありません。何も変更されていません。", L"没有要清理的对象。没有任何更改。"},
    [UI_T_CL_RUN_START_FAILED] = {L"정리를 시작하지 못했습니다(메모리 또는 스레드 생성 실패).",
                                  L"Couldn't start cleanup (out of memory or couldn't create a thread).",
                                  L"整理を開始できませんでした（メモリ不足またはスレッドを作成できません）。",
                                  L"无法开始清理（内存不足或无法创建线程）。"},
    [UI_T_CL_RUNNING_START] = {L"정리하는 중...", L"Cleaning up...", L"整理しています...", L"正在清理..."},
    [UI_T_CL_EMPTYING] = {L"휴지통을 비우는 중...", L"Emptying the Recycle Bin...", L"ごみ箱を空にしています...",
                          L"正在清空回收站..."},
    [UI_T_CL_RUNNING] = {L"정리하는 중... %u/%u · 옮김 %u개(%ls) · 건너뜀·실패 %u개",
                         L"Cleaning up... %u/%u · moved %u (%ls) · skipped or failed %u",
                         L"整理しています... %u/%u · 移動 %u 個（%ls） · スキップ・失敗 %u 個",
                         L"正在清理... %u/%u · 已移动 %u 个（%ls） · 跳过或失败 %u 个"},
    [UI_T_CL_RAN] = {L"정리 완료: %u개 옮김(%ls, 휴지통·격리 폴더를 비우면 확보)%ls · 건너뜀 %u개 · 실패 %u개%ls · %.1f초",
                     L"Cleanup finished: moved %u (%ls, freed when the Recycle Bin or quarantine folder is "
                     L"emptied)%ls · skipped %u · failed %u%ls · %.1f s",
                     L"整理完了: %u 個を移動（%ls、ごみ箱・隔離フォルダーを空にすると解放）%ls · スキップ %u 個 · "
                     L"失敗 %u 個%ls · %.1f 秒",
                     L"清理完成：已移动 %u 个（%ls，清空回收站或隔离文件夹后释放）%ls · 跳过 %u 个 · 失败 %u 个%ls · "
                     L"%.1f 秒"},
    [UI_T_CL_RAN_RECYCLE_DONE] = {L" · 휴지통 비움(%ls)", L" · Recycle Bin emptied (%ls)",
                                  L" · ごみ箱を空にしました（%ls）", L" · 已清空回收站（%ls）"},
    [UI_T_CL_RAN_RECYCLE_FAILED] = {L" · 휴지통 비우지 못함", L" · couldn't empty the Recycle Bin",
                                    L" · ごみ箱を空にできませんでした", L" · 无法清空回收站"},
    [UI_T_CL_RAN_CANCELLED] = {L" · 취소됨", L" · cancelled", L" · キャンセル済み", L" · 已取消"},

    /* ---- Cleaner: 확인·결과 창 ---- */
    [UI_T_CL_CONFIRM_HEAD] = {L"선택한 %u개 대상을 정리합니다. 실제 확보 용량: %ls%ls\n\n",
                              L"%u selected targets will be cleaned up. Reclaimable space: %ls%ls\n\n",
                              L"選択した %u 個の対象を整理します。実際の解放量: %ls%ls\n\n",
                              L"将清理所选的 %u 个对象。实际释放空间：%ls%ls\n\n"},
    [UI_T_CL_CONFIRM_RECYCLE] = {L" + 휴지통 비우기", L" + empty Recycle Bin", L" + ごみ箱を空にする",
                                 L" + 清空回收站"},
    [UI_T_CL_TAG_PERMANENT] = {L" [영구 삭제]", L" [permanent deletion]", L" [完全な削除]", L" [永久删除]"},
    [UI_T_CL_TAG_CAUTION] = {L" [주의]", L" [caution]", L" [注意]", L" [注意]"},
    [UI_T_CL_CONFIRM_DEV] = {L"\n개발 도구 캐시 %u개가 들어 있습니다. 여러 프로젝트가 함께 쓰는 캐시라 정리하면 다음 "
                             L"빌드·설치 때 다시 내려받습니다.",
                             L"\nThis includes %u developer tool caches. They're shared by multiple projects, so "
                             L"after cleanup they're downloaded again at the next build or install.",
                             L"\n開発ツールのキャッシュ %u 個が含まれています。複数のプロジェクトで共有するキャッシュ"
                             L"のため、整理すると次回のビルド・インストール時に再ダウンロードします。",
                             L"\n其中包含 %u 个开发工具缓存。这些缓存由多个项目共用，清理后下次构建或安装时会重新下载。"},
    [UI_T_CL_CONFIRM_DEV_CAUTION] = {L" 이 중 [주의] %u개는 정리하면 인터넷이 없을 때 빌드할 수 없습니다.",
                                     L" Of these, %u marked [caution] can't be rebuilt without an internet connection "
                                     L"after cleanup.",
                                     L" このうち [注意] の %u 個は、整理するとインターネットがないときにビルドできません。",
                                     L" 其中 %u 个 [注意] 项清理后，没有网络时将无法构建。"},
    [UI_T_CL_CONFIRM_INFO] =
        {L"\n휴지통 비우기를 뺀 항목은 Windows 휴지통(휴지통이 없는 드라이브는 격리 폴더)으로 "
         L"옮기며 영구 삭제하지 않습니다. 공간은 그곳을 비울 때 확보됩니다. 사용 중인 파일과 "
         L"분석 뒤 바뀐 임시 파일은 건너뛰고 항목별로 알려 드립니다. 브라우저 캐시는 그 브라우저가 실행 중이면 정리하지 않습니다.\n"
         L"\n계속할까요?",
         L"\nItems other than emptying the Recycle Bin are moved to the Windows Recycle Bin (the quarantine "
         L"folder on drives without one) and aren't permanently deleted. Space is freed when you empty "
         L"it. Files in use and temporary files changed after the analysis are skipped and reported "
         L"item by item. Browser caches aren't cleaned up while that browser is running.\n"
         L"\nDo you want to continue?",
         L"\nごみ箱を空にする以外の項目は Windows のごみ箱（ごみ箱のないドライブは隔離フォルダー）に移動し、"
         L"完全には削除しません。容量はそこを空にしたときに解放されます。使用中のファイルと分析後に変更"
         L"された一時ファイルはスキップし、項目ごとにお知らせします。ブラウザーのキャッシュはそのブラウザー"
         L"が実行中の場合は整理しません。\n"
         L"\n続行しますか？",
         L"\n除清空回收站外的项目会移到 Windows 回收站（没有回收站的驱动器移到隔离文件夹），不会永久删除。"
         L"清空那里后才会释放空间。正在使用的文件和分析后更改的临时文件会被跳过，并逐项告知。浏览器缓存在"
         L"该浏览器运行时不会清理。\n"
         L"\n要继续吗？"},
    [UI_T_CL_RECYCLE_CONFIRM] =
        {L"휴지통의 항목 %llu개(%ls)를 영구 삭제합니다.\n\n"
         L"영구 삭제한 파일은 복원할 수 없습니다. ByteSieve가 이전에 휴지통으로 옮긴 항목도 "
         L"함께 지워집니다.\n\n"
         L"[예] 휴지통을 비우고 정리\n[아니요] 휴지통은 그대로 두고 나머지만 정리\n"
         L"[취소] 아무것도 하지 않음",
         L"%llu items (%ls) in the Recycle Bin will be permanently deleted.\n\n"
         L"Permanently deleted files can't be restored. Items ByteSieve moved to the Recycle Bin earlier "
         L"are deleted too.\n\n"
         L"[Yes] Empty the Recycle Bin and clean up\n[No] Keep the Recycle Bin and clean up the rest only\n"
         L"[Cancel] Do nothing",
         L"ごみ箱の項目 %llu 個（%ls）を完全に削除します。\n\n"
         L"完全に削除したファイルは復元できません。ByteSieve が以前ごみ箱に移動した項目も一緒に削除"
         L"されます。\n\n"
         L"[はい] ごみ箱を空にして整理\n[いいえ] ごみ箱はそのままにして残りだけ整理\n"
         L"[キャンセル] 何もしない",
         L"将永久删除回收站中的 %llu 个项目（%ls）。\n\n"
         L"永久删除的文件无法恢复。ByteSieve 之前移到回收站的项目也会一并删除。\n\n"
         L"[是] 清空回收站并清理\n[否] 保留回收站，只清理其余项目\n"
         L"[取消] 不执行任何操作"},
    [UI_T_CL_RECYCLE_TITLE] = {L"휴지통 비우기(영구 삭제)", L"Empty Recycle Bin (permanent deletion)",
                               L"ごみ箱を空にする（完全な削除）", L"清空回收站（永久删除）"},
    [UI_T_CL_FAILURES_HEAD] = {L"%u개 항목을 정리하지 못했습니다. 이 항목들은 그대로 남아 있습니다.\n"
                               L"사용 중인 프로그램을 닫은 뒤 다시 분석해 정리할 수 있습니다.\n\n",
                               L"%u items couldn't be cleaned up. They remain where they were.\n"
                               L"Close the programs using them, then analyze again to clean them up.\n\n",
                               L"%u 個の項目を整理できませんでした。これらの項目はそのまま残っています。\n"
                               L"使用中のプログラムを閉じてから再分析すると整理できます。\n\n",
                               L"%u 个项目无法清理。这些项目保持原样。\n"
                               L"关闭正在使用它们的程序后重新分析即可清理。\n\n"},
    [UI_T_CL_FAILURES_RECYCLE] = {L"휴지통\n    비우지 못함: %ls (0x%08X)\n",
                                  L"Recycle Bin\n    couldn't empty: %ls (0x%08X)\n",
                                  L"ごみ箱\n    空にできません: %ls (0x%08X)\n", L"回收站\n    无法清空：%ls (0x%08X)\n"},
    [UI_T_CL_MOVE_FAILED] = {L"옮기지 못함", L"Couldn't move", L"移動できません", L"无法移动"},
    [UI_T_CL_RESULT_TITLE] = {L"Cleaner 결과", L"Cleaner results", L"Cleaner の結果", L"Cleaner 结果"},

    /* 부스터 */
    [UI_T_B_REFRESH] = {L"새로고침", L"Refresh", L"更新", L"刷新"},
    [UI_T_B_START_LABEL] = {L"시작 프로그램", L"Startup apps", L"スタートアップ", L"启动项"},
    [UI_T_B_DISABLE] = {L"사용 안 함", L"Disable", L"無効にする", L"禁用"},
    [UI_T_B_ENABLE] = {L"사용", L"Enable", L"有効にする", L"启用"},
    [UI_T_B_PROC_LABEL] = {L"프로세스", L"Processes", L"プロセス", L"进程"},
    [UI_T_B_BACKGROUND_ONLY] = {L"백그라운드만", L"Background only", L"バックグラウンドのみ", L"仅后台"},
    [UI_T_B_END] = {L"선택한 프로세스 끝내기...", L"End selected processes...", L"選択したプロセスを終了...",
                    L"结束所选进程..."},
    [UI_T_B_COL_NAME] = {L"이름", L"Name", L"名前", L"名称"},
    [UI_T_B_COL_STATE] = {L"상태", L"Status", L"状態", L"状态"},
    [UI_T_B_COL_SOURCE] = {L"위치", L"Location", L"場所", L"位置"},
    [UI_T_B_COL_COMMAND] = {L"명령", L"Command", L"コマンド", L"命令"},
    [UI_T_B_COL_RESULT] = {L"결과", L"Result", L"結果", L"结果"},
    [UI_T_B_COL_PID] = {L"PID", L"PID", L"PID", L"PID"},
    [UI_T_B_COL_KIND] = {L"종류", L"Type", L"種類", L"类型"},
    [UI_T_B_COL_CPU] = {L"CPU", L"CPU", L"CPU", L"CPU"},
    [UI_T_B_COL_MEMORY] = {L"메모리", L"Memory", L"メモリ", L"内存"},
    [UI_T_B_COL_PATH] = {L"경로", L"Path", L"パス", L"路径"},
    [UI_T_B_MEASURING] = {L"측정 중", L"measuring", L"測定中", L"测量中"},
    [UI_T_B_SUMMARY] = {L"CPU %ls · 메모리 %ls / %ls (%u%%) · 프로세스 %u개(백그라운드 %u개)",
                        L"CPU %ls · Memory %ls / %ls (%u%%) · %u processes (%u background)",
                        L"CPU %ls · メモリ %ls / %ls (%u%%) · プロセス %u 個(バックグラウンド %u 個)",
                        L"CPU %ls · 内存 %ls / %ls (%u%%) · 进程 %u 个(后台 %u 个)"},
    [UI_T_B_SUMMARY_MEM_FAIL] = {L"CPU %ls · 메모리 측정 실패", L"CPU %ls · Couldn't measure memory",
                                 L"CPU %ls · メモリを測定できません", L"CPU %ls · 无法测量内存"},
    [UI_T_B_START_INFO] = {L"%u개 중 사용 %u개 · 선택 %u개%ls", L"%u items, %u enabled · %u selected%ls",
                           L"%u 個中 有効 %u 個 · 選択 %u 個%ls", L"共 %u 个，已启用 %u 个 · 已选 %u 个%ls"},
    [UI_T_B_START_READ_ERRORS] = {L" · 일부 위치를 읽지 못함", L" · Some locations couldn't be read",
                                  L" · 一部の場所を読み取れません", L" · 部分位置无法读取"},
    [UI_T_B_APPLIED_ON] = {L"시작 프로그램 %u개를 사용으로 바꿈%ls", L"Enabled %u startup apps%ls",
                           L"スタートアップ %u 個を有効にしました%ls", L"已启用 %u 个启动项%ls"},
    [UI_T_B_APPLIED_OFF] = {L"시작 프로그램 %u개를 사용 안 함으로 바꿈%ls", L"Disabled %u startup apps%ls",
                            L"スタートアップ %u 個を無効にしました%ls", L"已禁用 %u 个启动项%ls"},
    [UI_T_B_PARTIAL_FAIL] = {L" · 일부 실패(행의 결과 참고)", L" · Some failed (see the Result column)",
                             L" · 一部失敗(各行の結果を参照)", L" · 部分失败(见各行结果)"},
    [UI_T_B_RES_CHANGED] = {L"바꿈", L"Changed", L"変更しました", L"已更改"},
    [UI_T_B_RES_DENIED] = {L"바꾸지 못함: 관리자 권한 필요", L"Couldn't change: administrator rights required",
                           L"変更できません: 管理者権限が必要", L"无法更改：需要管理员权限"},
    [UI_T_B_RES_FAILED] = {L"바꾸지 못함(오류 %lu)", L"Couldn't change (error %lu)", L"変更できません(エラー %lu)",
                           L"无法更改(错误 %lu)"},
    [UI_T_B_RES_NEEDS_ADMIN] = {L"바꾸려면 관리자 권한 필요", L"Administrator rights required to change",
                                L"変更には管理者権限が必要", L"更改需要管理员权限"},
    [UI_T_B_STATE_ON] = {L"사용", L"Enabled", L"有効", L"已启用"},
    [UI_T_B_STATE_OFF] = {L"사용 안 함", L"Disabled", L"無効", L"已禁用"},
    [UI_T_B_KIND_APP] = {L"앱", L"App", L"アプリ", L"应用"},
    [UI_T_B_KIND_BACKGROUND] = {L"백그라운드", L"Background", L"バックグラウンド", L"后台"},
    [UI_T_B_PROC_INFO] = {L"선택 %u개 · 확보 예상 메모리 %ls", L"%u selected · Memory to free: %ls",
                          L"選択 %u 個 · 解放見込みメモリ %ls", L"已选 %u 个 · 预计释放内存 %ls"},
    [UI_T_B_PROC_READING] = {L"프로세스 목록과 CPU 사용률을 읽는 중...", L"Reading the process list and CPU usage...",
                             L"プロセス一覧と CPU 使用率を読み取っています...", L"正在读取进程列表和 CPU 使用率..."},
    [UI_T_B_END_RESULT] = {L"끝냄 %u개(메모리 약 %ls) · 끝내지 못함 %u개(행의 상태 참고)",
                           L"Ended %u (about %ls of memory) · Couldn't end %u (see the Status column)",
                           L"終了 %u 個(メモリ約 %ls) · 終了できず %u 個(各行の状態を参照)",
                           L"已结束 %u 个(内存约 %ls) · 无法结束 %u 个(见各行状态)"},
    [UI_T_B_STATE_ERROR] = {L"%ls (오류 %lu)", L"%ls (error %lu)", L"%ls (エラー %lu)", L"%ls (错误 %lu)"},
    [UI_T_B_PROTECTED] = {L"보호: %ls", L"Protected: %ls", L"保護: %ls", L"受保护：%ls"},
    [UI_T_B_CONFIRM_HEAD] = {L"선택한 프로세스 %u개를 끝냅니다(확보 예상 메모리 %ls).\n\n",
                             L"End %u selected processes (memory to free: %ls).\n\n",
                             L"選択した %u 個のプロセスを終了します(解放見込みメモリ %ls)。\n\n",
                             L"将结束所选的 %u 个进程(预计释放内存 %ls)。\n\n"},
    [UI_T_B_CONFIRM_MORE] = {L"... 외 %u개\n", L"... and %u more\n", L"... ほか %u 個\n", L"... 另有 %u 个\n"},
    [UI_T_B_CONFIRM_WARN] =
        {L"\n프로세스를 바로 끝내므로 저장하지 않은 작업은 사라질 수 있습니다. 끝낸 프로그램은 "
         L"다시 실행할 수 있으며, 시작 프로그램이면 다음 로그인 때 다시 실행됩니다.\n"
         L"시스템·Windows 구성 요소·ByteSieve 는 고를 수 없고, 목록을 읽은 뒤 다른 프로세스로 "
         L"바뀐 경우(같은 PID 재사용)에는 끝내지 않습니다.\n\n계속할까요?",
         L"\nProcesses are ended immediately, so unsaved work may be lost. You can run ended programs "
         L"again, and startup apps run again at the next sign-in.\n"
         L"System processes, Windows components and ByteSieve can't be selected, and a process that "
         L"changed after the list was read (PID reused) isn't ended.\n\nContinue?",
         L"\nプロセスをすぐに終了するため、保存していない作業は失われることがあります。終了したプログラムは"
         L"再び実行でき、スタートアップ プログラムは次回サインイン時に再び実行されます。\n"
         L"システム・Windows コンポーネント・ByteSieve は選択できず、一覧を読み取った後に別のプロセスに"
         L"変わった場合(同じ PID の再利用)は終了しません。\n\n続行しますか?",
         L"\n进程会被立即结束，未保存的工作可能会丢失。已结束的程序可以再次运行，"
         L"启动项会在下次登录时再次运行。\n"
         L"无法选择系统进程、Windows 组件和 ByteSieve；读取列表后已变为其他进程(PID 被重用)的进程不会被结束。"
         L"\n\n要继续吗?"},
    [UI_T_B_CONFIRM_TITLE] = {L"Booster - 프로세스 끝내기", L"Booster - End processes", L"Booster - プロセスの終了",
                              L"Booster - 结束进程"},
    [UI_T_B_SRC_RUN_USER] = {L"레지스트리(현재 사용자)", L"Registry (current user)", L"レジストリ(現在のユーザー)",
                             L"注册表(当前用户)"},
    [UI_T_B_SRC_RUN_MACHINE] = {L"레지스트리(모든 사용자)", L"Registry (all users)",
                                L"レジストリ(すべてのユーザー)", L"注册表(所有用户)"},
    [UI_T_B_SRC_RUN_MACHINE32] = {L"레지스트리(모든 사용자, 32비트)", L"Registry (all users, 32-bit)",
                                  L"レジストリ(すべてのユーザー、32 ビット)", L"注册表(所有用户，32 位)"},
    [UI_T_B_SRC_FOLDER_USER] = {L"시작프로그램 폴더(현재 사용자)", L"Startup folder (current user)",
                                L"スタートアップ フォルダー(現在のユーザー)", L"启动文件夹(当前用户)"},
    [UI_T_B_SRC_FOLDER_COMMON] = {L"시작프로그램 폴더(모든 사용자)", L"Startup folder (all users)",
                                  L"スタートアップ フォルダー(すべてのユーザー)", L"启动文件夹(所有用户)"},
    [UI_T_B_PROT_NONE] = {L"끝낼 수 있음", L"Can be ended", L"終了できます", L"可以结束"},
    [UI_T_B_PROT_SYSTEM] = {L"시스템 프로세스", L"System process", L"システム プロセス", L"系统进程"},
    [UI_T_B_PROT_SESSION] = {L"다른 사용자 세션", L"Another user's session", L"別のユーザーのセッション",
                             L"其他用户的会话"},
    [UI_T_B_PROT_CRITICAL] = {L"Windows 필수 프로세스", L"Essential Windows process", L"Windows の必須プロセス",
                              L"Windows 关键进程"},
    [UI_T_B_PROT_WINDOWS] = {L"Windows 구성 요소", L"Windows component", L"Windows コンポーネント",
                             L"Windows 组件"},
    [UI_T_B_PROT_SELF] = {L"ByteSieve", L"ByteSieve", L"ByteSieve", L"ByteSieve"},
    [UI_T_B_PROT_NO_ACCESS] = {L"정보를 읽을 수 없음(권한)", L"Can't read information (permissions)",
                               L"情報を読み取れません(権限)", L"无法读取信息(权限)"},
    [UI_T_B_END_OK] = {L"끝냄", L"Ended", L"終了しました", L"已结束"},
    [UI_T_B_END_PROTECTED] = {L"보호 대상이라 끝내지 않음", L"Protected, not ended", L"保護対象のため終了しません",
                              L"受保护，未结束"},
    [UI_T_B_END_GONE] = {L"이미 끝남", L"Already ended", L"既に終了しています", L"进程已不存在"},
    [UI_T_B_END_CHANGED] = {L"다른 프로세스로 바뀌어 끝내지 않음", L"Changed to another process, not ended",
                            L"別のプロセスに変わったため終了しません", L"已变为其他进程，未结束"},
    [UI_T_B_END_DENIED] = {L"권한 없음", L"Access denied", L"権限がありません", L"权限不足"},
    [UI_T_B_END_FAILED] = {L"끝내지 못함", L"Couldn't end", L"終了できません", L"无法结束"},

    /* 드라이브 */
    [UI_T_DR_REFRESH] = {L"새로고침(&R)", L"&Refresh", L"更新(&R)", L"刷新(&R)"},
    [UI_T_DR_HINT] = {L"[새로고침]을 누르면 드라이브 정보를 가져옵니다.", L"Click [Refresh] to get drive information.",
                      L"[更新] を押すとドライブ情報を取得します。", L"点击 [刷新] 获取驱动器信息。"},
    [UI_T_DR_LOADING] = {L"드라이브 정보를 가져오는 중...", L"Getting drive information...",
                         L"ドライブ情報を取得しています...", L"正在获取驱动器信息..."},
    [UI_T_DR_LIST_FAILED] = {L"드라이브 목록을 가져오지 못했습니다 (Windows 오류 %lu).",
                             L"Couldn't get the drive list (Windows error %lu).",
                             L"ドライブ一覧を取得できませんでした (Windows エラー %lu)。",
                             L"无法获取驱动器列表 (Windows 错误 %lu)。"},
    [UI_T_DR_SUMMARY] = {L"드라이브 %u개 · 전체 %ls · 사용 %ls · 여유 %ls · 조회 실패 %u개 · "
                         L"BitLocker 암호화 %u/%u개 · %02u:%02u:%02u 기준",
                         L"%u drives · Total %ls · Used %ls · Free %ls · %u failed · "
                         L"BitLocker encrypted %u/%u · As of %02u:%02u:%02u",
                         L"ドライブ %u 台 · 合計 %ls · 使用 %ls · 空き %ls · 取得失敗 %u 台 · "
                         L"BitLocker 暗号化 %u/%u 台 · %02u:%02u:%02u 時点",
                         L"驱动器 %u 个 · 总计 %ls · 已用 %ls · 可用 %ls · 查询失败 %u 个 · "
                         L"BitLocker 加密 %u/%u 个 · 截至 %02u:%02u:%02u"},
    [UI_T_DR_START_NOMEM] = {L"새로고침을 시작하지 못했습니다(메모리 부족).", L"Couldn't start refreshing (out of memory).",
                             L"更新を開始できませんでした(メモリ不足)。", L"无法开始刷新(内存不足)。"},
    [UI_T_DR_START_NOTHREAD] = {L"새로고침을 시작하지 못했습니다(스레드 생성 실패).",
                                L"Couldn't start refreshing (couldn't create a thread).",
                                L"更新を開始できませんでした(スレッドを作成できません)。",
                                L"无法开始刷新(无法创建线程)。"},
    [UI_T_DR_COL_ROOT] = {L"드라이브", L"Drive", L"ドライブ", L"驱动器"},
    [UI_T_DR_COL_LABEL] = {L"이름", L"Name", L"名前", L"名称"},
    [UI_T_DR_COL_TYPE] = {L"종류", L"Type", L"種類", L"类型"},
    [UI_T_DR_COL_FS] = {L"파일 시스템", L"File system", L"ファイル システム", L"文件系统"},
    [UI_T_DR_COL_ENCRYPTION] = {L"암호화", L"Encryption", L"暗号化", L"加密"},
    [UI_T_DR_COL_TOTAL] = {L"전체", L"Total", L"合計", L"总计"},
    [UI_T_DR_COL_USED] = {L"사용", L"Used", L"使用", L"已用"},
    [UI_T_DR_COL_FREE] = {L"여유", L"Free", L"空き", L"可用"},
    [UI_T_DR_COL_USAGE] = {L"사용률", L"Usage", L"使用率", L"使用率"},
    [UI_T_DR_TYPE_REMOVABLE] = {L"이동식", L"Removable", L"リムーバブル", L"可移动"},
    [UI_T_DR_TYPE_FIXED] = {L"로컬 디스크", L"Local disk", L"ローカル ディスク", L"本地磁盘"},
    [UI_T_DR_TYPE_REMOTE] = {L"네트워크", L"Network", L"ネットワーク", L"网络"},
    [UI_T_DR_TYPE_CDROM] = {L"CD/DVD", L"CD/DVD", L"CD/DVD", L"CD/DVD"},
    [UI_T_DR_TYPE_RAMDISK] = {L"RAM 디스크", L"RAM disk", L"RAM ディスク", L"RAM 磁盘"},
    [UI_T_DR_TYPE_NO_ROOT] = {L"루트 없음", L"No root directory", L"ルートなし", L"无根目录"},
    [UI_T_DR_TYPE_UNKNOWN] = {L"알 수 없음", L"Unknown", L"不明", L"未知"},
    [UI_T_DR_ST_OK] = {L"정상", L"OK", L"正常", L"正常"},
    [UI_T_DR_ST_NOT_READY] = {L"미디어 없음/준비 안 됨", L"No media/not ready", L"メディアなし/準備ができていません",
                              L"无介质/未就绪"},
    [UI_T_DR_ST_ACCESS_DENIED] = {L"접근 거부", L"Access denied", L"アクセス拒否", L"拒绝访问"},
    [UI_T_DR_ST_MISSING] = {L"분리됨", L"Removed", L"取り外し済み", L"已移除"},
    [UI_T_DR_ST_DISCONNECTED] = {L"연결 안 됨", L"Disconnected", L"未接続", L"未连接"},
    [UI_T_DR_ST_FAILED] = {L"조회 실패", L"Query failed", L"取得失敗", L"查询失败"},
    [UI_T_DR_ENC_RAW] = {L"%ls(값 %ld)", L"%ls (value %ld)", L"%ls(値 %ld)", L"%ls(值 %ld)"},
    [UI_T_DR_LOCKED] = {L"BitLocker로 잠김(탐색기에서 잠금 해제 필요)", L"Locked by BitLocker (unlock it in File Explorer)",
                        L"BitLocker でロック中(エクスプローラーでロック解除が必要)",
                        L"已被 BitLocker 锁定(需在文件资源管理器中解锁)"},
    [UI_T_DR_STATUS_ERROR] = {L"%ls (오류 %lu)", L"%ls (error %lu)", L"%ls (エラー %lu)", L"%ls (错误 %lu)"},
    [UI_T_DR_ENC_NOT_QUERIED] = {L"-", L"-", L"-", L"-"},
    [UI_T_DR_ENC_NONE] = {L"정보 없음", L"No information", L"情報なし", L"无信息"},
    [UI_T_DR_ENC_OFF] = {L"암호화 안 됨", L"Not encrypted", L"暗号化なし", L"未加密"},
    [UI_T_DR_ENC_ON] = {L"BitLocker 켜짐", L"BitLocker on", L"BitLocker オン", L"BitLocker 已开启"},
    [UI_T_DR_ENC_LOCKED] = {L"BitLocker 잠김", L"BitLocker locked", L"BitLocker ロック中", L"BitLocker 已锁定"},
    [UI_T_DR_ENC_ENCRYPTING] = {L"BitLocker 암호화 중", L"BitLocker encrypting", L"BitLocker 暗号化中",
                                L"BitLocker 加密中"},
    [UI_T_DR_ENC_DECRYPTING] = {L"BitLocker 해독 중", L"BitLocker decrypting", L"BitLocker 復号中",
                                L"BitLocker 解密中"},
    [UI_T_DR_ENC_SUSPENDED] = {L"BitLocker 일시 중단(보호 꺼짐)", L"BitLocker suspended (protection off)",
                               L"BitLocker 一時停止(保護オフ)", L"BitLocker 已暂停(保护关闭)"},
    [UI_T_DR_ENC_PENDING] = {L"BitLocker 활성화 대기", L"BitLocker waiting for activation", L"BitLocker 有効化待ち",
                             L"BitLocker 等待激活"},
    [UI_T_DR_ENC_UNKNOWN] = {L"알 수 없음", L"Unknown", L"不明", L"未知"},
    [UI_T_DR_ENC_FAILED] = {L"조회 실패", L"Query failed", L"取得失敗", L"查询失败"},

    /* 모니터 */
    [UI_T_M_HINT] = {L"탭을 열면 RAM·CPU 사용률 측정을 시작합니다.",
                     L"Opening this tab starts measuring RAM and CPU usage.",
                     L"タブを開くと RAM・CPU 使用率の測定を開始します。", L"打开此标签页即开始测量 RAM 和 CPU 使用率。"},
    [UI_T_M_STARTING] = {L"측정을 시작하는 중...", L"Starting measurement...", L"測定を開始しています...",
                         L"正在开始测量..."},
    [UI_T_M_SUMMARY] = {L"%u초마다 갱신 · %02u:%02u:%02u 측정%ls", L"Updated every %u s · Measured at %02u:%02u:%02u%ls",
                        L"%u 秒ごとに更新 · %02u:%02u:%02u 測定%ls", L"每 %u 秒更新 · %02u:%02u:%02u 测量%ls"},
    [UI_T_M_PARTIAL] = {L" · 일부 값을 측정하지 못했습니다", L" · Some values couldn't be measured",
                        L" · 一部の値を測定できませんでした", L" · 部分数值无法测量"},
    [UI_T_M_START_NOEVENT] = {L"측정을 시작하지 못했습니다(이벤트 생성 실패).",
                              L"Couldn't start measuring (couldn't create an event).",
                              L"測定を開始できませんでした(イベントを作成できません)。",
                              L"无法开始测量(无法创建事件)。"},
    [UI_T_M_START_NOTHREAD] = {L"측정을 시작하지 못했습니다(스레드 생성 실패).",
                               L"Couldn't start measuring (couldn't create a thread).",
                               L"測定を開始できませんでした(スレッドを作成できません)。",
                               L"无法开始测量(无法创建线程)。"},
    [UI_T_M_RAM_TITLE] = {L"메모리(RAM)", L"Memory (RAM)", L"メモリ(RAM)", L"内存(RAM)"},
    [UI_T_M_MEM_TEXT] = {L"사용 %ls / 전체 %ls · 사용 가능 %ls · 사용률 %u.%u%%",
                         L"Used %ls / Total %ls · Available %ls · Usage %u.%u%%",
                         L"使用 %ls / 合計 %ls · 利用可能 %ls · 使用率 %u.%u%%",
                         L"已用 %ls / 总计 %ls · 可用 %ls · 使用率 %u.%u%%"},
    [UI_T_M_FAILED] = {L"측정 실패 (Windows 오류 %lu)", L"Measurement failed (Windows error %lu)",
                       L"測定できません (Windows エラー %lu)", L"测量失败 (Windows 错误 %lu)"},
    [UI_T_M_MEM_INVALID] = {L"측정값이 올바르지 않습니다", L"The measured value is invalid", L"測定値が正しくありません",
                            L"测量值无效"},
    [UI_T_M_CPU_TEXT] = {L"사용률 %u.%u%% · 논리 프로세서 %u개", L"Usage %u.%u%% · %u logical processors",
                         L"使用率 %u.%u%% · 論理プロセッサ %u 個", L"使用率 %u.%u%% · 逻辑处理器 %u 个"},
    [UI_T_M_CPU_USAGE] = {L"사용률 %u.%u%%", L"Usage %u.%u%%", L"使用率 %u.%u%%", L"使用率 %u.%u%%"},
    [UI_T_M_MEASURING] = {L"측정 중...", L"Measuring...", L"測定中...", L"测量中..."},
    [UI_T_M_CPU_INVALID] = {L"측정값이 올바르지 않아 다시 측정합니다", L"The measured value is invalid; measuring again",
                            L"測定値が正しくないため再測定します", L"测量值无效，正在重新测量"},
    [UI_T_M_GRAPH_TITLE] = {L"최근 60초 사용률", L"Usage over the last 60 seconds", L"過去 60 秒の使用率",
                            L"最近 60 秒使用率"},
    [UI_T_M_LEGEND_MEMORY] = {L"메모리", L"Memory", L"メモリ", L"内存"},
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

unsigned short ui_lang_langid(UiLang lang)
{
    switch (lang) {
    case UI_LANG_KO:
        return MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN);
    case UI_LANG_JA:
        return MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN);
    case UI_LANG_ZH_HANS:
        return MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);
    default:
        return MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
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
