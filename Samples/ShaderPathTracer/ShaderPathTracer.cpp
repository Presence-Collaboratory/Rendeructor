#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <memory> // Для std::unique_ptr (умное управление памятью)

#include <Rendeructor.h>

#pragma comment( lib, "winmm.lib")  
#pragma comment( lib, "Rendeructor.lib") 

using namespace Math;

// =========================================================
// GPU DATA STRUCTURES (Must match HLSL)
// =========================================================

const int MAX_OBJECTS = 64;
const int TILE_SIZE = 64;
const int SSAA_FACTOR = 2;

enum class PrimitiveType {
    Sphere = 0,
    Box = 1,
    Plane = 2
};

// "Сырая" структура для отправки в шейдер
struct SDFObjectGPU {
    Math::float4 PositionAndType; // .xyz = Pos, .w = Type
    Math::float4 SizeAndPadding;  // .xyz = Size, .w = Padding
    Math::float4 Rotation;        // .xyz = Radians, .w = Padding
    Math::float4 Color;           // .xyz = RGB, .w = Reflectivity/Spec
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
    Math::float4   Params;
};

// =========================================================
// CLASSES
// =========================================================

// 1. Базовый класс объекта (Трансформация)
class Object {
public:
    Object(int id) : m_id(id) {}
    virtual ~Object() = default;

    void SetPosition(const float3& pos) { m_position = pos; }
    void SetPosition(float x, float y, float z) { m_position = float3(x, y, z); }

    // Вращение задаем в градусах для удобства
    void SetRotation(const float3& rotDeg) { m_rotationDeg = rotDeg; }
    void SetRotation(float x, float y, float z) { m_rotationDeg = float3(x, y, z); }

    void SetScale(const float3& scale) { m_scale = scale; }
    void SetScale(float s) { m_scale = float3(s, s, s); }
    void SetScale(float x, float y, float z) { m_scale = float3(x, y, z); }

    int GetID() const { return m_id; }
    float3 GetPosition() const { return m_position; }
    float3 GetRotation() const { return m_rotationDeg; }
    float3 GetScale() const { return m_scale; }

protected:
    int m_id;
    float3 m_position = { 0, 0, 0 };
    float3 m_rotationDeg = { 0, 0, 0 }; // Храним в градусах
    float3 m_scale = { 1, 1, 1 };
};

// 2. Геометрический примитив (SDF свойства + упаковка для GPU)
class GeometryPrimitive : public Object {
public:
    GeometryPrimitive(int id, PrimitiveType type) : Object(id), m_type(type) {}

    void SetColor(const float3& color) { m_color = color; }
    void SetColor(float r, float g, float b) { m_color = float3(r, g, b); }

    void SetReflectivity(float r) { m_reflectivity = r; }

    // Метод упаковки данных в формат, понятный шейдеру
    SDFObjectGPU GetGPUData() const {
        SDFObjectGPU data;

        // 1. Position & Type
        data.PositionAndType = float4(m_position.x, m_position.y, m_position.z, (float)m_type);

        // 2. Size (Scale)
        data.SizeAndPadding = float4(m_scale.x, m_scale.y, m_scale.z, 0.0f);

        // 3. Rotation (Конвертируем Градусы -> Радианы прямо перед отправкой)
        float radX = m_rotationDeg.x * 3.14159f / 180.0f;
        float radY = m_rotationDeg.y * 3.14159f / 180.0f;
        float radZ = m_rotationDeg.z * 3.14159f / 180.0f;
        data.Rotation = float4(radX, radY, radZ, 0.0f);

        // 4. Color & Material
        data.Color = float4(m_color.x, m_color.y, m_color.z, m_reflectivity);

        return data;
    }

private:
    PrimitiveType m_type;
    float3 m_color = { 1, 1, 1 };
    float m_reflectivity = 0.0f;
};

// 3. Сцена (Менеджер объектов)
class Scene {
public:
    Scene() = default;

