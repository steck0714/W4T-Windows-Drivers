/* SPDX-License-Identifier: MIT */
/*
 * SwitchPowerTray - Native Win32 ARM64 Power Mode, Charging Limit & 5s Idle
 * Cursor Auto-Hide tray utility for Nintendo Switch (T210B01 / Windows 11).
 *
 * Original clean-room Win32 implementation:
 *   - Power Mode menu -> \\.\SwitchBacklight (SwitchFan v15 + TegraGpuRender)
 *   - Charging Limit menu -> \\.\SwitchBattery (SwitchBattery v12 BQ24193 +-5%
 *     hysteresis: 55% -> 50-60% band, 85% -> 80-90% band, Off -> 100%, keeping
 *     external VBUS input power active without BQ24193 EN_HIZ battery drain)
 *   - 5-second desktop mouse idle cursor auto-hide using CreateCursor +
 *     CopyIcon per OCR_* ID + SystemParametersInfoW(SPI_SETCURSORS), with
 *     HKCU crash-recovery breadcrumb, WM_ENDSESSION, and --restore-cursors.
 *   - Never reads or restores legacy Erista SwitchClock\LastProfile, and never
 *     enables automatic screen rotation or automatic brightness.
 */
#ifndef UNICODE
#define UNICODE 1
#endif
#ifndef _UNICODE
#define _UNICODE 1
#endif
#define WIN32_LEAN_AND_MEAN
#define OEMRESOURCE

#include <windows.h>
#include <shellapi.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
#include "../Shared/ScheduledStartup.h"

#include "../../drivers/SwitchFan/src/power_ioctl.h"
#include "../../drivers/SwitchFan/src/fan_profile_ioctl.h"
#include "../../drivers/SwitchBattery/src/charge_limit_ioctl.h"

#define APP_NAME            L"Switch Power & Battery"
#define WND_CLASS_NAME      L"SwitchWOAPowerTrayWnd"
#define SINGLE_MUTEX_NAME   L"Local\\SwitchWOA.SwitchPowerTray"
#ifndef REG_SETTINGS_KEY
#define REG_SETTINGS_KEY    L"Software\\SwitchWOA\\SwitchPowerTray"
#endif
#define REG_RUN_KEY         L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#ifndef REG_RUN_VALUE
#define REG_RUN_VALUE       L"SwitchPowerTray"
#endif
#define REG_RUN_TEST_VALUE  L"SwitchPowerTray_SelfTest"
#define REG_AUTOHIDE_VALUE  L"AutoHideCursor"
#define REG_HIDDEN_VALUE    L"CursorHidden"
#define REG_POWER_MODE_VALUE L"LastPowerMode"
#define REG_FAN_MODE_VALUE L"LastFanMode"

#define WM_TRAYICON         (WM_APP + 1)
#define WM_TRAY_SHUTDOWN    (WM_APP + 2)

#define TIMER_ID_CURSOR     1
#define TIMER_ID_STATUS     2

#define CURSOR_IDLE_MS          5000ULL
#define CURSOR_POLL_VISIBLE_MS  200U
#define CURSOR_POLL_HIDDEN_MS   50U
#define STATUS_POLL_MS          3000U

enum MenuCommandId {
    IDM_POWER_BASE      = 1000, /* 1000..1007 */
    IDM_FAN_BASE        = 1050, /* Console, Handheld, Cool */
    IDM_LIMIT_OFF       = 1100,
    IDM_LIMIT_85        = 1101,
    IDM_LIMIT_55        = 1102,
    IDM_AUTOHIDE_TOGGLE = 1200,
    IDM_RESTORE_CURSOR  = 1201,
    IDM_STARTUP_TOGGLE  = 1202,
    IDM_DETAILS         = 1203,
    IDM_RESET_DEFAULTS  = 1204,
    IDM_EXIT            = 1205
};

static const DWORD kSystemCursorIds[] = {
    32512, /* OCR_NORMAL */
    32513, /* OCR_IBEAM */
    32514, /* OCR_WAIT */
    32515, /* OCR_CROSS */
    32516, /* OCR_UP */
    32642, /* OCR_SIZENWSE */
    32643, /* OCR_SIZENESW */
    32644, /* OCR_SIZEWE */
    32645, /* OCR_SIZENS */
    32646, /* OCR_SIZEALL */
    32648, /* OCR_NO */
    32649, /* OCR_HAND */
    32650, /* OCR_APPSTARTING */
    32651  /* OCR_HELP */
};

typedef struct _TRAY_STATE {
    HWND Hwnd;
    NOTIFYICONDATAW Nid;
    HICON Icon;
    BOOL IconAdded;

    BOOL AutoHideEnabled;
    BOOL CursorHidden;
    HCURSOR SavedCursors[14];
    POINT LastMousePt;
    BOOL HaveMousePt;
    ULONGLONG LastMoveTick;

    BOOL HavePower;
    DWORD PowerError;
    SWFAN_POWER_STATUS Power;
    BOOL HaveFan;
    SWFAN_PROFILE_STATUS Fan;

    BOOL HaveBattery;
    DWORD BatteryError;
    SWBAT_CHARGE_LIMIT_STATUS Battery;
} TRAY_STATE;

static TRAY_STATE g_app;
static UINT g_taskbarCreated;
static BOOL g_startupProfilePending;
static BOOL g_startupFanPending;

/* --------------------------------------------------------- Registry helpers */

static DWORD ReadRegDword(LPCWSTR subKey, LPCWSTR name, DWORD defaultValue)
{
    HKEY key = NULL;
    DWORD value = defaultValue;
    DWORD type = 0;
    DWORD size = sizeof(value);

    if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey, 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExW(key, name, NULL, &type, (LPBYTE)&value, &size) != ERROR_SUCCESS ||
            type != REG_DWORD || size != sizeof(DWORD)) {
            value = defaultValue;
        }
        RegCloseKey(key);
    }
    return value;
}

static LSTATUS WriteRegDword(LPCWSTR subKey, LPCWSTR name, DWORD value)
{
    HKEY key = NULL;
    LSTATUS st = RegCreateKeyExW(HKEY_CURRENT_USER, subKey, 0, NULL, 0,
                                 KEY_SET_VALUE, NULL, &key, NULL);
    if (st == ERROR_SUCCESS) {
        st = RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
        RegCloseKey(key);
    }
    return st;
}

static BOOL IsAutostartEnabledEx(LPCWSTR valueName)
{
    BOOL enabled = FALSE;
    HRESULT hr = SwitchStartupTask(valueName, FALSE, &enabled);
    if (hr != S_FALSE) return SUCCEEDED(hr) && enabled;
    HKEY key = NULL;
    DWORD type = 0;
    DWORD size = 0;
    BOOL exists = FALSE;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_KEY, 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExW(key, valueName, NULL, &type, NULL, &size) == ERROR_SUCCESS &&
            type == REG_SZ && size > sizeof(WCHAR)) {
            exists = TRUE;
        }
        RegCloseKey(key);
    }
    return exists;
}

