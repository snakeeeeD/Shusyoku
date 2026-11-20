#pragma once
#include <string>
#include <d3d11.h>
#include <nlohmann/json.hpp>
#include "EffectPart.h"

// 敵とプレイヤーをつなぐ物（引き寄せの触手・舌・糸。effects.json の "parts" の "type": "tether"）。EffectManager::PlayFrom で出す。
// 伸びる（extendTime）→ くっついたまま（EffectManager::SetEnds で両端を動かせる）→ Stop で敵の方へ戻る。
// 1区切りの絵を長さに合わせて並べ、うねらせ、根元から先へ太さを変え、先に別の絵を付ける
struct TetherDef : EffectPartDef
{
    std::string texture;               // 1区切りの絵（上下がつながる。上が先の向き）
    std::string tipTexture;            // 先の絵（上が先。空なら付けない）
    float extendTime = 0.15f;          // 伸びる秒数（敵の windup と同じ数字にする）
    float maxHold = 3.0f;              // 戻せと言われなくても、伸びてからこの秒数で戻る（止め忘れ対策）
    float tileLen = 0.3f;              // 1区切りの長さ（ワールド）
    float widthBase = 0.3f;            // 根元（敵の側）の太さ
    float widthTip = 0.2f;             // 先の太さ
    float tipAspect = 2.0f;            // 先の絵の 高さ÷幅
    float wave = 0.0f;                 // うねる幅（ワールド。真ん中ほど大きく、両端は0）
    float waveFreq = 7.0f;             // うねりの細かさ
    float waveSpeed = 18.0f;           // うねりが流れる速さ
    XMFLOAT4 color = { 1, 1, 1, 1 };
    bool additive = false;             // 加算合成で光らせる（糸）

    static std::unique_ptr<EffectPartDef> Load(const nlohmann::json& j);   // 種類の表から呼ばれる
    std::unique_ptr<EffectPart> Spawn(const SpawnAt& at) const override;
};

// 出ている「つなぐ物」1本
class Tether : public EffectPart
{
public:
    Tether(const TetherDef& def, const SpawnAt& at);
    void Update(float dt) override;
    void Draw(Renderer3D* r, EffectLayer layer) override;   // 上の順番（キャラの後）で描く
    bool IsDone() const override;
    void Stop(float fadeSec) override;                                 // fadeSec 秒で敵の方へ縮んで消える
    void SetEnds(const XMFLOAT3& from, const XMFLOAT3& to) override;   // 両端を動かす（引っぱられて動くプレイヤーに付いていく）
private:
    float Reach() const;                 // 根元から先までの伸び具合（0から1）
    void DrawSegment(Renderer3D* r, ID3D11ShaderResourceView* tex, const XMFLOAT3& a, const XMFLOAT3& b, float wa, float wb);   // a→b の帯1枚
    const TetherDef& m_def;              // 太さなどはデータを見に行く（データは起動中ずっと残る）
    ID3D11ShaderResourceView* m_tex = nullptr;
    ID3D11ShaderResourceView* m_tipTex = nullptr;
    XMFLOAT3 m_from = { 0, 0, 0 }, m_to = { 0, 0, 0 };
    XMFLOAT3 m_fwd = { 0, 0, 1 };        // カメラの前（帯の横向きを決める）
    XMFLOAT4 m_color = { 1, 1, 1, 1 };   // データの色×呼んだ側の色
    float m_scale = 1.0f;
    float m_elapsed = 0.0f;
    float m_releaseAt = -1.0f;           // 戻り始めた時刻（マイナス＝まだ）
    float m_retract = 0.15f;             // 戻る秒数
    float m_reachAtRelease = 1.0f;       // 戻り始めた時の伸び具合
};