    // Фабричный метод для создания объектов
    GeometryPrimitive* CreatePrimitive(PrimitiveType type) {
        if (m_primitives.size() >= MAX_OBJECTS) return nullptr;

        int newID = (int)m_primitives.size();
        auto newPrim = std::make_unique<GeometryPrimitive>(newID, type);
        GeometryPrimitive* ptr = newPrim.get();

        m_primitives.push_back(std::move(newPrim));
        return ptr;
    }

    // Генерация буфера для шейдера
    SceneObjectsBuffer GenerateGPUBuffer() const {
        SceneObjectsBuffer buffer;
        buffer.ObjectCount = (int)m_primitives.size();

        for (int i = 0; i < buffer.ObjectCount; i++) {
            buffer.Objects[i] = m_primitives[i]->GetGPUData();
        }
        return buffer;
    }

    void Clear() {
        m_primitives.clear();
    }

private:
    std::vector<std::unique_ptr<GeometryPrimitive>> m_primitives;
};

// =========================================================
// MAIN
// =========================================================

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Настройки
    int WindowW = 1280;
    int WindowH = 720;
    int RenderW = WindowW * SSAA_FACTOR;
    int RenderH = WindowH * SSAA_FACTOR;

    // Окно
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW,
        [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
            if (m == WM_DESTROY) PostQuitMessage(0); return DefWindowProc(h, m, w, l); },
        0, 0, hInstance, nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, "OOPRenderer", nullptr };
    RegisterClassEx(&wc);
    HWND hwnd = CreateWindowEx(0, "OOPRenderer", "Initializing...", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, WindowW, WindowH, nullptr, nullptr, hInstance, nullptr);

    // Rendeructor
    Rendeructor renderer;
    BackendConfig config;
    config.Width = WindowW; 
    config.Height = WindowH; 
    config.WindowHandle = hwnd; 
    config.API = RenderAPI::DirectX11;

    if (!renderer.Create(config)) 
        return 0;

    Texture rtHDR; 
    rtHDR.Create(RenderW, RenderH, TextureFormat::RGBA32F);

    Sampler linearSampler; 
    linearSampler.Create("Linear");

    // PSO
    PipelineState stateTileRender; 
    stateTileRender.Cull = CullMode::None; 
    stateTileRender.DepthWrite = false; 
    stateTileRender.DepthFunc = CompareFunc::Always; 
    stateTileRender.ScissorTest = true;

    PipelineState stateFullScreen; 
    stateFullScreen.Cull = CullMode::None; 
    stateFullScreen.DepthWrite = false; 
    stateFullScreen.DepthFunc = CompareFunc::Always; 
    stateFullScreen.ScissorTest = false;

    // Shaders
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
    displayPass.AddSampler("Smp", linearSampler); 
    renderer.CompilePass(displayPass);

    // ==========================================
    // СОЗДАНИЕ СЦЕНЫ (НОВЫЙ ПОДХОД)
    // ==========================================
    Scene myScene;

    // 1. Пол
    auto floor = myScene.CreatePrimitive(PrimitiveType::Plane);
    floor->SetPosition(0, 0, 0); // Высота 0
    floor->SetColor(0.5f, 0.5f, 0.5f);

    // 2. Красная Сфера
    auto sphere = myScene.CreatePrimitive(PrimitiveType::Sphere);
    sphere->SetPosition(-1.2f, 1.0f, 0.0f);
    sphere->SetScale(1.0f); // Радиус 1.0
    sphere->SetColor(0.8f, 0.1f, 0.1f);

    // 3. Зеленый Куб (Повернутый)
    auto box = myScene.CreatePrimitive(PrimitiveType::Box);
    box->SetPosition(1.5f, 1.0f, 0.0f);
    box->SetScale(0.8f);
    box->SetRotation(0.0f, 45.0f, 0.0f); // 45 градусов по Y
    box->SetColor(0.1f, 0.8f, 0.2f);

    // 4. Синий маленький кубик сверху
    auto smallBox = myScene.CreatePrimitive(PrimitiveType::Box);
    smallBox->SetPosition(0.0f, 0.5f, 0.0f);
    smallBox->SetScale(0.3f);
    smallBox->SetRotation(45.0f, 45.0f, 0.0f);
    smallBox->SetColor(0.1f, 0.2f, 0.9f);

    // 5. Добавим еще что-нибудь, например высокую платформу
    auto tower = myScene.CreatePrimitive(PrimitiveType::Box);
    tower->SetPosition(-2.5f, 2.0f, 2.0f);
    tower->SetScale(0.5f, 2.0f, 0.5f); // Высокий параллелепипед
    tower->SetColor(0.9f, 0.6f, 0.1f); // Оранжевый

    // ==========================================

    // Подготовка к рендеру
    PTSceneData sceneData;
    sceneData.Resolution = float4((float)RenderW, (float)RenderH, 0, 0);
    Math::float3 camPos = { 0.0f, 2.5f, -5.0f };
    Math::float3 camTarget = { 0.0f, 1.0f, 0.0f };

    int tilesX = (RenderW + TILE_SIZE - 1) / TILE_SIZE;
    int tilesY = (RenderH + TILE_SIZE - 1) / TILE_SIZE;
    int totalTiles = tilesX * tilesY;
    int currentTileIndex = 0;

    renderer.Clear(rtHDR, 0, 0, 0, 1);

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        else {
            if (GetAsyncKeyState(VK_SPACE) & 0x8000) {
                currentTileIndex = 0;
                renderer.Clear(rtHDR, 0, 0, 0, 1);
                // Можно здесь даже изменить сцену, например:
                // sphere->SetPosition(...)
            }

            if (currentTileIndex < totalTiles) {
                char title[128]; sprintf_s(title, "Rendering Tile: %d / %d", currentTileIndex + 1, totalTiles); SetWindowText(hwnd, title);

                int tx = currentTileIndex % tilesX; int ty = currentTileIndex / tilesX;

                // Камера
                Math::float3 forward = (camTarget - camPos).normalize();
                Math::float3 right = Math::float3(0, 1, 0).cross(forward).normalize();
                Math::float3 up = forward.cross(right).normalize();
                float fovY = 3.14f / 3.0f;
                float ar = (float)RenderW / RenderH;
                float thf = tan(fovY * 0.5f);
                Math::float3 rr = right * thf * ar; Math::float3 ru = up * thf;

                sceneData.CameraPos = float4(camPos.x, camPos.y, camPos.z, 1.0f);
                sceneData.CameraDir = float4(forward.x, forward.y, forward.z, 0.0f);
                sceneData.CameraRight = float4(rr.x, rr.y, rr.z, 0.0f);
                sceneData.CameraUp = float4(ru.x, ru.y, ru.z, 0.0f);
                sceneData.Params.x = 0.5f;

                renderer.SetRenderTarget(rtHDR);
                renderer.SetPipelineState(stateTileRender);
                renderer.SetScissor(tx * TILE_SIZE, ty * TILE_SIZE, TILE_SIZE, TILE_SIZE);

                renderer.SetShaderPass(ptPass);
                renderer.SetCustomConstant("SceneBuffer", sceneData);

                SceneObjectsBuffer gpuBuffer = myScene.GenerateGPUBuffer();
                renderer.SetCustomConstant("ObjectBuffer", gpuBuffer);

                renderer.DrawFullScreenQuad();
                currentTileIndex++;
            }
            else {
                Sleep(16);
            }

            renderer.RenderPassToScreen();
            renderer.SetPipelineState(stateFullScreen);
            renderer.SetShaderPass(displayPass);
            renderer.DrawFullScreenQuad();
            renderer.Present();
        }
    }
    renderer.Destroy();
    return 0;
}
