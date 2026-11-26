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

// 右の敵の一覧の窓（位置・中身・クリック）

// 敵の窓の寸法（ここ1か所で決める）
static const int   INTENT_CH = 11;      // 予告の1行の文字数（13px・幅170）

static const float INTENT_TOP = 30.0f;   // 予告の1行目（HPの下）

static const float INTENT_LH = 15.0f;   // 予告の行の高さ

static const float ENTRY_MIN_H = 66.0f;   // アイコン(55)＋余白

static const float DETAIL_W = 220.0f;  // 詳細の窓の幅

static const int   DETAIL_CH = 13;      // 詳細の1行の文字数（15px・幅200）

// 一覧に出す「次の行動」の行
static std::vector<std::wstring> IntentLines(Enemy* e)
{
    const EnemyAction* a = e->GetNextAction();
    if (!a) return {};
    return BattleUI::WrapLines(EnemyIntentVisual::GetActionText(*a, e->GetBuffManager()), INTENT_CH, true);
}

void BattleUI::GetEnemyEntryRect(int i, const std::vector<Enemy*>& enemies, int screenWidth,
    float& x, float& y, float& w, float& h)
{
    w = 240.0f;
    x = screenWidth - 250.0f;
    y = 50.0f + RelicManager::BarTotalHeight(screenWidth);
    if (i < 0) i = 0;
    for (int k = 0; ; k++)   // 上の敵の窓の高さを積み上げて、i番目の位置を出す
    {
        float lines = (k < (int)enemies.size()) ? (float)IntentLines(enemies[k]).size() : 0.0f;
        h = max(ENTRY_MIN_H, INTENT_TOP + lines * INTENT_LH + 8.0f);   // 予告の行数に合わせて伸びる
        if (k == i) return;
        y += h + 5.0f;
    }
}

int BattleUI::EnemyEntryAt(POINT p, const std::vector<Enemy*>& enemies, int screenWidth)
{
    for (int i = 0; i < (int)enemies.size(); i++)
    {
        float x, y, w, h;
        GetEnemyEntryRect(i, enemies, screenWidth, x, y, w, h);
        if (p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h) return i;
    }
    return -1;
}

// 詳細の窓の中身（行動予定・ブロック・移動不可・バフ）。高さもここで決まる
std::vector<BattleUI::DetailRow> BattleUI::EnemyDetailRows(Enemy* e, float x, float topY, POINT mp,
    float minH, float& height) const
{
    std::vector<DetailRow> rows;
    float y = topY + 6.0f;
    auto add = [&](const std::wstring& s, float size, D2D1_COLOR_F col, float lh, int ch) {
        for (auto& l : WrapLines(s, ch, false)) { rows.push_back({ l, size, col, y }); y += lh; }
        };

    for (auto& act : e->GetPlannedActions())
        add(EnemyIntentVisual::GetActionText(act, e->GetBuffManager()), 15.0f, D2D1::ColorF(1.0f, 0.8f, 0.3f), 20.0f, DETAIL_CH);
    if (e->GetBlock() > 0)
        add(L"Block: " + std::to_wstring(e->GetBlock()), 15.0f, D2D1::ColorF(D2D1::ColorF::LightBlue), 18.0f, DETAIL_CH);

    for (auto& buff : e->GetBuffManager().GetBuffsSummed())
    {
        const auto& info = BuffInfo::Get(buff.type);
        std::wstring buffText;
        if (buff.type == BuffType::HeadWeak || BuffInfo::HidesValue(buff.type))
            buffText = info.name;                       // 弱点は名前だけ（ターン表記なし）
        else if (BuffInfo::IsDurationBased(buff.type))
            buffText = info.name + L": " + std::to_wstring(buff.duration) + L"ターン";
        else
        {
            buffText = info.name + L": " + std::to_wstring(buff.value)
                + BuffInfo::TurnsSuffix(BuffInfo::TurnsNote(e->GetBuffManager(), buff.type));   // 永続なら何もつけない
        }
        bool hov = (mp.x >= x && mp.x <= x + DETAIL_W && mp.y >= y && mp.y <= y + 20.0f);
        add(buffText, 14.0f, hov ? D2D1::ColorF(1.0f, 1.0f, 0.5f) : D2D1::ColorF(0.6f, 1.0f, 0.6f), 20.0f, DETAIL_CH + 1);
        if (hov)   // ホバー中のバフは説明も（この分も高さに入る）
        {
            int descNum = BuffInfo::IsDurationBased(buff.type) ? buff.duration : buff.value;
            add(BuffInfo::GetDescription(buff.type, descNum), 12.0f, D2D1::ColorF(0.8f, 0.8f, 0.8f), 16.0f, 16);
        }
    }
    height = max(minH, y + 6.0f - topY);
    return rows;
}

