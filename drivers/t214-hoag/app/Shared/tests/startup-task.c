/* SPDX-License-Identifier: MIT
 * Native Windows control: exercise the shared C/C++ task backend on the two
 * installed tasks. Only changes Enabled, immediately restores the initial value.
 */
#include <stdio.h>
#include "../ScheduledStartup.h"
int wmain(void)
{
    const WCHAR *apps[] = {L"SwitchPowerTray", L"Pacecar"};
    for (unsigned i = 0; i < 2; ++i) {
        BOOL before = FALSE, state;
        HRESULT hr = SwitchStartupTask(apps[i], FALSE, &before);
        if (hr != S_OK) { printf("read task failed: %lx\n", (unsigned long)hr); return 1; }
        state = !before;
        hr = SwitchStartupTask(apps[i], TRUE, &state);
        BOOL changed = hr == S_OK && state != before;
        state = before;
        HRESULT restored = SwitchStartupTask(apps[i], TRUE, &state);
        if (!changed || restored != S_OK || state != before) {
            printf("toggle/restore failed: %lx/%lx\n", (unsigned long)hr, (unsigned long)restored);
            return 2;
        }
    }
    puts("PASS: both installed startup tasks toggle and restore through the application backend");
    return 0;
}
