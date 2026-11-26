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

// 説明の窓（バフ・予告・罠・聖地・光るウロコ）

// 文字列の幅（実際のフォントで測る）
static float TextWidth(TextRenderer* tr, const std::wstring& s, float size)
{
    return tr->Measure(s.c_str(), size, FontRole::Body).width;
}

// バフの説明の窓（バフの一覧か頭の上のバフにホバーした時）
void BattleUI::DrawBuffTooltip(const BattleUIContext& ctx)
{
    if (m_hasHoveredBuff && !CardTooltip::s_debugHide)
    {
        const auto& info = BuffInfo::Get(m_hoveredBuffType);
        std::wstring desc = BuffInfo::GetDescription(m_hoveredBuffType, m_hoveredBuffValue);
        auto lines = WrapLines(desc, 20, false);                // 文の長さで枠を決める
        size_t noteLine = lines.size();                         // ここから下は残りターン（橙）
        if (!m_hoveredBuffNote.empty())
            for (auto& l : WrapLines(m_hoveredBuffNote + L"で切れる", 20, false)) lines.push_back(l);
        float tipW = TextWidth(m_textRenderer, info.name, 13.0f);               // 一番長い行に合わせる
        for (auto& l : lines) tipW = max(tipW, TextWidth(m_textRenderer, l, 11.0f));
        tipW += 12.0f;
        float tipH = 22.0f + lines.size() * 15.0f;
        float tipX = m_hoveredBuffX;
        float tipY = m_hoveredBuffY - tipH - 8.0f;
        if (tipX + tipW > m_screenWidth - 8.0f) tipX = m_screenWidth - 8.0f - tipW;   // 画面内に収める
        if (tipX < 8.0f) tipX = 8.0f;
        if (tipY < 8.0f) tipY = m_hoveredBuffY + 24.0f;         // 上に出せない時は下へ

        m_textRenderer->End();
        m_spriteRenderer->Begin();
        DrawWindow(tipX - 4.0f, tipY - 4.0f, tipW + 8.0f, tipH + 8.0f);
        m_spriteRenderer->End();
        m_textRenderer->Begin();

        m_textRenderer->DrawText(info.name.c_str(), tipX + 5.0f, tipY + 2.0f, 13.0f,
            D2D1::ColorF(1.0f, 1.0f, 0.5f));
        for (int i = 0; i < (int)lines.size(); i++)
            m_textRenderer->DrawText(lines[i].c_str(), tipX + 5.0f, tipY + 18.0f + i * 15.0f, 11.0f,
                (size_t)i >= noteLine ? D2D1::ColorF(1.0f, 0.7f, 0.3f) : D2D1::ColorF(0.8f, 0.8f, 0.8f));
    }
}

// 敵の予告アイコンの説明の窓
void BattleUI::DrawIntentTooltip(const BattleUIContext& ctx)
{
    if (m_intentHover && !CardTooltip::s_debugHide)
    {
        std::vector<std::wstring> bodyLines;                    // 行ごとに分ける
        for (size_t s = 0; s <= m_intentBody.size(); )
        {
            size_t n = m_intentBody.find(L'\n', s);
            if (n == std::wstring::npos) n = m_intentBody.size();
            bodyLines.push_back(m_intentBody.substr(s, n - s));
            s = n + 1;
        }
        float tw = TextWidth(m_textRenderer, m_intentTitle, 15.0f);             // 一番長い行に合わせる
        for (auto& l : bodyLines) tw = max(tw, TextWidth(m_textRenderer, l, 12.0f));
        tw += 20.0f;
        float th = 30.0f + bodyLines.size() * 17.0f;
        float tx = m_intentX - tw * 0.5f;
        float ty = m_intentY - th - 8.0f;
        if (tx < 4.0f) tx = 4.0f;
        if (tx + tw > m_screenWidth - 4.0f) tx = m_screenWidth - 4.0f - tw;
        if (ty < 4.0f) ty = m_intentY + 24.0f;   // 上に出せない時は下へ

        m_textRenderer->End();
        m_spriteRenderer->Begin();
        DrawWindow(tx, ty, tw, th);
        m_spriteRenderer->End();
        m_textRenderer->Begin();

        m_textRenderer->DrawText(m_intentTitle.c_str(), tx + 10.0f, ty + 6.0f, 15.0f,
            D2D1::ColorF(1.0f, 0.85f, 0.35f));
        for (int i = 0; i < (int)bodyLines.size(); i++)
        {
            bool rangeLine = bodyLines[i].rfind(L"範囲", 0) == 0;   // 範囲行は青系
            m_textRenderer->DrawText(bodyLines[i].c_str(), tx + 10.0f, ty + 25.0f + i * 17.0f, 12.0f,
                rangeLine ? D2D1::ColorF(0.75f, 0.85f, 1.0f) : D2D1::ColorF(0.9f, 0.9f, 0.9f));
        }
    }
}

