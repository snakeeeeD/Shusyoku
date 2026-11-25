#include "CardExecutor.h"
#include "CardEffect.h"
#include "CardDamage.h"
#include "EffectManager.h"
#include "BattleHighlighter.h"
#include "TerrainDataBase.h"
#include "RelicManager.h"
#include "CardDataBase.h"
#include "RangeShape.h"
#include "ScreenShake.h"
#include "HitStop.h"
#include "Se.h"
#include "Faith.h"
#include <algorithm>
#include <queue>
#include <map>

// 罠（罠のスキル・踏んだ時・起爆・地形）
// （CardExecutor を機能ごとに分けたファイル。Play＝1回のカード使用。CardExecutor.h）

// 罠を1マスに置く（置き直しは育ち・導火線も0から）。罠の手引き書なら引く
void CardExecutor::Play::SkillPlaceTrap()
{
    auto& cell = gridMap->GetCell(targetCol, targetRow);
    cell.tileEffect = TileEffect();   // 置き直しは育ち・導火線も0から
    cell.tileEffect.active = true;
    cell.tileEffect.id = data.mainEffect.trapType;
    cell.tileEffect.value = data.mainEffect.value;
    cell.tileEffect.duration = data.mainEffect.duration;
    const TerrainDef* tDef = TerrainDataBase::Get(data.mainEffect.trapType);
    if (tDef) cell.tileEffect.persistent = tDef->persistent;
    // 罠を置いた音は、着地の時に鳴らす（TileDropFx → effects.json の tile_land）
    for (int i = 0; i < RelicManager::SumValue("trapDraw"); i++)   // 設置成功時のみ
    {
        std::string did = deck.DrawCard();
        if (!did.empty()) { hand.AddCard(did); result.drawnCards.push_back(did); }
    }
}

// 範囲の空いたマスに罠を置く。1マスでも置けたら罠の手引き書の分だけ引く
void CardExecutor::Play::SkillPlaceTrapArea()
{
    int placed = 0;
    for (int r = 0; r < gridMap->GetRows(); r++)
        for (int c = 0; c < gridMap->GetCols(); c++)
        {
            if (!RangeShape::Contains(playerCol, playerRow, c, r, data.rangeType, data.range)) continue;
            Cell& cell = gridMap->GetCell(c, r);
            if (cell.type != CellType::Empty || cell.tileEffect.active) continue;
            cell.tileEffect = TileEffect();
            cell.tileEffect.active = true;
            cell.tileEffect.id = data.mainEffect.trapType;
            cell.tileEffect.value = data.mainEffect.value;
            cell.tileEffect.duration = data.mainEffect.duration;
            const TerrainDef* tDef = TerrainDataBase::Get(data.mainEffect.trapType);
            if (tDef) cell.tileEffect.persistent = tDef->persistent;
            placed++;
        }
    // 罠を置いた音は、1マスずつの着地の時に鳴らす（TileDropFx → effects.json の tile_land）
    if (placed > 0)   // 1マスでも新規設置できた時だけ1回ぶん
        for (int i = 0; i < RelicManager::SumValue("trapDraw"); i++)
        {
            std::string did = deck.DrawCard();
            if (!did.empty()) { hand.AddCard(did); result.drawnCards.push_back(did); }
        }
}

// 一斉起爆：範囲（範囲なし＝盤面の全部）の自分の罠を集める（起爆は BattleScene が順番に）
void CardExecutor::Play::SkillDetonate()
{
    result.seqFull = (data.mainEffect.value > 0);     // value>0＝全開
    bool all = (data.rangeType == RangeType::None);   // 範囲なし＝盤面の全部
    for (int r = 0; r < gridMap->GetRows(); r++)
        for (int c = 0; c < gridMap->GetCols(); c++)
        {
            Cell& cell = gridMap->GetCell(c, r);
            if (!cell.tileEffect.active || cell.tileEffect.enemyOwned) continue;   // 敵床は集めない
            if (all || InRange(c, r))
                result.seqDetonateCells.push_back({ c, r });
        }
    if (!result.seqDetonateCells.empty()) result.startSeqDetonate = true;
}