static LSTATUS SetAutostartEnabledEx(LPCWSTR valueName, BOOL enable)
{
    HRESULT hr = SwitchStartupTask(valueName, TRUE, &enable);
    if (hr != S_FALSE) return SUCCEEDED(hr) ? ERROR_SUCCESS : (LSTATUS)hr;
    HKEY key = NULL;
    WCHAR exePath[MAX_PATH];
    WCHAR cmdLine[MAX_PATH + 64];
    LSTATUS st;

    if (!GetModuleFileNameW(NULL, exePath, MAX_PATH)) {
        return (LSTATUS)GetLastError();
    }
    st = RegCreateKeyExW(HKEY_CURRENT_USER, REG_RUN_KEY, 0, NULL, 0,
                         KEY_SET_VALUE, NULL, &key, NULL);
    if (st != ERROR_SUCCESS) {
        return st;
    }
    if (enable) {
        _snwprintf(cmdLine, sizeof(cmdLine) / sizeof(cmdLine[0]), L"\"%s\" --startup-profile --minimized", exePath);
        cmdLine[(sizeof(cmdLine) / sizeof(cmdLine[0])) - 1] = L'\0';
        st = RegSetValueExW(key, valueName, 0, REG_SZ,
                            (const BYTE *)cmdLine,
                            (DWORD)((wcslen(cmdLine) + 1) * sizeof(WCHAR)));
    } else {
        st = RegDeleteValueW(key, valueName);
        if (st == ERROR_FILE_NOT_FOUND) {
            st = ERROR_SUCCESS;
        }
    }
    RegCloseKey(key);
    return st;
}

/* --------------------------------------------------- Cursor hide / recovery */

static void FreeSavedCursors(void)
{
    size_t i;
    for (i = 0; i < sizeof(kSystemCursorIds) / sizeof(kSystemCursorIds[0]); ++i) {
        if (g_app.SavedCursors[i] != NULL) {
            DestroyCursor(g_app.SavedCursors[i]);
            g_app.SavedCursors[i] = NULL;
        }
    }
}

static BOOL RestoreSystemCursors(void)
{
    BOOL ok = SystemParametersInfoW(SPI_SETCURSORS, 0, NULL, 0);
    if (ok) {
        FreeSavedCursors();
    } else {
        /* Fallback when SPI_SETCURSORS is unimplemented/unavailable (e.g. Wine):
         * hand our duplicated pre-hide cursor handles back to SetSystemCursor. */
        size_t i, restored = 0;
        for (i = 0; i < sizeof(kSystemCursorIds) / sizeof(kSystemCursorIds[0]); ++i) {
            if (g_app.SavedCursors[i] != NULL) {
                if (SetSystemCursor(g_app.SavedCursors[i], kSystemCursorIds[i])) {
                    ++restored;
                } else {
                    DestroyCursor(g_app.SavedCursors[i]);
                }
                g_app.SavedCursors[i] = NULL;
            }
        }
        if (restored == sizeof(kSystemCursorIds) / sizeof(kSystemCursorIds[0])) {
            ok = TRUE;
        }
    }
    if (ok) {
        (void)WriteRegDword(REG_SETTINGS_KEY, REG_HIDDEN_VALUE, 0);
        g_app.CursorHidden = FALSE;
    }
    return ok;
}

static void RecoverCursorsIfNeeded(void)
{
    if (ReadRegDword(REG_SETTINGS_KEY, REG_HIDDEN_VALUE, 0) != 0) {
        (void)RestoreSystemCursors();
    }
}

static BOOL HideSystemCursors(void)
{
    BYTE andMask[128];
    BYTE xorMask[128];
    HCURSOR blank;
    size_t i;
    size_t applied = 0;

    memset(andMask, 0xFF, sizeof(andMask));
    memset(xorMask, 0x00, sizeof(xorMask));

    blank = CreateCursor(GetModuleHandleW(NULL), 0, 0, 32, 32, andMask, xorMask);
    if (blank == NULL) {
        return FALSE;
    }

    FreeSavedCursors();
    for (i = 0; i < sizeof(kSystemCursorIds) / sizeof(kSystemCursorIds[0]); ++i) {
        HCURSOR cur = (HCURSOR)LoadImageW(NULL, MAKEINTRESOURCEW(kSystemCursorIds[i]),
                                          IMAGE_CURSOR, 0, 0, LR_SHARED);
        if (cur != NULL) {
            g_app.SavedCursors[i] = (HCURSOR)CopyIcon((HICON)cur);
        }
        if (g_app.SavedCursors[i] == NULL) {
            FreeSavedCursors();
            DestroyCursor(blank);
            return FALSE;
        }
    }

    /* Record breadcrumb BEFORE modifying global system cursors so abnormal
     * termination or power loss recovers user cursors on next startup. */
    if (WriteRegDword(REG_SETTINGS_KEY, REG_HIDDEN_VALUE, 1) != ERROR_SUCCESS) {
        FreeSavedCursors();
        DestroyCursor(blank);
        return FALSE;
    }

    for (i = 0; i < sizeof(kSystemCursorIds) / sizeof(kSystemCursorIds[0]); ++i) {
        HCURSOR copy = (HCURSOR)CopyIcon((HICON)blank);
        if (copy != NULL) {
            /* SetSystemCursor takes ownership of and destroys `copy` on success. */
            if (SetSystemCursor(copy, kSystemCursorIds[i])) {
                ++applied;
            } else {
                DestroyCursor(copy);
            }
        }
    }
    DestroyCursor(blank);

    if (applied == 0) {
        FreeSavedCursors();
        (void)WriteRegDword(REG_SETTINGS_KEY, REG_HIDDEN_VALUE, 0);
        return FALSE;
    }
    g_app.CursorHidden = TRUE;
    return TRUE;
}

static void UpdateCursorTimerInterval(HWND hwnd)
{
    UINT interval = g_app.CursorHidden ? CURSOR_POLL_HIDDEN_MS : CURSOR_POLL_VISIBLE_MS;
    SetTimer(hwnd, TIMER_ID_CURSOR, interval, NULL);
}

static void PollCursorIdle(HWND hwnd)
{
    POINT pt;
    ULONGLONG now = GetTickCount64();
    BOOL buttonDown;

    if (!g_app.AutoHideEnabled) {
        if (g_app.CursorHidden) {
            (void)RestoreSystemCursors();
            UpdateCursorTimerInterval(hwnd);
        }
        return;
    }

    if (!GetCursorPos(&pt)) {
        return;
    }

    buttonDown = ((GetAsyncKeyState(VK_LBUTTON) |
                   GetAsyncKeyState(VK_RBUTTON) |
                   GetAsyncKeyState(VK_MBUTTON)) & 0x8000) != 0;

    if (!g_app.HaveMousePt) {
        g_app.LastMousePt = pt;
        g_app.HaveMousePt = TRUE;
        g_app.LastMoveTick = now;
        return;
    }

    if (pt.x != g_app.LastMousePt.x || pt.y != g_app.LastMousePt.y || buttonDown) {
        g_app.LastMousePt = pt;
        g_app.LastMoveTick = now;
        if (g_app.CursorHidden) {
            (void)RestoreSystemCursors();
            UpdateCursorTimerInterval(hwnd);
        }
        return;
    }

    if (!g_app.CursorHidden && (now - g_app.LastMoveTick) >= CURSOR_IDLE_MS) {
        if (HideSystemCursors()) {
            UpdateCursorTimerInterval(hwnd);
        }
    }
}

