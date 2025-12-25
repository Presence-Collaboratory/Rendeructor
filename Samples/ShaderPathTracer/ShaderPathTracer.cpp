#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <memory>
#include <algorithm>

#include <Imgui/imgui.h>
#include <Imgui/backends/imgui_impl_dx11.h>
#include <Imgui/backends/imgui_impl_win32.h>

#include <Rendeructor.h>

#pragma comment(lib, "winmm.lib")  
#pragma comment(lib, "Rendeructor.lib") 

// =========================================================
// EXTERNS & CONSTANTS
// =========================================================
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using namespace Math;

const int MAX_OBJECTS = 128;
const int TILE_SIZE = 32;
const int SSAA_FACTOR = 1;

enum class PrimitiveType { Sphere = 0, Box = 1, Plane = 2 };

// =========================================================
// GPU STRUCTS
// =========================================================
struct PostProcessData {
    float Exposure;
    Math::float3 Padding;
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
    Math::float4 CameraPos;
    Math::float4 CameraDir;
    Math::float4 CameraRight;
    Math::float4 CameraUp;
    Math::float4 Resolution;
    Math::float4 Params; // x=Seed, z=FrameIdx
};

// =========================================================
// SCENE SYSTEM
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
private:
    std::vector<std::unique_ptr<GeometryPrimitive>> m_primitives;
};

// =========================================================
// APPLICATION CLASS
// =========================================================
class PathTracerApp {
public:
    PathTracerApp(int width, int height) :
        m_windowW(width), m_windowH(height),
        m_renderW(width* SSAA_FACTOR), m_renderH(height* SSAA_FACTOR) {}

    ~PathTracerApp() {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        m_renderer.Destroy();
    }

    bool Initialize(HINSTANCE hInstance) {
        if (!InitWindow(hInstance)) return false;
        if (!InitRenderer()) return false;
        if (!InitImGui()) return false;

        SetupResources();
        SetupScene();

        return true;
    }

    void Run() {
        MSG msg = {};
        while (msg.message != WM_QUIT) {
            if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            else {
                UpdateAndRender();
            }
        }
    }

private:
    // --- Window & Engine ---
    HWND m_hwnd = nullptr;
    Rendeructor m_renderer;
    int m_windowW, m_windowH;
    int m_renderW, m_renderH;

    // --- Resources ---
    // [FIX 1] Сэмплер теперь член класса (не удаляется из памяти после Init)
    Sampler m_linearSampler;
    Texture m_rtHistory[2];
    ShaderPass m_ptPass, m_displayPass, m_highlightPass;
    PipelineState m_stateTileRender, m_stateFullScreen, m_stateUI;

    // --- Scene & Data ---
    Scene m_scene;
    SceneObjectsBuffer m_gpuBuffer;

    // --- Logic State ---
    Math::float3 m_camPos = { 9.0f, 15.0f, -6.0f };
    Math::float3 m_camTarget = { 9.0f, 0.0f, 9.0f };

    int m_currentTileIndex = 0;
    int m_frameIndex = 0;

    // [FIX 2] Разделяем логику буферов:
    // activeBuffer: индекс для swap'а в процессе расчета
    // displayBuffer: индекс готового кадра для вывода на экран
    int m_activeBuffer = 0;
    int m_displayBuffer = 0;

    float m_globalSeedTime = 1.0f;
    float m_currentExposure = 1.0f;

private:
    bool InitWindow(HINSTANCE hInstance) {
        WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW,
            [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
                if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return true;
                if (m == WM_DESTROY) PostQuitMessage(0); return DefWindowProc(h, m, w, l); },
            0, 0, hInstance, nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, "StochasticPT" };

        if (!RegisterClassEx(&wc)) return false;