// 罠の回収：自分の罠を全部消し、1つにつきエナジーとドロー
void CardExecutor::Play::SkillRecallTraps()
{
    int recalled = 0;
    for (int r = 0; r < gridMap->GetRows(); r++)
        for (int c = 0; c < gridMap->GetCols(); c++)
        {
            Cell& cell = gridMap->GetCell(c, r);
            if (cell.tileEffect.active && !cell.tileEffect.enemyOwned)   // 自分の罠だけ（敵の床は「地ならし」で消す）
            {
                cell.tileEffect = TileEffect();   // 削除（起爆せず消す）
                recalled++;
            }
        }
    player->AddEnergy(recalled * data.mainEffect.value);
    for (int i = 0; i < recalled * data.mainEffect.value; i++)
    {
        std::string id = deck.DrawCard();
        if (!id.empty()) { hand.AddCard(id); result.drawnCards.push_back(id); }
    }
}

// 地ならし：敵の床を消す（範囲なし＝盤面の全部）。1つにつきブロック
void CardExecutor::Play::SkillClearFloors()
{
    bool all = (data.rangeType == RangeType::None);
    int cleared = 0;
    for (int r = 0; r < gridMap->GetRows(); r++)
        for (int c = 0; c < gridMap->GetCols(); c++)
        {
            Cell& cell = gridMap->GetCell(c, r);
            if (!cell.tileEffect.active || !cell.tileEffect.enemyOwned || !(all || InRange(c, r))) continue;
            cell.tileEffect = TileEffect();
            cleared++;
        }
    if (cleared > 0 && data.mainEffect.value > 0)   // 1つにつきブロック
        player->AddBlock(player->GetBuffManager().GetFinalBlock(cleared * data.mainEffect.value));
}

// 指定した1マスの罠を起爆
void CardExecutor::Play::SkillDetonateAt()
{
    bool full = (data.mainEffect.value > 0);
    Cell& cell = gridMap->GetCell(targetCol, targetRow);
    CardExecutor::DetonateTrap(cell, targetCol, targetRow, gridMap, enemies, full);
}

// 連鎖起爆を始める（実際の起爆は BattleScene が時間差で）
void CardExecutor::Play::SkillDetonateChain()
{
    result.startChainDetonate = true;
    result.chainCol = targetCol;
    result.chainRow = targetRow;
    result.chainFull = (data.mainEffect.value > 0);
}

int CardExecutor::TrapDamageBonus()
{
    int b = RelicManager::SumValue("trapDamage");
    if (s_battlePlayer) b += s_battlePlayer->GetBuffManager().GetBuffValue(BuffType::TrapPower);
    return b;
}

void CardExecutor::TriggerTrap(Cell& cell, Enemy* enemy, int col, int row,
    GridMap* gridMap, std::vector<Enemy*>& enemies)
{
    if (!cell.tileEffect.active) return;
    if (cell.tileEffect.persistent) return;

    const TerrainDef* def = TerrainDataBase::Get(cell.tileEffect.id);
    if (!def) return;

    HitStop::Add(0.08f);

    float wx = (col - gridMap->GetCols() / 2.0f) * 1.1f;
    float wz = (row - gridMap->GetRows() / 2.0f) * 1.1f;
    std::string fx = def->vfx.empty() ? "explosion" : def->vfx;
    EffectManager::Play(fx, wx, 0.5f, wz);

    if (def->effect == "Damage")
    {
        enemy->ClearHitMark();   // 罠は部位に関係なし・脱皮にも数えない
        enemy->TakeDamage(cell.tileEffect.value + TrapDamageBonus());

        if (def->aoe)
        {
            int halfDmg = cell.tileEffect.value / 2;
            std::vector<Enemy*> hit{ enemy };   // 大きい敵は周りの複数マスにかかっても1回だけ
            for (int dr = -1; dr <= 1; dr++)
            {
                for (int dc = -1; dc <= 1; dc++)
                {
                    if (dc == 0 && dr == 0) continue;
                    int nc = col + dc;
                    int nr = row + dr;
                    if (nc < 0 || nc >= gridMap->GetCols() || nr < 0 || nr >= gridMap->GetRows())
                        continue;
                    Enemy* nearby = GetEnemyAt(nc, nr, enemies);
                    if (nearby) nearby->ClearHitMark();   // 探した時に付いた部位の印は使わない
                    if (nearby && std::find(hit.begin(), hit.end(), nearby) == hit.end())
                    {
                        nearby->TakeDamage(halfDmg);
                        hit.push_back(nearby);
                    }
                }
            }
        }
    }
    else if (def->effect == "ApplyDebuff")
    {
        Buff debuff;
        debuff.type = StringToBuffType(def->buffType);
        int extra = RelicManager::SumValue("trapDebuff");
        debuff.value = cell.tileEffect.value + extra;   // ←毒等の値
        debuff.duration = (BuffInfo::IsDurationBased(debuff.type) ? cell.tileEffect.value : def->buffDuration) + extra;   // 持続で効くデバフは罠の数字が持続
        debuff.name = def->name;
        debuff.description = L"";
        enemy->GetBuffManager().AddBuff(debuff);
    }

    if (!cell.tileEffect.persistent)
        cell.tileEffect = TileEffect();
}

