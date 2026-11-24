/* SPDX-License-Identifier: MIT
 * Win32 controls with fake driver, cursor and notification-area APIs.
 * Uses a separate registry key; does not change hardware or system cursors.
 */
#define UNICODE 1
#define _UNICODE 1
#define WIN32_LEAN_AND_MEAN
#define OEMRESOURCE
#include <windows.h>
#include <shellapi.h>
#include <assert.h>
#include <stdio.h>

static BOOL restore_ok;
static unsigned adds, modifies;
static BOOL WINAPI fake_spi(UINT action, UINT param, PVOID data, UINT flags)
{
    (void)param; (void)data; (void)flags;
    assert(action == SPI_SETCURSORS);
    if (!restore_ok) SetLastError(ERROR_GEN_FAILURE);
    return restore_ok;
}
static BOOL WINAPI fake_notify(DWORD message, PNOTIFYICONDATAW data)
{
    assert(data->hWnd != NULL);
    if (message == NIM_ADD) ++adds;
    else if (message == NIM_MODIFY) ++modifies;
    return TRUE;
}
static HANDLE WINAPI fake_open(LPCWSTR name, DWORD access, DWORD sharing,
    LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD flags, HANDLE templ)
{
    (void)name; (void)access; (void)sharing; (void)security;
    (void)disposition; (void)flags; (void)templ;
    SetLastError(ERROR_NOT_READY);
    return INVALID_HANDLE_VALUE;
}
static HWND WINAPI fake_find(LPCWSTR class_name, LPCWSTR title)
{
    (void)class_name; (void)title;
    return NULL;
}

#define REG_SETTINGS_KEY L"Software\\SwitchWOA\\SwitchPowerTrayContractTests"
#define REG_RUN_VALUE L"SwitchPowerTrayContractTests"
#define SystemParametersInfoW fake_spi
#define Shell_NotifyIconW fake_notify
#define CreateFileW fake_open
#define FindWindowW fake_find
#include "../SwitchPowerTray.c"

