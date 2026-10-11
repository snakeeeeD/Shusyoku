#pragma once

class SpriteRenderer;
class TextRenderer;

// 合成（土台・素材）編集ツール（Debugビルド専用の ImGui ウィンドウ）
// - materials.json の bases（土台）と materials（素材）を「生のJSON」のまま編集・追加・削除する
// - 「この土台＋この素材」でできる合成カードを、ゲームと同じ計算
//   （MaterialDataBase::ParseXxxText → CardDataBase::BuildCraftedFrom）で作り、ゲーム画面に実物で表示する
// - 開閉・描画は EditorHub から呼ばれる
namespace CraftEditor
{
    void Toggle();
    bool IsOpen();
    void Draw();
    void DrawGameOverlay(SpriteRenderer* sprite, TextRenderer* text);
}
