#pragma once

// 敵の行動編集ツール（Debugビルド専用の ImGui ウィンドウ）
// - enemies.json を「生のJSON」のまま編集して保存する（エディタが知らない項目も消えない）
// - 選んだ行動の攻撃範囲を、ゲームと同じ RangeShape::Contains で盤面に表示する
// - 盤面の敵とプレイヤーはドラッグで動かせる（どこに立つと当たるかを確かめる）
// - 開閉・描画は EditorHub から呼ばれる
namespace EnemyEditor
{
    void Toggle();
    bool IsOpen();
    void Draw();
}
