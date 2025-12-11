#include "pch.h"
#include "Rendeructor.h"
#include "BackendDX11.h"
#include <stb_image/stb_image.h>

Rendeructor* Rendeructor::s_instance = nullptr;

void Texture::Create(int width, int height, TextureFormat format) {
    m_width = width;
    m_height = height;
    m_format = format;
    if (Rendeructor::GetCurrent() && Rendeructor::GetCurrent()->GetBackendAPI()) {
        m_backendHandle = Rendeructor::GetCurrent()->GetBackendAPI()->CreateTextureResource(width, height, (int)format, nullptr);
    }
}

bool Texture::LoadFromDisk(const std::string& path) {
    int w, h, channels;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);

    if (!data) {
        return false;
    }

    m_width = w;
    m_height = h;
    m_format = TextureFormat::RGBA8;

    if (Rendeructor::GetCurrent() && Rendeructor::GetCurrent()->GetBackendAPI()) {
        m_backendHandle = Rendeructor::GetCurrent()->GetBackendAPI()->CreateTextureResource(w, h, (int)m_format, data);
    }

    stbi_image_free(data);
    return (m_backendHandle != nullptr);
}

void Texture::Copy(const Texture& source) {
    if (Rendeructor::GetCurrent() && Rendeructor::GetCurrent()->GetBackendAPI()) {
        Rendeructor::GetCurrent()->GetBackendAPI()->CopyTexture(m_backendHandle, source.GetHandle());
    }
}

void Texture3D::Create(int width, int height, int depth, const void* data) {
    if (Rendeructor::GetCurrent()) {
        m_backendHandle = Rendeructor::GetCurrent()->GetBackendAPI()->CreateTexture3DResource(width, height, depth, 0, data);
    }
}

void Sampler::Create(const std::string& filterName) {
    if (Rendeructor::GetCurrent()) {
        m_backendHandle = Rendeructor::GetCurrent()->GetBackendAPI()->CreateSamplerResource(filterName);
    }
}

void ShaderPass::AddTexture(const std::string& name, const Texture& texture) {
    m_textures[name] = &texture;
}

void ShaderPass::AddTexture(const std::string& name, const Texture3D& texture) {
    m_textures3D[name] = &texture;
}

void ShaderPass::AddSampler(const std::string& name, const Sampler& sampler) {
    m_samplers[name] = &sampler;
}

void Mesh::Create(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices) {
    if (Rendeructor::GetCurrent() && Rendeructor::GetCurrent()->GetBackendAPI()) {

        m_vbHandle = Rendeructor::GetCurrent()->GetBackendAPI()->CreateVertexBuffer(
            vertices.data(),
            vertices.size() * sizeof(Vertex),
            sizeof(Vertex)
        );

        m_ibHandle = Rendeructor::GetCurrent()->GetBackendAPI()->CreateIndexBuffer(
            indices.data(),
            indices.size() * sizeof(unsigned int)
        );

        m_indexCount = (int)indices.size();
    }
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

void Rendeructor::SetDepthWrite(bool enabled) {
    if (m_backend) m_backend->SetDepthState(true, enabled);
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

void Rendeructor::RenderToTexture(const Texture& target) {
    if (m_backend) {
        m_backend->SetRenderTarget(target.GetHandle());
    }
}

void Rendeructor::RenderPassToTexture(const Texture& target) {
    if (m_backend) {
        m_backend->SetRenderTarget(target.GetHandle());
        m_backend->DrawFullScreenQuad();
    }
}

void Rendeructor::RenderPassToScreen() {
    if (m_backend) {
        m_backend->SetRenderTarget(nullptr);
        m_backend->DrawFullScreenQuad();
    }
}

void Rendeructor::Clear(float r, float g, float b, float a) {
    if (m_backend) m_backend->Clear(r, g, b, a);
}

void Rendeructor::DrawMesh(const Mesh& mesh) {
    if (m_backend) {
        m_backend->DrawMesh(mesh.GetVB(), mesh.GetIB(), mesh.GetIndexCount());
    }
}

void Rendeructor::DrawFullScreenQuad() {
    if (m_backend) m_backend->DrawFullScreenQuad();
}

void Rendeructor::Present() {
    if (m_backend) {
        m_backend->EndFrame();
    }
}