// マスの説明（罠・聖地・光るウロコ）。カードを持っていない時に、同じマスへ少し止めたら出す
void BattleUI::DrawCellTooltips(const BattleUIContext& ctx)
{
    // マスの説明（罠・聖地）は、カードを持っていない時に同じマスへ少し止めたら出す
    if (ctx.hoveredCell != m_tipCell) { m_tipCell = ctx.hoveredCell; m_tipSince = GetTickCount64(); }
    bool showCellTip = ctx.selectedCardIndex < 0 && ctx.hoveredCardIndex < 0 && !CardTooltip::s_debugHide
        && !m_overUnitStatus && !m_intentHover
        && GetTickCount64() - m_tipSince >= CELL_TIP_DELAY_MS;
    float trapTipH = 0.0f;   // 罠の説明の高さ（聖地の説明をその下に並べる）

    // 罠のホバー詳細
    if (showCellTip && ctx.hoveredCell.first >= 0 && ctx.hoveredCell.second >= 0
        && ctx.hoveredCell.first < ctx.gridMap->GetCols()
        && ctx.hoveredCell.second < ctx.gridMap->GetRows())
    {
        auto& hCell = ctx.gridMap->GetCell(ctx.hoveredCell.first, ctx.hoveredCell.second);
        if (hCell.tileEffect.active)
        {
            const TerrainDef* def = TerrainDataBase::Get(hCell.tileEffect.id);
            if (def)
            {
                auto detail = CardTooltip::TrapLines(def, hCell.tileEffect.value);
                if (hCell.tileEffect.hazardTurns > 0)
                    detail.back() = L"残り" + std::to_wstring(hCell.tileEffect.hazardTurns) + L"ターン";
                if (!hCell.tileEffect.enemyOwned && def->growPct > 0)   // 自分の罠：育ち・導火線
                {
                    detail.push_back(L"育ち " + std::to_wstring(hCell.tileEffect.grown) + L"/3（ターンごと）");
                    detail.push_back(L"導火線 " + std::to_wstring(hCell.tileEffect.fuse) + L"/3（敵が隣にいる間進む）");
                }

                float tipX = (float)ctx.mousePos.x + 15.0f;
                float tipY = (float)ctx.mousePos.y - 50.0f;
                float tipW = TextWidth(m_textRenderer, def->name, 13.0f);       // 一番長い行に合わせる
                for (auto& l : detail) tipW = max(tipW, TextWidth(m_textRenderer, l, 11.0f));
                tipW += 12.0f;
                float tipH = 24.0f + detail.size() * 15.0f;
                trapTipH = tipH + 16.0f;

                m_textRenderer->End();
                m_spriteRenderer->Begin();
                DrawWindow(tipX - 4.0f, tipY - 4.0f, tipW + 8.0f, tipH + 8.0f);
                m_spriteRenderer->End();
                m_textRenderer->Begin();

                m_textRenderer->DrawText(def->name.c_str(), tipX + 5.0f, tipY + 2.0f, 13.0f,
                    D2D1::ColorF(def->color.x, def->color.y, def->color.z));
                for (int i = 0; i < (int)detail.size(); i++)
                    m_textRenderer->DrawText(detail[i].c_str(), tipX + 5.0f, tipY + 20.0f + i * 15.0f, 11.0f,
                        D2D1::ColorF(D2D1::ColorF::LightGray));
            }
        }
    }

    // 聖地のホバー詳細（罠と同じマスなら、その下に並べる）
    if (showCellTip && ctx.hoveredCell.first >= 0 && ctx.hoveredCell.second >= 0
        && ctx.hoveredCell.first < ctx.gridMap->GetCols()
        && ctx.hoveredCell.second < ctx.gridMap->GetRows())
    {
        auto& fCell = ctx.gridMap->GetCell(ctx.hoveredCell.first, ctx.hoveredCell.second);
        int f = fCell.faith;
        if (f > 0)
        {
            bool sanct = Faith::IsSanctuary(f);
            wchar_t l1[48], l2[48], l3[64];
            swprintf_s(l1, sanct ? L"聖域　信仰 %d/%d" : L"聖地　信仰 %d/%d", f, Faith::MAX);
            swprintf_s(l2, sanct ? L"立つと 攻撃+%d・被ダメ半分" : L"立つと 攻撃+%d", Faith::AttackBonus(f));
            swprintf_s(l3, L"ターン終了時 ここの敵に%dダメージ（信仰は半分に）", f);

            float tipX = (float)ctx.mousePos.x + 15.0f;
            float tipY = (float)ctx.mousePos.y - 50.0f + trapTipH;
            float tipW = 270.0f, tipH = 58.0f;

            m_textRenderer->End();
            m_spriteRenderer->Begin();
            DrawWindow(tipX - 4.0f, tipY - 4.0f, tipW + 8.0f, tipH + 8.0f);
            m_spriteRenderer->End();
            m_textRenderer->Begin();

            m_textRenderer->DrawText(l1, tipX + 5.0f, tipY + 2.0f, 13.0f, D2D1::ColorF(1.0f, 0.85f, 0.4f));
            m_textRenderer->DrawText(l2, tipX + 5.0f, tipY + 20.0f, 11.0f, D2D1::ColorF(D2D1::ColorF::LightGray));
            m_textRenderer->DrawText(l3, tipX + 5.0f, tipY + 36.0f, 11.0f, D2D1::ColorF(D2D1::ColorF::LightGray));
        }
    }

    // 光るウロコのホバー説明
    if (showCellTip && ctx.enemies)
        for (auto e : *ctx.enemies)
            if (e->GetWeakCell() == ctx.hoveredCell && ctx.hoveredCell.first >= 0)
            {
                float tipX = (float)ctx.mousePos.x + 15.0f, tipY = (float)ctx.mousePos.y - 50.0f;
                float tipW = 230.0f, tipH = 40.0f;
                m_textRenderer->End();
                m_spriteRenderer->Begin();
                DrawWindow(tipX - 4.0f, tipY - 4.0f, tipW + 8.0f, tipH + 8.0f);
                m_spriteRenderer->End();
                m_textRenderer->Begin();
                m_textRenderer->DrawText(L"光るウロコ", tipX + 5.0f, tipY + 2.0f, 13.0f, D2D1::ColorF(0.5f, 1.0f, 0.4f));
                m_textRenderer->DrawText(L"殴ると2倍ダメージ（1回で割れる）", tipX + 5.0f, tipY + 20.0f, 11.0f,
                    D2D1::ColorF(D2D1::ColorF::LightGray));
                break;
            }
}