/* ----------------------------------------------------- Driver IOCTL helpers */

static void FormatWin32Error(DWORD err, WCHAR *buf, size_t cch)
{
    WCHAR msg[160];
    DWORD len;

    msg[0] = L'\0';
    len = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                         NULL, err, 0, msg, (DWORD)(sizeof(msg) / sizeof(msg[0])), NULL);
    while (len > 0 && (msg[len - 1] == L'\r' || msg[len - 1] == L'\n' || msg[len - 1] == L' ')) {
        msg[--len] = L'\0';
    }
    if (len > 0) {
        _snwprintf(buf, cch, L"Win32 error %lu (%s)", (unsigned long)err, msg);
    } else {
        _snwprintf(buf, cch, L"Win32 error %lu (0x%08lX)", (unsigned long)err, (unsigned long)err);
    }
    buf[cch - 1] = L'\0';
}

static BOOL SendDeviceIoctl(LPCWSTR devicePath, DWORD ioctlCode,
                            const void *inBuf, DWORD inSize,
                            void *outBuf, DWORD outSize,
                            DWORD *outError)
{
    HANDLE h = CreateFileW(devicePath, GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD bytes = 0;
    BOOL ok;

    if (h == INVALID_HANDLE_VALUE) {
        if (outError) *outError = GetLastError();
        return FALSE;
    }
    ok = DeviceIoControl(h, ioctlCode, (LPVOID)inBuf, inSize, outBuf, outSize, &bytes, NULL);
    if (!ok) {
        if (outError) *outError = GetLastError();
        CloseHandle(h);
        return FALSE;
    }
    CloseHandle(h);
    if (outSize != 0 && bytes < outSize) {
        if (outError) *outError = ERROR_BAD_LENGTH;
        return FALSE;
    }
    if (outError) *outError = ERROR_SUCCESS;
    return TRUE;
}

static volatile LONG g_loadEnabled;
static HANDLE g_loadStop, g_loadThread;
#include "cpu_load.h"

static void RefreshDriverStatuses(void)
{
    SWFAN_POWER_STATUS pwr;
    SWBAT_CHARGE_LIMIT_STATUS bat;
    DWORD err = 0;

    memset(&pwr, 0, sizeof(pwr));
    if (SendDeviceIoctl(L"\\\\.\\SwitchBacklight", IOCTL_SWFAN_GET_POWER_STATUS,
                        NULL, 0, &pwr, sizeof(pwr), &err) &&
        pwr.Version == SWFAN_POWER_STATUS_VERSION) {
        g_app.Power = pwr;
        g_app.HavePower = TRUE;
        g_app.PowerError = ERROR_SUCCESS;
    } else {
        g_app.HavePower = FALSE;
        g_app.PowerError = err ? err : ERROR_INVALID_DATA;
    }

    InterlockedExchange(&g_loadEnabled, g_app.HavePower && g_app.Power.CpuDfllActive &&
                         g_app.Power.PowerMode < SwFanPowerModeDefaultFixed);
    memset(&g_app.Fan,0,sizeof(g_app.Fan));
    g_app.HaveFan=SendDeviceIoctl(L"\\\\.\\SwitchBacklight",IOCTL_SWFAN_GET_FAN_PROFILE,
        NULL,0,&g_app.Fan,sizeof(g_app.Fan),&err) &&
        g_app.Fan.Version==SWFAN_PROFILE_VERSION && g_app.Fan.Mode<SwFanProfileCount;
    err = 0;
    memset(&bat, 0, sizeof(bat));
    if (SendDeviceIoctl(L"\\\\.\\SwitchBattery", IOCTL_SWBAT_GET_CHARGE_LIMIT,
                        NULL, 0, &bat, sizeof(bat), &err) &&
        bat.Version == SWBAT_CHARGE_LIMIT_STATUS_VERSION) {
        g_app.Battery = bat;
        g_app.HaveBattery = TRUE;
        g_app.BatteryError = ERROR_SUCCESS;
    } else {
        g_app.HaveBattery = FALSE;
        g_app.BatteryError = err ? err : ERROR_INVALID_DATA;
    }
}

/* ----------------------------------------------------- Formatting & Strings */

static LPCWSTR PowerModeMenuLabel(UINT mode)
{
    static const WCHAR *labels[SwFanPowerModeDefaultFixed] = {
        L"Console",
        L"Handheld",
        L"OC_CPU",
        L"OC_GPU",
        L"OC_All",
        L"Perf_All",
        L"Perf_OC_All"
    };
    return mode < SwFanPowerModeDefaultFixed ? labels[mode] : L"Unavailable Power Mode";
}

static void FormatChargeBandLabel(UINT limitCenter, WCHAR *buf, size_t cch)
{
    if (limitCenter == SWBAT_CHARGE_LIMIT_DOCKED) {
        _snwprintf(buf, cch, L"50-60%% Band (Nominal 55%%)");
    } else if (limitCenter == SWBAT_CHARGE_LIMIT_HIGH) {
        _snwprintf(buf, cch, L"80-90%% Band (Nominal 85%%)");
    } else {
        _snwprintf(buf, cch, L"Off (100%% Full Charge)");
    }
    buf[cch - 1] = L'\0';
}

static BOOL HasFreshGpu(const SWFAN_POWER_STATUS *p)
{
    return p->Version == SWFAN_POWER_STATUS_VERSION && p->GpuReportedKhz != 0 &&
           p->GpuSampleAgeMs <= 2000u && (p->Flags & SWFAN_POWER_FLAG_LIVE_GPU_COUNTER) != 0;
}

static void FormatPowerHeader(const TRAY_STATE *st, WCHAR *buf, size_t cch)
{
    if (!st->HavePower) {
        WCHAR errText[192];
        FormatWin32Error(st->PowerError, errText, sizeof(errText) / sizeof(errText[0]));
        _snwprintf(buf, cch, L"Power Driver Unavailable: %s", errText);
        buf[cch - 1] = L'\0';
        return;
    }

    const SWFAN_POWER_STATUS *p = &st->Power;
    WCHAR gpuPart[96];
    if (HasFreshGpu(p)) {
        _snwprintf(gpuPart, sizeof(gpuPart) / sizeof(gpuPart[0]),
                   L"GPU %u.%u MHz (%u mV)",
                   p->GpuReportedKhz / 1000u, (p->GpuReportedKhz % 1000u) / 100u,
                   p->GpuReportedUv / 1000u);
    } else {
        _snwprintf(gpuPart, sizeof(gpuPart) / sizeof(gpuPart[0]),
                   L"GPU <=%u.%u MHz (%s)",
                   p->GpuPolicyMaxKhz / 1000u, (p->GpuPolicyMaxKhz % 1000u) / 100u,
                   p->GpuPolicyMode ? L"HighOPT" : L"Fixed");
    }
    gpuPart[(sizeof(gpuPart) / sizeof(gpuPart[0])) - 1] = L'\0';

    if (p->TempValid && p->TempSampleAgeMs <= 6000u) {
        _snwprintf(buf, cch, L"CPU %u.%u MHz (%u mV) | %s | RAM %u MHz | %u C",
                   p->CpuActualKhz / 1000u, (p->CpuActualKhz % 1000u) / 100u,
                   p->CpuMv, gpuPart, p->RamActualKhz / 1000u, p->TempC);
    } else {
        _snwprintf(buf, cch, L"CPU %u.%u MHz (%u mV) | %s | RAM %u MHz",
                   p->CpuActualKhz / 1000u, (p->CpuActualKhz % 1000u) / 100u,
                   p->CpuMv, gpuPart, p->RamActualKhz / 1000u);
    }
    buf[cch - 1] = L'\0';
}

static void FormatBatteryHeader(const TRAY_STATE *st, WCHAR *buf, size_t cch)
{
    if (!st->HaveBattery) {
        WCHAR errText[192];
        FormatWin32Error(st->BatteryError, errText, sizeof(errText) / sizeof(errText[0]));
        _snwprintf(buf, cch, L"Charging Limit Driver Unavailable: %s", errText);
        buf[cch - 1] = L'\0';
        return;
    }

    const SWBAT_CHARGE_LIMIT_STATUS *b = &st->Battery;
    WCHAR band[64];
    LPCWSTR powerState;

    FormatChargeBandLabel(b->LimitCenter, band, sizeof(band) / sizeof(band[0]));
    if (b->Flags & SWBAT_CHARGE_FLAG_OTG_ACTIVE) {
        powerState = L"USB-C OTG 5V Out";
    } else if (b->Flags & SWBAT_CHARGE_FLAG_EXTERNAL_ONLINE) {
        if (b->Flags & SWBAT_CHARGE_FLAG_CHARGING) {
            powerState = L"AC Charging";
        } else if (b->Flags & SWBAT_CHARGE_FLAG_INHIBITED) {
            powerState = L"Hold (Ext Power Active, No HIZ)";
        } else {
            powerState = L"AC Connected (Not Charging)";
        }
    } else {
        powerState = L"On Battery";
    }

    _snwprintf(buf, cch, L"Battery %u%% (%u mV) | Limit: %s | %s",
               b->CapacityPercent, b->VoltageMv, band, powerState);
    buf[cch - 1] = L'\0';
}

/* ---------------------------------------------------------------- Tray Icon */

static HICON CreateStatusIcon(void)
{
    int w = GetSystemMetrics(SM_CXSMICON);
    int h = GetSystemMetrics(SM_CYSMICON);
    if (w <= 0) w = 16;
    if (h <= 0) h = 16;
    /* Original Switchroot Joy-Con artwork; owned handle, released on exit. */
    HICON icon = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(101),
                                 IMAGE_ICON, w, h, LR_DEFAULTCOLOR);
    return icon ? icon : CopyIcon(LoadIconW(NULL, IDI_APPLICATION));
}

