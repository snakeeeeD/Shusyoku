#pragma once

// イベント編集ツール（Debugビルド専用の ImGui ウィンドウ）
// - events.json を「生のJSON」のまま編集して保存する（エディタが知らない項目も消えない）
// - 結果(outcomes)の種類ごとに、必要な値(value)と引数(param)だけを出す
// - プレビューは「本物のイベント画面をゲーム内で開く」（描き方を二重に持たない）
// - 開閉・描画は EditorHub から呼ばれる
namespace EventEditor
{
    void Toggle();
    bool IsOpen();
    void Draw();
}
