#include "pch.h"
#include <windows.h>
#include <string>
#include <cmath>
#include <chrono>
#include <vector>

#include "Rendeructor.h"
#pragma comment(lib, "Rendeructor.lib")

// Глобальные переменные
Rendeructor* g_RenderBackend = nullptr;
bool g_IsRunning = true;

std::vector<float> Generate3DGradient(int w, int h, int d) {
    std::vector<float> data(w * h * d * 4);
    for (int z = 0; z < d; ++z) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int idx = (z * w * h + y * w + x) * 4;
                data[idx + 0] = (float)x / (float)w; // R
                data[idx + 1] = (float)y / (float)h; // G
                data[idx + 2] = (float)z / (float)d; // B
                data[idx + 3] = 1.0f;                // A
            }
        }
    }
    return data;
}

// Создаем простой куб
std::vector<Vertex> CreateCubeVertices() {
    return {
        // Front face
        {-0.5f, -0.5f, -0.5f, 0.0f, 1.0f},
        {-0.5f,  0.5f, -0.5f, 0.0f, 0.0f},
        { 0.5f, -0.5f, -0.5f, 1.0f, 1.0f},
        { 0.5f,  0.5f, -0.5f, 1.0f, 0.0f},

        // Back face  
        {-0.5f, -0.5f,  0.5f, 1.0f, 1.0f},
        {-0.5f,  0.5f,  0.5f, 1.0f, 0.0f},
        { 0.5f, -0.5f,  0.5f, 0.0f, 1.0f},
        { 0.5f,  0.5f,  0.5f, 0.0f, 0.0f},

        // Top face
        {-0.5f,  0.5f, -0.5f, 0.0f, 1.0f},
        {-0.5f,  0.5f,  0.5f, 0.0f, 0.0f},
        { 0.5f,  0.5f, -0.5f, 1.0f, 1.0f},
        { 0.5f,  0.5f,  0.5f, 1.0f, 0.0f},

        // Bottom face
        {-0.5f, -0.5f, -0.5f, 1.0f, 1.0f},
        {-0.5f, -0.5f,  0.5f, 1.0f, 0.0f},
        { 0.5f, -0.5f, -0.5f, 0.0f, 1.0f},
        { 0.5f, -0.5f,  0.5f, 0.0f, 0.0f},

        // Right face
        { 0.5f, -0.5f, -0.5f, 0.0f, 1.0f},
        { 0.5f,  0.5f, -0.5f, 0.0f, 0.0f},
        { 0.5f, -0.5f,  0.5f, 1.0f, 1.0f},
        { 0.5f,  0.5f,  0.5f, 1.0f, 0.0f},

        // Left face
        {-0.5f, -0.5f, -0.5f, 1.0f, 1.0f},
        {-0.5f,  0.5f, -0.5f, 1.0f, 0.0f},
        {-0.5f, -0.5f,  0.5f, 0.0f, 1.0f},
        {-0.5f,  0.5f,  0.5f, 0.0f, 0.0f}
    };
}

std::vector<unsigned int> CreateCubeIndices() {
    return {
        // Front
        0, 1, 2, 2, 1, 3,
        // Back
        4, 6, 5, 6, 7, 5,
        // Top
        8, 9, 10, 10, 9, 11,
        // Bottom
        12, 14, 13, 14, 15, 13,
        // Right
        16, 17, 18, 18, 17, 19,
        // Left
        20, 22, 21, 22, 23, 21
    };
}

