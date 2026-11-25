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
