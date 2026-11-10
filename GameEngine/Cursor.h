#pragma once
#include <windows.h>

// マウスカーソルの見た目。毎フレーム頭で Begin → シーンが Request → 描画の後に Apply
// 種類を増やす時は enum と FILES に1行ずつ（後ろほど優先）。絵は tools/cursors/make_cursors.py で作る
enum class CursorKind { Normal, Hover, Camera, Count };   // 後ろほど優先

namespace Cursor
{
    // { ふだんの絵, 左ボタンを押している間の絵（nullptr＝変えない） }
    inline const wchar_t* FILES[][2] = {
        { L"Assets/UI/Cursor/normal.cur", nullptr },
        { L"Assets/UI/Cursor/hover.cur",  L"Assets/UI/Cursor/hover_press.cur" },   // 押すと人差し指を折る
        { L"Assets/UI/Cursor/camera.cur", nullptr },
    };
    inline HCURSOR    s_handle[(int)CursorKind::Count][2] = {};
    inline CursorKind s_want = CursorKind::Normal;
    inline int        s_applied = -1;   // 最後に反映した 種類*2+押し中

    inline void Load()
    {
        for (int i = 0; i < (int)CursorKind::Count; i++)
            for (int p = 0; p < 2; p++)
                s_handle[i][p] = FILES[i][p] ? LoadCursorFromFileW(FILES[i][p]) : nullptr;
    }
    inline bool Pressed() { return (GetKeyState(VK_LBUTTON) & 0x8000) != 0; }   // 左ボタンを押している間
    inline HCURSOR Handle()   // 押し中の絵が無ければふだんの絵、それも無ければ Windows の矢印
    {
        HCURSOR h = Pressed() ? s_handle[(int)s_want][1] : nullptr;
        if (!h) h = s_handle[(int)s_want][0];
        return h ? h : LoadCursor(nullptr, IDC_ARROW);
    }
    inline void Begin() { s_want = CursorKind::Normal; }
    inline void Request(CursorKind k) { if ((int)k > (int)s_want) s_want = k; }   // 優先度の高い方を残す
    inline void Apply(HWND hwnd)   // 変わった時だけ、カーソルが窓の中にあれば差し替える
    {
        int state = (int)s_want * 2 + (Pressed() ? 1 : 0);
        if (state == s_applied) return;
        s_applied = state;
        POINT p; GetCursorPos(&p);
        if (WindowFromPoint(p) != hwnd) return;
        ScreenToClient(hwnd, &p);
        RECT rc; GetClientRect(hwnd, &rc);
        if (PtInRect(&rc, p)) SetCursor(Handle());
    }
}
