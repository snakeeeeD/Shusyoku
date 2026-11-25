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

// 盤面の空気とバフの演出（バフが付いた時・足元の信仰）
// （BattleScene のメンバー。BattleScene.cpp から機能ごとに分けたファイル）

// バフ・デバフが付いた／増えた時の演出。増やす時は1行。ここに無いもの（表示用など）は何も出さない
//   fx     ＝体の所の演出（プレイヤーも敵も。nullptr なら無し）
//   screen ＝画面のふちのオーラ（プレイヤーだけ。バフは左右から立ちのぼる・デバフは上からアーチ状に落ちる）
//   popup  ＝頭の上にアイコンを出すか（聖地のように、歩くたびに付いたり消えたりするものは false）
static const struct { BuffType type; const char* fx; const char* screen; bool popup = true; } BUFF_GAIN_FX[] = {
    // 攻撃が上がる（ふち：赤）
    { BuffType::AttackUp, "buff_attack", "aura_buff_attack" },     { BuffType::AttackUpTurn, "buff_attack", "aura_buff_attack" },
    { BuffType::AttackGrowth, "buff_attack", "aura_buff_attack" }, { BuffType::Frenzy, "buff_attack", "aura_buff_attack" },
    { BuffType::LastStand, "buff_attack", "aura_buff_attack" },    { BuffType::DeepStand, "buff_attack", "aura_buff_attack" },
    { BuffType::KnifePower, "buff_attack", "aura_buff_attack" },   { BuffType::HolyGround, "buff_attack", nullptr, false },
    { BuffType::Charge, "buff_attack", "aura_buff_attack" },       { BuffType::TrapPower, "buff_attack", "aura_buff_attack" },
    { BuffType::Berserker, "buff_attack", "aura_buff_attack" },
    // 守りが上がる（ふち：青）
    { BuffType::DefenseUp, "buff_defense", "aura_buff_defense" },  { BuffType::Barricade, "buff_defense", "aura_buff_defense" },
    { BuffType::Thorns, "buff_defense", "aura_buff_defense" },     { BuffType::Momentum, "buff_defense", "aura_buff_defense" },
    { BuffType::Riposte, "buff_defense", "aura_buff_defense" },    { BuffType::CardBlock, "buff_defense", "aura_buff_defense" },
    { BuffType::Sanctuary, "buff_defense", "aura_buff_defense" },  { BuffType::GuardianVow, "buff_defense", "aura_buff_defense" },
    { BuffType::Invincible, "buff_defense", "aura_buff_defense" }, { BuffType::HalfCarry, "buff_defense", "aura_buff_defense" },
    { BuffType::StandFirm, "buff_defense", "aura_buff_defense" },  { BuffType::BurnGuard, "buff_defense", "aura_buff_defense" },
    // そのほかの強化（ふち：金）
    { BuffType::RangeUp, "buff_up", "aura_buff_up" },      { BuffType::MoveUp, "buff_up", "aura_buff_up" },
    { BuffType::HitAndRun, "buff_up", "aura_buff_up" },    { BuffType::Reposition, "buff_up", "aura_buff_up" },
    { BuffType::NoxiousFumes, "buff_up", "aura_buff_up" }, { BuffType::ToxicRhythm, "buff_up", "aura_buff_up" },
    { BuffType::KnifeThrow, "buff_up", "aura_buff_up" },   { BuffType::KnifeGen, "buff_up", "aura_buff_up" },
    { BuffType::EmberTrail, "buff_up", "aura_buff_up" },   { BuffType::DrawPerTurn, "buff_up", "aura_buff_up" },
    { BuffType::Cathedral, "buff_up", "aura_buff_up" },    { BuffType::BloodThirst, "buff_up", "aura_buff_up" },
    { BuffType::BloodFlash, "buff_up", "aura_buff_up" },   { BuffType::BurnStrike, "buff_up", "aura_buff_up" },
    // デバフ（体の所の演出は、付けた側のカード・攻撃が出しているので無し）
    { BuffType::Weak, nullptr, "aura_debuff_weak" },       { BuffType::AttackDown, nullptr, "aura_debuff_weak" },
    { BuffType::Vulnerable, nullptr, "aura_debuff_vuln" }, { BuffType::Frail, nullptr, "aura_debuff_vuln" },
    { BuffType::DefenseDown, nullptr, "aura_debuff_vuln" },
    { BuffType::Root, nullptr, "aura_debuff_bind" },       { BuffType::Slow, nullptr, "aura_debuff_bind" },
    { BuffType::Poison, nullptr, "aura_debuff_poison" },   { BuffType::Burn, nullptr, "aura_debuff_burn" },
};

