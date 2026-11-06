#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <propsys.h>
#include <shobjidl.h>

#include <string.h>

#include "drive.h"

/* 탐색기가 드라이브의 BitLocker 상태(자물쇠 아이콘)에 쓰는 셸 속성. */
#define BITLOCKER_PROPERTY L"System.Volume.BitLockerProtection"

int drive_root_from_letter(wchar_t letter, wchar_t *buf, size_t len)
{
    if (buf == NULL || len < DRIVE_ROOT_LEN) {
        return -1;
    }
    if (letter >= L'a' && letter <= L'z') {
        letter = (wchar_t)(letter - L'a' + L'A');
    }
    if (letter < L'A' || letter > L'Z') {
        return -1;
    }
    buf[0] = letter;
    buf[1] = L':';
    buf[2] = L'\\';
    buf[3] = L'\0';
    return 0;
}

DriveStatus drive_status_from_error(unsigned long error)
{
    switch (error) {
    case ERROR_SUCCESS:
        return DRIVE_STATUS_OK;
    case ERROR_NOT_READY:
    case ERROR_NO_MEDIA_IN_DRIVE:
    case ERROR_UNRECOGNIZED_MEDIA:
    case ERROR_UNRECOGNIZED_VOLUME:
    case ERROR_MEDIA_CHANGED:
    case ERROR_WRONG_DISK:
        return DRIVE_STATUS_NOT_READY;
    case ERROR_ACCESS_DENIED:
        return DRIVE_STATUS_ACCESS_DENIED;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
    case ERROR_DEVICE_NOT_CONNECTED:
    case ERROR_DEV_NOT_EXIST:
    case ERROR_DEVICE_REMOVED:
        return DRIVE_STATUS_MISSING;
    case ERROR_BAD_NETPATH:
    case ERROR_NETNAME_DELETED:
    case ERROR_BAD_NET_NAME:
    case ERROR_NETWORK_UNREACHABLE:
    case ERROR_NO_NET_OR_BAD_PATH:
    case ERROR_NOT_CONNECTED:
    case ERROR_CONNECTION_UNAVAIL:
    case ERROR_SEM_TIMEOUT:
        return DRIVE_STATUS_DISCONNECTED;
    default:
        return DRIVE_STATUS_ERROR;
    }
}

DriveType drive_type_from_win32(unsigned int type)
{
    switch (type) {
    case DRIVE_NO_ROOT_DIR:
        return DRIVE_TYPE_NO_ROOT;
    case DRIVE_REMOVABLE:
        return DRIVE_TYPE_REMOVABLE;
    case DRIVE_FIXED:
        return DRIVE_TYPE_FIXED;
    case DRIVE_REMOTE:
        return DRIVE_TYPE_REMOTE;
    case DRIVE_CDROM:
        return DRIVE_TYPE_CDROM;
    case DRIVE_RAMDISK:
        return DRIVE_TYPE_RAMDISK;
    default:
        return DRIVE_TYPE_UNKNOWN;
    }
}

static void set_failure(DriveInfo *out, DWORD error)
{
    out->status = drive_status_from_error(error);
    if (out->status == DRIVE_STATUS_OK) {
        /* 실패했는데 오류 코드가 0 으로 남는 경우도 실패로 본다. */
        out->status = DRIVE_STATUS_ERROR;
    }
    out->error = error;
    out->total_bytes = 0;
    out->free_bytes = 0;
    out->available_bytes = 0;
}

DriveEncryption drive_encryption_from_bitlocker(int has_value, long value)
{
    if (!has_value) {
        return DRIVE_ENC_NONE;
    }
    switch (value) {
    case 0:
        return DRIVE_ENC_NONE;
    case 1:
        return DRIVE_ENC_ON;
    case 2:
        return DRIVE_ENC_OFF;
    case 3:
        return DRIVE_ENC_ENCRYPTING;
    case 4:
        return DRIVE_ENC_DECRYPTING;
    case 5:
        return DRIVE_ENC_SUSPENDED;
    case 6:
        return DRIVE_ENC_LOCKED;
    case 8:
        return DRIVE_ENC_PENDING;
    default:
        return DRIVE_ENC_UNKNOWN;
    }
}

int drive_encryption_is_encrypted(DriveEncryption enc)
{
    return enc == DRIVE_ENC_ON || enc == DRIVE_ENC_LOCKED || enc == DRIVE_ENC_SUSPENDED ||
           enc == DRIVE_ENC_PENDING || enc == DRIVE_ENC_DECRYPTING;
}

/* 정수형 PROPVARIANT 의 값. 정수가 아니면 0. */
static int propvariant_long(const PROPVARIANT *pv, long *value)
{
    switch (pv->vt) {
    case VT_I4:
    case VT_INT:
        *value = pv->lVal;
        return 1;
    case VT_UI4:
    case VT_UINT:
        *value = (long)pv->ulVal;
        return 1;
    case VT_I2:
        *value = pv->iVal;
        return 1;
    case VT_UI2:
        *value = pv->uiVal;
        return 1;
    case VT_UI1:
        *value = pv->bVal;
        return 1;
    default:
        return 0;
    }
}

