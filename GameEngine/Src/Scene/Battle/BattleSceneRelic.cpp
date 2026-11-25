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

// レリックの呼び出し口（移動・カード使用・カウント系・使用済みの表示）
// （BattleScene のメンバー。BattleScene.cpp から機能ごとに分けたファイル）

void BattleScene::OnPlayerMoved(int fromCol, int fromRow, int moveDist)
{
    BuffManager& bm = m_player->GetBuffManager();
    m_player->AddMoved(moveDist);                                        // このターンの移動マス数
    m_player->AddBlock(bm.GetBuffValue(BuffType::Momentum) * moveDist);
    if (int charge = bm.GetBuffValue(BuffType::Charge) * moveDist)       // このターンの攻撃力
    {
        Buff b; b.type = BuffType::AttackUpTurn; b.value = charge; b.duration = 1;
        b.name = BuffInfo::Get(BuffType::AttackUpTurn).name; b.description = L"";
        bm.AddBuff(b);
    }
    if (int hit = bm.GetBuffValue(BuffType::HitAndRun))                  // 隣の敵（体も含む）へ
        for (auto e : m_enemies)
        {
            if (e->GetHp() <= 0) continue;
            bool adj = e->OccupiesCell(m_playerCol + 1, m_playerRow) || e->OccupiesCell(m_playerCol - 1, m_playerRow)
                || e->OccupiesCell(m_playerCol, m_playerRow + 1) || e->OccupiesCell(m_playerCol, m_playerRow - 1);
            if (adj) e->TakeDamage(hit);
        }
    m_player->AddBlock(RelicManager::SumValue("moveBlock"));
    Faith::Engrave(m_gridMap, fromCol, fromRow, RelicManager::SumValue("moveEngrave"));   // 巡礼者の笠：元のマスに刻む
    if (m_player->GetBuffManager().HasBuff(BuffType::Frenzy))
        m_player->TakeDamage(1, DamageFeel::Hit);   // 狂乱中の移動は身を削る
    // 今後の「移動時○○」レリックはここに追記
}

// カウント系レリックを1つ進める（kind＝attack/skill/move/card/burn）。しきい値に達したら effect を発動して0に戻す
void BattleScene::TickCountRelics(const char* kind)
{
    auto& pd = PlayerDataManager::GetData();
    for (auto* r : RelicManager::OwnedByKind(kind))
    {
        if (r->count <= 0) continue;
        int& c = pd.relicCounters[r->id];
        if (++c >= r->count)                 // 進んで、しきい値に達したら
        {
            c = 0;                           // 0に戻す
            if (r->effect == "energy") m_player->AddEnergy(r->value);
            else                       m_player->AddBlock(r->value);   // 既定=ブロック
            RelicManager::NotifyFired(r->id);
        }
    }
}

void BattleScene::OnCardPlayed(const CardData* d)
{
    if (!d) return;
    const char* k = (d->type == CardType::Attack) ? "attack" :
        (d->type == CardType::Skill) ? "skill" :
        (d->type == CardType::Move) ? "move" : "";
    if (k[0]) TickCountRelics(k);   // その種別
    TickCountRelics("card");        // 全カード共通
}

bool BattleScene::IsRelicSpent(const std::string& id) const
{
    auto def = RelicManager::Get(id);
    if (!def) return false;
    const std::string& k = def->kind;
    // 戦闘開始で発動する系（最初のターン）＝戦闘中は常に使用済み
    if (k == "startBlock" || k == "startHeal" || k == "startDraw"
        || k == "startBuffAtk" || k == "startBuffAtkTurn" || k == "startPoison" || k == "startCard") return true;
    // 最初の攻撃
    if (k == "firstAttack") return m_firstAttackDone;
    if (k == "burnStatusEnergy") return m_burnStatusEnergyUsed;   // 煤けた手袋：このターンはもう使った
    if (k == "revive")   // 不死鳥の尾羽：1度使ったら暗く
    {
        const auto& rc = PlayerDataManager::GetData().relicCounters;
        auto it = rc.find(id);
        return it != rc.end() && it->second > 0;
    }
    return false;
}