int wmain(void)
{
    SW_CPU_TIMES before[4] = {0}, after[4] = {0};
    unsigned int busy = 0;
    for (unsigned i = 0; i < 4; ++i) {
        after[i].KernelTime.QuadPart = 1000;
        after[i].IdleTime.QuadPart = i == 0 ? 0 : 1000;
    }
    assert(CpuBusyPermille(before, after, 4, &busy) && busy == 1000);
    after[0].IdleTime.QuadPart = 1000;
    assert(CpuBusyPermille(before, after, 4, &busy) && busy == 0);
    after[0].KernelTime.QuadPart = -1;
    assert(!CpuBusyPermille(before, after, 4, &busy));
    assert(sizeof(SWFAN_POWER_REQUEST) == 8 && SwFanPowerModeDefaultFixed == 7);
    /* A stale KMD sample must not be displayed as the current GPU rate. */
    SWFAN_POWER_STATUS sample = {0};
    sample.Version = SWFAN_POWER_STATUS_VERSION;
    sample.GpuReportedKhz = 460800;
    sample.GpuSampleAgeMs = 2000;
    sample.Flags = SWFAN_POWER_FLAG_LIVE_GPU_COUNTER;
    assert(HasFreshGpu(&sample));
    sample.GpuSampleAgeMs = 2001;
    assert(!HasFreshGpu(&sample));
    sample.GpuSampleAgeMs = 0;
    sample.Flags = 0;
    assert(!HasFreshGpu(&sample));

    assert(WriteRegDword(REG_SETTINGS_KEY, REG_HIDDEN_VALUE, 1u) == ERROR_SUCCESS);
    g_app.CursorHidden = TRUE;
    restore_ok = FALSE;
    assert(!RestoreSystemCursors());
    assert(g_app.CursorHidden && ReadRegDword(REG_SETTINGS_KEY, REG_HIDDEN_VALUE, 0u) == 1u);
    restore_ok = TRUE;
    assert(RestoreSystemCursors());
    assert(!g_app.CursorHidden && ReadRegDword(REG_SETTINGS_KEY, REG_HIDDEN_VALUE, 1u) == 0u);

    /* A successful cursor reset cannot conceal unavailable hardware drivers. */
    assert(ResetAllToDefaults(NULL, TRUE) == ERROR_NOT_READY);
    assert(RunUninstallOrRestore(FALSE, TRUE) == ERROR_NOT_READY);

    /* Login can precede KMD activation. Do not consume the one-shot default
     * until the provider is ready, nor overwrite a manually chosen mode later. */
    g_startupProfilePending = TRUE;
    g_app.HavePower = FALSE;
    ApplyStartupProfile(NULL);
    assert(g_startupProfilePending);
    g_app.HavePower = TRUE;
    g_app.Power.IsLite = g_app.Power.CpuDfllAvailable = 1;
    g_app.Power.Flags = 0;
    ApplyStartupProfile(NULL);
    assert(g_startupProfilePending);
    g_app.Power.Flags = SWFAN_POWER_FLAG_LIVE_GPU_COUNTER;
    g_app.Power.TempValid=1;
    g_app.Power.PowerMode = SwFanPowerModeHandheld;
    ApplyStartupProfile(NULL);
    assert(!g_startupProfilePending);
    /* A saved non-default selection survives logon. Fan intent is separate
     * and may be restored before the GPU provider becomes available. */
    assert(WriteRegDword(REG_SETTINGS_KEY,REG_POWER_MODE_VALUE,SwFanPowerModeOcGpu)==ERROR_SUCCESS);
    g_app.Power.PowerMode=SwFanPowerModeOcGpu;
    g_startupProfilePending=TRUE;
    ApplyStartupProfile(NULL);
    assert(!g_startupProfilePending &&
        ReadRegDword(REG_SETTINGS_KEY,REG_POWER_MODE_VALUE,999)==SwFanPowerModeOcGpu);
    assert(WriteRegDword(REG_SETTINGS_KEY,REG_FAN_MODE_VALUE,SwFanProfileCool)==ERROR_SUCCESS);
    g_app.HaveFan=TRUE;g_app.Fan.Mode=SwFanProfileCool;
    g_app.Fan.Flags=SWFAN_PROFILE_VALID;g_app.Fan.SampleAgeMs=0;
    g_startupFanPending=TRUE;g_app.Power.Flags=0;
    ApplyStartupProfile(NULL);
    assert(!g_startupFanPending &&
        ReadRegDword(REG_SETTINGS_KEY,REG_POWER_MODE_VALUE,999)==SwFanPowerModeOcGpu);
    assert(SetFanMode(SwFanProfileConsole)==ERROR_NOT_READY);
    assert(ReadRegDword(REG_SETTINGS_KEY,REG_FAN_MODE_VALUE,999)==SwFanProfileCool);
    g_app.HaveFan=FALSE;
    g_app.HavePower = FALSE;

    /* Recreate the notification icon after Explorer broadcasts TaskbarCreated. */
    g_taskbarCreated = WM_APP + 73;
    g_app.Nid.cbSize = sizeof(g_app.Nid);
    g_app.Nid.hWnd = (HWND)(UINT_PTR)1;
    g_app.IconAdded = TRUE;
    adds = modifies = 0;
    assert(TrayWndProc(g_app.Nid.hWnd, g_taskbarCreated, 0, 0) == 0);
    assert(g_app.IconAdded && adds == 1 && modifies == 0);
    UpdateTrayVisuals();
    assert(adds == 1 && modifies == 1);

    if (g_app.Icon != NULL) DestroyIcon(g_app.Icon);
    assert(RegDeleteKeyW(HKEY_CURRENT_USER, REG_SETTINGS_KEY) == ERROR_SUCCESS);
    puts("PASS: failed cursor restore retains recovery state; driver failures propagate; Explorer restart recreates tray icon (API controls only)");
    return 0;
}
