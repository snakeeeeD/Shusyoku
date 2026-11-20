#include "MoveFeel.h"
#include "EffectManager.h"
#include <algorithm>
#include <cmath>

namespace
{
    const float CELL = 1.1f;              // 1マスのワールドの長さ
    const float CHAIN_GAP = 0.08f;        // これより短い間で次の移動が始まったら「続けて動いた」とみなす
    const float GHOST_MIN_CELLS = 1.95f;  // 続けてこのマス数以上動いたら残像（＝2マス以上）
    const float GHOST_STEP = 0.025f;      // 残像を置く間隔（秒）
    const float GHOST_LIFE = 0.18f;       // 残像が消えるまでの秒数
    const float GHOST_ALPHA = 0.5f;       // 残像の最初の濃さ
    const float TRAIL_STEP = 0.03f;       // 光る跡を置く間隔（秒）
    const float HOP_RATIO = 0.25f;        // 跳ねる高さ（キャラの高さに対する割合）
    const float STRETCH = 0.08f;          // 空中で縦に伸びる量
    const float SQUASH_TIME = 0.12f;      // 着地でつぶれて戻るまでの秒数
    const float SQUASH_Y = 0.18f;         // 着地で縦につぶれる量
    const float SQUASH_X = 0.15f;         // 着地で横に広がる量
    const float DASH_DUST_STEP = 0.035f;  // 突進中に土煙を出す間隔（秒）
    const float BLUR_TIME = 0.10f;        // ブラーの長さ＝速さ×この秒数（この間に動いた分だけ後ろへ伸びる）
    const float BLUR_FOLLOW = 12.0f;      // ブラーが目標の長さに近づく速さ（止まった後は、この速さで縮んで消える）
}

MoveStyle MoveFeel::StyleFromName(const std::string& name)
{
    if (name == "hop")   return MoveStyle::Hop;
    if (name == "float") return MoveStyle::Float;
    if (name == "glide") return MoveStyle::Glide;
    return MoveStyle::Walk;
}

void MoveFeel::OnMoveStart(float fromX, float fromZ, float toX, float toZ, float plannedCells)
{
    float dx = toX - fromX, dz = toZ - fromZ;
    float len = sqrtf(dx * dx + dz * dz);
    if (len < 0.01f) return;                      // 動かない移動には何もしない
    m_dirX = dx / len; m_dirZ = dz / len;
    float cells = len / CELL;
    m_hops = (std::max)(1, (int)(cells + 0.5f));
    m_landed = 0;

    if (m_idle > CHAIN_GAP) m_runCells = 0.0f;    // 前の移動から間があいた＝新しい移動
    m_runCells += cells;
    m_ghostOn = !m_dash && (m_style != MoveStyle::Glide)   // 突進中は残像の代わりにブラー
        && (std::max)(m_runCells, (std::max)(plannedCells, cells)) >= GHOST_MIN_CELLS;

    if (m_style == MoveStyle::Walk)               // 進む向きと反対へ土ぼこりを蹴り出す（粒の "forward" は (sin rot, 0, cos rot) へ飛ぶ）
        EffectManager::Play("move_dust", fromX, 0.05f, fromZ, atan2f(-m_dirX, -m_dirZ));
}

