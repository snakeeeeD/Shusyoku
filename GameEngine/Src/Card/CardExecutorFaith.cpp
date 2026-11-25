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

// 信仰のスキル（刻む・審判・奇跡・浄化）
// （CardExecutor を機能ごとに分けたファイル。Play＝1回のカード使用。CardExecutor.h）

// 刻む：射程0なら自分のマス、射程があれば選んだマス
void CardExecutor::Play::SkillEngrave()
{
    if (data.range > 0) Faith::Engrave(gridMap, targetCol, targetRow, data.mainEffect.value);
    else                Faith::Engrave(gridMap, playerCol, playerRow, data.mainEffect.value);
}

// 範囲（と足元）に刻む
void CardExecutor::Play::SkillEngraveArea()
{
    for (int r = 0; r < gridMap->GetRows(); r++)
        for (int c = 0; c < gridMap->GetCols(); c++)
            if (RangeShape::Contains(playerCol, playerRow, c, r, data.rangeType, data.range)
                || (c == playerCol && r == playerRow))
                Faith::Engrave(gridMap, c, r, data.mainEffect.value);
}

// 審判：聖地に立つ敵すべてに 信仰×value
void CardExecutor::Play::SkillJudgement()
{
    for (auto en : enemies)
    {
        if (!en || en->GetHp() <= 0) continue;
        int best = 0;
        for (auto& c : en->OccupiedCells())
            if (int f = Faith::At(gridMap, c.first, c.second); f > best) best = f;
        if (best <= 0) continue;
        en->TakeDamage(best * data.mainEffect.value);
        EffectManager::Play("area_burst", en->worldX, en->worldY + 0.5f, en->worldZ);
    }
}

// 奇跡：自分のマスの信仰を全て払い、範囲の敵に 払った信仰×value
void CardExecutor::Play::SkillMiracle()
{
    int paid = Faith::At(gridMap, playerCol, playerRow);
    gridMap->GetCell(playerCol, playerRow).faith = 0;
    for (auto en : GetEnemiesInRange(data, playerCol, playerRow, enemies, 0, 0))
    {
        en->TakeDamage(paid * data.mainEffect.value);
        EffectManager::Play("area_burst", en->worldX, en->worldY + 0.5f, en->worldZ);
    }
}

// 浄化：信仰を払って、手札の呪いを廃棄（廃棄そのものはシーン側）
void CardExecutor::Play::SkillPurify()
{
    gridMap->GetCell(playerCol, playerRow).faith -= data.mainEffect.value;
    result.purgeHandCurses = true;
}
