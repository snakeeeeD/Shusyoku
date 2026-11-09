#pragma once
#ifdef _DEBUG
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdio>
#include <string>

// ゲーム内録画（Debug 専用）：F9 で開始／停止。描き終わった画面を 30コマ/秒で読み取り、
// ffmpeg に流し込んで Assets/Video/rec_日付_時刻.mpg（MPEG-1、画面の解像度のまま・最大1920幅・高画質）に書き出す。
// 用語集用の短い動画は、ここから見せたい場所を切り抜いて作る（全体を縮めると文字がつぶれるため）。
// 用語集の動画（VideoClip）の素材を撮るための道具。
class ClipRecorder
{
public:
    static void Frame(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc, HWND hwnd)
    {
        bool key = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
        if (key && !s_keyWas && GetForegroundWindow() == hwnd)
        {
            if (s_pipe) Stop(hwnd);
            else Start(dev, sc, hwnd);
        }
        s_keyWas = key;
        if (!s_pipe) return;

        // 30コマ/秒の間隔で読み取る（描画のコマ数に関係なく、動画の速さを実時間に合わせる）
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        double t = (double)(now.QuadPart - s_start.QuadPart) / s_freq.QuadPart;
        if (t < s_frames / FPS) return;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> back;
        if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)back.GetAddressOf()))) return;
        ctx->CopyResource(s_staging.Get(), back.Get());
        D3D11_MAPPED_SUBRESOURCE m;
        if (FAILED(ctx->Map(s_staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return;
        // 遅れた分は同じコマを重ねて、動画の長さを実時間とそろえる
        int write = 1;
        while (t >= (s_frames + write) / FPS && write < 10) write++;
        for (int k = 0; k < write; k++)
            for (UINT y = 0; y < s_h; y++)
                fwrite((const char*)m.pData + y * m.RowPitch, 1, s_w * 4, s_pipe);
        ctx->Unmap(s_staging.Get(), 0);
        s_frames += write;
    }

    static bool Recording() { return s_pipe != nullptr; }

private:
    static constexpr double FPS = 30.0;

    static void Start(ID3D11Device* dev, IDXGISwapChain* sc, HWND hwnd)
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> back;
        if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)back.GetAddressOf()))) return;
        D3D11_TEXTURE2D_DESC d; back->GetDesc(&d);
        d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
        s_staging.Reset();
        if (FAILED(dev->CreateTexture2D(&d, nullptr, s_staging.GetAddressOf()))) return;
        s_w = d.Width; s_h = d.Height;

        CreateDirectoryA("Recordings", nullptr);   // 録画は素材と分ける（配る Assets に混ざらないように）
        SYSTEMTIME st; GetLocalTime(&st);
        char name[96];
        sprintf_s(name, "Recordings/rec_%04d%02d%02d_%02d%02d%02d.mpg",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        char cmd[512];
        sprintf_s(cmd, "ffmpeg -y -loglevel error -f rawvideo -pix_fmt bgra -s %ux%u -r 30 -i - "
            "-vf scale='min(1920,iw)':-2:flags=lanczos -c:v mpeg1video -q:v 2 -g 30 -an \"%s\"", s_w, s_h, name);
        s_pipe = _popen(cmd, "wb");
        if (!s_pipe) { OutputDebugStringA("ClipRecorder: ffmpeg を起動できませんでした\n"); return; }
        QueryPerformanceFrequency(&s_freq);
        QueryPerformanceCounter(&s_start);
        s_frames = 0;
        SetWindowTextW(hwnd, L"Think or Sink  ● REC（F9で停止）");   // 画面に描くと動画に映るので、題名に出す
    }

    static void Stop(HWND hwnd)
    {
        _pclose(s_pipe);
        s_pipe = nullptr;
        s_staging.Reset();
        SetWindowTextW(hwnd, L"Think or Sink");
    }

    static inline FILE* s_pipe = nullptr;
    static inline Microsoft::WRL::ComPtr<ID3D11Texture2D> s_staging;
    static inline UINT s_w = 0, s_h = 0;
    static inline bool s_keyWas = false;
    static inline LARGE_INTEGER s_freq{}, s_start{};
    static inline long long s_frames = 0;
};
#endif
