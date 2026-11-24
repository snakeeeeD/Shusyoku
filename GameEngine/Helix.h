#pragma once
#include <string>
#include <d3d11.h>
#include <nlohmann/json.hpp>
#include "EffectPart.h"

// らせんが回りながら昇る柱（effects.json の "parts" の "type": "helix"）。虚無の柱など。
// 線は区切りごとにカメラへ向いた帯（Renderer3D::DrawBand）。奥の線→真ん中の柱→手前の線、の順に描いて前後を出す
struct HelixDef : EffectPartDef
{
    std::string texture = "fx_trail";  // らせんの線の絵（横がやわらかく消える帯）
    std::string coreTexture;           // 真ん中の柱の絵（空なら無し）。下端を地面に立てる
    int strands = 3;                   // らせんの本数（等間隔にずらす）
    float radius = 0.35f;              // らせんの半径（ワールド）
    float height = 2.6f;               // 高さ
    float turns = 2.0f;                // 下から上までに巻く回数
    float spin = 360.0f;               // 回る速さ（度/秒）
    float width = 0.09f;               // 線の太さ（手前を通る所）
    float backWidth = 0.55f;           // 奥を通る所の太さ（手前に対する割合）
    float backBright = 0.4f;           // 奥を通る所の明るさ（手前に対する割合）
    float outline = 0.05f;             // 縁取りの太さ（線の太さに足す分。0なら無し）
    XMFLOAT4 outlineColor = { 0.1f, 0.0f, 0.16f, 0.9f };
    float coreWidth = 0.35f;           // 真ん中の柱の幅
    float life = 0.6f;                 // 出てから消えるまで
    float growTime = 0.15f;            // 下から上へ伸びる秒数
    float fadeFrom = 0.4f;             // 寿命のどこから細く薄くなり始めるか（0～1）
    int segments = 48;                 // 1本を何区切りで描くか（多いほどなめらか）
    XMFLOAT4 color = { 1, 1, 1, 1 };
    XMFLOAT4 coreColor = { 1, 1, 1, 1 };

    static std::unique_ptr<EffectPartDef> Load(const nlohmann::json& j);   // 種類の表から呼ばれる
    std::unique_ptr<EffectPart> Spawn(const SpawnAt& at) const override;
};

// 出ているらせんの柱1本
class Helix : public EffectPart
{
public:
    Helix(const HelixDef& def, const SpawnAt& at);
    void Update(float dt) override { m_elapsed += dt; }
    void Draw(Renderer3D* r, EffectLayer layer) override;   // 上の順番（キャラの後）で描く
    bool IsDone() const override { return m_elapsed >= m_def.life; }
private:
    void DrawStrands(Renderer3D* r, bool front, float top, float thin, float fade, float rot);   // 奥（front=false）か手前の区切りだけ描く
    const HelixDef& m_def;               // 太さなどはデータを見に行く（データは起動中ずっと残る）
    ID3D11ShaderResourceView* m_tex = nullptr;
    ID3D11ShaderResourceView* m_coreTex = nullptr;
    XMFLOAT3 m_pos = { 0, 0, 0 };        // 根もと（地面）
    XMFLOAT3 m_fwd = { 0, 0, 1 };        // カメラの前（帯の向きと、手前／奥を決める）
    float m_elapsed = 0.0f;
};
