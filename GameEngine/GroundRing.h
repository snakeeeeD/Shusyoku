#pragma once
#include <string>
#include <d3d11.h>
#include <nlohmann/json.hpp>
#include "EffectPart.h"

// 地面に広がる輪（衝撃波）の種類のデータ（effects.json の "parts" の "type": "ring"）
struct GroundRingDef : EffectPartDef
{
    std::string texture = "fx_ring";   // 輪の画像（textures.json のキー）
    float life = 0.35f;                // 広がって消えるまでの秒数
    float sizeStart = 0.3f;            // 最初の直径（ワールド。1マス＝1.1）
    float sizeEnd = 2.4f;              // 最後の直径
    float ease = 3.0f;                 // 広がり方の勢い（大きいほど最初に一気に広がり、後はゆっくり）
    XMFLOAT4 color = { 1, 1, 1, 1 };
    float delay = 0.0f;                // この輪だけの遅延（輪を重ねるとき用）
    float y = 0.02f;                   // 地面からの高さ
    bool additive = true;              // 加算合成で光らせる（土ぼこりのような輪は false）

    static std::unique_ptr<EffectPartDef> Load(const nlohmann::json& j);   // 種類の表から呼ばれる
    std::unique_ptr<EffectPart> Spawn(const SpawnAt& at) const override;
};

// 出ている輪1つ。地面に寝かせた板を、時間とともに大きく・薄くする
class GroundRing : public EffectPart
{
public:
    GroundRing(const GroundRingDef& def, const SpawnAt& at);
    void Update(float dt) override { m_elapsed += dt; }
    void Draw(Renderer3D* r, EffectLayer layer) override;   // 地面の順番でだけ描く（キャラの下に見せる）
    bool IsDone() const override { return m_elapsed >= m_def.life; }
private:
    const GroundRingDef& m_def;          // 大きさ・寿命などはデータを見に行く（データは起動中ずっと残る）
    ID3D11ShaderResourceView* m_tex = nullptr;
    float m_x = 0.0f, m_z = 0.0f;
    XMFLOAT4 m_color = { 1, 1, 1, 1 };   // データの色×呼んだ側の色
    float m_scale = 1.0f;                // 呼んだ側の大きさの倍率（直径に掛ける）
    float m_elapsed = 0.0f;              // マイナスの間は待ち（遅延）
};
