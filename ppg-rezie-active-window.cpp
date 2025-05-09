// ==WindhawkMod==
// @id              ppg-resize-active-window
// @name            PPG - Resize Active Window
// @description     Resize the currently selected window to 1440x900 desktop breakpoint
// @version         1
// @author          Pepe Gazzo
// @include         explorer.exe
// @compilerOptions -luser32 -lpsapi
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# PPG - Resize Active Window

This mod resizes the currently active window to a fixed size of 1440×900 when a hotkey is pressed.

It's designed for users who want consistent window sizing across workspaces in multi-monitor setups — especially useful for web designers and developers.

You can customize the hotkey in the mod settings.

*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- Hotkey: Ctrl+Shift+Alt+F5
  $name: "Hotkey combination"
  $description: "The hotkey combination to resize the active window. Format: Modifiers+Key, e.g. Ctrl+Shift+Alt+F5"
  $options:
    - "Ctrl+Shift+Alt+F1": "Ctrl+Shift+Alt+F1"
    - "Ctrl+Shift+Alt+F2": "Ctrl+Shift+Alt+F2"
    - "Ctrl+Shift+Alt+F3": "Ctrl+Shift+Alt+F3"
    - "Ctrl+Shift+Alt+F4": "Ctrl+Shift+Alt+F4"
    - "Ctrl+Shift+Alt+F5": "Ctrl+Shift+Alt+F5"
    - "Ctrl+Shift+Alt+F6": "Ctrl+Shift+Alt+F6"
    - "Ctrl+Shift+Alt+F7": "Ctrl+Shift+Alt+F7"
    - "Ctrl+Shift+Alt+F8": "Ctrl+Shift+Alt+F8"
    - "Ctrl+Shift+Alt+F9": "Ctrl+Shift+Alt+F9"
    - "Ctrl+Shift+Alt+F10": "Ctrl+Shift+Alt+F10"
    - "Ctrl+Shift+Alt+F11": "Ctrl+Shift+Alt+F11"
    - "Ctrl+Shift+Alt+F12": "Ctrl+Shift+Alt+F12"
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <psapi.h>
#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <cwctype>

#define HOTKEY_ID  1

static std::thread       g_hotkeyThread;
static std::atomic<HWND> g_msgWindow{ nullptr };
static std::atomic<bool> g_running{ false };

// Custom hotkey storage (filled in Wh_ModInit)
static UINT g_hotkeyModifiers = MOD_CONTROL | MOD_SHIFT | MOD_ALT;
static UINT g_hotkeyVk        = VK_F5;

// Split a L"Mod1+Mod2+Key" string into modifiers & virtual-key
bool ParseHotkey(const std::wstring& hotkeyStr, UINT& modifiers, UINT& vk) {
    modifiers = 0;
    vk = 0;
    std::vector<std::wstring> tokens;
    size_t start = 0;
    while (true) {
        auto pos = hotkeyStr.find(L'+', start);
        if (pos == std::wstring::npos) {
            tokens.push_back(hotkeyStr.substr(start));
            break;
        }
        tokens.push_back(hotkeyStr.substr(start, pos - start));
        start = pos + 1;
    }
    for (size_t i = 0; i < tokens.size(); ++i) {
        std::wstring tok = tokens[i];
        for (auto& c : tok) c = towupper(c);
        if (i + 1 < tokens.size()) {
            if (tok == L"SHIFT")    modifiers |= MOD_SHIFT;
            else if (tok == L"CTRL" || tok == L"CONTROL") modifiers |= MOD_CONTROL;
            else if (tok == L"ALT")  modifiers |= MOD_ALT;
            else if (tok == L"WIN" || tok == L"WINDOWS") modifiers |= MOD_WIN;
        } else {
            if (tok.size() == 1) {
                vk = (UINT)tok[0];
            } else if (tok[0] == L'F' && tok.size() > 1) {
                int fn = _wtoi(tok.substr(1).c_str());
                if (fn >= 1 && fn <= 24) vk = VK_F1 + fn - 1;
            } else if (tok == L"LEFT")   vk = VK_LEFT;
            else if (tok == L"RIGHT")    vk = VK_RIGHT;
            else if (tok == L"UP")       vk = VK_UP;
            else if (tok == L"DOWN")     vk = VK_DOWN;
            else if (tok == L"TAB")      vk = VK_TAB;
            else if (tok == L"ESC" || tok == L"ESCAPE") vk = VK_ESCAPE;
            else if (tok == L"SPACE")    vk = VK_SPACE;
        }
    }
    return vk != 0;
}

