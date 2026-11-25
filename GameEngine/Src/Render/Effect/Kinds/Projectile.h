#pragma once
#include <string>
#include <d3d11.h>
#include <nlohmann/json.hpp>
#include "EffectPart.h"

// 飛び道具（effects.json の "parts" の "type": "projectile"）。EffectManager::PlayFrom で出す。
// 飛び始めから着く所まで、決まった秒数で少し弧を描いて飛ぶ一枚絵と、その後ろに伸びる軌跡。
// 当たるまでの秒数は一定なので、遠いほど速い。軌跡の長さと弧の高さは「距離の割合」（距離が変わっても同じ形）
struct ProjectileDef : EffectPartDef
{
    std::string texture;               // 飛ぶ絵（上向きに描く。faceTravel なら先が進む向きを向く）
    float life = 0.3f;                 // 飛ぶ秒数＝当たるまで（敵の windup と同じ数字にする）
    float size = 0.3f;                 // 絵の幅（ワールド）
    float aspect = 1.0f;               // 高さ÷幅
    XMFLOAT4 color = { 1, 1, 1, 1 };
    bool faceTravel = false;           // 絵の上を、進む向きに向ける（矢）
    float spin = 0.0f;                 // 回す速さ（度/秒。岩など。faceTravel でない時）
    float arc = 0.1f;                  // 弧の高さ（距離の割合）
    std::string trailTexture = "fx_trail";   // 軌跡の帯の絵（横はやわらかく消え、縦は一様）
    float trailWidth = 0.05f;          // 軌跡の太さ（ワールド。頭の所。後ろほど細い）。0なら軌跡なし
    float trailLen = 0.4f;             // 軌跡の長さ（距離の割合）
    XMFLOAT4 trailColor = { 1, 1, 1, 0.8f };

    static std::unique_ptr<EffectPartDef> Load(const nlohmann::json& j);   // 種類の表から呼ばれる
    std::unique_ptr<EffectPart> Spawn(const SpawnAt& at) const override;
};

// 飛んでいる飛び道具1つ
class Projectile : public EffectPart
{
public:
    Projectile(const ProjectileDef& def, const SpawnAt& at);
    void Update(float dt) override { m_elapsed += dt; }
    void Draw(Renderer3D* r, EffectLayer layer) override;   // 上の順番（キャラの後）で描く
    bool IsDone() const override { return m_elapsed >= m_def.life; }
private:
    XMFLOAT3 PosAt(float u) const;       // 飛んだ割合 u（0から1）の位置：まっすぐ進む＋上へ弧
    void DrawTrail(Renderer3D* r, float u);
    void DrawHead(Renderer3D* r, float u);
    const ProjectileDef& m_def;          // 大きさなどはデータを見に行く（データは起動中ずっと残る）
    ID3D11ShaderResourceView* m_tex = nullptr;
    ID3D11ShaderResourceView* m_trailTex = nullptr;
    XMFLOAT3 m_from = { 0, 0, 0 }, m_to = { 0, 0, 0 };
    float m_arcH = 0.0f;                 // 弧の高さ（ワールド）＝距離×arc
    XMFLOAT3 m_right = { 1, 0, 0 }, m_up = { 0, 1, 0 }, m_fwd = { 0, 0, 1 };   // カメラの右・上・前
    XMFLOAT4 m_color = { 1, 1, 1, 1 }, m_trailColor = { 1, 1, 1, 1 };       // データの色×呼んだ側の色
    float m_scale = 1.0f;
    float m_elapsed = 0.0f;
};
