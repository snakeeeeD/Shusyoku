#pragma once

// レリック編集ツール（Debugビルド専用の ImGui ウィンドウ）
// - relics.json を「生のJSON」のまま編集・追加・削除する
// - kind（効果の種類）は EnumNames::RelicKinds の表から選ぶ（value の意味も出す）
// - 説明文の数字と value / count が食い違っていれば警告する
// - 「Save & Give」で保存して、そのまま所持品に加えて試せる
// - 開閉・描画は EditorHub から呼ばれる
namespace RelicEditor
{
    void Toggle();
    bool IsOpen();
    void Draw();
}
