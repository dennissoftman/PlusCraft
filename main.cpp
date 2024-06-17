#include <spdlog/spdlog.h>

#include <SDL2/SDL.h>
#include <SDL_syswm.h>

#include "LoadEngineDll.h"
#include "EngineFactoryVk.h"
#include "EngineFactoryD3D11.h"
#include "EngineFactoryD3D12.h"

#include "RefCntAutoPtr.hpp"
#include "BasicMath.hpp"
#include "MapHelper.hpp"


// TODO: move to files
static const char *const vertex_shader_code = R"(
cbuffer Constants
{
    float4x4 g_WorldViewProj;
};

// Vertex shader takes two inputs: vertex position and color.
// By convention, Diligent Engine expects vertex shader inputs to be
// labeled 'ATTRIBn', where n is the attribute number.
struct VSInput
{
    float3 Pos   : ATTRIB0;
    float4 Color : ATTRIB1;
};

struct PSInput
{
    float4 Pos   : SV_POSITION;
    float4 Color : COLOR0;
};

// Note that if separate shader objects are not supported (this is only the case for old GLES3.0 devices), vertex
// shader output variable name must match exactly the name of the pixel shader input variable.
// If the variable has structure type (like in this example), the structure declarations must also be identical.
void main(in  VSInput VSIn,
          out PSInput PSIn)
{
    PSIn.Pos   = mul(float4(VSIn.Pos, 1.0), g_WorldViewProj);
    PSIn.Color = VSIn.Color;
}
)";

static const char *const pixel_shader_code = R"(
struct PSInput
{
    float4 Pos   : SV_POSITION;
    float4 Color : COLOR0;
};

struct PSOutput
{
    float4 Color : SV_TARGET;
};

// Note that if separate shader objects are not supported (this is only the case for old GLES3.0 devices), vertex
// shader output variable name must match exactly the name of the pixel shader input variable.
// If the variable has structure type (like in this example), the structure declarations must also be identical.
void main(in  PSInput  PSIn,
          out PSOutput PSOut)
{
    float4 Color = PSIn.Color;
    PSOut.Color = Color;
}
)";
//

enum WindowMode {
    WINDOWED,
    FULLSCREEN,
    BORDERLESS
};

struct VideoMode {
    int width, height;
    int syncInterval = 0;
    WindowMode windowMode = WINDOWED;
};


static SDL_Window *m_mainWindow = nullptr;
static bool m_windowShouldClose = false;

namespace dg = Diligent;

// Diligent structures
static dg::RefCntAutoPtr<dg::IRenderDevice> m_pDevice;
static dg::RefCntAutoPtr<dg::IDeviceContext> m_pImmediateContext;
static dg::RefCntAutoPtr<dg::ISwapChain> m_pSwapChain;
static dg::RefCntAutoPtr<dg::IPipelineState> m_pPSO;
static dg::RefCntAutoPtr<dg::IBuffer> m_pVSConstants;
static dg::RefCntAutoPtr<dg::IShaderResourceBinding> m_pSRB;

static dg::float4x4 m_projMatrix, m_viewMatrix, m_modelMatrix;
//

void cleanup() {
    if (m_mainWindow) SDL_DestroyWindow(m_mainWindow);
    SDL_Quit();
}

SDL_SysWMinfo GetNativeWindowInfo() {
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(m_mainWindow, &wmInfo);
    return wmInfo;
}

