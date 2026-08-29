#include "SettingsWindowInternal.h"

#include <commctrl.h>
#include <windowsx.h>

namespace clawhud::settings::internal
{
LRESULT CALLBACK ForwardPanelNotifications(HWND window, UINT message, WPARAM wParam,
    LPARAM lParam, UINT_PTR subclassId, DWORD_PTR)
{
    if (message == WM_COMMAND || message == WM_HSCROLL)
        return SendMessageW(GetParent(window), message, wParam, lParam);
    if (message == WM_MOUSEWHEEL)
        return SendMessageW(GetParent(window), message, wParam, lParam);
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(window, ForwardPanelNotifications, subclassId);
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK ForwardPanGesture(HWND window, UINT message, WPARAM wParam,
    LPARAM lParam, UINT_PTR subclassId, DWORD_PTR)
{
    if (message == WM_GESTURE)
    {
        GESTUREINFO info{};
        info.cbSize = sizeof(info);
        const HGESTUREINFO gesture = reinterpret_cast<HGESTUREINFO>(lParam);
        if (!GetGestureInfo(gesture, &info) || info.dwID != GID_PAN)
            return DefSubclassProc(window, message, wParam, lParam);

        if (HWND root = GetAncestor(window, GA_ROOT))
            return SendMessageW(root, message, wParam, lParam);
    }
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(window, ForwardPanGesture, subclassId);
    return DefSubclassProc(window, message, wParam, lParam);
}

void ConfigureVerticalPan(HWND window)
{
    if (!window)
        return;

    GESTURECONFIG config{};
    config.dwID = GID_PAN;
    config.dwWant = GC_PAN |
        GC_PAN_WITH_SINGLE_FINGER_VERTICALLY |
        GC_PAN_WITH_INERTIA;
    config.dwBlock = GC_PAN_WITH_SINGLE_FINGER_HORIZONTALLY;
    SetGestureConfig(window, 0, 1, &config, sizeof(config));
}

void MoveControl(HWND parent, int id, int x, int y, int width, int height)
{
    if (HWND control = GetDlgItem(parent, id))
        MoveWindow(control, x, y, width, height, TRUE);
}

void EnableMouseWheelForwarding(HWND control)
{
    if (control)
        SetWindowSubclass(control, ForwardPanelNotifications, 4, 0);
}

void EnableStaticPanForwarding(HWND panel)
{
    if (!panel)
        return;

    ConfigureVerticalPan(panel);
    SetWindowSubclass(panel, ForwardPanGesture, 5, 0);
    EnumChildWindows(panel, [](HWND child, LPARAM) -> BOOL
    {
        wchar_t className[32]{};
        GetClassNameW(child, className, static_cast<int>(sizeof(className) / sizeof(className[0])));
        if (_wcsicmp(className, L"Static") == 0)
        {
            ConfigureVerticalPan(child);
            SetWindowSubclass(child, ForwardPanGesture, 5, 0);
        }
        return TRUE;
    }, 0);
}
}
