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

// 敵の攻撃の演出（範囲・口・当たり・引き寄せの線・床を踏んだ時）
// （BattleScene のメンバー。BattleScene.cpp から機能ごとに分けたファイル）

// ダメージの無い、プレイヤーへのデバフだけの行動：範囲のマスに出す「しみこむ」演出（色は画面のふちのデバフと同じ）
static const struct { BuffType type; const char* fx; } DEBUFF_AREA_FX[] = {
    { BuffType::Poison, "debuff_area_poison" },
    { BuffType::Slow, "debuff_area_bind" },       { BuffType::Root, "debuff_area_bind" },
    { BuffType::Vulnerable, "debuff_area_vuln" }, { BuffType::Frail, "debuff_area_vuln" }, { BuffType::DefenseDown, "debuff_area_vuln" },
    { BuffType::Weak, "debuff_area_weak" },       { BuffType::AttackDown, "debuff_area_weak" },
    { BuffType::Burn, "debuff_area_burn" },
};

// 敵の攻撃が当たった範囲の演出（当たり外れに関わらず出す）
//  中心から広がる形（周り・正方形・ひし形）：衝撃波を範囲のマス数ぶん、外へ重ねて出す（敵の識別色）
//  それ以外（十字・直線・突進・市松など）：マスごとに、敵から近い順に時間差で出す
void BattleScene::PlayEnemyAreaFx(Enemy* enemy, const EnemyAction& a, int tC, int tR, int preCol, int preRow,
    const std::vector<std::pair<int, int>>& preCells)
{
    if (a.target.unavoidable) return;
    bool isAtkAction = false;
    const char* debuffFx = nullptr;   // ダメージが無くデバフだけの行動なら、そのデバフの「しみこむ」演出
    for (auto& ef : a.effects)
    {
        if (ef.kind == EffectKind::Damage) isAtkAction = true;
        else if (ef.kind == EffectKind::Debuff && ef.applyTo == ApplyTo::Player && !debuffFx)
            for (auto& d : DEBUFF_AREA_FX)
                if (d.type == StringToBuffType(ef.buff)) { debuffFx = d.fx; break; }
    }
    if (!isAtkAction && debuffFx)   // デバフだけ：範囲のマスごとに、敵から近い順に（輪がしみこみ、粒が降る。粒を上から降らせるので高さ1で出す）
    {
        for (auto& [c, r] : preCells)
            EffectManager::Play(debuffFx, CellWX(c), 1.0f, CellWZ(r), 0.0f, 0.04f * (abs(c - preCol) + abs(r - preRow)));
        return;
    }
    if (!isAtkAction) return;

    bool isDash = (a.target.approach == ApproachType::Dash);
    RangeType rt = a.target.rangeType;
    const int SHOCK_MIN_RANGE = 1;   // この範囲以上の攻撃を衝撃波にする（1＝周り1マスの普通の攻撃も含めて全部）
    int range = a.target.range + enemy->GetBuffManager().GetBuffValue(BuffType::RangeUp);
    bool radial = (rt == RangeType::Area || rt == RangeType::Square || rt == RangeType::Diamond);
    if (radial && range >= SHOCK_MIN_RANGE && !isDash && a.impactVfx.empty())   // 着弾の演出（隕石の爆発など）がある攻撃は、そちらに任せる
    {
        float cx, cz, body;
        AroundCenter(enemy, a, tC, tR, preCol, preRow, cx, cz, body);
        if (!a.aroundVfx.empty())              // なぎ払いなど：衝撃波の代わりに、範囲の外の縁まで届く大きさで1つ（回り始めはランダム）
        {
            EffectManager::PlayAround(a.aroundVfx, cx, 0.5f, cz, range + 0.5f + body, (float)rand() / RAND_MAX * XM_2PI);
            if (a.waveTime > 0.0f && !a.areaVfx.empty())   // 広がる波：範囲のマスが、波の届いた順に（滅びの息吹の炎）
                for (auto& [c, r] : preCells)
                    EffectManager::Play(a.areaVfx, CellWX(c), 0.15f, CellWZ(r), 0.0f,
                        WaveArrival(enemy, a, tC, tR, preCol, preRow, CellWX(c), CellWZ(r)));
            return;
        }
        const float WAVE_GAP = 0.12f;          // 波と波の間の秒数
        for (int k = 1; k <= range; k++)       // 範囲1マスごとに波を1つ。k マス目まで届く大きさで、少しずつ遅らせて外へ重ねる
            EffectManager::PlayAround("enemy_shock", cx, 0.5f, cz, k + 0.5f + body, 0.0f, WAVE_GAP * (k - 1), enemy->hueColor);
        return;
    }

    const std::string afx = a.areaVfx.empty() ? "area_burst" : a.areaVfx;
    // 突進は「実際に走った経路」、それ以外は実行前の攻撃範囲
    std::vector<std::pair<int, int>> cells = isDash ? enemy->GetMovePath() : preCells;
    float step = isDash ? 0.10f : 0.04f;   // 突進はグライドに合わせて遅め
    for (int k = 0; k < (int)cells.size(); k++)
    {
        int c = cells[k].first, r = cells[k].second;
        int d = isDash ? k                                     // 走った順に尾を引く
            : abs(c - preCol) + abs(r - preRow);
        float delay = (a.waveTime > 0.0f && !isDash) ? WaveArrival(enemy, a, tC, tR, preCol, preRow, CellWX(c), CellWZ(r))   // 波：ダメージと同じ時間
                                                      : step * d;
        EffectManager::Play(afx, CellWX(c), 0.15f, CellWZ(r), 0.0f, delay);
    }
}

