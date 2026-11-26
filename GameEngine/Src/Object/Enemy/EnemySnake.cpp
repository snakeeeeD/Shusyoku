#include "Enemy.h"
#include "Renderer3D.h"
#include "TextureManager.h"
#include "EffectManager.h"
#include "TerrainDataBase.h"
#include "Player.h"
#include "BuffInfo.h"
#include "GameUtils.h"
#include "RangeShape.h"
#include "FloatingText.h"
#include "ScreenShake.h"
#include "HitStop.h"
#include "Audio.h"
#include "EffectDataBase.h"   // 溶ける見た目の材質（fx_dissolve）
#include "CardExecutor.h"     // 床の効果（TriggerTerrain）
#include "RelicManager.h"     // 床の効果を受けないレリック
#include <algorithm>
#include <map>
#include <queue>

// 大蛇（巻きつき・首を伸ばす／戻す・体のつながり・中央へ押す）

void Enemy::InitSnake(GridMap* gridMap)
{
    if (!m_isSnake) return;
    int cols = gridMap->GetCols(), rows = gridMap->GetRows();
    m_gridCols = cols; m_gridRows = rows;
    m_grid = gridMap;
    m_spiral.clear(); m_bodyCells.clear(); m_spiralIdx = 0;
    int top = 0, bot = rows - 1, left = 0, right = cols - 1;   // 外周→内へ渦巻き
    while (top <= bot && left <= right)
    {
        for (int c = left; c <= right; c++) m_spiral.push_back({ c, top });   top++;
        for (int r = top; r <= bot; r++) m_spiral.push_back({ right, r });  right--;
        if (top <= bot) { for (int c = right; c >= left; c--) m_spiral.push_back({ c, bot }); bot--; }
        if (left <= right) { for (int r = bot;   r >= top;  r--) m_spiral.push_back({ left, r }); left++; }
    }
    gridMap->SetCellType(gridCol, gridRow, CellType::Empty);   // 元の位置を空ける
    gridCol = m_spiral[0].first; gridRow = m_spiral[0].second; // 頭を渦巻き先頭へ
    gridMap->SetCellType(gridCol, gridRow, CellType::Boss);
    worldX = (gridCol - cols / 2.0f) * 1.1f;
    worldZ = (gridRow - rows / 2.0f) * 1.1f;
}

// 蛇の1マスの時間：長い距離ほど速く（base より遅くはしない）
static float SnakeStepDur(int cells, float base)
{
    return min(base, 0.03f + 0.2f / cells);
}

void Enemy::GrowCoil(GridMap* gridMap, int steps, Player* player)
{
    if (!m_isSnake || m_spiral.empty()) return;
    if (m_buffManager.HasBuff(BuffType::Root)) return;   // 拘束中は巻きつきも、まるのみの準備も止まる

    m_hitPart = HitPart::None;                       // とぐろ移動の罠は2倍対象外
    std::vector<std::pair<float, float>> pts;
    bool boxedIn = false;   // プレイヤーを押せなかった（周りが全部ふさがっていて、もう逃げ場がない）
    for (int s = 0; s < steps; s++)
    {
        if (m_spiralIdx + 1 >= (int)m_spiral.size()) break;

        auto nx = m_spiral[m_spiralIdx + 1];
        CellType t = gridMap->GetCell(nx.first, nx.second).type;
        if (t == CellType::Wall) break;
        if (t == CellType::Player)
        {
            if (player) player->TakeDamage(8);              // 轢かれダメージ（攻撃とは別）
            if (!PushPlayerToCenter(gridMap, player)) { boxedIn = true; break; }   // 押せない＝プレイヤーは周りを全部ふさがれていて動けない
        }
        // 以降、頭を nx へ進める（既存のまま）
        QueueSnakeOp(true, nx);
        m_bodyCells.push_back({ gridCol, gridRow });
        gridMap->SetCellType(gridCol, gridRow, CellType::Boss);
        gridCol = nx.first; gridRow = nx.second;
        gridMap->SetCellType(gridCol, gridRow, CellType::Boss);
        m_spiralIdx++;
        m_movePath.push_back({ gridCol, gridRow });        // 通過マス（罠判定用）
        int cc = gridMap->GetCols() / 2, cr = gridMap->GetRows() / 2;
        if (abs(gridCol - cc) + abs(gridRow - cr) <= 1) m_coilReached = true;
        pts.push_back({ (gridCol - gridMap->GetCols() / 2.0f) * 1.1f,
                        (gridRow - gridMap->GetRows() / 2.0f) * 1.1f });
    }
    if (!pts.empty())                          // 首を戻す歩きの続きにつなぐ（合わせた距離で速さを決める）
        AppendWalk(pts, SnakeStepDur((int)(m_walkPoints.size() + pts.size()), 0.1f));

    // もう進めない（渦の最後のマスまで来た／次のマスが壁／プレイヤーが囲まれて押せない）と分かったら、次のターンにまるのみ（フィニッシャー解禁）
    bool atEnd = (m_spiralIdx + 1 >= (int)m_spiral.size());
    bool walled = !atEnd && gridMap->GetCell(m_spiral[m_spiralIdx + 1].first, m_spiral[m_spiralIdx + 1].second).type == CellType::Wall;
    if (atEnd || walled || boxedIn) m_coilComplete = true;
}

