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

// 勝ち負け（不死鳥の尾羽の復活）
// （BattleScene のメンバー。BattleScene.cpp から機能ごとに分けたファイル）

// 不死鳥の尾羽：HPが0になったら1度だけ復活（HP＝廃棄札1枚につき value、最大HPの半分まで。最低1）。使ったかは relicCounters に1（セーブされる）
bool BattleScene::TryPhoenixRevive()
{
    auto& pd = PlayerDataManager::GetData();
    for (auto* r : RelicManager::OwnedByKind("revive"))
    {
        if (pd.relicCounters[r->id] > 0) continue;   // 使用済み
        int hp = std::clamp(m_deck.GetExhaustPileCount() * r->value, 1, (std::max)(1, m_player->GetMaxHp() / 2));
        m_player->SetHp(hp);
        pd.relicCounters[r->id] = 1;
        RelicManager::NotifyFired(r->id);
        EffectManager::Play("nova_fire", m_player->worldX, m_player->worldY + 0.4f, m_player->worldZ);   // 炎が広がって
        EffectManager::Play("heal", m_player->worldX, m_player->worldY + 0.4f, m_player->worldZ);        // 回復
        return true;
    }
    return false;
}

// テレメトリ：戦闘開始（敵の構成・開始HP）
void BattleScene::LogBattleStart()
{
    m_hpStart = m_player ? m_player->GetHp() : 0;
    {
        std::string foes;
        for (auto* e : m_enemies) { if (!foes.empty()) foes += ","; foes += e->GetId(); }
        Telemetry::Instance().Log("battle_start", {
            {"enc",   m_battleEnemyId},
            {"cat",   (int)m_category},
            {"tier",  m_battleTier},
            {"count", (int)m_enemies.size()},
            {"foes",  foes},
            {"hp",    m_hpStart},
            });
    }
}

// 勝敗が決まった後：操作は止めるが、飛んでいる途中のカード演出は最後まで動かす（止めると画面に残る）
void BattleScene::UpdateAfterBattleEnd(float deltaTime)
{
    Hint::Clear();   // 勝利/敗北画面にヒントを残さない
    m_battleUI->UpdateDrawCardEffects(deltaTime);
    m_battleUI->UpdatePlayCardEffects(deltaTime);
    m_battleUI->UpdateDiscardEffects(deltaTime);
    m_battleUI->UpdateAddedCardEffects(deltaTime);
    PileFx::Update(deltaTime);   // 山札・捨て札の光の帯と弾み
}

// 勝ち負けの判定（勝ち→OnVictory／HP0なら不死鳥の尾羽→それでもHP0なら OnDefeat）。決まったら true
bool BattleScene::CheckBattleEnd()
{
    if (m_enemies.empty()) { OnVictory(); return true; }
    if (m_player->GetHp() <= 0) TryPhoenixRevive();   // 不死鳥の尾羽
    if (m_player->GetHp() <= 0) { OnDefeat(); return true; }
    return false;
}

// 勝った：回復・報酬（ボスのレリック・素材・レア・ゴールド・エリート）を決めて保存
void BattleScene::OnVictory()
{
    CancelDiscardSelect();   // 捨てる画面と勝利バナーを重ねない
    m_battleResult = BattleResult::Win;
    LogBattleEnd("win", m_player->GetHp());
    HealOnVictory();
    OfferBossRelics();
    int matDrops = DropMaterials();
    RollRewardRare();

    auto& pd = PlayerDataManager::GetData();
    int nodeIdx = pd.fieldPlayerCol * 7 + pd.fieldPlayerRow;
    if (nodeIdx >= 0 && nodeIdx < (int)pd.fieldNodeVisited.size())
        pd.fieldNodeVisited[nodeIdx] = true;
    PlayerDataManager::AddGold(8 + rand() % 8);
    GiveEliteRewards(matDrops);

    for (auto& dr : m_dropResult)
        Telemetry::Instance().Log("material_drop", {
            {"id", dr.id}, {"n", dr.count}, {"rare", dr.rare}
            });
    PlayerDataManager::Save();
}

// 負けた
void BattleScene::OnDefeat()
{
    CancelDiscardSelect();
    m_battleResult = BattleResult::Lose;
    LogBattleEnd("lose", 0);
}

// テレメトリ：戦闘の終わり（勝ち・負けで共通）
void BattleScene::LogBattleEnd(const char* result, int hpLeft)
{
    Telemetry::Instance().Log("battle_end", {
        {"result", result},
        {"turns",  m_turnCount},
        {"hpStart",m_hpStart},
        {"hpLeft", hpLeft},
        {"enc",    m_battleEnemyId},
        });
}

