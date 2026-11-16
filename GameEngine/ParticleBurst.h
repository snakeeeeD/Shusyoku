#pragma once
#include <string>
#include <vector>
#include <d3d11.h>
#include <nlohmann/json.hpp>
#include "EffectPart.h"

struct FxMaterial;

enum class Spread { Sphere, Screen, Forward };   // 粒の散らし方（球状＋やや上向き / 画面に平行な円状 / 出す向きを中心に）

// 粒をばらまく種類のデータ（effects.json の "bursts" の1つ）
struct BurstDef : EffectPartDef
{
    int count = 10;
    float speed = 1.5f, life = 0.5f, scale = 0.1f;
    float gravity = 4.0f, drag = 0.02f;
    XMFLOAT4 colorStart = { 1,1,1,1 };
    XMFLOAT4 colorEnd = { 1,1,1,0 };
    std::string texture;
    bool center = false;   // true: 発生点を絵の中心にする（既定は下端＝これまで通り）
    std::string material;  // エフェクト用シェーダーの材質（"materials" の名前。空なら texture の絵のまま）
    float stretch = 0.0f;  // 0より大きいと速さの向きに伸ばして描く（stretch 秒で進む距離ぶんの長さ）。速い＝長い線、遅い＝短い欠片
    float dragY = 0.0f;    // 縦の空気抵抗（drag は横だけにかかる。既定0＝今まで通り）
    Spread spread = Spread::Sphere;   // "spread": "screen" で画面に平行な円状（どの向きの線も同じ長さに見える）
    float cone = 30.0f;    // "spread": "forward" の広がり（度）。出す向き（Play の rot）から左右へ、上へ、この角度まで
    float sway = 0.0f;     // 左右にゆらゆらする幅（ワールド。火の粉など。0＝ゆれない）
    float flicker = 0.0f;  // 明るさのちらつき（0～1。0＝ちらつかない）

    static std::unique_ptr<EffectPartDef> Load(const nlohmann::json& j);   // 種類の表から呼ばれる
    std::unique_ptr<EffectPart> Spawn(const SpawnAt& at) const override;
};

// 出ている粒の群れ1つ。粒の1つ1つが、速さ・重力・空気の抵抗で動き、寿命で消える
class ParticleBurst : public EffectPart
{
public:
    ParticleBurst(const BurstDef& def, const SpawnAt& at);
    void Update(float dt) override;
    void Draw(Renderer3D* r, EffectLayer layer) override;
    bool IsDone() const override { return m_particles.empty(); }
private:
    struct Particle
    {
        XMFLOAT3 pos, vel, origin;   // origin＝出た位置（伸ばす粒の尾を、ここより後ろへ伸ばさない）
        float life, lifeMax;
        float seed;                  // 粒ごとの乱数（ノイズの模様をずらす）
    };
    const BurstDef& m_def;           // 色・重力などは全部の粒で同じなので、データを見に行く（データは起動中ずっと残る）
    ID3D11ShaderResourceView* m_tex = nullptr;
    const FxMaterial* m_mat = nullptr;
    ID3D11ShaderResourceView *m_shape = nullptr, *m_noise = nullptr, *m_ramp = nullptr;   // 材質の画像（毎フレーム探さない）
    std::vector<Particle> m_particles;
    float m_time = 0.0f;             // 出てからの時間（エフェクト用シェーダーのノイズを流す）
};
