#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include "Game.h"
#include "input.h"
#include "Settings.h"
#include "RenderConfig.h"
#ifdef _DEBUG
#include "External/imgui/imgui.h"
#include "External/imgui/backends/imgui_impl_win32.h"
#include "External/imgui/backends/imgui_impl_dx11.h"
#endif

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "ole32.lib")

// �O���[�o���ϐ�
HWND g_hWnd = nullptr;
ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pImmediateContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_pRenderTargetView = nullptr;

int g_renderWidth = LOGICAL_WIDTH;
int g_renderHeight = LOGICAL_HEIGHT;

Game* g_game = nullptr;

const int SCREEN_WIDTH = 1280;
const int SCREEN_HEIGHT = 720;

// �O���錾
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
bool InitWindow(HINSTANCE hInstance, int nCmdShow);
bool InitD3D();
void CleanupDevice();
void Render();

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine, _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    SetProcessDPIAware();

    Settings::Load();

    if (!InitWindow(hInstance, nCmdShow))
        return 0;

    if (!InitD3D())
    {
        CleanupDevice();
        CoUninitialize();
        return 0;
    }

    // メインループ
    MSG msg = { 0 };
    LARGE_INTEGER freq, prev, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    while (WM_QUIT != msg.message)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            QueryPerformanceCounter(&now);
            float deltaTime = (float)(now.QuadPart - prev.QuadPart) / freq.QuadPart;
            prev = now;
            if (deltaTime > 0.1f) deltaTime = 0.1f;   // ブレークポイントで飛ばないよう上限

            if (g_game)
            {
                g_game->HandleInput();
                g_game->Update(deltaTime);
            }
            Render();
        }
    }

    CleanupDevice();
    CoUninitialize();
    return (int)msg.wParam;
}

#ifdef _DEBUG
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
#ifdef _DEBUG
    if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
        return true;
#endif

    switch (message)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_MOUSEWHEEL:
        Input::SetWheelDelta(GET_WHEEL_DELTA_WPARAM(wParam));
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

static void ApplyDisplayMode(HWND hWnd, DisplayMode mode)
{
    if (mode == DisplayMode::Borderless)
    {
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongPtr(hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hWnd, HWND_TOP,
            mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right - mi.rcMonitor.left,
            mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    else // Windowed（1280x720固定・中央）
    {
        DWORD style = WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX;
        RECT rc = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };
        AdjustWindowRect(&rc, style, FALSE);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
        SetWindowLongPtr(hWnd, GWL_STYLE, style | WS_VISIBLE);
        SetWindowPos(hWnd, HWND_TOP, (sw - w) / 2, (sh - h) / 2, w, h,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
}

// シーンから呼ぶ実行時切替（swapchainはそのまま＝再起動不要で切り替わる）
void SetDisplayMode(DisplayMode mode)
{
    Settings::Get().displayMode = mode;
    Settings::Save();
    ApplyDisplayMode(g_hWnd, mode);
}

bool InitWindow(HINSTANCE hInstance, int nCmdShow)
{
    WNDCLASSEX wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = L"GameEngineClass";

    if (!RegisterClassEx(&wcex))
        return false;

    RECT rc = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    g_hWnd = CreateWindow(
        L"GameEngineClass",
        L"2D Game Engine - DirectX11",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!g_hWnd)
        return false;

    ApplyDisplayMode(g_hWnd, Settings::Get().displayMode);   // 窓 or ボーダーレス
    UpdateWindow(g_hWnd);

    return true;
}

bool InitD3D()
{
    HRESULT hr = S_OK;

    g_renderWidth = GetSystemMetrics(SM_CXSCREEN);
    g_renderHeight = GetSystemMetrics(SM_CYSCREEN);

    DXGI_SWAP_CHAIN_DESC sd = { 0 };
    sd.BufferCount = 1;
    sd.BufferDesc.Width = g_renderWidth;
    sd.BufferDesc.Height = g_renderHeight;
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = g_hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL featureLevel;

    hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        featureLevels, 1, D3D11_SDK_VERSION, &sd,
        &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pImmediateContext
    );

    if (FAILED(hr))
        return false;

    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (FAILED(hr))
        return false;

    hr = g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRenderTargetView);
    pBackBuffer->Release();
    if (FAILED(hr))
        return false;

    g_pImmediateContext->OMSetRenderTargets(1, &g_pRenderTargetView, nullptr);

    D3D11_VIEWPORT vp;  
    vp.Width = (FLOAT)g_renderWidth;
    vp.Height = (FLOAT)g_renderHeight;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    g_pImmediateContext->RSSetViewports(1, &vp);

    // 自前モード管理と競合するDXGIのAlt+Enterフルスクリーンを無効化
    IDXGIDevice* dxgiDev = nullptr;
    if (SUCCEEDED(g_pd3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev)))
    {
        IDXGIAdapter* ad = nullptr;
        if (SUCCEEDED(dxgiDev->GetAdapter(&ad)))
        {
            IDXGIFactory* fac = nullptr;
            if (SUCCEEDED(ad->GetParent(__uuidof(IDXGIFactory), (void**)&fac)))
            {
                fac->MakeWindowAssociation(g_hWnd, DXGI_MWA_NO_ALT_ENTER);
                fac->Release();
            }
            ad->Release();
        }
        dxgiDev->Release();
    }

    // �Q�[��������
    g_game = new Game();
    if (!g_game->Init(g_pd3dDevice, g_pImmediateContext, SCREEN_WIDTH, SCREEN_HEIGHT, g_hWnd, g_pSwapChain))
    {
        OutputDebugStringW(L"�� Game���������s\n");
        return false;
    }

#ifdef _DEBUG
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplWin32_Init(g_hWnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pImmediateContext);
    ImGui::StyleColorsDark();
#endif

    return true;
}

void Render()
{
    float ClearColor[4] = { 0.1f, 0.4f, 0.2f, 1.0f };
    g_pImmediateContext->ClearRenderTargetView(g_pRenderTargetView, ClearColor);

    // �Q�[���`��
    if (g_game)
    {
        g_game->Draw();
    }

#ifdef _DEBUG
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (g_game)
        g_game->DrawImGui();

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
#endif

    g_pSwapChain->Present(0, 0);
}

void CleanupDevice()
{
#ifdef _DEBUG
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
#endif

    if (g_pImmediateContext) g_pImmediateContext->ClearState();

    if (g_game)
    {
        delete g_game;
        g_game = nullptr;
    }

    if (g_pRenderTargetView) g_pRenderTargetView->Release();
    if (g_pSwapChain) g_pSwapChain->Release();
    if (g_pImmediateContext) g_pImmediateContext->Release();
    if (g_pd3dDevice) g_pd3dDevice->Release();
}