#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <cmath>

// Подключение движка
#include <Rendeructor.h>

// Линковка библиотек (если не настроено в проекте)
#pragma comment( lib, "winmm.lib")  
#pragma comment( lib, "Rendeructor.lib") 

using namespace Math;

// Структура данных для шейдера (должна совпадать с cbuffer в HLSL)
struct SceneData {
    Math::float4x4 World;           // Матрица мира (заглушка)
    Math::float4x4 ViewProjection;  // Матрица вида (заглушка)
    Math::float4   Resolution;      // .xy = Ширина, Высота
    Math::float4   Params;          // .x = Время
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // 1. Создание окна
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW,
        [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
            if (m == WM_DESTROY) PostQuitMessage(0); return DefWindowProc(h, m, w, l); },
        0, 0, hInstance, nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, "RaymarchSample", nullptr };
    RegisterClassEx(&wc);

    int W = 1280, H = 720;
    HWND hwnd = CreateWindowEx(0, "RaymarchSample", "Rendeructor: Raymarching & PostProcess", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, W, H, nullptr, nullptr, hInstance, nullptr);

    // 2. Инициализация движка
    Rendeructor renderer;
    BackendConfig config;
    config.Width = W;
    config.Height = H;
    config.WindowHandle = hwnd;
    config.API = RenderAPI::DirectX11;

    if (!renderer.Create(config)) return 0;

    // ---------------------------------------------------------
    // НАСТРОЙКА РЕСУРСОВ
    // ---------------------------------------------------------

    // Текстура, в которую будем рисовать первый проход (Off-screen)
    Texture offscreenTexture;
    offscreenTexture.Create(W, H, TextureFormat::RGBA8);

    // Сэмплер для чтения текстуры
    Sampler linearSampler;
    linearSampler.Create("Linear");

    // ---------------------------------------------------------
    // НАСТРОЙКА ПРОХОДОВ (PASSES)
    // ---------------------------------------------------------

    // PASS 1: Raymarching Pass
    // Рисуем куб математикой внутри пиксельного шейдера
    ShaderPass raymarchPass;
    raymarchPass.VertexShaderPath = "Shader.hlsl";
    raymarchPass.VertexShaderEntryPoint = "VS_Quad";         // Стандартный квад
    raymarchPass.PixelShaderPath = "Shader.hlsl";
    raymarchPass.PixelShaderEntryPoint = "PS_Scene_Raymarch"; // Логика SDF
    renderer.CompilePass(raymarchPass);

    // PASS 2: Post Process Pass
    // Берет результат Pass 1 и накладывает эффекты
    ShaderPass postProcessPass;
    postProcessPass.VertexShaderPath = "Shader.hlsl";
    postProcessPass.VertexShaderEntryPoint = "VS_Quad";
    postProcessPass.PixelShaderPath = "Shader.hlsl";
    postProcessPass.PixelShaderEntryPoint = "PS_PostProcess";

    // Связываем выход Pass 1 с входом Pass 2
    postProcessPass.AddTexture("InputTexture", offscreenTexture);
    postProcessPass.AddSampler("InputSampler", linearSampler);
    renderer.CompilePass(postProcessPass);

    // ---------------------------------------------------------
    // ГЛАВНЫЙ ЦИКЛ
    // ---------------------------------------------------------

    SceneData sceneCB;
    sceneCB.Resolution = Math::float4((float)W, (float)H, 0, 0);
    // Остальные матрицы можно оставить Identity, так как raymarching считает камеру сам
    sceneCB.World = Math::float4x4::identity();
    sceneCB.ViewProjection = Math::float4x4::identity();

    MSG msg = {};
    float time = 0.0f;

    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        else {
            time += 0.01f;
            sceneCB.Params.x = time; // Обновляем время

            // ==================================================
            // ПРОХОД 1: Рендер в текстуру
            // ==================================================

            // Ставим целью нашу текстуру
            renderer.SetRenderTarget(offscreenTexture);
            // Очищаем (цвет не важен, так как SDF перерисует каждый пиксель, но для надежности черный)
            renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);

            // Активируем шейдер Raymarching
            renderer.SetShaderPass(raymarchPass);
            // Отправляем данные (время, разрешение)
            renderer.SetCustomConstant("SceneBuffer", sceneCB);

            // Рисуем на весь экран
            renderer.DrawFullScreenQuad();

            // ==================================================
            // ПРОХОД 2: Вывод на экран с обработкой
            // ==================================================

            // Возвращаем рендер на экран
            renderer.RenderPassToScreen();
            renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f); // Очистка экрана

            // Активируем шейдер Пост-процесса
            renderer.SetShaderPass(postProcessPass);
            // Рисуем результат
            renderer.DrawFullScreenQuad();

            renderer.Present();
        }
    }

    renderer.Destroy();
    return 0;
}
