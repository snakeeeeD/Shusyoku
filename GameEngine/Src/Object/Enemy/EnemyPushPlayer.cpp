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

// プレイヤーを動かす（引き寄せ・吹き飛ばし。氷で滑る・引きずる）
// （Enemy のメンバー。Enemy.cpp から機能ごとに分けたファイル）

// 敵に動かされたプレイヤーを (nc,nr) へ1マス進める。床の効果もかける（カードで動いた時と同じ TriggerTerrain）。進めなければ false
static bool DragPlayerStep(int nc, int nr, int& pc, int& pr, GridMap* g, Player* player, std::vector<std::pair<float, float>>& pts)
{
    if (nc < 0 || nc >= g->GetCols() || nr < 0 || nr >= g->GetRows()) return false;
    if (g->GetCell(nc, nr).type != CellType::Empty) return false;   // 壁・敵で止まる
    g->SetCellType(pc, pr, CellType::Empty);
    pc = nc; pr = nr;
    g->SetCellType(pc, pr, CellType::Player);
    pts.push_back({ (pc - g->GetCols() / 2.0f) * 1.1f, (pr - g->GetRows() / 2.0f) * 1.1f });
    CardExecutor::TriggerTerrain(g->GetCell(pc, pr), player);
    return true;
}

// 動かされて氷の上で止まったら、同じ向きに滑り続ける（ふさがるか、氷でない所に着くまで）
static void SlideOnIce(int dc, int dr, int& pc, int& pr, GridMap* g, Player* player, std::vector<std::pair<float, float>>& pts)
{
    if (pts.empty() || (dc == 0 && dr == 0) || RelicManager::HasKind("floorImmune")) return;   // 動いていない時は滑らない
    auto onIce = [&]() {
        const auto& t = g->GetCell(pc, pr).tileEffect;
        const TerrainDef* def = t.active ? TerrainDataBase::Get(t.id) : nullptr;
        return def && def->effect == "Slide";
    };
    while (onIce() && DragPlayerStep(pc + dc, pr + dr, pc, pr, g, player, pts)) {}
}

void Enemy::PullPlayer(int playerCol, int playerRow, GridMap* gridMap, Player* player, int steps)
{
    if (!player) return;
    if (player->GetBuffManager().HasBuff(BuffType::MoveLock)) return;   // 踏ん張り中は引き寄せられない
    int pc = playerCol, pr = playerRow;
    std::vector<std::pair<float, float>> pts;
    // ちょうど斜めの線の上から引かれた時は、斜めにまっすぐ引く（縦横のジグザグで横にずれないように）
    const bool diag = (gridCol != pc && abs(gridCol - pc) == abs(gridRow - pr));
    int lastDc = 0, lastDr = 0;   // 最後に動いた向き（氷で滑る向き）

    for (int step = 0; step < steps; step++)
    {
        int dc = gridCol - pc;      // 敵の方向へ
        int dr = gridRow - pr;
        if (diag ? abs(dc) <= 1 : abs(dc) + abs(dr) <= 1) break;   // 敵に隣接したら止める（斜めなら斜め隣で）

        std::vector<std::pair<int, int>> candidates;
        if (diag)
            candidates.push_back({ pc + (dc > 0 ? 1 : -1), pr + (dr > 0 ? 1 : -1) });   // ふさがっていたら止める（横にはずらさない）
        else if (abs(dc) >= abs(dr))
        {
            candidates.push_back({ pc + (dc > 0 ? 1 : -1), pr });
            if (dr != 0) candidates.push_back({ pc, pr + (dr > 0 ? 1 : -1) });
        }
        else
        {
            candidates.push_back({ pc, pr + (dr > 0 ? 1 : -1) });
            if (dc != 0) candidates.push_back({ pc + (dc > 0 ? 1 : -1), pr });
        }

        bool moved = false;
        for (auto& [nc, nr] : candidates)
        {
            int sc = nc - pc, sr = nr - pr;
            if (!DragPlayerStep(nc, nr, pc, pr, gridMap, player, pts)) continue;   // 壁・敵で止まる（床の効果もかかる）
            lastDc = sc; lastDr = sr;
            moved = true;
            break;
        }
        if (!moved) break;
    }
    SlideOnIce(lastDc, lastDr, pc, pr, gridMap, player, pts);   // 氷で止まったら、引かれた向きに滑り続ける

    player->gridCol = pc;
    player->gridRow = pr;
    if (!pts.empty())
    {
        player->StartWalk(pts, 0.1f);
        player->m_feel.SetDash(true, true);  // 引きずられる間も土煙とブラー
        player->StartHurt();   // 引っぱられている間は、ダメージが無くても被弾のアニメ（よろけながら引きずられる）
    }
}

void Enemy::KnockbackPlayer(int playerCol, int playerRow, GridMap* gridMap, Player* player, int steps)
{
    if (!player) return;
    if (player->GetBuffManager().HasBuff(BuffType::MoveLock)) return;   // 踏ん張り中は吹き飛ばない
    int pc = playerCol, pr = playerRow;

    // 敵から離れる主方向を1軸に固定（まっすぐ吹き飛ばす）
    int dc = pc - gridCol, dr = pr - gridRow;
    if (dc == 0 && dr == 0) return;
    int stepCol = 0, stepRow = 0;
    if (abs(dc) >= abs(dr)) stepCol = (dc >= 0) ? 1 : -1;
    else                    stepRow = (dr >= 0) ? 1 : -1;

    std::vector<std::pair<float, float>> pts;
    for (int step = 0; step < steps; step++)
        if (!DragPlayerStep(pc + stepCol, pr + stepRow, pc, pr, gridMap, player, pts)) break;   // 壁・敵で止まる（床の効果もかかる）
    SlideOnIce(stepCol, stepRow, pc, pr, gridMap, player, pts);   // 氷で止まったら、飛ばされた向きに滑り続ける

    player->gridCol = pc;
    player->gridRow = pr;
    if (!pts.empty())
    {
        player->StartWalk(pts, 0.08f);       // 速めで吹き飛ばす感
        player->m_feel.SetDash(true, true);  // 飛ばされる間は土煙とブラー（止まったら自動で終わる）
        player->StartHurt();                 // ダメージをブロックで防いでも、よろけながら飛ばされる
    }
}
