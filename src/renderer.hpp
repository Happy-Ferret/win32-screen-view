#pragma once

#include <d3d10_1.h>
#include <dxgi1_2.h>

#include "logger.hpp"
#include "util.hpp"
#include "shaders.h"
#include "com_ptr.hpp"

// Renders our desktop view scene
template<class TSource>
class Renderer {
    util::dll_func<HRESULT (REFIID, IDXGIFactory1 **)> m_dxgiCreator { L"dxgi.dll", "CreateDXGIFactory1" };
    util::dll_func<HRESULT (IDXGIAdapter *,
                            D3D10_DRIVER_TYPE,
                            HMODULE,
                            UINT,
                            D3D10_FEATURE_LEVEL1,
                            UINT,
                            DXGI_SWAP_CHAIN_DESC *,
                            IDXGISwapChain **,
                            ID3D10Device1 **)> m_d3dCreator { L"d3d10_1.dll", "D3D10CreateDeviceAndSwapChain1" };

    com_ptr<IDXGIFactory1>          m_dxgiFactory;
    com_ptr<ID3D10Device1>          m_device;
    com_ptr<IDXGISwapChain>         m_swap;
    com_ptr<ID3D10RenderTargetView> m_renderTarget;
    com_ptr<ID3D10PixelShader>      m_pshader;
    com_ptr<ID3D10VertexShader>     m_vshader;
    com_ptr<ID3D10InputLayout>      m_ilayout;
    com_ptr<ID3D10SamplerState>     m_sampler;
    com_ptr<ID3D10BlendState>       m_blendState;

    com_ptr<ID3D10Texture2D>          m_desktopTexture;
    com_ptr<ID3D10ShaderResourceView> m_desktopSrv;
    com_ptr<ID3D10Texture2D>          m_cursorTexture;
    com_ptr<ID3D10ShaderResourceView> m_cursorSrv;
    com_ptr<ID3D10Buffer>             m_desktopVBuffer;
    com_ptr<ID3D10Buffer>             m_cursorVBuffer;

    bool m_cursorVisible = true;
    UINT m_cursorWidth   = 0;
    UINT m_cursorHeight  = 0;
    LONG m_cursorX       = 0;
    LONG m_cursorY       = 0;
    int  m_desktopWidth  = 0;
    int  m_desktopHeight = 0;

    struct VERTEX { float x; float y; float z; float u; float v; };

    TSource m_source;

    bool setupDxgiAndD3DDevice(HWND hwnd)
    {
        HRESULT hr;

        if (!m_dxgiCreator || !m_d3dCreator)
            return false;

        // By default, D3D10CreateDeviceAndSwapChain1 creates a DXGI1.0 factory, but the desktop duplication
        // API needs at least a DXGI1.1 factory. So we create that manually and feed it into D3D10
        IDXGIFactory1 *fac = nullptr;
        hr = m_dxgiCreator(m_dxgiFactory.uuid(), &fac);
        m_dxgiFactory = com_ptr<IDXGIFactory1>::take(fac);
        if FAILED(hr) {
            logger << "Failed to create IDXGIFactory1: " << util::hresult_to_utf8(hr) << std::endl;
            return false;
        }

        com_ptr<IDXGIAdapter> desktopAdapter;
        hr = m_dxgiFactory->EnumAdapters(0, desktopAdapter.pptr_cleared());
        if FAILED(hr) {
            logger << "Failed to get Adapter #0: " << util::hresult_to_utf8(hr) << std::endl;
            return false;
        }

        // We create the device and swap chain in one go
        DXGI_SWAP_CHAIN_DESC desc;
        memset(&desc, 0, sizeof(desc));
        desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count  = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 1;
        desc.OutputWindow = hwnd;
        desc.Windowed = true;

        hr = m_d3dCreator(desktopAdapter,
                        D3D10_DRIVER_TYPE_HARDWARE,
                        nullptr,
                        D3D10_CREATE_DEVICE_BGRA_SUPPORT,
                        D3D10_FEATURE_LEVEL_9_1,
                        D3D10_1_SDK_VERSION,
                        &desc,
                        m_swap.pptr_cleared(),
                        m_device.pptr_cleared());
        if FAILED(hr) {
            logger << "Failed to create device and swap chain :( " << util::hresult_to_utf8(hr) << std::endl;
            return false;
        }

        // make vsync possible
        auto dxgiDevice = m_device.query<IDXGIDevice1>();
        if (dxgiDevice)
            dxgiDevice->SetMaximumFrameLatency(1);

        return true;
    }

