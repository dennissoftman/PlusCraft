#include <spdlog/spdlog.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_vulkan.h>
#include <SDL_syswm.h>

#include "EngineFactoryOpenGL.h"
#include "EngineFactoryVk.h"
#include "DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "DiligentCore/Common/interface/BasicMath.hpp"


// TODO: move to files
static const char *const vertex_shader_code = R"(
struct PSInput
{
    float4 Pos   : SV_POSITION;
    float3 Color : COLOR;
};

void main(in  uint    VertId : SV_VertexID,
          out PSInput PSIn)
{
    float4 Pos[3];
    Pos[0] = float4(-0.5, -0.5, 0.0, 1.0);
    Pos[1] = float4( 0.0, +0.5, 0.0, 1.0);
    Pos[2] = float4(+0.5, -0.5, 0.0, 1.0);

    float3 Col[3];
    Col[0] = float3(1.0, 0.0, 0.0); // red
    Col[1] = float3(0.0, 1.0, 0.0); // green
    Col[2] = float3(0.0, 0.0, 1.0); // blue

    PSIn.Pos   = Pos[VertId];
    PSIn.Color = Col[VertId];
}
)";

static const char *const pixel_shader_code = R"(
struct PSInput
{
    float4 Pos   : SV_POSITION;
    float3 Color : COLOR;
};

struct PSOutput
{
    float4 Color : SV_TARGET;
};

void main(in  PSInput  PSIn,
          out PSOutput PSOut)
{
    PSOut.Color = float4(PSIn.Color.rgb, 1.0);
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
    int syncInterval=0;
    WindowMode windowMode=WINDOWED;
};


static SDL_Window *m_mainWindow = nullptr;
static bool m_windowShouldClose = false;

namespace dg = Diligent;

// Diligent structures
static dg::RefCntAutoPtr<dg::IRenderDevice> m_pDevice;
static dg::RefCntAutoPtr<dg::IDeviceContext> m_pImmediateContext;
static dg::RefCntAutoPtr<dg::ISwapChain> m_pSwapChain;
static dg::RefCntAutoPtr<dg::IPipelineState> m_pPSO;
//

void cleanup()
{
    if (m_mainWindow) SDL_DestroyWindow(m_mainWindow);
    SDL_Quit();
}

SDL_SysWMinfo GetNativeWindowInfo()
{
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(m_mainWindow, &wmInfo);
    return wmInfo;
}

