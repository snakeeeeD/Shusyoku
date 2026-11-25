#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <DirectXMath.h>
#include "EffectPart.h"

using namespace DirectX;

// エフェクト用シェーダーの材質（effects.json の "materials"）。粒の見た目を決める
struct FxMaterial
{
    std::string shape, noise, ramp;   // 形・ノイズ・色の帯の画像（textures.json のキー）
    float erodeStart = 0.5f, erodeEnd = 1.0f, glow = 1.0f, noiseScale = 1.0f;
    XMFLOAT2 scroll = { 0, 0 };
    bool additive = false;            // 加算合成で光らせるか
    float appear = 0.0f;              // 出てくるのにかかる寿命の割合（0＝最初から全部。形の画像の G の順に出る）
};

// effects.json の演出1つ（"id" ごと）
struct EffectDef
{
    std::string id;
    std::vector<std::unique_ptr<EffectPartDef>> parts;   // 部品（粒・コマ送り…）。どの種類かはクラスで分かれる（EffectKinds）
    std::string then;                 // 着弾後などに続けて鳴らすエフェクトid
    float thenDelay = 0.0f;           // その遅延(秒)
    float shake = 0.0f;               // 再生時の画面振動の強さ(0~1)
    std::string se;                   // 再生時に鳴らすSE（Assets/Sound/se/ のファイル名）
    float seVol = 1.0f;               // そのSEの音量
    bool randomSide = false;          // 出すたびに、左右を逆にするかをランダムで決める（全部の部品が同じ側にそろう）
};

class EffectDataBase
{
public:
    static void Load(const std::string& path);
    static const EffectDef* Get(const std::string& id);
    static const FxMaterial* GetMaterial(const std::string& id);
private:
    static std::unordered_map<std::string, EffectDef> s_effects;
    static std::unordered_map<std::string, FxMaterial> s_materials;
};