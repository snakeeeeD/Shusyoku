#include "BattleCamera.h"
#include "input.h"
#include "Renderer3D.h"
#include "ScreenShake.h"
#include "Cursor.h"
#include <algorithm>
#include <cmath>

static constexpr float CELL = 1.1f;          // 1マスの大きさ（ワールド座標）
static constexpr float HOME_BACK = 0.7f;     // 初期位置はプレイヤーの少し奥を見る（手札に隠れない）

void BattleCamera::Home(float px, float pz)
{
    m_x = px;
    m_z = pz - HOME_BACK;
    m_zoom = ZOOM_MAX;
}

void BattleCamera::ApplyAt(Renderer3D* r, float x, float z, float zoom)
{
    float sx, sz;
    ScreenShake::GetOffset(sx, sz);
    XMFLOAT3 target(x + sx, -2.0f, z + sz);
    XMFLOAT3 pos(x + sx, target.y + 17.0f * zoom, z + sz + 6.0f * zoom);
    r->SetCamera(pos, target, XMFLOAT3(0.0f, 1.0f, 0.0f));
}

void BattleCamera::ClampToBoard(int cols, int rows)
{
    float halfW = (cols / 2.0f) * CELL;
    float halfH = (rows / 2.0f) * CELL;
    float f = (m_zoom > 1.0f) ? 1.0f / m_zoom : 1.0f;   // ズームアウト時に制限が狭くなる
    m_x = (std::max)((-halfW + 2.0f) * f, (std::min)((halfW - 3.0f) * f, m_x));
    m_z = (std::max)((-halfH - 1.0f) * f, (std::min)((halfH - 2.0f) * f, m_z));
}

bool BattleCamera::HandleInput(Input& in, int cols, int rows, float px, float pz)
{
    // ズーム（ホイール）
    int wheel = in.GetMouseWheelDelta();
    if (wheel != 0)
    {
        m_homing = false;   // 自分で触ったらデモ後の戻りはやめる
        m_zoom -= wheel > 0 ? ZOOM_SPEED : -ZOOM_SPEED;
        m_zoom = (std::max)(ZOOM_MIN, (std::min)(ZOOM_MAX, m_zoom));
    }

    // パン（右ドラッグ）
    bool tapped = false;
    if (in.GetMouseButtonPress(1))
    {
        POINT mp = in.GetMousePos();
        if (!m_dragging)
        {
            m_dragging = true;
            m_dragStart = mp;
            m_dragged = false;
        }
        else
        {
            float dx = (float)(mp.x - m_dragStart.x);
            float dy = (float)(mp.y - m_dragStart.y);
            if (fabsf(dx) > 1.0f || fabsf(dy) > 1.0f)
            {
                m_dragged = true;
                m_homing = false;
            }
            m_x += dx * 0.02f * m_zoom;
            m_z -= dy * 0.02f * m_zoom;
            ClampToBoard(cols, rows);   // クランプはずらした後
            m_dragStart = mp;
        }
    }
    else
    {
        tapped = m_dragging && !m_dragged;   // ドラッグせずに離した＝単押し
        m_dragging = false;
    }

    // 初期位置へ（ホイールクリック）
    if (in.GetMouseButtonTrigger(2))
    {
        m_homing = false;
        Home(px, pz);
    }
    return tapped;
}

bool BattleCamera::AimPan(float dX, float dZ, float speed, float dt)
{
    if (fabsf(dX) <= 0.01f && fabsf(dZ) <= 0.01f) return false;
    if (!m_aimPanned)
    {
        if (!m_aimReturning) { m_aimStartX = m_x; m_aimStartZ = m_z; }
        m_aimPanned = true;
    }
    m_aimReturning = false;
    float k = (std::min)(1.0f, speed * dt);
    m_x += dX * k;
    m_z += dZ * k;
    m_autoMoving = true;
    return true;
}

bool BattleCamera::AimFollow(float goalX, float goalZ, float dt)
{
    float dX = goalX - m_x, dZ = goalZ - m_z;
    // 遊び：少し離れただけでは動かず、一定以上離れたら中央まで寄る（途中で止めると端で止まって中途半端になる）
    float dist = sqrtf(dX * dX + dZ * dZ);
    if (!m_ghostFollowing && dist > AIM_FOLLOW_START) m_ghostFollowing = true;
    if (m_ghostFollowing && dist < 0.05f)             m_ghostFollowing = false;
    if (!m_ghostFollowing) { dX = 0.0f; dZ = 0.0f; }
    return AimPan(dX, dZ, AIM_FOLLOW_SPEED, dt);
}

