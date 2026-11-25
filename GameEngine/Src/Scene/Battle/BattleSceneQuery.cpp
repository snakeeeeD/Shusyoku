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

// 調べるだけの関数（射程・敵の場所・狙っている敵・窓の位置）
// （BattleScene のメンバー。BattleScene.cpp から機能ごとに分けたファイル）

bool BattleScene::CellInCardRange(const CardData& d, int c, int r) const
{
    if (c < 0 || r < 0) return false;
    if (CanPickOwnCell(d) && c == m_playerCol && r == m_playerRow) return true;   // 足元も選べる
    int rng = d.range;
    if (m_player->GetBuffManager().HasBuff(BuffType::Reposition))
        rng += m_player->GetBuffManager().GetBuffValue(BuffType::Reposition);
    int adx = 0, ady = 0;
    if (d.rangeType == RangeType::Cone)   // 扇は指したマスの方向へ向ける
        RangeShape::CardinalAim(m_playerCol, m_playerRow, c, r, adx, ady);
    return RangeShape::Contains(m_playerCol, m_playerRow, c, r, d.rangeType, rng, 0, adx, ady);
}

int BattleScene::EnemyIndexAt(int c, int r) const
{
    if (c < 0 || r < 0) return -1;
    for (int i = 0; i < (int)m_enemies.size(); i++)
        for (auto& cell : m_enemies[i]->OccupiedCells())
            if (cell.first == c && cell.second == r) return i;
    return -1;
}

int BattleScene::AttackTargetEnemy() const
{
    if (m_selectedCardIndex < 0 || m_selectedCardIndex >= (int)m_hand.GetCards().size()) return -1;
    CardData d = *m_hand.GetCards()[m_selectedCardIndex]->GetData();
    ApplyKnifeThrow(d, m_player);
    if (d.type != CardType::Attack) return -1;

    auto inRange = [&](const Enemy* e) {
        for (auto& [c, r] : e->OccupiedCells()) if (CellInCardRange(d, c, r)) return true;
        return false;
        };
    // 射程内の敵が1体だけなら自動ターゲット（使用時と同じ判定）
    int sole = -1, count = 0;
    for (int i = 0; i < (int)m_enemies.size(); i++)
        if (m_enemies[i]->GetHp() > 0 && inRange(m_enemies[i])) { sole = i; count++; }
    bool autoAttack = (count == 1);

    bool hoverInRange = CellInCardRange(d, m_hoveredCell.first, m_hoveredCell.second);
    int hoverEnemy = EnemyIndexAt(m_hoveredCell.first, m_hoveredCell.second);

    // 射程内が1体だけなら、空きマスを指していてもその敵（突進先・扇の向きなど指したマスに意味があるカードは除く）
    if (autoAttack && hoverEnemy < 0 && !AimsFreely(d)) return sole;

    if (m_hoveredCell.first >= 0 && (hoverInRange || !autoAttack))
        return hoverInRange ? hoverEnemy : -1;   // 指したマスの敵（射程外・空きマス＝当たらない）
    return autoAttack ? sole : -1;
}

// 優先：攻撃の対象予測 > パネルのホバー > マウスの下の敵 > クリック選択
int BattleScene::FocusEnemy() const
{
    int t = AttackTargetEnemy();
    if (t >= 0) return t;
    int hi = m_battleUI ? m_battleUI->GetPanelHoveredEnemy() : -1;
    if (hi >= 0 && hi < (int)m_enemies.size()) return hi;
    if (m_selectedCardIndex < 0 && m_hoveredCardIndex < 0)   // カードを触っていない時だけ
        for (int i = 0; i < (int)m_enemies.size(); i++)
            for (auto& cell : m_enemies[i]->OccupiedCells())
                if (cell == m_hoveredCell) return i;
    return m_selectedEnemyRange;
}

bool BattleScene::SelectedNeedsTarget() const
{
    if (m_selectedCardIndex < 0 || m_selectedCardIndex >= (int)m_hand.GetCards().size()) return false;
    const CardData* sd = m_hand.GetCards()[m_selectedCardIndex]->GetData();
    CardType ct = sd->type;
    return ct == CardType::Attack || ct == CardType::Move || PicksCell(*sd);
}

bool BattleScene::GetHitmarkRect(float& x, float& y, float& w, float& h) const
{
    if (m_enemies.empty() || !m_battleUI) return false;
    return m_battleUI->GetHitmarkRect(m_enemies[0], m_renderer3D, x, y, w, h);
}

bool BattleScene::GetEnemyPanelRect(float& x, float& y, float& w, float& h) const
{
    if (m_enemies.empty()) return false;
    float ex, ey, ew, eh, lx, ly;
    BattleUI::GetEnemyEntryRect(0, m_enemies, m_screenWidth, ex, ey, ew, eh);
    BattleUI::GetEnemyEntryRect((int)m_enemies.size() - 1, m_enemies, m_screenWidth, lx, ly, ew, eh);
    x = ex - 6.0f; y = ey - 6.0f;
    w = ew + 12.0f; h = (ly + eh) - ey + 12.0f;   // 先頭〜末尾の敵まで
    return true;
}