void CardExecutor::TriggerTerrain(Cell& cell, Player* player)
{
    if (!cell.tileEffect.active) return;
    if (!cell.tileEffect.persistent) return;  // 罠はここでは発動しない
    if (RelicManager::HasKind("floorImmune")) return;   // 床の効果を受けないレリック

    const TerrainDef* def = TerrainDataBase::Get(cell.tileEffect.id);
    if (!def) return;

    if (def->effect == "Damage")
    {
        player->TakeDamage(cell.tileEffect.value);
    }
    else if (def->effect == "ApplyDebuff")
    {
        Buff debuff;
        debuff.type = StringToBuffType(def->buffType);
        debuff.value = cell.tileEffect.value;
        debuff.duration = def->buffDuration;
        debuff.name = def->name;
        debuff.description = L"";
        player->GetBuffManager().AddBuff(debuff);
    }

}

bool CardExecutor::DetonateTrap(Cell& cell, int col, int row,
    GridMap* gridMap, std::vector<Enemy*>& enemies, bool fullPower, bool chain)
{
    if (!cell.tileEffect.active) return false;
    if (cell.tileEffect.enemyOwned) return false;   // 敵設置の床は起爆しない
    const TerrainDef* def = TerrainDataBase::Get(cell.tileEffect.id);
    if (!def) return false;

    // 演出：爆発エフェクト＋揺れ
    float wx = (col - gridMap->GetCols() / 2.0f) * 1.1f;
    float wz = (row - gridMap->GetRows() / 2.0f) * 1.1f;
    std::string fx = def->vfx.empty() ? "explosion" : def->vfx;
    EffectManager::Play(fx, wx, 0.5f, wz);
    ScreenShake::Add(0.3f);

    std::vector<std::pair<Enemy*, bool>> hits;   // 範囲にかかった敵（大きい敵も1回だけ。真ん中にかかれば全開）
    for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++)
        {
            int nc = col + dc, nr = row + dr;
            if (nc < 0 || nc >= gridMap->GetCols() || nr < 0 || nr >= gridMap->GetRows()) continue;
            Enemy* e = GetEnemyAt(nc, nr, enemies);
            if (!e) continue;
            bool center = (dc == 0 && dr == 0);
            auto it = std::find_if(hits.begin(), hits.end(), [&](auto& h) { return h.first == e; });
            if (it == hits.end()) hits.push_back({ e, center });
            else it->second = it->second || center;
        }
    for (auto& [e, center] : hits)
        {
            int val = cell.tileEffect.value;
            if (!center && !fullPower) val /= 2;          // 周囲は半分（強化で全開）

            e->ClearHitMark();   // 起爆は部位に関係なし・脱皮にも数えない（探した時の印を消す）
            if (def->effect == "Damage")
                e->TakeDamage(val + TrapDamageBonus());
            else if (def->effect == "ApplyDebuff")
            {
                Buff b; b.type = StringToBuffType(def->buffType);
                b.value = val + RelicManager::SumValue("trapDebuff");
                b.duration = (BuffInfo::IsDurationBased(b.type) ? cell.tileEffect.value : def->buffDuration)
                    + RelicManager::SumValue("trapDebuff");   // 持続で効くデバフは罠の数字が持続
                b.name = def->name; b.description = L"";
                e->GetBuffManager().AddBuff(b);
            }
        }
    cell.tileEffect = TileEffect();

    if (chain)
    {
        for (int dr = -1; dr <= 1; dr++)
            for (int dc = -1; dc <= 1; dc++)
            {
                if (dc == 0 && dr == 0) continue;
                int nc = col + dc, nr = row + dr;
                if (nc < 0 || nc >= gridMap->GetCols() || nr < 0 || nr >= gridMap->GetRows()) continue;
                Cell& nx = gridMap->GetCell(nc, nr);
                if (nx.tileEffect.active)
                    DetonateTrap(nx, nc, nr, gridMap, enemies, fullPower, true);  // 再帰
            }
    }
    return true;
    return true;
}
