#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <cstdint>
#include "External/pl_mpeg/pl_mpeg.h"   // 実装は SceneManager.cpp で1回だけ（PL_MPEG_IMPLEMENTATION）

// 短い動画（MPEG-1）をループ再生して、テクスチャとして描けるようにする（用語集の説明用）
class VideoClip
{
public:
    ~VideoClip() { Close(); }

    bool Open(ID3D11Device* dev, const std::string& path)
    {
        Close();
        m_path = path;   // 開けなくても覚えておく（無いファイルを毎フレーム開き直さないため）
        m_plm = plm_create_with_filename(path.c_str());
        if (!m_plm) return false;
        plm_set_audio_enabled(m_plm, 0);
        plm_set_loop(m_plm, 1);
        m_w = plm_get_width(m_plm); m_h = plm_get_height(m_plm);
        m_rgba.assign((size_t)m_w * m_h * 4, 255);   // plm_frame_to_rgba は α を書かないので、不透明で埋めておく

        D3D11_TEXTURE2D_DESC d{};
        d.Width = m_w; d.Height = m_h; d.MipLevels = 1; d.ArraySize = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DYNAMIC; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(dev->CreateTexture2D(&d, nullptr, m_tex.GetAddressOf()))
            || FAILED(dev->CreateShaderResourceView(m_tex.Get(), nullptr, m_srv.GetAddressOf())))
        {
            Close(); m_path = path; return false;
        }
        plm_set_video_decode_callback(m_plm, &VideoClip::OnFrame, this);
        return true;
    }

    void Close()
    {
        if (m_plm) plm_destroy(m_plm);
        m_plm = nullptr; m_tex.Reset(); m_srv.Reset(); m_path.clear(); m_hasFrame = false;
    }

    // dt 秒ぶん進めて、新しいコマが出たらテクスチャへ書き込む
    void Update(ID3D11DeviceContext* ctx, double dt)
    {
        if (!m_plm) return;
        if (dt > 0.1) dt = 0.1;
        m_dirty = false;
        plm_decode(m_plm, dt);
        if (!m_dirty) return;
        D3D11_MAPPED_SUBRESOURCE m;
        if (FAILED(ctx->Map(m_tex.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
        for (int y = 0; y < m_h; y++)
            memcpy((uint8_t*)m.pData + y * m.RowPitch, m_rgba.data() + (size_t)y * m_w * 4, (size_t)m_w * 4);
        ctx->Unmap(m_tex.Get(), 0);
        m_hasFrame = true;
    }

    ID3D11ShaderResourceView* SRV() const { return m_hasFrame ? m_srv.Get() : nullptr; }
    const std::string& Path() const { return m_path; }

private:
    static void OnFrame(plm_t*, plm_frame_t* frame, void* user)
    {
        auto* self = static_cast<VideoClip*>(user);
        plm_frame_to_rgba(frame, self->m_rgba.data(), self->m_w * 4);
        self->m_dirty = true;
    }

    plm_t* m_plm = nullptr;
    int m_w = 0, m_h = 0;
    std::vector<uint8_t> m_rgba;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_tex;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv;
    std::string m_path;
    bool m_dirty = false, m_hasFrame = false;
};