        m_hwnd = CreateWindowEx(0, "StochasticPT", "DirectX11 PathTracer",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            100, 100, m_windowW, m_windowH, nullptr, nullptr, hInstance, nullptr);
        return (m_hwnd != nullptr);
    }

    bool InitRenderer() {
        BackendConfig config;
        config.Width = m_windowW; config.Height = m_windowH;
        config.WindowHandle = m_hwnd; config.API = RenderAPI::DirectX11;
        return m_renderer.Create(config);
    }

    bool InitImGui() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        ImGui_ImplWin32_Init(m_hwnd);
        auto d3dDevice = (ID3D11Device*)m_renderer.GetDevice();
        auto d3dContext = (ID3D11DeviceContext*)m_renderer.GetContext();
        return ImGui_ImplDX11_Init(d3dDevice, d3dContext);
    }

    void SetupResources() {
        // Init Resources
        m_rtHistory[0].Create(m_renderW, m_renderH, TextureFormat::RGBA32F);
        m_rtHistory[1].Create(m_renderW, m_renderH, TextureFormat::RGBA32F);
        // Заливаем черным для старта
        m_renderer.Clear(m_rtHistory[0], 0, 0, 0, 0);
        m_renderer.Clear(m_rtHistory[1], 0, 0, 0, 0);

        m_linearSampler.Create("Linear"); // Валидный указатель теперь живет вечно

        // States
        m_stateTileRender.Cull = CullMode::None;
        m_stateTileRender.DepthWrite = false;
        m_stateTileRender.DepthFunc = CompareFunc::Always; // Для фуллскрина Always надежнее
        m_stateTileRender.ScissorTest = true;
        m_stateTileRender.Blend = BlendMode::Opaque;

        m_stateFullScreen = m_stateTileRender;
        m_stateFullScreen.ScissorTest = false;

        m_stateUI = m_stateTileRender;
        m_stateUI.ScissorTest = false;
        m_stateUI.Blend = BlendMode::AlphaBlend;

        // Shaders
        m_ptPass.VertexShaderPath = "PathTracer.hlsl"; m_ptPass.VertexShaderEntryPoint = "VS_Quad";
        m_ptPass.PixelShaderPath = "PathTracer.hlsl";  m_ptPass.PixelShaderEntryPoint = "PS_PathTrace";
        m_renderer.CompilePass(m_ptPass);

        m_displayPass.VertexShaderPath = "FinalOutput.hlsl"; m_displayPass.VertexShaderEntryPoint = "VS_Quad";
        m_displayPass.PixelShaderPath = "FinalOutput.hlsl";  m_displayPass.PixelShaderEntryPoint = "PS_ToneMap";
        // Важно: здесь теперь передается ссылка на живой член класса
        m_displayPass.AddSampler("Smp", m_linearSampler);
        m_renderer.CompilePass(m_displayPass);

        m_highlightPass.VertexShaderPath = "highlight.hlsl"; m_highlightPass.VertexShaderEntryPoint = "VS_Main";
        m_highlightPass.PixelShaderPath = "highlight.hlsl";  m_highlightPass.PixelShaderEntryPoint = "PS_Main";
        m_renderer.CompilePass(m_highlightPass);
    }

    void SetupScene() {
        // Создаем сцену 1 раз (если надо пересоздавать - нужно делать Clear у Scene)
        auto floor = m_scene.CreatePrimitive(PrimitiveType::Plane);
        floor->SetPosition(0, 0, 0); floor->SetColor(0.05f, 0.05f, 0.05f); floor->SetRoughness(1.0f);

        auto light = m_scene.CreatePrimitive(PrimitiveType::Box);
        light->SetPosition(8.0f, 10.0f, 8.0f); light->SetScale(3.0f, 0.1f, 3.0f);
        light->SetColor(1.0f, 0.9f, 0.8f); light->SetEmission(2.0f);

        int rows = 8, cols = 8;
        float spacing = 2.5f;
        for (int z = 0; z < rows; ++z) {
            for (int x = 0; x < cols; ++x) {
                auto s = m_scene.CreatePrimitive(PrimitiveType::Sphere);
                s->SetPosition(x * spacing, 1.0f, z * spacing);
                s->SetScale(0.9f);
                s->SetRoughness(std::max((float)x / (cols - 1), 0.04f));
                s->SetMetalness((float)z / (rows - 1));
                s->SetColor(0.9f, 0.1f, 0.1f);
            }
        }
        m_gpuBuffer = m_scene.GenerateGPUBuffer();
    }

    // [FIX 3] Хак для сброса кэша состояний Backend'а после ImGui
    void ForceStateFlush() {
        PipelineState dummyState;
        dummyState.Blend = BlendMode::Additive; // Ставим что-то отличное от Opaque
        dummyState.Cull = CullMode::Front;
        m_renderer.SetPipelineState(dummyState);
    }

    void UpdateControls(bool& simChanged) {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Renderer Control");
        ImGui::Text("Passes: %d", m_frameIndex);
        int tilesX = (m_renderW + TILE_SIZE - 1) / TILE_SIZE;
        ImGui::Text("Tile: %d / %d", m_currentTileIndex, tilesX * ((m_renderH + TILE_SIZE - 1) / TILE_SIZE));
        ImGui::SliderFloat("Exposure", &m_currentExposure, 0.0f, 10.0f);
        if (ImGui::Button("Reset Render")) simChanged = true;
        ImGui::End();

        if (GetAsyncKeyState(VK_SPACE) & 0x8000) simChanged = true;
    }

    void UpdateAndRender() {
        bool simChanged = false;
        UpdateControls(simChanged);
        if (simChanged) ResetSimulation();

        // 1. Ray Tracing (Offscreen)
        // ВАЖНО: Принудительно сбрасываем кэш стейтов, т.к. ImGui "загрязнил" контекст DX11
        ForceStateFlush();
        RenderPTBatches(8); // Рендерим пачку тайлов (чем больше число, тем быстрее обновляется кадр)

        // 2. Clear Screen & Tone Map
        // Принудительно очищаем BackBuffer цветом (дебаг, чтоб видеть если ToneMap упал)
        m_renderer.RenderPassToScreen(); // Bind Backbuffer
        m_renderer.Clear(0.1f, 0.1f, 0.15f, 1.0f);

        RenderDisplayPass(); // ToneMap поверх очищенного экрана

        // 3. UI Overlays
        RenderOverlayPass();

        // 4. Show it
        m_renderer.Present();
    }

    void ResetSimulation() {
        m_currentTileIndex = 0;
        m_frameIndex = 0;
        m_globalSeedTime = 1.0f;
        m_activeBuffer = 0;
        m_displayBuffer = 0;

        // Очищаем историю, чтобы убрать "призраков"
        m_renderer.Clear(m_rtHistory[0], 0, 0, 0, 0);
        m_renderer.Clear(m_rtHistory[1], 0, 0, 0, 0);
    }

    void RenderPTBatches(int batchCount) {
        m_renderer.SetPipelineState(m_stateTileRender);

        int tilesX = (m_renderW + TILE_SIZE - 1) / TILE_SIZE;
        int tilesY = (m_renderH + TILE_SIZE - 1) / TILE_SIZE;
        int totalTiles = tilesX * tilesY;

        // Double Buffering: читаем пред. кадр, пишем в текущий
        int readIdx = (m_activeBuffer == 0) ? 1 : 0;
        int writeIdx = m_activeBuffer;

        PTSceneData camData = CalculateCameraData();

        for (int b = 0; b < batchCount; b++) {
            // Если тайлы закончились — завершаем "sub-frame"
            if (m_currentTileIndex >= totalTiles) {
                m_currentTileIndex = 0;

                // [FIX 2 - Update] Теперь этот буфер готов для показа
                m_displayBuffer = m_activeBuffer;

                // Свапаем для следующего кадра
                m_activeBuffer = (m_activeBuffer == 0) ? 1 : 0;

                m_frameIndex++;
                break; // Выходим, чтобы дать ToneMap показать результат
            }

            int tx = m_currentTileIndex % tilesX;
            int ty = m_currentTileIndex / tilesX;

            m_globalSeedTime += 1.61803f;
            camData.Params.x = m_globalSeedTime;
            camData.Params.z = (float)m_frameIndex;

            // Bind Resources
            m_renderer.SetRenderTarget(m_rtHistory[writeIdx]);

            // ВАЖНО: Текстуру нужно биндить явно в PT Pass
            m_ptPass.AddTexture("TexHistory", m_rtHistory[readIdx]);
            m_renderer.SetShaderPass(m_ptPass);

            m_renderer.SetCustomConstant("SceneBuffer", camData);
            m_renderer.SetCustomConstant("ObjectBuffer", m_gpuBuffer);

            m_renderer.SetScissor(tx * TILE_SIZE, ty * TILE_SIZE, TILE_SIZE, TILE_SIZE);
            m_renderer.DrawFullScreenQuad();

            m_currentTileIndex++;
        }
    }

    void RenderDisplayPass() {
        m_renderer.SetPipelineState(m_stateFullScreen); // Depth Off, Scissor Off

        // [FIX 2] Используем ЯВНО индекс готового буфера
        m_displayPass.AddTexture("TexHDR", m_rtHistory[m_displayBuffer]);

        PostProcessData ppData = { m_currentExposure };
        m_renderer.SetCustomConstant("PostProcessParams", ppData);

        m_renderer.SetShaderPass(m_displayPass);
        m_renderer.DrawFullScreenQuad();
    }

    void RenderOverlayPass() {
        m_renderer.SetPipelineState(m_stateUI);
        int tilesX = (m_renderW + TILE_SIZE - 1) / TILE_SIZE;

        // Показываем текущий тайл (он бегает по экрану)
        HighlightCB hlParams = { (float)m_currentTileIndex, (float)tilesX, (float)TILE_SIZE, 0 };

        m_renderer.SetCustomConstant("HighlightParams", hlParams);
        m_renderer.SetShaderPass(m_highlightPass);
        m_renderer.DrawFullScreenQuad();

        // ImGui в самом конце
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    PTSceneData CalculateCameraData() {
        Math::float3 fwd = (m_camTarget - m_camPos).normalize();
        Math::float3 rgt = Math::float3(0, 1, 0).cross(fwd).normalize();
        Math::float3 up = fwd.cross(rgt).normalize();
        float ar = (float)m_renderW / m_renderH;
        float thf = tan(3.14159f / 3.0f * 0.5f);

        PTSceneData data;
        data.Resolution = float4((float)m_renderW, (float)m_renderH, 0, 0);
        data.CameraPos = float4(m_camPos.x, m_camPos.y, m_camPos.z, 1.0f);
        data.CameraDir = float4(fwd.x, fwd.y, fwd.z, 0.0f);
        data.CameraRight = float4(rgt.x * thf * ar, rgt.y * thf * ar, rgt.z * thf * ar, 0.0f);
        data.CameraUp = float4(up.x * thf, up.y * thf, up.z * thf, 0.0f);
        return data;
    }
};

// =========================================================
// MAIN - остается минимальным
// =========================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    PathTracerApp app(1280, 720);
    if (app.Initialize(hInstance)) {
        app.Run();
    }
    return 0;
}
