#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <cmath>
#include <ctime>

// Подключаем движок
#include <Rendeructor.h>

// Линкуем библиотеки
#pragma comment( lib, "winmm.lib")  
#pragma comment( lib, "Rendeructor.lib") 

using namespace Math;

// =========================================================
// CONFIG
// =========================================================

const int TILE_SIZE = 64; // Размер плитки

struct PTSceneData {
    Math::float4   CameraPos;
    Math::float4   CameraDir;   // Куда смотрит (Forward)
    Math::float4   CameraRight; // Вектор вправо
    Math::float4   CameraUp;    // Вектор вверх
    Math::float4   Resolution;
    Math::float4   Params;
};

// =========================================================
// ENTRY POINT
// =========================================================

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // 1. Создание окна
    int W = 1280, H = 720;
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW,
        [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
            if (m == WM_DESTROY) PostQuitMessage(0); return DefWindowProc(h, m, w, l); },
        0, 0, hInstance, nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, "TiledRenderer", nullptr };
    RegisterClassEx(&wc);

    HWND hwnd = CreateWindowEx(0, "TiledRenderer", "Waiting for render...", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, W, H, nullptr, nullptr, hInstance, nullptr);

    // 2. Инициализация Rendeructor
    Rendeructor renderer;
    BackendConfig config;
    config.Width = W;
    config.Height = H;
    config.WindowHandle = hwnd;
    config.API = RenderAPI::DirectX11;

    if (!renderer.Create(config)) return 0;

    // ---------------------------------------------------------
    // РЕСУРСЫ
    // ---------------------------------------------------------
    Texture rtHDR;
    rtHDR.Create(W, H, TextureFormat::RGBA32F);

    Sampler pointSampler;
    pointSampler.Create("Point");

    // ---------------------------------------------------------
    // PIPELINE STATES
    // ---------------------------------------------------------

    // State для тайлов (с обрезкой)
    PipelineState stateTileRender;
    stateTileRender.Cull = CullMode::None;
    stateTileRender.DepthWrite = false;
    stateTileRender.DepthFunc = CompareFunc::Always;
    stateTileRender.ScissorTest = true;

    // State для финального вывода (без обрезки)
    PipelineState stateFullScreen;
    stateFullScreen.Cull = CullMode::None;
    stateFullScreen.DepthWrite = false;
    stateFullScreen.DepthFunc = CompareFunc::Always;
    stateFullScreen.ScissorTest = false;

    // ---------------------------------------------------------
    // ШЕЙДЕРЫ
    // ---------------------------------------------------------
    ShaderPass ptPass;
    ptPass.VertexShaderPath = "PathTracer.hlsl";
    ptPass.VertexShaderEntryPoint = "VS_Quad";
    ptPass.PixelShaderPath = "PathTracer.hlsl";
    ptPass.PixelShaderEntryPoint = "PS_PathTrace";
    renderer.CompilePass(ptPass);

    ShaderPass displayPass;
    displayPass.VertexShaderPath = "FinalOutput.hlsl";
    displayPass.VertexShaderEntryPoint = "VS_Quad";
    displayPass.PixelShaderPath = "FinalOutput.hlsl";
    displayPass.PixelShaderEntryPoint = "PS_ToneMap";
    displayPass.AddTexture("TexHDR", rtHDR);
    displayPass.AddSampler("Smp", pointSampler);
    renderer.CompilePass(displayPass);

    // ---------------------------------------------------------
    // ЛОГИКА
    // ---------------------------------------------------------
    PTSceneData sceneData;
    sceneData.Resolution = Math::float4((float)W, (float)H, 0, 0);

    Math::float3 camPos = { 0.0f, 2.0f, -4.0f }; // Чуть повыше
    Math::float3 camTarget = { 0.0f, 1.0f, 0.0f };

    // Параметры для расчета FOV
    float fovY = 3.14f / 3.0f; // 60 градусов
    float aspectRatio = (float)W / H;
    float tanHalfFov = tan(fovY * 0.5f);

    int tilesX = (W + TILE_SIZE - 1) / TILE_SIZE;
    int tilesY = (H + TILE_SIZE - 1) / TILE_SIZE;
    int totalTiles = tilesX * tilesY;

    // Начальное состояние
    int currentTileIndex = 0;
    renderer.Clear(rtHDR, 0, 0, 0, 1); // Очищаем HDR буфер перед стартом

    MSG msg = {};
    float time = 0.0f;

    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        else {

            // --- УПРАВЛЕНИЕ: ПЕРЕЗАПУСК ---
            // Если нажат ПРОБЕЛ -> Сброс рендера
            if (GetAsyncKeyState(VK_SPACE) & 0x8000) {
                currentTileIndex = 0;
                renderer.Clear(rtHDR, 0, 0, 0, 1);
                SetWindowText(hwnd, "Restarting Render...");
            }

            // =========================================================
            // PASS 1: Рендеринг (Только если есть необработанные тайлы)
            // =========================================================

            if (currentTileIndex < totalTiles) {

                // Обновляем заголовок окна для информации
                char title[64];
                sprintf_s(title, "Rendering Tile: %d / %d (Press SPACE to reset)", currentTileIndex + 1, totalTiles);
                SetWindowText(hwnd, title);

                // Координаты тайла
                int tx = currentTileIndex % tilesX;
                int ty = currentTileIndex / tilesX;
                int scissorX = tx * TILE_SIZE;
                int scissorY = ty * TILE_SIZE;

                // 1. Считаем базис камеры вручную (так надежнее всего)
                Math::float3 forward = (camTarget - camPos).normalize();
                Math::float3 right = Math::float3(0, 1, 0).cross(forward).normalize();
                Math::float3 up = forward.cross(right).normalize();

                // 2. Масштабируем вектора для Ray Casting'а
                // (Чтобы в шейдере просто умножать на UV)
                Math::float3 rayRight = right * tanHalfFov * aspectRatio;
                Math::float3 rayUp = up * tanHalfFov;

                // 3. Заполняем структуру
                sceneData.CameraPos = Math::float4(camPos.x, camPos.y, camPos.z, 1.0f);
                sceneData.CameraDir = Math::float4(forward.x, forward.y, forward.z, 0.0f);
                sceneData.CameraRight = Math::float4(rayRight.x, rayRight.y, rayRight.z, 0.0f);
                sceneData.CameraUp = Math::float4(rayUp.x, rayUp.y, rayUp.z, 0.0f);

                sceneData.Params.x = time;

                // --- DRAW TILE ---
                renderer.SetRenderTarget(rtHDR);
                renderer.SetPipelineState(stateTileRender);
                renderer.SetScissor(scissorX, scissorY, TILE_SIZE, TILE_SIZE);

                renderer.SetShaderPass(ptPass);
                renderer.SetCustomConstant("SceneBuffer", sceneData);
                renderer.DrawFullScreenQuad();

                // Переходим к следующему
                currentTileIndex++;

                if (currentTileIndex >= totalTiles) {
                    SetWindowText(hwnd, "Rendering FINISHED! (Press SPACE to restart)");
                }
            }

            // =========================================================
            // PASS 2: Вывод на экран (ВСЕГДА)
            // =========================================================
            // Даже если рендеринг закончен, мы должны продолжать выводить 
            // содержимое rtHDR на экран, иначе окно станет черным или зависнет.

            renderer.RenderPassToScreen();
            renderer.SetPipelineState(stateFullScreen); // Без Scissor

            renderer.SetShaderPass(displayPass);
            renderer.DrawFullScreenQuad();

            renderer.Present();

            // Если рендер закончен, даем процессору отдохнуть
            if (currentTileIndex >= totalTiles) {
                Sleep(16); // ~60 FPS idle
            }
        }
    }

    renderer.Destroy();
    return 0;
}
