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
