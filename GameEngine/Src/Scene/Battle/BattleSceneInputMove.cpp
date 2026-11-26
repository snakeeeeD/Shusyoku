#include "CardEffect.h"
#include "BattleScene.h"
#include "CardTooltip.h"
#include "TextureLoader.h"
#include "EffectManager.h"
#include "RelicManager.h"
#include "CardExecutor.h"
#include "TerrainDataBase.h"
#include "MaterialDataBase.h"
#include "HighlightPalette.h"
#include "FloatingText.h"
#include "ScreenShake.h"
#include "HitStop.h"
#include "TurnBanner.h"
#include "DamageFeedback.h"
#include "RangeShape.h"
#include "UiNotice.h"
#include "Cursor.h"
#include "Audio.h"
#include "Hint.h"
#include "PileFx.h"
#include "Telemetry.h"
#include "EnemyIntentVisual.h"
#include "Faith.h"

#include <algorithm>
#include <cstdio>
#include <queue>
#include <map>
#include <set>

#ifdef _DEBUG
#include "External/imgui/imgui.h"
#endif

// 移動の入力（経路づくり・最短経路・カードで動いた後の歩き／氷）

// 移動カードの経路づくり（カメラが自動で動いただけの時は経路を変えない）
void BattleScene::UpdateMovePath()
{
    // カメラが自動で動いて、マウスの下のマスが変わっただけ（マウスは止まっている）なら、経路は変えない
    POINT mpNow = m_input.GetMousePos();
    bool mouseMoved = (mpNow.x != m_prevPathMouse.x || mpNow.y != m_prevPathMouse.y);
    m_prevPathMouse = mpNow;
    bool cameraDriven = m_camera.AutoMoving() && !mouseMoved;

    // === 移動カード：手動経路構築 ===
    int moveRangeSel = SelectedMoveRange();

    if (moveRangeSel >= 0)
    {
        if (!m_pathBuilding) m_movePath.clear();   // 選択直後にリセット
        m_pathBuilding = true;

        // 右クリックキャンセル中：カーソルを別マスに動かすまで経路を出さない
        if (m_moveReleaseSuppress
            && m_hoveredCell.first == m_suppressCell.first
            && m_hoveredCell.second == m_suppressCell.second)
        {
            m_movePath.clear();
        }
        else if (m_hoveredCell.first >= 0 && !cameraDriven)
            EditMovePath(moveRangeSel);
    }
    else
    {
        m_pathBuilding = false;
        m_movePath.clear();
    }
}

// 選んでいるのが移動カードなら、その移動範囲（移動カードでなければ -1）
int BattleScene::SelectedMoveRange()
{
    const auto& cards = m_hand.GetCards();
    if (m_selectedCardIndex >= 0 && m_selectedCardIndex < (int)cards.size())
    {
        const CardData* sd = cards[m_selectedCardIndex]->GetData();
        if (sd && sd->type == CardType::Move) return m_player->GetMoveRange(sd->range);
    }
    return -1;
}

