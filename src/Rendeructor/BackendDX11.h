#pragma once
#include "BackendInterface.h"

using Microsoft::WRL::ComPtr;

struct DX11TextureWrapper {
    ComPtr<ID3D11Texture2D> Texture;
    ComPtr<ID3D11ShaderResourceView> SRV;
    ComPtr<ID3D11RenderTargetView> RTV;
    int Width;
    int Height;
};

struct DX11SamplerWrapper {
    ComPtr<ID3D11SamplerState> State;
};

struct ConstantBufferVariable {
    std::string Name;
    UINT Offset;
    UINT Size;
};

struct DX11ReflectionData {
    std::vector<ConstantBufferVariable> Variables;
    UINT BufferSize;
};

struct DX11ShaderWrapper {
    ComPtr<ID3D11VertexShader> VertexShader;
    ComPtr<ID3D11PixelShader> PixelShader;
    ComPtr<ID3D11InputLayout> InputLayout;
    DX11ReflectionData ReflectionVS;
    DX11ReflectionData ReflectionPS;
};

struct DX11BufferWrapper {
    ComPtr<ID3D11Buffer> Buffer;
    UINT Size; // Размер в байтах
    UINT Stride; // Размер одного элемента (для VB)
};

class BackendDX11 : public BackendInterface
{
public:
    BackendDX11();
    ~BackendDX11();

    bool Initialize(const BackendConfig& config) override;
    void Shutdown() override;
    void Resize(int width, int height) override;
    void BeginFrame() override;
    void EndFrame() override;

    void* CreateTextureResource(int width, int height, int format, const void* initialData) override;
    void* CreateSamplerResource(const std::string& filterMode) override;

    // Новые методы
    void CopyTexture(void* dstHandle, void* srcHandle) override;
    void SetRenderTarget(void* textureHandle) override;

    void Clear(float r, float g, float b, float a) override;

    void PrepareShaderPass(const ShaderPass& pass) override;
    void SetShaderPass(const ShaderPass& pass) override;
    void UpdateConstantRaw(const std::string& name, const void* data, size_t size) override;

    void DrawFullScreenQuad() override;

    void* CreateVertexBuffer(const void* data, size_t size, int stride) override;
    void* CreateIndexBuffer(const void* data, size_t size) override;

    void DrawMesh(void* vbHandle, void* ibHandle, int indexCount) override;

private:
    bool InitD3D(const BackendConfig& config);
    void InitQuadGeometry();
    bool CompileShader(const std::string& path, const std::string& entry, const std::string& profile, ID3DBlob** outBlob);
    DX11ReflectionData ReflectShader(ID3DBlob* blob);

    void* CreateBufferInternal(const void* data, size_t size, UINT bindFlags);

    int m_screenWidth = 0;
    int m_screenHeight = 0;

    HWND m_hwnd = nullptr;
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGISwapChain> m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_backBufferRTV;

    ComPtr<ID3D11Buffer> m_quadVertexBuffer;
    ComPtr<ID3D11Buffer> m_quadIndexBuffer;

    std::vector<DX11TextureWrapper*> m_textures;
    std::vector<DX11SamplerWrapper*> m_samplers;
    std::map<std::string, DX11ShaderWrapper> m_shaderCache;

    DX11ShaderWrapper* m_activeShader = nullptr;

    struct StoredConstant {
        std::vector<uint8_t> Data;
    };
    std::map<std::string, StoredConstant> m_cpuConstantsStorage;

    ComPtr<ID3D11Buffer> m_cbVS;
    ComPtr<ID3D11Buffer> m_cbPS;
    size_t m_cbVSSize = 0;
    size_t m_cbPSSize = 0;

    ComPtr<ID3D11Texture2D> m_depthStencilBuffer;
    ComPtr<ID3D11DepthStencilView> m_depthStencilView;
    ComPtr<ID3D11DepthStencilState> m_depthStencilState;
    ComPtr<ID3D11RasterizerState> m_rasterizerState;

    void CreateDepthResources(int width, int height);
};