    bool setupShaders()
    {
        HRESULT hr;

        // Load the shaders
        hr = m_device->CreatePixelShader(shader_compiled_PShader, sizeof(shader_compiled_PShader), m_pshader.pptr_cleared());
        if FAILED(hr) {
            logger << "Failed to create pixel shader :( " << util::hresult_to_utf8(hr) << std::endl;
            return false;
        }

        hr = m_device->CreateVertexShader(shader_compiled_VShader, sizeof(shader_compiled_VShader), m_vshader.pptr_cleared());
        if FAILED(hr) {
            logger << "Failed to create vertex shader :( " << util::hresult_to_utf8(hr) << std::endl;
            return false;
        }

        m_device->VSSetShader(m_vshader);
        m_device->PSSetShader(m_pshader);

        return true;
    }

    bool setupInputLayout()
    {
        HRESULT hr;

        // Setup the input layout
        D3D10_INPUT_ELEMENT_DESC ied[] =
        {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D10_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D10_INPUT_PER_VERTEX_DATA, 0},
        };

        hr = m_device->CreateInputLayout(ied, 2, shader_compiled_VShader, sizeof(shader_compiled_VShader), m_ilayout.pptr_cleared());
        if FAILED(hr) {
            logger << "Failed to create input layout " << util::hresult_to_utf8(hr) << std::endl;
            return false;
        }

        m_device->IASetInputLayout(m_ilayout);

