#pragma once
#include "framework.h"
#include "RendeructorAPI.h"

struct BackendConfig;
class Texture;
class Sampler;
class ShaderPass;

class BackendInterface
{
public:
    virtual ~BackendInterface() = default;

    virtual bool Initialize(const BackendConfig& config) = 0;
    virtual void Shutdown() = 0;
    virtual void Resize(int width, int height) = 0;

    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;

    virtual void* CreateTextureResource(int width, int height, int format, const void* initialData) = 0;
    virtual void* CreateSamplerResource(const std::string& filterMode) = 0;

    virtual void CopyTexture(void* dstHandle, void* srcHandle) = 0;
    virtual void SetRenderTarget(void* textureHandle) = 0;

    virtual void Clear(float r, float g, float b, float a) = 0;

    virtual void PrepareShaderPass(const ShaderPass& pass) = 0;
    virtual void SetShaderPass(const ShaderPass& pass) = 0;
    virtual void UpdateConstantRaw(const std::string& name, const void* data, size_t size) = 0;

    virtual void DrawFullScreenQuad() = 0;

    virtual void* CreateVertexBuffer(const void* data, size_t size, int stride) = 0;
    virtual void* CreateIndexBuffer(const void* data, size_t size) = 0;

    virtual void DrawMesh(void* vbHandle, void* ibHandle, int indexCount) = 0;
};
