#include "pch.h"
#include "Log.h"
#include "BackendDX11.h"
#include "Rendeructor.h"
#include <cstdio>
#include <string>
#include <vector>

struct SimpleVertex {
    float x, y, z;
    float u, v;
};

BackendDX11::BackendDX11() {
    LogDebug("[BackendDX11] Constructor called.");
}

BackendDX11::~BackendDX11() {
    Shutdown();
}

bool BackendDX11::Initialize(const BackendConfig& config) {
    LogDebug("[BackendDX11] Initializing...");

    if (!config.WindowHandle) {
        LogDebug("[BackendDX11] Error: WindowHandle is NULL");
        return false;
    }
    m_hwnd = (HWND)config.WindowHandle;

    if (!InitD3D(config)) {
        LogDebug("[BackendDX11] Error: InitD3D failed.");
        return false;
    }

    LogDebug("[BackendDX11] InitD3D success. Initializing Geometry...");
    InitQuadGeometry();

    LogDebug("[BackendDX11] Initialization Complete.");
    return true;
}

bool BackendDX11::InitD3D(const BackendConfig& config) {
    LogDebug("[BackendDX11] Setting up SwapChain...");

    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 1;
    scd.BufferDesc.Width = config.Width;
    scd.BufferDesc.Height = config.Height;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

    // Безопасная частота обновления
    scd.BufferDesc.RefreshRate.Numerator = 0;
    scd.BufferDesc.RefreshRate.Denominator = 1;

    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = m_hwnd;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.Windowed = TRUE; // Принудительно оконный для теста
    scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    // Снимаем ВСЕ флаги отладки для стабильности
    UINT flags = 0;
    // flags |= D3D11_CREATE_DEVICE_DEBUG; // <--- ВЫКЛЮЧЕНО

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL featureLevel;

    LogDebug("[BackendDX11] Calling D3D11CreateDeviceAndSwapChain...");

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, &scd,
        m_swapChain.GetAddressOf(), m_device.GetAddressOf(), &featureLevel, m_context.GetAddressOf()
    );

    if (FAILED(hr)) {
        LogDebug("[BackendDX11] HARDWARE Creation FAILED! HRESULT: 0x%08X", (unsigned int)hr);
        LogDebug("[BackendDX11] Trying WARP (Software) Driver...");

        // Попытка 2: Программный рендеринг (если видеокарта виновата)
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
            featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, &scd,
            m_swapChain.GetAddressOf(), m_device.GetAddressOf(), &featureLevel, m_context.GetAddressOf()
        );

        if (FAILED(hr)) {
            LogDebug("[BackendDX11] WARP Creation FAILED too! HRESULT: 0x%08X", (unsigned int)hr);
            return false;
        }
        else {
            LogDebug("[BackendDX11] WARP (Software) Created successfully.");
        }
    }
    else {
        LogDebug("[BackendDX11] Hardware Device Created successfully. Feature Level: 0x%X", featureLevel);
    }

    // RTV
    ComPtr<ID3D11Texture2D> backBuffer;
    hr = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer.GetAddressOf());
    if (FAILED(hr)) {
        LogDebug("[BackendDX11] Failed to get backbuffer. HRESULT: 0x%08X", (unsigned int)hr);
        return false;
    }

    hr = m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_backBufferRTV.GetAddressOf());
    if (FAILED(hr)) {
        LogDebug("[BackendDX11] Failed to create RTV. HRESULT: 0x%08X", (unsigned int)hr);
        return false;
    }

    LogDebug("[BackendDX11] Initializing Viewport...");
    Resize(config.Width, config.Height);
    return true;
}

void BackendDX11::Shutdown() {
    LogDebug("[BackendDX11] Shutdown called.");
    for (auto* t : m_textures) delete t;
    m_textures.clear();
    for (auto* s : m_samplers) delete s;
    m_samplers.clear();
    m_shaderCache.clear();
}

void BackendDX11::Resize(int width, int height) {
    if (!m_context) return;
    D3D11_VIEWPORT vp = {};
    vp.Width = (float)width;
    vp.Height = (float)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);
}

void BackendDX11::BeginFrame() {}

void BackendDX11::EndFrame() {
    if (m_swapChain) m_swapChain->Present(1, 0);
}

void* BackendDX11::CreateTextureResource(int width, int height, int format) {
    auto* wrapper = new DX11TextureWrapper();
    wrapper->Width = width;
    wrapper->Height = height;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    switch ((TextureFormat)format) {
    case TextureFormat::RGBA16F: desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; break;
    case TextureFormat::R16F: desc.Format = DXGI_FORMAT_R16_FLOAT; break;
    case TextureFormat::R32F: desc.Format = DXGI_FORMAT_R32_FLOAT; break;
    default: desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; break;
    }

    HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, wrapper->Texture.GetAddressOf());
    if (FAILED(hr)) {
        LogDebug("[BackendDX11] Failed to create texture. HRESULT: 0x%08X", (unsigned int)hr);
        delete wrapper;
        return nullptr;
    }

    m_device->CreateShaderResourceView(wrapper->Texture.Get(), nullptr, wrapper->SRV.GetAddressOf());
    m_device->CreateRenderTargetView(wrapper->Texture.Get(), nullptr, wrapper->RTV.GetAddressOf());
    m_textures.push_back(wrapper);
    return wrapper;
}

