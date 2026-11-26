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

// 頭の上の表示（HP・バフ・予告・ヒットマーク・浮かぶ数字・バフのポップ）

static bool StatusAboveHead(Enemy* enemy, Player* player, const std::vector<Enemy*>& enemies, int depth = 0);   // 下の GetStatusFoot の上で定義

static bool EnemyRightBelow(Player* player, const std::vector<Enemy*>& enemies);                                 // 同上

void BattleUI::DrawEnemyHPBar(Enemy* enemy, Renderer3D* renderer3D)
{
    float screenX, screenY;
    if (!GetEnemyScreenPos(enemy, renderer3D, screenX, screenY)) return;

    float barWidth = enemy->IsBoss() ? 150.0f : 100.0f;
    float barHeight = enemy->IsBoss() ? 20.0f : 16.0f;
    float barX = screenX - barWidth / 2.0f;
    float barY = screenY - barHeight;

    m_spriteRenderer->DrawSprite(m_whiteTexture,
        barX - 2.0f, barY - 2.0f, barWidth + 4.0f, barHeight + 4.0f,
        0.0f, XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f));

    m_spriteRenderer->DrawSprite(m_whiteTexture,
        barX, barY, barWidth, barHeight,
        0.0f, XMFLOAT4(0.3f, 0.0f, 0.0f, 1.0f));

    float ratio = (float)enemy->GetHp() / (float)enemy->GetMaxHp();
    XMFLOAT4 barColor = ratio > 0.5f
        ? XMFLOAT4(0.0f, 0.8f, 0.0f, 1.0f)
        : ratio > 0.25f
        ? XMFLOAT4(0.8f, 0.8f, 0.0f, 1.0f)
        : XMFLOAT4(0.8f, 0.0f, 0.0f, 1.0f);

    m_spriteRenderer->DrawSprite(m_whiteTexture,
        barX, barY, barWidth * ratio, barHeight,
        0.0f, barColor);
}

bool BattleUI::GetEnemyScreenPos(Enemy* enemy, Renderer3D* renderer3D, float& outX, float& outY) const
{
    float pitch = XMConvertToRadians(-Renderer3D::BILLBOARD_PITCH);
    return WorldToScreen(enemy->worldX,
        enemy->worldY + enemy->height * cos(pitch),
        enemy->worldZ + 0.5f - enemy->height * sin(pitch),
        renderer3D, outX, outY);
}

bool BattleUI::GetEnemyFootPos(Enemy* enemy, Renderer3D* renderer3D, float& outX, float& outY) const
{
    return WorldToScreen(enemy->worldX, 0.0f, enemy->worldZ + 0.5f, renderer3D, outX, outY);
}

// 自分のすぐ上のマスに立つ敵か（その敵のUIは足元ではなく頭の上に出す。足元だと自分のキャラやUIと重なる）
// UIを頭の上に出す敵か：すぐ下のマスに「自分」か「UIを上げた敵」がいる（上げたUIと重なるので、上へ連鎖して上げる）
static bool StatusAboveHead(Enemy* enemy, Player* player, const std::vector<Enemy*>& enemies, int depth)
{
    if (depth > 8) return false;   // 盤の行数より深くは連鎖しない（念のため）
    auto raisedBelow = [&](int c, int r) {
        if (c == player->gridCol && r + 1 == player->gridRow) return true;
        for (auto other : enemies)
        {
            if (other == enemy) continue;
            bool under = other->IsSnake()   // 蛇は頭の上にだけUIを上げるので、頭のマスだけ見る
                ? (other->gridCol == c && other->gridRow == r + 1)
                : other->OccupiesCell(c, r + 1);
            if (under && StatusAboveHead(other, player, enemies, depth + 1)) return true;
        }
        return false;
        };
    if (enemy->IsSnake()) return raisedBelow(enemy->gridCol, enemy->gridRow);   // 蛇のUIは頭の下に出るので頭だけ
    for (auto& [c, r] : enemy->OccupiedCells())
        if (raisedBelow(c, r)) return true;
    return false;
}