/*
 * 로컬·이동식 드라이브의 BitLocker 상태를 셸 속성으로 읽는다(관리자 권한 불필요). 드라이브가
 * 잠겨 용량을 읽지 못해도 조회한다. 작업 스레드에서도 쓰므로 COM 을 여기서 초기화·정리한다.
 */
static void query_encryption(DriveInfo *out)
{
    PROPERTYKEY key;
    IShellItem2 *item = NULL;
    PROPVARIANT pv;
    HRESULT init, hr;
    long value = -1;
    int has_value = 0;

    out->encryption = DRIVE_ENC_NOT_QUERIED;
    out->encryption_raw = -1;
    out->encryption_error = 0;
    if ((out->type != DRIVE_TYPE_FIXED && out->type != DRIVE_TYPE_REMOVABLE) ||
        out->status == DRIVE_STATUS_MISSING) {
        return;
    }
    init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    hr = PSGetPropertyKeyFromName(BITLOCKER_PROPERTY, &key);
    if (SUCCEEDED(hr)) {
        hr = SHCreateItemFromParsingName(out->root, NULL, &IID_IShellItem2, (void **)&item);
    }
    if (SUCCEEDED(hr)) {
        PropVariantInit(&pv);
        hr = IShellItem2_GetProperty(item, &key, &pv);
        if (SUCCEEDED(hr)) {
            has_value = propvariant_long(&pv, &value);
            if (!has_value && pv.vt != VT_EMPTY) {
                /* 정수가 아닌 값: 뜻을 모른다. */
                out->encryption = DRIVE_ENC_UNKNOWN;
            }
        }
        PropVariantClear(&pv);
        IShellItem2_Release(item);
    }
    if (FAILED(hr)) {
        out->encryption = DRIVE_ENC_FAILED;
        out->encryption_error = (long)hr;
    } else if (out->encryption != DRIVE_ENC_UNKNOWN) {
        out->encryption = drive_encryption_from_bitlocker(has_value, value);
        out->encryption_raw = has_value ? value : -1;
    }
    /* 용량도 못 읽고(미디어 없음 등) 암호화 정보도 없으면 판단하지 않은 것으로 둔다. */
    if (out->status != DRIVE_STATUS_OK &&
        (out->encryption == DRIVE_ENC_NONE || out->encryption == DRIVE_ENC_FAILED)) {
        out->encryption = DRIVE_ENC_NOT_QUERIED;
        out->encryption_error = 0;
    }
    if (init == S_OK || init == S_FALSE) {
        CoUninitialize();
    }
}

void drive_query_system(DriveInfo *out, void *ctx)
{
    ULARGE_INTEGER avail, total, free_bytes;
    DWORD old_mode;
    BOOL mode_set;

    (void)ctx;
    if (out == NULL) {
        return;
    }
    /* 미디어가 없는 이동식·광학 드라이브에서 "디스크를 넣으세요" 대화상자가 뜨지 않게 한다.
     * 스레드 단위 설정이라 다른 스레드(스캔 작업 등)에는 영향을 주지 않는다. */
    mode_set = SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &old_mode);

    out->type = drive_type_from_win32(GetDriveTypeW(out->root));
    out->label[0] = L'\0';
    out->fs[0] = L'\0';

    if (out->type == DRIVE_TYPE_NO_ROOT) {
        set_failure(out, ERROR_PATH_NOT_FOUND);
    } else if (!GetDiskFreeSpaceExW(out->root, &avail, &total, &free_bytes)) {
        set_failure(out, GetLastError());
    } else {
        out->status = DRIVE_STATUS_OK;
        out->error = 0;
        out->total_bytes = total.QuadPart;
        out->free_bytes = free_bytes.QuadPart;
        out->available_bytes = avail.QuadPart;
        /* 이름·파일 시스템은 부가 정보라 실패해도 용량 결과는 그대로 둔다. */
        if (!GetVolumeInformationW(out->root, out->label, DRIVE_LABEL_LEN, NULL, NULL, NULL,
                                   out->fs, DRIVE_FS_LEN)) {
            out->label[0] = L'\0';
            out->fs[0] = L'\0';
        }
    }
    query_encryption(out);

    if (mode_set) {
        SetThreadErrorMode(old_mode, NULL);
    }
}

