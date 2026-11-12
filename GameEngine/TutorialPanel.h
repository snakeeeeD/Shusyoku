#pragma once
// ページ型のチュートリアル：暗幕＋スポットライト＋説明、クリックでページ送り。遅れて出すこともできる（勝利画面を少し見せてから など）。
// 照らす場所のうち戦闘の物（手札のカード・当たりマーカー・敵のパネル）の位置と、表示中に戦闘を動かすのは SceneManager（戦闘を知っている側）
#include <string>
#include <vector>
#include "URect.h"
#include "UiContext.h"

class TutorialPanel
{
public:
    struct Page
    {
        std::wstring text;
        float hx = 0, hy = 0, hw = 0, hh = 0;   // 照らす場所（hw=0 なら全体を暗く）
        // 教えるための仕組み（今のページでは使っていないが、ページを足す時に使える）
        int  hoverCard = -1;        // 手札のこのカードを浮かせて照らす
        bool stepsTip = false;      // 歩数の注記を出す
        bool showHitmark = false;   // 当たりマーカーを強制表示して照らす
        bool enemyPanel = false;    // 敵のパネルを照らす
    };

    void Show(const std::vector<Page>& pages);
    void ShowDelayed(const std::vector<Page>& pages, float delay);
    bool IsOpen() const { return m_open; }
    bool Active() const { return m_open || !m_pending.empty(); }   // 表示中か表示待ち（その間はヒントを出さない）
    bool Waiting() const { return m_delay > 0.0f; }                 // 表示待ち（入力を止める：勝利の連打で飛ばないように）
    const Page& Current() const { return m_pages[m_page]; }

    bool HandleInput(UiContext& ui);              // クリックで次へ。最後のページを閉じたら true
    void Update(float dt);                        // 待ち時間 → 表示
    void Draw(UiContext& ui, const URect& spot);  // spot＝今照らす場所

private:
    std::vector<Page> m_pages;
    int  m_page = 0;
    bool m_open = false;
    std::vector<Page> m_pending;   // 遅れて出すページ
    float m_delay = 0.0f;
};
