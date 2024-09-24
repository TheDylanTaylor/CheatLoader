#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>
#include "Fonts.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_internal.h"
#include "imgui_settings.h"
#include <d3d11.h>
#include <D3DX11.h>
#pragma comment (lib, "d3dx11.lib")
#include "hash.h"
#include "spoofcalls.hpp"

static int current_news = 0;
inline POINTS GuiPosition;
static ID3D11Device* g_pd3dDevice = NULL;
static ID3D11DeviceContext* g_pd3dDeviceContext = NULL;
static IDXGISwapChain* g_pSwapChain = NULL;
static ID3D11RenderTargetView* g_mainRenderTargetView = NULL;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

ID3D11ShaderResourceView* news[3] = { nullptr, nullptr, nullptr };
ID3D11ShaderResourceView* Product[3] = { nullptr, nullptr, nullptr };

ID3D11ShaderResourceView* avatar = nullptr;
ID3D11ShaderResourceView* bg = nullptr;

void LoadImages()
{
    rtx_spoof_func;

    D3DX11_IMAGE_LOAD_INFO iInfo;
    ID3DX11ThreadPump* threadPump{ nullptr };

    if (news[0] == nullptr) D3DX11CreateShaderResourceViewFromFile(g_pd3dDevice, OBFUSCATE_STR(L"C:\\codeine\\img\\news1.png"), &iInfo, threadPump, &news[0], 0);
    if (news[1] == nullptr) D3DX11CreateShaderResourceViewFromFile(g_pd3dDevice, OBFUSCATE_STR(L"C:\\codeine\\img\\news2.png"), &iInfo, threadPump, &news[1], 0);
    if (news[2] == nullptr) D3DX11CreateShaderResourceViewFromFile(g_pd3dDevice, OBFUSCATE_STR(L"C:\\codeine\\img\\news3.png"), &iInfo, threadPump, &news[2], 0);

    if (Product[0] == nullptr) D3DX11CreateShaderResourceViewFromFile(g_pd3dDevice, OBFUSCATE_STR(L"C:\\codeine\\img\\Rust.png"), &iInfo, threadPump, &Product[0], 0);
    if (Product[1] == nullptr) D3DX11CreateShaderResourceViewFromFile(g_pd3dDevice, OBFUSCATE_STR(L"C:\\codeine\\img\\Rust.png"), &iInfo, threadPump, &Product[1], 0);

    if (avatar == nullptr) D3DX11CreateShaderResourceViewFromFile(g_pd3dDevice, OBFUSCATE_STR(L"C:\\codeine\\img\\background.jpg"), &iInfo, threadPump, &avatar, 0);
    if (bg == nullptr) D3DX11CreateShaderResourceViewFromFile(g_pd3dDevice, OBFUSCATE_STR(L"C:\\codeine\\img\\background.jpg"), &iInfo, threadPump, &bg, 0);
}

static int menu_state = 0;

struct menu_anim
{
    float if_auth_offset;
    float main_loading_offset;
    float auth_loading;
    float inject_alpha;
    float inject_progress;
}menu;

void LoadingScreen(ImVec2 p)
{
    rtx_spoof_func;
    ImGui::GetWindowDrawList()->AddRect(p + ImVec2(10, 10), p + ImVec2(690, 130), second_color, 4.f);

    ImGui::PushClipRect(p + ImVec2(10, 10), p + ImVec2(690, 130), true);
    ImGui::GetWindowDrawList()->AddShadowRect(p + ImVec2(0 + menu.auth_loading * 2, 0), p + ImVec2(menu.auth_loading * 2, 450), main_color, 125.f, ImVec2(0, 0));
    ImGui::PopClipRect();

    ImGui::GetWindowDrawList()->AddRect(p + ImVec2(10, 140), p + ImVec2(310, 440), second_color, 4.f);

    ImGui::PushClipRect(p + ImVec2(10, 140), p + ImVec2(310, 440), true);
    ImGui::GetWindowDrawList()->AddShadowRect(p + ImVec2(0 + menu.auth_loading, 0), p + ImVec2(menu.auth_loading + 1, 450), main_color, 125.f, ImVec2(0, 0));
    ImGui::PopClipRect();

    ImGui::GetWindowDrawList()->AddRect(p + ImVec2(320, 140), p + ImVec2(690, 440), second_color, 4.f);

    ImGui::PushClipRect(p + ImVec2(320, 140), p + ImVec2(690, 440), true);
    ImGui::GetWindowDrawList()->AddShadowRect(p + ImVec2(320 + menu.auth_loading, 0), p + ImVec2(322 + menu.auth_loading, 450), main_color, 125.f, ImVec2(0, 0));
    ImGui::PopClipRect();

    menu.auth_loading = ImLinearSweep(menu.auth_loading, menu.if_auth_offset > 398.f ? 350.f : -90.f, ImGui::GetIO().DeltaTime * 200);

    if (menu.auth_loading == 350.f)
    {
        menu_state = 2;
        menu.auth_loading = 0;
    }
}

namespace ImGui
{
    int rotation_start_index;
    void ImRotateStart()
    {
        rtx_spoof_func;
        rotation_start_index = ImGui::GetWindowDrawList()->VtxBuffer.Size;
    }