// プレイヤーのすぐ下のマスに敵がいるか（いればプレイヤーのUIを頭の上へ。足元だと敵の頭や警告と重なる）
static bool EnemyRightBelow(Player* player, const std::vector<Enemy*>& enemies)
{
    for (auto e : enemies)
    {
        if (!e || e->GetHp() <= 0) continue;
        bool below = e->IsSnake()   // 蛇は平置きなので頭のマスだけ
            ? (e->gridCol == player->gridCol && e->gridRow == player->gridRow + 1)
            : e->OccupiesCell(player->gridCol, player->gridRow + 1);
        if (below) return true;
    }
    return false;
}

bool BattleUI::GetStatusFoot(Enemy* enemy, const BattleUIContext& ctx, float& outX, float& outY) const
{
    if (!GetEnemyFootPos(enemy, ctx.renderer3D, outX, outY)) return false;
    if (StatusAboveHead(enemy, ctx.player, *ctx.enemies))
    {
        float hx, hy;   // HPバー・バフの下端を頭のすぐ上へ（行動予告はバーの上に並ぶので、さらに上）
        bool ok = enemy->IsSnake()   // 蛇は平置きなので、頭のマスの奥の縁を「頭の上」とする
            ? WorldToScreen(enemy->worldX, 0.0f, enemy->worldZ - 0.55f, ctx.renderer3D, hx, hy)
            : GetEnemyScreenPos(enemy, ctx.renderer3D, hx, hy);
        if (ok)
        {
            URect r = UnitStatusRect(outX, outY, 1.0f / ctx.cameraZoom, enemy->IsBoss(), enemy->GetBuffManager());
            outY += (hy - 6.0f) - (r.y + r.h);
        }
    }
    else if (enemy->IsSnake()) outY += 40.0f * (1.0f / ctx.cameraZoom);   // 蛇は平置きなのでUIを下げる
    return true;
}

// HPバーとバフ列が占める範囲（DrawUnitStatusSprites と同じ並び方）
URect BattleUI::UnitStatusRect(float footX, float footY, float scale, bool isBoss, BuffManager& bm) const
{
    float barWidth = (isBoss ? 100.0f : 50.0f) * scale;
    float barHeight = (isBoss ? 15.0f : 10.0f) * scale;
    float barX = footX - barWidth / 2.0f, barY = footY - 30.0f;
    float maxX = barX + barWidth + 40.0f;
    float x = barX, y = barY + barHeight + 4.0f, bottom = barY + barHeight;
    for (auto& buff : bm.GetBuffsSummed())
    {
        if (x + 36.0f > maxX) { x = barX; y += 22.0f; }
        bottom = y + 16.0f;
        x += 36.0f;
    }
    return { barX - 10.0f, barY, barWidth + 50.0f, bottom - barY };
}

void BattleUI::DrawFloatingTexts(const BattleUIContext& ctx)
{
    for (auto& t : FloatingTextManager::GetAll())
    {
        float rise, alpha, size;
        FloatingTextManager::GetVisual(t, rise, alpha, size);

        float sx, sy;
        if (!WorldToScreen(t.worldX, t.worldY + rise, t.worldZ, ctx.renderer3D, sx, sy))
            continue;

        sx += t.offsetX - t.text.size() * size * 0.25f;    // 中央寄せ

        float o = size * 0.06f;                        // 文字サイズに比例させる
        const float dir[4][2] = { {-1,0},{1,0},{0,-1},{0,1} };
        for (auto& d : dir)
            m_textRenderer->DrawText(t.text.c_str(), sx + d[0] * o, sy + d[1] * o, size,
                D2D1::ColorF(0, 0, 0, alpha));
        m_textRenderer->DrawText(t.text.c_str(), sx, sy, size,
            D2D1::ColorF(t.color.x, t.color.y, t.color.z, alpha));
    }
}

