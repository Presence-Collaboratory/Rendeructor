#include "pch.h"
#include "Rendeructor.h"
#include "BackendDX11.h"

void ShaderPass::AddTexture(const std::string& name, const Texture& texture) {
    auto it = m_textures.find(name);
    if (it == m_textures.end() || it->second != &texture) {
        m_textures[name] = &texture;
    }
}

void ShaderPass::AddTexture(const std::string& name, const Texture3D& texture) {
    auto it = m_textures3D.find(name);
    if (it == m_textures3D.end() || it->second != &texture) {
        m_textures3D[name] = &texture;
    }
}

void ShaderPass::AddTexture(const std::string& name, const TextureCube& texture) {
    auto it = m_texturesCube.find(name);
    if (it == m_texturesCube.end() || it->second != &texture) {
        m_texturesCube[name] = &texture;
    }
}

void ShaderPass::AddSampler(const std::string& name, const Sampler& sampler) {
    m_samplers[name] = &sampler;
}

void Sampler::Create(const std::string& filterName) {
    if (Rendeructor::GetCurrent() && Rendeructor::GetCurrent()->GetBackendAPI()) {
        m_backendHandle = Rendeructor::GetCurrent()->GetBackendAPI()->CreateSamplerResource(filterName);
    }
}