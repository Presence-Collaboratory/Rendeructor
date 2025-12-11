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

// Оконная процедура
LRESULT CALLBACK WndProc(HWND hwnd, UINT umessage, WPARAM wparam, LPARAM lparam) {
    if (umessage == WM_CLOSE || umessage == WM_DESTROY) {
        g_IsRunning = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, umessage, wparam, lparam);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    // --- 1. Инициализация окна ---
    const int W = 1280;
    const int H = 720;
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, L"RendeructorTest", NULL };
    RegisterClassEx(&wc);
    HWND hwnd = CreateWindow(L"RendeructorTest", L"Rendeructor 3D Cube", WS_OVERLAPPEDWINDOW, 100, 100, W, H, NULL, NULL, wc.hInstance, NULL);
    ShowWindow(hwnd, SW_SHOW);

    // --- 2. Инициализация Rendeructor ---
    g_RenderBackend = new Rendeructor();
    BackendConfig config;
    config.Width = W;
    config.Height = H;
    config.WindowHandle = hwnd;
    config.API = RenderAPI::DirectX11;

    if (!g_RenderBackend->Create(config)) {
        MessageBox(NULL, L"Failed to init backend", L"Error", MB_OK);
        return -1;
    }

    // --- 3. Создание Ресурсов (Текстура и Семплер) ---
    Texture cubeTexture;
    // Пытаемся загрузить картинку, если нет - создаем фиолетовую 1x1
    if (!cubeTexture.LoadFromDisk("test.png")) {
        cubeTexture.Create(1, 1, TextureFormat::RGBA8);
    }

    Sampler sampler;
    sampler.Create("Linear");

    // --- 4. Создание Геометрии (Меш Куба) вручную ---
    // Нам нужно 24 вершины (4 на каждую грань), чтобы текстуры накладывались правильно
    std::vector<Vertex> vertices = {
        // Front Face (Z = -1)
        { -1.0f, -1.0f, -1.0f,  0.0f, 1.0f }, // Bottom-Left
        { -1.0f,  1.0f, -1.0f,  0.0f, 0.0f }, // Top-Left
        {  1.0f,  1.0f, -1.0f,  1.0f, 0.0f }, // Top-Right
        {  1.0f, -1.0f, -1.0f,  1.0f, 1.0f }, // Bottom-Right

        // Back Face (Z = +1)
        {  1.0f, -1.0f,  1.0f,  0.0f, 1.0f }, // Bottom-Left
        {  1.0f,  1.0f,  1.0f,  0.0f, 0.0f }, // Top-Left
        { -1.0f,  1.0f,  1.0f,  1.0f, 0.0f }, // Top-Right
        { -1.0f, -1.0f,  1.0f,  1.0f, 1.0f }, // Bottom-Right

        // Top Face (Y = +1)
        { -1.0f,  1.0f, -1.0f,  0.0f, 1.0f },
        { -1.0f,  1.0f,  1.0f,  0.0f, 0.0f },
        {  1.0f,  1.0f,  1.0f,  1.0f, 0.0f },
        {  1.0f,  1.0f, -1.0f,  1.0f, 1.0f },

        // Bottom Face (Y = -1)
        { -1.0f, -1.0f,  1.0f,  0.0f, 1.0f },
        { -1.0f, -1.0f, -1.0f,  0.0f, 0.0f },
        {  1.0f, -1.0f, -1.0f,  1.0f, 0.0f },
        {  1.0f, -1.0f,  1.0f,  1.0f, 1.0f },

        // Left Face (X = -1)
        { -1.0f, -1.0f,  1.0f,  0.0f, 1.0f },
        { -1.0f,  1.0f,  1.0f,  0.0f, 0.0f },
        { -1.0f,  1.0f, -1.0f,  1.0f, 0.0f },
        { -1.0f, -1.0f, -1.0f,  1.0f, 1.0f },

        // Right Face (X = +1)
        {  1.0f, -1.0f, -1.0f,  0.0f, 1.0f },
        {  1.0f,  1.0f, -1.0f,  0.0f, 0.0f },
        {  1.0f,  1.0f,  1.0f,  1.0f, 0.0f },
        {  1.0f, -1.0f,  1.0f,  1.0f, 1.0f },
    };

    std::vector<unsigned int> indices;
    // Генерируем 2 треугольника на каждую из 6 граней
    for (int i = 0; i < 6; ++i) {
        unsigned int offset = i * 4;
        indices.push_back(offset + 0);
        indices.push_back(offset + 1);
        indices.push_back(offset + 2);

        indices.push_back(offset + 2);
        indices.push_back(offset + 3);
        indices.push_back(offset + 0);
    }

    // Создаем объект Mesh и загружаем данные в GPU
    Mesh cubeMesh;
    cubeMesh.Create(vertices, indices);

    // --- 5. Настройка Шейдера ---
    ShaderPass cubePass;
    cubePass.VertexShaderPath = "Shader.hlsl";
    cubePass.VertexShaderEntryPoint = "VSMain";
    cubePass.PixelShaderPath = "Shader.hlsl";
    cubePass.PixelShaderEntryPoint = "PSMain";

    cubePass.AddTexture("tex_Diffuse", cubeTexture);
    cubePass.AddSampler("smp_Main", sampler);

    // Предварительная компиляция
    g_RenderBackend->CompilePass(cubePass);

    // --- 6. Настройка Математики ---
    auto startTime = std::chrono::high_resolution_clock::now();

    Math::float4x4 projection = Math::float4x4::perspective_lh_zo(
        45.0f * Math::Constants::DEG_TO_RAD,
        (float)W / (float)H,
        0.1f,
        100.0f
    );

    Math::float3 eye(0.0f, 2.0f, -5.0f);
    Math::float3 target(0.0f, 0.0f, 0.0f);
    Math::float3 up(0.0f, 1.0f, 0.0f);
    Math::float4x4 view = Math::float4x4::look_at_lh(eye, target, up);

    // --- 7. Главный Цикл ---
    MSG msg = { 0 };
    while (g_IsRunning) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            auto currentTime = std::chrono::high_resolution_clock::now();
            float time = std::chrono::duration<float>(currentTime - startTime).count();

            // 1. Сначала ставим таргет на экран
            g_RenderBackend->RenderPassToScreen();

            // 2. ЧИСТИМ ЭКРАН (Цвет серый, чтобы видеть черный куб, если текстура не загрузится)
            g_RenderBackend->Clear(0.2f, 0.2f, 0.2f, 1.0f);

            // 3. Математика (Row-Major: Model * View * Projection)
            Math::float4x4 model = Math::float4x4::rotation_y(time) * Math::float4x4::rotation_x(time * 0.5f);
            Math::float4x4 mvp = model * view * projection; // Порядок верный для нашей C++ библиотеки

            // 4. Отрисовка
            g_RenderBackend->SetShaderPass(cubePass);
            g_RenderBackend->SetConstant("u_MVP", mvp);
            g_RenderBackend->DrawMesh(cubeMesh);

            g_RenderBackend->Present();
        }
    }

    g_RenderBackend->Destroy();
    delete g_RenderBackend;
    return 0;
}
