#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <memory>
#include <algorithm> // for max

#include <Imgui/imgui.h>
#include <Imgui/backends/imgui_impl_dx11.h>
#include <Imgui/backends/imgui_impl_win32.h>

#include <Rendeructor.h>

#pragma comment( lib, "winmm.lib")  
#pragma comment( lib, "Rendeructor.lib") 
#pragma comment(lib, "d3d11.lib") 

// =========================================================
// IMGUI WNDPROC HANDLER
// =========================================================
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using namespace Math;

// =========================================================
// GPU DATA STRUCTURES
// =========================================================

const int MAX_OBJECTS = 128;
const int TILE_SIZE = 32; // Размер плитки рендеринга
const int SSAA_FACTOR = 1; // 1 = Нативное разрешение (быстрее)

enum class PrimitiveType { Sphere = 0, Box = 1, Plane = 2 };

struct PostProcessData {
    float Exposure;
    Math::float3 Padding; // Выравнивание до 16 байт (float4 size)
};

struct HighlightCB {
    float TileIndex;
    float TilesStride;
    float TileSize;
    float Padding;
};

struct SDFObjectGPU {
    Math::float4 PositionAndType;
    Math::float4 SizeAndRough;
    Math::float4 RotationAndMetal;
    Math::float4 ColorAndEmit;
};

struct SceneObjectsBuffer {
    SDFObjectGPU Objects[MAX_OBJECTS];
    int          ObjectCount;
    Math::float3 Padding;
};

struct PTSceneData {
    Math::float4   CameraPos;
    Math::float4   CameraDir;
    Math::float4   CameraRight;
    Math::float4   CameraUp;
    Math::float4   Resolution;
    Math::float4   Params; // x = Seed, y = Probability
};

// =========================================================
// HELPERS (Scene Graph)
// =========================================================

class Object {
public:
    Object(int id) : m_id(id) {}
    virtual ~Object() = default;
    void SetPosition(float x, float y, float z) { m_position = float3(x, y, z); }
    void SetRotation(float x, float y, float z) { m_rotationDeg = float3(x, y, z); }
    void SetScale(float s) { m_scale = float3(s, s, s); }
    void SetScale(float x, float y, float z) { m_scale = float3(x, y, z); }
protected:
    int m_id;
    float3 m_position = { 0, 0, 0 };
    float3 m_rotationDeg = { 0, 0, 0 };
    float3 m_scale = { 1, 1, 1 };
};

class GeometryPrimitive : public Object {
public:
    GeometryPrimitive(int id, PrimitiveType type) : Object(id), m_type(type) {}
    void SetColor(float r, float g, float b) { m_color = float3(r, g, b); }
    void SetRoughness(float r) { m_roughness = r; }
    void SetMetalness(float m) { m_metalness = m; }
    void SetEmission(float e) { m_emission = e; }

    SDFObjectGPU GetGPUData() const {
        SDFObjectGPU data;
        data.PositionAndType = float4(m_position.x, m_position.y, m_position.z, (float)m_type);
        data.SizeAndRough = float4(m_scale.x, m_scale.y, m_scale.z, m_roughness);
        float radX = m_rotationDeg.x * 3.14159f / 180.0f;
        float radY = m_rotationDeg.y * 3.14159f / 180.0f;
        float radZ = m_rotationDeg.z * 3.14159f / 180.0f;
        data.RotationAndMetal = float4(radX, radY, radZ, m_metalness);
        data.ColorAndEmit = float4(m_color.x, m_color.y, m_color.z, m_emission);
        return data;
    }
private:
    PrimitiveType m_type;
    float3 m_color = { 1, 1, 1 };
    float m_roughness = 0.5f;
    float m_metalness = 0.0f;
    float m_emission = 0.0f;
};