// Создаем fullscreen quad вершины
std::vector<Vertex> CreateFullScreenQuadVertices() {
    return {
        {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
        {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},
        { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f},
        { 1.0f,  1.0f, 0.0f, 1.0f, 0.0f}
    };
}

std::vector<unsigned int> CreateFullScreenQuadIndices() {
    return { 0, 1, 2, 2, 1, 3 };
}

// --- Main ---
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    const int W = 1280;
    const int H = 720;

    // 1. Создаем окно
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, DefWindowProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, L"RendeructorTest", NULL };
    RegisterClassEx(&wc);
    HWND hwnd = CreateWindow(L"RendeructorTest", L"Rendeructor Test",
        WS_OVERLAPPEDWINDOW, 100, 100, W, H, NULL, NULL, wc.hInstance, NULL);
    ShowWindow(hwnd, SW_SHOW);

    // 2. Движок
    Rendeructor* backend = new Rendeructor();
    BackendConfig cfg;
    cfg.Width = W;
    cfg.Height = H;
    cfg.WindowHandle = hwnd;
    cfg.API = RenderAPI::DirectX11;

    if (!backend->Create(cfg)) {
        MessageBox(hwnd, L"Failed to create render backend", L"Error", MB_OK);
        return -1;
    }

    // 3. Ресурсы
    // A) 3D Текстура (16x16x16 для теста - меньше памяти)
    Texture3D volTex;
    auto volData = Generate3DGradient(16, 16, 16);
    volTex.Create(16, 16, 16, volData.data());

    // B) Простая цветная текстура
    Texture colorTex;
    {
        // Создаем 2x2 цветную текстуру
        unsigned char pixels[] = {
            255, 0, 0, 255,     // Красный
            0, 255, 0, 255,     // Зеленый
            0, 0, 255, 255,     // Синий
            255, 255, 255, 255  // Белый
        };
        colorTex.Create(2, 2, TextureFormat::RGBA8);
        // TODO: Нужно установить данные текстуры - это требует доп. метода в API
    }

    // C) Рендер таргет
    Texture renderTarget;
    renderTarget.Create(W, H, TextureFormat::RGBA8);

    Sampler linearSampler;
    linearSampler.Create("Linear");

    // 4. Меши
    Mesh cubeMesh;
    {
        auto vertices = CreateCubeVertices();
        auto indices = CreateCubeIndices();
        cubeMesh.Create(vertices, indices);
    }

    Mesh quadMesh;
    {
        auto vertices = CreateFullScreenQuadVertices();
        auto indices = CreateFullScreenQuadIndices();
        quadMesh.Create(vertices, indices);
    }

    // 5. Шейдеры - используем простые тестовые шейдеры
    // ВАЖНО: Убедитесь, что файл Shader.hlsl существует в той же папке, что и exe
    ShaderPass simplePass;
    simplePass.VertexShaderPath = "Shader.hlsl";
    simplePass.VertexShaderEntryPoint = "VS_Simple";
    simplePass.PixelShaderPath = "Shader.hlsl";
    simplePass.PixelShaderEntryPoint = "PS_Simple";
    simplePass.AddTexture("colorTex", colorTex);
    simplePass.AddSampler("linearSampler", linearSampler);

    ShaderPass postProcessPass;
    postProcessPass.VertexShaderPath = "Shader.hlsl";
    postProcessPass.VertexShaderEntryPoint = "VS_Fullscreen";
    postProcessPass.PixelShaderPath = "Shader.hlsl";
    postProcessPass.PixelShaderEntryPoint = "PS_Fullscreen";
    postProcessPass.AddTexture("renderTex", renderTarget);
    postProcessPass.AddSampler("linearSampler", linearSampler);

    backend->CompilePass(simplePass);
    backend->CompilePass(postProcessPass);

    // 6. Матрицы
    Math::float4x4 proj = Math::float4x4::perspective_lh_zo(45.0f * 0.01745f, (float)W / H, 0.1f, 100.0f);

    auto startTime = std::chrono::high_resolution_clock::now();
    bool running = true;
    MSG msg;

    while (running) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            float time = std::chrono::duration<float>(
                std::chrono::high_resolution_clock::now() - startTime).count();

            // --- ШАГ 1: Рендер в текстуру ---
            backend->RenderToTexture(renderTarget);
            backend->Clear(0.1f, 0.2f, 0.3f, 1.0f); // Очищаем голубым

            Math::float4x4 view = Math::float4x4::look_at_lh(
                Math::float3(cos(time * 0.5f) * 3, 2, sin(time * 0.5f) * 3),
                Math::float3(0, 0, 0),
                Math::float3(0, 1, 0));

            // Рисуем куб
            backend->SetShaderPass(simplePass);
            Math::float4x4 model = Math::float4x4::rotation_euler(Math::float3(time, time * 0.7f, 0));
            Math::float4x4 mvp = model * view * proj;

            backend->SetConstant("mvp", mvp);
            backend->SetConstant("time", time);

            backend->DrawMesh(cubeMesh);

            // --- ШАГ 2: Рендер на экран с пост-процессом ---
            // Рендерим на экран (передаем пустую текстуру)
            Texture screenTarget; // Пустая текстура означает экран
            backend->RenderToTexture(screenTarget);
            backend->Clear(0.0f, 0.0f, 0.0f, 1.0f);

            backend->SetShaderPass(postProcessPass);
            backend->SetConstant("time", time);

            // Рисуем fullscreen quad
            backend->DrawMesh(quadMesh);

            backend->Present();
        }
    }

    delete backend;
    return 0;
}
