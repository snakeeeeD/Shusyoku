#pragma once
// イベントの窓：選択肢 → 結果（受け取った物の一覧）／カード選択・レリック選択（削除・強化・変化・入れ替え）。
// 窓の外でやること（戦闘・ショップ・カード報酬へ進む、呪いが手に入る瞬間を見せる）は HandleInput の戻り値で SceneManager に頼む
#include <Windows.h>
#include <string>
#include <vector>
#include "URect.h"
#include "UiContext.h"
#include "ScrollList.h"
#include "EventDataBase.h"

class EventPanel
{
public:
    // 窓の外でやってほしいこと
    struct Request
    {
        enum class Go { None, Battle, Shop, CardSelect } go = Go::None;   // 結果画面のクリックで進む先
        std::string param;                     // 戦闘の種類（"event" など）／カード報酬のモード
        std::vector<std::string> showCards;    // 手に入る瞬間を見せるカード（呪い）
    };

    void Open(const std::string& id);
    bool IsOpen() const { return m_open; }
    bool Animating() const { return m_animIdx >= 0; }   // カード・レリックが変わる演出中（説明を残さない）
    Request HandleInput(UiContext& ui);
    void    Update(UiContext& ui, float dt);   // 揺れ・表示HP・変わる演出・一覧のスクロール
    void    Draw(UiContext& ui);               // 選択肢と結果／カード選択／レリック選択 を出し分け

private:
    enum class Pick { None, RemoveCard, UpgradeCard, TransformCard, TransformRelic };   // 選んだ後に何を選ばせるか
    static Pick PickOf(const EventChoice& c);                     // 選択肢の結果から（events.json の type）
    static bool Pickable(Pick p, const std::string& cardId);      // このカードを選べるか（強化できる／外せる）

    void HandleInputBody(UiContext& ui, Request& rq);
    void ApplyOutcomes(const EventChoice& c);   // 選んだ結果を反映（HP・ゴールド・素材・レリック・カード・進む先）
    bool ChoiceEnabled(const EventChoice& c) const;
    void DrawChoices(UiContext& ui);            // 選択肢と結果
    void DrawCardPicker(UiContext& ui);         // カード選択（削除・強化・変化）
    void DrawRelicPicker(UiContext& ui);        // レリック選択（入れ替え）

    // 位置（描画とクリックで共有）
    void  ChoiceRect(int i, float& x, float& y, float& w, float& h) const;
    URect PanelRect(int choiceCount, int gainCount = 0) const   // 選択肢の数、または受け取った物の数で高さが決まる
    {
        float w = 640.0f;
        float hc = 200.0f + choiceCount * 72.0f, hg = 264.0f + gainCount * 52.0f;
        return { (m_sw - w) / 2.0f, 96.0f, w, hc > hg ? hc : hg };
    }
    URect GainRect(int i, const URect& panel) const
    {
        const float w = 380.0f;
        return { panel.x + (panel.w - w) / 2.0f, panel.y + 184.0f + i * 52.0f, w, 44.0f };
    }
    void  CardSlot(int i, float& x, float& y) const;
    URect CardPickerPanel() const;               // カード選択の一覧を囲む窓（デッキ枚数で高さが決まる）
    int   CardAt(POINT p) const;
    URect RelicRect(int i) const
    {
        const float w = 150.0f, h = 124.0f, gap = 14.0f; const int cols = 6;
        float startX = (m_sw - (cols * w + (cols - 1) * gap)) / 2.0f;
        return { startX + (i % cols) * (w + gap), 170.0f + (i / cols) * (h + gap), w, h };
    }
    URect RelicPanel() const;
    int   RelicAt(POINT p) const;

    int  m_sw = 1280, m_sh = 720;   // 位置の計算に使う画面の大きさ（HandleInput/Update/Draw の最初で受け取る）

    bool m_open = false;
    std::string m_id;
    int  m_result = -1;             // -1=選択中, >=0=結果表示（選んだ選択肢）

    // 実際に増減したもの（結果画面に一覧表示。確率つきの結果も起きたものだけ）
    struct Gain { std::string kind; std::string id; int value = 0; };   // kind: gold/hp/maxHp/material/relic/card
    std::vector<Gain> m_gains;
    float m_hpDisplay = -1.0f;      // HPが減った時の表示HP（バトルと同じく徐々に減らす。-1=なし）

    Pick m_pick = Pick::None;       // 選ばせている物（None=選択肢・結果の画面）
    int  m_pickChoice = -1;         // 選ばせる前に選んだ選択肢（選び終わったら結果に出す）
    ScrollList m_pickScroll;        // カード選択の一覧（デッキが多い時にスクロール）
    int   m_animIdx = -1;           // 変わる演出中のカード／レリック（-1=なし）
    float m_animTimer = 0.0f;
    std::string m_animTo;           // 変化・強化・入れ替え先（空=削除の塵化）
    static constexpr float ANIM_DUR = 2.0f;

    enum class Pending { None, Battle, Shop, Treasure, CardSelect };   // 結果画面のクリックで進む先
    Pending m_pending = Pending::None;
    std::string m_battleParam;      // 戦闘の種類
    std::string m_selectMode;       // カード報酬のモード
    std::vector<std::string> m_showCards;   // 手に入る瞬間を見せるカード（HandleInput の戻り値で渡す）
    std::string m_lastHoverKey;     // ホバー音を重ねないためのキー
};
