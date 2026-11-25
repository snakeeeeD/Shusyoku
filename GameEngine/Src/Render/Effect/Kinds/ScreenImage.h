#pragma once
#include <string>
#include <d3d11.h>
#include <nlohmann/json.hpp>
#include "EffectPart.h"

// 画面に絵を1枚貼って、決めた所から動かして止める種類のデータ（effects.json の "parts" の "type": "screenImage"）。
// 位置は画面の割合（左上0,0～右下1,1）。出す場所（Play の x, y, z）は使わない。まるのみの顎（画面の上下から迫る）など
struct ScreenImageDef : EffectPartDef
{
    std::string texture;               // 絵（textures.json のキー）
    XMFLOAT4 rect = { 0, 0, 1, 1 };    // 止まる所（x0, y0, x1, y1。画面の割合。はみ出してもよい）
    XMFLOAT2 from = { 0, 0 };          // 動き始めのずれ（画面の割合。止まる所からこれだけずれた所から動く。0,0＝動かない）
    float delay = 0.0f;                // 出るまでの秒数
    float moveTime = 0.0f;             // 止まる所まで動く秒数
    float ease = 1.0f;                 // 動き方（1＝一定の速さ。大きいほど、最初ゆっくり→最後に一気に）
    float life = 1.0f;                 // 出てから消えるまでの秒数（delay の後から数える）
    float fadeIn = 0.0f;               // 出だしに濃くなる秒数
    float fadeOut = 0.0f;              // 最後に薄くなる秒数
    XMFLOAT4 color = { 1, 1, 1, 1 };
    bool additive = false;             // 加算合成で光らせる
    bool overUI = false;               // true なら UI・帯より上に描く（まるのみの顎など）。false なら3Dの上・UIの下

    static std::unique_ptr<EffectPartDef> Load(const nlohmann::json& j);   // 種類の表から呼ばれる
    std::unique_ptr<EffectPart> Spawn(const SpawnAt& at) const override;
};

// 出ている画面の絵1枚
class ScreenImage : public EffectPart
{
public:
    ScreenImage(const ScreenImageDef& def, const SpawnAt& at);
    void Update(float dt) override { m_elapsed += dt; }
    void Draw(Renderer3D* r, EffectLayer layer) override;   // 画面の順番でだけ描く（3Dの上・UIの下）
    bool IsDone() const override { return m_elapsed >= m_def.life; }
private:
    const ScreenImageDef& m_def;         // 位置・寿命などはデータを見に行く（データは起動中ずっと残る）
    ID3D11ShaderResourceView* m_tex = nullptr;
    XMFLOAT4 m_color = { 1, 1, 1, 1 };   // データの色×呼んだ側の色
    float m_elapsed = 0.0f;              // マイナスの間は待ち（delay）
};
