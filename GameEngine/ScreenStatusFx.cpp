#include "ScreenStatusFx.h"
#include "SpriteRenderer.h"
#include "Renderer3D.h"
#include "TextureManager.h"
#include "EffectDataBase.h"   // 毒のもやの材質（FxMaterial）
#include "UiContext.h"        // 上の帯の高さ（しずくが出てくる所）
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace DirectX;

namespace
{
    const float LOW_HP_RATIO = 0.3f;     // HPがこの割合以下で赤くする
    const float FADE_TIME = 0.5f;        // 出る・消えるのにかかる秒数
    const float BEAT_PERIOD = 1.1f;      // 鼓動1回の秒数（ドクン・ドクンの2回で1組）
    const float BASE_ALPHA = 0.32f;      // ふだんの濃さ（画面の端で）
    const float BEAT_ALPHA = 0.45f;      // 鼓動で足す濃さ（入った直後だけなので大きめ）
    const int   BEAT_SETS = 3;           // 入った直後に鼓動する回数（ドクン・ドクンで1回）。その後は明滅しない（考える邪魔にならないように）
    const XMFLOAT3 COLOR = { 0.75f, 0.02f, 0.04f };

    // 毒：端のもや（effects.json の材質 fx_poison_edge。形・ノイズ・色の帯）
    const char* POISON_MATERIAL = "fx_poison_edge";
    const float MIST_LIFE = 0.3f;        // シェーダーに渡す「進み具合」（ずっとこの値。大きいほど冷えた暗い色）
    const float MIST_ALPHA = 0.55f;      // もやの濃さ
    // 毒：しずく（fx_drop_poison の画像。色は画像のまま）
    const float DRIP_W = 18.0f;          // しずくの幅（ピクセル）
    const float DRIP_GROW_MIN = 0.4f, DRIP_GROW_MAX = 0.55f;   // 上の帯の下でふくらむ秒数
    const float DRIP_GAP_MIN = 0.45f, DRIP_GAP_MAX = 0.9f;     // 次のしずくまでの秒数
    const float DRIP_GRAVITY = 3000.0f;  // 落ちる加速（1秒ごとに、落ちる速さがこのピクセル/秒ずつ増える）
    const float DRIP_STRETCH = 0.025f;   // 速さの向きに伸ばす長さ＝速さ×これ（直近この秒数に進む距離）
    const float DRIP_STRETCH_MAX = 52.0f;
    const float DRIP_FADE_START = 0.4f;  // 落ち始めてこの秒数で薄くなり始め（画面の約4割の高さ）
    const float DRIP_FADE_END = 0.56f;   // この秒数で消える（画面の約7割の高さ）

    float Rand(float lo, float hi) { return lo + (hi - lo) * (float)rand() / RAND_MAX; }
    float Approach(float v, float target, float step) { return v + (std::max)(-step, (std::min)(step, target - v)); }   // 目標へ一定の速さで近づける

    // 鼓動の強さ（0～約1）。1周期の中で、ドクン（強）・ドクン（弱）と2回ふくらむ
    float Heartbeat(float t)
    {
        float p = fmodf(t, BEAT_PERIOD);
        auto beat = [p](float center) { float d = (p - center) / 0.06f; return expf(-d * d); };
        return beat(0.10f) + 0.6f * beat(0.32f);
    }
}

void ScreenStatusFx::Update(float dt, const ScreenStatus& s)
{
    // HPが低いとき
    bool low = (s.maxHp > 0 && s.hp > 0 && s.hp <= s.maxHp * LOW_HP_RATIO);
    if (low && !m_wasLow) m_beatLeft = BEAT_PERIOD * BEAT_SETS;   // 3割以下に入った瞬間：鼓動を最初から
    if (!low) m_beatLeft = 0.0f;                                   // 回復したら鼓動もやめる
    m_wasLow = low;
    m_beatLeft = (std::max)(0.0f, m_beatLeft - dt);
    m_lowHp = Approach(m_lowHp, low ? 1.0f : 0.0f, dt / FADE_TIME);

    // 毒のとき
    m_time += dt;
    m_poison = Approach(m_poison, s.poisoned ? 1.0f : 0.0f, dt / FADE_TIME);
    UpdateDrips(dt, s.poisoned);
}

