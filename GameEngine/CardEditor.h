#pragma once

class SpriteRenderer;
class TextRenderer;

// カード編集ツール（Debugビルド専用の ImGui ウィンドウ）
// - cards.json を「生のJSON」のまま編集して保存する（エディタが知らない項目も消えない）
// - 編集中のカードを、ゲームと同じ CardDataBase::ParseCardText → CardVisual で
//   ゲーム画面の上に実物の見た目（強化前／強化後）で表示する
// - 開閉・描画は EditorHub から呼ばれる
namespace CardEditor
{
    void Toggle();
    bool IsOpen();
    void Draw();
    void DrawGameOverlay(SpriteRenderer* sprite, TextRenderer* text);
}