void* BackendDX11::CreateSamplerResource(const std::string& filterMode) {
    auto* wrapper = new DX11SamplerWrapper();
    D3D11_SAMPLER_DESC desc = {};
    desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    desc.MaxLOD = D3D11_FLOAT32_MAX;

    if (filterMode == "Point") desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    else desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;

    m_device->CreateSamplerState(&desc, wrapper->State.GetAddressOf());
    m_samplers.push_back(wrapper);
    return wrapper;
}

void BackendDX11::InitQuadGeometry() {
    SimpleVertex vertices[] = {
        { -1.0f, -1.0f, 0.0f,  0.0f, 1.0f },
        { -1.0f,  1.0f, 0.0f,  0.0f, 0.0f },
        {  1.0f, -1.0f, 0.0f,  1.0f, 1.0f },
        {  1.0f,  1.0f, 0.0f,  1.0f, 0.0f },
    };

    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(SimpleVertex) * 4;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = vertices;
    m_device->CreateBuffer(&bd, &initData, m_quadVertexBuffer.GetAddressOf());

    unsigned long indices[] = { 0, 1, 2, 2, 1, 3 };
    bd.ByteWidth = sizeof(unsigned long) * 6;
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    initData.pSysMem = indices;
    m_device->CreateBuffer(&bd, &initData, m_quadIndexBuffer.GetAddressOf());
}

bool BackendDX11::CompileShader(const std::string& path, const std::string& entry, const std::string& profile, ID3DBlob** outBlob) {
    std::wstring wpath(path.begin(), path.end());
    ID3DBlob* errorBlob = nullptr;
    // ВАЖНО: Добавим флаг отладки для шейдеров, чтобы видеть ошибки
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG;
#endif

    HRESULT hr = D3DCompileFromFile(wpath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry.c_str(), profile.c_str(), flags, 0, outBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            LogDebug("[Shader Error] %s", (char*)errorBlob->GetBufferPointer());
            errorBlob->Release();
        }
        else {
            LogDebug("[Shader Error] Failed to find file: %s", path.c_str());
        }
        return false;
    }
    return true;
}

DX11ReflectionData BackendDX11::ReflectShader(ID3DBlob* blob) {
    DX11ReflectionData data;
    data.BufferSize = 0;

    ComPtr<ID3D11ShaderReflection> reflector;
    if (FAILED(D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(), IID_ID3D11ShaderReflection, (void**)reflector.GetAddressOf()))) {
        return data;
    }

    D3D11_SHADER_DESC shaderDesc;
    reflector->GetDesc(&shaderDesc);

    for (UINT i = 0; i < shaderDesc.ConstantBuffers; ++i) {
        ID3D11ShaderReflectionConstantBuffer* cb = reflector->GetConstantBufferByIndex(i);
        D3D11_SHADER_BUFFER_DESC bufferDesc;
        cb->GetDesc(&bufferDesc);

        if (data.BufferSize == 0) data.BufferSize = bufferDesc.Size;

        for (UINT j = 0; j < bufferDesc.Variables; ++j) {
            ID3D11ShaderReflectionVariable* var = cb->GetVariableByIndex(j);
            D3D11_SHADER_VARIABLE_DESC varDesc;
            var->GetDesc(&varDesc);

            ConstantBufferVariable v;
            v.Name = varDesc.Name;
            v.Offset = varDesc.StartOffset;
            v.Size = varDesc.Size;
            data.Variables.push_back(v);
        }
    }
    return data;
}

void BackendDX11::PrepareShaderPass(const ShaderPass& pass) {
    std::string key = pass.VertexShaderPath + ":" + pass.VertexShaderEntryPoint + "|" + pass.PixelShaderPath + ":" + pass.PixelShaderEntryPoint;
    if (m_shaderCache.find(key) != m_shaderCache.end()) return;

    LogDebug("[BackendDX11] Compiling Shader Pass: %s", key.c_str());

    DX11ShaderWrapper sw;
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;

    if (CompileShader(pass.VertexShaderPath, pass.VertexShaderEntryPoint, "vs_5_0", &vsBlob)) {
        m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, sw.VertexShader.GetAddressOf());
        sw.ReflectionVS = ReflectShader(vsBlob);

        D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };
        m_device->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), sw.InputLayout.GetAddressOf());
        vsBlob->Release();
    }
    else {
        LogDebug("[BackendDX11] Vertex Shader Compilation Failed!");
    }

    if (CompileShader(pass.PixelShaderPath, pass.PixelShaderEntryPoint, "ps_5_0", &psBlob)) {
        m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, sw.PixelShader.GetAddressOf());
        sw.ReflectionPS = ReflectShader(psBlob);
        psBlob->Release();
    }
    else {
        LogDebug("[BackendDX11] Pixel Shader Compilation Failed!");
    }

    m_shaderCache[key] = sw;
}

