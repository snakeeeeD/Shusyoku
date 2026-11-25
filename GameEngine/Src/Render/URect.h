#pragma once
#include "Audio.h"
#include "Cursor.h"
#include <string>
#include <windows.h>

struct URect {
    float x, y, w, h;
    bool has(POINT p) const {
        return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
    }
};

inline bool UiHover(float x, float y, float w, float h, POINT mp,
    const char* key, std::string& hoverKey)
{
    bool over = mp.x >= x && mp.x <= x + w && mp.y >= y && mp.y <= y + h;
    if (over) Cursor::Request(CursorKind::Hover);   // 押せる物の上＝指差し
    if (over && hoverKey != key) { Audio::PlaySE("Assets/Sound/se/hover.mp3"); hoverKey = key; }
    else if (!over && hoverKey == key) hoverKey.clear();
    return over;
}