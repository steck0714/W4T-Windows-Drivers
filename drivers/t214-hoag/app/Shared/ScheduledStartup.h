/* SPDX-License-Identifier: MIT */
#pragma once
#include <windows.h>
#include <initguid.h>
#include <taskschd.h>
#include <oleauto.h>

#ifdef __cplusplus
#define SW_START_CALL(p, method, ...) (p)->method(__VA_ARGS__)
#define SW_START_GUID(g) (g)
#else
#define SW_START_CALL(p, method, ...) (p)->lpVtbl->method(p, ##__VA_ARGS__)
#define SW_START_GUID(g) (&(g))
#endif

/* S_FALSE means this installation still uses the application's Run-key backend.
 * A registered task that cannot be accessed is an error, never a Run fallback.
 * The installer registers an interactive task owned by this user and removes
 * the old Run value. Both application startup checkboxes then control that task.
 */
static inline HRESULT SwitchStartupTask(LPCWSTR app, BOOL write, BOOL *enabled)
{
    WCHAR path[512];
    DWORD bytes = sizeof(path);
    LSTATUS st = RegGetValueW(HKEY_CURRENT_USER, L"Software\\SwitchWOA\\StartupTasks",
                             app, RRF_RT_REG_SZ, NULL, path, &bytes);
    ITaskService *service = NULL;
    ITaskFolder *root = NULL;
    IRegisteredTask *task = NULL;
    BSTR rootPath = NULL, taskPath = NULL;
    VARIANT empty;
    VARIANT_BOOL value = VARIANT_FALSE;
    HRESULT hr, init;
    if (st == ERROR_FILE_NOT_FOUND || st == ERROR_PATH_NOT_FOUND) return S_FALSE;
    if (st != ERROR_SUCCESS) return HRESULT_FROM_WIN32(st);
    if (bytes <= sizeof(WCHAR) || path[0] != L'\\') return E_INVALIDARG;
    init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) return init;
    VariantInit(&empty);
    hr = CoCreateInstance(SW_START_GUID(CLSID_TaskScheduler), NULL,
                          CLSCTX_INPROC_SERVER, SW_START_GUID(IID_ITaskService), (void **)&service);
    if (FAILED(hr)) goto done;
    hr = SW_START_CALL(service, Connect, empty, empty, empty, empty);
    if (FAILED(hr)) goto done;
    rootPath = SysAllocString(L"\\");
    taskPath = SysAllocString(path);
    if (!rootPath || !taskPath) { hr = E_OUTOFMEMORY; goto done; }
    hr = SW_START_CALL(service, GetFolder, rootPath, &root);
    if (FAILED(hr)) goto done;
    hr = SW_START_CALL(root, GetTask, taskPath, &task);
    if (FAILED(hr)) goto done;
    if (write) {
        hr = SW_START_CALL(task, put_Enabled, *enabled ? VARIANT_TRUE : VARIANT_FALSE);
        if (FAILED(hr)) goto done;
    }
    hr = SW_START_CALL(task, get_Enabled, &value);
    if (SUCCEEDED(hr)) *enabled = value != VARIANT_FALSE;
done:
    if (task) SW_START_CALL(task, Release);
    if (root) SW_START_CALL(root, Release);
    if (service) SW_START_CALL(service, Release);
    SysFreeString(rootPath);
    SysFreeString(taskPath);
    if (SUCCEEDED(init)) CoUninitialize();
    return hr;
}
#undef SW_START_CALL
#undef SW_START_GUID