void InitializeGraphicsEngine(const VideoMode &videoMode, dg::RENDER_DEVICE_TYPE renderDeviceType = dg::RENDER_DEVICE_TYPE_GL)
{
    auto nativeWindowInfo = GetNativeWindowInfo();

    dg::SwapChainDesc SCDesc;
    SCDesc.Width = videoMode.width;
    SCDesc.Height = videoMode.height;

#ifdef __MACOSX__
    SCDesc.BufferCount = 3;
#endif

    switch (renderDeviceType)
    {
#ifdef _WIN32
        case dg::RENDER_DEVICE_TYPE_D3D11:
        {
            dg::EngineD3D11CreateInfo EngineCI;
#    if ENGINE_DLL
            // Load the dll and import GetEngineFactoryD3D11() function
            auto* GetEngineFactoryD3D11 = dg::LoadGraphicsEngineD3D11();
#    endif
            auto* pFactoryD3D11 = dg::GetEngineFactoryD3D11();
            pFactoryD3D11->CreateDeviceAndContextsD3D11(EngineCI, &m_pDevice, &m_pImmediateContext);
            dg::Win32NativeWindow Window{nativeWindowInfo.info.win.window};
            pFactoryD3D11->CreateSwapChainD3D11(m_pDevice, m_pImmediateContext, SCDesc,
                                                dg::FullScreenModeDesc{}, Window, &m_pSwapChain);
            break;
        }

        case RENDER_DEVICE_TYPE_D3D12:
        {
#    if ENGINE_DLL
            // Load the dll and import GetEngineFactoryD3D12() function
            auto GetEngineFactoryD3D12 = dg::LoadGraphicsEngineD3D12();
#    endif
            dg::EngineD3D12CreateInfo EngineCI;

            auto* pFactoryD3D12 = dg::GetEngineFactoryD3D12();
            pFactoryD3D12->CreateDeviceAndContextsD3D12(EngineCI, &m_pDevice, &m_pImmediateContext);
            dg::Win32NativeWindow Window{nativeWindowInfo.info.win.window};
            pFactoryD3D12->CreateSwapChainD3D12(m_pDevice, m_pImmediateContext, SCDesc,
                                                dg::FullScreenModeDesc{}, Window, &m_pSwapChain);
            break;
        }
#endif
        case dg::RENDER_DEVICE_TYPE_GL:
        {
#    if EXPLICITLY_LOAD_ENGINE_GL_DLL
            // Load the dll and import GetEngineFactoryOpenGL() function
            auto GetEngineFactoryOpenGL = dg::LoadGraphicsEngineOpenGL();
#    endif
            auto *pFactoryOpenGL = dg::GetEngineFactoryOpenGL();

            dg::EngineGLCreateInfo EngineCI;
#ifdef __MACOSX__
            EngineCI.Window.pNSView = nativeWindowInfo.info.cocoa.window;
#elif _WIN32
            EngineCI.Window.hWnd = nativeWindowInfo.info.win.window;
#endif

            pFactoryOpenGL->CreateDeviceAndSwapChainGL(EngineCI, &m_pDevice, &m_pImmediateContext,
                                                       SCDesc, &m_pSwapChain);
            break;
        }
        case dg::RENDER_DEVICE_TYPE_VULKAN:
        {
#    if EXPLICITLY_LOAD_ENGINE_VK_DLL
            // Load the dll and import GetEngineFactoryVk() function
            auto GetEngineFactoryVk = dg::LoadGraphicsEngineVk();
#    endif
            dg::EngineVkCreateInfo EngineCI;

            auto *pFactoryVk = dg::GetEngineFactoryVk();
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

int main()
{
    if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        spdlog::error("SDL2 init failed: {}", SDL_GetError());
        return -1;
    }
    spdlog::debug("SDL2 init OK");

    atexit(cleanup);

    VideoMode videoMode{
        1280, 720,
        1, WindowMode::WINDOWED
    };

    Uint32 winFlags = SDL_WINDOW_SHOWN;

    if(videoMode.windowMode == WindowMode::WINDOWED)
        winFlags |= SDL_WINDOW_RESIZABLE;
    else if(videoMode.windowMode == WindowMode::FULLSCREEN)
        winFlags |= SDL_WINDOW_FULLSCREEN;
    else if(videoMode.windowMode == WindowMode::BORDERLESS)
        winFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    // IF OPENGL / VULKAN / etc...
    winFlags |= SDL_WINDOW_OPENGL;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    m_mainWindow = SDL_CreateWindow("PlusCraft",
                                    SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                    videoMode.width, videoMode.height,
                                    winFlags);

    if (m_mainWindow == nullptr)
    {
        spdlog::error("Failed to create SDL2 window: {}", SDL_GetError());
        return -3;
    }
    SDL_GL_CreateContext(m_mainWindow);

    //
    try
    {
        InitializeGraphicsEngine(videoMode, dg::RENDER_DEVICE_TYPE_GL);
    } catch (std::exception &e)
    {
        spdlog::error("DiligentEngine Init failed: {}", e.what());
        return -5;
    }

    // Init PSO
    dg::GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PSOCreateInfo.PSODesc.Name = "Triangle PSO";
    PSOCreateInfo.PSODesc.PipelineType = dg::PIPELINE_TYPE_GRAPHICS;

    PSOCreateInfo.GraphicsPipeline.NumRenderTargets = 1;
    PSOCreateInfo.GraphicsPipeline.RTVFormats[0]    = m_pSwapChain->GetDesc().ColorBufferFormat;
    PSOCreateInfo.GraphicsPipeline.DSVFormat        = m_pSwapChain->GetDesc().DepthBufferFormat;
    PSOCreateInfo.GraphicsPipeline.PrimitiveTopology = dg::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode = dg::CULL_MODE_NONE;
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;

    // Create shaders
    dg::ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage = dg::SHADER_SOURCE_LANGUAGE_HLSL;
    ShaderCI.Desc.UseCombinedTextureSamplers = true;
    dg::RefCntAutoPtr<dg::IShader> pVS;
    {
        ShaderCI.Desc.ShaderType = dg::SHADER_TYPE_VERTEX;
        ShaderCI.EntryPoint      = "main";
        ShaderCI.Desc.Name       = "Triangle vertex shader";
        ShaderCI.Source          = vertex_shader_code;
        m_pDevice->CreateShader(ShaderCI, &pVS);
    }
    dg::RefCntAutoPtr<dg::IShader> pPS;
    {
        ShaderCI.Desc.ShaderType = dg::SHADER_TYPE_PIXEL;
        ShaderCI.EntryPoint      = "main";
        ShaderCI.Desc.Name       = "Triangle pixel shader";
        ShaderCI.Source          = pixel_shader_code;
        m_pDevice->CreateShader(ShaderCI, &pPS);
    }

    // Link shaders
    PSOCreateInfo.pVS = pVS;
    PSOCreateInfo.pPS = pPS;
    m_pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pPSO);
    //

    dg::float4 ClearColor = {  0.350f,  0.350f,  0.350f, 1.0f };

    SDL_Event ev;
    do
    {
        // Poll events
        while (SDL_PollEvent(&ev))
        {
            if (ev.type == SDL_QUIT)
            {
                m_windowShouldClose = true;
                break;
            }
        }

        // Render
        auto* pRTV = m_pSwapChain->GetCurrentBackBufferRTV();
        auto* pDSV = m_pSwapChain->GetDepthBufferDSV();

        m_pImmediateContext->SetRenderTargets(1, &pRTV, pDSV, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_pImmediateContext->ClearRenderTarget(pRTV, ClearColor.Data(), dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_pImmediateContext->ClearDepthStencil(pDSV, dg::CLEAR_DEPTH_FLAG, 1.f, 0,
                                               dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_pImmediateContext->SetPipelineState(m_pPSO);

        dg::DrawAttribs drawAttrs;
        drawAttrs.NumVertices = 3;
        m_pImmediateContext->Draw(drawAttrs);

        m_pSwapChain->Present();
    } while (!m_windowShouldClose);

    return 0;
}
