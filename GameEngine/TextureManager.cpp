#include "TextureManager.h"
#include "TextureLoader.h"
#include <fstream>
#include <nlohmann/json.hpp>

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
        //OutputDebugStringA(("TextureManager: 見つかりません " + name + "\n").c_str());
        return nullptr;
    }
    return it->second;
}
// 画像の一覧をまとめて読む。{ "groups": [ { "name": ..., "textures": [ { "key": ..., "path": ... } ] } ] }
// （パスは ASCII だけ。読み込み済みの key は Load が飛ばす）
int TextureManager::LoadList(const std::string& jsonPath)
{
    std::ifstream f(jsonPath);
    if (!f) { OutputDebugStringA(("TextureManager: list not found " + jsonPath + "\n").c_str()); return 0; }
    nlohmann::json j;
    try { f >> j; }
    catch (const std::exception& e) { OutputDebugStringA(("TextureManager: list parse error " + std::string(e.what()) + "\n").c_str()); return 0; }
    int n = 0;
    for (auto& g : j.value("groups", nlohmann::json::array()))
        for (auto& tx : g.value("textures", nlohmann::json::array()))
        {
            std::string key = tx.value("key", ""), path = tx.value("path", "");
            if (key.empty() || path.empty()) continue;
            std::wstring wpath(path.begin(), path.end());
            if (Load(key, wpath.c_str())) n++;
        }
    return n;
}