        return true;
    }

    bool setupSamplers()
    {
        HRESULT hr;

        // Create and set a sampler
        D3D10_SAMPLER_DESC samplerdsc = {
            .Filter = D3D10_FILTER_MIN_MAG_MIP_LINEAR,
            .AddressU = D3D10_TEXTURE_ADDRESS_CLAMP,
            .AddressV = D3D10_TEXTURE_ADDRESS_CLAMP,
            .AddressW = D3D10_TEXTURE_ADDRESS_CLAMP,
            .MipLODBias = 0.0f,
            .MaxAnisotropy = 1,
            .ComparisonFunc = D3D10_COMPARISON_ALWAYS,
            .BorderColor = { 0.0f, 0.0f, 0.0f, 1.0f },
            .MinLOD = 0.0f,
            .MaxLOD = D3D10_FLOAT32_MAX
        };
        hr = m_device->CreateSamplerState(&samplerdsc, m_sampler.pptr_cleared());
        if FAILED(hr) {
            logger << "Failed to create sampler state: " << util::hresult_to_utf8(hr) << std::endl;
            return false;
        }
        m_device->PSSetSamplers(0, 1, m_sampler.pptr());

        return true;
    }

    bool setupBlendState()
    {
        HRESULT hr;

        // setup the blend state
        D3D10_BLEND_DESC blenddsc = {
            .AlphaToCoverageEnable = FALSE,
            .BlendEnable = { TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE },
            .SrcBlend = D3D10_BLEND_SRC_ALPHA,
            .DestBlend = D3D10_BLEND_INV_SRC_ALPHA,
            .BlendOp = D3D10_BLEND_OP_ADD,
            .SrcBlendAlpha = D3D10_BLEND_ZERO,
            .DestBlendAlpha = D3D10_BLEND_ZERO,
            .BlendOpAlpha = D3D10_BLEND_OP_ADD,
            .RenderTargetWriteMask = { D3D10_COLOR_WRITE_ENABLE_ALL, D3D10_COLOR_WRITE_ENABLE_ALL, D3D10_COLOR_WRITE_ENABLE_ALL, D3D10_COLOR_WRITE_ENABLE_ALL, D3D10_COLOR_WRITE_ENABLE_ALL, D3D10_COLOR_WRITE_ENABLE_ALL, D3D10_COLOR_WRITE_ENABLE_ALL, D3D10_COLOR_WRITE_ENABLE_ALL }
        };
        hr = m_device->CreateBlendState(&blenddsc, m_blendState.pptr_cleared());
        if FAILED(hr) {
            logger << "Failed to create blend state: " << util::hresult_to_utf8(hr) << std::endl;
            return false;
        }
        m_device->OMSetBlendState(m_blendState, nullptr, 0xFFFFFFFF);

        return true;
    }

    bool setupDesktopTextureAndVertices()
    {
        HRESULT hr;

        m_desktopTexture = com_ptr<ID3D10Texture2D>::take(m_source.createDesktopTexture());
        if (!m_desktopTexture)
            return false;

        hr = m_device->CreateShaderResourceView(m_desktopTexture, nullptr, m_desktopSrv.pptr_cleared());
        if FAILED(hr) {
            logger << "Faile: CreateShaderResourceView: " << util::hresult_to_utf8(hr) << std::endl;
            return false;
        }

        // create vertex buffers
        VERTEX desktopVertices[] = {
            //  X  |   Y  |  Z  |  U  |  V   |
            { -1.0f,  1.0f, 0.0f, 0.0f, 0.0f }, // LEFT TOP
            {  1.0f, -1.0f, 0.0f, 1.0f, 1.0f }, // RIGHT BOTTOM
            { -1.0f, -1.0f, 0.0f, 0.0f, 1.0f }, // LEFT BOTTOM
            { -1.0f,  1.0f, 0.0f, 0.0f, 0.0f }, // LEFT TOP
            {  1.0f,  1.0f, 0.0f, 1.0f, 0.0f }, // RIGHT TOP
            {  1.0f, -1.0f, 0.0f, 1.0f, 1.0f }  // RIGHT BOTTOM
        };
        D3D10_BUFFER_DESC desktopVBufferDesc = {
            .ByteWidth = sizeof(desktopVertices),
            .Usage = D3D10_USAGE_IMMUTABLE,
            .BindFlags = D3D10_BIND_VERTEX_BUFFER,
            .CPUAccessFlags = 0,
            .MiscFlags = 0
        };
        D3D10_SUBRESOURCE_DATA desktopVBufferData = {
            .pSysMem = desktopVertices,
            .SysMemPitch = 0,
            .SysMemSlicePitch = 0
        };
        hr = m_device->CreateBuffer(&desktopVBufferDesc, &desktopVBufferData, m_desktopVBuffer.pptr_cleared());
        if FAILED(hr) {
            logger << "FAILED: CreateBuffer (desktopVBuffer): " << util::hresult_to_utf8(hr) << std::endl;
            return false;
        }

        return true;
    }

    bool setupCursorTextureAndVertices()
    {
        HRESULT hr;

        m_cursorTexture = com_ptr<ID3D10Texture2D>::take(m_source.createCursorTexture());
        if (!m_cursorTexture)
            return false;

        hr = m_device->CreateShaderResourceView(m_cursorTexture, nullptr, m_cursorSrv.pptr_cleared());
        if FAILED(hr) {
            logger << "Fail: CreateShaderResourceView: " << util::hresult_to_utf8(hr) << std::endl;
            return false;
        }

        D3D10_TEXTURE2D_DESC texdsc;
        m_cursorTexture->GetDesc(&texdsc);
        m_cursorWidth  = texdsc.Width;
        m_cursorHeight = texdsc.Height;
        logger << "Cursor size: width=" << m_cursorWidth << " height=" << m_cursorHeight << std::endl;

        // the cursor needs a vertex buffer, too!
        VERTEX vertices[6] = {
            //  X  |   Y  |  Z  |  U  |  V   |
            {  0.0f,  0.0f, 0.0f, 0.0f, 0.0f }, // LEFT TOP
            {  0.0f,  0.0f, 0.0f, 1.0f, 1.0f }, // RIGHT BOTTOM
            {  0.0f,  0.0f, 0.0f, 0.0f, 1.0f }, // LEFT BOTTOM
            {  0.0f,  0.0f, 0.0f, 0.0f, 0.0f }, // LEFT TOP
            {  0.0f,  0.0f, 0.0f, 1.0f, 0.0f }, // RIGHT TOP
            {  0.0f,  0.0f, 0.0f, 1.0f, 1.0f }  // RIGHT BOTTOM
        };
        D3D10_BUFFER_DESC vbufferDesc = {
            .ByteWidth = sizeof(vertices),
            .Usage = D3D10_USAGE_DYNAMIC,
            .BindFlags = D3D10_BIND_VERTEX_BUFFER,
            .CPUAccessFlags = D3D10_CPU_ACCESS_WRITE,
            .MiscFlags = 0
        };
        D3D10_SUBRESOURCE_DATA vbufferData = {
            .pSysMem = vertices,
            .SysMemPitch = 0,
            .SysMemSlicePitch = 0
        };
        hr = m_device->CreateBuffer(&vbufferDesc, &vbufferData, m_cursorVBuffer.pptr_cleared());
        if FAILED(hr) {
            logger << "FAILED: CreateBuffer (cursorVBuffer): " << util::hresult_to_utf8(hr) << std::endl;
            return false;
        }

        return true;
    }

    void updateCursorPosition()
    {
        if (!m_cursorVBuffer)
            return;

        float left   = -1.0f + 2.0f*static_cast<float>(m_cursorX)/static_cast<float>(m_desktopWidth);
        float top    =  1.0f - 2.0f*static_cast<float>(m_cursorY)/static_cast<float>(m_desktopHeight);
        float right  =  left + 2.0f*static_cast<float>(m_cursorWidth)/static_cast<float>(m_desktopWidth);
        float bottom =  top  - 2.0f*static_cast<float>(m_cursorHeight)/static_cast<float>(m_desktopHeight);
        float uleft   = 0.0f;
        float vtop    = 0.0f;
        float uright  = 1.0f;
        float vbottom = 1.0f;

        // now write this into the vertex buffer
        VERTEX *vertices = nullptr;

        HRESULT hr = m_cursorVBuffer->Map(D3D10_MAP_WRITE_DISCARD, 0, reinterpret_cast<void**>(&vertices));
        if FAILED(hr) {
            logger << "FAILED: ID3D10Buffer::Map: " << util::hresult_to_utf8(hr) << std::endl;
            return;
        }

        //  X   |   Y   |  Z  |   U   |  V     |
        vertices[0] = { left,  top,    0.0f, uleft,  vtop    }; // LEFT TOP
        vertices[1] = { right, bottom, 0.0f, uright, vbottom }; // RIGHT BOTTOM
        vertices[2] = { left,  bottom, 0.0f, uleft,  vbottom }; // LEFT BOTTOM
        vertices[3] = { left,  top,    0.0f, uleft,  vtop    }; // LEFT TOP
        vertices[4] = { right, top,    0.0f, uright, vtop    }; // RIGHT TOP
        vertices[5] = { right, bottom, 0.0f, uright, vbottom }; // RIGHT BOTTOM

        m_cursorVBuffer->Unmap();
    }

