#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <DirectXMath.h>

using namespace DirectX;

struct BurstDef
{
    int count = 10;
    float speed = 1.5f, life = 0.5f, scale = 0.1f;
    float gravity = 4.0f, drag = 0.02f;
    XMFLOAT4 colorStart = { 1,1,1,1 };
    XMFLOAT4 colorEnd = { 1,1,1,0 };
};

struct EffectDef
{
    std::string id;
    std::vector<BurstDef> bursts;
};

class EffectDataBase
{
public:
    static void Load(const std::string& path);
    static const EffectDef* Get(const std::string& id);
private:
    static std::unordered_map<std::string, EffectDef> s_effects;
};