// 指しているマスへ経路を伸ばす・戻す（戻すのは数フレーム止まってから）・届く所まで引き直す・離れたマスは最短経路
void BattleScene::EditMovePath(int moveRangeSel)
{
        m_moveReleaseSuppress = false;   // カーソルが動いたら再開
        int hc = m_hoveredCell.first, hr = m_hoveredCell.second;

        int tipCol = m_movePath.empty() ? m_playerCol : m_movePath.back().first;
        int tipRow = m_movePath.empty() ? m_playerRow : m_movePath.back().second;

        int idx = -1;
        for (int i = 0; i < (int)m_movePath.size(); i++)
            if (m_movePath[i].first == hc && m_movePath[i].second == hr) { idx = i; break; }

        bool onTip = (hc == tipCol && hr == tipRow);
        bool onPlayer = (hc == m_playerCol && hr == m_playerRow);
        bool isRetract = onPlayer || (idx >= 0 && idx < (int)m_movePath.size() - 1);

        if (onTip)
        {
            // 先端の上：何もしない
            m_backtrackFrames = 0;
        }
        else if (isRetract)
        {
            // 戻す：数フレーム安定してから（縦の一瞬のブレで誤爆させない）
            if (hc == m_backtrackCell.first && hr == m_backtrackCell.second)
                m_backtrackFrames++;
            else { m_backtrackCell = { hc, hr }; m_backtrackFrames = 1; }

            if (m_backtrackFrames >= 4)
            {
                if (onPlayer) m_movePath.clear();
                else m_movePath.resize(idx + 1);
            }
        }
        else
        {
            m_backtrackFrames = 0;
            int distTip = abs(hc - tipCol) + abs(hr - tipRow);

            if (distTip == 1)
            {
                int budget = moveRangeSel - (int)m_movePath.size();
                if (budget > 0 && m_gridMap->GetCell(hc, hr).type == CellType::Empty)
                {
                    // 余裕あり：そのまま1マス追加
                    m_movePath.push_back({ hc, hr });
                }
                else if (m_gridMap->GetCell(hc, hr).type == CellType::Empty)
                {
                    // 最大到達中：後ろから1マスずつ戻して、届く接頭辞を探す
                    for (int keep = (int)m_movePath.size() - 1; keep >= 0; keep--)
                    {
                        int b = moveRangeSel - keep;
                        int ptc = (keep == 0) ? m_playerCol : m_movePath[keep - 1].first;
                        int ptr = (keep == 0) ? m_playerRow : m_movePath[keep - 1].second;

                        std::set<std::pair<int, int>> blocked(m_movePath.begin(), m_movePath.begin() + keep);
                        blocked.insert({ m_playerCol, m_playerRow });
                        blocked.erase({ ptc, ptr });
                        auto conn = ShortestPath({ ptc, ptr }, { hc, hr }, b, blocked);
                        if (!conn.empty())
                        {
                            m_movePath.resize(keep);
                            for (auto& c : conn) m_movePath.push_back(c);
                            break;
                        }
                    }
                }
            }
            else
            {
                // 離れたマス：プレイヤーから自動最短経路（手動経路を破棄）
                m_movePath = ShortestPath({ m_playerCol, m_playerRow }, { hc, hr }, moveRangeSel);
            }
        }
}

// from から to への最短経路（空きマスだけ・budget 歩以内・blocked は通らない）。from は含まず to を含む。見つからなければ空
std::vector<std::pair<int, int>> BattleScene::ShortestPath(std::pair<int, int> from, std::pair<int, int> to, int budget,
    const std::set<std::pair<int, int>>& blocked) const
{
    static const int dirs[4][2] = { {0,1},{0,-1},{1,0},{-1,0} };
    std::queue<std::pair<int, int>> q;
    std::map<std::pair<int, int>, std::pair<int, int>> par;
    std::map<std::pair<int, int>, int> dist;
    q.push(from); dist[from] = 0; par[from] = { -1, -1 };
    bool found = false;
    while (!q.empty())
    {
        auto [cc, rr] = q.front(); q.pop();
        if (cc == to.first && rr == to.second) { found = true; break; }
        if (dist[{cc, rr}] >= budget) continue;
        for (auto& d : dirs)
        {
            int nc = cc + d[0], nr = rr + d[1];
            if (nc < 0 || nc >= m_gridMap->GetCols() || nr < 0 || nr >= m_gridMap->GetRows()) continue;
            auto np = std::make_pair(nc, nr);
            if (dist.count(np) || blocked.count(np)) continue;
            if (m_gridMap->GetCell(nc, nr).type != CellType::Empty) continue;
            dist[np] = dist[{cc, rr}] + 1;
            par[np] = { cc, rr };
            q.push(np);
        }
    }
    std::vector<std::pair<int, int>> path;
    if (!found) return path;
    for (auto cur = to; cur != from; cur = par[cur]) path.push_back(cur);
    std::reverse(path.begin(), path.end());
    return path;
}