// 毎フレーム、前のフレームのバフの値と比べて、付いた／増えたものにエフェクト（どこで付いても拾える）
void BattleScene::UpdateBuffGainFx()
{
    std::unordered_map<const void*, std::map<int, BuffSnap>> next;
    // y＝体の真ん中（体の所の演出）、headY＝頭の上（アイコン）。画面のふちはプレイヤーだけ
    auto check = [&](const void* key, const BuffManager& bm, float x, float y, float z, float headY, bool isPlayer) {
        std::map<int, BuffSnap> cur;
        for (auto& b : bm.GetBuffs())
        {
            BuffSnap& s = cur[(int)b.type];
            s.value += (b.value > 0) ? b.value : 1;           // 値の無いバフも「付いた」で拾う
            if (!b.isPermanent()) s.turns += b.duration;      // 重ねがけで残りターンだけ延びた時も拾う
        }
        auto get = [](const std::map<int, BuffSnap>& m, BuffType t) { auto f = m.find((int)t); return f == m.end() ? BuffSnap{} : f->second; };
        auto it = m_buffSnap.find(key);
        if (it != m_buffSnap.end())   // 初めて見た時（戦闘開始・召喚直後）は出さない
        {
            std::vector<const char*> played;   // 同じフレームに同じ演出は1回
            auto playOnce = [&](const char* id, float px, float py, float pz) {
                if (!id || std::find(played.begin(), played.end(), id) != played.end()) return;
                EffectManager::Play(id, px, py, pz);
                played.push_back(id);
            };
            for (auto& e : BUFF_GAIN_FX)
            {
                BuffSnap now = get(cur, e.type), before = get(it->second, e.type);
                bool more = now.value > before.value;         // 値が増えた（毒・攻撃UPなど）
                bool longer = now.turns > before.turns;       // 残りターンが延びた（脱力・弱体の重ねがけなど）
                if (!more && !longer) continue;
                playOnce(e.fx, x, y, z);
                if (isPlayer) playOnce(e.screen, 0.0f, 0.0f, 0.0f);   // 画面の演出なので、場所は使わない
                if (e.popup)   // 頭の上にアイコン。値のあるもの（毒・攻撃UPなど）は増えた数も
                    BuffPopupManager::Spawn(e.type, (more && bm.GetBuffValue(e.type) > 0) ? now.value - before.value : 0, x, headY, z);
            }
        }
        next[key] = std::move(cur);
    };
    check(m_player, m_player->GetBuffManager(), m_player->worldX, m_player->worldY + 0.5f, m_player->worldZ,
        m_player->worldY + m_player->height + 0.15f, true);
    for (auto e : m_enemies)
        if (e && e->GetHp() > 0)
            check(e, e->GetBuffManager(), e->worldX, e->worldY + e->height * 0.5f, e->worldZ,
                e->worldY + e->height + 0.15f, false);
    m_buffSnap.swap(next);   // いなくなった敵の分は自然に消える
}

// 立っているマスの信仰を、プレイヤーのバフとして反映する（毎フレーム）
// バフにしておくと、攻撃の加算がカードの数値表示（強化で緑）とバフ一覧にそのまま出る
void BattleScene::SyncFaithBuffs()
{
    if (!m_player) return;
    m_player->SetExhaustCount(m_deck.GetExhaustPileCount());   // 灰燼撃のダメージ（表示と実際）
    auto& bm = m_player->GetBuffManager();
    bm.RemoveBuff(BuffType::HolyGround);
    bm.RemoveBuff(BuffType::Sanctuary);

    int f = Faith::At(m_gridMap, m_playerCol, m_playerRow);
    m_player->SetFeetFaith(f);
    auto add = [&](BuffType t, int v) {
        Buff b; b.type = t; b.value = v; b.duration = -1;   // 永続（離れたら外す）
        b.name = BuffInfo::Get(t).name; b.description = L"";
        bm.AddBuff(b);
        };
    if (Faith::AttackBonus(f) > 0) add(BuffType::HolyGround, Faith::AttackBonus(f));
    if (Faith::IsSanctuary(f))     add(BuffType::Sanctuary, 1);

    bm.RemoveBuff(BuffType::Mire);                                  // 沼は上にいる間だけ
    const auto& tile = m_gridMap->GetCell(m_playerCol, m_playerRow).tileEffect;
    const TerrainDef* def = tile.active ? TerrainDataBase::Get(tile.id) : nullptr;
    if (def && def->effect == "WhileOn" && !RelicManager::HasKind("floorImmune"))
        add(StringToBuffType(def->buffType), tile.value);
}
