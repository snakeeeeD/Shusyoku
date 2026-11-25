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

// ターンの節目に発動するバフ・レリック（ターン開始／プレイヤーのターンの終わり）
// （BattleScene のメンバー。BattleScene.cpp から機能ごとに分けたファイル）

// ターン開始に発動するバフ・レリック（闘気・巨人の篭手・毒の瘴気・大聖堂）
void BattleScene::ApplyTurnStartPowers()
{
    // 攻撃力成長：闘気（戦士の誓い・成長石）と巨人の篭手（turnBuffAtk）は、どちらも2ターンに1回（偶数ターンの終わり）
    {
        int grow = 0;
        if ((m_turnCount % 2) == 0)
            grow = m_player->GetBuffManager().GetBuffValue(BuffType::AttackGrowth) + RelicManager::SumValue("turnBuffAtk");
        if (grow > 0)
        {
            Buff gb; gb.type = BuffType::AttackUp; gb.value = grow; gb.duration = -1;
            gb.name = L""; gb.description = L"";
            m_player->GetBuffManager().AddBuff(gb);
        }
    }

    // 毒の瘴気：2ターンに1回、全敵に毒を付与
    int nox = ((m_turnCount % 2) == 0)
        ? m_player->GetBuffManager().GetBuffValue(BuffType::NoxiousFumes) : 0;
    if (nox > 0) EffectManager::Play("miasma_screen", 0.0f, 0.0f, 0.0f);   // カードを使ったときと同じ画面の瘴気（画面の演出なので、場所は使わない）
    if (nox > 0)
        for (auto enemy : m_enemies)
        {
            if (enemy->GetHp() <= 0) continue;
            float wx = (enemy->gridCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
            float wz = (enemy->gridRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
            Buff pb; pb.type = BuffType::Poison;
            pb.value = nox + RelicManager::SumValue("poisonAdd");   // ← +毒の心得
            pb.duration = pb.value;
            pb.name = BuffInfo::Get(BuffType::Poison).name; pb.description = L"";
            enemy->GetBuffManager().AddBuff(pb);
            EffectManager::Play("poison_apply", wx, 0.5f, wz);
            FloatingTextManager::Spawn(wx, 0.7f, wz, std::to_wstring(pb.value),
                BuffInfo::Get(BuffType::Poison).color, 32.0f);
        }

    // 大聖堂：ターン開始時、聖地に立っていれば周り8マスに信仰を刻む（自分の足元は増えない）
    if (int cat = m_player->GetBuffManager().GetBuffValue(BuffType::Cathedral);
        cat > 0 && Faith::At(m_gridMap, m_playerCol, m_playerRow) > 0)
        for (int dr = -1; dr <= 1; dr++)
            for (int dc = -1; dc <= 1; dc++)
                if (dc != 0 || dr != 0)
                    Faith::Engrave(m_gridMap, m_playerCol + dc, m_playerRow + dr, cat);
}

// プレイヤーのターンの終わりに発動するもの（守護の誓い・不退転・聖地の焼き）
void BattleScene::ApplyTurnEndPowers()
{
    // 守護の誓い：ターン終了時、聖地に立っていればブロック（敵ターンの攻撃に備える）
    if (int gv = m_player->GetBuffManager().GetBuffValue(BuffType::GuardianVow);
        gv > 0 && Faith::At(m_gridMap, m_playerCol, m_playerRow) > 0)
    {
        m_player->AddBlock(gv);
        EffectManager::Play("block_gain", m_player->worldX, m_player->worldY + 0.4f, m_player->worldZ);
    }

    // 不退転：ターン終了時、隣（上下左右）の敵全員に、残っているブロックの value% のダメージ。
    // 隣にしか効かない＝前に出て受け止める軸の核（遠くでブロックを積んで待つだけでは削れない）
    if (int sf = m_player->GetBuffManager().GetBuffValue(BuffType::StandFirm); sf > 0)
        if (int dmg = m_player->GetBlock() * sf / 100; dmg > 0)
            for (auto enemy : m_enemies)
            {
                if (!enemy || enemy->GetHp() <= 0 || enemy->IsDying()) continue;
                bool adj = false;   // 大きい敵・大蛇の体は、占めているマスのどれかが隣なら
                for (auto& c : enemy->OccupiedCells())
                    if (abs(c.first - m_playerCol) + abs(c.second - m_playerRow) == 1) { adj = true; break; }
                if (!adj) continue;
                enemy->TakeDamage(dmg);
                EffectManager::Play("hit", enemy->worldX, enemy->worldY + 0.6f, enemy->worldZ);
            }

    // 聖地：プレイヤーのターン終了時、聖地に立つ敵は信仰と同じダメージ。聖域でなければそのマスの信仰は半分に
    for (auto enemy : m_enemies)
    {
        if (!enemy || enemy->GetHp() <= 0) continue;
        int best = 0, bc = -1, br = -1;   // 大きな敵は、占めているマスのうち信仰が一番高いマスで判定
        for (auto& c : enemy->OccupiedCells())
            if (int f = Faith::At(m_gridMap, c.first, c.second); f > best) { best = f; bc = c.first; br = c.second; }
        if (best <= 0) continue;
        enemy->TakeDamage(best, DamageFeel::Holy);   // 表示どおりの固定ダメージ
        EffectManager::Play(Faith::IsSanctuary(best) ? "faith_smite_big" : "faith_smite",      // 聖域は大きく・画面を揺らす
            enemy->worldX, enemy->worldY + 0.5f, enemy->worldZ);
        m_gridMap->GetCell(bc, br).faith = best / 2;   // 焼いたら、聖域も含めて信仰は半分に（踏ませると居場所が削れる）
    }
}

// 攻撃を受けた時に返すダメージ（トゲ・受け返し＝ブロックで防いだ量の何%か）
void BattleScene::ApplyCounterDamage(Enemy* enemy, int blocked)
{
    int th = m_player->GetBuffManager().GetBuffValue(BuffType::Thorns);
    if (th > 0 && enemy->GetHp() > 0) enemy->TakeDamage(th);
    int rip = m_player->GetBuffManager().GetBuffValue(BuffType::Riposte);
    if (blocked > 0 && rip > 0 && enemy->GetHp() > 0)   // 受け返し：防いだダメージの rip% を返す
    {
        enemy->TakeDamage(blocked * rip / 100);
        float wx = (enemy->gridCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
        float wz = (enemy->gridRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
        EffectManager::Play("hit", wx, 0.6f, wz);
        ScreenShake::Add(0.2f);
    }
}
