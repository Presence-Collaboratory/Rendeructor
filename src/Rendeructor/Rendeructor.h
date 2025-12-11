#pragma once
#include "framework.h"
#include "BackendInterface.h"
#include "RendeructorAPI.h"

enum class ScreenMode { Windowed, Fullscreen, Borderless };
enum class RenderAPI { DirectX11, DirectX12, OpenGL, Vulkan };
enum class TextureFormat { RGBA8, RGBA16F, R16F, R32F };

struct RENDER_API BackendConfig {
    int Width = 1920;
    int Height = 1080;
    ScreenMode ScreenMode = ScreenMode::Windowed;
    RenderAPI API = RenderAPI::DirectX11;
    void* WindowHandle = nullptr;
};

class RENDER_API Texture {
public:
    Texture() = default;

    void Create(int width, int height, TextureFormat format);

    bool LoadFromDisk(const std::string& path);

    void Copy(const Texture& source);

    void* GetHandle() const { return m_backendHandle; }
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    TextureFormat GetFormat() const { return m_format; }

private:
    void* m_backendHandle = nullptr;
    int m_width = 0;
    int m_height = 0;
    TextureFormat m_format = TextureFormat::RGBA8;
};


class RENDER_API Sampler {
public:
    void Create(const std::string& filterName = "Linear");
    void* GetHandle() const { return m_backendHandle; }
private:
    void* m_backendHandle = nullptr;
};

class RENDER_API ShaderPass {
public:
    std::string PixelShaderPath;
    std::string PixelShaderEntryPoint = "main";
    std::string VertexShaderPath;
    std::string VertexShaderEntryPoint = "main";

    void AddTexture(const std::string& name, const Texture& texture);
    void AddSampler(const std::string& name, const Sampler& sampler);

    const std::map<std::string, const Texture*>& GetTextures() const { return m_textures; }
    const std::map<std::string, const Sampler*>& GetSamplers() const { return m_samplers; }

private:
    std::map<std::string, const Texture*> m_textures;
    std::map<std::string, const Sampler*> m_samplers;
};

struct Vertex {
    Math::float3 Position;
    Math::float2 UV;

    Vertex() = default;
    Vertex(float x, float y, float z, float u, float v)
        : Position(x, y, z), UV(u, v) {}
};

class RENDER_API Mesh {
public:
    Mesh() = default;

    void Create(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices);

    void* GetVB() const { return m_vbHandle; }
    void* GetIB() const { return m_ibHandle; }
    int GetIndexCount() const { return m_indexCount; }

private:
    void* m_vbHandle = nullptr;
    void* m_ibHandle = nullptr;
    int m_indexCount = 0;
};


class RENDER_API Rendeructor {
public:
    Rendeructor();
    ~Rendeructor();

    bool Create(const BackendConfig& config);
    void Restart(const BackendConfig& config);
    void Destroy();

    void SetShaderPass(ShaderPass& pass);

    void CompilePass(ShaderPass& pass);

    void SetConstant(const std::string& name, float value);
    void SetConstant(const std::string& name, const Math::float2& value);
    void SetConstant(const std::string& name, const Math::float3& value);
    void SetConstant(const std::string& name, const Math::float4& value);
    void SetConstant(const std::string& name, const Math::float4x4& value);

    void RenderToTexture(const Texture& target = Texture());
    void RenderPassToTexture(const Texture& target);

    void RenderPassToScreen();

    void Clear(float r, float g, float b, float a = 1.0f);

    void DrawMesh(const Mesh& mesh);

    void Present();

    static Rendeructor* GetCurrent();
    BackendInterface* GetBackendAPI() { return m_backend; }

private:
    BackendInterface* m_backend = nullptr;
    BackendConfig m_currentConfig;
    static Rendeructor* s_instance;
};
