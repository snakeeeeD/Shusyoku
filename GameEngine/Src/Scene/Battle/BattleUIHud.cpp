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

// 左下の自分の状態・山札類のアイコン・バフの一覧・ターン終了ボタン・ターンのバナー

BattleUI::PlayerHudRects BattleUI::PlayerHud() const
{
    float sh = (float)m_screenHeight;
    PlayerHudRects r;
    r.panel = { 10.0f, sh - 172.0f, 280.0f, 102.0f };   // 山札・捨て札(sh-60)のすぐ上
    r.orb = { 20.0f, sh - 166.0f, 90.0f, 90.0f };
    r.hp = { 118.0f, sh - 150.0f, 160.0f, 26.0f };
    r.block = { 118.0f, sh - 116.0f, 34.0f, 34.0f };
    return r;
}

void BattleUI::DrawPlayerHud(const BattleUIContext& ctx)
{
    PlayerHudRects r = PlayerHud();
    Player* p = ctx.player;

    bool hasBlock = p->GetBlock() > 0;

    m_spriteRenderer->Begin();
    DrawWindow(r.panel.x, r.panel.y, r.panel.w, r.panel.h);

    // エナジー：カードのコストと同じオーブ（払う元と払う先を見た目でつなぐ）。0なら暗く
    m_spriteRenderer->DrawSprite(TextureManager::Get("ui_cost"), r.orb.x, r.orb.y, r.orb.w, r.orb.h, 0.0f,
        p->GetEnergy() == 0 ? XMFLOAT4(0.45f, 0.45f, 0.5f, 1.0f) : XMFLOAT4(1, 1, 1, 1));

    HPBarInfo bar;
    bar.currentHP = p->GetHp();
    bar.maxHP = p->GetMaxHp();
    bar.displayHP = p->GetDisplayHp();
    bar.block = p->GetBlock();
    bar.poisonDmg = p->GetBuffManager().GetTurnEndDamage().total();
    bar.hasBurn = p->GetBuffManager().HasBuff(BuffType::Burn);
    DrawHPBar(r.hp.x, r.hp.y, r.hp.w, r.hp.h, bar, ctx.highlightTimer);

    // ブロック：盾のアイコン（0の時も薄く出して欄を空けない）
    m_spriteRenderer->DrawSprite(TextureManager::Get("icon_block"), r.block.x, r.block.y, r.block.w, r.block.h, 0.0f,
        hasBlock ? XMFLOAT4(1, 1, 1, 1) : XMFLOAT4(0.4f, 0.4f, 0.45f, 0.8f));
    m_spriteRenderer->End();

    const D2D1::ColorF white(1, 1, 1), black(0, 0, 0);
    wchar_t buf[32];
    m_textRenderer->Begin();
    swprintf_s(buf, L"%d", p->GetEnergy());
    m_textRenderer->DrawOutlinedTextCentered(buf, r.orb.x + r.orb.w / 2.0f, r.orb.y, r.orb.w, r.orb.h - 10.0f,
        38.0f, white, black, 3.0f);
    swprintf_s(buf, L"/%d", p->GetMaxEnergy());
    m_textRenderer->DrawOutlinedTextCentered(buf, r.orb.x + r.orb.w / 2.0f, r.orb.y + r.orb.h * 0.62f, r.orb.w, 20.0f,
        15.0f, D2D1::ColorF(0.95f, 0.9f, 0.75f), black, 2.0f);

    swprintf_s(buf, L"%d / %d", p->GetHp(), p->GetMaxHp());
    m_textRenderer->DrawOutlinedTextCentered(buf, r.hp.x + r.hp.w / 2.0f, r.hp.y, r.hp.w, r.hp.h,
        17.0f, white, black, 2.0f);

    swprintf_s(buf, L"%d", p->GetBlock());
    m_textRenderer->DrawOutlinedTextCentered(buf, r.block.x + r.block.w / 2.0f, r.block.y, r.block.w, r.block.h,
        16.0f, hasBlock ? white : D2D1::ColorF(0.6f, 0.6f, 0.6f), black, 2.0f);
    m_textRenderer->DrawText(L"ブロック", r.block.x + r.block.w + 6.0f, r.block.y + 8.0f, 15.0f,
        hasBlock ? D2D1::ColorF(0.7f, 0.85f, 1.0f) : D2D1::ColorF(0.5f, 0.5f, 0.55f));
    m_textRenderer->End();
}