void BattleCamera::AimEnd()
{
    if (!m_aimPanned) return;
    m_aimReturning = !m_aimKeep;
    m_aimPanned = false;
    m_aimKeep = false;
    m_ghostFollowing = false;
}

void BattleCamera::Toward(float gx, float gz, float gzoom, float dt)
{
    float k = 1.0f - expf(-dt * 5.0f);
    m_x    += (gx - m_x) * k;
    m_z    += (gz - m_z) * k;
    m_zoom += (gzoom - m_zoom) * k;
}

// 操作デモの手順。手順を増やす時は表に1行、動きは DemoGoal に足す
enum class DemoGoal { Figure8, ZoomIn, Home };
static constexpr float DEMO8_W = 2.6f;   // 八の字の横の振れ幅（約2マス半）
static constexpr float DEMO8_H = 1.4f;   // 八の字の縦の振れ幅（盤面の中央から離れすぎない）
static const struct { const wchar_t* text; const char* icon; float dur; DemoGoal goal; } DEMO[] = {
    { L"右ドラッグ：カメラを動かす",         "mouse_rdrag",  3.6f, DemoGoal::Figure8 },
    { L"ホイール：ズーム",                   "mouse_wheel",  2.0f, DemoGoal::ZoomIn },
    { L"ホイールクリック：自分の位置に戻る", "mouse_mclick", 1.8f, DemoGoal::Home   },
};
static constexpr int DEMO_N = (int)(sizeof(DEMO) / sizeof(DEMO[0]));

void BattleCamera::StartDemo()
{
    m_demoStep = 0;
    m_demoTime = 0.0f;
    m_dragging = false;
}

bool BattleCamera::DemoCaption(const wchar_t*& text, const char*& icon, float& alpha) const
{
    if (m_demoStep < 0) return false;
    text = DEMO[m_demoStep].text;
    icon = DEMO[m_demoStep].icon;
    alpha = (std::min)(1.0f, m_demoTime / 0.2f);   // 手順ごとにふわっと
    return true;
}

void BattleCamera::Update(float dt, int cols, int rows, float px, float pz)
{
    // 狙いカメラ：寄せる前の位置へ戻す
    if (m_aimReturning)
    {
        float k = (std::min)(1.0f, 8.0f * dt);
        m_x += (m_aimStartX - m_x) * k;
        m_z += (m_aimStartZ - m_z) * k;
        if (fabsf(m_aimStartX - m_x) < 0.01f && fabsf(m_aimStartZ - m_z) < 0.01f)
        {
            m_x = m_aimStartX;
            m_z = m_aimStartZ;
            m_aimReturning = false;
        }
    }

    float hx = px, hz = pz - HOME_BACK;   // 初期位置（ホイールクリックと同じ）
    if (m_homing)   // デモの後：初期の位置へ滑らかに戻す
    {
        Toward(hx, hz, ZOOM_MAX, dt);
        if (fabsf(hx - m_x) < 0.01f && fabsf(hz - m_z) < 0.01f && fabsf(ZOOM_MAX - m_zoom) < 0.005f)
        {
            m_x = hx; m_z = hz; m_zoom = ZOOM_MAX;
            m_homing = false;
        }
        return;
    }
    if (m_demoStep < 0) return;

    Cursor::Request(CursorKind::Camera);
    m_demoTime += dt;
    if (m_demoTime >= DEMO[m_demoStep].dur)
    {
        m_demoTime = 0.0f;
        if (++m_demoStep >= DEMO_N) { EndDemo(); return; }
    }
    float gx = hx, gz = hz, gzoom = ZOOM_MAX;   // Home＝ホイールクリックと同じ位置
    // 盤面の中央（ホイールクリックの位置と同じく、少し奥に寄せた所）
    float cx = ((cols - 1) / 2.0f - cols / 2.0f) * CELL;
    float cz = ((rows - 1) / 2.0f - rows / 2.0f) * CELL - HOME_BACK;
    switch (DEMO[m_demoStep].goal)
    {
    case DemoGoal::Figure8:   // 中央を軸に八の字を1周（中央から始まり中央で終わる）
    {
        float t = m_demoTime / DEMO[m_demoStep].dur * 6.2831853f;
        gx = cx + DEMO8_W * sinf(t);
        gz = cz + DEMO8_H * sinf(2.0f * t);
        break;
    }
    case DemoGoal::ZoomIn: gx = cx; gz = cz; gzoom = ZOOM_MIN; break;
    case DemoGoal::Home:   break;
    }
    Toward(gx, gz, gzoom, dt);
}