int drive_list_build(unsigned long mask, DriveQueryFn query, void *ctx, DriveList *out)
{
    int i;

    if (out == NULL || query == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    for (i = 0; i < DRIVE_MAX; i++) {
        DriveInfo *d;

        if ((mask & (1UL << i)) == 0) {
            continue;
        }
        d = &out->items[out->len++];
        drive_root_from_letter((wchar_t)(L'A' + i), d->root, DRIVE_ROOT_LEN);
        query(d, ctx);
        /* 조회 함수가 루트를 덮어쓰더라도 목록 순서와 맞게 되돌린다. */
        drive_root_from_letter((wchar_t)(L'A' + i), d->root, DRIVE_ROOT_LEN);
        if (d->status != DRIVE_STATUS_OK) {
            d->total_bytes = 0;
            d->free_bytes = 0;
            d->available_bytes = 0;
        }
    }
    return 0;
}

int drive_list_query(DriveList *out, unsigned long *error)
{
    DWORD mask;

    if (error != NULL) {
        *error = 0;
    }
    if (out == NULL) {
        return -1;
    }
    SetLastError(ERROR_SUCCESS);
    mask = GetLogicalDrives();
    if (mask == 0) {
        DWORD code = GetLastError();
        memset(out, 0, sizeof(*out));
        if (code != ERROR_SUCCESS) {
            if (error != NULL) {
                *error = code;
            }
            return -1;
        }
        /* 드라이브가 정말 하나도 없는 경우. */
        return 0;
    }
    return drive_list_build(mask, drive_query_system, NULL, out);
}

uint64_t drive_used_bytes(const DriveInfo *d)
{
    if (d == NULL || d->status != DRIVE_STATUS_OK || d->free_bytes >= d->total_bytes) {
        return 0;
    }
    return d->total_bytes - d->free_bytes;
}

unsigned drive_usage_permille(const DriveInfo *d)
{
    uint64_t used = drive_used_bytes(d);
    uint64_t total;

    if (used == 0) {
        return 0;
    }
    total = d->total_bytes;
    /* used * 1000 이 넘치지 않도록 큰 값은 함께 줄인다(정밀도는 천분율이면 충분). */
    while (used > UINT64_MAX / 1000) {
        used >>= 10;
        total >>= 10;
    }
    if (total == 0) {
        return 0;
    }
    used = used * 1000 / total;
    return used > 1000 ? 1000 : (unsigned)used;
}

const wchar_t *drive_type_name(DriveType type)
{
    switch (type) {
    case DRIVE_TYPE_NO_ROOT:
        return L"no_root";
    case DRIVE_TYPE_REMOVABLE:
        return L"removable";
    case DRIVE_TYPE_FIXED:
        return L"fixed";
    case DRIVE_TYPE_REMOTE:
        return L"remote";
    case DRIVE_TYPE_CDROM:
        return L"cdrom";
    case DRIVE_TYPE_RAMDISK:
        return L"ramdisk";
    default:
        return L"unknown";
    }
}

const wchar_t *drive_status_name(DriveStatus status)
{
    switch (status) {
    case DRIVE_STATUS_OK:
        return L"ok";
    case DRIVE_STATUS_NOT_READY:
        return L"not_ready";
    case DRIVE_STATUS_ACCESS_DENIED:
        return L"access_denied";
    case DRIVE_STATUS_MISSING:
        return L"missing";
    case DRIVE_STATUS_DISCONNECTED:
        return L"disconnected";
    default:
        return L"error";
    }
}

const wchar_t *drive_encryption_name(DriveEncryption enc)
{
    switch (enc) {
    case DRIVE_ENC_NOT_QUERIED:
        return L"not_queried";
    case DRIVE_ENC_NONE:
        return L"none";
    case DRIVE_ENC_OFF:
        return L"off";
    case DRIVE_ENC_ON:
        return L"on";
    case DRIVE_ENC_LOCKED:
        return L"locked";
    case DRIVE_ENC_ENCRYPTING:
        return L"encrypting";
    case DRIVE_ENC_DECRYPTING:
        return L"decrypting";
    case DRIVE_ENC_SUSPENDED:
        return L"suspended";
    case DRIVE_ENC_PENDING:
        return L"pending";
    case DRIVE_ENC_FAILED:
        return L"failed";
    default:
        return L"unknown";
    }
}

const wchar_t *drive_encryption_text(DriveEncryption enc)
{
    switch (enc) {
    case DRIVE_ENC_NOT_QUERIED:
        return L"-";
    case DRIVE_ENC_NONE:
        return L"정보 없음";
    case DRIVE_ENC_OFF:
        return L"암호화 안 됨";
    case DRIVE_ENC_ON:
        return L"BitLocker 켜짐";
    case DRIVE_ENC_LOCKED:
        return L"BitLocker 잠김";
    case DRIVE_ENC_ENCRYPTING:
        return L"BitLocker 암호화 중";
    case DRIVE_ENC_DECRYPTING:
        return L"BitLocker 해독 중";
    case DRIVE_ENC_SUSPENDED:
        return L"BitLocker 일시 중단(보호 꺼짐)";
    case DRIVE_ENC_PENDING:
        return L"BitLocker 활성화 대기";
    case DRIVE_ENC_FAILED:
        return L"조회 실패";
    default:
        return L"알 수 없음";
    }
}
