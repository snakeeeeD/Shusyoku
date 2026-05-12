#include "TextureManager.h"
#include "TextureLoader.h"

ID3D11Device* TextureManager::m_device = nullptr;
std::unordered_map<std::string, ID3D11ShaderResourceView*> TextureManager::m_textures;

bool TextureManager::Init(ID3D11Device* device)
{
    m_device = device;
    return true;
}

void TextureManager::Shutdown()
{
    for (auto& pair : m_textures)
    {
        if (pair.second)
            pair.second->Release();
    }
    m_textures.clear();
}

bool TextureManager::Load(const std::string& name, const wchar_t* filepath)
{
    // すでに読み込み済みならスキップ
    if (m_textures.count(name) > 0)
        return true;

    ID3D11ShaderResourceView* texture = nullptr;
    if (!TextureLoader::LoadFromFile(m_device, filepath, &texture))
    {
        OutputDebugStringA(("TextureManager: 読み込み失敗 " + name + "\n").c_str());
        return false;
    }

    m_textures[name] = texture;
    return true;
}

ID3D11ShaderResourceView* TextureManager::Get(const std::string& name)
{
    auto it = m_textures.find(name);
    if (it == m_textures.end())
    {
        OutputDebugStringA(("TextureManager: 見つかりません " + name + "\n").c_str());
        return nullptr;
    }
    return it->second;
}