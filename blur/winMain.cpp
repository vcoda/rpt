#include <memory>
#include <string>
#include <Windows.h>

namespace
{
bool quit = false;

void onError(const std::string& msg, const char *caption)
{
    MessageBox(NULL,
        msg.c_str(), caption,
        MB_ICONHAND);
}

void showWindow(HWND wnd, DWORD style, LONG width, LONG height)
{
    // Adjust window size according to style
    const HDC hDC = GetDC(wnd);
    RECT rc = {0L, 0L, width, height};
    ReleaseDC(wnd, hDC);
    AdjustWindowRect(&rc, style, FALSE);
    SetWindowPos(wnd, HWND_TOP, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_HIDEWINDOW);

    // Get desktop resolution
    const HWND desktopWnd = GetDesktopWindow();
    RECT desktopRect;
    GetWindowRect(desktopWnd, &desktopRect);

    const int cx = static_cast<int>(rc.right - rc.left);
    const int cy = static_cast<int>(rc.bottom - rc.top);
    int x = 0, y = 0;
    if (cx < desktopRect.right && cy < desktopRect.bottom)
    {   // Place window in the center of the desktop
        x = (desktopRect.right - cx) / 2;
        y = (desktopRect.bottom - cy) / 2;
    }
    SetWindowPos(wnd, HWND_TOP, x, y, cx, cy, SWP_SHOWWINDOW);
}

LRESULT WINAPI wndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_KEYDOWN:
        break;
    case WM_KEYUP:
        break;
    case WM_MOUSEMOVE:
        break;
#ifndef _DEBUG
    case WM_PAINT:
        break;
#endif
    case WM_CLOSE:
        quit = true;
        break;
    case WM_DESTROY:
        return 0;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}
} // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
    constexpr LONG width = 512UL;
    constexpr LONG height = 512UL;
    const char *className = "rpt";

    // Register window class
    const WNDCLASSEX wc = {
        sizeof(WNDCLASSEX), CS_CLASSDC, wndProc, 0, 0, hInstance,
        NULL, LoadCursor(NULL, IDC_ARROW),
        NULL, NULL, className, NULL
    };
    RegisterClassEx(&wc);

    // Create window
    const DWORD style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    HWND wnd = CreateWindow(wc.lpszClassName, "Render Programmer Test", style,
        0, 0, width, height,
        NULL, NULL, wc.hInstance, NULL);

    showWindow(wnd, style, width, height);

    while (!quit)
    {
        MSG msg;
        if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            if (!IsIconic(wnd))
            {
            }
        }
    }

    DestroyWindow(wnd);
    UnregisterClass(className, hInstance);
    return 0;
}