/* SPDX-License-Identifier: MIT
 * User-mode load hint. Frequency/voltage/thermal ownership stays in SwitchFan.
 * SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION: Microsoft's documented layout;
 * KernelTime includes IdleTime. Use the busiest core, not package average.
 */
#ifndef SWITCH_TRAY_CPU_LOAD_H
#define SWITCH_TRAY_CPU_LOAD_H
typedef struct {
    LARGE_INTEGER IdleTime, KernelTime, UserTime, Reserved1[2];
    ULONG Reserved2;
} SW_CPU_TIMES;
typedef LONG (WINAPI *SW_QUERY_SYSTEM)(ULONG, PVOID, ULONG, PULONG);

static BOOL CpuBusyPermille(const SW_CPU_TIMES *before, const SW_CPU_TIMES *after,
                           ULONG count, unsigned int *out)
{
    unsigned int peak = 0;
    for (ULONG i = 0; i < count; ++i) {
        LONGLONG idle = after[i].IdleTime.QuadPart - before[i].IdleTime.QuadPart;
        LONGLONG kernel = after[i].KernelTime.QuadPart - before[i].KernelTime.QuadPart;
        LONGLONG user = after[i].UserTime.QuadPart - before[i].UserTime.QuadPart;
        if (idle < 0 || kernel < 0 || user < 0 || kernel + user <= 0 || idle > kernel + user)
            return FALSE;
        unsigned int busy = (unsigned int)(((kernel + user - idle) * 1000) / (kernel + user));
        if (busy > peak) peak = busy;
    }
    *out = peak;
    return count != 0;
}

static DWORD WINAPI CpuLoadWorker(void *unused)
{
    SW_CPU_TIMES previous[64], current[64];
    ULONG previousCount = 0;
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    SW_QUERY_SYSTEM query = ntdll ? (SW_QUERY_SYSTEM)(void *)GetProcAddress(ntdll, "NtQuerySystemInformation") : NULL;
    (void)unused;
    if (!query) return ERROR_PROC_NOT_FOUND;
    while (WaitForSingleObject(g_loadStop, 250) == WAIT_TIMEOUT) {
        ULONG bytes = 0, count;
        unsigned int busy;
        DWORD err = 0;
        if (query(8u, current, sizeof(current), &bytes) < 0 ||
            bytes == 0 || bytes > sizeof(current) || bytes % sizeof(current[0])) {
            previousCount = 0; continue;
        }
        count = bytes / sizeof(current[0]);
        if (previousCount == count && CpuBusyPermille(previous, current, count, &busy) &&
            InterlockedCompareExchange(&g_loadEnabled, 0, 0)) {
            SWFAN_CPU_LOAD hint = {SWFAN_POWER_STATUS_VERSION, busy};
            if (!SendDeviceIoctl(L"\\\\.\\SwitchBacklight", IOCTL_SWFAN_CPU_LOAD,
                                 &hint, sizeof(hint), NULL, 0, &err))
                InterlockedExchange(&g_loadEnabled, 0);
        }
        memcpy(previous, current, bytes);
        previousCount = count;
    }
    return 0;
}
#endif
