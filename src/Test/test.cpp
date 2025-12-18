#include "pch.h"
#include <windows.h>
#include <iostream>
#include <vector>
#include <cmath>

#include "Rendeructor.h"

using namespace Math;

float RandomFloat() { return (float)rand() / (float)RAND_MAX; }
float Lerp(float a, float b, float f) { return a + f * (b - a); }

float Halton(int index, int base) {
    float f = 1.0f; float r = 0.0f;
    while (index > 0) { f /= (float)base; r += f * (index % base); index /= base; }
    return r;
}

struct SSAOConfig {
    Math::float4x4 View;
    Math::float4x4 Projection;
    Math::float4 Resolution;
    Math::float4 CameraPosition;
    Math::float4 Kernel[64];
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW,
        [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
            if (m == WM_DESTROY) PostQuitMessage(0); return DefWindowProc(h, m, w, l); },
        0, 0, hInstance, nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, "GClass", nullptr };
    RegisterClassEx(&wc);

    int W = 1600, H = 900;
    HWND hwnd = CreateWindowEx(0, "GClass", "Massive Instancing Demo (Pipeline States)", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, W, H, nullptr, nullptr, hInstance, nullptr);

    Rendeructor renderer;
    BackendConfig config; config.Width = W; config.Height = H; config.WindowHandle = hwnd; config.API = RenderAPI::DirectX11;
    if (!renderer.Create(config)) return 0;

    // --- РЕСУРСЫ ---
    Mesh objectMesh;
    if (!objectMesh.LoadFromOBJ("teapot.obj")) Mesh::GenerateSphere(objectMesh, 1.0f, 24, 16);
    Mesh floorMesh; Mesh::GeneratePlane(floorMesh, 1000.0f, 1000.0f);

    // --- ГЕНЕРАЦИЯ ИНСТАНСОВ ---
    int gridSize = 100; float spacing = 6.0f;
    std::vector<Math::float4x4> instancesData; instancesData.reserve(gridSize * gridSize);
    for (int z = 0; z < gridSize; ++z) {
        for (int x = 0; x < gridSize; ++x) {
            float posY = sin(x * 0.1f) * cos(z * 0.1f) * 2.0f + 2.0f;
            Math::float4x4 world = Math::float4x4::rotation_y(((x + z) % 2 == 0) ? (x * 0.1f) : 0.0f) *
                Math::float4x4::translation((x - gridSize / 2.0f) * spacing, posY, (z - gridSize / 2.0f) * spacing);
            instancesData.push_back(world);
        }
    }
    InstanceBuffer instanceBuffer;
    instanceBuffer.Create(instancesData.data(), (int)instancesData.size(), sizeof(Math::float4x4));

    // --- ТЕКСТУРЫ И ШЕЙДЕРЫ ---
    // (Инициализация такая же, как в оригинале - сокращено для краткости чтения, ресурсы те же)
    Texture rtAlbedo, rtPos, rtNorm, rtSSAORaw, rtSSAODenoised, rtShadow;
    rtAlbedo.Create(W, H, TextureFormat::RGBA8); rtPos.Create(W, H, TextureFormat::RGBA16F); rtNorm.Create(W, H, TextureFormat::RGBA16F);
    rtSSAORaw.Create(W / 2, H / 2, TextureFormat::RGBA8); rtSSAODenoised.Create(W, H, TextureFormat::RGBA8); rtShadow.Create(4096, 4096, TextureFormat::R32F);

    Texture noiseTexture; // Заполнение шумом...
    std::vector<float4> noiseData(16); for (int i = 0; i < 16; i++) noiseData[i] = float4(RandomFloat() * 2 - 1, RandomFloat() * 2 - 1, 0, 0);
    noiseTexture.Create(4, 4, TextureFormat::RGBA16F, noiseData.data());

    Sampler smpLin; smpLin.Create("Linear");
    Sampler smpPt; smpPt.Create("Point");

    // Подготовка Passes...
    ShaderPass shadowInstPass, shadowStaticPass, gbufInstPass, gbufStaticPass, ssaoPass, denoisePass, combinePass;
    // ...Заполнение путей шейдеров идентично оригиналу...
    // Для экономии места опустим повтор строк .VertexShaderPath = ..., полагая что они заполнены как в прошлом коде
    // Но убедитесь, что они инициализированы (здесь пропуск только для наглядности стейтов!)

    // FILL PASSES DATA (As in original code)
    shadowInstPass.VertexShaderPath = "Shader.hlsl"; shadowInstPass.VertexShaderEntryPoint = "VS_ShadowInstanced"; shadowInstPass.PixelShaderPath = "Shader.hlsl"; shadowInstPass.PixelShaderEntryPoint = "PS_Shadow"; renderer.CompilePass(shadowInstPass);
    shadowStaticPass.VertexShaderPath = "Shader.hlsl"; shadowStaticPass.VertexShaderEntryPoint = "VS_Shadow"; shadowStaticPass.PixelShaderPath = "Shader.hlsl"; shadowStaticPass.PixelShaderEntryPoint = "PS_Shadow"; renderer.CompilePass(shadowStaticPass);
    gbufInstPass.VertexShaderPath = "Shader.hlsl"; gbufInstPass.VertexShaderEntryPoint = "VS_MeshInstanced"; gbufInstPass.PixelShaderPath = "Shader.hlsl"; gbufInstPass.PixelShaderEntryPoint = "PS_GBuffer"; renderer.CompilePass(gbufInstPass);
    gbufStaticPass.VertexShaderPath = "Shader.hlsl"; gbufStaticPass.VertexShaderEntryPoint = "VS_Mesh"; gbufStaticPass.PixelShaderPath = "Shader.hlsl"; gbufStaticPass.PixelShaderEntryPoint = "PS_GBuffer"; renderer.CompilePass(gbufStaticPass);

    ssaoPass.VertexShaderPath = "Shader.hlsl"; ssaoPass.VertexShaderEntryPoint = "VS_Quad"; ssaoPass.PixelShaderPath = "Shader.hlsl"; ssaoPass.PixelShaderEntryPoint = "PS_SSAO_Raw";
    ssaoPass.AddTexture("TexPosition", rtPos); ssaoPass.AddTexture("TexNormal", rtNorm); ssaoPass.AddTexture("TexNoise", noiseTexture); ssaoPass.AddSampler("SamplerClamp", smpLin); ssaoPass.AddSampler("SamplerPoint", smpPt); renderer.CompilePass(ssaoPass);

    denoisePass.VertexShaderPath = "Shader.hlsl"; denoisePass.VertexShaderEntryPoint = "VS_Quad"; denoisePass.PixelShaderPath = "Shader.hlsl"; denoisePass.PixelShaderEntryPoint = "PS_Denoise";
    denoisePass.AddTexture("TexSSAO_Raw", rtSSAORaw); denoisePass.AddSampler("SamplerClamp", smpLin); renderer.CompilePass(denoisePass);

    combinePass.VertexShaderPath = "Shader.hlsl"; combinePass.VertexShaderEntryPoint = "VS_Quad"; combinePass.PixelShaderPath = "Shader.hlsl"; combinePass.PixelShaderEntryPoint = "PS_Combine";
    combinePass.AddTexture("TexAlbedo", rtAlbedo); combinePass.AddTexture("TexSSAO", rtSSAODenoised); combinePass.AddTexture("TexPosWorld", rtPos); combinePass.AddTexture("TexNormalWorld", rtNorm); combinePass.AddTexture("TexShadow", rtShadow); combinePass.AddSampler("SamplerClamp", smpLin); renderer.CompilePass(combinePass);


    SSAOConfig ssaoConfig;
    ssaoConfig.Resolution = float4((float)W, (float)H, 0, 0);
    for (int i = 0; i < 64; ++i) { /* Заполнение Kernel... */ ssaoConfig.Kernel[i] = float4(0, 0, 0, 0); } // Заглушка, код из оригинала

    Math::float3 lightPos = { 100.0f, 200.0f, -100.0f };
    Math::float4x4 lightVP = Math::float4x4::look_at_lh(lightPos, { 0,0,0 }, { 0,1,0 }) * Math::float4x4::orthographic_lh_zo(250.0f, 250.0f, 10.0f, 500.0f);
    Math::float4x4 proj = Math::float4x4::perspective_lh_zo(3.14159f / 4.0f, (float)W / H, 0.5f, 500.0f);


    // ==========================================
    // ОПРЕДЕЛЕНИЕ PIPELINE STATES (ИЗМЕНЕНИЕ!)
    // ==========================================

    // 1. Стандартный 3D (Opaque, Back Cull, Depth Read/Write)
    // Используем для ShadowMap и G-Buffer
    PipelineState stateScene;
    stateScene.Cull = CullMode::Back;
    stateScene.Blend = BlendMode::Opaque;
    stateScene.DepthWrite = true;
    stateScene.DepthFunc = CompareFunc::Less;

    // 2. Пост-процессинг / Full Screen Quad (No Cull, No Depth Write)
    // Используем для SSAO, Denoise, Combine
    PipelineState statePostProcess;
    statePostProcess.Cull = CullMode::None;      // Для FS Quad
    statePostProcess.Blend = BlendMode::Opaque;
    statePostProcess.DepthWrite = false;         // Не пишем в глубину при обработке картинки
    statePostProcess.DepthFunc = CompareFunc::Always; // Игнорируем проверку (или LessEqual)

    MSG msg = {};
    float time = 0;
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        else {
            time += 0.005f;
            Math::float3 camPos = { sin(time * 0.5f) * 50.0f, 5.0f, cos(time * 0.5f) * 50.0f };
            Math::float4x4 view = Math::float4x4::look_at_lh(camPos, { 0,0,0 }, { 0,1,0 });
            ssaoConfig.View = view; ssaoConfig.Projection = proj; ssaoConfig.CameraPosition = Math::float4(camPos.x, camPos.y, camPos.z, 1);

            // =========================
            // 1. SHADOW PASS (INSTANCED)
            // =========================
            renderer.SetRenderTarget(rtShadow);
            renderer.Clear(rtShadow, 1, 1, 1, 1);
            renderer.ClearDepth(1.0f);

            // <--- ИЗМЕНЕНИЕ: Вместо SetCullMode и SetDepthWrite ставим структуру
            renderer.SetPipelineState(stateScene);

            renderer.SetConstant("ViewProjection", lightVP);
            renderer.SetShaderPass(shadowInstPass);
            renderer.DrawMeshInstanced(objectMesh, instanceBuffer);

            renderer.SetShaderPass(shadowStaticPass);
            renderer.SetConstant("World", Math::float4x4::identity());
            renderer.DrawMesh(floorMesh);

            // =========================
            // 2. G-BUFFER (INSTANCED)
            // =========================
            renderer.SetRenderTarget(rtAlbedo, rtPos, rtNorm);
            renderer.Clear(0, 0, 0, 1);
            renderer.ClearDepth();

            // Здесь состояние такое же (state3D), но для надежности можно выставить снова
            renderer.SetPipelineState(stateScene);

            renderer.SetConstant("ViewProjection", view * proj);

            renderer.SetShaderPass(gbufInstPass);
            renderer.DrawMeshInstanced(objectMesh, instanceBuffer);

            renderer.SetShaderPass(gbufStaticPass);
            renderer.SetConstant("World", Math::float4x4::identity());
            renderer.DrawMesh(floorMesh);

            // =========================
            // 3. SSAO & POST PROCESS
            // =========================

            // <--- ИЗМЕНЕНИЕ: Переключаемся на режим обработки (нет depth write)
            renderer.SetPipelineState(statePostProcess);

            // -- Raw SSAO --
            renderer.SetRenderTarget(rtSSAORaw);
            renderer.Clear(1, 1, 1, 1);
            renderer.SetShaderPass(ssaoPass);
            renderer.SetCustomConstant("SSAOConfigBuffer", ssaoConfig);
            renderer.DrawFullScreenQuad();

            // -- Denoise --
            renderer.SetRenderTarget(rtSSAODenoised);
            // State все тот же, не меняем
            renderer.SetShaderPass(denoisePass);
            renderer.DrawFullScreenQuad();

            // -- Combine to Screen --
            renderer.RenderPassToScreen();
            renderer.Clear(0.2f, 0.2f, 0.2f, 1);

            // И здесь то же состояние для рисования квада
            // (можно убрать, так как statePostProcess уже установлен выше)
            // renderer.SetPipelineState(statePostProcess); 

            renderer.SetShaderPass(combinePass);
            renderer.SetConstant("LightViewProjection", lightVP);
            renderer.SetCustomConstant("SSAOConfigBuffer", ssaoConfig);
            renderer.DrawFullScreenQuad();

            renderer.Present();
        }
    }
    renderer.Destroy();
    return 0;
}

