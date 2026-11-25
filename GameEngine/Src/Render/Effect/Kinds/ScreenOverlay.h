#pragma once
#include <string>
#include <d3d11.h>
#include <nlohmann/json.hpp>
#include "EffectPart.h"

struct FxMaterial;

// 画面いっぱいに出す演出の種類のデータ（effects.json の "parts" の "type": "screen"）。
// エフェクト用シェーダー（形・ノイズ・色の帯の3枚）で、画面全体に1枚の板を描く。出す場所（Play の x, y, z）は使わない
struct ScreenOverlayDef : EffectPartDef
{
    std::string material;              // 使う材質（effects.json の "materials" のキー）
    float life = 1.8f;                 // 出てから消えるまでの秒数
    float fadeIn = 0.12f;              // 出だしにふわっと濃くなる秒数
    XMFLOAT4 color = { 1, 1, 1, 1 };   // 全体に掛ける色（w＝全体の濃さ）

    static std::unique_ptr<EffectPartDef> Load(const nlohmann::json& j);   // 種類の表から呼ばれる
    std::unique_ptr<EffectPart> Spawn(const SpawnAt& at) const override;
};

// 出ている画面の演出1つ
class ScreenOverlay : public EffectPart
{
public:
    ScreenOverlay(const ScreenOverlayDef& def, const SpawnAt& at);
    void Update(float dt) override { m_elapsed += dt; }
    void Draw(Renderer3D* r, EffectLayer layer) override;   // 画面の順番でだけ描く（3Dの上・UIの下）
    bool IsDone() const override { return m_elapsed >= m_def.life; }
private:
    const ScreenOverlayDef& m_def;       // 寿命などはデータを見に行く（データは起動中ずっと残る）
    const FxMaterial* m_mat = nullptr;   // 材質（無ければ何も描かない）
    ID3D11ShaderResourceView* m_shape = nullptr;
    ID3D11ShaderResourceView* m_noise = nullptr;
    ID3D11ShaderResourceView* m_ramp = nullptr;
    XMFLOAT4 m_color = { 1, 1, 1, 1 };   // データの色×呼んだ側の色
    float m_seed = 0.0f;                 // 出すたびに模様を変える
    float m_elapsed = 0.0f;
};
