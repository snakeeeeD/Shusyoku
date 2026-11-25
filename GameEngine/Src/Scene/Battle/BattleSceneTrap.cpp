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

// 罠（ターン開始で育つ・導火線）
// （BattleScene のメンバー。BattleScene.cpp から機能ごとに分けたファイル）

// 自分の罠：自分のターンの始め（＝敵のターンが終わった所）に育つ（上限3回）。
// 周り1マスに敵がいる間だけ導火線が進み、3たまった次のターン、まだ敵が隣にいたら自分で起爆する（周りは半分）。敵がいない罠はそのまま残る（回収できる）
static constexpr int TRAP_GROW_MAX = 3, TRAP_FUSE = 3;

void BattleScene::UpdateTrapsAtTurnStart()
{
    int maxFuse = 0;   // このターンに進んだ導火線の、一番大きい数（「ピッ」の回数を決める）
    for (int r = 0; r < m_gridMap->GetRows(); r++)
        for (int c = 0; c < m_gridMap->GetCols(); c++)
        {
            auto& t = m_gridMap->GetCell(c, r).tileEffect;
            if (!t.active || t.enemyOwned || t.persistent) continue;   // 自分の罠だけ（床・ハザードは除く）
            const TerrainDef* def = TerrainDataBase::Get(t.id);
            if (!def || def->growPct <= 0) continue;
            if (t.grown < TRAP_GROW_MAX) { t.value += (std::max)(1, (t.value * def->growPct + 50) / 100); t.grown++; }
            bool nearEnemy = false;   // 周り1マスに敵
            for (int dr = -1; dr <= 1 && !nearEnemy; dr++)
                for (int dc = -1; dc <= 1 && !nearEnemy; dc++)
                {
                    int nc = c + dc, nr = r + dr;
                    if (nc < 0 || nc >= m_gridMap->GetCols() || nr < 0 || nr >= m_gridMap->GetRows()) continue;
                    int ei = EnemyIndexAt(nc, nr);   // 大きい敵・蛇の体も含む
                    nearEnemy = (ei >= 0 && m_enemies[ei]->GetHp() > 0);
                }
            if (nearEnemy)
            {
                if (t.fuse < TRAP_FUSE)
                {
                    t.fuse++;   // 導火線が進む（3になってもすぐには爆発しない＝3の間に敵に踏ませることもできる）
                    maxFuse = (std::max)(maxFuse, t.fuse);
                    // 進んだのが分かるように、罠の上に数を出す（3は「次のターンに爆発」）。初めての時はヒントで仕組みを説明
                    bool last = (t.fuse >= TRAP_FUSE);
                    FloatingTextManager::Spawn(CellWX(c), 0.6f, CellWZ(r),
                        last ? std::wstring(L"次のターンに爆発！") : L"導火線 " + std::to_wstring(t.fuse) + L"/3",
                        last ? XMFLOAT4(1.0f, 0.3f, 0.2f, 1.0f) : XMFLOAT4(1.0f, 0.6f, 0.2f, 1.0f), 24.0f);
                    Hint::Request("fuse");
                }
                else if (!m_detonate.Any([&](const TrapFire& f) { return f.c == c && f.r == r; }))
                    m_detonate.Push({ c, r, false });   // 3たまった次のターン、まだ敵が隣にいたら自分で起爆（起爆カードと同じ順番待ち。周りは半分）
            }
        }
    if (!m_detonate.Empty()) m_detonate.Delay(0.3f);   // 少し間をおいて

    // 導火線が進んだら、点滅に合わせて「ピッ」を数回だけ（低HPの鼓動と同じく、鳴り続けて考える邪魔をしないように）
    if (maxFuse > 0)
    {
        static const int BEEPS[TRAP_FUSE + 1] = { 0, 2, 3, 4 };   // 導火線1は点滅が遅いので2回、2は3回、3は4回
        m_fuseBeep.period = 1.0f / (float)(1 << (maxFuse - 1));   // 点滅と同じ間隔（BattleSceneDraw）
        m_fuseBeep.left = BEEPS[maxFuse];
        m_fuseBeep.next = ceilf(m_highlightTimer / m_fuseBeep.period) * m_fuseBeep.period;   // 次に光る瞬間にそろえる
    }
}

// 導火線の「ピッ」：光る瞬間に1回ずつ
void BattleScene::UpdateFuseBeep()
{
    if (m_fuseBeep.left <= 0 || m_highlightTimer < m_fuseBeep.next) return;
    Se::Play(Se::FuseBeep, 0.03f, 0.7f);
    m_fuseBeep.left--;
    m_fuseBeep.next += m_fuseBeep.period;
}

// 敵が通過した各マスの罠（大蛇のとぐろは順番待ちへ）
void BattleScene::TriggerTrapsOnPath(Enemy* enemy)
{
    if (enemy->IsSnake())
    {
        // とぐろ：経路の罠を段階発火キューへ（実際の発火はUpdateのシーケンサ）
        for (auto& mp : enemy->GetMovePath())
            if (m_gridMap->GetCell(mp.first, mp.second).tileEffect.active)
                m_coilTraps.Push({ mp.first, mp.second, enemy });
        if (!m_coilTraps.Empty()) m_coilTraps.Delay(0.0f);   // すぐ1つ目
    }
    else
    {
        for (auto& mp : enemy->GetMovePath())   // 通ったマスごとに、体の下の罠を全部踏む
        {
            TriggerTrapsUnder(enemy, mp.first, mp.second);
            if (enemy->GetHp() <= 0) break;
        }
        if (enemy->GetHp() > 0)
            TriggerTrapsUnder(enemy, enemy->gridCol, enemy->gridRow);
    }
}

// 敵が (col,row) に立った時、体の下の罠を全部踏む（大きい敵は形の全マス）
void BattleScene::TriggerTrapsUnder(Enemy* e, int col, int row)
{
    GridMap* g = m_gridMap;
    std::vector<Enemy*>& enemies = m_enemies;
    for (auto& [dc, dr] : e->GetGridShape())
    {
        int c = col + dc, r = row + dr;
        if (c < 0 || r < 0 || c >= g->GetCols() || r >= g->GetRows()) continue;
        auto& cell = g->GetCell(c, r);
        if (cell.tileEffect.active) CardExecutor::TriggerTrap(cell, e, c, r, g, enemies);
        if (e->GetHp() <= 0) return;
    }
}