// カードで動いた後のプレイヤー：歩く（経路があれば経路どおり）・氷で滑る・着いたマスの地形
void BattleScene::MovePlayerAfterCard(int newCol, int newRow, int moveDist, const std::vector<std::pair<int, int>>* usePath)
{
    int oldCol = m_playerCol;
    int oldRow = m_playerRow;
    m_playerCol = newCol;
    m_playerRow = newRow;
    m_player->gridCol = m_playerCol;
    m_player->gridRow = m_playerRow;
    OnPlayerMoved(oldCol, oldRow, moveDist);   // レリック：移動時
    float px = (m_playerCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
    float pz = (m_playerRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
    if (usePath && !usePath->empty())
    {
        std::vector<std::pair<float, float>> pts;
        for (auto& p : *usePath)
            pts.push_back({ (p.first - m_gridMap->GetCols() / 2.0f) * 1.1f,
                            (p.second - m_gridMap->GetRows() / 2.0f) * 1.1f });
        m_player->StartWalk(pts, 0.08f);
    }
    else
    {
        m_player->StartMove(px, pz);
    }

    auto& movedCell = m_gridMap->GetCell(m_playerCol, m_playerRow);

    // 氷チェック（TriggerTerrainでdurationが消える前に）
    int slideDx = 0, slideDy = 0;
    bool isSlide = IceSlideDirection(movedCell, oldCol, oldRow, usePath, slideDx, slideDy);

    CardExecutor::TriggerTerrain(movedCell, m_player);

    if (isSlide && (slideDx != 0 || slideDy != 0))
        SlideOnIce(slideDx, slideDy, px, pz, usePath);
}

// 着いたマスが氷なら、滑る向きを決める（向きは最後の1歩で決める。斜めならランダムに縦か横）。滑るなら true（地形の効果が消える前に調べる）
bool BattleScene::IceSlideDirection(const Cell& movedCell, int oldCol, int oldRow, const std::vector<std::pair<int, int>>* usePath, int& slideDx, int& slideDy)
{
    bool isSlide = false;
    if (movedCell.tileEffect.active)
    {
        const TerrainDef* def = TerrainDataBase::Get(movedCell.tileEffect.id);
        if (def && def->effect == "Slide" && !RelicManager::HasKind("floorImmune"))
        {
            isSlide = true;
            // 向きは「最後の1歩」で決める（回り込んだ時に全体のずれで決めると斜め扱い→ランダムになる）
            int fromCol = oldCol, fromRow = oldRow;
            if (usePath && usePath->size() >= 2)
            {
                fromCol = (*usePath)[usePath->size() - 2].first;
                fromRow = (*usePath)[usePath->size() - 2].second;
            }
            int dx = m_playerCol - fromCol;
            int dy = m_playerRow - fromRow;
            if (dx != 0 && dy != 0)
            {
                if (rand() % 2 == 0)
                    slideDx = dx > 0 ? 1 : -1;
                else
                    slideDy = dy > 0 ? 1 : -1;
            }
            else
            {
                slideDx = (dx > 0) ? 1 : (dx < 0) ? -1 : 0;
                slideDy = (dy > 0) ? 1 : (dy < 0) ? -1 : 0;
            }
        }
    }
    return isSlide;
}

// 氷の上を滑る：空いている間は進み、氷でないマスに着いたらその地形の効果（歩き終わってから同じ速さで続ける）
void BattleScene::SlideOnIce(int slideDx, int slideDy, float px, float pz, const std::vector<std::pair<int, int>>* usePath)
{
    bool walked = (usePath && !usePath->empty());
    std::vector<std::pair<float, float>> slidePts;   // すべった先のマス（歩きの続きにつなぐ）
    if (!walked) slidePts.push_back({ px, pz });     // 経路なしの移動は、まず着地点へ
    while (true)
    {
        int nextCol = m_playerCol + slideDx;
        int nextRow = m_playerRow + slideDy;

        if (nextCol < 0 || nextCol >= m_gridMap->GetCols() ||
            nextRow < 0 || nextRow >= m_gridMap->GetRows())
            break;

        auto& nextCell = m_gridMap->GetCell(nextCol, nextRow);
        if (nextCell.type != CellType::Empty)
            break;

        m_gridMap->SetCellType(m_playerCol, m_playerRow, CellType::Empty);
        m_playerCol = nextCol;
        m_playerRow = nextRow;
        m_gridMap->SetCellType(m_playerCol, m_playerRow, CellType::Player);
        m_player->gridCol = m_playerCol;
        m_player->gridRow = m_playerRow;
        float sx = (m_playerCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
        float sz = (m_playerRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
        slidePts.push_back({ sx, sz });

        auto& slideCell = m_gridMap->GetCell(m_playerCol, m_playerRow);
        if (slideCell.tileEffect.active)
        {
            const TerrainDef* slideDef = TerrainDataBase::Get(slideCell.tileEffect.id);
            if (slideDef && slideDef->effect == "Slide")
                continue;

            CardExecutor::TriggerTerrain(slideCell, m_player);
        }
        break;
    }
    if (slidePts.size() > (walked ? 0u : 1u))         // 1マスでもすべった時だけ
        m_player->AppendWalk(slidePts, 0.08f);        // 歩き終わってから、同じ速さでスーッと続ける
}