class Scene {
public:
    GeometryPrimitive* CreatePrimitive(PrimitiveType type) {
        if (m_primitives.size() >= MAX_OBJECTS) return nullptr;
        int newID = (int)m_primitives.size();
        auto newPrim = std::make_unique<GeometryPrimitive>(newID, type);
        GeometryPrimitive* ptr = newPrim.get();
        m_primitives.push_back(std::move(newPrim));
        return ptr;
    }
    SceneObjectsBuffer GenerateGPUBuffer() const {
        SceneObjectsBuffer buffer;
        buffer.ObjectCount = (int)m_primitives.size();
        for (int i = 0; i < buffer.ObjectCount; i++) buffer.Objects[i] = m_primitives[i]->GetGPUData();
        return buffer;
    }
    void Clear() { m_primitives.clear(); }
private:
    std::vector<std::unique_ptr<GeometryPrimitive>> m_primitives;
};

// =========================================================
// MAIN LOGIC
// =========================================================

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    int WindowW = 1280;
    int WindowH = 720;
    int RenderW = WindowW * SSAA_FACTOR;
    int RenderH = WindowH * SSAA_FACTOR;

    // ... (Создание окна и Rendeructor init остаются без изменений) ...
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW,
        [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
            if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return true;
            if (m == WM_DESTROY) PostQuitMessage(0); return DefWindowProc(h, m, w, l); },
        0, 0, hInstance, nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, "StochasticPT - Moving Avg", nullptr };
    RegisterClassEx(&wc);
    HWND hwnd = CreateWindowEx(0, "StochasticPT - Moving Avg", "Initializing...", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, WindowW, WindowH, nullptr, nullptr, hInstance, nullptr);

    Rendeructor renderer;
    BackendConfig config;
    config.Width = WindowW; config.Height = WindowH; config.WindowHandle = hwnd; config.API = RenderAPI::DirectX11;
    if (!renderer.Create(config)) return 0;

    // ==========================================
    // IMGUI INIT
    // ==========================================
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);

    // Получаем сырые указатели DirectX11 из Rendeructor
    // Важно: кастим void* в реальные типы D3D11
    ID3D11Device* d3dDevice = (ID3D11Device*)renderer.GetDevice();
    ID3D11DeviceContext* d3dContext = (ID3D11DeviceContext*)renderer.GetContext();
    ImGui_ImplDX11_Init(d3dDevice, d3dContext);

    // --- Resources ---
    // Нам нужно ДВА HDR буфера для Ping-Pong аккумуляции
    Texture rtHistory[2];
    rtHistory[0].Create(RenderW, RenderH, TextureFormat::RGBA32F);
    rtHistory[1].Create(RenderW, RenderH, TextureFormat::RGBA32F);

    Sampler linearSampler; linearSampler.Create("Linear");

    // --- Pipeline States ---
    PipelineState stateTileRender;
    stateTileRender.Cull = CullMode::None;
    stateTileRender.DepthWrite = false;
    stateTileRender.DepthFunc = CompareFunc::Always;
    stateTileRender.ScissorTest = true;
    stateTileRender.Blend = BlendMode::Opaque;

    PipelineState stateFullScreen = stateTileRender;
    stateFullScreen.ScissorTest = false;

    PipelineState stateUI;
    stateUI.Cull = CullMode::None;
    stateUI.DepthWrite = false;
    stateUI.DepthFunc = CompareFunc::Always;
    stateUI.ScissorTest = false;
    stateUI.Blend = BlendMode::AlphaBlend;

    // --- Shaders ---
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
    displayPass.AddSampler("Smp", linearSampler);
    renderer.CompilePass(displayPass);

    ShaderPass highlightPass;
    highlightPass.VertexShaderPath = "highlight.hlsl";
    highlightPass.VertexShaderEntryPoint = "VS_Main";
    highlightPass.PixelShaderPath = "highlight.hlsl";
    highlightPass.PixelShaderEntryPoint = "PS_Main";
    renderer.CompilePass(highlightPass);

    // --- Создание сцены (PBR Chart) ---
    Scene myScene;
    // Пол
    auto floor = myScene.CreatePrimitive(PrimitiveType::Plane);
    floor->SetPosition(0, 0, 0); floor->SetColor(0.05f, 0.05f, 0.05f); floor->SetRoughness(1.0f);

    // Свет (большая панель сверху)
    auto light = myScene.CreatePrimitive(PrimitiveType::Box);
    light->SetPosition(8.0f, 10.0f, 8.0f); light->SetScale(3.0f, 0.1f, 3.0f);
    light->SetColor(1.0f, 0.9f, 0.8f); light->SetEmission(5.0f);

    // Сферы 8x8
    int rows = 8, cols = 8;
    float spacing = 2.5f;
    for (int z = 0; z < rows; ++z) {
        for (int x = 0; x < cols; ++x) {
            auto s = myScene.CreatePrimitive(PrimitiveType::Sphere);
            s->SetPosition(x * spacing, 1.0f, z * spacing);
            s->SetScale(0.9f);

            // X: Roughness (0..1)
            float r = (float)x / (float)(cols - 1);
            s->SetRoughness(std::max(r, 0.04f));

            // Z: Metalness (0..1)
            float m = (float)z / (float)(rows - 1);
            s->SetMetalness(m);

            s->SetColor(0.9f, 0.1f, 0.1f); // Красные шарики
        }
    }
    SceneObjectsBuffer gpuBuffer = myScene.GenerateGPUBuffer();

    // --- Камера и данные сцены ---
    PTSceneData sceneData;
    sceneData.Resolution = float4((float)RenderW, (float)RenderH, 0, 0);

    Math::float3 camPos = { 9.0f, 15.0f, -6.0f }; // Вид сверху-сбоку
    Math::float3 camTarget = { 9.0f, 0.0f, 9.0f }; // Смотрим в центр чарта

    int tilesX = (RenderW + TILE_SIZE - 1) / TILE_SIZE;
    int tilesY = (RenderH + TILE_SIZE - 1) / TILE_SIZE;
    int totalTiles = tilesX * tilesY;

    // === ДАННЫЕ ДЛЯ Moving Average ===
    int currentTileIndex = 0;
    float globalSeedTime = 1.0f;
    int frameIndex = 0;   // Сколько кадров уже усреднено
    int activeBuffer = 0; // 0 или 1, куда пишем сейчас
    float currentExposure = 1.0f;

    // Чистим оба буфера на старте
    renderer.Clear(rtHistory[0], 0, 0, 0, 0);
    renderer.Clear(rtHistory[1], 0, 0, 0, 0);

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        else {
            // =========================
            // START IMGUI FRAME
            // =========================
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            // Создаем окно настроек
            ImGui::Begin("Renderer Control");
            ImGui::Text("Tiles: %dx%d (%d px)", tilesX, tilesY, TILE_SIZE);
            ImGui::Separator();

            // Настройки, НЕ требующие перезапуска симуляции (PostProcess)
            ImGui::SliderFloat("Exposure", &currentExposure, 0.0f, 10.0f);

            ImGui::Separator();
            ImGui::Text("Simulation Parameters (Resets Render)");

            // Настройки, требующие перезапуска (изменяют сцену/камеру)
            bool simChanged = false;

            if (ImGui::Button("Reset Render")) simChanged = true;

            ImGui::End();

            // [Пробел] -> Полный сброс аккумуляции
            if (GetAsyncKeyState(VK_SPACE) & 0x8000 || simChanged) {
                currentTileIndex = 0;
                frameIndex = 0;
                globalSeedTime = 1.0f;
            }
            if (GetAsyncKeyState(VK_ADD) & 0x8000) currentExposure += 0.01f; // Numpad +
            if (GetAsyncKeyState(VK_SUBTRACT) & 0x8000) currentExposure -= 0.01f; // Numpad -

            if (currentExposure < 0.0f) currentExposure = 0.0f;

            // Рендерим пачку тайлов
            int tilesBatch = 8;
            renderer.SetPipelineState(stateTileRender);

            int readIdx = (activeBuffer == 0) ? 1 : 0; // Откуда читаем (предыдущий кадр)
            int writeIdx = activeBuffer;               // Куда пишем (текущий кадр)

            for (int b = 0; b < tilesBatch; b++) {
                if (currentTileIndex >= totalTiles) {
                    // Конец полного прохода по экрану
                    currentTileIndex = 0;

                    // Меняем буферы местами
                    activeBuffer = (activeBuffer == 0) ? 1 : 0;
                    readIdx = (activeBuffer == 0) ? 1 : 0;
                    writeIdx = activeBuffer;

                    frameIndex++;
                    break;
                }

                int tx = currentTileIndex % tilesX;
                int ty = currentTileIndex / tilesX;

                // (Код расчета матрицы камеры как у вас...)
                Math::float3 fwd = (camTarget - camPos).normalize();
                Math::float3 rgt = Math::float3(0, 1, 0).cross(fwd).normalize();
                Math::float3 up = fwd.cross(rgt).normalize();
                float ar = (float)RenderW / RenderH;
                float thf = tan(3.14159f / 3.0f * 0.5f);

                sceneData.CameraPos = float4(camPos.x, camPos.y, camPos.z, 1.0f);
                sceneData.CameraDir = float4(fwd.x, fwd.y, fwd.z, 0.0f);
                sceneData.CameraRight = float4(rgt.x * thf * ar, rgt.y * thf * ar, rgt.z * thf * ar, 0.0f);
                sceneData.CameraUp = float4(up.x * thf, up.y * thf, up.z * thf, 0.0f);

                // PARAMS:
                // x = Random Seed
                // z = Индекс текущего кадра аккумуляции (для Lerp веса)
                globalSeedTime += 1.61803f;
                sceneData.Params.x = globalSeedTime;
                sceneData.Params.z = (float)frameIndex;

                // --- BINDING ---

                // 1. Ставим RenderTarget для ЗАПИСИ
                renderer.SetRenderTarget(rtHistory[writeIdx]);

                // 2. Биндим текстуру для ЧТЕНИЯ (История)
                ptPass.AddTexture("TexHistory", rtHistory[readIdx]);
                renderer.SetShaderPass(ptPass);

                renderer.SetCustomConstant("SceneBuffer", sceneData);
                renderer.SetCustomConstant("ObjectBuffer", gpuBuffer);

                renderer.SetScissor(tx * TILE_SIZE, ty * TILE_SIZE, TILE_SIZE, TILE_SIZE);
                renderer.DrawFullScreenQuad();

                currentTileIndex++;
            }

            // --- Present to Screen ---
            // Отрисовываем буфер, в который МЫ ПИСАЛИ (rtHistory[writeIdx])
            // Важно сбросить рендер таргет на экран
            renderer.RenderPassToScreen();

            renderer.SetPipelineState(stateFullScreen);

            // Динамически обновляем текстуру для отображения
            displayPass.AddTexture("TexHDR", rtHistory[writeIdx]);

            PostProcessData ppData;
            ppData.Exposure = currentExposure;
            renderer.SetCustomConstant("PostProcessParams", ppData);

            renderer.SetShaderPass(displayPass);

            renderer.DrawFullScreenQuad();


            renderer.SetPipelineState(stateUI); // Blend Alpha, Depth Off

            HighlightCB hlParams;
            hlParams.TileIndex = (float)currentTileIndex;
            hlParams.TilesStride = (float)tilesX;
            hlParams.TileSize = (float)TILE_SIZE;

            renderer.SetCustomConstant("HighlightParams", hlParams);

            renderer.SetShaderPass(highlightPass);
            renderer.DrawFullScreenQuad();

            ImGui::Render();
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

            renderer.Present();

            char title[128];
            sprintf_s(title, "PT Moving Avg | Pass: %d | Tile: %d/%d", frameIndex, currentTileIndex, totalTiles);
            SetWindowText(hwnd, title);
        }
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    renderer.Destroy();
    return 0;
}