void BattleUI::DrawHPBar(float x, float y, float w, float h, const HPBarInfo& info, float time)
{
    HpBar::Draw(m_spriteRenderer, m_whiteTexture, x, y, w, h, info, time);
}

// 左下の自分の状態と、山札・捨て札・廃棄のアイコンとその説明（敵のUIより手前）。最後は文字の描画を閉じて戻る
void BattleUI::DrawPilesAndHud(const BattleUIContext& ctx)
{
        m_textRenderer->End();
        DrawPlayerHud(ctx);

        m_spriteRenderer->Begin();
        using PileFx::Pile;
        const float pileSW = (float)m_screenWidth, pileSH = (float)m_screenHeight;
        // 山のアイコン：弾む時は真ん中を中心に大きくする
        auto pileIcon = [&](const char* tex, float x, float y, float w, float h, Pile p, const XMFLOAT4& tint) {
            float k = PileFx::IconScale(p);
            m_spriteRenderer->DrawSprite(TextureManager::Get(tex), x - w * (k - 1) * 0.5f, y - h * (k - 1) * 0.5f, w * k, h * k, 0.0f, tint);
        };
        XMFLOAT2 dpPos = PileFx::PilePos(Pile::Draw, pileSW, pileSH);   // 左下
        float drawPileX = dpPos.x;
        float drawPileY = dpPos.y;
        float drawPileW = PileFx::PILE_W;
        float drawPileH = PileFx::PILE_H;
        bool hoverDrawPile = UiHover(drawPileX, drawPileY, drawPileW, drawPileH, ctx.mousePos, "drawpile", s_hoverKey);
        float dpy = hoverDrawPile ? drawPileY - 6.0f : drawPileY;
        XMFLOAT4 drawPileColor = hoverDrawPile ? XMFLOAT4(0.3f, 0.3f, 0.9f, 1.0f) : XMFLOAT4(0.2f, 0.2f, 0.6f, 1.0f);
        XMFLOAT4 dpTint = hoverDrawPile ? XMFLOAT4(1, 1, 1, 1) : XMFLOAT4(0.82f, 0.82f, 0.82f, 1);
        pileIcon("ui_draw", drawPileX, dpy, drawPileW, drawPileH, Pile::Draw, dpTint);
        // 山札バッジ（209の直後）
        m_spriteRenderer->DrawSprite(TextureManager::Get("particle"),
            drawPileX + drawPileW - 24.0f, dpy + drawPileH - 22.0f, 24.0f, 24.0f, 0.0f, XMFLOAT4(0.10f, 0.09f, 0.13f, 1.0f));

        XMFLOAT2 dcPos = PileFx::PilePos(Pile::Discard, pileSW, pileSH);   // 右下
        float discardX = dcPos.x;
        float discardY = dcPos.y;
        float discardW = PileFx::PILE_W;
        float discardH = PileFx::PILE_H;
        bool hoverDiscard = UiHover(discardX, discardY, discardW, discardH, ctx.mousePos, "discardpile", s_hoverKey);
        float ddy = hoverDiscard ? discardY - 6.0f : discardY;
        XMFLOAT4 discardColor = hoverDiscard ? XMFLOAT4(0.8f, 0.3f, 0.3f, 1.0f) : XMFLOAT4(0.5f, 0.2f, 0.2f, 1.0f);
        XMFLOAT4 dcTint = hoverDiscard ? XMFLOAT4(1, 1, 1, 1) : XMFLOAT4(0.82f, 0.82f, 0.82f, 1);
        pileIcon("ui_discard", discardX, ddy, discardW, discardH, Pile::Discard, dcTint);
        m_spriteRenderer->DrawSprite(TextureManager::Get("particle"),
            discardX + discardW - 24.0f, ddy + discardH - 22.0f, 24.0f, 24.0f, 0.0f, XMFLOAT4(0.10f, 0.09f, 0.13f, 1.0f));

        if (ctx.deck->GetExhaustPileCount() > 0)
        {
            XMFLOAT2 exPos = PileFx::PilePos(Pile::Exhaust, pileSW, pileSH);   // 右下（捨て札の左）
            float exhaustX = exPos.x;
            float exhaustY = exPos.y;
            float exhaustW = PileFx::PILE_W;
            float exhaustH = PileFx::PILE_H;
            bool hoverExhaust = UiHover(exhaustX, exhaustY, exhaustW, exhaustH, ctx.mousePos, "exhaustpile", s_hoverKey);
            float epy = hoverExhaust ? exhaustY - 6.0f : exhaustY;
            XMFLOAT4 exhaustColor = hoverExhaust ? XMFLOAT4(0.6f, 0.6f, 0.3f, 1.0f) : XMFLOAT4(0.4f, 0.4f, 0.2f, 1.0f);
            XMFLOAT4 exTint = hoverExhaust ? XMFLOAT4(1, 1, 1, 1) : XMFLOAT4(0.82f, 0.82f, 0.82f, 1);
            pileIcon("ui_exhaust", exhaustX, epy, exhaustW, exhaustH, Pile::Exhaust, exTint);
            m_spriteRenderer->DrawSprite(TextureManager::Get("particle"),
                exhaustX + exhaustW - 24.0f, epy + exhaustH - 22.0f, 24.0f, 24.0f, 0.0f, XMFLOAT4(0.10f, 0.09f, 0.13f, 1.0f));
        }

        PileFx::Draw(m_spriteRenderer, pileSW, pileSH);   // 光の帯と、着いた山の光の輪
        m_spriteRenderer->End();
        m_textRenderer->Begin();

        // 山札枚数（バッジ上・アウトライン）
        {
            wchar_t t[16]; swprintf_s(t, L"%d", ctx.deck->GetDrawPileCount());
            m_textRenderer->DrawOutlinedText(t, drawPileX + drawPileW - 18.0f, dpy + drawPileH - 20.0f, 17.0f,
                D2D1::ColorF(1, 1, 1), D2D1::ColorF(0, 0, 0), 2.0f);
        }

        // 捨て札枚数（バッジ上・アウトライン）
        {
            wchar_t t[16]; swprintf_s(t, L"%d", ctx.deck->GetDiscardPileCount());
            m_textRenderer->DrawOutlinedText(t, discardX + discardW - 18.0f, ddy + discardH - 20.0f, 17.0f,
                D2D1::ColorF(1, 1, 1), D2D1::ColorF(0, 0, 0), 2.0f);
        }

        if (ctx.deck->GetExhaustPileCount() > 0)
        {
            XMFLOAT2 ep = PileFx::PilePos(Pile::Exhaust, pileSW, pileSH);
            float ex2 = ep.x, ey2 = ep.y;
            bool he = PileFx::OnPile(Pile::Exhaust, pileSW, pileSH, (float)ctx.mousePos.x, (float)ctx.mousePos.y);
            float ty = he ? ey2 - 6.0f : ey2;                       // ホバーで浮く分
            wchar_t t[16]; swprintf_s(t, L"%d", ctx.deck->GetExhaustPileCount());   // ← 廃棄札の枚数
            m_textRenderer->DrawOutlinedText(t, ex2 + 50.0f - 18.0f, ty + 40.0f - 20.0f, 17.0f,
                D2D1::ColorF(1, 1, 1), D2D1::ColorF(0, 0, 0), 2.0f);
        }

        // パイルのホバー説明ウィンドウ
        const wchar_t* pileTip = nullptr;
        if (hoverDrawPile) pileTip = L"山札：これから引くカード";
        else if (hoverDiscard)  pileTip = L"捨て札：使ったカード（尽きたら戻る）";
        else if (ctx.deck->GetExhaustPileCount() > 0
            && PileFx::OnPile(Pile::Exhaust, pileSW, pileSH, (float)ctx.mousePos.x, (float)ctx.mousePos.y))
            pileTip = L"廃棄：このバトル中は戻らない";

        if (pileTip)
        {
            // 説明は、その山の上に出す（右下の山なら右に寄せる）
            float tw = 320.0f, th = 28.0f, ty = m_screenHeight - 100.0f;
            float tx = hoverDrawPile ? 20.0f : pileSW - tw - 20.0f;
            m_textRenderer->End();
            m_spriteRenderer->Begin();
            DrawWindow(tx - 6, ty - 6, tw + 12, th + 12);   // 枠分だけ広げてパディング確保
            m_spriteRenderer->End();
            m_textRenderer->Begin();                                 // テキスト再開（以降の描画はこのバッチで続く）
            m_textRenderer->DrawText(pileTip, tx + 10.0f, ty + 5.0f, 15.0f, D2D1::ColorF(1, 1, 1));
        }
        m_textRenderer->End();
}

