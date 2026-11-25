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

// その場ヒント（初めてその状況になった時だけ出す）
// （BattleScene のメンバー。BattleScene.cpp から機能ごとに分けたファイル）

// その場ヒント（初めてその状況になった時だけ）
void BattleScene::UpdateHints()
{
    if (!m_turnManager.IsEnemyTurn())
    {
        bool threatened = std::any_of(m_enemies.begin(), m_enemies.end(),
            [](const Enemy* e) { return e->hitsPlayer; });
        if (threatened) Hint::Request("threat"); else Hint::Resolve("threat");
        if (m_player->GetEnergy() == 0) Hint::Request("endTurn");
        for (auto e : m_enemies)
            if (e->IsSnake())   // オロチの見どころ（出会った時点で）
            {
                Hint::Request("snakeWeak");
                Hint::Request("molt");
            }
        if (!m_camera.DemoActive() && PlayerDataManager::GetData().tutorialCamera)   // カメラのデモを見た後に（デモの後ろに重ねない）
        { Hint::Request("focus"); Hint::Request("camera"); }
    }
    else Hint::Resolve("endTurn");
}
