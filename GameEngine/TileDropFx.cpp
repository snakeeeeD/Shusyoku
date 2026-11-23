#include "TileDropFx.h"
#include "GridMap.h"
#include "EffectManager.h"
#include <algorithm>
#include <cmath>

namespace
{
    const float DROP_TIME = 0.28f;     // 落ちてくる秒数
    const float DROP_HEIGHT = 1.6f;    // どの高さから落とすか（ワールド）
    const float SQUASH_TIME = 0.12f;   // 着地でぺたっと広がって戻る秒数
    const float SQUASH = 0.12f;        // 広がる量（1.12倍まで）
    const float STAGGER = 0.03f;       // 同じ時に何マスも置かれたら、1マスずつずらす秒数
    const float STAGGER_MAX = 0.15f;   // ずらす最大
    const float DONE = 999.0f;         // 落ち終わった
}

void TileDropFx::Update(float dt, GridMap* grid)
{
    if (!grid) return;
    int cols = grid->GetCols(), rows = grid->GetRows();
    if (!m_ready || cols != m_cols || rows != m_rows)   // 最初（と盤面が変わった時）は、今ある床を覚えるだけ
    {
        m_cols = cols; m_rows = rows;
        m_snap.assign(cols * rows, std::string());
        m_t.assign(cols * rows, DONE);
        for (int r = 0; r < rows; r++)
            for (int c = 0; c < cols; c++)
            {
                const auto& te = grid->GetCell(c, r).tileEffect;
                m_snap[r * cols + c] = te.active ? te.id : std::string();
            }
        m_ready = true;
        return;
    }
    int placed = 0;   // このフレームに新しく置かれたマスの数（順番にずらして落とす）
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
        {
            int i = r * cols + c;
            const auto& te = grid->GetCell(c, r).tileEffect;
            std::string now = te.active ? te.id : std::string();
            if (!now.empty() && now != m_snap[i])   // 新しく置かれた（別の床に変わったのも）→ 落とす
                m_t[i] = -(std::min)(STAGGER * placed++, STAGGER_MAX);
            else if (now.empty())
                m_t[i] = DONE;
            m_snap[i] = now;

            if (m_t[i] >= DONE) continue;
            float before = m_t[i];
            m_t[i] += dt;
            if (before < DROP_TIME && m_t[i] >= DROP_TIME)   // 着地した瞬間：土煙
                EffectManager::Play("tile_land", (c - cols / 2.0f) * 1.1f, 0.05f, (r - rows / 2.0f) * 1.1f);
            if (m_t[i] > DROP_TIME + SQUASH_TIME) m_t[i] = DONE;
        }
}

TileDropFx::Look TileDropFx::Get(int col, int row, const std::string& nowId) const
{
    Look l;
    if (!m_ready || col < 0 || row < 0 || col >= m_cols || row >= m_rows) return l;
    if (nowId != m_snap[row * m_cols + col]) { l.hidden = true; return l; }   // このフレームに置かれて、まだ気づいていない：次の Update で落とし始める
    float t = m_t[row * m_cols + col];
    if (t >= DONE) return l;
    if (t < 0.0f) { l.hidden = true; return l; }
    if (t < DROP_TIME)   // 落ちている：だんだん速く（u²）、影はだんだん濃く
    {
        float u = t / DROP_TIME;
        l.lift = DROP_HEIGHT * (1.0f - u * u);
        l.alpha = 0.4f + 0.6f * u;
        l.shadow = u;
        return l;
    }
    l.scale = 1.0f + SQUASH * sinf((t - DROP_TIME) / SQUASH_TIME * 3.14159265f);   // 着地：ぺたっと広がって戻る
    return l;
}