// 自分のバフの一覧（名前と値・期限つきは残りターン。ホバーで説明）
void BattleUI::DrawPlayerBuffList(const BattleUIContext& ctx)
{
    float oy = RelicManager::BarExtraHeight(m_screenWidth);   // レリック折り返しぶん下げる
        const auto& buffs = ctx.player->GetBuffManager().GetBuffsSummed();
        float buffY = 90.0f + oy;
        // 名前と値（期限つきの分は残りターンも）。窓の幅は一番長い文に合わせる
        std::vector<std::wstring> buffTexts;
        float listW = 190.0f;
        for (auto& buff : buffs)
        {
            const auto& info = BuffInfo::Get(buff.type);
            bool perm = (buff.duration < 0 || buff.duration >= 999);   // 永続判定
            std::wstring s;
            if (buff.type == BuffType::Poison)
                s = info.name + L" " + std::to_wstring(buff.value);
            else if (BuffInfo::IsDurationBased(buff.type))
                s = info.name + (perm ? L" 永続" : L" " + std::to_wstring(buff.duration) + L"ターン");
            else
            {
                std::wstring note = BuffInfo::TurnsNote(ctx.player->GetBuffManager(), buff.type);
                s = info.name + L" " + std::to_wstring(buff.value) + (note.empty() ? L" 永続" : BuffInfo::TurnsSuffix(note));
            }
            float w = 20.0f + m_textRenderer->Measure(s.c_str(), 14.0f, FontRole::Body).width + 8.0f;   // アイコン＋文
            if (w > listW) listW = w;
            buffTexts.push_back(s);
        }

        // バフがある時だけ、後ろにウィンドウを敷いて見やすく
        if (!buffs.empty())
        {
            const float pad = 6.0f;
            // ホバー説明が挟まると以降が1行(22px)下がるので、その分も窓を伸ばす
            bool anyHover = (ctx.mousePos.x >= 20.0f && ctx.mousePos.x <= 20.0f + listW
                && ctx.mousePos.y >= buffY
                && ctx.mousePos.y <= buffY + (float)buffs.size() * 20.0f);
            float winX = 20.0f - pad;
            float winY = buffY - pad;
            float winW = listW + pad;
            float winH = (float)buffs.size() * 20.0f + (anyHover ? 22.0f : 0.0f) + pad * 2.0f;
            m_textRenderer->End();
            m_spriteRenderer->Begin();
            UiWindow::Draw(m_spriteRenderer, m_whiteTexture, winX, winY, winW, winH);
            m_spriteRenderer->End();
            m_textRenderer->Begin();
        }

        for (size_t bi = 0; bi < buffs.size(); bi++)
        {
        const auto& buff = buffs[bi];
        const auto& info = BuffInfo::Get(buff.type);
        bool buffHover = (ctx.mousePos.x >= 20.0f && ctx.mousePos.x <= 20.0f + listW
            && ctx.mousePos.y >= buffY && ctx.mousePos.y <= buffY + 20.0f);
        // アイコン
               // アイコン（実アイコン画像。無ければ従来の色四角）
        m_textRenderer->End();
        m_spriteRenderer->Begin();
        auto btex = TextureManager::Get(info.texture);
        if (btex)
            m_spriteRenderer->DrawSprite(btex, 20.0f, buffY, 16.0f, 16.0f, 0.0f,
                buffHover ? XMFLOAT4(1, 1, 1, 1) : XMFLOAT4(0.85f, 0.85f, 0.85f, 1));
        else
        {
            m_spriteRenderer->DrawSprite(m_whiteTexture, 20.0f, buffY, 16.0f, 16.0f, 0.0f,
                XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f));
            m_spriteRenderer->DrawSprite(m_whiteTexture, 21.0f, buffY + 1.0f, 14.0f, 14.0f, 0.0f,
                info.color);
        }
        m_spriteRenderer->End();
        m_textRenderer->Begin();
        // 名前と値（上で作った文）
        const std::wstring& buffText = buffTexts[bi];
        D2D1::ColorF textColor = buffHover
            ? D2D1::ColorF(1.0f, 1.0f, 0.5f)
            : D2D1::ColorF(0.6f, 1.0f, 0.6f);
        m_textRenderer->DrawText(buffText.c_str(), 40.0f, buffY, 14.0f, textColor);
        buffY += 20.0f;
        // ホバーで説明
        if (buffHover)
        {
            int descNum = BuffInfo::IsDurationBased(buff.type) ? buff.duration : buff.value;
            std::wstring desc = BuffInfo::GetDescription(buff.type, descNum);

            // 背景ウィンドウ
            float dw = 300.0f, dh = 20.0f;
            m_textRenderer->End();
            m_spriteRenderer->Begin();
            DrawWindow(34.0f, buffY - 4.0f, dw + 8.0f, dh + 8.0f);
            m_spriteRenderer->End();
            m_textRenderer->Begin();

            m_textRenderer->DrawText(desc.c_str(), 44.0f, buffY + 3.0f, 12.0f,
                D2D1::ColorF(0.85f, 0.85f, 0.85f));
            buffY += 22.0f;
        }
    }
}