void InitializeGraphicsEngine(const VideoMode &videoMode,
                              dg::RENDER_DEVICE_TYPE renderDeviceType = dg::RENDER_DEVICE_TYPE_GL) {
    auto nativeWindowInfo = GetNativeWindowInfo();

    dg::SwapChainDesc SCDesc;
    SCDesc.Width = videoMode.width;
    SCDesc.Height = videoMode.height;

#ifdef __MACOSX__
    SCDesc.BufferCount = 3;
#endif

    switch (renderDeviceType) {
#ifdef _WIN32
        case dg::RENDER_DEVICE_TYPE_D3D11: {
            dg::EngineD3D11CreateInfo EngineCI;
#    if ENGINE_DLL
            // Load the dll and import GetEngineFactoryD3D11() function
            auto *GetEngineFactoryD3D11 = dg::LoadGraphicsEngineD3D11();
#    endif

            auto *pFactoryD3D11 = GetEngineFactoryD3D11();
            pFactoryD3D11->CreateDeviceAndContextsD3D11(EngineCI, &m_pDevice, &m_pImmediateContext);
            dg::Win32NativeWindow Window{nativeWindowInfo.info.win.window};
            pFactoryD3D11->CreateSwapChainD3D11(m_pDevice, m_pImmediateContext, SCDesc,
                                                dg::FullScreenModeDesc{}, Window, &m_pSwapChain);
            break;
        }

        case dg::RENDER_DEVICE_TYPE_D3D12: {
#    if ENGINE_DLL
            // Load the dll and import GetEngineFactoryD3D12() function
            auto GetEngineFactoryD3D12 = dg::LoadGraphicsEngineD3D12();
#endif
            dg::EngineD3D12CreateInfo EngineCI;

            auto *pFactoryD3D12 = GetEngineFactoryD3D12();
            pFactoryD3D12->CreateDeviceAndContextsD3D12(EngineCI, &m_pDevice, &m_pImmediateContext);
            dg::Win32NativeWindow Window{nativeWindowInfo.info.win.window};
            pFactoryD3D12->CreateSwapChainD3D12(m_pDevice, m_pImmediateContext, SCDesc,
                                                dg::FullScreenModeDesc{}, Window, &m_pSwapChain);
            break;
        }
#endif
        case dg::RENDER_DEVICE_TYPE_VULKAN: {
#    if EXPLICITLY_LOAD_ENGINE_VK_DLL
            // Load the dll and import GetEngineFactoryVk() function
            auto GetEngineFactoryVk = dg::LoadGraphicsEngineVk();
#else
            auto GetEngineFactoryVk = dg::GetEngineFactoryVk;
#endif
            dg::EngineVkCreateInfo EngineCI;

            auto *pFactoryVk = GetEngineFactoryVk();
            pFactoryVk->CreateDeviceAndContextsVk(EngineCI, &m_pDevice, &m_pImmediateContext);

#ifdef __MACOSX__
            dg::MacOSNativeWindow Window{nativeWindowInfo.info.cocoa.window};
#elif _WIN32
            dg::Win32NativeWindow Window{nativeWindowInfo.info.win.window};
#endif
            pFactoryVk->CreateSwapChainVk(m_pDevice, m_pImmediateContext, SCDesc, Window, &m_pSwapChain);
            break;
        }
        default:
            throw std::runtime_error("Unsupported render device type");
    }
}

// Events
void OnResize(const int width, const int height) {
    m_projMatrix = dg::float4x4::Projection(M_PI_2, static_cast<float>(width) / static_cast<float>(height), 0.1f, 1000.f, false);
    m_pSwapChain->Resize(width, height);
}

