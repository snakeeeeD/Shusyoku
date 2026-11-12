#pragma once
// 休憩の窓：回復／カードを強化（デッキの強化モードへ）。
// 「休憩中」は強化の画面に行っている間も続く（Active）。休憩を終える時のまとめ（デッキ・合成を閉じて保存）は SceneManager
#include "URect.h"
#include "UiContext.h"

class RestPanel
{
public:
    enum class Action { None, Finish, Upgrade };   // Finish＝休憩を終える（回復した・戻る）、Upgrade＝デッキの強化へ

    void Open() { m_shown = true; m_active = true; m_armed = false; }   // フィールドの休憩マスから
    void Show() { m_shown = true; m_armed = false; }                    // 強化の画面から戻る
    void Hide() { m_shown = false; }                                     // 強化の画面へ行く（休憩は続いている）
    void End()  { m_shown = false; m_active = false; }                  // 休憩を終える
    bool IsShown() const { return m_shown; }   // 2択の画面が出ている
    bool Active() const { return m_active; }   // 休憩中（強化の画面に行っている間も）

    Action HandleInput(UiContext& ui, bool backHit);   // 押し始めが窓の中のクリックだけ受け付ける
    void   Draw(UiContext& ui, bool showTips);         // showTips＝ホバーで説明を出す（チュートリアル中は出さない）

private:
    static constexpr int REST_HEAL = 20;
    static int HealAmount();            // 実際に回復する量（20＋レリック）
    URect BtnRect(int i) const;         // 0=回復 1=強化（描画とクリックで共有）
    int  m_sw = 1280, m_sh = 720;
    bool m_shown = false, m_active = false;
    bool m_armed = false;               // 押下が休憩画面の中で始まった（その「離す」だけ有効）
};