// 頭からプレイヤーのマスまでの経路（空きマスだけを通る、最大8マス）
std::vector<std::pair<int, int>> Enemy::LungePath(int aimCol, int aimRow) const
{
    constexpr int MAX_LEN = 8;
    static const int DIRS[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    std::map<std::pair<int, int>, std::pair<int, int>> from;
    std::queue<std::pair<std::pair<int, int>, int>> q;
    std::pair<int, int> start{ gridCol, gridRow }, goal{ aimCol, aimRow };
    q.push({ start, 0 }); from[start] = start;
    bool found = false;
    while (!q.empty() && !found)
    {
        auto [p, d] = q.front(); q.pop();
        if (p == goal) { found = true; break; }
        if (d >= MAX_LEN) continue;
        for (auto& dir : DIRS)
        {
            std::pair<int, int> n{ p.first + dir[0], p.second + dir[1] };
            if (n.first < 0 || n.first >= m_gridCols || n.second < 0 || n.second >= m_gridRows) continue;
            bool free = (n == goal) || m_grid->GetCell(n.first, n.second).type == CellType::Empty;
            if (from.count(n) || !free) continue;
            from[n] = p; q.push({ n, d + 1 });
        }
    }
    std::vector<std::pair<int, int>> path;
    if (!found) return path;
    for (auto p = goal; p != start; p = from[p]) path.push_back(p);
    std::reverse(path.begin(), path.end());
    return path;
}

// 噛みつき：予告した経路を進む。空いていないマス（プレイヤーなど）の手前で止まる
void Enemy::LungeTo(GridMap* gridMap)
{
    m_turnsSinceLunge = 0;
    std::vector<std::pair<float, float>> pts;
    for (auto& c : m_lungePlan)
    {
        if (gridMap->GetCell(c.first, c.second).type != CellType::Empty) break;
        QueueSnakeOp(true, c);
        m_bodyCells.push_back({ gridCol, gridRow });
        gridCol = c.first; gridRow = c.second;
        gridMap->SetCellType(gridCol, gridRow, CellType::Boss);
        m_movePath.push_back(c);
        pts.push_back({ (gridCol - m_gridCols / 2.0f) * 1.1f, (gridRow - m_gridRows / 2.0f) * 1.1f });
        m_neckLen++;
    }
    m_lungePlan.clear();
    m_plannedMovePath.clear();   // 手前で止まっても矢印が残らないよう消す
    if (!pts.empty()) StartWalk(pts, SnakeStepDur((int)pts.size(), 0.06f));
}

// 体の見た目の操作をためる（最初の1つの時に、今の体を土台として保存）
void Enemy::QueueSnakeOp(bool push, std::pair<int, int> cell)
{
    if (m_snakeOps.empty())
    {
        m_visualBase = m_bodyCells;
        m_visualBase.push_back({ gridCol, gridRow });
        m_opApplied = 0;
    }
    m_snakeOps.push_back({ push, cell });
}

// 描画用の体の列：頭の歩きの進み具合に合わせて、操作を1つずつ反映する
std::vector<std::pair<int, int>> Enemy::VisualChain()
{
    bool walking = IsMoving() || !m_walkPoints.empty();
    if (!walking) { m_snakeOps.clear(); m_visualBase.clear(); }
    if (m_snakeOps.empty())
    {
        auto chain = m_bodyCells;
        chain.push_back({ gridCol, gridRow });
        return chain;
    }
    int done = m_walkIndex;                                       // 歩き終えたマス数
    while (m_opApplied < (int)m_snakeOps.size())
    {
        auto& op = m_snakeOps[m_opApplied];
        if (op.push ? m_opApplied < done : m_opApplied <= done)   // 伸ばす＝通過後、戻す＝歩き始めに反映
        {
            if (op.push) m_visualBase.push_back(op.cell); else m_visualBase.pop_back();
            m_opApplied++;
        }
        else break;
    }
    auto chain = m_visualBase;
    if (done < (int)m_snakeOps.size() && m_snakeOps[done].push)
        chain.push_back(m_snakeOps[done].cell);                   // 今向かっているマス（頭の向きと曲がり角のため）
    return chain;
}

void Enemy::RetractNeck(GridMap* gridMap)
{
    std::vector<std::pair<float, float>> pts;
    for (; m_neckLen > 0; m_neckLen--)
    {
        QueueSnakeOp(false, m_bodyCells.back());
        gridMap->SetCellType(gridCol, gridRow, CellType::Empty);
        auto b = m_bodyCells.back(); m_bodyCells.pop_back();
        gridCol = b.first; gridRow = b.second;
        pts.push_back({ (gridCol - m_gridCols / 2.0f) * 1.1f, (gridRow - m_gridRows / 2.0f) * 1.1f });
    }
    if (!pts.empty()) StartWalk(pts, SnakeStepDur((int)pts.size(), 0.06f));
}

bool Enemy::PushPlayerToCenter(GridMap* gridMap, Player* player)
{
    if (!player) return false;
    int pc = player->gridCol, pr = player->gridRow;
    int cc = gridMap->GetCols() / 2, cr = gridMap->GetRows() / 2;
    int dc = cc - pc, dr = cr - pr;

    std::vector<std::pair<int, int>> tries;
    if (abs(dc) >= abs(dr)) { if (dc) tries.push_back({ dc > 0 ? 1 : -1, 0 }); if (dr) tries.push_back({ 0, dr > 0 ? 1 : -1 }); }
    else { if (dr) tries.push_back({ 0, dr > 0 ? 1 : -1 }); if (dc) tries.push_back({ dc > 0 ? 1 : -1, 0 }); }
    // フォールバック：中央方向が塞がってたら空いてる方へ押し出す
    tries.push_back({ 1,0 }); tries.push_back({ -1,0 }); tries.push_back({ 0,1 }); tries.push_back({ 0,-1 });

    for (auto& [sx, sy] : tries)
    {
        int nc = pc + sx, nr = pr + sy;
        if (nc < 0 || nc >= gridMap->GetCols() || nr < 0 || nr >= gridMap->GetRows()) continue;
        if (gridMap->GetCell(nc, nr).type != CellType::Empty) continue;
        gridMap->SetCellType(pc, pr, CellType::Empty);
        player->gridCol = nc; player->gridRow = nr;
        gridMap->SetCellType(nc, nr, CellType::Player);
        std::vector<std::pair<float, float>> pts = {
            { (nc - gridMap->GetCols() / 2.0f) * 1.1f, (nr - gridMap->GetRows() / 2.0f) * 1.1f } };
        player->StartWalk(pts, 0.08f);
        return true;
    }
    return false;   // 完全に囲まれてて押せない
}
