#include "BattleUI.h"
#include "BuffInfo.h"
#include "BuffPopup.h"
#include "Faith.h"
#include "CardExecutor.h"
#include "CardVisual.h"
#include "CardTooltip.h"
#include "GlossaryLink.h"
#include "EnemyIntentVisual.h"
#include "TerrainDataBase.h"
#include "GameUtils.h"
#include "RangeShape.h"
#include "UiNotice.h"
#include "TurnBanner.h"
#include "MaterialDataBase.h"
#include "HighlightPalette.h"
#include "RelicManager.h"
#include "UiWindow.h"
#include "Audio.h"
#include "AnimTune.h"
#include "URect.h"
#include "PileFx.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

// 戦闘のUI（初期化と、描く順番。中身は機能ごとのファイル：BattleUITarget / UnitStatus / Hud / Hand / Pile / EnemyPanel
//  / Tooltip / Result / DrawFx / CardFx .cpp。どれも同じ BattleUI クラスのメンバー）

AnimTuneData g_animTune;

BattleUI::~BattleUI()
{
    delete m_spriteRenderer;
    delete m_textRenderer;
}

bool BattleUI::Init(ID3D11Device* device, ID3D11DeviceContext* context,
    int screenWidth, int screenHeight, IDXGISwapChain* swapChain)
{
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    m_spriteRenderer = new SpriteRenderer();
    if (!m_spriteRenderer->Init(device, context, screenWidth, screenHeight))
        return false;

    m_textRenderer = new TextRenderer();
    if (!m_textRenderer->Init(device, context, swapChain))
        return false;
    m_cardBurn.Init(device, context);

    m_whiteTexture = TextureManager::Get("white");
    return true;
}

// 戦闘UIを描く（描く順番だけ。中身は機能ごとのファイル）
void BattleUI::Draw(const BattleUIContext& ctx)
{
    m_hasHoveredBuff = false;
    m_overUnitStatus = false;

    m_spriteRenderer->Begin();
    DrawTargetIndicators(ctx);          // 狙いの矢印・突進／貫通の予告（BattleUITarget.cpp）
    DrawEnemyMoveArrows(ctx);           // 敵の移動予定
    DrawPlayerOffScreenIndicator(ctx);
    m_spriteRenderer->End();
    m_textRenderer->Begin();

    DrawUnitStatuses(ctx);              // 頭の上（BattleUIUnitStatus.cpp）
    DrawPilesAndHud(ctx);               // 左下の自分の状態・山札類（BattleUIHud.cpp）
    DrawHandArea(ctx);                  // 手札まわり（BattleUIHand.cpp）
    DrawPlayerBuffList(ctx);            // バフの一覧（BattleUIHud.cpp）
    DrawTurnEndButton(ctx);             // ターン終了ボタン（BattleUIHud.cpp）
    if (ctx.battleResult == BattleResult::Win) DrawVictory(ctx);   // 勝利画面（BattleUIResult.cpp）
    DrawBuffTooltip(ctx);               // 説明の窓（BattleUITooltip.cpp）
    DrawIntentTooltip(ctx);
    DrawCellTooltips(ctx);

    if (ctx.showDrawPile || ctx.showDiscardPile || ctx.showExhaustPile)
    {
        m_textRenderer->End();
        DrawPileViewer(ctx);
        m_textRenderer->Begin();
    }

    DrawBuffPopups(ctx);     // 頭の上のアイコン（数字より下に）
    DrawFloatingTexts(ctx);
    DrawTurnBanner();                   // ターンのバナー（BattleUIHud.cpp）

    m_textRenderer->End();

    m_spriteRenderer->Begin();
    if (ctx.discardSelectCount <= 0 || ctx.discardViewMode)   // 暗幕の間は出さない（「盤面を見る」では出す）
    {
        DrawEnemyInfoPanel(ctx);
    }

    if (ctx.selectedCardIndex < 0   // 選んでいる間は出さない（説明の窓が狙うマスを隠して押せなくなるため）
        && ctx.hoveredCardIndex >= 0 && ctx.hoveredCardIndex < (int)ctx.hand->GetCards().size())
    {
        float cx, cy, cw, ch; GetCardRect(ctx.hoveredCardIndex, cx, cy, cw, ch);
        CardTooltip::Draw(m_spriteRenderer, m_textRenderer, m_whiteTexture,
            ctx.hand->GetCards()[ctx.hoveredCardIndex]->GetData(),
            cx + cw / 2.0f, cy, cw, ch, m_screenWidth, m_screenHeight);
    }
}

void BattleUI::GridToWorld(GridMap* gridMap, int col, int row, float& outX, float& outZ)
{
    outX = (col - gridMap->GetCols() / 2.0f + 0.5f) * 1.1f;
    outZ = (row - gridMap->GetRows() / 2.0f + 0.5f) * 1.1f;
}

std::vector<std::wstring> BattleUI::WrapLines(const std::wstring& s, int maxCh, bool breakAtComma)
{
    std::vector<std::wstring> out;
    size_t start = 0;
    while (start < s.size())
    {
        size_t end = s.size();
        if (breakAtComma)   // 「、」までを1節に
        {
            size_t comma = s.find(L'、', start);
            if (comma != std::wstring::npos) end = comma + 1;
        }
        std::wstring wrapped = CardVisual::WrapText(s.substr(start, end - start), maxCh);   // 長い節は折り返す
        size_t p = 0, nl;
        while ((nl = wrapped.find(L'\n', p)) != std::wstring::npos) { out.push_back(wrapped.substr(p, nl - p)); p = nl + 1; }
        out.push_back(wrapped.substr(p));
        start = end;
    }
    return out;
}

void BattleUI::DrawWindow(float x, float y, float w, float h, const XMFLOAT4& tint)
{
    UiWindow::Draw(m_spriteRenderer, m_whiteTexture, x, y, w, h, tint);
}

bool BattleUI::WorldToScreen(float wx, float wy, float wz, Renderer3D* renderer3D,
    float& outX, float& outY) const
{
    XMMATRIX view = renderer3D->GetViewMatrix();
    XMMATRIX proj = renderer3D->GetProjectionMatrix();
    XMFLOAT4 clip;
    XMStoreFloat4(&clip, XMVector4Transform(XMVectorSet(wx, wy, wz, 1.0f), view * proj));
    if (clip.w <= 0.0f) return false;
    outX = (clip.x / clip.w + 1.0f) * 0.5f * m_screenWidth;
    outY = (1.0f - clip.y / clip.w) * 0.5f * m_screenHeight;
    return true;
}