    ImVec2 ImRotationCenter()
    {
        rtx_spoof_func;
        ImVec2 l(FLT_MAX, FLT_MAX), u(-FLT_MAX, -FLT_MAX); // bounds

        const auto& buf = ImGui::GetWindowDrawList()->VtxBuffer;
        for (int i = rotation_start_index; i < buf.Size; i++)
            l = ImMin(l, buf[i].pos), u = ImMax(u, buf[i].pos);

        return ImVec2((l.x + u.x) / 2, (l.y + u.y) / 2); // or use _ClipRectStack?
    }


    void ImRotateEnd(float rad, ImVec2 center = ImRotationCenter())
    {
        rtx_spoof_func;
        float s = sin(rad), c = cos(rad);
        center = ImRotate(center, s, c) - center;

        auto& buf = ImGui::GetWindowDrawList()->VtxBuffer;
        for (int i = rotation_start_index; i < buf.Size; i++)
            buf[i].pos = ImRotate(buf[i].pos, s, c) - center;
    }
}

void Trinage_background()
{
    rtx_spoof_func;
    ImVec2 screen_size = { (float)GetSystemMetrics(SM_CXSCREEN), (float)GetSystemMetrics(SM_CYSCREEN) };

    static ImVec2 particle_pos[100];
    static ImVec2 particle_target_pos[100];
    static float particle_speed[100];
    static float particle_size[100];
    static float particle_radius[100];
    static float particle_rotate[100];

    for (int i = 1; i < 50; i++)
    {
        if (particle_pos[i].x == 0 || particle_pos[i].y == 0)
        {
            particle_pos[i].x = rand() % (int)screen_size.x + 1;
            particle_pos[i].y = 15.f;
            particle_speed[i] = 1 + rand() % 25;
            particle_radius[i] = rand() % 4;
            particle_size[i] = rand() % 8;

            particle_target_pos[i].x = rand() % (int)screen_size.x;
            particle_target_pos[i].y = screen_size.y * 2;
        }

        particle_pos[i] = ImLerp(particle_pos[i], particle_target_pos[i], ImGui::GetIO().DeltaTime * (particle_speed[i] / 60));
        particle_rotate[i] += ImGui::GetIO().DeltaTime;

        if (particle_pos[i].y > screen_size.y)
        {
            particle_pos[i].x = 0;
            particle_pos[i].y = 0;
            particle_rotate[i] = 0;
        }

        ImGui::ImRotateStart();
        ImGui::GetWindowDrawList()->AddCircleFilled(particle_pos[i], particle_radius[i], IM_COL32(255, 255, 255, 255), 12);
        ImGui::ImRotateEnd(particle_rotate[i]);
    }
}

bool CreateDeviceD3D(HWND hWnd)
{
    rtx_spoof_func;
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_WARP, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    rtx_spoof_func;
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = NULL; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = NULL; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = NULL; }
}

void CreateRenderTarget()
{
    rtx_spoof_func;
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    rtx_spoof_func;
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = NULL; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    rtx_spoof_func;
    if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
        return true;

    switch (message)
    {
    case WM_SIZE:
    {
        if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();

            // Recalculate window position to keep it centered
            int screenWidth = GetSystemMetrics(SM_CXSCREEN);
            int screenHeight = GetSystemMetrics(SM_CYSCREEN);
            RECT windowRect;
            GetWindowRect(hWnd, &windowRect);
            int windowWidth = windowRect.right - windowRect.left;
            int windowHeight = windowRect.bottom - windowRect.top;
            int newLeft = (screenWidth - windowWidth) / 2;
            int newTop = (screenHeight - windowHeight) / 2;
            SetWindowPos(hWnd, NULL, newLeft, newTop, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
        return 0;
    }

    case WM_SYSCOMMAND:
    {
        if ((wParam & 0xFFF0) == SC_KEYMENU)
            return 0L;
    }

    case WM_DESTROY:
    {
        ::PostQuitMessage(0);
        return 0L;
    }

    case WM_LBUTTONDOWN:
    {
        GuiPosition = MAKEPOINTS(lParam);
        return 0L;
    }

    case WM_MOUSEMOVE:
    {
        if (wParam == MK_LBUTTON)
        {
            const auto points = MAKEPOINTS(lParam);
            auto rect = ::RECT{};
            GetWindowRect(hWnd, &rect);

            // Calculate the new window position
            int newLeft = rect.left + points.x - GuiPosition.x;
            int newTop = rect.top + points.y - GuiPosition.y;

            // Get screen dimensions
            int screenWidth = GetSystemMetrics(SM_CXSCREEN);
            int screenHeight = GetSystemMetrics(SM_CYSCREEN);

            // Calculate the window's half-width and half-height
            int windowWidth = rect.right - rect.left;
            int windowHeight = rect.bottom - rect.top;
            int halfWidth = windowWidth / 2;
            int halfHeight = windowHeight / 2;

            // Ensure the window stays centered if moved
            newLeft = max(min(newLeft, screenWidth - windowWidth), -halfWidth);
            newTop = max(min(newTop, screenHeight - windowHeight), -halfHeight);

            // Move the window
            SetWindowPos(hWnd, NULL, newLeft, newTop, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }

        return 0L;
    }
    }

    return ::DefWindowProc(hWnd, message, wParam, lParam);
}
