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

// カードを使う（Execute＝振り分けと、どのファイルからも使う小さな関数）
// 使い方ごとの処理は、1回のカード使用＝Play のメンバー関数で、機能ごとのファイルにある：
//   CardExecutorAttack.cpp  攻撃      CardExecutorMove.cpp   移動     CardExecutorSkill.cpp  スキル（サブ効果も）
//   CardExecutorTrap.cpp    罠        CardExecutorFaith.cpp  信仰     CardExecutorFinish.cpp パワー・後片付け・自傷
//   CardExecutorVfx.cpp     カードの演出                               CardExecutorPush.cpp   ノックバック・引き寄せ

CardExecutor::ExecuteResult CardExecutor::Execute(
    const CardData& data, const std::string& cardId,
    int targetCol, int targetRow,
    Player* player, std::vector<Enemy*>& enemies,
    GridMap* gridMap, int playerCol, int playerRow,
    Hand& hand, int cardIndex, Deck& deck,
    int& outNewPlayerCol, int& outNewPlayerRow,
    const std::vector<std::pair<int, int>>* explicitPath)
{
    outNewPlayerCol = playerCol;
    outNewPlayerRow = playerRow;

    // 1回のカード使用（使い方ごとの処理は Play のメンバー関数＝CardExecutorAttack.cpp など）
    Play p{ data, cardId, targetCol, targetRow, player, enemies, gridMap, playerCol, playerRow,
            hand, cardIndex, deck, outNewPlayerCol, outNewPlayerRow, explicitPath };


    // エネルギーが足りるかを先に確認
    int costOverride = (cardIndex >= 0 && cardIndex < (int)hand.GetCards().size()) ? hand.GetCards()[cardIndex]->CostThisTurn() : -1;
    p.payCost = player->CostOf(data, costOverride);   // この1枚のこのターンのコストがあればそれ
    if (player->GetEnergy() < p.payCost)
        return p.result;

    if (data.unplayable) return p.result;   // 使用不可カードは弾く

    struct SlotGuard {
        Hand& h;
        SlotGuard(Hand& hand) : h(hand) { h.ReserveSlot(true); }
        ~SlotGuard() { h.ReserveSlot(false); }
    } slotGuard(hand);

    bool go = false;   // true＝使えたので後片付けへ（false＝使えなかった、またはその場で使い切った）
    switch (data.type)
    {
    case CardType::Attack: go = p.Attack(); break;
    case CardType::Move:   go = p.Move();   break;
    case CardType::Status:   // 妨害＝スキルと同じ再生処理（効果なし＝廃棄/捨て札へ）
    case CardType::Curse:
    case CardType::Skill:  go = p.Skill();  break;
    case CardType::Power:  go = p.Power();  break;
    }
    if (go)
    {
        p.Finish();
        p.PlayCardVfx();
    }
    return p.result;
}

Enemy* CardExecutor::GetEnemyAt(int col, int row, std::vector<Enemy*>& enemies)
{
    for (auto enemy : enemies)
        if (enemy->OccupiesCell(col, row))                                    // 頭＋胴体
        {
            enemy->MarkHeadHit(enemy->IsWeakPoint(col, row));   // 頭か光るウロコ
            enemy->MarkWeakHit(col, row);
            enemy->MarkHitCell(col, row);
            return enemy;
        }
    return nullptr;
}

std::vector<Enemy*> CardExecutor::GetEnemiesInRange(
    const CardData& data, int playerCol, int playerRow,
    std::vector<Enemy*>& enemies, int aimDx, int aimDy)
{
    std::vector<Enemy*> result;
    auto candidates = BattleHighlighter::GetCandidates(
        playerCol, playerRow, data.rangeType, data.range, aimDx, aimDy);

    for (auto enemy : enemies)
    {
        bool inRange = false, headHit = false;
        for (auto& [cc, cr] : candidates)
        {
            if (enemy->OccupiesCell(cc, cr))
            {
                inRange = true;
                enemy->MarkHitCell(cc, cr);
                if (enemy->IsWeakPoint(cc, cr)) { headHit = true; enemy->MarkWeakHit(cc, cr); }   // 頭か光るウロコを含む
            }
        }
        if (inRange) { enemy->MarkHeadHit(headHit); result.push_back(enemy); }
    }
    return result;
}

int CardExecutor::EffectiveHits(const CardData& data, const Player* player)
{
    int h = data.hits;
    if (data.sanctuaryHits > 0 && Faith::IsSanctuary(player->GetFeetFaith())) h += data.sanctuaryHits;
    if (data.type == CardType::Attack && h > 1)           // ← 複数回攻撃の時だけ
        h += RelicManager::SumValue("multiHit");
    return h;
}

// onHitデバフ付与時のVFX（buffType → effects.jsonのID）
void CardExecutor::PlayOnHitVfx(const CardEffectData& e, Enemy* enemy, GridMap* gridMap)
{
    if (!e.hasEffect || e.buffType.empty() || !enemy) return;

    std::string fx;
    const std::string& b = e.buffType;
    if (b == "Poison")     fx = "poison_apply";
    else if (b == "Weak")       fx = "weaken";
    else if (b == "Vulnerable") fx = "expose";
    else if (b == "Root")       fx = "bind";
    else if (b == "Burn")       fx = "burn";
    else return;   // 対応エフェクトが無い種は出さない

    float wx = (enemy->gridCol - gridMap->GetCols() / 2.0f) * 1.1f;
    float wz = (enemy->gridRow - gridMap->GetRows() / 2.0f) * 1.1f;
    EffectManager::Play(fx, wx, 0.5f, wz);

    if (b == "Poison")
    {
        int shown = e.value + RelicManager::SumValue("poisonAdd");   // 毒の心得ぶんも表示
        FloatingTextManager::Spawn(wx, 0.7f, wz, std::to_wstring(shown),
            BuffInfo::Get(BuffType::Poison).color, 32.0f);
    }
}

// 命中時効果の「刻む」：当たった敵の立っているマスに信仰を刻む
void CardExecutor::ApplyOnHitFaith(const CardEffectData& e, Enemy* enemy, GridMap* gridMap)
{
    if (!e.hasEffect || e.type != CardEffectType::Engrave || !enemy) return;
    Faith::Engrave(gridMap, enemy->gridCol, enemy->gridRow, e.value);
}

void CardExecutor::ApplyAllEnemyEffect(const CardData& data, std::vector<Enemy*>& enemies, GridMap* gridMap)
{
    if (!data.allEnemyEffect.hasEffect || data.allEnemyEffect.buffType.empty()) return;
    BuffType bt = StringToBuffType(data.allEnemyEffect.buffType);
    for (auto e : enemies)
    {
        if (!e || e->GetHp() <= 0) continue;
        Buff b; b.type = bt;
        b.value = data.allEnemyEffect.value;
        b.duration = data.allEnemyEffect.duration;
        b.name = BuffInfo::Get(bt).name; b.description = L"";
        e->GetBuffManager().AddBuff(b);

        float wx = (e->gridCol - gridMap->GetCols() / 2.0f) * 1.1f;
        float wz = (e->gridRow - gridMap->GetRows() / 2.0f) * 1.1f;
        if (bt == BuffType::Poison) EffectManager::Play("poison_apply", wx, 0.5f, wz);
        FloatingTextManager::Spawn(wx, 0.7f, wz, std::to_wstring(b.value),
            BuffInfo::Get(bt).color, 32.0f);
    }
}
