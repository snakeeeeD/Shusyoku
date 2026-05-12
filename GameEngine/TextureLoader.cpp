#include "TextureLoader.h"
#include <vector>

#pragma comment(lib, "windowscodecs.lib")

bool TextureLoader::LoadFromFile(ID3D11Device* device, const wchar_t* filename, ID3D11ShaderResourceView** textureView)
{
    ID3D11Texture2D* texture = nullptr;
    bool result = LoadWICTexture(device, filename, &texture, textureView);

    if (texture)
        texture->Release();

    return result;
}

bool TextureLoader::LoadWICTexture(ID3D11Device* device, const wchar_t* filename,
    ID3D11Texture2D** texture, ID3D11ShaderResourceView** textureView)
{
    HRESULT hr;

    // WICファクトリ作成
    ComPtr<IWICImagingFactory> wicFactory;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(wicFactory.GetAddressOf()));
    if (FAILED(hr))
        return false;

    // デコーダー作成
    ComPtr<IWICBitmapDecoder> decoder;
    hr = wicFactory->CreateDecoderFromFilename(filename, nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf());
    if (FAILED(hr))
        return false;

    // フレーム取得
    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.GetAddressOf());
    if (FAILED(hr))
        return false;

    // フォーマットコンバーター作成
    ComPtr<IWICFormatConverter> converter;
    hr = wicFactory->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr))
        return false;

    // RGBA32形式に変換
    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr))
        return false;

    // 画像サイズ取得
    UINT width, height;
    hr = converter->GetSize(&width, &height);
    if (FAILED(hr))
        return false;

    // ピクセルデータ読み込み
    UINT stride = width * 4;
    UINT imageSize = stride * height;
    std::vector<BYTE> pixels(imageSize);

    hr = converter->CopyPixels(nullptr, stride, imageSize, pixels.data());
    if (FAILED(hr))
        return false;

    // テクスチャ作成
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.CPUAccessFlags = 0;
    texDesc.MiscFlags = 0;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = pixels.data();
    initData.SysMemPitch = stride;

    hr = device->CreateTexture2D(&texDesc, &initData, texture);
    if (FAILED(hr))
        return false;

    // シェーダーリソースビュー作成
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    hr = device->CreateShaderResourceView(*texture, &srvDesc, textureView);
    if (FAILED(hr))
        return false;

    return true;
}
