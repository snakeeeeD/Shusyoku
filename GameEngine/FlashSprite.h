#pragma once
#include <string>
#include <d3d11.h>
#include <nlohmann/json.hpp>
#include "EffectPart.h"

// カメラに向いた絵を1枚、少しふくらませながら出して消す種類のデータ（effects.json の "parts" の "type": "flash"）。
// faceRot にすると、絵の右側が Play の rot の向き（(sin rot, 0, cos rot)）を指すように、画面の中で回す（ブロックの盾など）
struct FlashSpriteDef : EffectPartDef
{
    std::string texture;               // 絵（textures.json のキー）
    float life = 0.3f;                 // 出てから消えるまでの秒数
    float sizeStart = 0.4f;            // 最初の幅（ワールド）
    float sizeEnd = 0.45f;             // 最後の幅
    float aspect = 1.0f;               // 高さ÷幅
    float ease = 3.0f;                 // ふくらみ方の勢い（大きいほど最初に一気に）
    float fadeFrom = 0.3f;             // 寿命のどこから薄くなり始めるか（0～1）
    XMFLOAT4 color = { 1, 1, 1, 1 };
    bool additive = false;             // 加算合成で光らせる
    bool faceRot = false;              // 絵の右側を rot の向きに向ける

    static std::unique_ptr<EffectPartDef> Load(const nlohmann::json& j);   // 種類の表から呼ばれる
    std::unique_ptr<EffectPart> Spawn(const SpawnAt& at) const override;
};

// 出ている絵1枚
class FlashSprite : public EffectPart
{
public:
    FlashSprite(const FlashSpriteDef& def, const SpawnAt& at);
    void Update(float dt) override { m_elapsed += dt; }
    void Draw(Renderer3D* r, EffectLayer layer) override;   // 出した所の手前側なら上の順番（キャラの前）、奥側なら地面の順番（キャラの後ろ）
    bool IsDone() const override { return m_elapsed >= m_def.life; }
private:
    const FlashSpriteDef& m_def;         // 寿命・大きさなどはデータを見に行く（データは起動中ずっと残る）
    ID3D11ShaderResourceView* m_tex = nullptr;
    XMFLOAT3 m_pos = { 0, 0, 0 };
    XMFLOAT3 m_right = { 1, 0, 0 }, m_up = { 0, 1, 0 };   // 絵の右と上（カメラに向けて、rot の向きに回したもの）
    EffectLayer m_layer = EffectLayer::Overlay;
    XMFLOAT4 m_color = { 1, 1, 1, 1 };   // データの色×呼んだ側の色
    float m_scale = 1.0f;
    float m_elapsed = 0.0f;
};
