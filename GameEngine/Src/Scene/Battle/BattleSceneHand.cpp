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

// デッキを作り、最初の手札を引く（初戦チュートリアルは 移動・アタック・ブロック で固定）
void BattleScene::InitDeckAndHand()
{
    // PlayerDataManagerからデッキを取得
    auto& playerData = PlayerDataManager::GetData();
    for (auto& cardId : playerData.deck)
        m_deck.AddCard(cardId);
    m_hand.SetDeck(&m_deck);

    m_deck.ShuffleDrawPile();

    // 最初の手札を引く
    m_holdForTutorial = !playerData.tutorialBattle;   // この戦闘で初戦チュートリアルが出る

    if (!playerData.tutorialBattle)
    {
        // 初戦チュートリアル：左から 移動・アタック・ブロック で固定
        for (const char* id : { "MOV_move", "ATK_strike", "SKL_defend" })
        {
            std::string c = m_deck.DrawSpecificCard(id);
            if (!c.empty()) m_hand.AddCard(c);
        }
        while ((int)m_hand.GetCards().size() < HAND_SIZE)
        {
            std::string id = m_deck.DrawCard();
            if (id.empty()) break;
            m_hand.AddCard(id);
        }
    }
    else
    {
        for (int i = 0; i < HAND_SIZE - 1; i++)
        {
            std::string id = m_deck.DrawCard();
            if (!id.empty()) m_hand.AddCard(id);
        }
    }
}

// このターンに引く枚数を決めて、時間差で引く（引く→山札が尽きたらリシャッフル演出→残りを引く）
void BattleScene::DrawTurnHand()
{
    int extraDraw = m_player->GetBuffManager().GetBuffValue(BuffType::DrawPerTurn);
    int keep = m_hand.GetCardCount() - m_enemyAddedToHand;   // 保留で残っている枚数（敵に入れられたお邪魔は数えない）
    if (keep < 0) keep = 0;
    m_enemyAddedToHand = 0;
    int drawN = HAND_SIZE - 1 + extraDraw - keep;
    if (drawN < 0) drawN = 0;
    StartDrawSequence(drawN);
}

// ターンの終わりに手札を片付ける（過負荷の演出・捨てる・呪いの追加・エセリアルの廃棄）。戻り値＝過負荷の演出が全部終わる秒数
float BattleScene::DiscardHandAtTurnEnd()
{
    float seq = 0.0f;
    float burnEnd = 0.0f;
    m_burnClock = 0.0f;
    const float BURN_GAP = 0.5f;   // 連続時は詰めて一連を速く
    for (int i = 0; i < (int)m_hand.GetCards().size(); i++)
    {
        const CardData* dd = CardDataBase::Get(m_hand.GetCards()[i]->GetId());
        if (dd && dd->endTurnDamage > 0)
        {
            if (dd->retain)
                m_battleUI->StartHandCardPeek(i, seq);                     // 保留：中央→手札へ戻る
            else
                m_battleUI->StartPlayCardEffectFromHand(dd, i, seq, true); // 非保留：中央→退場
            m_burnHits.push_back({ seq + 0.36f * 0.75f, dd->endTurnDamage,
               dd->type == CardType::Curse });
            burnEnd = seq + 0.75f;
            seq += BURN_GAP;
        }
    }

    // 残り（過負荷以外）は通常の捨てアニメ（保留はアニメ無し）
    for (int i = 0; i < (int)m_hand.GetCards().size(); i++)
    {
        const CardData* dd = CardDataBase::Get(m_hand.GetCards()[i]->GetId());
        if (dd && dd->endTurnDamage > 0) continue;
        if (dd && dd->retain) continue;
        bool ex = (dd && dd->ethereal);
        m_battleUI->StartDiscardEffectAt(i, dd, seq, ex);   // エセリアル=その場で溶ける
    }
   
    // 呪い・増殖など：手札にあると捨て札にカードを加える（敵がお邪魔カードを足す時と同じ演出で見せる）
    for (int i = 0; i < (int)m_hand.GetCards().size(); i++)
    {
        const CardData* dd = CardDataBase::Get(m_hand.GetCards()[i]->GetId());
        if (!dd || dd->endTurnAddCard.empty()) continue;
        m_deck.DiscardCard(dd->endTurnAddCard);
        m_battleUI->StartAddedCardEffect(CardDataBase::Get(dd->endTurnAddCard), "discard", 0, 1);
    }

    // 非保留カードだけ手札から取り除く（保留はスロット/アニメ状態を維持）
    std::vector<const CardData*> etherealBurned;   // ターン終わりに燃えたエセリアル（全部片付けてから OnCardBurned）
    for (int i = (int)m_hand.GetCards().size() - 1; i >= 0; i--)
    {
        const CardData* dd = CardDataBase::Get(m_hand.GetCards()[i]->GetId());
        if (dd && dd->retain) continue;                               // 保留：残す
        if (dd && dd->ethereal) { m_deck.ExhaustCard(m_hand.GetCards()[i]->GetId()); etherealBurned.push_back(dd); }
        else                    m_deck.DiscardCard(m_hand.GetCards()[i]->GetId());
        m_hand.RemoveCard(i);
        m_battleUI->OnCardRemoved(i);                                 // アニメ側も同indexを除去
    }
    m_selectedCardIndex = -1;   // 手札が消えたので選択も無効化
    for (auto* dd : etherealBurned) OnCardBurned(dd);
    m_hoveredCardIndex = -1;    // ホバーも無効化
    m_cardSelecting = false;
    return burnEnd;
}