// バフ・デバフが付いた時の、頭の上のアイコン（文字を描いている途中で呼ばれるので、絵の間だけ切り替える）
void BattleUI::DrawBuffPopups(const BattleUIContext& ctx)
{
    const auto& all = BuffPopupManager::GetAll();
    if (all.empty()) return;
    m_textRenderer->End();
    m_spriteRenderer->Begin();
    for (auto& p : all)
    {
        float rise, alpha, scale, sx, sy;
        BuffPopupManager::GetVisual(p, rise, alpha, scale);
        if (!WorldToScreen(p.worldX, p.worldY + rise, p.worldZ, ctx.renderer3D, sx, sy)) continue;
        ID3D11ShaderResourceView* tex = TextureManager::Get(BuffInfo::Get(p.type).texture);
        if (!tex) continue;
        float s = BuffPopupManager::ICON * scale;
        m_spriteRenderer->DrawSprite(tex, sx + p.offsetX - s * 0.5f, sy - s * 0.5f, s, s, 0.0f, XMFLOAT4(1, 1, 1, alpha));
    }
    m_spriteRenderer->End();
    m_textRenderer->Begin();
    for (auto& p : all)   // 増えた数（アイコンの右下に、ふちどりつき）
    {
        if (p.amount <= 0) continue;
        float rise, alpha, scale, sx, sy;
        BuffPopupManager::GetVisual(p, rise, alpha, scale);
        if (!WorldToScreen(p.worldX, p.worldY + rise, p.worldZ, ctx.renderer3D, sx, sy)) continue;
        std::wstring t = L"+" + std::to_wstring(p.amount);
        float size = 20.0f * scale;
        float tx = sx + p.offsetX + BuffPopupManager::ICON * 0.3f, ty = sy;
        XMFLOAT4 c = BuffInfo::Get(p.type).color;
        const float dir[4][2] = { {-1,0},{1,0},{0,-1},{0,1} };
        for (auto& d : dir)
            m_textRenderer->DrawText(t.c_str(), tx + d[0] * 1.5f, ty + d[1] * 1.5f, size, D2D1::ColorF(0, 0, 0, alpha));
        m_textRenderer->DrawText(t.c_str(), tx, ty, size, D2D1::ColorF(c.x, c.y, c.z, alpha));
    }
}

void BattleUI::DrawUnitStatusSprites(float footX, float footY, float scale, bool isBoss,
    const HPBarInfo& bar, BuffManager& bm, POINT mousePos, float timer)
{
    float barWidth = (isBoss ? 100.0f : 50.0f) * scale;
    float barHeight = (isBoss ? 15.0f : 10.0f) * scale;
    float barX = footX - barWidth / 2.0f;
    float barY = footY - 30.0f;

    DrawHPBar(barX, barY, barWidth, barHeight, bar, timer);

    // ブロックアイコン（HPバー左端）
    if (bar.block > 0)
    {
        float iconSize = barHeight * 2.0f;
        float iconX = barX - iconSize / 2.0f;
        float iconY = barY + (barHeight - iconSize) / 2.0f;
        m_spriteRenderer->DrawSprite(TextureManager::Get("icon_block"), iconX, iconY, iconSize, iconSize, 0.0f, XMFLOAT4(1, 1, 1, 1));
    }

    // バフ/デバフアイコン
    float bIconY = barY + barHeight + 4.0f, bIconX = barX;
    float bMaxX = barX + barWidth + 40.0f;
    for (auto& buff : bm.GetBuffsSummed())
    {
        float iconSize = 16.0f;
        if (bIconX + iconSize + 20.0f > bMaxX) { bIconX = barX; bIconY += iconSize + 6.0f; }
        XMFLOAT4 buffColor = BuffInfo::Get(buff.type).color;
        bool hov = (mousePos.x >= bIconX && mousePos.x <= bIconX + iconSize
            && mousePos.y >= bIconY && mousePos.y <= bIconY + iconSize);
        if (hov)
        {
            m_hasHoveredBuff = true; m_hoveredBuffType = buff.type;
            m_hoveredBuffValue = BuffInfo::IsDurationBased(buff.type) ? buff.duration : buff.value;
            m_hoveredBuffNote = BuffInfo::TurnsNote(bm, buff.type);
            m_hoveredBuffX = bIconX; m_hoveredBuffY = bIconY;
        }
        auto btex = TextureManager::Get(BuffInfo::Get(buff.type).texture);
        if (btex)
            m_spriteRenderer->DrawSprite(btex, bIconX, bIconY, iconSize, iconSize, 0.0f, hov ? XMFLOAT4(1, 1, 1, 1) : XMFLOAT4(0.85f, 0.85f, 0.85f, 1));
        else
        {
            m_spriteRenderer->DrawSprite(m_whiteTexture, bIconX - 1, bIconY - 1, iconSize + 2, iconSize + 2, 0.0f, XMFLOAT4(0, 0, 0, 1));
            m_spriteRenderer->DrawSprite(m_whiteTexture, bIconX, bIconY, iconSize, iconSize, 0.0f, hov ? XMFLOAT4(1, 1, 1, 1) : buffColor);
        }
        bIconX += iconSize + 20.0f;
    }

    URect area = UnitStatusRect(footX, footY, scale, isBoss, bm);
    if (mousePos.x >= area.x && mousePos.x <= area.x + area.w
        && mousePos.y >= area.y && mousePos.y <= area.y + area.h)
        m_overUnitStatus = true;
}