int main(int argc, char **argv) {
    if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        spdlog::error("SDL2 init failed: {}", SDL_GetError());
        return -1;
    }
    spdlog::debug("SDL2 init OK");

    atexit(cleanup);

    VideoMode videoMode{
        1920, 1080,
        1, WindowMode::WINDOWED
    };

    Uint32 winFlags = SDL_WINDOW_SHOWN;

    if (videoMode.windowMode == WindowMode::WINDOWED)
        winFlags |= SDL_WINDOW_RESIZABLE;
    else if (videoMode.windowMode == WindowMode::FULLSCREEN)
        winFlags |= SDL_WINDOW_FULLSCREEN;
    else if (videoMode.windowMode == WindowMode::BORDERLESS)
        winFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    m_mainWindow = SDL_CreateWindow("PlusCraft",
                                    SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                    videoMode.width, videoMode.height,
                                    winFlags);

    if (m_mainWindow == nullptr) {
        spdlog::error("Failed to create SDL2 window: {}", SDL_GetError());
        return -3;
    }

    //
    try {
        InitializeGraphicsEngine(videoMode, dg::RENDER_DEVICE_TYPE_D3D11);
    } catch (std::exception &e) {
        spdlog::error("DiligentEngine Init failed: {}", e.what());
        return -5;
    }

    // Init PSO
    dg::GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PSOCreateInfo.PSODesc.Name = "Triangle PSO";
    PSOCreateInfo.PSODesc.PipelineType = dg::PIPELINE_TYPE_GRAPHICS;

    PSOCreateInfo.GraphicsPipeline.NumRenderTargets = 1;
    PSOCreateInfo.GraphicsPipeline.RTVFormats[0] = m_pSwapChain->GetDesc().ColorBufferFormat;
    PSOCreateInfo.GraphicsPipeline.DSVFormat = m_pSwapChain->GetDesc().DepthBufferFormat;
    PSOCreateInfo.GraphicsPipeline.PrimitiveTopology = dg::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = dg::CULL_MODE_BACK;
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = true;

    // Create shaders
    dg::ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage = dg::SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.Desc.UseCombinedTextureSamplers = true;
    dg::RefCntAutoPtr<dg::IShader> pVS; {
        ShaderCI.Desc.ShaderType = dg::SHADER_TYPE_VERTEX;
        ShaderCI.EntryPoint = "main";
        ShaderCI.Desc.Name = "Triangle vertex shader";
        ShaderCI.Source = vertex_shader_code;
        m_pDevice->CreateShader(ShaderCI, &pVS);
    }
    dg::RefCntAutoPtr<dg::IShader> pPS; {
        ShaderCI.Desc.ShaderType = dg::SHADER_TYPE_PIXEL;
        ShaderCI.EntryPoint = "main";
        ShaderCI.Desc.Name = "Triangle pixel shader";
        ShaderCI.Source = pixel_shader_code;
        m_pDevice->CreateShader(ShaderCI, &pPS);
    }

    // Buffers
    dg::BufferDesc CBDesc;
    CBDesc.Name = "VS constants CB";
    CBDesc.Size = sizeof(dg::float4x4);
    CBDesc.Usage = dg::USAGE_DYNAMIC;
    CBDesc.BindFlags = dg::BIND_UNIFORM_BUFFER;
    CBDesc.CPUAccessFlags = dg::CPU_ACCESS_WRITE;
    m_pDevice->CreateBuffer(CBDesc, nullptr, &m_pVSConstants);

    // Vertex Input Layout
    dg::LayoutElement LayoutElements[] = {
        dg::LayoutElement{0, 0, 3, dg::VT_FLOAT32, false},
        dg::LayoutElement{1, 0, 4, dg::VT_FLOAT32, false}
    };
    PSOCreateInfo.GraphicsPipeline.InputLayout.LayoutElements = LayoutElements;
    PSOCreateInfo.GraphicsPipeline.InputLayout.NumElements = std::size(LayoutElements);

    PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType = dg::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;

    // Link shaders
    PSOCreateInfo.pVS = pVS;
    PSOCreateInfo.pPS = pPS;
    m_pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pPSO);

    // Bind constants
    m_pPSO->GetStaticVariableByName(dg::SHADER_TYPE_VERTEX, "Constants")->Set(m_pVSConstants);

    // Vertex buffer
    struct Vertex {
        dg::float3 pos;
        dg::float4 color;
    };
    Vertex CubeVerts[] =
    {
        {dg::float3(-1, -1, -1), dg::float4(1, 0, 0, 1)},
        {dg::float3(-1, +1, -1), dg::float4(0, 1, 0, 1)},
        {dg::float3(+1, +1, -1), dg::float4(0, 0, 1, 1)},
        {dg::float3(+1, -1, -1), dg::float4(1, 1, 1, 1)},

        {dg::float3(-1, -1, +1), dg::float4(1, 1, 0, 1)},
        {dg::float3(-1, +1, +1), dg::float4(0, 1, 1, 1)},
        {dg::float3(+1, +1, +1), dg::float4(1, 0, 1, 1)},
        {dg::float3(+1, -1, +1), dg::float4(0.2f, 0.2f, 0.2f, 1)},
    };

    uint32_t CubeIndices[] =
    {
        2, 0, 1, 2, 3, 0,
        4, 6, 5, 4, 7, 6,
        0, 7, 4, 0, 3, 7,
        1, 0, 4, 1, 4, 5,
        1, 5, 2, 5, 6, 2,
        3, 6, 7, 3, 2, 6
    };

    dg::RefCntAutoPtr<dg::IBuffer> cubeVertexBuffer, cubeIndexBuffer;

    dg::BufferDesc VertBuffDesc;
    VertBuffDesc.Name = "Cube vertex buffer";
    VertBuffDesc.Usage = dg::USAGE_IMMUTABLE;
    VertBuffDesc.BindFlags = dg::BIND_VERTEX_BUFFER;
    VertBuffDesc.Size = sizeof(CubeVerts);
    dg::BufferData VBData;
    VBData.pData = CubeVerts;
    VBData.DataSize = sizeof(CubeVerts);
    m_pDevice->CreateBuffer(VertBuffDesc, &VBData, &cubeVertexBuffer);

    dg::BufferDesc IndBuffDesc;
    IndBuffDesc.Name = "Cube index buffer";
    IndBuffDesc.Usage = dg::USAGE_IMMUTABLE;
    IndBuffDesc.BindFlags = dg::BIND_INDEX_BUFFER;
    IndBuffDesc.Size = sizeof(CubeIndices);
    dg::BufferData IBData;
    IBData.pData = CubeIndices;
    IBData.DataSize = sizeof(CubeIndices);
    m_pDevice->CreateBuffer(IndBuffDesc, &IBData, &cubeIndexBuffer);

    // Init SRB
    m_pPSO->CreateShaderResourceBinding(&m_pSRB, true);

    dg::float4 ClearColor = {0.350f, 0.350f, 0.350f, 1.0f};

    m_projMatrix = dg::float4x4::Projection(M_PI_2, 16.f / 9.f, 0.1f, 1000.f, false);
    m_viewMatrix = dg::float4x4::Identity();
    m_modelMatrix = dg::float4x4::Identity();

    SDL_Event ev;
    do {
        // Poll events
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT: {
                    m_windowShouldClose = true;
                    break;
                }
                case SDL_WINDOWEVENT: {
                    switch (ev.window.event) {
                        case SDL_WINDOWEVENT_RESIZED:
                        case SDL_WINDOWEVENT_SIZE_CHANGED: {
                            int newWidth = ev.window.data1, newHeight = ev.window.data2;
                            OnResize(newWidth, newHeight);
                            break;
                        }
                        default:
                            break;
                    }
                    break;
                }
                default:
                    break;
            }
        }

        // Render
        auto *pRTV = m_pSwapChain->GetCurrentBackBufferRTV();
        auto *pDSV = m_pSwapChain->GetDepthBufferDSV();

        m_pImmediateContext->SetRenderTargets(1, &pRTV, pDSV, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_pImmediateContext->ClearRenderTarget(pRTV, ClearColor.Data(), dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_pImmediateContext->ClearDepthStencil(pDSV, dg::CLEAR_DEPTH_FLAG, 1.f, 0,
                                               dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        uint64_t offset = 0;
        dg::IBuffer *pBuffs[] = {cubeVertexBuffer};
        m_pImmediateContext->SetVertexBuffers(0, 1, pBuffs, &offset, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                              dg::SET_VERTEX_BUFFERS_FLAG_RESET);
        m_pImmediateContext->SetIndexBuffer(cubeIndexBuffer, 0, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_pImmediateContext->SetPipelineState(m_pPSO);

        m_modelMatrix = dg::float4x4::RotationY(SDL_GetTicks() / 500.f) * dg::float4x4::Translation(0.f, 0.f, 5.f);

        {
            dg::MapHelper<dg::float4x4> CBConstants(m_pImmediateContext, m_pVSConstants, dg::MAP_WRITE,
                                                    dg::MAP_FLAG_DISCARD);
            *CBConstants = (m_modelMatrix * m_viewMatrix * m_projMatrix).Transpose();
        }

        m_pImmediateContext->CommitShaderResources(m_pSRB, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        dg::DrawIndexedAttribs drawAttrs;
        drawAttrs.IndexType = dg::VT_UINT32;
        drawAttrs.NumIndices = 36;
        drawAttrs.Flags = dg::DRAW_FLAG_VERIFY_ALL;
        m_pImmediateContext->DrawIndexed(drawAttrs);

        m_pSwapChain->Present(videoMode.syncInterval);
    } while (!m_windowShouldClose);

    return 0;
}
