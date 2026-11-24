// swagent service host. The swagent service used to run cmd.exe directly, which never talks to the
// service control manager: the SCM waited its full 45 s pipe timeout for it on every boot (event
// 7009) before giving up, while the agent ran anyway. This host reports RUNNING at once, runs
//   cmd.exe /c C:\swagent\boot.cmd
// and reports STOPPED when that script ends. A stop request only reports STOP_PENDING: the script
// (driver installs, settings) is left to finish.
// Build: drivers/nvk container, aarch64-w64-mingw32-clang -O2 -municode swagentsvc.c -o swagentsvc.exe
#include <windows.h>

static SERVICE_STATUS_HANDLE g_handle;
static SERVICE_STATUS g_status;

static void report(DWORD state, DWORD exitCode)
{
    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_status.dwCurrentState = state;
    g_status.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    g_status.dwWin32ExitCode = exitCode;
    g_status.dwWaitHint = 0;
    SetServiceStatus(g_handle, &g_status);
}

static DWORD WINAPI control(DWORD code, DWORD type, LPVOID data, LPVOID context)
{
    (void) type; (void) data; (void) context;
    if (code == SERVICE_CONTROL_STOP || code == SERVICE_CONTROL_SHUTDOWN) {
        report(SERVICE_STOP_PENDING, NO_ERROR);
        return NO_ERROR;
    }
    if (code == SERVICE_CONTROL_INTERROGATE)
        return NO_ERROR;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

static void WINAPI service_main(DWORD argc, LPWSTR *argv)
{
    LPWSTR cmd = GetCommandLineW();
    BOOL quoted = FALSE;
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    DWORD exitCode = NO_ERROR;

    (void) argc; (void) argv;
    g_handle = RegisterServiceCtrlHandlerExW(L"swagent", control, NULL);
    if (!g_handle)
        return;
    report(SERVICE_RUNNING, NO_ERROR);
    // skip our own path (quoted or not) and the spaces after it
    while (*cmd && (quoted || *cmd != L' ')) {
        if (*cmd == L'"')
            quoted = !quoted;
        cmd++;
    }
    while (*cmd == L' ')
        cmd++;
    if (!*cmd) {
        report(SERVICE_STOPPED, ERROR_BAD_ARGUMENTS);
        return;
    }
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, L"C:\\swagent", &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        exitCode = GetLastError();
    }
    report(SERVICE_STOPPED, exitCode);
}

int wmain(void)
{
    SERVICE_TABLE_ENTRYW table[] = { { L"swagent", service_main }, { NULL, NULL } };
    return StartServiceCtrlDispatcherW(table) ? 0 : (int) GetLastError();
}