static void UpdateTrayVisuals(void)
{
    if (g_app.Icon == NULL) {
        g_app.Icon = CreateStatusIcon();
        g_app.Nid.hIcon = g_app.Icon;
    }

    if (g_app.HavePower && g_app.HaveBattery) {
        WCHAR gpu[32];
        if (HasFreshGpu(&g_app.Power))
            _snwprintf(gpu, sizeof(gpu) / sizeof(gpu[0]), L"%uMHz", g_app.Power.GpuReportedKhz / 1000u);
        else wcscpy_s(gpu, sizeof(gpu) / sizeof(gpu[0]), L"--");
        if (g_app.Battery.LimitCenter != 0) {
            _snwprintf(g_app.Nid.szTip, sizeof(g_app.Nid.szTip) / sizeof(WCHAR),
                       L"CPU %uMHz GPU %s | Bat %u%% (%u-%u%%)",
                       g_app.Power.CpuActualKhz / 1000u,
                       gpu,
                       g_app.Battery.CapacityPercent,
                       g_app.Battery.BandMin,
                       g_app.Battery.BandMax);
        } else {
            _snwprintf(g_app.Nid.szTip, sizeof(g_app.Nid.szTip) / sizeof(WCHAR),
                       L"CPU %uMHz GPU %s | Bat %u%% (Limit Off)",
                       g_app.Power.CpuActualKhz / 1000u,
                       gpu,
                       g_app.Battery.CapacityPercent);
        }
    } else {
        _snwprintf(g_app.Nid.szTip, sizeof(g_app.Nid.szTip) / sizeof(WCHAR),
                   L"%s (Driver Offline - Check Status)", APP_NAME);
    }
    g_app.Nid.szTip[(sizeof(g_app.Nid.szTip) / sizeof(WCHAR)) - 1] = L'\0';

    if (g_app.Nid.hWnd != NULL &&
        (!g_app.IconAdded || !Shell_NotifyIconW(NIM_MODIFY, &g_app.Nid))) {
        g_app.IconAdded = Shell_NotifyIconW(NIM_ADD, &g_app.Nid);
    }
}

/* ----------------------------------------------------------- User Actions */

static void ApplyPowerModeAction(HWND hwnd, UINT requestedMode)
{
    SWFAN_POWER_REQUEST modeIn = {SWFAN_POWER_STATUS_VERSION, requestedMode};
    DWORD err = 0;

    if (!g_app.HavePower || !g_app.Power.IsLite) {
        MessageBoxW(hwnd,
                    L"SwitchFan DFLL v2 Switch Lite power control is not available on this system.\n\n"
                    L"Install the updated switchfan.sys and matching DFLL firmware package first.",
                    APP_NAME, MB_OK | MB_ICONERROR);
        return;
    }


    if (!SendDeviceIoctl(L"\\\\.\\SwitchBacklight", IOCTL_SWFAN_SET_POWER_MODE,
                         &modeIn, sizeof(modeIn), NULL, 0, &err)) {
        WCHAR errText[192], msg[320];
        FormatWin32Error(err, errText, sizeof(errText) / sizeof(errText[0]));
        _snwprintf(msg, sizeof(msg) / sizeof(msg[0]),
                   L"Failed to set Power Mode %u:\n%s", requestedMode, errText);
        msg[(sizeof(msg) / sizeof(msg[0])) - 1] = L'\0';
        MessageBoxW(hwnd, msg, APP_NAME, MB_OK | MB_ICONERROR);
        RefreshDriverStatuses();
        UpdateTrayVisuals();
        return;
    }

    RefreshDriverStatuses();
    UpdateTrayVisuals();
    if (!g_app.HavePower || g_app.Power.PowerMode != requestedMode) {
        MessageBoxW(hwnd,
                    L"Driver did not confirm the requested Power Mode on hardware readback.",
                    APP_NAME, MB_OK | MB_ICONERROR);
        return;
    }
    /* Save confirmed user intent, not a failed request or a temporary kernel
     * protection mode. Startup restores this independently of the fan mode. */
    if (WriteRegDword(REG_SETTINGS_KEY, REG_POWER_MODE_VALUE, requestedMode) != ERROR_SUCCESS) {
        MessageBoxW(hwnd, L"Power mode applied, but its startup setting could not be saved.",
                    APP_NAME, MB_OK | MB_ICONWARNING);
    }
}