// 引く演出：手番中に山札を戻した時の演出と、ターン開始の時間差のドロー（山札が尽きたら捨て札を戻す演出を挟む）
void BattleScene::UpdateDrawSequence(float deltaTime)
{
    if (int n = m_deck.ConsumeReshuffled())   // 手番中のドローで山札を戻した時：戻す演出 → 終わってから引いたカードを出す
    {
        m_battleUI->StartReshuffleEffect(m_deck.GetDrawPileCount() + n);   // 戻した枚数（今の山札＋引いた分）
        m_battleUI->HoldNewestDraws(n, CardVisual::RESHUFFLE_FX_DUR);
        Audio::PlaySE("Assets/Sound/se/card.mp3");
    }
    m_battleUI->UpdateDrawCardEffects(deltaTime);
    // 時間差ドローの進行
    if (m_reshuffleFxTimer > 0.0f)
    {
        m_reshuffleFxTimer -= deltaTime;               // 演出中はドロー停止
    }
    else if (m_drawSeqRemaining > 0)
    {
        m_drawSeqTimer -= deltaTime;
        if (m_drawSeqTimer <= 0.0f)
        {
            if (m_deck.GetDrawPileCount() == 0 && m_deck.GetDiscardPileCount() > 0)
            {
                // 山札が尽きた → 捨て札を山札へ戻す演出を挟む
                int back = m_deck.GetDiscardPileCount();   // 山札へ戻す枚数
                m_deck.Reset();
                m_battleUI->StartReshuffleEffect(back);
                Audio::PlaySE("Assets/Sound/se/card.mp3");
                m_reshuffleFxTimer = CardVisual::RESHUFFLE_FX_DUR;
            }
            else
            {
                std::string id = m_deck.DrawCard();
                if (!id.empty()) { m_hand.AddCard(id); m_battleUI->StartDrawCardEffect(id); }
                m_drawSeqRemaining--;
                m_drawSeqTimer = DRAW_SEQ_INTERVAL;
                if (m_drawSeqRemaining == 0) OnDrawSequenceComplete();
            }
        }
    }
}

// 過負荷（ターンの終わりに手札にあるとダメージ）：演出に合わせて1枚ずつダメージ
void BattleScene::UpdateOverloadHits(float deltaTime)
{
    if (m_burnHits.empty()) return;
    m_burnClock += deltaTime;
    for (auto it = m_burnHits.begin(); it != m_burnHits.end(); )
    {
        if (m_burnClock >= it->atTime)
        {
            m_player->TakeDamage(it->dmg);
            if (it->curse)
                EffectManager::Play("curse_skull",
                    m_player->worldX, m_player->worldY + 0.7f, m_player->worldZ);
            it = m_burnHits.erase(it);
        }
        else ++it;
    }
}

// 手札のアニメ（並び・ホバー・選択）と、捨てる・加わる演出
void BattleScene::UpdateHandAnimations(float deltaTime)
{
    bool selectedNeedsTarget = SelectedNeedsTarget();
    m_battleUI->UpdateCardAnimations(deltaTime, (int)m_hand.GetCards().size(), m_hoveredCardIndex, 
        m_selectedCardIndex, m_input.GetMousePos(), selectedNeedsTarget,
        &m_discardSelected);
    m_battleUI->UpdateDiscardEffects(deltaTime);
    m_battleUI->UpdateAddedCardEffects(deltaTime);
}

// 敵が手札・山札・捨て札に入れるカード（手札は一番左へ、真ん中で見せてから入る）
void BattleScene::InsertEnemyCards(Enemy* enemy)
{
    for (auto& pc : enemy->PendingCurses())
    {
        const CardData* cd = CardDataBase::Get(pc.cardId);
        for (int k = 0; k < pc.count; k++)
        {
            if (pc.target == "hand")   // 手札の一番左へ：真ん中で見せてから、すっと入る（何枚もなら1枚ずつ）
            {
                if (m_hand.AddCardFront(pc.cardId))
                {
                    m_battleUI->OnCardInserted(true, k * 0.25f);
                    m_enemyAddedToHand++;
                    m_hoveredCardIndex = -1;                            // 左に入って番号がずれるので、マウスの所は選び直す
                    if (m_selectedCardIndex >= 0) m_selectedCardIndex++;   // 選んでいたカードは同じカードのまま
                }
            }
            else if (pc.target == "discard")  m_deck.DiscardCard(pc.cardId);
            else                              m_deck.AddCardRandom(pc.cardId);
            if (pc.target != "hand")          // 山札/捨て札は見せてから飛ばす
                m_battleUI->StartAddedCardEffect(cd, pc.target, k, pc.count);
        }
    }
    enemy->PendingCurses().clear();
}
