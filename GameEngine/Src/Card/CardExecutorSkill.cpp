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

// スキルカード（使えるかの確認・効果・サブ効果）

// スキルカード（状態異常・呪いも同じ。効果なし＝廃棄/捨て札へ）
bool CardExecutor::Play::Skill()
{
    if (!CanUseSkill()) return false;
    player->UseEnergy(payCost);
    return ApplySkillEffect();
}

// スキルが使えるか（使えない時はエナジーを使わずに不発）：罠を置くマス・起爆する罠・敵の床・信仰
bool CardExecutor::Play::CanUseSkill() const
{
    // 設置カード：エナジー消費前に対象セルを検証
    if (data.mainEffect.type == CardEffectType::PlaceTrap)
    {
        bool ownCell = (targetCol == playerCol && targetRow == playerRow);   // 足元も置ける
        if (!ownCell && !RangeShape::Contains(playerCol, playerRow, targetCol, targetRow,
            data.rangeType, data.range))
            return false;
        if (targetCol < 0 || targetCol >= gridMap->GetCols() ||
            targetRow < 0 || targetRow >= gridMap->GetRows())
            return false;
        auto& cell = gridMap->GetCell(targetCol, targetRow);
        if ((cell.type != CellType::Empty && !ownCell) || cell.tileEffect.active)
            return false;
    }

    if (data.mainEffect.type == CardEffectType::Detonate)
    {
        bool all = (data.rangeType == RangeType::None);   // 範囲なし＝盤面の全部
        bool any = false;
        for (int r = 0; r < gridMap->GetRows() && !any; r++)
            for (int c = 0; c < gridMap->GetCols() && !any; c++)
                if (gridMap->GetCell(c, r).tileEffect.active && !gridMap->GetCell(c, r).tileEffect.enemyOwned && (all || InRange(c, r)))
                    any = true;
        if (!any) return false;   // 範囲内に罠が無ければ不発（ノーコスト）
    }

    if (data.mainEffect.type == CardEffectType::ClearFloors)   // 範囲に敵の床が無ければ不発（ノーコスト）
    {
        bool all = (data.rangeType == RangeType::None), any = false;
        for (int r = 0; r < gridMap->GetRows() && !any; r++)
            for (int c = 0; c < gridMap->GetCols() && !any; c++)
                if (gridMap->GetCell(c, r).tileEffect.active && gridMap->GetCell(c, r).tileEffect.enemyOwned
                    && (all || InRange(c, r))) any = true;
        if (!any) return false;
    }

    if (data.mainEffect.type == CardEffectType::DetonateAt)
    {
        if (!InRange(targetCol, targetRow))
            return false;
        if (!gridMap->GetCell(targetCol, targetRow).tileEffect.active)
            return false;   // 罠が無いマスは不可（ノーコスト）
    }
    if (data.mainEffect.type == CardEffectType::DetonateChain)
    {
        if (!InRange(targetCol, targetRow)) return false;
        if (!gridMap->GetCell(targetCol, targetRow).tileEffect.active) return false;
    }

    // 聖地型：使えない時は不発（ノーコスト）
    {
        int myFaith = Faith::At(gridMap, playerCol, playerRow);
        CardEffectType mt = data.mainEffect.type;
        if (mt == CardEffectType::Engrave && data.range > 0
            && !(targetCol == playerCol && targetRow == playerRow)   // 足元も刻める
            && (!RangeShape::Contains(playerCol, playerRow, targetCol, targetRow, data.rangeType, data.range)
                || targetCol < 0 || targetRow < 0 || targetCol >= gridMap->GetCols() || targetRow >= gridMap->GetRows()))
            return false;                                                       // 刻むマスが射程外
        if (mt == CardEffectType::Miracle && myFaith <= 0) return false;       // 払う信仰が無い
        if (mt == CardEffectType::Purify && myFaith < data.mainEffect.value) return false;   // 信仰が足りない
        if (mt == CardEffectType::Judgement)
        {
            bool any = false;
            for (auto en : enemies)
                if (en && en->GetHp() > 0)
                    for (auto& c : en->OccupiedCells())
                        if (Faith::At(gridMap, c.first, c.second) > 0) { any = true; break; }
            if (!any) return false;                                             // 聖地に敵がいない
        }
    }
    return true;
}

// スキルの射程（起爆などは足元の罠も含む）
bool CardExecutor::Play::InRange(int c, int r) const
{
    return (CanPickOwnCell(data) && c == playerCol && r == playerRow)
        || RangeShape::Contains(playerCol, playerRow, c, r, data.rangeType, data.range);
}