// 勝った時の回復（勝利回復・止血帯）とHPの保存。ボスを倒したら全回復
void BattleScene::HealOnVictory()
{
    m_player->Heal(RelicManager::SumValue("winHeal"));   // レリック（pd.hpに反映される）
    // 止血帯：この戦闘で自傷で失ったHPの value% を戻す（端数は切り上げ）
    if (int pct = RelicManager::SumValue("selfHealBack"); pct > 0 && m_player->GetSelfLost() > 0)
        m_player->Heal((m_player->GetSelfLost() * pct + 99) / 100);

    // HPを保存、現在のマスをクリア済みに
    auto& pd = PlayerDataManager::GetData();
    pd.hp = m_player->GetHp();

    if (m_category == EncCategory::Boss)   // ボス撃破で全回復
    {
        m_player->Heal(m_player->GetMaxHp());
        pd.hp = m_player->GetHp();
    }
}

// ボスのレリックは3つの候補から選ばせる（次の層のフィールドで選択画面）。候補が無ければランダムに1つ
void BattleScene::OfferBossRelics()
{
    auto& pd = PlayerDataManager::GetData();
    if (m_category == EncCategory::Boss && pd.layer < 3)
    {
        // ボスのレリックは3つの候補から選ばせる（次の層のフィールドで選択画面）。候補が無ければ従来どおりランダム付与
        pd.bossRelicChoices = RelicManager::RandomUnownedMany("boss", 3);
        if (pd.bossRelicChoices.empty())
        {
            std::string br = RelicManager::RandomDrop();
            if (!br.empty()) { PlayerDataManager::AddRelic(br); m_rewardRelic = br; }
        }
    }
}

// 素材のドロップ（初勝利のクラフト導入・エリートのコア・敵ごとの素材）。落とした数（最大3）を返す
int BattleScene::DropMaterials()
{
    auto& pd = PlayerDataManager::GetData();
    // 初勝利：クラフト導入（コア＋素材を確定ドロップ。
    // 勝利画面で行0=コア/行1=素材に固定し、チュートリアルのスポットライトを合わせる）
    int matDrops = 0;   // コア込みのドロップ数（最大3）
    if (!pd.tutorialCraft)
    {
        pd.materials["core_slash"] += 1;      m_dropResult.push_back({ "core_slash", 1, true });  matDrops++;
        pd.materials["sword_fragment"] += 1;  m_dropResult.push_back({ "sword_fragment", 1, false }); matDrops++;
    }

    if (m_category == EncCategory::Elite)   // エリートはコアを1つ必ず落とす（今の層までのコアからランダム）
    {
        std::vector<std::string> cores;
        for (auto& kv : MaterialDataBase::AllBases())
            if (kv.second.layer == 0 || kv.second.layer <= pd.layer) cores.push_back(kv.first);
        if (!cores.empty() && matDrops < 3)
        {
            std::string cid = cores[rand() % cores.size()];
            pd.materials[cid] += 1; m_dropResult.push_back({ cid, 1, true }); matDrops++;
        }
    }

    // 素材ドロップ判定
    for (auto& eid : m_defeatedEnemyIds)
    {
        if (matDrops >= 3) break;                 // 上限
        const EnemyData* ed = EnemyDataBase::Get(eid);
        if (!ed) continue;
        for (auto& d : ed->drops)
        {
            if (matDrops >= 3) break;
            const MaterialDef* md = MaterialDataBase::GetMaterial(d.id);
            if (md && md->layer != 0 && md->layer != PlayerDataManager::GetData().layer) continue;  // 層外素材は落とさない
            if (rand() % 100 < d.chance)
            {
                int n = d.min + (d.max > d.min ? rand() % (d.max - d.min + 1) : 0);
                pd.materials[d.id] += n;
                m_dropResult.push_back({ d.id, n, d.rare });
                matDrops++;                        // 加算
            }
        }
    }
    return matDrops;
}

// カード報酬のレア出現をプール/カテゴリ別に抽選
void BattleScene::RollRewardRare()
{
    auto& pd = PlayerDataManager::GetData();
    {
        int rareChance = 0;
        if (m_category == EncCategory::Elite)
            rareChance = 30;                 // エリート
        else if (m_category == EncCategory::Normal)
            rareChance = (m_battleTier <= 1) ? 0 : (m_battleTier == 2) ? 2 : 5;  // 弱/中/強
        pd.rewardRare = (rand() % 100 < rareChance);
    }
}

// エリートの報酬（ゴールド・レリック・素材の追加）。matDrops＝今までに落とした素材の数（最大3まで）
void BattleScene::GiveEliteRewards(int matDrops)
{
    auto& pd = PlayerDataManager::GetData();
    if (m_category == EncCategory::Elite)
    {
        PlayerDataManager::AddGold(25);
        int em = RelicManager::SumValue("eliteMaterial");
        // エリート報酬：レリック
        std::string rr = RelicManager::RandomDrop();
        if (!rr.empty()) { PlayerDataManager::AddRelic(rr); m_rewardRelic = rr; }
        if (em > 0)
        {
            std::vector<std::string> mids;
            for (auto& kv : MaterialDataBase::AllMaterials()) mids.push_back(kv.first);
            for (int k = 0; k < em && !mids.empty() && matDrops < 3; k++)
            {
                std::string mid = mids[rand() % mids.size()];
                pd.materials[mid] += 1;
                m_dropResult.push_back({ mid, 1, false });
                matDrops++;                        // 加算
            }
        }
    }
}