void BattleUI::DrawUnitStatusText(float footX, float footY, float scale, bool isBoss,
    const HPBarInfo& bar, BuffManager& bm)
{
    float barWidth = (isBoss ? 100.0f : 50.0f) * scale;
    float barHeight = (isBoss ? 15.0f : 10.0f) * scale;
    float barX = footX - barWidth / 2.0f;
    float barY = footY - 30.0f;
    float fontSize = max(8.0f, (isBoss ? 10.0f : 8.0f) * scale);

    // HP数値
    wchar_t hp[32]; swprintf_s(hp, L"%d/%d", bar.currentHP, bar.maxHP);
    D2D1_SIZE_F hs = m_textRenderer->Measure(hp, fontSize, FontRole::Body);   // 実際の文字の大きさでバーの中央に
    float tx = barX + (barWidth - hs.width) / 2.0f;
    float ty = barY + (barHeight - hs.height) / 2.0f;
    m_textRenderer->DrawText(hp, tx + 1, ty + 1, fontSize, D2D1::ColorF(0, 0, 0, 1));
    m_textRenderer->DrawText(hp, tx, ty, fontSize, D2D1::ColorF(D2D1::ColorF::White));

    // ブロック数値
    if (bar.block > 0)
    {
        float iconSize = barHeight * 2.0f;
        float iconX = barX - iconSize / 2.0f;
        float iconY = barY + (barHeight - iconSize) / 2.0f;   // 盾のアイコンと同じ位置
        wchar_t bt[16]; swprintf_s(bt, L"%d", bar.block);
        float bFont = max(7.0f, fontSize * 0.9f);
        D2D1_SIZE_F bs = m_textRenderer->Measure(bt, bFont, FontRole::Body);   // 盾の四角の中央に
        float bx = iconX + (iconSize - bs.width) / 2.0f, by = iconY + (iconSize - bs.height) / 2.0f;
        m_textRenderer->DrawText(bt, bx + 1.0f, by + 1.0f, bFont, D2D1::ColorF(0, 0, 0, 1));
        m_textRenderer->DrawText(bt, bx, by, bFont, D2D1::ColorF(D2D1::ColorF::White));
    }

    // バフ/デバフ数値
    float vY = barY + barHeight + 4.0f, vX = barX, vMaxX = barX + barWidth + 40.0f;
    for (auto& buff : bm.GetBuffsSummed())
    {
        float iconSize = 16.0f;
        if (vX + iconSize + 20.0f > vMaxX) { vX = barX; vY += iconSize + 6.0f; }
        wchar_t bv[16];
        if (BuffInfo::IsDurationBased(buff.type)) swprintf_s(bv, L"%dT", buff.duration);
        else swprintf_s(bv, L"%d", buff.value);
        if (!BuffInfo::HidesValue(buff.type))
            m_textRenderer->DrawText(bv, vX + iconSize + 2.0f, vY + 1.0f, 11.0f, D2D1::ColorF(D2D1::ColorF::White));
        // 期限つきの分があれば、アイコンの右下に一番早く切れる分の残りターン（橙）
        if (!BuffInfo::TurnsNote(bm, buff.type).empty())
        {
            int soon = bm.TimedParts(buff.type).front().turns;
            m_textRenderer->DrawOutlinedText(std::to_wstring(soon).c_str(), vX + iconSize - 6.0f, vY + iconSize - 10.0f, 9.0f,
                D2D1::ColorF(1.0f, 0.65f, 0.2f), D2D1::ColorF(0, 0, 0), 2.0f);
        }
        vX += iconSize + 20.0f;
    }
}

