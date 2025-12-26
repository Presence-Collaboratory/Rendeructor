#define NOMINMAX
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
const int TILE_SIZE = 64;
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
enum class AppState {
    Config,     // Окно настроек (рендеринг стоит)
    Rendering   // Окно прогресса (рендеринг идет)
};

class PathTracerApp {
public:
    PathTracerApp(int width, int height) :
        m_windowW(width), m_windowH(height),
        m_renderW(width), m_renderH(height) // SSAA пока 1 к 1
    {}

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
    // --- Engine Vars ---
    HWND m_hwnd = nullptr;
    Rendeructor m_renderer;
    int m_windowW, m_windowH;
    int m_renderW, m_renderH;

    // --- Resources ---
    Sampler m_linearSampler;
    Texture m_rtHistory[2];
    ShaderPass m_ptPass, m_displayPass, m_highlightPass;
    PipelineState m_stateTileRender, m_stateFullScreen, m_stateUI;

    // --- Scene ---
    Scene m_scene;
    SceneObjectsBuffer m_gpuBuffer;

    // --- State & Config ---
    AppState m_state = AppState::Config;

    // Параметры рендера (изменяемые из GUI)
    int m_tileSize = 64;           // Размер плитки
    int m_tilesPerFrame = 8;       // Скорость (сколько плиток за кадр UI)
    int m_maxIterations = 1000;    // Лимит итераций (сэмплов)

    // Логика
    bool m_isPaused = false;
    float m_globalSeedTime = 1.0f;
    float m_currentExposure = 1.0f;

    // Внутренние счетчики
    int m_currentTileIndex = 0;
    int m_frameIndex = 0;       // Текущий sample count
    int m_activeBuffer = 0;     // Куда пишем (0 или 1)
    int m_displayBuffer = 0;    // Что показываем (0 или 1)

    // Камера
    Math::float3 m_camPos = { 9.0f, 15.0f, -6.0f };
    Math::float3 m_camTarget = { 9.0f, 0.0f, 9.0f };

private:
    bool InitWindow(HINSTANCE hInstance) {
        WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW,
        [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
                // Ловим ImGui
                if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return true;

                // Пытаемся достать указатель на наше приложение из окна
                PathTracerApp* app = (PathTracerApp*)GetWindowLongPtr(h, GWLP_USERDATA);

                switch (m) {
                case WM_SIZE:
                    // Если указатель есть и окно не минимизировано (SIZE_MINIMIZED = 1)
                    if (app && w != SIZE_MINIMIZED) {
                        // LOWORD/HIWORD дают ширину/высоту
                        app->OnResize((int)(short)LOWORD(l), (int)(short)HIWORD(l));
                    }
                    return 0;
                case WM_DESTROY:
                    PostQuitMessage(0);
                    return 0;
                }
                return DefWindowProc(h, m, w, l);
            },
            0, 0, hInstance, nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, "StochasticPT" };

        if (!RegisterClassEx(&wc)) return false;

        m_hwnd = CreateWindowEx(0, "StochasticPT", "PT Configurator",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            100, 100, m_windowW, m_windowH, nullptr, nullptr, hInstance, nullptr);