// ターン終了ボタン
void BattleUI::DrawTurnEndButton(const BattleUIContext& ctx)
{
    if (ctx.battleResult == BattleResult::None && ctx.isPlayerTurn)
    {
        XMFLOAT4 er = PileFx::EndTurnRect((float)ctx.screenWidth, (float)ctx.screenHeight);   // 右下の捨て札の上
        float btnW = er.z;
        float btnH = er.w;
        float btnX = er.x;
        float btnY = er.y;

        bool hoverEnd = UiHover(btnX, btnY, btnW, btnH, ctx.mousePos, "turnend", s_hoverKey);
        float bey = hoverEnd ? btnY - 6.0f : btnY;
        XMFLOAT4 btnColor = hoverEnd ? XMFLOAT4(0.3f, 0.7f, 1.0f, 1.0f) : XMFLOAT4(0.2f, 0.5f, 0.8f, 1.0f);
        XMFLOAT4 teTint = hoverEnd ? XMFLOAT4(1, 1, 1, 1) : XMFLOAT4(0.85f, 0.85f, 0.85f, 1);
        // エナジーを使い切ったら、後ろを金色に脈動させて「次はここ」と知らせる
        if (ctx.player->GetEnergy() == 0)
        {
            float pl = 0.5f + 0.5f * sinf(ctx.highlightTimer * 6.0f);
            float g = 4.0f + 4.0f * pl;
            m_spriteRenderer->DrawSprite(m_whiteTexture, btnX - g, bey - g, btnW + g * 2, btnH + g * 2, 0.0f,
                XMFLOAT4(1.0f, 0.85f, 0.2f, 0.25f + 0.35f * pl));
        }
        m_spriteRenderer->DrawSprite(TextureManager::Get("ui_turnend"), btnX, bey, btnW, btnH, 0.0f, teTint);

        if (hoverEnd) {
            const wchar_t* tip = L"ターンを終了して敵の番へ";
            D2D1_SIZE_F ts = m_textRenderer->Measure(tip, 15.0f);   // 文字の実際の大きさで窓を作る
            float tx = btnX - 40.0f, ty = btnY - 40.0f, tw = ts.width + 16.0f, th = ts.height + 8.0f;
            m_spriteRenderer->End();
            m_spriteRenderer->Begin();
            DrawWindow(tx - 4.0f, ty - 4.0f, tw + 8.0f, th + 8.0f);
            m_textRenderer->DrawText(tip, tx + 8.0f, ty + 4.0f, 15.0f, D2D1::ColorF(1, 1, 1));
        }
    }
}