bool BattleUI::GetHitmarkRect(Enemy* enemy, Renderer3D* r3d, float& x, float& y, float& w, float& h) const
{
    float hx, hy;
    if (!GetEnemyScreenPos(enemy, r3d, hx, hy)) return false;
    const float mk = 34.0f;                       // 当たりマーカーと同じレイアウト
    x = hx - mk / 2.0f - 8.0f;
    y = hy - mk - 14.0f - 8.0f;
    w = mk + 16.0f; h = mk + 16.0f;
    return true;
}

// 敵とプレイヤーの頭の上（HP・バフ・予告）。文字を描いている途中で呼ばれ、文字を描いている状態で戻る
void BattleUI::DrawUnitStatuses(const BattleUIContext& ctx)
{
        m_textRenderer->End();
        m_spriteRenderer->Begin();

        m_intentHover = false;

        for (auto enemy : *ctx.enemies)
        {
            float headX, headY, footX, footY;
            if (!GetEnemyScreenPos(enemy, ctx.renderer3D, headX, headY)) continue;
            if (!GetStatusFoot(enemy, ctx, footX, footY)) continue;

            float scale = 1.0f / ctx.cameraZoom;  // ズームアウト時に小さくなる

            float barWidth = (enemy->IsBoss() ? 100.0f : 50.0f) * scale;
            float barHeight = (enemy->IsBoss() ? 10.0f : 7.0f) * scale;

            // --- HPバー（足元の少し下） ---
            float barX = footX - barWidth / 2.0f;
            float barY = footY - 30.0f;

            // この敵の攻撃が今あなたに当たる → キャラの頭上に大きめの赤警告（敵の識別色フチ）
            if (enemy->hitsPlayer && ctx.isPlayerTurn)
            {
                float mk = 34.0f;
                float mx = headX - mk / 2.0f;
                float my = headY - mk - 14.0f;
                if (StatusAboveHead(enemy, ctx.player, *ctx.enemies))   // UIを頭上へ上げた時は、行動予告のさらに上に
                    my = barY - EnemyIntentVisual::ICON_SIZE - 2.0f - mk - 4.0f;
                float pulse = 0.75f + 0.25f * sinf(ctx.highlightTimer * 6.0f);
                const XMFLOAT4& hue = enemy->hueColor;
                m_spriteRenderer->DrawSprite(TextureManager::Get("ui_hitring"),
                    mx, my, mk, mk, 0.0f, XMFLOAT4(hue.x, hue.y, hue.z, pulse));   // 敵色フチ
                m_spriteRenderer->DrawSprite(TextureManager::Get("ui_hitmark"),
                    mx, my, mk, mk, 0.0f, XMFLOAT4(1.0f, 1.0f, 1.0f, pulse));       // 赤バッジ

                if (ctx.mousePos.x >= mx && ctx.mousePos.x <= mx + mk
                    && ctx.mousePos.y >= my && ctx.mousePos.y <= my + mk)
                {
                    m_intentHover = true;
                    m_intentX = mx + mk * 0.5f;
                    m_intentY = my;
                    m_intentTitle = L"攻撃が当たる！";
                    m_intentBody = L"今の位置はこの敵の攻撃範囲内\n移動して避けよう";
                }
            }

            float footX2, footY2;

            int trE = 1 + ctx.player->GetBuffManager().GetBuffValue(BuffType::ToxicRhythm);
            int ppE = enemy->GetBuffManager().GetBuffValue(BuffType::Poison);
            int psumE = 0; for (int i = 0; i < trE; i++) { int v = ppE - i; if (v <= 0) break; psumE += v; }

            HPBarInfo eBar;
            eBar.currentHP = enemy->GetHp(); eBar.maxHP = enemy->GetMaxHp();
            eBar.displayHP = enemy->GetDisplayHp(); eBar.block = enemy->GetBlock();
            eBar.poisonDmg = psumE; eBar.poisonBoosted = (trE > 1); eBar.hasBurn = enemy->GetBuffManager().HasBuff(BuffType::Burn);

            DrawUnitStatusSprites(footX, footY, scale, enemy->IsBoss(), eBar,
                enemy->GetBuffManager(), ctx.mousePos, ctx.highlightTimer);

            // --- 次の行動アイコン（頭上） ---
            if (!EnemyIntentVisual::Hidden())   // 目隠し中は出さない
            {
                float iconSize = EnemyIntentVisual::ICON_SIZE;
                float iconX = barX;
                float iconY = barY - iconSize - 2.0f;

                for (auto& act : enemy->GetPlannedActions())
                    for (auto& e : act.effects)
                    {
                        if (!EnemyIntentVisual::ShouldShow(e)) continue;

                        bool hov = (ctx.mousePos.x >= iconX && ctx.mousePos.x <= iconX + iconSize
                            && ctx.mousePos.y >= iconY && ctx.mousePos.y <= iconY + iconSize);
                        if (hov)
                        {
                            float p = 3.0f + 2.0f * (0.5f + 0.5f * sinf(ctx.highlightTimer * 6.0f));
                            m_spriteRenderer->DrawSprite(m_whiteTexture, iconX - p, iconY - p,
                                iconSize + p * 2, iconSize + p * 2, 0.0f, XMFLOAT4(1.0f, 0.85f, 0.35f, 0.9f));
                            m_intentHover = true;
                            m_intentX = iconX + iconSize * 0.5f;
                            m_intentY = iconY;
                            EnemyIntentVisual::GetEffectDesc(e, act, enemy->GetBuffManager(),
                                m_intentTitle, m_intentBody);
                        }

                        EnemyIntentVisual::DrawIcon(m_spriteRenderer, m_whiteTexture, e,
                            iconX, iconY, EnemyIntentVisual::ICON_SIZE);
                        iconX += EnemyIntentVisual::STEP;
                    }
            }
        }

        {
            float footX, footY;
            if (WorldToScreen(ctx.player->worldX, 0.0f, ctx.player->worldZ + 0.5f, ctx.renderer3D, footX, footY))
            {
                float scale = 1.0f / ctx.cameraZoom;
                URect me = UnitStatusRect(footX, footY, scale, false, ctx.player->GetBuffManager());
                if (EnemyRightBelow(ctx.player, *ctx.enemies))   // すぐ下に敵：UIの下端を頭のすぐ上へ（敵のUIを上げる時と同じ）
                {
                    float pitch = XMConvertToRadians(-Renderer3D::BILLBOARD_PITCH);
                    float hx, hy;
                    if (WorldToScreen(ctx.player->worldX, ctx.player->worldY + ctx.player->height * cos(pitch),
                        ctx.player->worldZ + 0.5f - ctx.player->height * sin(pitch), ctx.renderer3D, hx, hy))
                    {
                        float d = (hy - 6.0f) - (me.y + me.h);
                        me.y += d; footY += d;
                    }
                }
                for (auto enemy : *ctx.enemies)                // 敵のUIと重なる時だけ、近いほう（上下）へずらす
                {
                    float ex, ey;
                    if (!GetStatusFoot(enemy, ctx, ex, ey)) continue;
                    URect r = UnitStatusRect(ex, ey, scale, enemy->IsBoss(), enemy->GetBuffManager());
                    if (!EnemyIntentVisual::Hidden())          // 行動アイコンの分も避ける
                    {
                        float icon = EnemyIntentVisual::ICON_SIZE + 2.0f;
                        r.y -= icon; r.h += icon;
                    }
                    if (me.x < r.x + r.w && r.x < me.x + me.w && me.y < r.y + r.h && r.y < me.y + me.h)
                    {
                        float up = me.y + me.h - r.y, down = r.y + r.h - me.y + 8.0f;   // 下へは少し間を空ける
                        float d = (up < down ? -up : down);
                        me.y += d; footY += d;
                    }
                }
                HPBarInfo pBar;
                pBar.currentHP = ctx.player->GetHp(); pBar.maxHP = ctx.player->GetMaxHp();
                pBar.displayHP = ctx.player->GetDisplayHp(); pBar.block = ctx.player->GetBlock();
                pBar.poisonDmg = ctx.player->GetBuffManager().GetTurnEndDamage().total();
                pBar.hasBurn = ctx.player->GetBuffManager().HasBuff(BuffType::Burn);

                m_spriteRenderer->Begin();
                DrawUnitStatusSprites(footX, footY, scale, false, pBar,
                    ctx.player->GetBuffManager(), ctx.mousePos, ctx.highlightTimer);
                m_spriteRenderer->End();

                m_textRenderer->Begin();
                DrawUnitStatusText(footX, footY, scale, false, pBar, ctx.player->GetBuffManager());
                m_textRenderer->End();
            }
        }

        m_spriteRenderer->End();
        m_textRenderer->Begin();

        for (auto enemy : *ctx.enemies)
        {
            float headX, headY, footX, footY;
            if (!GetEnemyScreenPos(enemy, ctx.renderer3D, headX, headY)) continue;
            if (!GetStatusFoot(enemy, ctx, footX, footY)) continue;

            float scale = 1.0f / ctx.cameraZoom;
            float barWidth = (enemy->IsBoss() ? 100.0f : 50.0f) * scale;
            float barHeight = (enemy->IsBoss() ? 15.0f : 10.0f) * scale;
            float barX = footX - barWidth / 2.0f;
            float barY = footY - 30.0f;
            float fontSize = max(8.0f, (enemy->IsBoss() ? 10.0f : 8.0f) * scale);

            HPBarInfo eBar;
            eBar.currentHP = enemy->GetHp(); eBar.maxHP = enemy->GetMaxHp();
            eBar.block = enemy->GetBlock();
            DrawUnitStatusText(footX, footY, scale, enemy->IsBoss(), eBar, enemy->GetBuffManager());

            // 行動の数値（頭上）
            const EnemyAction* action = enemy->GetNextAction();
            // 行動の数値（複合行動はすべて並べる）
            if (!EnemyIntentVisual::Hidden())   // 目隠し中は出さない
            {
                float iconSize = EnemyIntentVisual::ICON_SIZE;
                float ix = barX;
                float iy = barY - iconSize - 2.0f;

                for (auto& act : enemy->GetPlannedActions())
                    for (auto& e : act.effects)
                    {
                        if (!EnemyIntentVisual::ShouldShow(e)) continue;

                        if (EnemyIntentVisual::HasValue(e))
                        {
                            std::wstring shown = EnemyIntentVisual::GetDisplayText(e, enemy->GetBuffManager());

                            m_textRenderer->DrawText(shown.c_str(), ix + iconSize + 4.0f, iy + 2.0f, 14.0f,
                                D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f));
                            m_textRenderer->DrawText(shown.c_str(), ix + iconSize + 3.0f, iy + 1.0f, 14.0f,
                                D2D1::ColorF(D2D1::ColorF::White));
                        }

                        ix += EnemyIntentVisual::STEP;
                    }
            }

        }
}