void ScreenStatusFx::UpdateDrips(float dt, bool spawn)
{
    for (auto& d : m_drips) d.age += dt;
    m_drips.erase(std::remove_if(m_drips.begin(), m_drips.end(),
        [](const Drip& d) { return d.age >= d.grow + DRIP_FADE_END; }), m_drips.end());   // 消えたしずくを捨てる
    if (!spawn) { m_dripTimer = 0.0f; return; }   // 毒が治ったら新しいしずくは出さない（落ちている途中のものは最後まで）
    m_dripTimer -= dt;
    if (m_dripTimer > 0.0f) return;
    m_dripTimer = Rand(DRIP_GAP_MIN, DRIP_GAP_MAX);
    m_drips.push_back({ Rand(0.06f, 0.94f), 0.0f, Rand(DRIP_GROW_MIN, DRIP_GROW_MAX), Rand(0.8f, 1.1f) });
}

void ScreenStatusFx::Draw(Renderer3D* r, SpriteRenderer* sr, float screenW, float screenH) const
{
    // 毒のもや：エフェクト用シェーダーで画面いっぱいに（端の枠の形×流れるノイズ×紫の色の帯）
    const FxMaterial* mat = EffectDataBase::GetMaterial(POISON_MATERIAL);
    if (m_poison > 0.0f && mat)
    {
        FxParams fx;
        fx.life = MIST_LIFE; fx.erodeStart = mat->erodeStart; fx.erodeEnd = mat->erodeEnd;
        fx.glow = mat->glow; fx.scroll = mat->scroll; fx.noiseScale = mat->noiseScale;
        fx.time = m_time;
        r->Begin();
        r->DrawFxScreen(TextureManager::Get(mat->shape), TextureManager::Get(mat->noise), TextureManager::Get(mat->ramp),
            fx, XMFLOAT4(1.0f, 1.0f, 1.0f, MIST_ALPHA * m_poison));
        r->End();
    }

    sr->Begin();
    DrawDrips(sr, screenW);
    if (m_lowHp > 0.0f || m_beatLeft > 0.0f)
    {
        float beat = (m_beatLeft > 0.0f) ? Heartbeat(BEAT_PERIOD * BEAT_SETS - m_beatLeft) : 0.0f;   // 入ってからの時間で鼓動
        float a = m_lowHp * BASE_ALPHA + BEAT_ALPHA * beat;   // 鼓動は出始めの薄さに関係なく、1回目から大きく
        // fx_vignette：白で、端ほど不透明・真ん中は透明の画像。色はここで掛ける
        sr->DrawSprite(TextureManager::Get("fx_vignette"), 0.0f, 0.0f, screenW, screenH,
            0.0f, XMFLOAT4(COLOR.x, COLOR.y, COLOR.z, a));
    }
    sr->End();
}

// しずく：上の帯の下の端でふくらんでから、重力で落ちる。落ちる間は速さの向き（下）に伸ばし、途中で薄くなって消える
void ScreenStatusFx::DrawDrips(SpriteRenderer* sr, float screenW) const
{
    ID3D11ShaderResourceView* tex = TextureManager::Get("fx_drop_poison");
    const float TOP_Y = UiContext::BAR_H;
    for (const Drip& d : m_drips)
    {
        float w = DRIP_W * d.size, h, y, alpha = 1.0f;
        if (d.age < d.grow)   // ふくらむ
        {
            w *= 0.35f + 0.65f * d.age / d.grow;
            h = w * 1.5f;
            y = TOP_Y;
        }
        else                  // 落ちる
        {
            float fall = d.age - d.grow;
            float speed = DRIP_GRAVITY * fall;
            y = TOP_Y + 0.5f * DRIP_GRAVITY * fall * fall;
            h = w * 1.5f + (std::min)(speed * DRIP_STRETCH, DRIP_STRETCH_MAX);
            alpha = 1.0f - (std::max)(0.0f, (fall - DRIP_FADE_START) / (DRIP_FADE_END - DRIP_FADE_START));
        }
        float a = alpha * (std::min)(1.0f, m_poison * 2.0f);   // 毒が治ったら、残っているしずくも早めに薄く
        if (a <= 0.0f) continue;
        sr->DrawSprite(tex, d.x * screenW - w * 0.5f, y, w, h, 0.0f, XMFLOAT4(1.0f, 1.0f, 1.0f, a));
    }
}
