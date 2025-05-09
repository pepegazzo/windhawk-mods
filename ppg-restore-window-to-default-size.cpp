// ==WindhawkMod==
// @id              window-restore-default-size
// @name            Default window size on restore
// @description     Forces any top-level window restored from maximised or minimised state to resize to 1280×800 and centre on the monitor.
// @version         2.3
// @author          Pepe Gazzo
// @include         *
// ==/WindhawkMod==

#include <windows.h>
#include <unordered_map>
#include <iostream>
#include <thread>
#include <chrono>

//---------------------------------------------------------------------
// CONFIG – change these to taste
//---------------------------------------------------------------------
static constexpr int kWidth  = 1280; // px
static constexpr int kHeight =  800; // px

//---------------------------------------------------------------------
// Data structures
//---------------------------------------------------------------------
struct WindowData {
    WNDPROC origProc;          // Original WndProc to forward messages
    UINT    prevSize = SIZE_RESTORED;
};
static std::unordered_map<HWND, WindowData> gData;

//---------------------------------------------------------------------
// Utility: centre a rect on monitor work-area
//---------------------------------------------------------------------
static void Centre(HWND hwnd)
{
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    if (!GetMonitorInfo(mon, &mi)) return;

    RECT rc = mi.rcWork;
    int x = rc.left + ((rc.right  - rc.left) - kWidth)  / 2;
    int y = rc.top  + ((rc.bottom - rc.top ) - kHeight) / 2;

    SetWindowPos(hwnd, nullptr, x, y, kWidth, kHeight,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
}

//---------------------------------------------------------------------
// Our subclass procedure
//---------------------------------------------------------------------
static LRESULT CALLBACK HookProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    auto it = gData.find(hwnd);
    if (it == gData.end()) {
        return DefWindowProc(hwnd, msg, wp, lp);
    }

    WindowData &wd = it->second;

    if (msg == WM_SIZE) {
        UINT type = static_cast<UINT>(wp);
        if (type == SIZE_RESTORED &&
            (wd.prevSize == SIZE_MAXIMIZED || wd.prevSize == SIZE_MINIMIZED)) {
            Centre(hwnd);
        }
        wd.prevSize = type;
    }
    else if (msg == WM_NCDESTROY) {
        // Restore original proc and clean up map
        SetWindowLongPtr(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(wd.origProc));
        gData.erase(it);
    }

    return CallWindowProc(wd.origProc, hwnd, msg, wp, lp);
}

//---------------------------------------------------------------------
// Attach to a qualifying window
//---------------------------------------------------------------------
static void Attach(HWND hwnd)
{
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd)) return;
    if (GetAncestor(hwnd, GA_ROOT) != hwnd)       return;

    // Check if already attached
    if (gData.find(hwnd) != gData.end()) return;

    WindowData wd{};
    wd.origProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtr(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HookProc)));
    gData[hwnd] = wd;

    std::cout << "Attached to window: " << hwnd << std::endl;
}

//---------------------------------------------------------------------
// Enum helper: attach to existing windows at load time
//---------------------------------------------------------------------
static BOOL CALLBACK EnumProc(HWND hwnd, LPARAM) {
    Attach(hwnd);
    return TRUE;
}

//---------------------------------------------------------------------
// Poll for new windows
//---------------------------------------------------------------------
static void PollForWindows()
{
    while (true) {
        EnumWindows(EnumProc, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // Poll every second
    }
}

//---------------------------------------------------------------------
// Windhawk entry points
//---------------------------------------------------------------------
BOOL Wh_ModInit()
{
    EnumWindows(EnumProc, 0);
    std::thread(PollForWindows).detach(); // Start polling in a separate thread
    std::cout << "Mod initialized and polling started." << std::endl;
    return TRUE;
}

void Wh_ModUninit()
{
    for (auto &pair : gData) {
        HWND hwnd = pair.first;
        if (IsWindow(hwnd))
            SetWindowLongPtr(hwnd, GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(pair.second.origProc));
    }
    gData.clear();
    std::cout << "Mod uninitialized." << std::endl;
}