// 敵の口の位置と向き。位置は絵の口（enemies.json の mouth）、向きは扇なら扇の向き、ほかは口から狙ったマスの方。向きが無ければ false
bool BattleScene::EnemyMouth(Enemy* enemy, const EnemyAction& a, int tC, int tR, XMFLOAT3& pos, float& rot)
{
    pos = enemy->MouthWorld();
    float ux, uz;
    if (a.target.rangeType == RangeType::Cone) { ux = (float)enemy->GetAimDx(); uz = (float)enemy->GetAimDy(); }
    else { ux = CellWX(tC) - pos.x; uz = CellWZ(tR) - pos.z; }
    if (ux * ux + uz * uz < 0.000001f) return false;   // 向きが無い
    rot = atan2f(ux, uz);
    return true;
}

// 敵の口から攻撃の向きへ出す演出（originVfx）
void BattleScene::PlayEnemyOriginFx(Enemy* enemy, const EnemyAction& a, int tC, int tR)
{
    XMFLOAT3 p; float rot;
    if (EnemyMouth(enemy, a, tC, tR, p, rot)) EffectManager::Play(a.originVfx, p.x, p.y, p.z, rot);
}

// 周り全体の攻撃の中心と、体の大きさ（マス数の半分）。範囲の外の縁＝中心から (範囲 + 0.5 + body) マス
void BattleScene::AroundCenter(Enemy* enemy, const EnemyAction& a, int tC, int tR, int preCol, int preRow,
    float& cx, float& cz, float& body)
{
    body = 0.0f;
    if (a.target.anchor != AnchorType::Self)   // 範囲の中心が、狙ったマスや決まったマス
    {
        int ac, ar; enemy->GetAnchorCell(a, tC, tR, ac, ar);
        cx = CellWX(ac); cz = CellWZ(ar);
    }
    else                                       // 範囲の中心が、敵自身（2*2 なら体の中心。体の幅のぶん範囲も広い）
    {
        enemy->CenterWorld(preCol, preRow, m_gridMap, cx, cz);   // 動く前の位置（予告の範囲の中心）
        int lo = 0, hi = 0;
        for (auto& [dc, dr] : enemy->GetGridShape()) { lo = (std::min)(lo, dc); hi = (std::max)(hi, dc); }
        body = (hi - lo) * 0.5f;
    }
}

