#pragma once

// エンカウント編集ツール（Debugビルド専用の ImGui ウィンドウ）
// - encounters.json を「生のJSON」のまま編集して保存する（エディタが知らない項目も消えない）
// - 敵の一覧・テクスチャ・占有マス(gridShape)は enemies.json から読む（読むだけ・書き換えない）
// - 保存すると EncounterDataBase::Reload() まで行うので、そのままゲームに反映される
// - 開閉・描画は EditorHub から呼ばれる
namespace EncounterEditor
{
    void Toggle();      // ウィンドウの開閉
    bool IsOpen();
    void Draw();        // ImGui のフレーム内で毎フレーム呼ぶ（閉じていれば何もしない）
}
