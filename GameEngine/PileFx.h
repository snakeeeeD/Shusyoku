#pragma once
#include <vector>
#include <cmath>
#include <DirectXMath.h>
using namespace DirectX;
class SpriteRenderer;

// 山札・捨て札・廃棄札の「置き場所」と「カードの流れの見せ方」。
//  置き場所：描く所（BattleUI）・押す所（BattleSceneInput）・カードが飛ぶ先で共用する（動かす時はここだけ）
//  見せ方  ：カードが飛ぶ道にそった光の帯と、着いた山のアイコンの弾み・光の輪（盤面を隠さないよう細く短く）
namespace PileFx
{
    enum class Pile { Draw, Discard, Exhaust, None };
    constexpr float PILE_W = 50.0f, PILE_H = 40.0f;

    // 山のボタンの左上。山札は左下、捨て札と廃棄札は右下（スレスパと同じ）
    inline XMFLOAT2 PilePos(Pile p, float sw, float sh)
    {
        switch (p)
        {
        case Pile::Draw:    return { 20.0f, sh - 60.0f };
        case Pile::Discard: return { sw - 70.0f, sh - 60.0f };
        default:            return { sw - 130.0f, sh - 60.0f };   // 廃棄札（捨て札の左）
        }
    }
    inline XMFLOAT2 PileCenter(Pile p, float sw, float sh)
    {
        XMFLOAT2 q = PilePos(p, sw, sh);
        return { q.x + PILE_W * 0.5f, q.y + PILE_H * 0.5f };
    }
    inline bool OnPile(Pile p, float sw, float sh, float mx, float my)
    {
        XMFLOAT2 q = PilePos(p, sw, sh);
        return mx >= q.x && mx <= q.x + PILE_W && my >= q.y && my <= q.y + PILE_H;
    }
    // ターン終了ボタン（x, y, w, h）。右下の捨て札の上
    inline XMFLOAT4 EndTurnRect(float sw, float sh) { return { sw - 200.0f, sh - 176.0f, 180.0f, 56.0f }; }   // 捨て札の説明ウィンドウ（高さ614〜654）に重ならない高さ

    // 山へ飛ぶ道：from→to を進み具合 u（0から1）で。easeOut＝最初に速く（ドロー）、でなければだんだん速く（捨て札へ）。arc＝上へふくらむ高さ。
    // カードの絵も光の帯も、この同じ道を通る（帯の先がカードの位置になる）
    inline XMFLOAT2 PathAt(XMFLOAT2 from, XMFLOAT2 to, float arc, float u, bool easeOut = false)
    {
        float e = easeOut ? 1.0f - (1.0f - u) * (1.0f - u) : u * u;
        return { from.x + (to.x - from.x) * e, from.y + (to.y - from.y) * e - sinf(u * 3.14159265f) * arc };
    }

    // 光の帯を1本。delay 秒待ってから dur 秒で from→to（PathAt の道）を飛ぶ。着いたら toPile の山を弾ませる
    void AddStreak(XMFLOAT2 from, XMFLOAT2 to, float arc, float dur, float delay, XMFLOAT4 color, Pile toPile,
        bool easeOut = false, float width = 5.0f);
    // シャッフル：捨て札から山札へ、count 本（最大12本）の帯を、手札の上を大きく弧を描いて順に飛ばす
    void Shuffle(int count, float sw, float sh);
    void Bounce(Pile p);          // その山のアイコンを弾ませ、光の輪を出す
    float IconScale(Pile p);      // アイコンの大きさの倍率（弾み）
    void Update(float dt);
    void Draw(SpriteRenderer* sr, float sw, float sh);   // 帯と光の輪（スプライトの描画の中で呼ぶ）
    void Clear();

    constexpr float SHUFFLE_DUR = 0.75f;   // シャッフルの全部の帯が着くまで（この間はドローを待たせる）
}