public:
    Renderer(HWND hwnd, int x, int y, int w, int h)
    {
        m_desktopWidth  = w;
        m_desktopHeight = h;

        if (!setupDxgiAndD3DDevice(hwnd))
            return;

        if (!setupShaders())
            return;

        if (!setupInputLayout())
            return;

        if (!setupSamplers())
            return;

        if (!setupBlendState())
            return;

        // sets render target and viewport
        RECT cr;
        GetClientRect(hwnd, &cr);
        this->resize(cr);

        this->reset(x, y, w, h);
    }

    void resize(const RECT& cr)
    {
        HRESULT hr;

        if (!m_device)
            return;

        // reset view
        m_device->OMSetRenderTargets(0, nullptr, nullptr);
        m_renderTarget.clear();

        hr = m_swap->ResizeBuffers(0, cr.right - cr.left, cr.bottom - cr.top, DXGI_FORMAT_UNKNOWN, 0);
        if FAILED(hr) {
            logger << "Failed to resize buffers :( " << util::hresult_to_utf8(hr) << std::endl;
        }

        // reset render target
        com_ptr<ID3D10Texture2D> backBuffer;
        hr = m_swap->GetBuffer(0, backBuffer.uuid(), backBuffer.pptr_as_void_cleared());
        if FAILED(hr) {
            logger << "Failed to get back buffer :( " << util::hresult_to_utf8(hr) << std::endl;
        }

        hr = m_device->CreateRenderTargetView(backBuffer, nullptr, m_renderTarget.pptr_cleared());
        if FAILED(hr) {
            logger << "Failed to create new render target view :( " << util::hresult_to_utf8(hr) << std::endl;
        }

        m_device->OMSetRenderTargets(1, m_renderTarget.pptr(), nullptr);

        // Create and set a viewport
        D3D10_VIEWPORT viewport = {
            .TopLeftX = 0,
            .TopLeftY = 0,
            .Width    = static_cast<UINT>(cr.right - cr.left),
            .Height   = static_cast<UINT>(cr.bottom - cr.top),
            .MinDepth = 0,
            .MaxDepth = 0
        };
        m_device->RSSetViewports(1, &viewport);
    }

    void reset(int x, int y, int w, int h)
    {
        m_desktopWidth  = w;
        m_desktopHeight = h;

        logger << "Resetting renderer to screen x="<<x<<" y="<<y<<" w="<<w<<" h="<<h << std::endl;

        m_source.reinit(m_device, x, y, w, h);

        setupDesktopTextureAndVertices();
        setupCursorTextureAndVertices();
    }

    void render() {
        if (!m_device || !m_renderTarget)
            return;

        // acquire and copy desktop texture
        m_source.acquireFrame();

        m_source.updateDesktop(m_desktopTexture);
        m_source.updateCursor(m_cursorTexture, m_cursorX, m_cursorY, m_cursorVisible);

        updateCursorPosition();

        m_source.releaseFrame();

        // draw the scene
        float gray[4] = { 0.5, 0.5, 0.5, 1.0 };
        m_device->ClearRenderTargetView(m_renderTarget, gray);

        UINT stride = sizeof(VERTEX);
        UINT offset = 0;
        m_device->IASetVertexBuffers(0, 1, m_desktopVBuffer.pptr(), &stride, &offset);
        m_device->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_device->PSSetShaderResources(0, 1, m_desktopSrv.pptr());
        m_device->Draw(6, 0);

        if (m_cursorVisible) {
            m_device->IASetVertexBuffers(0, 1, m_cursorVBuffer.pptr(), &stride, &offset);
            m_device->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            m_device->PSSetShaderResources(0, 1, m_cursorSrv.pptr());
            m_device->Draw(6, 0);
        }

        m_swap->Present(1, 0);
    }

    ~Renderer()
    {
        m_device->ClearState();
    }
};
