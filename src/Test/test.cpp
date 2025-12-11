#include "pch.h"
#include <windows.h>
#include <string>
#include <cmath>
#include <chrono>

#include "Rendeructor.h"
#pragma comment(lib, "Rendeructor.lib")

// Глобальные переменные
Rendeructor* g_RenderBackend = nullptr;
bool g_IsRunning = true;

// Простейшая процедура обработки сообщений окна
LRESULT CALLBACK WndProc(HWND hwnd, UINT umessage, WPARAM wparam, LPARAM lparam) {
    switch (umessage) {
    case WM_CLOSE:
    case WM_DESTROY:
        g_IsRunning = false;
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        // Если бекенд инициализирован, обновляем размер
        if (g_RenderBackend && g_RenderBackend->GetBackendAPI()) {
            RECT rect;
            GetClientRect(hwnd, &rect);
            g_RenderBackend->GetBackendAPI()->Resize(rect.right - rect.left, rect.bottom - rect.top);
        }
        return 0;
    }
    return DefWindowProc(hwnd, umessage, wparam, lparam);
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    // 1. Создаем окно Windows (Client Side)
    const int W = 1280;
    const int H = 720;
    const wchar_t* className = L"RendeructorTestClass";

    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, className, NULL };
    RegisterClassEx(&wc);

    HWND hwnd = CreateWindow(className, L"Rendeructor Test App",
        WS_OVERLAPPEDWINDOW, 100, 100, W, H, NULL, NULL, wc.hInstance, NULL);

    if (!hwnd) return 1;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    // 2. Инициализация Rendeructor
    g_RenderBackend = new Rendeructor();

    BackendConfig config;
    config.Width = W;
    config.Height = H;
    config.ScreenMode = ScreenMode::Windowed;
    config.API = RenderAPI::DirectX11;
    config.WindowHandle = hwnd; // Передаем дескриптор окна

    if (!g_RenderBackend->Create(config)) {
        MessageBox(NULL, L"Failed to initialize Rendeructor Backend!", L"Error", MB_ICONERROR);
        return -1;
    }

    // 3. Создаем ресурсы
    // Создадим текстуру, в которую теоретически можно рендерить (для теста API)
    Texture offscreenTex;
    offscreenTex.create(W, H, TextureFormat::RGBA16F);

    // Семплер
    Sampler linearSampler;
    linearSampler.create("Linear");

    // 4. Настраиваем шейдерный проход
    ShaderPass simplePass;
    simplePass.VertexShaderPath = "Shader.hlsl";
    simplePass.VertexShaderEntryPoint = "VSMain";
    simplePass.PixelShaderPath = "Shader.hlsl";
    simplePass.PixelShaderEntryPoint = "PSMain";

    // Привязываем ресурсы (даже если шейдер их пока не использует, тестируем API)
    simplePass.AddTexture("tex_Input", offscreenTex);
    simplePass.AddSampler("smp_Linear", linearSampler);

    g_RenderBackend->CompilePass(simplePass);

    // 5. Главный цикл
    auto startTime = std::chrono::high_resolution_clock::now();

    MSG msg = { 0 };
    while (g_IsRunning) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) g_IsRunning = false;
        }
        else {
            // --- Рендеринг ---

            // Считаем время
            auto currentTime = std::chrono::high_resolution_clock::now();
            float timeSeconds = std::chrono::duration<float>(currentTime - startTime).count();

            // Подготавливаем проход
            g_RenderBackend->SetShaderPass(simplePass);

            // Устанавливаем константы (Uniforms)
            // Имена должны совпадать с переменными в cbuffer шейдера
            g_RenderBackend->SetConstant("u_Time", timeSeconds);

            // Анимация цвета
            Math::float4 colorVal;
            colorVal.x = (sin(timeSeconds) * 0.5f) + 0.5f;
            colorVal.y = (cos(timeSeconds * 1.3f) * 0.5f) + 0.5f;
            colorVal.z = 0.5f;
            colorVal.w = 1.0f;
            g_RenderBackend->SetConstant("u_Color", colorVal);

            // Вызов отрисовки (target = default/screen)
            g_RenderBackend->RenderViewportSurface();

            g_RenderBackend->Present();
        }
    }

    // 6. Очистка
    g_RenderBackend->Destroy();
    delete g_RenderBackend;
    UnregisterClass(className, wc.hInstance);

    return 0;
}