// 縦・横1列の攻撃の、敵に近い端と遠い端（ワールド。盤の外へ少しはみ出す）。列の攻撃でなければ false
bool BattleScene::LineEnds(Enemy* enemy, const TargetSpec& tg, int tC, int tR, int preCol, int preRow, XMFLOAT3& nearEnd, XMFLOAT3& farEnd)
{
    RangeType rt = tg.rangeType;
    if (rt != RangeType::Column && rt != RangeType::Row) return false;
    int ac, ar; enemy->GetAnchorCell(tg, tC, tR, ac, ar);
    if (tg.anchor == AnchorType::Self) { ac = preCol; ar = preRow; }   // 敵自身の列
    float cx, cz; enemy->CenterWorld(preCol, preRow, m_gridMap, cx, cz);
    const float EDGE_OUT = 0.6f * 1.1f;   // 盤の端から外へ（裂け目を盤の外に開く）。OUT は Windows のマクロなので使わない
    XMFLOAT3 e0, e1;
    if (rt == RangeType::Column)
    {
        e0 = XMFLOAT3(CellWX(ac), 0.5f, CellWZ(0) - EDGE_OUT);
        e1 = XMFLOAT3(CellWX(ac), 0.5f, CellWZ(m_gridMap->GetRows() - 1) + EDGE_OUT);
    }
    else
    {
        e0 = XMFLOAT3(CellWX(0) - EDGE_OUT, 0.5f, CellWZ(ar));
        e1 = XMFLOAT3(CellWX(m_gridMap->GetCols() - 1) + EDGE_OUT, 0.5f, CellWZ(ar));
    }
    float d0 = (e0.x - cx) * (e0.x - cx) + (e0.z - cz) * (e0.z - cz);
    float d1 = (e1.x - cx) * (e1.x - cx) + (e1.z - cz) * (e1.z - cz);
    nearEnd = (d0 <= d1) ? e0 : e1;
    farEnd = (d0 <= d1) ? e1 : e0;
    return true;
}

// 周りに広がる波（aroundVfx）が (x, z) に届くまでの秒数（プレイヤーへのダメージ・マスの炎で共通）。波は waveTime 秒で、範囲の外の縁まで一定の速さで広がる
float BattleScene::WaveArrival(Enemy* enemy, const EnemyAction& a, int tC, int tR, int preCol, int preRow, float x, float z)
{
    // 列の攻撃：(x, z) を通る列ごとに、敵に近い端からの割合×waveTime。いちばん早く届く列の時間（十字の交わる所は早い方）
    float onLine = -1.0f, nearest = -1.0f;
    auto lineT = [&](const TargetSpec& tg) {
        XMFLOAT3 ne, fe;
        if (!LineEnds(enemy, tg, tC, tR, preCol, preRow, ne, fe)) return;
        float lx = fe.x - ne.x, lz = fe.z - ne.z;
        float u = ((x - ne.x) * lx + (z - ne.z) * lz) / (lx * lx + lz * lz);
        float d = a.waveTime * (std::max)(0.0f, (std::min)(1.0f, u));
        float off = (tg.rangeType == RangeType::Column) ? fabsf(x - ne.x) : fabsf(z - ne.z);   // 列からの横のずれ
        if (off < 0.55f && (onLine < 0.0f || d < onLine)) onLine = d;
        if (nearest < 0.0f || d < nearest) nearest = d;
    };
    lineT(a.target);
    for (auto& tg : a.extraTargets) lineT(tg);
    if (nearest >= 0.0f) return (onLine >= 0.0f) ? onLine : nearest;
    float cx, cz, body;
    AroundCenter(enemy, a, tC, tR, preCol, preRow, cx, cz, body);
    int range = a.target.range + enemy->GetBuffManager().GetBuffValue(BuffType::RangeUp);
    float radius = (range + 0.5f + body) * 1.1f;
    float dx = x - cx, dz = z - cz;
    return a.waveTime * (std::min)(1.0f, sqrtf(dx * dx + dz * dz) / radius);
}

// 敵の攻撃が1発当たるたびの演出（hitVfx）。同じ所に重ならないよう、横の位置と傾きを少しずつ変える
void BattleScene::PlayEnemyHitFx(const std::string& id)
{
    auto rnd = []() { return (float)rand() / RAND_MAX * 2.0f - 1.0f; };   // -1〜1
    const float POS_JITTER = 0.15f;   // 横にずらす幅（ワールド）
    const float ROT_JITTER = 0.14f;   // 傾ける幅（ラジアン。約8度。flash の絵は rot の分だけ画面の中で回る）
    EffectManager::Play(id, m_player->worldX + rnd() * POS_JITTER, m_player->worldY + m_player->height * 0.5f,
        m_player->worldZ, rnd() * ROT_JITTER);
}

