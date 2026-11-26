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

// ボスの仕掛け（3層ボスの楔・鎖・無敵）

void BattleScene::UpdateBossGimmick()
{
    Enemy* boss = nullptr;
    int wedgesAlive = 0;
    for (auto e : m_enemies)
    {
        if (!e || e->IsDying()) continue;
        const std::string& id = e->GetId();
        if (id.rfind("wedge_", 0) == 0) wedgesAlive++;
        else if (id == "l3_sovereign") boss = e;
    }
    if (!boss) return;   // このバトルに3層ボスがいなければ何もしない

    if (wedgesAlive > 0) m_bossHadWedges = true;

    // 倒れた柱の鎖を外す（破片＋画面の揺れ）
    if (m_bossHadWedges)
        for (auto& c : BOSS_CHAINS)
        {
            bool alive = std::any_of(m_enemies.begin(), m_enemies.end(), [&](Enemy* e) {
                return e && !e->IsDying() && e->GetId() == c.wedge; });
            if (!alive && boss->RemoveOverlay(c.chain))
            {
                EffectManager::Play(c.fx, boss->worldX, 1.4f, boss->worldZ);
                ScreenShake::Add(0.35f);
            }
        }

    if (!m_bossAwakened)
    {
        if (wedgesAlive > 0) m_bossHadWedges = true;

        if (!m_bossAwakened)
        {
            boss->SetInvulnerable(true);   // 覚醒するまでは無敵維持
            if (m_bossHadWedges && wedgesAlive == 0 && m_bossAwakenAtTurn < 0)
                m_bossAwakenAtTurn = m_turnCount + 1;   // 楔全滅→次の敵ターンで覚醒
        }
        else
        {
            boss->SetInvulnerable(true);   // 覚醒前は常に無敵
        }
    }
}

void BattleScene::SwapWedges()
{
    if (m_wedgeCorners.empty()) return;

    // プレイヤーが四隅にいるターンは重なり防止でスキップ
    for (auto& s : m_wedgeCorners)
        if (s.first == m_playerCol && s.second == m_playerRow) return;

    std::vector<Enemy*> wedges;
    for (auto e : m_enemies)
        if (e && !e->IsDying() && e->GetHp() > 0 && e->GetId().rfind("wedge_", 0) == 0)
            wedges.push_back(e);
    int n = (int)wedges.size();
    if (n < 2) return;   // 1体以下は交換なし

    // 使う四隅をシャッフルして先頭n個（4=四隅/3=三角/2=ランダム2）
    std::vector<std::pair<int, int>> slots = m_wedgeCorners;
    for (int i = (int)slots.size() - 1; i > 0; i--) std::swap(slots[i], slots[rand() % (i + 1)]);
    if ((int)slots.size() > n) slots.resize(n);

    // 楔の並びもシャッフル（位置が入れ替わる）
    for (int i = n - 1; i > 0; i--) std::swap(wedges[i], wedges[rand() % (i + 1)]);

    // 全楔のセルをクリア → 新座標へ一括配置（同時交換の衝突回避）
    for (auto w : wedges)
        for (auto& [dc, dr] : w->GetGridShape())
        {
            int c = w->gridCol + dc, r = w->gridRow + dr;
            if (c >= 0 && c < m_gridMap->GetCols() && r >= 0 && r < m_gridMap->GetRows())
                m_gridMap->SetCellType(c, r, CellType::Empty);
        }

    for (int i = 0; i < n; i++)
    {
        Enemy* w = wedges[i];
        int nc = slots[i].first, nr = slots[i].second;
        w->gridCol = nc; w->gridRow = nr;
        for (auto& [dc, dr] : w->GetGridShape())
        {
            int c = nc + dc, r = nr + dr;
            if (c >= 0 && c < m_gridMap->GetCols() && r >= 0 && r < m_gridMap->GetRows())
                m_gridMap->SetCellType(c, r, CellType::Enemy);
        }
        float wx = (nc - m_gridMap->GetCols() / 2.0f) * 1.1f;
        float wz = (nr - m_gridMap->GetRows() / 2.0f) * 1.1f;
        w->StartMove(wx, wz, 0.35f);   // グライドで移動
    }
}