// スキルの効果（罠は CardExecutorTrap.cpp、信仰は CardExecutorFaith.cpp）
bool CardExecutor::Play::ApplySkillEffect()
{
    switch (data.mainEffect.type)
    {
    case CardEffectType::Block:
    {
        int gained = player->GetBuffManager().GetFinalBlock(data.mainEffect.value);
        player->AddBlock(gained);
        if (gained > 0) EffectManager::Play("block_gain", player->worldX, player->worldY + 0.4f, player->worldZ);
        break;
    }
    case CardEffectType::Draw:
        for (int i = 0; i < data.mainEffect.value; i++)
        {
            std::string id = deck.DrawCard();
            if (!id.empty())
            {
                hand.AddCard(id);
                result.drawnCards.push_back(id);
            }
        }
        break;
    case CardEffectType::Heal:
        player->Heal(data.mainEffect.value);
        break;
    case CardEffectType::AddEnergy:
        player->AddEnergy(data.mainEffect.value);
        break;
    case CardEffectType::ApplyBuff:
        CardEffect::ApplyEffectToPlayer(data.mainEffect, player);
        break;
    case CardEffectType::UpgradeHand:
        hand.UpgradeAll();
        break;
    case CardEffectType::CreateCard:
        for (int i = 0; i < data.mainEffect.value; i++)
        {
            hand.AddCard(data.mainEffect.cardId);
            result.drawnCards.push_back(data.mainEffect.cardId);
        }
        break;
    case CardEffectType::CreateRandom:   // ランダムなカードを value 枚（自分は出さない）。手札へ入れるのはシーン側
        for (int i = 0; i < data.mainEffect.value; i++)
        {
            std::string id = CardDataBase::RandomPoolCard(cardId, data.mainEffect.pool);
            if (!id.empty()) result.addToHand.push_back(id);
        }
        result.addCost = data.mainEffect.costThisTurn;
        break;
    case CardEffectType::Discard:
        pendingDiscard += data.mainEffect.value;
        break;
    case CardEffectType::Incinerate:   // 手札を燃やす：枚数と選び方だけシーンへ渡す
        result.pendingBurn += data.mainEffect.value;
        result.burnRandom = data.mainEffect.random;
        break;
    case CardEffectType::PlaceTrap:         SkillPlaceTrap(); break;
    case CardEffectType::PlaceTrapArea:     SkillPlaceTrapArea(); break;
    case CardEffectType::PlaceDecoy:
    {
        if (!RangeShape::Contains(playerCol, playerRow, targetCol, targetRow,
            data.rangeType, data.range)) return false;
        if (gridMap->GetCell(targetCol, targetRow).type != CellType::Empty) return false;
        result.placeDecoy = true;
        result.decoyCol = targetCol;
        result.decoyRow = targetRow;
        break;
    }
    case CardEffectType::Detonate:          SkillDetonate(); break;
    case CardEffectType::RecallTraps:       SkillRecallTraps(); break;
    case CardEffectType::ClearFloors:       SkillClearFloors(); break;
    case CardEffectType::DetonateAt:        SkillDetonateAt(); break;
    case CardEffectType::DetonateChain:     SkillDetonateChain(); break;
    case CardEffectType::Engrave:           SkillEngrave(); break;
    case CardEffectType::EngraveArea:       SkillEngraveArea(); break;
    case CardEffectType::Judgement:         SkillJudgement(); break;
    case CardEffectType::Miracle:           SkillMiracle(); break;
    case CardEffectType::Purify:            SkillPurify(); break;
    case CardEffectType::Search:
    case CardEffectType::Salvage:
    case CardEffectType::Reclaim:
        result.pendingSelection = data.mainEffect.type;
        break;
    default:
        break;
    }
    return true;
}

// サブ効果（ドロー・回復など）。subEffect と subEffect2 で共通、パワー・自傷でも使う
void CardExecutor::Play::ApplySub(const CardEffectData& sub)
{
    if (!sub.hasEffect) return;
    switch (sub.type)
    {
    case CardEffectType::Draw:
        for (int i = 0; i < sub.value; i++)
        {
            std::string id = deck.DrawCard();
            if (!id.empty())
            {
                hand.AddCard(id);
                result.drawnCards.push_back(id);
            }
        }
        break;
    case CardEffectType::Heal:
        player->Heal(sub.value);
        break;
    case CardEffectType::AddEnergy:
        player->AddEnergy(sub.value);
        break;
    case CardEffectType::ApplyBuff:
        CardEffect::ApplyEffectToPlayer(sub, player);
        break;
    case CardEffectType::Block:
    {
        int gainedS = player->GetBuffManager().GetFinalBlock(sub.value);
        player->AddBlock(gainedS);
        if (gainedS > 0) EffectManager::Play("block_gain", player->worldX, player->worldY + 0.4f, player->worldZ);
        break;
    }
    case CardEffectType::CreateCard:
        for (int i = 0; i < sub.value; i++)
        {
            hand.AddCard(sub.cardId);
            result.drawnCards.push_back(sub.cardId);
        }
        break;
    case CardEffectType::Engrave:   // 自分のマス（移動カードなら移動前のマス）に刻む
        Faith::Engrave(gridMap, playerCol, playerRow, sub.value);
        break;
    case CardEffectType::Discard:
        pendingDiscard += sub.value;
        break;
    case CardEffectType::Incinerate:   // 攻撃の後に手札を燃やす（生贄の刃）
        result.pendingBurn += sub.value;
        result.burnRandom = sub.random;
        break;
    default:
        break;
    }
}
