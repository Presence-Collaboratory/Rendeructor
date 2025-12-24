#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <memory>

#include <Rendeructor.h>

#pragma comment( lib, "winmm.lib")  
#pragma comment( lib, "Rendeructor.lib") 

using namespace Math;

// =========================================================
// GPU DATA STRUCTURES
// =========================================================

// !!! ВАЖНО: Увеличили лимит, так как 8x8 = 64, плюс пол и свет = 66
const int MAX_OBJECTS = 128;

const int TILE_SIZE = 64;
const int SSAA_FACTOR = 2;

enum class PrimitiveType {
    Sphere = 0,
    Box = 1,
    Plane = 2
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
    Math::float4   Params;
};

// =========================================================
// CLASSES
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
// MAIN
// =========================================================

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    int WindowW = 1280;
    int WindowH = 720;
    int RenderW = WindowW * SSAA_FACTOR;
    int RenderH = WindowH * SSAA_FACTOR;

    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW,
        [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
            if (m == WM_DESTROY) PostQuitMessage(0); return DefWindowProc(h, m, w, l); },
        0, 0, hInstance, nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, "OOPRenderer", nullptr };
    RegisterClassEx(&wc);
    HWND hwnd = CreateWindowEx(0, "OOPRenderer", "Initializing...", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, WindowW, WindowH, nullptr, nullptr, hInstance, nullptr);

    Rendeructor renderer;
    BackendConfig config;
    config.Width = WindowW; config.Height = WindowH; config.WindowHandle = hwnd; config.API = RenderAPI::DirectX11;
    if (!renderer.Create(config)) return 0;

    Texture rtHDR; rtHDR.Create(RenderW, RenderH, TextureFormat::RGBA32F);
    Sampler linearSampler; linearSampler.Create("Linear");

    PipelineState stateTileRender; stateTileRender.Cull = CullMode::None; stateTileRender.DepthWrite = false; stateTileRender.DepthFunc = CompareFunc::Always; stateTileRender.ScissorTest = true;
    PipelineState stateFullScreen; stateFullScreen.Cull = CullMode::None; stateFullScreen.DepthWrite = false; stateFullScreen.DepthFunc = CompareFunc::Always; stateFullScreen.ScissorTest = false;

    ShaderPass ptPass; ptPass.VertexShaderPath = "PathTracer.hlsl"; ptPass.VertexShaderEntryPoint = "VS_Quad"; ptPass.PixelShaderPath = "PathTracer.hlsl"; ptPass.PixelShaderEntryPoint = "PS_PathTrace"; renderer.CompilePass(ptPass);
    ShaderPass displayPass; displayPass.VertexShaderPath = "FinalOutput.hlsl"; displayPass.VertexShaderEntryPoint = "VS_Quad"; displayPass.PixelShaderPath = "FinalOutput.hlsl"; displayPass.PixelShaderEntryPoint = "PS_ToneMap"; displayPass.AddTexture("TexHDR", rtHDR); displayPass.AddSampler("Smp", linearSampler); renderer.CompilePass(displayPass);

    // ==========================================
    // ГЕНЕРАЦИЯ СЦЕНЫ: PBR CHART
    // ==========================================
    Scene myScene;

    // 1. ПОЛ (Темно-серый)
    auto floor = myScene.CreatePrimitive(PrimitiveType::Plane);
    floor->SetPosition(0, 0, 0);
    floor->SetColor(0.1f, 0.1f, 0.1f);
    floor->SetRoughness(1.0f);

    // 2. ИСТОЧНИК СВЕТА СВЕРХУ
    // Большая плита над всей сценой (Softbox)
    auto light = myScene.CreatePrimitive(PrimitiveType::Box);
    light->SetPosition(8.0f, 10.0f, 8.0f); // По центру сетки (сетка от 0 до 16 примерно)
    light->SetScale(1.0f, 0.1f, 1.0f);   // 20x20 метров
    light->SetColor(1.0f, 1.0f, 1.0f);
    light->SetEmission(50.0f);

    // 3. МАССИВ СФЕР 8x8
    int rows = 8; // Z axis (Metalness)
    int cols = 8; // X axis (Roughness)
    float spacing = 2.5f;

    // Сдвинем сетку так, чтобы (0,0) был где-то удобным
    for (int z = 0; z < rows; ++z) {
        for (int x = 0; x < cols; ++x) {
            auto sphere = myScene.CreatePrimitive(PrimitiveType::Sphere);
            if (!sphere) break;

            float posX = x * spacing;
            float posZ = z * spacing;

            sphere->SetPosition(posX, 1.0f, posZ);
            sphere->SetScale(0.9f); // Радиус чуть меньше 1

            // ПАРАМЕТРЫ МАТЕРИАЛОВ
            // X (0..7) -> Roughness (0.0 .. 1.0)
            float roughness = (float)x / (float)(cols - 1);
            // Если roughness = 0, могут быть артефакты в мат. моделях, лучше клампить 0.05
            roughness = std::max(roughness, 0.05f);

            // Z (0..7) -> Metalness (0.0 .. 1.0)
            float metalness = (float)z / (float)(rows - 1);

            sphere->SetRoughness(roughness);
            sphere->SetMetalness(metalness);

            // Цвет шариков: Единый красный, чтобы видеть разницу бликов
            sphere->SetColor(0.9f, 0.1f, 0.1f);
        }
    }

    // ==========================================
    // НАСТРОЙКА КАМЕРЫ
    // ==========================================
    PTSceneData sceneData;
    sceneData.Resolution = float4((float)RenderW, (float)RenderH, 0, 0);

    // Центр сетки примерно в (8*2.5 / 2, 0, 8*2.5 / 2) ~ (10, 0, 10)
    Math::float3 gridCenter = { 9.0f, 0.0f, 9.0f };

    // Смотрим сверху-сбоку
    Math::float3 camPos = { 9.0f, 20.0f, -8.0f };
    Math::float3 camTarget = { 9.0f, 0.0f, 12.0f }; // Смотрим чуть вглубь сетки

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
            }

            if (currentTileIndex < totalTiles) {
                char title[128]; sprintf_s(title, "Rendering: %d%%", (currentTileIndex * 100) / totalTiles); SetWindowText(hwnd, title);
                int tx = currentTileIndex % tilesX; int ty = currentTileIndex / tilesX;

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
            else { SetWindowText(hwnd, "Done"); Sleep(16); }

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