        SetWindowLongPtr(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);

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
        auto d3dDev = (ID3D11Device*)m_renderer.GetDevice();
        auto d3dCtx = (ID3D11DeviceContext*)m_renderer.GetContext();
        return ImGui_ImplDX11_Init(d3dDev, d3dCtx);
    }

    void SetupResources() {
        m_rtHistory[0].Create(m_renderW, m_renderH, TextureFormat::RGBA32F);
        m_rtHistory[1].Create(m_renderW, m_renderH, TextureFormat::RGBA32F);
        m_renderer.Clear(m_rtHistory[0], 0, 0, 0, 0);
        m_renderer.Clear(m_rtHistory[1], 0, 0, 0, 0);

        m_linearSampler.Create("Linear");

        m_stateTileRender.ScissorTest = true;
        m_stateTileRender.Blend = BlendMode::Opaque;
        m_stateTileRender.DepthWrite = false;
        m_stateTileRender.DepthFunc = CompareFunc::Always;

        m_stateFullScreen = m_stateTileRender;
        m_stateFullScreen.ScissorTest = false;

        m_stateUI = m_stateTileRender;
        m_stateUI.ScissorTest = false;
        m_stateUI.Blend = BlendMode::AlphaBlend;

        m_ptPass.VertexShaderPath = "PathTracer.hlsl";      m_ptPass.VertexShaderEntryPoint = "VS_Quad";
        m_ptPass.PixelShaderPath = "PathTracer.hlsl";       m_ptPass.PixelShaderEntryPoint = "PS_PathTrace";
        m_renderer.CompilePass(m_ptPass);

        m_displayPass.VertexShaderPath = "FinalOutput.hlsl"; m_displayPass.VertexShaderEntryPoint = "VS_Quad";
        m_displayPass.PixelShaderPath = "FinalOutput.hlsl";  m_displayPass.PixelShaderEntryPoint = "PS_ToneMap";
        m_displayPass.AddSampler("Smp", m_linearSampler);
        m_renderer.CompilePass(m_displayPass);

        m_highlightPass.VertexShaderPath = "highlight.hlsl"; m_highlightPass.VertexShaderEntryPoint = "VS_Main";
        m_highlightPass.PixelShaderPath = "highlight.hlsl";  m_highlightPass.PixelShaderEntryPoint = "PS_Main";
        m_renderer.CompilePass(m_highlightPass);
    }

    void SetupScene() {
        auto floor = m_scene.CreatePrimitive(PrimitiveType::Plane);
        floor->SetPosition(0, 0, 0); floor->SetColor(0.05f, 0.05f, 0.05f); floor->SetRoughness(1.0f);

        auto light = m_scene.CreatePrimitive(PrimitiveType::Box);
        light->SetPosition(8.0f, 10.0f, 8.0f); light->SetScale(3.0f, 0.1f, 3.0f);
        light->SetColor(1.0f, 0.9f, 0.8f); light->SetEmission(5.0f);

        int rows = 8, cols = 8; float sp = 2.5f;
        for (int z = 0; z < rows; z++) for (int x = 0; x < cols; x++) {
            auto s = m_scene.CreatePrimitive(PrimitiveType::Sphere);
            s->SetPosition(x * sp, 1.0f, z * sp); s->SetScale(0.9f);
            s->SetRoughness(std::max((float)x / (cols - 1), 0.04f)); s->SetMetalness((float)z / (rows - 1));
            s->SetColor(0.9f, 0.1f, 0.1f);
        }
        m_gpuBuffer = m_scene.GenerateGPUBuffer();
    }

    void ResetSimulation() {
        m_currentTileIndex = 0;
        m_frameIndex = 0;
        m_globalSeedTime = 1.0f;
        m_activeBuffer = 0;
        m_displayBuffer = 0;
        // Чистим историю, чтобы начать с нуля
        m_renderer.Clear(m_rtHistory[0], 0, 0, 0, 0);
        m_renderer.Clear(m_rtHistory[1], 0, 0, 0, 0);
    }

    void OnResize(int w, int h) {
        if (w <= 0 || h <= 0) return; // Защита от минимизации

        m_windowW = w;
        m_windowH = h;
        m_renderW = w;
        m_renderH = h;

        // ВАЖНО: Обновляем матрицы/данные для следующего кадра ImGui
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)w, (float)h);

        // Пытаемся добраться до бекенда и ресайзнуть его (если у Engine есть доступ)
        // Сейчас мы реализуем это в Шаге 2 и 3
        if (auto* backend = m_renderer.GetBackendAPI()) {
            backend->Resize(w, h);
        }
    }

    void StopRendering() {
        m_state = AppState::Config;
        m_isPaused = false;
    }

    // --- GUI Functions ---

    void DrawConfigUI() {
        // Окно настроек всегда по центру
        ImGui::SetNextWindowPos(ImVec2(m_windowW * 0.5f, m_windowH * 0.5f), ImGuiCond_Once, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(400, 300));

        ImGui::Begin("Render Settings", nullptr, ImGuiWindowFlags_NoResize);

        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Configuration Mode");
        ImGui::Separator();

        // Параметры
        ImGui::Text("Resolution: %dx%d", m_renderW, m_renderH);

        // Target Iterations
        ImGui::DragInt("Target Samples", &m_maxIterations, 1, 1, 100000);

        // Tile Size (16, 32, 64...)
        if (ImGui::BeginCombo("Tile Size", std::to_string(m_tileSize).c_str())) {
            int sizes[] = { 16, 32, 64, 128, 256 };
            for (int s : sizes) {
                bool isSelected = (m_tileSize == s);
                if (ImGui::Selectable(std::to_string(s).c_str(), isSelected)) m_tileSize = s;
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        // Speed settings
        ImGui::SliderInt("Batch Size", &m_tilesPerFrame, 1, 64, "%d tiles/frame");

        // Post Process (можно менять и в превью)
        ImGui::SliderFloat("Exposure", &m_currentExposure, 0.0f, 10.0f);

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        // Кнопка Старта
        // Центрируем кнопку
        float availW = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - 120) * 0.5f);
        if (ImGui::Button("START RENDER", ImVec2(120, 40))) {
            m_state = AppState::Rendering;
            ResetSimulation();
        }

        ImGui::End();
    }

    void DrawStatusUI() {
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 160));

        // Флаг NoTitleBar убрали, чтобы окно можно было таскать, но можно вернуть
        ImGui::Begin("Rendering Progress", nullptr, ImGuiWindowFlags_NoResize);

        // Инфо
        ImGui::Text("Progress: %d / %d samples", m_frameIndex, m_maxIterations);
        float progress = (float)m_frameIndex / (float)m_maxIterations;
        ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));

        ImGui::Text("Tile Index: %d", m_currentTileIndex);

        // Пост-процесс на лету
        ImGui::SliderFloat("Exposure", &m_currentExposure, 0.0f, 10.0f);

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        // Управление
        if (ImGui::Button(m_isPaused ? "RESUME" : "PAUSE", ImVec2(80, 0))) {
            m_isPaused = !m_isPaused;
        }

        ImGui::SameLine();

        if (ImGui::Button("STOP", ImVec2(80, 0))) {
            StopRendering();
        }

        ImGui::End();
    }

    // --- Main Logic ---

    void UpdateAndRender() {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Логика состояний GUI
        if (m_state == AppState::Config) {
            DrawConfigUI();
        }
        else if (m_state == AppState::Rendering) {
            DrawStatusUI();

            // Если достигли лимита
            if (m_frameIndex >= m_maxIterations) {
                StopRendering();
            }
        }

        // === ГРАФИЧЕСКИЙ ПАЙПЛАЙН ===

        // 1. Ray Tracing Pass (Только если идет рендеринг и не пауза)
        if (m_state == AppState::Rendering && !m_isPaused) {
            // Лимит тайлов на кадр (в конфиге выбираем, чтобы интерфейс не лагал)
            RenderPTBatches(m_tilesPerFrame);
        }

        // 2. Display Pass (Тонмаппинг) - вызываем ВСЕГДА, 
        // чтобы видеть картинку даже в меню конфига (как стоп-кадр)
        m_renderer.RenderPassToScreen();

        // В меню Config можно залить фон потемнее, чтобы окно выделялось
        if (m_state == AppState::Config) {
            // Например, можно вообще ничего не рисовать под меню, 
            // но мы оставим "призрака" прошлого рендера
        }

        RenderDisplayPass();

        // 3. Overlay (Красный квадратик тайла) - рисуем только при рендеринге
        if (m_state == AppState::Rendering && !m_isPaused) {
            RenderOverlayPass();
        }

        // 4. GUI Rendering
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        m_renderer.Present();
    }

    void RenderPTBatches(int batchCount) {
        m_renderer.SetPipelineState(m_stateTileRender);

        int tilesX = (m_renderW + m_tileSize - 1) / m_tileSize;
        int totalTiles = tilesX * ((m_renderH + m_tileSize - 1) / m_tileSize);

        // Опасность! Если ресайз окна сделан, буфер может не соответствовать
        // Для простоты предполагаем, что Resolution константно, меняем только тайлы.

        int readIdx = (m_activeBuffer == 0) ? 1 : 0;
        int writeIdx = m_activeBuffer;

        PTSceneData camData = CalculateCameraData();

        for (int b = 0; b < batchCount; b++) {
            // Конец кадра?
            if (m_currentTileIndex >= totalTiles) {
                m_currentTileIndex = 0;

                // Фиксируем результат
                m_displayBuffer = m_activeBuffer;
                // Меняем буфер
                m_activeBuffer = !m_activeBuffer;

                m_frameIndex++;
                break; // Дадим шанс интерфейсу отрисоваться
            }

            int tx = m_currentTileIndex % tilesX;
            int ty = m_currentTileIndex / tilesX;

            // Update Constant Data
            m_globalSeedTime += 1.61803f;
            camData.Params.x = m_globalSeedTime;
            camData.Params.z = (float)m_frameIndex;

            // Setup Render
            m_renderer.SetRenderTarget(m_rtHistory[writeIdx]);

            // Внимание: мы берем картинку для чтения из readIdx, 
            // которая содержит данные ПРОШЛОГО ПОЛНОГО прохода (если мы правильно свапаем).
            // Или содержит данные предыдущей пачки тайлов этого прохода?
            // Ping-Pong история работает корректно, когда читаем Полный Прошлый кадр.
            m_ptPass.AddTexture("TexHistory", m_rtHistory[readIdx]);
            m_renderer.SetShaderPass(m_ptPass);

            m_renderer.SetCustomConstant("SceneBuffer", camData);
            m_renderer.SetCustomConstant("ObjectBuffer", m_gpuBuffer);

            // Важно: Использование динамического размера тайла
            m_renderer.SetScissor(tx * m_tileSize, ty * m_tileSize, m_tileSize, m_tileSize);
            m_renderer.DrawFullScreenQuad();

            m_currentTileIndex++;
        }
    }

    void RenderDisplayPass() {
        m_renderer.SetPipelineState(m_stateFullScreen);
        m_displayPass.AddTexture("TexHDR", m_rtHistory[m_displayBuffer]);
        PostProcessData ppData = { m_currentExposure };
        m_renderer.SetCustomConstant("PostProcessParams", ppData);
        m_renderer.SetShaderPass(m_displayPass);
        m_renderer.DrawFullScreenQuad();
    }

    void RenderOverlayPass() {
        m_renderer.SetPipelineState(m_stateUI);

        int tilesX = (m_renderW + m_tileSize - 1) / m_tileSize;
        HighlightCB hlParams = {
            (float)m_currentTileIndex,
            (float)tilesX,
            (float)m_tileSize, // <--- Передаем динамический размер тайла в шейдер!
            0
        };

        m_renderer.SetCustomConstant("HighlightParams", hlParams);
        m_renderer.SetShaderPass(m_highlightPass);
        m_renderer.DrawFullScreenQuad();
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
