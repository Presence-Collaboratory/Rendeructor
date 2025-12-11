#include "pch.h"
#include "Rendeructor.h"
#include "BackendDX11.h"

Rendeructor* Rendeructor::s_instance = nullptr;

void Texture::create(int width, int height, TextureFormat format) {
    m_width = width;
    m_height = height;
    m_format = format;
    if (Rendeructor::GetCurrent() && Rendeructor::GetCurrent()->GetBackendAPI()) {
        m_backendHandle = Rendeructor::GetCurrent()->GetBackendAPI()->CreateTextureResource(width, height, (int)format);
    }
}

void Sampler::create(const std::string& name) {
    if (Rendeructor::GetCurrent()) {
        m_backendHandle = Rendeructor::GetCurrent()->GetBackendAPI()->CreateSamplerResource(name);
    }
}

void ShaderPass::AddTexture(const std::string& name, const Texture& texture) {
    m_textures[name] = &texture;
}

void ShaderPass::AddSampler(const std::string& name, const Sampler& sampler) {
    m_samplers[name] = &sampler;
}

Rendeructor::Rendeructor() {
    s_instance = this;
}

Rendeructor::~Rendeructor() {
    Destroy();
    if (s_instance == this) s_instance = nullptr;
}

Rendeructor* Rendeructor::GetCurrent() {
    return s_instance;
}

bool Rendeructor::Create(const BackendConfig& config) {
    m_currentConfig = config;
    if (config.API == RenderAPI::DirectX11) {
        m_backend = new BackendDX11();
    }
    if (!m_backend) return false;
    return m_backend->Initialize(config);
}

void Rendeructor::Destroy() {
    if (m_backend) {
        m_backend->Shutdown();
        delete m_backend;
        m_backend = nullptr;
    }
}

void Rendeructor::Restart(const BackendConfig& config) {
    Destroy();
    Create(config);
}

void Rendeructor::SetShaderPass(ShaderPass& pass) {
    if (m_backend) {
        m_backend->PrepareShaderPass(pass);
        m_backend->SetShaderPass(pass);
    }
}

void Rendeructor::CompilePass(ShaderPass& pass) {
    if (m_backend) {
        m_backend->PrepareShaderPass(pass);
    }
}

void Rendeructor::SetConstant(const std::string& name, float value) {
    if (m_backend) m_backend->UpdateConstantRaw(name, &value, sizeof(float));
}

void Rendeructor::SetConstant(const std::string& name, const Math::float2& value) {
    if (m_backend) m_backend->UpdateConstantRaw(name, &value, sizeof(Math::float2));
}

void Rendeructor::SetConstant(const std::string& name, const Math::float3& value) {
    if (m_backend) m_backend->UpdateConstantRaw(name, &value, sizeof(Math::float3));
}

void Rendeructor::SetConstant(const std::string& name, const Math::float4& value) {
    if (m_backend) m_backend->UpdateConstantRaw(name, &value, sizeof(Math::float4));
}

void Rendeructor::SetConstant(const std::string& name, const Math::float4x4& value) {
    if (m_backend) m_backend->UpdateConstantRaw(name, &value, sizeof(Math::float4x4));
}

void Rendeructor::RenderViewportSurface(const Texture& target) {
    if (m_backend) m_backend->RenderViewportSurface(target.GetHandle());
}

void Rendeructor::Present() {
    if (m_backend) {
        m_backend->EndFrame();
    }
}