// ターンのバナー（戦闘開始・プレイヤーターン・敵のターン）。最前面＝全ステータスの後に描く
void BattleUI::DrawTurnBanner()
{
    if (TurnBanner::IsActive() && !CardTooltip::s_debugHide)   // 録画用に隠せる
    {
        float a = TurnBanner::GetAlpha();
        float ox = TurnBanner::GetSlideX();
        TurnBannerType t = TurnBanner::GetType();

        const wchar_t* txt; D2D1::ColorF col(1, 1, 1, a); float chars; float charW; float spacing;
        if (t == TurnBannerType::BattleStart) { txt = L"戦闘開始";         col = D2D1::ColorF(1.0f, 0.9f, 0.4f, a); chars = 4.0f; charW = 50.0f; spacing = 14.0f; }
        else if (t == TurnBannerType::Player) { txt = L"プレイヤーターン"; col = D2D1::ColorF(0.4f, 0.85f, 1.0f, a); chars = 8.0f; charW = 30.0f; spacing = 18.0f; }
        else { txt = L"敵のターン";       col = D2D1::ColorF(1.0f, 0.4f, 0.35f, a); chars = 5.0f; charW = 44.0f; spacing = 18.0f; }

        float fh = 64.0f;
        TextRenderer::RoleScope heading(*m_textRenderer, FontRole::Heading);   // 見出しのフォント
        auto charWidth = [&](wchar_t c) {                                      // 1文字ずつ実際の幅を測る
            wchar_t s[2] = { c, L'\0' };
            return m_textRenderer->Measure(s, fh, FontRole::Heading).width;
            };
        float totalW = 0.0f;
        for (int ci = 0; ci < (int)chars; ci++)
        {
            totalW += charWidth(txt[ci]);
            if (ci < (int)chars - 1) totalW += spacing;
        }
        float startX = m_screenWidth / 2.0f - totalW / 2.0f + ox;
        float y = m_screenHeight * 0.28f;
        float cx = startX;
        for (int ci = 0; ci < (int)chars; ci++)
        {
            wchar_t ch[2] = { txt[ci], L'\0' };
            float w = charWidth(txt[ci]);
            m_textRenderer->DrawOutlinedText(ch, cx, y, fh, col, D2D1::ColorF(0, 0, 0, a), 2.5f);
            cx += w + spacing;
        }
    }
}
