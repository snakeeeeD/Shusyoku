#pragma once
// デッキの窓：一覧（見るだけ／強化後の姿の切り替え／クリックで拡大）と、休憩の強化（強化できるカードだけ・「強化する」）。
// 窓の外のこと（「戻る」ボタン・ドラッグの判定・休憩へ戻す・強化したカードの出現演出）は SceneManager が持つ
#include <Windows.h>
#include <string>
#include <vector>
#include "URect.h"
#include "UiContext.h"

class DeckPanel
{
public:
    enum class Mode { View, Upgrade };   // 帯の「デッキ」＝見るだけ／休憩の「強化」
    void Open(Mode mode);
    void Close();
    bool IsOpen() const { return m_open; }

    // クリック・ホイール。戻る（backHit）で閉じたら true（休憩から来ていたら休憩へ戻すのは外）
    bool HandleInput(UiContext& ui, POINT m, bool clickRel, bool backHit);
    std::string TakeUpgraded();                  // 強化したカード（出現演出用。受け取ったら空に）
    void ScrollBy(float dy) { m_scrollTarget += dy; }   // ドラッグ（判定は持ち物と共通なので外）
    void Update(UiContext& ui, float dt);        // スクロールの補間
    void DrawGrid(UiContext& ui);                // 暗幕・一覧・「強化後表示」（帯より先に、帯と同じまとまりの中で）
    void DrawTop(UiContext& ui, bool showTooltip);   // 拡大表示・ホバーの説明（一番手前。強化の演出中は説明を出さない）

private:
    bool IsUpgrade() const { return m_mode == Mode::Upgrade; }
    static constexpr float DECK_SCALE = 1.1f;

    // 位置（描画とクリックで共有）
    bool GetDeckCardBase(int i, float& baseX, float& baseY) const;   // カードの基準位置
    float DeckMaxScroll() const;
    int  GetDeckCardAt(POINT p) const;           // 座標→デッキの番号
    std::vector<int> VisibleDeckIndices() const; // 並べるカード（強化モードでは強化できるものだけ）
    URect UpgradeBtnRect() const;                // 「強化する」（描画とクリックで同じ矩形）
    int  DeckHoverIndex(UiContext& ui);          // ホバー中のカード（説明の上では前のカードのまま）
    int  m_sw = 1280, m_sh = 720;                // 位置の計算に使う画面の大きさ（各関数の最初で受け取る）

    void DrawCards(UiContext& ui, bool textPass);
    void DrawPreview(UiContext& ui);

    bool m_open = false;
    Mode m_mode = Mode::View;
    float m_scroll = 0.0f, m_scrollTarget = 0.0f;
    int  m_previewIdx = -1;             // クリックで拡大中のカード（-1=なし）
    bool m_showUpgrade = false;         // 一覧で強化後の姿を見せるか
    bool m_previewShowUpgrade = false;  // 拡大表示で+版を見せるか
    int  m_tipIdx = -1;                 // 説明を出しているカード
    int  m_hoveredPrev = -1;            // ホバー音を1回だけ鳴らす用
    bool m_upgradeBtnHov = false;
    std::string m_upgraded;             // 強化したカード（TakeUpgraded で渡す）
};
