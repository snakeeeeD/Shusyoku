#pragma once
#include <d3d11.h>
#include <string>
#include <unordered_map>

class TextureManager
{
public:
    static bool Init(ID3D11Device* device);
    static void Shutdown();

    // テクスチャ読み込み
    static bool Load(const std::string& name, const wchar_t* filepath);

    // テクスチャ取得
    static ID3D11ShaderResourceView* Get(const std::string& name);

private:
    static ID3D11Device* m_device;
    static std::unordered_map<std::string, ID3D11ShaderResourceView*> m_textures;
};