void BattleScene::BossInvulnAction(Enemy* boss)
{
    if (m_turnCount % 2 != 0)
        return;   // 溜めターン（何もしない）

    if (((m_turnCount / 2) % 2) == 0)
    {
        // 楔の攻撃力を永続アップ
        for (auto e : m_enemies)
        {
            if (!e || e->IsDying()) continue;
            if (e->GetId().rfind("wedge_", 0) == 0)
                e->GetBuffManager().AddBuff({ BuffType::AttackUp, 2, -1 });   // +2・永続
        }
        EffectManager::Play("explosion_all",
            boss->worldX, boss->worldY + boss->height * 0.5f, boss->worldZ);
    }
    else
    {
        // お邪魔カードを山札に挿入
        const char* junk = "status_static";
        m_deck.AddCard(junk);
        m_battleUI->StartAddedCardEffect(CardDataBase::Get(junk), "deck", 0, 1);
    }
}

// 覚醒ターンのボスは、予告を「覚醒」に差し替える
void BattleScene::MarkBossAwakenIntent()
{
    if (m_bossAwakenAtTurn >= 0 && m_turnCount >= m_bossAwakenAtTurn && !m_bossAwakened)
        for (auto enemy : m_enemies)
            if (enemy->GetId() == "l3_sovereign") enemy->SetAwakenIntent();
}

// 3層ボスの覚醒演出：演出だけ更新する（操作・ターン遷移・時間経過は止める）。演出中なら true（Update はそこで終わる）
bool BattleScene::UpdateAwakenCinematic(float deltaTime)
{
    if (m_awakenCinematic <= 0.0f) return false;
    m_awakenCinematic -= deltaTime;

    // 画面全体の円を一定間隔で追加し、経過時間を進める
    m_awakenRingTimer -= deltaTime;
    if (m_awakenRingTimer <= 0.0f) { m_awakenRings.push_back(0.0f); m_awakenRingTimer = 0.32f; }
    for (auto& a : m_awakenRings) a += deltaTime;
    m_awakenRings.erase(
        std::remove_if(m_awakenRings.begin(), m_awakenRings.end(),
            [](float a) { return a > 1.1f; }),   // 寿命1.1秒
        m_awakenRings.end());

    ScreenShake::Add(0.6f);              // ずっと揺らす（グワングワン）

    // 演出だけ更新（操作・ターン遷移・時間経過は止める）
    ScreenShake::Update(deltaTime);
    EffectManager::Update(deltaTime);
    FloatingTextManager::Update(deltaTime);
    BuffPopupManager::Update(deltaTime);
    // 演出中：ボス付近にズームイン（揺れも乗せる。通常のカメラ更新はこの下でreturnされて通らない）
    if (m_awakenBoss)   // ズーム0.4（小さいほど寄る）
        BattleCamera::ApplyAt(m_renderer3D, m_awakenBoss->worldX, m_awakenBoss->worldZ - 1.5f, 0.4f);
    return true;
}

// オロチの脱皮：ターンは使わず、その瞬間に演出だけ出す
void BattleScene::UpdateSnakeMolt()
{
    for (auto enemy : m_enemies)
        if (enemy->IsSnake() && enemy->ConsumeShed())
        {
            auto [hc, hr] = enemy->HitCell();                              // 殴った場所に出す
            float wx = (hc - m_gridMap->GetCols() / 2.0f) * 1.1f;
            float wz = (hr - m_gridMap->GetRows() / 2.0f) * 1.1f;
            EffectManager::Play("chain_break_venom", wx, 0.6f, wz);        // 緑の破片
            ScreenShake::Add(0.5f);
            FloatingTextManager::Spawn(wx, 1.0f, wz, L"脱皮！ 巻きつき・攻撃力アップ",
                XMFLOAT4(0.5f, 1.0f, 0.4f, 1.0f), 40.0f);
        }
}

// 3層ボスの覚醒ターンか（この行動の代わりに覚醒する）
bool BattleScene::IsBossAwakenTurn(const Enemy* enemy) const
{
    return enemy->GetId() == "l3_sovereign" && !m_bossAwakened && m_bossAwakenAtTurn >= 0 && m_turnCount >= m_bossAwakenAtTurn;
}

// 覚醒ターン：何もせず演出＋無敵解除
void BattleScene::BeginBossAwaken(Enemy* enemy)
{
    m_bossAwakened = true;
    m_bossAwakenAtTurn = -1;
    enemy->SetInvulnerable(false);
    enemy->SetTexture("darkload_awaken");
    m_awakenCinematic = 2.5f;
    m_awakenRingTimer = 0.0f;
    m_awakenRings.clear();
    m_awakenBoss = enemy;
    ScreenShake::Add(1.0f);
}
