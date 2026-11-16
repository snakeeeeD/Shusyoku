#pragma once
#include <memory>
#include <DirectXMath.h>

using namespace DirectX;
class Renderer3D;

// どの順番で描くか（地面に置くものはキャラより先に描いて、キャラの下に見せる）
enum class EffectLayer
{
    Ground,    // 地面（罠の後・キャラの前）。衝撃波の輪、壁の奥半分など
    Overlay,   // いちばん上（キャラの後）。粒・コマ送り、壁の手前半分など
    Screen,    // 画面（3Dを全部描いた後・UIの前）。画面いっぱいの演出（瘴気など）
};

// 演出を出すときの情報（EffectManager::Play の引数と、そのときのカメラの向き）
struct SpawnAt
{
    float x = 0, y = 0, z = 0;          // 出す場所（ワールド）
    float rot = 0.0f;                    // 向き（ラジアン。コマ送りの絵の回転に使う）
    XMFLOAT4 tint = { 1, 1, 1, 1 };      // 呼んだ側が掛ける色
    float scale = 1.0f;                  // 呼んだ側が掛ける大きさの倍率（範囲に合わせる時など。使うかどうかは種類が決める）
    bool hasCam = false;                 // 下のカメラの向きが入っているか（最初の1フレームは無い）
    XMFLOAT3 camRight = { 1, 0, 0 }, camUp = { 0, 1, 0 }, camFwd = { 0, 0, 1 }, camPos = { 0, 0, 0 };
};

// 出ている演出1つ（粒の群れ1つ、コマ送りの絵1つ…）。
// EffectManager はこの形だけを知っている。どう動いてどう描くかは、種類ごとのクラスが書く
class EffectPart
{
public:
    virtual ~EffectPart() = default;
    virtual void Update(float dt) = 0;          // 1フレーム進める
    virtual void Draw(Renderer3D* r, EffectLayer layer) = 0;   // 描く。毎フレーム、地面の順番と上の順番の2回呼ばれるので、その順番で描く分だけ描く
    virtual bool IsDone() const = 0;            // 終わったか（true なら EffectManager が消す）
    virtual void Stop(float fadeSec) {}         // 途中で止める（止め方がある種類だけ書く）
    int handle = 0;                             // Play が返した番号（Stop で探す用）
};

// 演出1つぶんのデータ（起動時に effects.json から1回だけ読む）。Spawn で「出ている演出」を作る
class EffectPartDef
{
public:
    virtual ~EffectPartDef() = default;
    virtual std::unique_ptr<EffectPart> Spawn(const SpawnAt& at) const = 0;
};
