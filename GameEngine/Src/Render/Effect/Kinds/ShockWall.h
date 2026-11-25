#pragma once
#include <string>
#include <d3d11.h>
#include <nlohmann/json.hpp>
#include "EffectPart.h"

// 輪から立ち上がる衝撃波の壁の種類のデータ（effects.json の "parts" の "type": "wall"）。
// 上の縁がとげとげの円柱を、外へ傾けながら広げ、最後は割れて縁から破片を散らす
struct ShockWallDef : EffectPartDef
{
    std::string texture = "fx_wall";   // 壁の画像（横＝一周、縦＝上→下。上の縁がとげとげ）
    float life = 0.35f;                // 立ち上がって消えるまでの秒数
    float radiusStart = 0.14f;         // 根もとの半径（最初）
    float radiusEnd = 1.1f;            // 根もとの半径（最後）
    float height = 0.6f;               // いちばん高いときの高さ
    float slant = 0.7f;                // 外への傾き（高さ1につき、外へどれだけ広がるか）
    float ease = 3.0f;                 // 広がり方の勢い（輪と同じにすると、輪にそろって広がる）
    float spin = 0.6f;                 // 回る速さ（ラジアン/秒。とげが止まって見えないように）
    int segments = 32;                 // 円柱を何枚の板で作るか
    XMFLOAT4 color = { 1, 1, 1, 1 };
    bool additive = true;              // 加算合成で光らせる
    float breakAt = 0.55f;             // 寿命のどこで割れるか（0～1）。割れた後は急に薄くなって消える
    std::string breakFx;               // 割れたときに縁から出す演出（effects.json の id。空なら出さない）
    int breakCount = 12;               // その演出を縁の何か所から出すか
    float breakHeight = 0.8f;          // 縁のどの高さから出すか（壁の高さに対する割合）

    static std::unique_ptr<EffectPartDef> Load(const nlohmann::json& j);   // 種類の表から呼ばれる
    std::unique_ptr<EffectPart> Spawn(const SpawnAt& at) const override;
};

// 出ている壁1つ
class ShockWall : public EffectPart
{
public:
    ShockWall(const ShockWallDef& def, const SpawnAt& at);
    void Update(float dt) override;
    void Draw(Renderer3D* r, EffectLayer layer) override;   // 奥半分は地面の順番（キャラの後ろ）、手前半分は上の順番（キャラの前）
    bool IsDone() const override { return m_elapsed >= m_def.life; }
private:
    void Shape(float t, float& radius, float& height) const;   // 寿命の進み具合 t（0→1）での根もとの半径と高さ
    void Break();                                              // 割れる：縁の breakCount か所から breakFx を外向きに出す
    const ShockWallDef& m_def;           // 大きさ・寿命などはデータを見に行く（データは起動中ずっと残る）
    ID3D11ShaderResourceView* m_tex = nullptr;
    float m_x = 0.0f, m_z = 0.0f;
    XMFLOAT4 m_color = { 1, 1, 1, 1 };   // データの色×呼んだ側の色
    float m_scale = 1.0f;                // 呼んだ側の大きさの倍率（半径と破片の数に掛ける。高さはそのまま）
    float m_elapsed = 0.0f;
    bool m_broken = false;
};
