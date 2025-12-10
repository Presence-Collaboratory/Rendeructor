#include "pch.h"
#include <Windows.h>

HWND g_window = nullptr;
bool g_running = true;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_DESTROY:
        g_running = false;
        PostQuitMessage(0);
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            g_running = false;
            PostQuitMessage(0);
        }
        return 0;

    case WM_SIZE:
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);

    return 0;
}
