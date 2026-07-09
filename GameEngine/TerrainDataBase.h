#pragma once
#include <string>
#include <unordered_map>
#include <DirectXMath.h>

using namespace DirectX;

struct TerrainDef {
    std::string id;
    std::wstring name;
    std::string texture = "";
    XMFLOAT4 color = { 1, 1, 1, 1 };
    std::string effect;
    int value = 0;
    std::string buffType = "";
    int buffDuration = 0;
    bool persistent = false;
    int duration = 0;
    int animFrames = 0;
    float animSpeed = 0.0f;
    bool aoe = false;
};

class TerrainDataBase
{
public:
    static void Load(const std::string& path);
    static const TerrainDef* Get(const std::string& id);

private:
    static std::unordered_map<std::string, TerrainDef> s_terrains;
};