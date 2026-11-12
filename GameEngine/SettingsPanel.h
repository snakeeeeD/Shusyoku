#pragma once
// 設定の窓（帯のギア・タイトルの左下から開く）。表示モード・音量・画面シェイク・カメラ追従・用語集・タイトルへ・終了。
// 窓の外でやること（用語集を開く・タイトルへ・終了）は HandleInput の戻り値で SceneManager に頼む
#include "URect.h"
#include "UiContext.h"

class SettingsPanel
{
public:
    enum class Action { None, OpenGlossary, ReturnToTitle, Quit };   // 窓の外でやってほしいこと

    void Open(bool canReturnToTitle) { m_open = true; m_canTitle = canReturnToTitle; m_confirm = 0; m_dragSlider = -1; }   // タイトル画面では「タイトルに戻る」を出さない
    void Close();                       // 設定を保存して閉じる
    bool IsOpen() const { return m_open; }
    Action HandleInput(UiContext& ui);
    void   Draw(UiContext& ui);

private:
    // 窓の各部の位置（描画とクリックで共有）
    struct Layout { URect panel, disp, bgmTrack, seTrack, shake, aimCam, glossary, title, gameEnd, close, cfmPanel, cfmYes, cfmNo; };
    Layout MakeLayout(const UiContext& ui) const;

    bool m_open = false;
    bool m_canTitle = true;   // 「タイトルに戻る」を出すか
    int  m_confirm = 0;       // はい/いいえの確認：0=なし 1=終了 2=タイトルへ
    int  m_dragSlider = -1;   // つまんでいる音量：0=BGM 1=SE -1=なし
};