void BattleUI::DrawEnemyInfoPanel(const BattleUIContext& ctx)
{
    const auto& enemies = *ctx.enemies;
    float iconSize = 55.0f;

    POINT mp = ctx.mousePos;
    int hoveredEnemy = -1;

    for (int i = 0; i < (int)enemies.size(); i++)
    {
        Enemy* enemy = enemies[i];
        float panelX, entryY, panelW, entryH;
        GetEnemyEntryRect(i, enemies, ctx.screenWidth, panelX, entryY, panelW, entryH);

        float detailX = panelX - DETAIL_W - 10.0f, detailH;
        EnemyDetailRows(enemy, detailX, entryY, mp, entryH, detailH);
        bool isEntryHover = (mp.x >= panelX && mp.x <= panelX + panelW
            && mp.y >= entryY && mp.y <= entryY + entryH);
        bool isDetailHover = (m_panelHoveredEnemy == i)
            && (mp.x >= detailX && mp.x <= panelX
                && mp.y >= entryY && mp.y <= entryY + detailH);
        bool isHover = isEntryHover || isDetailHover;

        bool isSel = (ctx.selectedEnemy == i);
        const XMFLOAT4& hue = enemy->hueColor;

        // 窓の枠を識別色に寄せる（範囲・足元と同じ色＝どの敵の窓か一目で分かる）
        DrawWindow(panelX, entryY, panelW, entryH, HighlightPalette::Tint(hue, 0.55f));
        // アイコンの下地も識別色
        m_spriteRenderer->DrawSprite(m_whiteTexture, panelX + 5.0f, entryY + 5.0f,
            iconSize, iconSize, 0.0f, XMFLOAT4(hue.x, hue.y, hue.z, 0.45f));

        if (isHover || isSel)   // 選択/ホバーは識別色を薄く重ねる＋角の印
        {
            m_spriteRenderer->DrawSprite(m_whiteTexture, panelX + 5.0f, entryY + 5.0f,
                panelW - 10.0f, entryH - 10.0f, 0.0f, XMFLOAT4(hue.x, hue.y, hue.z, 0.22f));

            XMFLOAT4 line(hue.x, hue.y, hue.z, 1.0f);
            const float th = 3.0f, L = 14.0f;
            float rx = panelX + panelW, by = entryY + entryH;
            m_spriteRenderer->DrawSprite(m_whiteTexture, panelX, entryY, L, th, 0.0f, line);
            m_spriteRenderer->DrawSprite(m_whiteTexture, panelX, entryY, th, L, 0.0f, line);
            m_spriteRenderer->DrawSprite(m_whiteTexture, rx - L, entryY, L, th, 0.0f, line);
            m_spriteRenderer->DrawSprite(m_whiteTexture, rx - th, entryY, th, L, 0.0f, line);
            m_spriteRenderer->DrawSprite(m_whiteTexture, panelX, by - th, L, th, 0.0f, line);
            m_spriteRenderer->DrawSprite(m_whiteTexture, panelX, by - L, th, L, 0.0f, line);
            m_spriteRenderer->DrawSprite(m_whiteTexture, rx - L, by - th, L, th, 0.0f, line);
            m_spriteRenderer->DrawSprite(m_whiteTexture, rx - th, by - L, th, L, 0.0f, line);
        }

        // アイコン
        ID3D11ShaderResourceView* tex = TextureManager::Get(enemy->GetTextureName());
        if (tex)
        {
            m_spriteRenderer->DrawSprite(tex, panelX + 5.0f, entryY + 5.0f,
                iconSize, iconSize, 0.0f, XMFLOAT4(1, 1, 1, 1));
        }

        // HPバー
        float barX = panelX + iconSize + 10.0f;
        float barY = entryY + 5.0f;
        float barW = panelW - iconSize - 20.0f;
        float barH = 12.0f;

        HPBarInfo panelBar;
        panelBar.currentHP = enemy->GetHp();
        panelBar.maxHP = enemy->GetMaxHp();
        panelBar.displayHP = enemy->GetDisplayHp();
        panelBar.block = enemy->GetBlock();
        int tr = 1 + ctx.player->GetBuffManager().GetBuffValue(BuffType::ToxicRhythm);
        int pp = enemy->GetBuffManager().GetBuffValue(BuffType::Poison);
        int psum = 0;
        for (int i = 0; i < tr; i++) { int v = pp - i; if (v <= 0) break; psum += v; }
        panelBar.poisonDmg = psum; panelBar.poisonBoosted = (tr > 1);
        panelBar.hasBurn = enemy->GetBuffManager().HasBuff(BuffType::Burn);
        DrawHPBar(barX, barY, barW, barH, panelBar, ctx.highlightTimer);

        if (enemy->GetBlock() > 0)
        {
            float bIconSize = barH * 2.0f;
            float bIconX = barX - bIconSize * 0.35f;
            float bIconY = barY + (barH - bIconSize) / 2.0f;
            m_spriteRenderer->DrawSprite(TextureManager::Get("icon_block"),
                bIconX, bIconY, bIconSize, bIconSize, 0.0f, XMFLOAT4(1, 1, 1, 1));
        }

        if (isHover && hoveredEnemy < 0)
            hoveredEnemy = i;

    }

    // ホバー中の詳細背景（スプライト）
    if (hoveredEnemy >= 0 && !CardTooltip::s_debugHide)
    {
        float px, ey, pw, eh;
        GetEnemyEntryRect(hoveredEnemy, enemies, ctx.screenWidth, px, ey, pw, eh);
        float detailH;
        EnemyDetailRows(enemies[hoveredEnemy], px - DETAIL_W - 10.0f, ey, mp, eh, detailH);
        DrawWindow(px - DETAIL_W - 10.0f, ey, DETAIL_W, detailH, HighlightPalette::Tint(enemies[hoveredEnemy]->hueColor, 0.55f));
    }

    m_panelHoveredEnemy = hoveredEnemy;

    m_spriteRenderer->End();
    m_textRenderer->Begin();

    // テキスト描画（全エネミーまとめて）
    const D2D1_COLOR_F OL = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.85f);   // 文字の縁取り
    const float OLT = 1.0f;
    for (int i = 0; i < (int)enemies.size(); i++)
    {
        Enemy* enemy = enemies[i];
        float panelX, entryY, panelW, entryH;
        GetEnemyEntryRect(i, enemies, ctx.screenWidth, panelX, entryY, panelW, entryH);

        // HP数値
        wchar_t hpText[32];
        swprintf_s(hpText, L"%d/%d", enemy->GetHp(), enemy->GetMaxHp());
        m_textRenderer->DrawOutlinedText(hpText,
            panelX + iconSize + 10.0f, entryY + 16.0f, 15.0f,
            D2D1::ColorF(D2D1::ColorF::White), OL, OLT);

        if (enemy->GetBlock() > 0)
        {
            float barX = panelX + iconSize + 10.0f;
            float barY = entryY + 5.0f;
            float barH = 12.0f;
            float bIconSize = barH * 2.0f;
            float bIconX = barX - bIconSize * 0.35f;
            float bIconY = barY + (barH - bIconSize) / 2.0f;
            wchar_t blockText[16];
            swprintf_s(blockText, L"%d", enemy->GetBlock());
            float bFont = 11.0f;
            float bTextW = wcslen(blockText) * bFont * 0.5f;
            m_textRenderer->DrawOutlinedText(blockText,
                bIconX + (bIconSize - bTextW) / 2.0f,
                bIconY + (bIconSize - bFont) / 2.0f,
                bFont, D2D1::ColorF(D2D1::ColorF::White), OL, OLT);
        }

        // 次の行動（窓の高さと同じ行で描く）
        float ty = entryY + INTENT_TOP;
        for (auto& line : IntentLines(enemy))
        {
            m_textRenderer->DrawOutlinedText(line.c_str(), panelX + iconSize + 10.0f, ty, 13.0f,
                D2D1::ColorF(D2D1::ColorF::Orange), OL, OLT);
            ty += INTENT_LH;
        }

        // ホバー中の詳細テキスト（背景と同じ行の一覧）
        if (hoveredEnemy == i && !CardTooltip::s_debugHide)
        {
            float detailX = panelX - DETAIL_W - 10.0f, detailH;
            for (auto& r : EnemyDetailRows(enemy, detailX, entryY, mp, entryH, detailH))
                m_textRenderer->DrawOutlinedText(r.text.c_str(), detailX + 10.0f, r.y, r.size, r.color, OL, OLT);
        }
    }
    m_textRenderer->End();
}