void BackendDX11::SetShaderPass(const ShaderPass& pass) {
    std::string key = pass.VertexShaderPath + ":" + pass.VertexShaderEntryPoint + "|" + pass.PixelShaderPath + ":" + pass.PixelShaderEntryPoint;
    if (m_shaderCache.find(key) == m_shaderCache.end()) return;

    m_activeShader = &m_shaderCache[key];

    m_context->IASetInputLayout(m_activeShader->InputLayout.Get());
    m_context->VSSetShader(m_activeShader->VertexShader.Get(), nullptr, 0);
    m_context->PSSetShader(m_activeShader->PixelShader.Get(), nullptr, 0);

    int slot = 0;
    for (auto const& [name, texturePtr] : pass.GetTextures()) {
        auto* tex = (DX11TextureWrapper*)texturePtr->GetHandle();
        if (tex) m_context->PSSetShaderResources(slot++, 1, tex->SRV.GetAddressOf());
    }

    slot = 0;
    for (auto const& [name, samplerPtr] : pass.GetSamplers()) {
        auto* smp = (DX11SamplerWrapper*)samplerPtr->GetHandle();
        if (smp) m_context->PSSetSamplers(slot++, 1, smp->State.GetAddressOf());
    }

    if (m_activeShader->ReflectionVS.BufferSize > m_cbVSSize) {
        m_cbVSSize = m_activeShader->ReflectionVS.BufferSize;
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = m_cbVSSize;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        m_device->CreateBuffer(&bd, nullptr, m_cbVS.ReleaseAndGetAddressOf());
    }

    if (m_activeShader->ReflectionPS.BufferSize > m_cbPSSize) {
        m_cbPSSize = m_activeShader->ReflectionPS.BufferSize;
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = m_cbPSSize;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        m_device->CreateBuffer(&bd, nullptr, m_cbPS.ReleaseAndGetAddressOf());
    }
}

void BackendDX11::UpdateConstantRaw(const std::string& name, const void* data, size_t size) {
    std::vector<uint8_t> buffer(size);
    memcpy(buffer.data(), data, size);
    m_cpuConstantsStorage[name] = { buffer };
}

void BackendDX11::RenderViewportSurface(void* targetTextureHandle) {
    if (!m_activeShader) return;

    if (m_cbVS && m_activeShader->ReflectionVS.BufferSize > 0) {
        D3D11_MAPPED_SUBRESOURCE map;
        if (SUCCEEDED(m_context->Map(m_cbVS.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
            for (const auto& var : m_activeShader->ReflectionVS.Variables) {
                if (m_cpuConstantsStorage.count(var.Name)) {
                    const auto& stored = m_cpuConstantsStorage[var.Name];
                    size_t copySize = std::min((size_t)var.Size, stored.Data.size());
                    memcpy((uint8_t*)map.pData + var.Offset, stored.Data.data(), copySize);
                }
            }
            m_context->Unmap(m_cbVS.Get(), 0);
        }
        m_context->VSSetConstantBuffers(0, 1, m_cbVS.GetAddressOf());
    }

    if (m_cbPS && m_activeShader->ReflectionPS.BufferSize > 0) {
        D3D11_MAPPED_SUBRESOURCE map;
        if (SUCCEEDED(m_context->Map(m_cbPS.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
            for (const auto& var : m_activeShader->ReflectionPS.Variables) {
                if (m_cpuConstantsStorage.count(var.Name)) {
                    const auto& stored = m_cpuConstantsStorage[var.Name];
                    size_t copySize = std::min((size_t)var.Size, stored.Data.size());
                    memcpy((uint8_t*)map.pData + var.Offset, stored.Data.data(), copySize);
                }
            }
            m_context->Unmap(m_cbPS.Get(), 0);
        }
        m_context->PSSetConstantBuffers(0, 1, m_cbPS.GetAddressOf());
    }

    ID3D11RenderTargetView* rtv = nullptr;
    if (targetTextureHandle) {
        rtv = ((DX11TextureWrapper*)targetTextureHandle)->RTV.Get();
    }
    else {
        rtv = m_backBufferRTV.Get();
    }

    ID3D11ShaderResourceView* nullSRVs[8] = { nullptr };
    m_context->PSSetShaderResources(0, 8, nullSRVs);
    m_context->OMSetRenderTargets(1, &rtv, nullptr);

    UINT stride = sizeof(SimpleVertex);
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, m_quadVertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->IASetIndexBuffer(m_quadIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->DrawIndexed(6, 0, 0);
}
