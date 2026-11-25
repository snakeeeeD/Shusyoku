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

// パワーカードと、使った後の後片付け・自傷
// （CardExecutor を機能ごとに分けたファイル。Play＝1回のカード使用。CardExecutor.h）

// パワーカード：自分にかける。サブ効果・全体効果・演出も。捨て札には入れない
bool CardExecutor::Play::Power()
{
    player->UseEnergy(payCost);
    CardEffect::ApplyEffectToPlayer(data.mainEffect, player);
    ApplySub(data.subEffect);    // パワーでもサブ効果（合成の素材など）を発動する
    ApplySub(data.subEffect2);
    // パワーカードは捨て札に入れない
    ApplyAllEnemyEffect(data, enemies, gridMap);
    {   // パワーカードの演出（画面の瘴気など）。自分にかけるカードなので、プレイヤーの位置で出す
        XMFLOAT4 vcol(data.vfxColor[0], data.vfxColor[1], data.vfxColor[2], data.vfxColor[3]);
        float px = (playerCol - gridMap->GetCols() / 2.0f) * 1.1f;
        float pz = (playerRow - gridMap->GetRows() / 2.0f) * 1.1f;
        for (const std::string& fx : data.vfx)
            if (!fx.empty()) EffectManager::Play(fx, px, 0.5f, pz, 0.0f, 0.0f, vcol);
    }
    hand.RemoveCard(cardIndex);
    result.success = true; result.cardUsed = true;
    result.pendingDiscard = pendingDiscard;   // 引いたカード・捨てる枚数もシーンへ渡す
    return false;   // パワーは捨て札に入れないので、ここで終わり
}

// 使った後の後片付け（攻撃・移動・スキル）：捨てる前のドロー・サブ効果・全体効果・廃棄か捨て札か・自傷
void CardExecutor::Play::Finish()
{
    // フラグ付きカード：捨てる前に1ドロー
    if (data.drawBeforeDiscard && data.subEffect.hasEffect
        && data.subEffect.type == CardEffectType::Discard)
    {
        std::string id = deck.DrawCard();
        if (!id.empty()) { hand.AddCard(id); result.drawnCards.push_back(id); }
    }

    // サブ効果の処理
    ApplySub(data.subEffect);
    ApplySub(data.subEffect2);

    ApplyAllEnemyEffect(data, enemies, gridMap);

    if (data.exhaust)
        deck.ExhaustCard(cardId);
    else
        deck.DiscardCard(cardId);

    hand.RemoveCard(cardIndex);
    result.pendingDiscard = pendingDiscard;      // 選択はシーン側に任せる
    result.success = true;

    ApplySelfDamage();
    result.cardUsed = true;
}

// 自傷のカード：HPを失う。狂戦士＝このターンの攻撃+、血の閃き＝カードを引く
void CardExecutor::Play::ApplySelfDamage()
{
    if (data.selfDamage <= 0) return;
    player->LoseHp(data.selfDamage);
    // 自傷したら：狂戦士＝このターンの攻撃+、血の閃き＝カードを引く
    if (int b = player->GetBuffManager().GetBuffValue(BuffType::Berserker); b > 0)
    {
        Buff ab; ab.type = BuffType::AttackUpTurn; ab.value = b; ab.duration = 1; ab.name = L""; ab.description = L"";
        player->GetBuffManager().AddBuff(ab);
    }
    if (int n = player->GetBuffManager().GetBuffValue(BuffType::BloodFlash); n > 0)
    {
        CardEffectData dr; dr.hasEffect = true; dr.type = CardEffectType::Draw; dr.value = n;
        ApplySub(dr);
    }
    float px = (playerCol - gridMap->GetCols() / 2.0f) * 1.1f;
    float pz = (playerRow - gridMap->GetRows() / 2.0f) * 1.1f;
    EffectManager::Play("bleed", px + 0.5, 0.6f, pz + 0.5);
}
