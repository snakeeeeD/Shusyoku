#pragma once
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class TextureLoader
{
public:
    static bool LoadFromFile(ID3D11Device* device, const wchar_t* filename, ID3D11ShaderResourceView** textureView);

private:
    static bool LoadWICTexture(ID3D11Device* device, const wchar_t* filename,
        ID3D11Texture2D** texture, ID3D11ShaderResourceView** textureView);
};
