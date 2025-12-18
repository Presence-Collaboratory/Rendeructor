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

// Конфиг SSAO
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
    HWND hwnd = CreateWindowEx(0, "GClass", "Massive Instancing Demo (10,000 Spheres)", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, W, H, nullptr, nullptr, hInstance, nullptr);

    Rendeructor renderer;
    BackendConfig config; config.Width = W; config.Height = H; config.WindowHandle = hwnd; config.API = RenderAPI::DirectX11;
    if (!renderer.Create(config)) return 0;

    // --- 1. РЕСУРСЫ ---
    // Чайник (или Сфера для стабильности, если файла нет)
    Mesh objectMesh;
    if (!objectMesh.LoadFromOBJ("teapot.obj")) {
        Mesh::GenerateSphere(objectMesh, 1.0f, 24, 16);
    }
    // Пол
    Mesh floorMesh;
    Mesh::GeneratePlane(floorMesh, 1000.0f, 1000.0f);

    // --- 2. ГЕНЕРАЦИЯ ИНСТАНСОВ ---
    // Создаем сетку 100x100 = 10 000 объектов!
    int gridSize = 100;
    float spacing = 6.0f; // Расстояние между объектами

    std::vector<Math::float4x4> instancesData;
    instancesData.reserve(gridSize * gridSize);

    for (int z = 0; z < gridSize; ++z) {
        for (int x = 0; x < gridSize; ++x) {
            float posX = (x - gridSize / 2.0f) * spacing;
            float posZ = (z - gridSize / 2.0f) * spacing;

            // Немного волнистости по высоте для красоты
            float posY = sin(x * 0.1f) * cos(z * 0.1f) * 2.0f + 2.0f;

            // Поворот каждый второй объект
            float rotY = ((x + z) % 2 == 0) ? (x * 0.1f) : 0.0f;

            Math::float4x4 world = Math::float4x4::rotation_y(rotY) *
                Math::float4x4::translation(posX, posY, posZ);
            instancesData.push_back(world);
        }
    }

    // Загружаем данные в GPU (Буфер Инстансов)
    // Stride = sizeof(Math::float4x4), Count = vector.size()
    InstanceBuffer instanceBuffer;
    instanceBuffer.Create(instancesData.data(), (int)instancesData.size(), sizeof(Math::float4x4));

    // --- 3. ТЕКСТУРЫ ---
    Texture rtAlbedo, rtPos, rtNorm, rtSSAORaw, rtSSAODenoised, rtShadow;
    rtAlbedo.Create(W, H, TextureFormat::RGBA8);
    rtPos.Create(W, H, TextureFormat::RGBA16F);
    rtNorm.Create(W, H, TextureFormat::RGBA16F);
    rtSSAORaw.Create(W / 2, H / 2, TextureFormat::RGBA8); // Можно меньше для производительности
    rtSSAODenoised.Create(W, H, TextureFormat::RGBA8);
    rtShadow.Create(4096, 4096, TextureFormat::R32F);

    // SSAO Setup
    SSAOConfig ssaoConfig;
    ssaoConfig.Resolution = float4((float)W, (float)H, 0, 0);

    for (int i = 0; i < 64; ++i) {
        float h1 = Halton(i, 2); float h2 = Halton(i, 3);
        float phi = h1 * 2.0f * 3.14159f;
        float cosTheta = 1.0f - h2; float sinTheta = sqrt(1.0f - cosTheta * cosTheta);
        float3 sample(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
        sample *= Lerp(0.1f, 1.0f, ((float)i / 64.0f) * ((float)i / 64.0f));
        ssaoConfig.Kernel[i] = float4(sample.x, sample.y, sample.z, 0.0f);
    }

    std::vector<float4> noiseData(16);
    for (int i = 0; i < 16; i++) {
        float3 noise(RandomFloat() * 2.0f - 1.0f, RandomFloat() * 2.0f - 1.0f, 0.0f);
        noise = noise.normalize();
        noiseData[i] = float4(noise.x, noise.y, noise.z, 0.0f);
    }
    Texture noiseTexture;
    noiseTexture.Create(4, 4, TextureFormat::RGBA16F, noiseData.data());

    // Семплеры
    Sampler smpLin, smpPt;
    smpLin.Create("Linear"); smpPt.Create("Point");

    // --- 4. ШЕЙДЕРЫ ---

    // A) Shadow (Instanced!)
    ShaderPass shadowInstPass;
    shadowInstPass.VertexShaderPath = "Shader.hlsl"; shadowInstPass.VertexShaderEntryPoint = "VS_ShadowInstanced"; // <--- НОВЫЙ VS
    shadowInstPass.PixelShaderPath = "Shader.hlsl"; shadowInstPass.PixelShaderEntryPoint = "PS_Shadow";
    renderer.CompilePass(shadowInstPass);

    // Обычный Shadow pass для пола
    ShaderPass shadowStaticPass;
    shadowStaticPass.VertexShaderPath = "Shader.hlsl"; shadowStaticPass.VertexShaderEntryPoint = "VS_Shadow";
    shadowStaticPass.PixelShaderPath = "Shader.hlsl";  shadowStaticPass.PixelShaderEntryPoint = "PS_Shadow";
    renderer.CompilePass(shadowStaticPass);

    // B) GBuffer (Instanced!)
    ShaderPass gbufInstPass;
    gbufInstPass.VertexShaderPath = "Shader.hlsl"; gbufInstPass.VertexShaderEntryPoint = "VS_MeshInstanced"; // <--- НОВЫЙ VS
    gbufInstPass.PixelShaderPath = "Shader.hlsl"; gbufInstPass.PixelShaderEntryPoint = "PS_GBuffer";
    renderer.CompilePass(gbufInstPass);

    // Обычный Gbuffer для пола
    ShaderPass gbufStaticPass;
    gbufStaticPass.VertexShaderPath = "Shader.hlsl"; gbufStaticPass.VertexShaderEntryPoint = "VS_Mesh";
    gbufStaticPass.PixelShaderPath = "Shader.hlsl";  gbufStaticPass.PixelShaderEntryPoint = "PS_GBuffer";
    renderer.CompilePass(gbufStaticPass);

    // C-E) Остальные пост-эффекты (SSAO, Combine) без изменений
    ShaderPass ssaoPass;
    ssaoPass.VertexShaderPath = "Shader.hlsl"; ssaoPass.VertexShaderEntryPoint = "VS_Quad"; ssaoPass.PixelShaderPath = "Shader.hlsl"; ssaoPass.PixelShaderEntryPoint = "PS_SSAO_Raw";
    ssaoPass.AddTexture("TexPosition", rtPos); ssaoPass.AddTexture("TexNormal", rtNorm); ssaoPass.AddTexture("TexNoise", noiseTexture);
    ssaoPass.AddSampler("SamplerClamp", smpLin); ssaoPass.AddSampler("SamplerPoint", smpPt);
    renderer.CompilePass(ssaoPass);

    ShaderPass denoisePass;
    denoisePass.VertexShaderPath = "Shader.hlsl"; denoisePass.VertexShaderEntryPoint = "VS_Quad"; denoisePass.PixelShaderPath = "Shader.hlsl"; denoisePass.PixelShaderEntryPoint = "PS_Denoise";
    denoisePass.AddTexture("TexSSAO_Raw", rtSSAORaw); denoisePass.AddSampler("SamplerClamp", smpLin);
    renderer.CompilePass(denoisePass);

    ShaderPass combinePass;
    combinePass.VertexShaderPath = "Shader.hlsl"; combinePass.VertexShaderEntryPoint = "VS_Quad"; combinePass.PixelShaderPath = "Shader.hlsl"; combinePass.PixelShaderEntryPoint = "PS_Combine";
    combinePass.AddTexture("TexAlbedo", rtAlbedo); combinePass.AddTexture("TexSSAO", rtSSAODenoised); combinePass.AddTexture("TexPosWorld", rtPos); combinePass.AddTexture("TexNormalWorld", rtNorm); combinePass.AddTexture("TexShadow", rtShadow);
    combinePass.AddSampler("SamplerClamp", smpLin);
    renderer.CompilePass(combinePass);


    // Камера и Свет
    // Свет отодвинем повыше, чтобы накрыть массив
    Math::float3 lightPos = { 100.0f, 200.0f, -100.0f };
    Math::float4x4 lightView = Math::float4x4::look_at_lh(lightPos, { 0,0,0 }, { 0,1,0 });
    // Большая ортогональная проекция (250 метров), чтобы накрыть всё поле
    Math::float4x4 lightProj = Math::float4x4::orthographic_lh_zo(250.0f, 250.0f, 10.0f, 500.0f);
    Math::float4x4 lightVP = lightView * lightProj;

    Math::float4x4 proj = Math::float4x4::perspective_lh_zo(3.14159f / 4.0f, (float)W / H, 0.5f, 500.0f);

    MSG msg = {};
    float time = 0;
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        else {
            time += 0.005f;
            // Камера летает вокруг массива
            float camX = sin(time * 0.5f) * 50.0f;
            float camZ = cos(time * 0.5f) * 50.0f;
            Math::float3 camPos = { camX, 5.0f, camZ };
            Math::float4x4 view = Math::float4x4::look_at_lh(camPos, { 0,0,0 }, { 0,1,0 });

            ssaoConfig.View = view; ssaoConfig.Projection = proj; ssaoConfig.CameraPosition = Math::float4(camPos.x, camPos.y, camPos.z, 1);

            // =========================
            // 1. SHADOW PASS (INSTANCED)
            // =========================
            renderer.SetRenderTarget(rtShadow);
            renderer.Clear(rtShadow, 1, 1, 1, 1); renderer.ClearDepth(1.0f);
            renderer.SetCullMode(CullMode::Back);
            renderer.SetDepthWrite(true);
            renderer.SetConstant("ViewProjection", lightVP); // Light Matrix

            // Рисуем 10 000 объектов ОДНИМ вызовом!
            renderer.SetShaderPass(shadowInstPass);
            renderer.DrawMeshInstanced(objectMesh, instanceBuffer);

            // Пол (обычный дро)
            renderer.SetShaderPass(shadowStaticPass);
            renderer.SetConstant("World", Math::float4x4::identity()); // World Matrix для пола
            renderer.DrawMesh(floorMesh);

            // =========================
            // 2. G-BUFFER (INSTANCED)
            // =========================
            renderer.SetRenderTarget(rtAlbedo, rtPos, rtNorm);
            renderer.Clear(0, 0, 0, 1); renderer.ClearDepth();
            renderer.SetConstant("ViewProjection", view * proj); // Camera Matrix

            // Инстансы
            renderer.SetShaderPass(gbufInstPass);
            renderer.DrawMeshInstanced(objectMesh, instanceBuffer);

            // Пол
            renderer.SetShaderPass(gbufStaticPass);
            renderer.SetConstant("World", Math::float4x4::identity());
            renderer.DrawMesh(floorMesh);

            // =========================
            // 3. SSAO & POST
            // =========================
            renderer.SetRenderTarget(rtSSAORaw);
            renderer.Clear(1, 1, 1, 1);
            renderer.SetDepthWrite(false);
            renderer.SetShaderPass(ssaoPass);
            renderer.SetCustomConstant("SSAOConfigBuffer", ssaoConfig);
            renderer.DrawFullScreenQuad();

            renderer.SetRenderTarget(rtSSAODenoised);
            renderer.SetShaderPass(denoisePass);
            renderer.DrawFullScreenQuad();

            renderer.RenderPassToScreen();
            renderer.Clear(0.2f, 0.2f, 0.2f, 1);
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
