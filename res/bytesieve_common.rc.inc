// 두 exe 가 함께 쓰는 리소스(아이콘·매니페스트·버전 정보). gui.rc·cli.rc 가 아래 이름을 정의한 뒤 포함한다.
//   BYTESIEVE_RC_NAME      exe 이름(확장자 제외), BYTESIEVE_RC_FILE exe 파일 이름
//   BYTESIEVE_RC_DESC      파일 설명(탐색기 속성·작업 관리자에 보임)
//   BYTESIEVE_RC_MANIFEST  앱 매니페스트 파일
// 버전 숫자는 생성된 version.h(CMake project VERSION)에서 온다. 링커는 /MANIFEST:NO 로 자체 매니페스트를 만들지 않는다.
#include <winres.h>
#include "version.h"

// 앱 아이콘(리소스 번호 1). GUI 는 창 클래스 아이콘으로, 탐색기는 exe 아이콘으로 쓴다.
1 ICON "bytesieve.ico"

1 RT_MANIFEST BYTESIEVE_RC_MANIFEST

VS_VERSION_INFO VERSIONINFO
 FILEVERSION BYTESIEVE_VERSION_MAJOR, BYTESIEVE_VERSION_MINOR, BYTESIEVE_VERSION_PATCH, 0
 PRODUCTVERSION BYTESIEVE_VERSION_MAJOR, BYTESIEVE_VERSION_MINOR, BYTESIEVE_VERSION_PATCH, 0
 FILEFLAGSMASK VS_FFI_FILEFLAGSMASK
 FILEFLAGS 0x0L
 FILEOS VOS_NT_WINDOWS32
 FILETYPE VFT_APP
 FILESUBTYPE VFT2_UNKNOWN
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904b0"
        BEGIN
            VALUE "CompanyName", "gajigaji04"
            VALUE "FileDescription", BYTESIEVE_RC_DESC
            VALUE "FileVersion", BYTESIEVE_VERSION_STRING
            VALUE "InternalName", BYTESIEVE_RC_NAME
            VALUE "LegalCopyright", "Copyright (c) 2026 gajigaji04. PolyForm Noncommercial License 1.0.0"
            VALUE "OriginalFilename", BYTESIEVE_RC_FILE
            VALUE "ProductName", "ByteSieve"
            VALUE "ProductVersion", BYTESIEVE_VERSION_STRING
        END
    END
    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x409, 1200
    END
END
