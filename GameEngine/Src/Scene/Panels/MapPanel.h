#pragma once
// マップの窓：フィールドの道・マス・現在地（ホバーでマスの説明）。帯のボタンで開いて「戻る」で閉じる（戻るの判定は SceneManager）
#include "UiContext.h"

class MapPanel
{
public:
    void Open() { m_open = true; }
    void Close() { m_open = false; }
    bool IsOpen() const { return m_open; }
    void Draw(UiContext& ui);

private:
    bool m_open = false;
};
