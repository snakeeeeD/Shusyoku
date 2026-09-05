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
    std::string texture;
};

struct SheetAnim
{
    std::string texture;              // シートのテクスチャID
    int cols = 1, rows = 1;           // 縦横のコマ数
    int frames = 0;                   // 総コマ数（0ならcols*rows）
    float fps = 20.0f;                // 再生速度
    float scale = 1.0f;               // 表示サイズ
    float yOffset = 0.0f;             // 発生位置のy補正
    XMFLOAT4 color = { 1, 1, 1, 1 };
    bool loop = false;                // falseで1周して消滅
};

struct EffectDef
{
    std::string id;
    std::vector<BurstDef> bursts;
    std::vector<SheetAnim> sheets;
};

class EffectDataBase
{
public:
    static void Load(const std::string& path);
    static const EffectDef* Get(const std::string& id);
private:
    static std::unordered_map<std::string, EffectDef> s_effects;
};