static DWORD SetFanMode(UINT mode)
{
    SWFAN_PROFILE_REQUEST request={SWFAN_PROFILE_VERSION,mode};
    DWORD err=0;
    if (!SendDeviceIoctl(L"\\\\.\\SwitchBacklight",IOCTL_SWFAN_SET_FAN_PROFILE,
        &request,sizeof(request),NULL,0,&err)) return err;
    RefreshDriverStatuses();
    if (!g_app.HaveFan || g_app.Fan.Mode!=mode) return ERROR_INVALID_DATA;
    return (DWORD)WriteRegDword(REG_SETTINGS_KEY,REG_FAN_MODE_VALUE,mode);
}

static void ApplyFanModeAction(HWND hwnd, UINT mode)
{
    DWORD err=SetFanMode(mode);
    if (err) {
        WCHAR message[240];
        FormatWin32Error(err,message,sizeof(message)/sizeof(message[0]));
        MessageBoxW(hwnd,message,L"Fan Mode",MB_OK|MB_ICONERROR);
    }
    UpdateTrayVisuals();
}

static void ApplyChargeLimitAction(HWND hwnd, UINT requestedLimit)
{
    ULONG limitIn = requestedLimit;
    DWORD err = 0;

    if (!SendDeviceIoctl(L"\\\\.\\SwitchBattery", IOCTL_SWBAT_SET_CHARGE_LIMIT,
                         &limitIn, sizeof(limitIn), NULL, 0, &err)) {
        WCHAR errText[192], msg[320];
        FormatWin32Error(err, errText, sizeof(errText) / sizeof(errText[0]));
        _snwprintf(msg, sizeof(msg) / sizeof(msg[0]),
                   L"Failed to apply Charging Limit (%u%%):\n%s", requestedLimit, errText);
        msg[(sizeof(msg) / sizeof(msg[0])) - 1] = L'\0';
        MessageBoxW(hwnd, msg, APP_NAME, MB_OK | MB_ICONERROR);
        RefreshDriverStatuses();
        UpdateTrayVisuals();
        return;
    }

    RefreshDriverStatuses();
    UpdateTrayVisuals();
    if (!g_app.HaveBattery || g_app.Battery.LimitCenter != requestedLimit ||
        (int32_t)g_app.Battery.LastApplyStatus < 0) {
        WCHAR msg[256];
        _snwprintf(msg, sizeof(msg) / sizeof(msg[0]),
                   L"Charging Limit readback verification failed (readback=%u%%, NTSTATUS=0x%08X).",
                   g_app.HaveBattery ? g_app.Battery.LimitCenter : 999u,
                   g_app.HaveBattery ? g_app.Battery.LastApplyStatus : 0xFFFFFFFFu);
        msg[(sizeof(msg) / sizeof(msg[0])) - 1] = L'\0';
        MessageBoxW(hwnd, msg, APP_NAME, MB_OK | MB_ICONERROR);
    }
}

static void ShowDetailsDialog(HWND hwnd)
{
    WCHAR pwrLine[256], batLine[256], details[1800];

    RefreshDriverStatuses();
    FormatPowerHeader(&g_app, pwrLine, sizeof(pwrLine) / sizeof(pwrLine[0]));
    FormatBatteryHeader(&g_app, batLine, sizeof(batLine) / sizeof(batLine[0]));

    _snwprintf(
        details, sizeof(details) / sizeof(details[0]),
        L"%s\n%s\n\n"
        L"Driver Readback Details:\n"
        L"  PowerMode: %u | CPU Target/Actual: %u / %u kHz (%u mV) | Fan PWM: %u/236\n"
        L"  GPU Policy: mode=%u (%u..%u kHz) | KMD Reported: %u kHz / %u uV (status=0x%08X)\n"
        L"  CPU cap requested/effective: %u / %u kHz; GPU requested: %u kHz\n"
        L"  CPU DFLL active=%u, Speedo=%u, process=%u, last error=0x%08X\n"
        L"  BQ24193 REG00=0x%02X (HIZ=%u) REG01=0x%02X REG08=0x%02X | NTSTATUS=0x%08X\n\n"
        L"Platform & Safety Notes:\n"
        L"  1. Charging Limit uses Switch L4T +-5%% SoC hysteresis: nominal 55%% regulates 50-60%%,\n"
        L"     and nominal 85%% regulates 80-90%%. Kernel enforces the band even if this tray exits.\n"
        L"  2. Unlike Linux bq2419x HIZ drain, Windows keeps external VBUS power connected above\n"
        L"     60%%/90%% (REG00 EN_HIZ=0) and only inhibits battery charging (REG01 CHG_CONFIG=0).\n"
        L"  3. 5s idle cursor hide replaces standard Windows OCR_* cursors and restores the user\n"
        L"     theme via SPI_SETCURSORS on movement/exit; apps drawing private cursors are unaffected.",
        pwrLine, batLine,
        g_app.HavePower ? g_app.Power.PowerMode : 0u,
        g_app.HavePower ? g_app.Power.CpuTargetKhz : 0u,
        g_app.HavePower ? g_app.Power.CpuActualKhz : 0u,
        g_app.HavePower ? g_app.Power.CpuMv : 0u,
        g_app.HavePower ? g_app.Power.FanDuty : 0u,
        g_app.HavePower ? g_app.Power.GpuPolicyMode : 0u,
        g_app.HavePower ? g_app.Power.GpuPolicyMinKhz : 0u,
        g_app.HavePower ? g_app.Power.GpuPolicyMaxKhz : 0u,
        g_app.HavePower ? g_app.Power.GpuReportedKhz : 0u,
        g_app.HavePower ? g_app.Power.GpuReportedUv : 0u,
        g_app.HavePower ? g_app.Power.GpuStatusWord : 0u,
        g_app.Power.CpuRequestedMaxKhz, g_app.Power.CpuPolicyMaxKhz, g_app.Power.GpuRequestedMaxKhz,
        g_app.Power.CpuDfllActive, g_app.Power.CpuSpeedo, g_app.Power.CpuProcessId, g_app.Power.CpuLastError,
        g_app.HaveBattery ? g_app.Battery.Reg00InputSrc : 0u,
        (g_app.HaveBattery && (g_app.Battery.Flags & SWBAT_CHARGE_FLAG_HIZ_ACTIVE)) ? 1u : 0u,
        g_app.HaveBattery ? g_app.Battery.Reg01PwrOn : 0u,
        g_app.HaveBattery ? g_app.Battery.Reg08SysStat : 0u,
        g_app.HaveBattery ? g_app.Battery.LastApplyStatus : 0u);
    details[(sizeof(details) / sizeof(details[0])) - 1] = L'\0';

    MessageBoxW(hwnd, details, APP_NAME, MB_OK | MB_ICONINFORMATION);
}

