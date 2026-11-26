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

// 廃棄の軸（手札を廃棄する・廃棄した時に発動するもの）

// 手札を燃やす（index は何個でも、順番は問わない）。燃える演出で消して廃棄置き場へ。全部燃やしてから効果を発動（途中で引くと index がずれる）
std::vector<const CardData*> BattleScene::BurnHandCards(std::vector<int> idx)
{
    std::sort(idx.rbegin(), idx.rend());
    std::vector<const CardData*> burned;
    for (int i : idx)
    {
        if (i < 0 || i >= (int)m_hand.GetCards().size()) continue;
        const CardData* dd = m_hand.GetCards()[i]->GetData();
        m_battleUI->StartBurnFromHand(dd, i, (int)burned.size());   // 真ん中へ飛んで燃える（どのカードが燃えたか見せる）
        m_deck.ExhaustCard(m_hand.GetCards()[i]->GetId());
        m_hand.RemoveCard(i);
        m_battleUI->OnCardRemoved(i);
        burned.push_back(dd);
    }
    m_selectedCardIndex = -1;   // 手札が減ったので、選択とホバーは無効に（index のずれ対策）
    m_hoveredCardIndex = -1;
    for (int k = 0; k < (int)burned.size(); k++) OnCardBurned(burned[k], k);   // k＝燃やした順番（火の玉を少しずつずらす）
    return burned;
}

// カードを1枚燃やした時（燃やす経路は全部ここを通る）。燃やされた時の効果と、燃やした数。燃やすと得をするパワー・レリックはここに足す
// 効果を1つ作る（燃やした時のパワー・レリックの「引く」「ブロック」を、捨てた時の効果の処理に渡す）
static CardEffectData MakeEffect(CardEffectType t, int v)
{
    CardEffectData e; e.hasEffect = true; e.type = t; e.value = v;
    return e;
}

void BattleScene::OnCardBurned(const CardData* dd, int slot)
{
    m_burnedThisCombat++;
    m_burnedThisTurn++;
    if (dd && dd->onBurnEffect.hasEffect) ApplyDiscardEffect(dd->onBurnEffect);   // 効果の中身は「捨てた時」と同じ物を使う

    // パワー：炉心（ランダムな敵へ火の玉）・残り火の盾（ブロック）
    auto& bm = m_player->GetBuffManager();
    if (int v = bm.GetBuffValue(BuffType::BurnStrike); v > 0)
        if (Enemy* en = RandomAliveEnemy()) FireBolt(en, v, slot * 0.1f);
    if (int v = bm.GetBuffValue(BuffType::BurnGuard); v > 0) ApplyDiscardEffect(MakeEffect(CardEffectType::Block, v));

    // レリック（光らせるのは、発動したレリックだけ）
    auto fired = [](const char* kind) { for (auto* r : RelicManager::OwnedByKind(kind)) RelicManager::NotifyFired(r->id); };
    if (int v = RelicManager::SumValue("burnBlock"); v > 0)   // 燃え残りの指輪
    {
        ApplyDiscardEffect(MakeEffect(CardEffectType::Block, v)); fired("burnBlock");
    }
    if (m_burnedThisTurn == 1)                                 // 煤けた火打石：このターン初めて燃やした時
        if (int v = RelicManager::SumValue("burnFirstDraw"); v > 0)
        {
            ApplyDiscardEffect(MakeEffect(CardEffectType::Draw, v)); fired("burnFirstDraw");
        }
    if (dd && (dd->type == CardType::Status || dd->type == CardType::Curse) && !m_burnStatusEnergyUsed)   // 煤けた手袋：1ターンに1回
        if (int v = RelicManager::SumValue("burnStatusEnergy"); v > 0)
        {
            m_player->AddEnergy(v); m_burnStatusEnergyUsed = true; fired("burnStatusEnergy");
        }
    TickCountRelics("burn");   // 灰の砂時計（カウント系：count 枚燃やすごとに effect）
}

// 焼き直し：燃やしたカードの種類 → 効果（攻撃＝このターンの攻撃+v／スキル・移動＝ブロックv／パワー＝エナジー+2／状態異常・呪い＝1枚引く）
static CardEffectData BurnTypeEffect(CardType t, int v)
{
    CardEffectData e; e.hasEffect = true;
    switch (t)
    {
    case CardType::Attack: e.type = CardEffectType::ApplyBuff; e.buffType = "AttackUpTurn"; e.value = v; e.duration = 1; break;
    case CardType::Power:  e.type = CardEffectType::AddEnergy; e.value = 2; break;
    case CardType::Status: case CardType::Curse: e.type = CardEffectType::Draw; e.value = 1; break;
    default:               e.type = CardEffectType::Block; e.value = v; break;   // スキル・移動
    }
    return e;
}