// 引き寄せの「つなぐ物」：プレイヤーが引っぱられて動いている間は、両端を敵とプレイヤーに合わせる。止まったら敵の方へ戻す
// （踏ん張りで動かなかった時は、すぐ戻る＝耐えたのが見える）
void BattleScene::UpdatePullTether()
{
    if (m_tetherHandle == 0) return;
    if (m_tetherEnemy && m_player->IsMoving())
    {
        EffectManager::SetEnds(m_tetherHandle,
            XMFLOAT3(m_tetherEnemy->worldX, m_tetherEnemy->worldY + m_tetherEnemy->height * 0.5f, m_tetherEnemy->worldZ),
            XMFLOAT3(m_player->worldX, m_player->worldY + 0.5f, m_player->worldZ));   // 着く高さは予備動作と同じ 0.5
        return;
    }
    EffectManager::Stop(m_tetherHandle, 0.15f);
    m_tetherHandle = 0; m_tetherEnemy = nullptr;
}

// 敵の床を踏んだ演出：プレイヤーの見た目が新しいマスの真ん中に着いた時、そのマスの床の vfx を出す
// （歩く・突進・氷で滑る・引き寄せ、のどれでも、足が着いた瞬間に合う。床の効果そのものは TriggerTerrain のまま）
void BattleScene::UpdateFloorStepFx()
{
    if (!m_player || !m_gridMap) return;
    const int cols = m_gridMap->GetCols(), rows = m_gridMap->GetRows();
    const float px = m_player->worldX, pz = m_player->worldZ;
    int c = (int)floorf(px / 1.1f + cols / 2.0f + 0.5f);   // 見た目の位置に一番近いマス
    int r = (int)floorf(pz / 1.1f + rows / 2.0f + 0.5f);
    if (c < 0 || r < 0 || c >= cols || r >= rows || (c == m_floorFxCol && r == m_floorFxRow)) return;
    float cx = (c - cols / 2.0f) * 1.1f, cz = (r - rows / 2.0f) * 1.1f;
    if (fabsf(px - cx) >= 0.25f || fabsf(pz - cz) >= 0.25f) return;   // マスの真ん中まで来たら（すれ違うだけでは出さない）
    m_floorFxCol = c; m_floorFxRow = r;
    const auto& tile = m_gridMap->GetCell(c, r).tileEffect;
    const TerrainDef* def = (tile.active && tile.persistent) ? TerrainDataBase::Get(tile.id) : nullptr;
    if (def && !def->vfx.empty() && !RelicManager::HasKind("floorImmune"))
        EffectManager::Play(def->vfx, cx, 0.05f, cz);
}

// 敵の行動の演出（着弾点・口から・味方へ・攻撃範囲）。当たり外れに関わらず出す
void BattleScene::PlayEnemyActionFx(Enemy* enemy, const EnemyAction* wa, int tC, int tR, int preCol, int preRow,
    const std::vector<std::pair<int, int>>& preCells, int damage)
{
    // 着弾点の演出（避けられても出す）
    if (wa && !wa->impactVfx.empty())
    {
        int ac, ar; enemy->GetAnchorCell(*wa, tC, tR, ac, ar);
        float ix = (ac - m_gridMap->GetCols() / 2.0f) * 1.1f;
        float iz = (ar - m_gridMap->GetRows() / 2.0f) * 1.1f;
        EffectManager::Play(wa->impactVfx, ix, 0.5f, iz);
        ScreenShake::Add(0.35f);
    }

    if (wa && !wa->originVfx.empty()) PlayEnemyOriginFx(enemy, *wa, tC, tR);   // 口から吐く（当たり外れに関係なし）
    if (wa && !wa->allyVfx.empty())   // 味方に向けて飛ばす（楔を強化する）。体の真ん中から体の真ん中へ
        for (auto* o : m_enemies)
            if (o != enemy && o->GetHp() > 0 && !o->IsDying())
                EffectManager::PlayFrom(wa->allyVfx, enemy->SpritePoint(0.0f, 0.5f), o->SpritePoint(0.0f, 0.5f));
    // 攻撃範囲に演出（当たり外れに関わらず出す）。hitVfx がある攻撃は、当たったら命中の演出（1発ごと）に任せる
    if (wa && (!wa->aroundVfx.empty() || !(damage > 0 && !wa->hitVfx.empty()))) PlayEnemyAreaFx(enemy, *wa, tC, tR, preCol, preRow, preCells);
}