void ResizeActiveWindow();
void HotkeyThreadProc();

// — Windhawk entry/exit —

BOOL Wh_ModInit() {
    Wh_Log(L"[resize-active-window] initializing...");
    // Load user-configured hotkey
    PCWSTR setting = Wh_GetStringSetting(L"Hotkey");
    if (setting) {
        std::wstring s = setting;
        Wh_FreeStringSetting(setting);
        UINT mods = 0, vk = 0;
        if (ParseHotkey(s, mods, vk)) {
            g_hotkeyModifiers = mods;
            g_hotkeyVk        = vk;
            Wh_Log(L"[resize-active-window] using hotkey: %s", s.c_str());
        } else {
            Wh_Log(L"[resize-active-window] failed to parse '%s', using default Ctrl+Shift+Alt+F5", s.c_str());
        }
    }
    g_running = true;
    g_hotkeyThread = std::thread(HotkeyThreadProc);
    return TRUE;
}

void Wh_ModUninit() {
    Wh_Log(L"[resize-active-window] uninitializing...");
    g_running = false;
    HWND hwnd = g_msgWindow.load();
    if (hwnd) PostMessage(hwnd, WM_QUIT, 0, 0);
    if (g_hotkeyThread.joinable()) g_hotkeyThread.join();
    Wh_Log(L"[resize-active-window] shutdown complete");
}

BOOL Wh_ModSettingsChanged(BOOL* bReload) {
    *bReload = TRUE;
    return TRUE;
}

// — Hotkey thread: message-only window + hotkey + loop —

void HotkeyThreadProc() {
    WNDCLASS wc = {};
    wc.lpfnWndProc   = DefWindowProc;
    wc.hInstance     = GetModuleHandle(NULL);
    wc.lpszClassName = L"Windhawk_ResizeActiveWindowMsgWnd";
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0, wc.lpszClassName, L"", 0,0,0,0,0,
        HWND_MESSAGE, NULL, wc.hInstance, NULL);

    if (!hwnd) {
        Wh_Log(L"[resize-active-window] failed to create message window: %u", GetLastError());
        return;
    }
    g_msgWindow = hwnd;

    if (!RegisterHotKey(hwnd, HOTKEY_ID, g_hotkeyModifiers, g_hotkeyVk))
        Wh_Log(L"[resize-active-window] failed to register hotkey: %u", GetLastError());
    else
        Wh_Log(L"[resize-active-window] Hotkey registered (mod=0x%X vk=0x%X)", g_hotkeyModifiers, g_hotkeyVk);

    MSG msg;
    while (g_running && GetMessage(&msg, hwnd, 0, 0) > 0) {
        if (msg.message == WM_HOTKEY && msg.wParam == HOTKEY_ID) {
            Wh_Log(L"[resize-active-window] hotkey pressed → resizing active window");
            ResizeActiveWindow();
        }
    }

    UnregisterHotKey(hwnd, HOTKEY_ID);
    DestroyWindow(hwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);
    g_msgWindow = nullptr;
    Wh_Log(L"[resize-active-window] message thread exiting");
}

// — Helper to resize the active window —

void ResizeActiveWindow() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid) {
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (hProc) {
            wchar_t fullPath[MAX_PATH] = {};
            DWORD size = _countof(fullPath);
            if (QueryFullProcessImageName(hProc, 0, fullPath, &size)) {
                const wchar_t* exe = wcsrchr(fullPath, L'\\');
                exe = exe ? exe + 1 : fullPath;
                // Skip Start menu, Shell Experience Host, Search UI and Search Host
                if (_wcsicmp(exe, L"ShellExperienceHost.exe") == 0 ||
                    _wcsicmp(exe, L"StartMenuExperienceHost.exe") == 0 ||
                    _wcsicmp(exe, L"SearchApp.exe") == 0 ||
                    _wcsicmp(exe, L"SearchUI.exe") == 0 ||
                    _wcsicmp(exe, L"SearchHost.exe") == 0 ||
                    _wcsicmp(exe, L"Rainlendar2.exe") == 0) {
                    CloseHandle(hProc);
                    return;
                }
            }
            CloseHandle(hProc);
        }
    }
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return;

    // Restore the window from maximized state
    ShowWindow(hwnd, SW_RESTORE);

    // Resize the window
    SetWindowPos(hwnd, nullptr, 0, 0, 2880, 1800, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}
