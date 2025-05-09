// ==WindhawkMod==
// @id              ppg-resize-windows
// @name            PPG - Resize All Restored Windows
// @description     Resize all visible, non-minimized, non-maximized windows to a default size.
// @version         1
// @author          Pepe Gazzo
// @include         explorer.exe
// @compilerOptions -luser32 -lpsapi
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# PPG – Resize All Restored Windows

This mod resizes all visible, restored (non-maximized, non-minimized) windows to a fixed size — perfect for cleaning up awkwardly sized windows left open across your desktop.

It’s especially useful when reopening apps that remember weird dimensions, or when you want to quickly bring order to a cluttered multi-window setup.

You can customize the hotkey in the mod settings.

*/
// ==/WindhawkModReadme==


// ==WindhawkModSettings==
/*
- Hotkey: Ctrl+Shift+Alt+F5
  $name: "Hotkey combination"
  $description: "The hotkey combination to resize windows. Format: Modifiers+Key, e.g. Ctrl+Shift+Alt+F5"
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

void ResizeAllWindows();
void HotkeyThreadProc();

// — Windhawk entry/exit —

BOOL Wh_ModInit() {
    Wh_Log(L"[resize-windows] initializing...");
    // Load user-configured hotkey
    PCWSTR setting = Wh_GetStringSetting(L"Hotkey");
    if (setting) {
        std::wstring s = setting;
        Wh_FreeStringSetting(setting);
        UINT mods = 0, vk = 0;
        if (ParseHotkey(s, mods, vk)) {
            g_hotkeyModifiers = mods;
            g_hotkeyVk        = vk;
            Wh_Log(L"[resize-windows] using hotkey: %s", s.c_str());
        } else {
            Wh_Log(L"[resize-windows] failed to parse '%s', using default Ctrl+Shift+Alt+F5", s.c_str());
        }
    }
    g_running = true;
    g_hotkeyThread = std::thread(HotkeyThreadProc);
    return TRUE;
}

void Wh_ModUninit() {
    Wh_Log(L"[resize-windows] uninitializing...");
    g_running = false;
    HWND hwnd = g_msgWindow.load();
    if (hwnd) PostMessage(hwnd, WM_QUIT, 0, 0);
    if (g_hotkeyThread.joinable()) g_hotkeyThread.join();
    Wh_Log(L"[resize-windows] shutdown complete");
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
    wc.lpszClassName = L"Windhawk_ResizeWindowsMsgWnd";
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0, wc.lpszClassName, L"", 0,0,0,0,0,
        HWND_MESSAGE, NULL, wc.hInstance, NULL);

    if (!hwnd) {
        Wh_Log(L"[resize-windows] failed to create message window: %u", GetLastError());
        return;
    }
    g_msgWindow = hwnd;

    if (!RegisterHotKey(hwnd, HOTKEY_ID, g_hotkeyModifiers, g_hotkeyVk))
        Wh_Log(L"[resize-windows] failed to register hotkey: %u", GetLastError());
    else
        Wh_Log(L"[resize-windows] Hotkey registered (mod=0x%X vk=0x%X)", g_hotkeyModifiers, g_hotkeyVk);

    MSG msg;
    while (g_running && GetMessage(&msg, hwnd, 0, 0) > 0) {
        if (msg.message == WM_HOTKEY && msg.wParam == HOTKEY_ID) {
            Wh_Log(L"[resize-windows] hotkey pressed → resizing windows");
            ResizeAllWindows();
        }
    }

    UnregisterHotKey(hwnd, HOTKEY_ID);
    DestroyWindow(hwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);
    g_msgWindow = nullptr;
    Wh_Log(L"[resize-windows] message thread exiting");
}

// — Helpers to enumerate & resize windows —

void ResizeWindow(HWND hwnd) {
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
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd) || IsZoomed(hwnd)) return;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return;

    SetWindowPos(hwnd, nullptr, 0, 0, 1440, 800, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lp) {
    ResizeWindow(hwnd);
    return TRUE;
}

void ResizeAllWindows() {
    EnumWindows(EnumProc, 0);
}