// このカードで燃やした1枚ごとの効果（燃やした後）。焚き付け＝引く、焼却＝ブロック、生贄の刃＝コスト×N を狙った敵へ、焼き直し＝種類で分かれる
void BattleScene::ApplyBurnEffect(const CardData* src, const std::vector<const CardData*>& burned, Enemy* target)
{
    if (!src || !src->burnEffect.hasEffect) return;
    const CardEffectData& e = src->burnEffect;
    bool targetAlive = false;   // 選んでいる間に倒れて消えた敵には触らない
    for (auto* en : m_enemies) if (en == target && en->GetHp() > 0) targetAlive = true;
    float delay = 0.0f;
    for (auto* b : burned)
    {
        if (!b) continue;
        if (e.type == CardEffectType::DamageByCost)   // 燃えたカードの火の玉が飛んで、コスト×value の追撃
        {
            int dmg = (std::max)(0, b->cost) * e.value;
            if (!targetAlive || dmg <= 0) continue;
            FireBolt(target, dmg, delay);
            delay += 0.1f;
        }
        else if (e.type == CardEffectType::Transform)    // 変容：ランダムなカード（種類も問わない）に入れ替える（value>0 なら強化済み）
        {
            std::string id = CardDataBase::RandomPoolCard(b->id);
            if (!id.empty() && e.value > 0 && CardDataBase::Get(id + "+")) id += "+";
            if (!id.empty()) AddToHand(id, -1, BattleUI::BurnShowTime() + 0.15f);   // 廃棄したカードが燃え上がってから見せる
        }
        else if (e.type == CardEffectType::ByCardType)   // 燃やしたカードの種類で分かれる
            ApplyDiscardEffect(BurnTypeEffect(b->type, e.value));
        else
            ApplyDiscardEffect(e);   // 引く・ブロック・エナジーなど（「捨てた時」と同じ物を使う）
    }
}

// 燃えたカードの火の玉を敵へ飛ばし、着いたら dmg の追撃（カードが真ん中で燃え上がってから）。生贄の刃・炉心で共通
void BattleScene::FireBolt(Enemy* target, int dmg, float extraDelay)
{
    if (!target || dmg <= 0) return;
    const float FLY = 0.25f;   // 火の玉が着くまで（effects.json の fire_bolt の life と同じ）
    float at = BattleUI::BurnShowTime() + extraDelay;
    EffectManager::PlayFrom("fire_bolt", XMFLOAT3(m_player->worldX, m_player->worldY + m_player->height * 0.5f, m_player->worldZ),
        target->SpritePoint(0.0f, 0.5f), at);
    PlayerHit h{ { target }, dmg };
    if (m_playerHits.Empty()) m_playerHits.Start({ h }, at + FLY); else m_playerHits.Push(h);
}

// 生きている敵からランダムに1体（炉心の的）
Enemy* BattleScene::RandomAliveEnemy()
{
    std::vector<Enemy*> alive;
    for (auto* en : m_enemies) if (en && en->GetHp() > 0) alive.push_back(en);
    if (alive.empty()) return nullptr;
    static std::mt19937 rng(std::random_device{}());
    return alive[std::uniform_int_distribution<int>(0, (int)alive.size() - 1)(rng)];
}

// 手札をランダムに n 枚燃やす
std::vector<const CardData*> BattleScene::BurnRandomCards(int n)
{
    std::vector<int> all;
    for (int i = 0; i < (int)m_hand.GetCards().size(); i++) all.push_back(i);
    std::shuffle(all.begin(), all.end(), std::mt19937(std::random_device{}()));
    if ((int)all.size() > n) all.resize(n);
    return BurnHandCards(all);
}

// 燃やすカードを選ぶ画面（捨てる画面を使い回す）。手札が n 枚以下なら、選ばずに全部燃やす
void BattleScene::OpenBurnSelect(int n)
{
    if ((int)m_hand.GetCards().size() <= n) { ApplyBurnEffect(m_burnSource, BurnRandomCards(n), m_burnTarget); return; }   // 選ぶまでもない時は全部
    OpenDiscardSelect(n);
    m_selectBurn = true;
}