void MoveFeel::Update(float dt, bool moving, float progress, float x, float z)
{
    m_idle = moving ? 0.0f : m_idle + dt;

    // 速さ（前のフレームからの動き）
    float vx = 0.0f, vz = 0.0f;
    if (m_hasPrev && dt > 0.0f) { vx = (x - m_prevX) / dt; vz = (z - m_prevZ) / dt; }
    m_prevX = x; m_prevZ = z; m_hasPrev = true;

    // ブラー：突進中は「速さ×BLUR_TIME」だけ後ろ（元いた方向）へ。突進が終わったら、なめらかに縮んで消える
    float tx = m_dash ? -vx * BLUR_TIME : 0.0f, tz = m_dash ? -vz * BLUR_TIME : 0.0f;
    float k = (std::min)(1.0f, BLUR_FOLLOW * dt);
    m_blurX += (tx - m_blurX) * k;
    m_blurZ += (tz - m_blurZ) * k;

    // 突進の土煙：動いている間、足もとから後ろへ蹴り出し続ける（足のあるキャラ・跳ねるキャラだけ）
    bool grounded = (m_style == MoveStyle::Walk || m_style == MoveStyle::Hop);
    if (m_dash && moving && grounded)
    {
        m_dustTimer += dt;
        if (m_dustTimer >= DASH_DUST_STEP)
        {
            m_dustTimer = 0.0f;
            EffectManager::Play("dash_dust", x, 0.05f, z, atan2f(-m_dirX, -m_dirZ));   // 進む向きと反対へ
        }
    }

    // 跳ねる（Hop）：1マスごとに山なり。k マス目を越えたら着地
    m_hop = 0.0f;
    if (m_style == MoveStyle::Hop && moving)
    {
        float f = progress * m_hops;
        m_hop = fabsf(sinf(3.14159265f * f));
        while (m_landed < m_hops && f >= m_landed + 1 - 0.001f)
        {
            m_landed++;
            Land(x, z);
        }
    }
    if (m_squash > 0.0f) m_squash = (std::max)(0.0f, m_squash - dt / SQUASH_TIME);

    // 光る跡（Glide）：動いている間、足元に少しずつ置いていく
    if (m_style == MoveStyle::Glide && moving)
    {
        m_trailTimer += dt;
        if (m_trailTimer >= TRAIL_STEP)
        {
            m_trailTimer = 0.0f;
            EffectManager::Play("move_trail", x, 0.05f, z);
        }
    }

    // 残像：動いている間、少し前の位置を置いていき、それぞれ薄くなって消える
    for (auto& g : m_ghosts) g.alpha -= GHOST_ALPHA * dt / GHOST_LIFE;
    m_ghosts.erase(std::remove_if(m_ghosts.begin(), m_ghosts.end(),
        [](const Ghost& g) { return g.alpha <= 0.0f; }), m_ghosts.end());
    if (m_ghostOn && moving)
    {
        m_ghostTimer += dt;
        if (m_ghostTimer >= GHOST_STEP)
        {
            m_ghostTimer = 0.0f;
            m_ghosts.push_back({ x, z, GHOST_ALPHA });
        }
    }
    if (!moving && m_idle > CHAIN_GAP) m_ghostOn = false;
}

void MoveFeel::SetDash(bool on)
{
    bool grounded = (m_style == MoveStyle::Walk || m_style == MoveStyle::Hop);
    if (m_dash && !on && grounded && m_hasPrev)   // 止まった所で、前へぶわっと土煙
        EffectManager::Play("dash_stop", m_prevX, 0.05f, m_prevZ, atan2f(m_dirX, m_dirZ));
    m_dash = on;
    m_dustTimer = DASH_DUST_STEP;   // 始まってすぐ1つ目を出す
    if (on) m_ghostOn = false;
}

void MoveFeel::Land(float x, float z)
{
    m_squash = 1.0f;
    float side = atan2f(m_dirZ, -m_dirX);         // 進む向きに直角（左右）。粒の "forward" の向き (sin, cos) が (dirZ, -dirX) になる角度
    EffectManager::Play("move_land", x, 0.05f, z, side);
    EffectManager::Play("move_land", x, 0.05f, z, side + 3.14159265f);
}

float MoveFeel::Lift(float height) const { return m_hop * HOP_RATIO * height; }
float MoveFeel::ScaleX() const { return 1.0f + SQUASH_X * m_squash - STRETCH * 0.5f * m_hop; }
float MoveFeel::ScaleY() const { return (1.0f + STRETCH * m_hop) * (1.0f - SQUASH_Y * m_squash); }
