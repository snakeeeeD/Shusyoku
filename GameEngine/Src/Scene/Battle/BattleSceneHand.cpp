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
#include "BuffPopup.h"
#include "PileFx.h"
#include "ScreenShake.h"
#include "HitStop.h"
#include "TurnBanner.h"
#include "DamageFeedback.h"
#include "RangeShape.h"
#include "UiNotice.h"
#include "Cursor.h"
#include "Audio.h"
#include "Hint.h"
#include "Telemetry.h"
#include "EnemyIntentVisual.h"
#include "Faith.h"

#include <algorithm>
#include <random>
#include <cstdio>
#include <queue>
#include <map>
#include <set>

#ifdef _DEBUG
#include "External/imgui/imgui.h"
#endif

// 手札（選んで捨てる・捨てた時の効果・手札に加える・呪いの浄化）
// （BattleScene のメンバー。BattleScene.cpp から機能ごとに分けたファイル）

void BattleScene::OpenDiscardSelect(int n)
{
    // とどめを刺した時は開かない（倒れる演出の後の勝利バナーと被るため）
    if (std::all_of(m_enemies.begin(), m_enemies.end(),
        [](const Enemy* e) { return !e || e->GetHp() <= 0; })) return;
    m_discardSelectCount = min(n, (int)m_hand.GetCards().size());
    m_discardSelected.clear();
    m_discardViewMode = false;
}

void BattleScene::CancelDiscardSelect()
{
    m_discardSelectCount = 0;
    m_discardSelected.clear();
    m_discardViewMode = false;
    m_selectBurn = false;
}

// 手札にカードを加える（山札からではなく、真ん中で見せてから右端へ。状態異常を入れられた時と同じ見せ方）
// costThisTurn>=0 ならこのターンだけそのコスト（灰からの再生など）。delay 秒は隠して待つ。満杯で捨て札へ行ったら nullptr
Card* BattleScene::AddToHand(const std::string& id, int costThisTurn, float delay)
{
    Card* c = m_hand.AddCard(id);
    if (!c) return nullptr;
    m_battleUI->OnCardInserted(false, delay);
    if (costThisTurn >= 0) c->SetCostThisTurn(costThisTurn);
    return c;
}

void BattleScene::ApplyDiscardEffect(const CardEffectData& e)
{
    if (!e.hasEffect) return;
    switch (e.type)
    {
    case CardEffectType::Draw:
        for (int i = 0; i < e.value; i++)
        {
            std::string id = m_deck.DrawCard();
            if (!id.empty()) { m_hand.AddCard(id); m_battleUI->StartDrawCardEffect(id); }
        }
        break;
    case CardEffectType::AddEnergy: m_player->AddEnergy(e.value); break;
    case CardEffectType::ApplyBuff: CardEffect::ApplyEffectToPlayer(e, m_player); break;   // 自分へのバフ（焼き直しの「このターンの攻撃+」）
    case CardEffectType::Block:
        m_player->AddBlock(m_player->GetBuffManager().GetFinalBlock(e.value));
        EffectManager::Play("block_gain", m_player->worldX, m_player->worldY + 0.4f, m_player->worldZ);   // 青い盾の光
        break;
    case CardEffectType::CreateCard: for (int i = 0; i < e.value; i++) { m_hand.AddCard(e.cardId); m_battleUI->StartDrawCardEffect(e.cardId); } break;
    case CardEffectType::ApplyDebuff:   // 捨てたら全敵にデバフ（毒など）
    {
        BuffType bt = StringToBuffType(e.buffType);
        for (auto enemy : m_enemies)
        {
            if (enemy->GetHp() <= 0) continue;
            Buff b; b.type = bt; b.value = e.value; b.duration = e.value;
            b.name = BuffInfo::Get(bt).name; b.description = L"";
            enemy->GetBuffManager().AddBuff(b);
            float wx = (enemy->gridCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
            float wz = (enemy->gridRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
            EffectManager::Play("poison_apply", wx, 0.5f, wz);
        }
        break;
    }
    default: break;
    }
}

void BattleScene::AutoDiscardAll()
{
    std::vector<CardEffectData> effects;
    for (int i = (int)m_hand.GetCards().size() - 1; i >= 0; i--)   // 後ろから消す
    {
        const CardData* dd = CardDataBase::Get(m_hand.GetCards()[i]->GetId());
        if (dd && dd->onDiscardEffect.hasEffect) effects.push_back(dd->onDiscardEffect);
        m_battleUI->StartDiscardEffectAt(i);
        if (dd && dd->endTurnDamage > 0) m_player->TakeDamage(dd->endTurnDamage);
        m_hand.DiscardAt(i);
        m_battleUI->OnCardRemoved(i);
    }
    for (auto& e : effects) ApplyDiscardEffect(e);   // 全部捨ててから発動
}

// 浄化：手札の呪いを全て廃棄する（ターン終了のエセリアルと同じく、その場で溶かす）
void BattleScene::PurgeHandCurses()
{
    std::vector<int> curses;
    for (int i = 0; i < (int)m_hand.GetCards().size(); i++)
    {
        const CardData* dd = m_hand.GetCards()[i]->GetData();
        if (dd && dd->type == CardType::Curse) curses.push_back(i);
    }
    BurnHandCards(curses);   // 燃やす入り口は1か所（燃える演出・燃やした時の効果も同じ）
}
