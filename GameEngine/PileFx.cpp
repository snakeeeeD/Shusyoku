#include "PileFx.h"
#include "SpriteRenderer.h"
#include "TextureManager.h"
#include <algorithm>

namespace
{
    struct Streak
    {
        XMFLOAT2 from, to; float arc, dur, delay, width; XMFLOAT4 color;
        PileFx::Pile toPile; bool easeOut;
        float t = 0.0f; bool arrived = false;
    };
    std::vector<Streak> s_streaks;
    float s_bounce[3] = { -1.0f, -1.0f, -1.0f };   // 弾み始めてからの秒数（マイナス＝弾んでいない）

    const float BOUNCE_DUR = 0.25f;   // アイコンが弾む秒数
    const float BOUNCE_AMP = 0.25f;   // 弾む大きさ（1.25倍まで）
    const float RING_DUR = 0.3f;      // 光の輪が広がって消える秒数
    const float TAIL = 0.35f;         // 帯の長さ（飛ぶ道の何割ぶん後ろまで）
    const int   SEG = 12;             // 帯の分け数（多いほど弧がなめらか）
    const int   SHUFFLE_MAX = 12;     // シャッフルの帯の最大本数
    const float SHUFFLE_FLY = 0.4f;   // シャッフルの帯1本が飛ぶ秒数
    const float SHUFFLE_ARC = 170.0f; // 手札の上を越える弧の高さ
    const XMFLOAT4 SHUFFLE_COLOR = { 0.65f, 0.82f, 1.0f, 0.9f };

    // a→b の帯を1枚（帯の絵は横がやわらかく消える。長さ方向を a→b に向けて回す）
    void DrawSeg(SpriteRenderer* sr, ID3D11ShaderResourceView* tex, XMFLOAT2 a, XMFLOAT2 b, float w, const XMFLOAT4& c)
    {
        float dx = b.x - a.x, dy = b.y - a.y;
        float len = sqrtf(dx * dx + dy * dy) + 1.0f;   // 1ピクセル重ねて、つなぎ目のすき間を消す
        float cx = (a.x + b.x) * 0.5f, cy = (a.y + b.y) * 0.5f;
        sr->DrawSprite(tex, cx - w * 0.5f, cy - len * 0.5f, w, len, atan2f(-dx, dy), c);
    }
}

void PileFx::AddStreak(XMFLOAT2 from, XMFLOAT2 to, float arc, float dur, float delay, XMFLOAT4 color, Pile toPile,
    bool easeOut, float width)
{
    Streak s; s.from = from; s.to = to; s.arc = arc; s.dur = (std::max)(0.01f, dur); s.delay = delay;
    s.color = color; s.toPile = toPile; s.easeOut = easeOut; s.width = width;
    s_streaks.push_back(s);
}

void PileFx::Shuffle(int count, float sw, float sh)
{
    int n = (std::min)((std::max)(count, 1), SHUFFLE_MAX);
    float spread = SHUFFLE_DUR - SHUFFLE_FLY;   // この間に、全部の帯を飛ばし始める
    for (int i = 0; i < n; i++)
        AddStreak(PileCenter(Pile::Discard, sw, sh), PileCenter(Pile::Draw, sw, sh),
            SHUFFLE_ARC + (i % 3) * 20.0f, SHUFFLE_FLY, spread * i / n, SHUFFLE_COLOR, Pile::Draw);
}

void PileFx::Bounce(Pile p)
{
    if (p != Pile::None) s_bounce[(int)p] = 0.0f;
}

float PileFx::IconScale(Pile p)
{
    float t = (p == Pile::None) ? -1.0f : s_bounce[(int)p];
    if (t < 0.0f || t > BOUNCE_DUR) return 1.0f;
    return 1.0f + BOUNCE_AMP * sinf(t / BOUNCE_DUR * 3.14159265f);
}

void PileFx::Update(float dt)
{
    for (auto& b : s_bounce) if (b >= 0.0f) b += dt;
    for (auto& s : s_streaks)
    {
        if (s.delay > 0.0f) { s.delay -= dt; continue; }
        s.t += dt;
        if (!s.arrived && s.t >= s.dur) { s.arrived = true; Bounce(s.toPile); }   // 着いた：山が弾む
    }
    // 頭が着いた後も、尾が追いつくまで描く
    s_streaks.erase(std::remove_if(s_streaks.begin(), s_streaks.end(),
        [](const Streak& s) { return s.t >= s.dur * (1.0f + TAIL); }), s_streaks.end());
}

void PileFx::Draw(SpriteRenderer* sr, float sw, float sh)
{
    ID3D11ShaderResourceView* trail = TextureManager::Get("fx_trail");
    if (trail)
        for (auto& s : s_streaks)
        {
            if (s.delay > 0.0f) continue;
            float head = (std::min)(1.0f, s.t / s.dur);
            float tail = (std::max)(0.0f, s.t / s.dur - TAIL);
            if (head - tail < 0.001f) continue;
            XMFLOAT2 a = PathAt(s.from, s.to, s.arc, tail, s.easeOut);
            for (int i = 1; i <= SEG; i++)
            {
                float f = (float)i / SEG;   // 0＝尾 1＝頭
                XMFLOAT2 b = PathAt(s.from, s.to, s.arc, tail + (head - tail) * f, s.easeOut);
                float w = s.width * (0.25f + 0.75f * f);   // 頭ほど太く
                XMFLOAT4 glow = s.color; glow.w *= f;      // 色の帯（太く）
                DrawSeg(sr, trail, a, b, w * 3.0f, glow);
                DrawSeg(sr, trail, a, b, w, XMFLOAT4(1, 1, 1, f * f));   // 白い芯（細く）
                a = b;
            }
        }
    ID3D11ShaderResourceView* ring = TextureManager::Get("fx_ring");
    if (ring)
        for (int p = 0; p < 3; p++)   // 着いた山の光の輪
        {
            float t = s_bounce[p];
            if (t < 0.0f || t > RING_DUR) continue;
            float u = t / RING_DUR, r = 28.0f + 26.0f * u;
            XMFLOAT2 c = PileCenter((Pile)p, sw, sh);
            sr->DrawSprite(ring, c.x - r, c.y - r * 0.8f, r * 2.0f, r * 1.6f, 0.0f, XMFLOAT4(1.0f, 0.95f, 0.85f, 0.8f * (1.0f - u)));
        }
}

void PileFx::Clear()
{
    s_streaks.clear();
    for (auto& b : s_bounce) b = -1.0f;
}