static void ShowTrayMenu(HWND hwnd)
{
    HMENU root, powerMenu, limitMenu, fanMenu;
    POINT pt;

    RefreshDriverStatuses();
    UpdateTrayVisuals();

    root = CreatePopupMenu();
    powerMenu = CreatePopupMenu();
    limitMenu = CreatePopupMenu();
    fanMenu = CreatePopupMenu();
    if (!root || !powerMenu || !limitMenu || !fanMenu) {
        if (root) DestroyMenu(root);
        if (powerMenu) DestroyMenu(powerMenu);
        if (limitMenu) DestroyMenu(limitMenu);
        if (fanMenu) DestroyMenu(fanMenu);
        return;
    }

    /* Power Mode submenu */
    for (UINT m = 0; m < SwFanPowerModeDefaultFixed; ++m) {
        UINT flags = MF_STRING;
        if (!g_app.HavePower || !g_app.Power.IsLite ||
            !(g_app.Power.Flags & SWFAN_POWER_FLAG_LIVE_GPU_COUNTER) || !g_app.Power.CpuDfllAvailable) {
            flags |= MF_DISABLED | MF_GRAYED;
        } else if (g_app.Power.PowerMode == m) {
            flags |= MF_CHECKED;
        }
        AppendMenuW(powerMenu, flags, IDM_POWER_BASE + m, PowerModeMenuLabel(m));
    }
    AppendMenuW(root, MF_POPUP, (UINT_PTR)powerMenu, L"Power Mode");
    static const wchar_t *fanNames[SwFanProfileCount]={L"Console",L"Handheld",L"Cool"};
    for (UINT m=0;m<SwFanProfileCount;++m) {
        UINT flags=MF_STRING;
        if (!g_app.HaveFan) flags|=MF_DISABLED|MF_GRAYED;
        else if (g_app.Fan.Mode==m) flags|=MF_CHECKED;
        AppendMenuW(fanMenu,flags,IDM_FAN_BASE+m,fanNames[m]);
    }
    AppendMenuW(root,MF_POPUP,(UINT_PTR)fanMenu,L"Fan Mode");

    /* Charging Limit submenu */
    {
        UINT baseFlags = g_app.HaveBattery ? MF_STRING : (MF_STRING | MF_DISABLED | MF_GRAYED);
        UINT cur = g_app.HaveBattery ? g_app.Battery.LimitCenter : 999u;

        AppendMenuW(limitMenu, baseFlags | (cur == SWBAT_CHARGE_LIMIT_OFF ? MF_CHECKED : 0),
                    IDM_LIMIT_OFF, L"Off (100%)");
        AppendMenuW(limitMenu, baseFlags | (cur == SWBAT_CHARGE_LIMIT_HIGH ? MF_CHECKED : 0),
                    IDM_LIMIT_85, L"85% (80-90%)");
        AppendMenuW(limitMenu, baseFlags | (cur == SWBAT_CHARGE_LIMIT_DOCKED ? MF_CHECKED : 0),
                    IDM_LIMIT_55, L"55% (50-60%)");
    }
    AppendMenuW(root, MF_POPUP, (UINT_PTR)limitMenu, L"Charging Limit");

    AppendMenuW(root, MF_SEPARATOR, 0, NULL);
    AppendMenuW(root, MF_STRING | (g_app.AutoHideEnabled ? MF_CHECKED : 0),
                IDM_AUTOHIDE_TOGGLE, L"Auto-hide Cursor");
    AppendMenuW(root, MF_STRING, IDM_RESTORE_CURSOR, L"Restore Cursor");
    AppendMenuW(root, MF_STRING | (IsAutostartEnabledEx(REG_RUN_VALUE) ? MF_CHECKED : 0),
                IDM_STARTUP_TOGGLE, L"Start at Login");

    AppendMenuW(root, MF_SEPARATOR, 0, NULL);
    AppendMenuW(root, MF_STRING, IDM_DETAILS, L"Hardware Details");
    AppendMenuW(root, MF_STRING, IDM_RESET_DEFAULTS, L"Reset Defaults");
    AppendMenuW(root, MF_STRING, IDM_EXIT, L"Exit");

    if (!GetCursorPos(&pt)) {
        pt.x = 100;
        pt.y = 100;
    }
    /* If cursor was hidden when opening menu via keyboard/touch, restore it. */
    if (g_app.CursorHidden) {
        (void)RestoreSystemCursors();
        UpdateCursorTimerInterval(hwnd);
    }

    SetForegroundWindow(hwnd);
    TrackPopupMenu(root, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(root);
}

static DWORD ResetAllToDefaults(HWND hwnd, BOOL quiet)
{
    ULONG chargeLimit = SWBAT_CHARGE_LIMIT_HIGH;
    SWFAN_POWER_REQUEST baseline = {SWFAN_POWER_STATUS_VERSION, SwFanPowerModeHandheld};
    DWORD err1 = 0, err2 = 0, fanError;
    BOOL ok1, ok2, cursorsOk;

    cursorsOk = RestoreSystemCursors();
    ok1 = SendDeviceIoctl(L"\\\\.\\SwitchBacklight", IOCTL_SWFAN_SET_POWER_MODE,
                          &baseline, sizeof(baseline), NULL, 0, &err1);
    ok2 = SendDeviceIoctl(L"\\\\.\\SwitchBattery", IOCTL_SWBAT_SET_CHARGE_LIMIT,
                          &chargeLimit, sizeof(chargeLimit), NULL, 0, &err2);
    fanError = SetFanMode(SwFanProfileHandheld);
    RefreshDriverStatuses();
    UpdateTrayVisuals();
    if (ok1 && (!g_app.HavePower || g_app.Power.PowerMode != SwFanPowerModeHandheld)) {
        ok1 = FALSE;
        err1 = g_app.PowerError ? g_app.PowerError : ERROR_INVALID_DATA;
    }
    if (ok1) {
        err1 = (DWORD)WriteRegDword(REG_SETTINGS_KEY, REG_POWER_MODE_VALUE, SwFanPowerModeHandheld);
        if (err1 != ERROR_SUCCESS) ok1 = FALSE;
    }
    if (ok2 && (!g_app.HaveBattery || g_app.Battery.LimitCenter != chargeLimit)) {
        ok2 = FALSE;
        err2 = g_app.BatteryError ? g_app.BatteryError : ERROR_INVALID_DATA;
    }

    if (!quiet && (!ok1 || !ok2 || fanError || !cursorsOk)) {
        WCHAR e1[160], e2[160], msg[420];
        FormatWin32Error(err1, e1, sizeof(e1) / sizeof(e1[0]));
        FormatWin32Error(err2, e2, sizeof(e2) / sizeof(e2[0]));
        _snwprintf(msg, sizeof(msg) / sizeof(msg[0]),
                   L"Reset failed or incomplete:\n  Handheld: %s\n  Charging Limit 85%%: %s\n  Fan Handheld: %s\n  Cursors: %s",
                   ok1 ? L"OK" : e1, ok2 ? L"OK" : e2, fanError ? L"Failed" : L"OK", cursorsOk ? L"OK" : L"Restore failed");
        msg[(sizeof(msg) / sizeof(msg[0])) - 1] = L'\0';
        MessageBoxW(hwnd, msg, APP_NAME, MB_OK | MB_ICONWARNING);
    }
    if (!ok1) return err1;
    if (!ok2) return err2;
    if (fanError) return fanError;
    return cursorsOk ? ERROR_SUCCESS : ERROR_GEN_FAILURE;
}

/* ------------------------------------------------------------ Window Proc */

static void ApplyStartupProfile(HWND hwnd)
{
    if (g_startupFanPending && g_app.HaveFan &&
        (g_app.Fan.Flags&SWFAN_PROFILE_VALID) && g_app.Fan.SampleAgeMs<=6000) {
        UINT fan=ReadRegDword(REG_SETTINGS_KEY,REG_FAN_MODE_VALUE,SwFanProfileHandheld);
        if (fan>=SwFanProfileCount) fan=SwFanProfileHandheld;
        g_startupFanPending=FALSE;
        if (g_app.Fan.Mode!=fan) ApplyFanModeAction(hwnd,fan);
    }
    /* The GPU provider becomes live after the login display-driver activation.
     * Reuse the existing status timer until ready, then issue at most one mode
     * request. A manual mode choice cancels this pending restore. Handheld is
     * the first-use/reset default, rather than overriding every saved choice. */
    if (!g_startupProfilePending || !g_app.HavePower || !g_app.Power.IsLite ||
        !g_app.Power.CpuDfllAvailable || !g_app.Power.TempValid || g_app.Power.CpuLastError ||
        (g_app.Power.Flags & SWFAN_POWER_FLAG_SCREEN_OFF) ||
        !(g_app.Power.Flags & SWFAN_POWER_FLAG_LIVE_GPU_COUNTER)) return;
    UINT mode = ReadRegDword(REG_SETTINGS_KEY, REG_POWER_MODE_VALUE, SwFanPowerModeHandheld);
    if (mode >= SwFanPowerModeDefaultFixed) mode = SwFanPowerModeHandheld;
    g_startupProfilePending = FALSE;
    if (g_app.Power.PowerMode != mode)
        ApplyPowerModeAction(hwnd, mode);
}

static LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g_taskbarCreated != 0 && msg == g_taskbarCreated) {
        /* Explorer removes all notification icons when its taskbar restarts. */
        g_app.IconAdded = FALSE;
        UpdateTrayVisuals();
        return 0;
    }
    switch (msg) {
    case WM_CREATE:
        memset(&g_app.Nid, 0, sizeof(g_app.Nid));
        g_app.Nid.cbSize = sizeof(g_app.Nid);
        g_app.Nid.hWnd = hwnd;
        g_app.Nid.uID = 1;
        g_app.Nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        g_app.Nid.uCallbackMessage = WM_TRAYICON;
        /* Startup restoration may update the tray immediately on a warm
         * relaunch. Initialize its identity first and retain the created icon. */
        RefreshDriverStatuses();
        ApplyStartupProfile(hwnd);
        UpdateTrayVisuals();
        g_app.LastMoveTick = GetTickCount64();
        UpdateCursorTimerInterval(hwnd);
        SetTimer(hwnd, TIMER_ID_STATUS, STATUS_POLL_MS, NULL);
        return 0;

    case WM_TIMER:
        if (wParam == TIMER_ID_CURSOR) {
            PollCursorIdle(hwnd);
        } else if (wParam == TIMER_ID_STATUS) {
            RefreshDriverStatuses();
            ApplyStartupProfile(hwnd);
            UpdateTrayVisuals();
        }
        return 0;

    case WM_TRAYICON:
        if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_LBUTTONUP ||
            LOWORD(lParam) == WM_CONTEXTMENU) {
            ShowTrayMenu(hwnd);
        }
        return 0;

    case WM_COMMAND: {
        UINT id = LOWORD(wParam);
        if (id>=IDM_FAN_BASE && id<IDM_FAN_BASE+SwFanProfileCount) {
            g_startupFanPending=FALSE;
            ApplyFanModeAction(hwnd,id-IDM_FAN_BASE);
            return 0;
        }
        if (id >= IDM_POWER_BASE && id < IDM_POWER_BASE + SwFanPowerModeDefaultFixed) {
            g_startupProfilePending = FALSE;
            ApplyPowerModeAction(hwnd, id - IDM_POWER_BASE);
            return 0;
        }
        switch (id) {
        case IDM_LIMIT_OFF:
            ApplyChargeLimitAction(hwnd, SWBAT_CHARGE_LIMIT_OFF);
            return 0;
        case IDM_LIMIT_85:
            ApplyChargeLimitAction(hwnd, SWBAT_CHARGE_LIMIT_HIGH);
            return 0;
        case IDM_LIMIT_55:
            ApplyChargeLimitAction(hwnd, SWBAT_CHARGE_LIMIT_DOCKED);
            return 0;
        case IDM_AUTOHIDE_TOGGLE:
            g_app.AutoHideEnabled = !g_app.AutoHideEnabled;
            (void)WriteRegDword(REG_SETTINGS_KEY, REG_AUTOHIDE_VALUE, g_app.AutoHideEnabled ? 1u : 0u);
            if (!g_app.AutoHideEnabled && g_app.CursorHidden) {
                (void)RestoreSystemCursors();
            }
            g_app.LastMoveTick = GetTickCount64();
            UpdateCursorTimerInterval(hwnd);
            return 0;
        case IDM_RESTORE_CURSOR:
            (void)RestoreSystemCursors();
            g_app.LastMoveTick = GetTickCount64();
            UpdateCursorTimerInterval(hwnd);
            return 0;
        case IDM_STARTUP_TOGGLE: {
            BOOL next = !IsAutostartEnabledEx(REG_RUN_VALUE);
            LSTATUS st = SetAutostartEnabledEx(REG_RUN_VALUE, next);
            if (st != ERROR_SUCCESS) {
                WCHAR errText[192];
                FormatWin32Error((DWORD)st, errText, sizeof(errText) / sizeof(errText[0]));
                MessageBoxW(hwnd, errText, APP_NAME, MB_OK | MB_ICONERROR);
            }
            return 0;
        }
        case IDM_DETAILS:
            ShowDetailsDialog(hwnd);
            return 0;
        case IDM_RESET_DEFAULTS:
            g_startupProfilePending = FALSE;
            g_startupFanPending = FALSE;
            ResetAllToDefaults(hwnd, FALSE);
            return 0;
        case IDM_EXIT:
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }

    case WM_TRAY_SHUTDOWN:
        (void)RestoreSystemCursors();
        DestroyWindow(hwnd);
        return 0;

    case WM_QUERYENDSESSION:
        (void)RestoreSystemCursors();
        return TRUE;

    case WM_ENDSESSION:
        if (wParam) {
            (void)RestoreSystemCursors();
        }
        return 0;

    case WM_DESTROY:
        if (g_loadStop) SetEvent(g_loadStop);
        KillTimer(hwnd, TIMER_ID_CURSOR);
        KillTimer(hwnd, TIMER_ID_STATUS);
        (void)RestoreSystemCursors();
        if (g_app.IconAdded) {
            Shell_NotifyIconW(NIM_DELETE, &g_app.Nid);
            g_app.IconAdded = FALSE;
        }
        if (g_app.Icon != NULL) {
            DestroyIcon(g_app.Icon);
            g_app.Icon = NULL;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* ---------------------------------------------------- CLI & Self-Test Mode */

static void StopRunningTrayInstance(void)
{
    HWND existing = FindWindowW(WND_CLASS_NAME, NULL);
    if (existing != NULL) {
        SendMessageTimeoutW(existing, WM_TRAY_SHUTDOWN, 0, 0, SMTO_ABORTIFHUNG, 2000, NULL);
    }
}

static int RunUninstallOrRestore(BOOL fullUninstall, BOOL resetDrivers)
{
    DWORD error;
    StopRunningTrayInstance();
    error = resetDrivers ? ResetAllToDefaults(NULL, TRUE) :
        (RestoreSystemCursors() ? ERROR_SUCCESS : ERROR_GEN_FAILURE);
    if (error != ERROR_SUCCESS) {
        return (int)error;
    }
    if (fullUninstall) {
        LSTATUS st = SetAutostartEnabledEx(REG_RUN_VALUE, FALSE);
        if (st != ERROR_SUCCESS) return (int)st;
        st = RegDeleteKeyW(HKEY_CURRENT_USER, REG_SETTINGS_KEY);
        if (st != ERROR_SUCCESS && st != ERROR_FILE_NOT_FOUND) return (int)st;
    }
    return 0;
}

static int RunSelfTest(void)
{
    WCHAR buf[256];
    HANDLE m1, m2;

    /* 1. Crash-recovery breadcrumb test */
    if (WriteRegDword(REG_SETTINGS_KEY, REG_HIDDEN_VALUE, 1) != ERROR_SUCCESS) {
        return 10;
    }
    RecoverCursorsIfNeeded();
    if (ReadRegDword(REG_SETTINGS_KEY, REG_HIDDEN_VALUE, 99) != 0 || g_app.CursorHidden) {
        return 11;
    }

    /* 2. Cursor hide (CreateCursor + CopyIcon + SetSystemCursor) & restore */
    (void)HideSystemCursors();
    if (!RestoreSystemCursors() || ReadRegDword(REG_SETTINGS_KEY, REG_HIDDEN_VALUE, 99) != 0) {
        return 12;
    }

    /* 3. Login autostart registry toggle on isolated test value */
    if (SetAutostartEnabledEx(REG_RUN_TEST_VALUE, TRUE) != ERROR_SUCCESS ||
        !IsAutostartEnabledEx(REG_RUN_TEST_VALUE)) {
        return 13;
    }
    if (SetAutostartEnabledEx(REG_RUN_TEST_VALUE, FALSE) != ERROR_SUCCESS ||
        IsAutostartEnabledEx(REG_RUN_TEST_VALUE)) {
        return 14;
    }

    /* 4. Single-instance mutex detection */
    m1 = CreateMutexW(NULL, TRUE, L"Local\\SwitchWOA.SwitchPowerTray.SelfTest");
    if (m1 == NULL) {
        return 15;
    }
    m2 = CreateMutexW(NULL, FALSE, L"Local\\SwitchWOA.SwitchPowerTray.SelfTest");
    if (m2 == NULL || GetLastError() != ERROR_ALREADY_EXISTS) {
        if (m2) CloseHandle(m2);
        CloseHandle(m1);
        return 16;
    }
    CloseHandle(m2);
    CloseHandle(m1);

    /* 5. Band formatting (never claim exact 55/85 cutoff) & honest offline error */
    FormatChargeBandLabel(55, buf, sizeof(buf) / sizeof(buf[0]));
    if (wcsstr(buf, L"50-60%") == NULL) {
        return 17;
    }
    FormatChargeBandLabel(85, buf, sizeof(buf) / sizeof(buf[0]));
    if (wcsstr(buf, L"80-90%") == NULL) {
        return 18;
    }
    RefreshDriverStatuses();
    if (!g_app.HavePower) {
        FormatPowerHeader(&g_app, buf, sizeof(buf) / sizeof(buf[0]));
        if (wcsstr(buf, L"Unavailable") == NULL) {
            return 19;
        }
    }
    if (!g_app.HaveBattery) {
        FormatBatteryHeader(&g_app, buf, sizeof(buf) / sizeof(buf[0]));
        if (wcsstr(buf, L"Unavailable") == NULL) {
            return 20;
        }
    }

    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
    HANDLE singleMutex;
    WNDCLASSEXW wc;
    MSG msg;

    (void)hPrevInstance;
    (void)nCmdShow;

    if (lpCmdLine != NULL) {
        if (wcsstr(lpCmdLine, L"--exit") != NULL) {
            StopRunningTrayInstance();
            return 0;
        }
        /* Keep existing scheduled/startup registrations working after update. */
        g_startupProfilePending = wcsstr(lpCmdLine, L"--startup-profile") != NULL ||
                                  wcsstr(lpCmdLine, L"--startup-handheld") != NULL;
        g_startupFanPending = g_startupProfilePending;
        if (wcsstr(lpCmdLine, L"--self-test") != NULL) {
            return RunSelfTest();
        }
        if (wcsstr(lpCmdLine, L"--restore-cursors") != NULL) {
            return RestoreSystemCursors() ? 0 : 1;
        }
        if (wcsstr(lpCmdLine, L"--restore-defaults") != NULL) {
            return RunUninstallOrRestore(FALSE, TRUE);
        }
        if (wcsstr(lpCmdLine, L"--uninstall") != NULL) {
            return RunUninstallOrRestore(TRUE, TRUE);
        }
        if (wcsstr(lpCmdLine, L"--install-autostart") != NULL) {
            return (SetAutostartEnabledEx(REG_RUN_VALUE, TRUE) == ERROR_SUCCESS) ? 0 : 1;
        }
    }

    /* Always recover system cursors first if a previous session terminated while hidden. */
    RecoverCursorsIfNeeded();

    singleMutex = CreateMutexW(NULL, TRUE, SINGLE_MUTEX_NAME);
    if (singleMutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (singleMutex != NULL) {
            CloseHandle(singleMutex);
        }
        return 0;
    }

    g_app.AutoHideEnabled = ReadRegDword(REG_SETTINGS_KEY, REG_AUTOHIDE_VALUE, 1u) != 0;
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = WND_CLASS_NAME;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    wc.hIconSm = wc.hIcon;
    if (!RegisterClassExW(&wc)) {
        CloseHandle(singleMutex);
        return 1;
    }

    g_app.Hwnd = CreateWindowExW(0, WND_CLASS_NAME, APP_NAME, WS_OVERLAPPED,
                                 0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    if (g_app.Hwnd == NULL) {
        CloseHandle(singleMutex);
        return 1;
    }

    g_loadStop = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (g_loadStop) g_loadThread = CreateThread(NULL, 0, CpuLoadWorker, NULL, 0, NULL);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_loadStop) SetEvent(g_loadStop);
    if (g_loadThread) {
        CancelSynchronousIo(g_loadThread);
        if (WaitForSingleObject(g_loadThread, 2000) == WAIT_OBJECT_0) {
            CloseHandle(g_loadStop); g_loadStop = NULL;
        }
        CloseHandle(g_loadThread);
    } else if (g_loadStop) CloseHandle(g_loadStop);
    (void)RestoreSystemCursors();
    CloseHandle(singleMutex);
    return (int)msg.wParam;
}
