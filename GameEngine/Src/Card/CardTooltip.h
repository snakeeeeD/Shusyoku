#pragma once
#include "SpriteRenderer.h"
#include "TextRenderer.h"
#include "UiWindow.h"
#include "CardData.h"
#include "CardDataBase.h"
#include "CardVisual.h"
#include "BuffInfo.h"
#include "TerrainDataBase.h"
#include "GameUtils.h"   // StringToBuffType
#include "GlossaryLink.h"
#include <vector>
#include <string>
#include <set>

class CardTooltip
{
public:
    // 効果タイプ（バフ以外の仕組み）の説明
    static bool TypeInfo(CardEffectType t, std::wstring& name, std::wstring& desc)
    {
        switch (t)
        {
        case CardEffectType::Damage:        name = L"ダメージ";   desc = L"対象にダメージを与える"; return true;
        case CardEffectType::Block:         name = L"ブロック";   desc = L"そのターン受けるダメージを防ぐ"; return true;
        case CardEffectType::Draw:          name = L"ドロー";     desc = L"山札からカードを引く"; return true;
        case CardEffectType::AddEnergy:     name = L"エナジー";   desc = L"エナジーを得る（カードのコスト）"; return true;
        case CardEffectType::Heal:          name = L"回復";       desc = L"HPを回復する"; return true;
        case CardEffectType::Knockback:     name = L"ノックバック"; desc = L"敵を突き飛ばす"; return true;
        case CardEffectType::Pull:          name = L"引き寄せ";   desc = L"敵を引き寄せる"; return true;
        case CardEffectType::PlaceTrap:
        case CardEffectType::PlaceTrapArea: name = L"罠設置";     desc = L"マスに罠を置く。敵が乗ると発動"; return true;
        case CardEffectType::Detonate:
        case CardEffectType::DetonateAt:
        case CardEffectType::DetonateChain: name = L"起爆";       desc = L"設置した罠を起爆する"; return true;
        case CardEffectType::RecallTraps:   name = L"罠回収";     desc = L"設置した罠を回収する"; return true;
        case CardEffectType::CreateCard:    name = L"生成";       desc = L"カードを手札/山札に加える"; return true;
        case CardEffectType::Search:        name = L"探索";     desc = L"山札からカードを探す"; return true;
        case CardEffectType::Salvage:       name = L"回収"; desc = L"捨札からカードを回収する"; return true;
        case CardEffectType::Discard:       name = L"捨てる";     desc = L"手札を捨てる"; return true;
        case CardEffectType::PlaceDecoy:    name = L"デコイ";     desc = L"囮を置き、敵の攻撃を引きつける"; return true;
        case CardEffectType::UpgradeHand:   name = L"手札強化";   desc = L"手札を一時的に強化する"; return true;
        case CardEffectType::Catalyst:      name = L"触媒";       desc = L"付与された毒を倍化する"; return true;
        case CardEffectType::DrainPoison:   name = L"毒吸収";     desc = L"敵の毒を奪う"; return true;
        case CardEffectType::PoisonBurst:   name = L"毒爆発";     desc = L"毒を即時ダメージに変える"; return true;
        case CardEffectType::Engrave:
        case CardEffectType::EngraveArea:   name = L"刻む";       desc = L"マスに信仰を刻む。信仰のあるマスは聖地、10で聖域になる"; return true;
        case CardEffectType::Judgement:     name = L"審判";       desc = L"聖地に立つ敵に、そのマスの信仰に応じたダメージ"; return true;
        case CardEffectType::Miracle:       name = L"奇跡";       desc = L"足元の信仰を全て払い、払った分に応じたダメージ"; return true;
        case CardEffectType::Purify:        name = L"浄化";       desc = L"手札の呪いを廃棄する"; return true;
        default: return false;
        }
    }

    // カードが持つキーワード（名前, 説明）を重複なしで集める
    static std::vector<std::pair<std::wstring, std::wstring>> Entries(const CardData* d)
    {
        std::vector<std::pair<std::wstring, std::wstring>> out;
        if (!d) return out;
        std::set<std::wstring> seen;
        auto push = [&](const std::wstring& n, const std::wstring& ds) {
            if (n.empty() || seen.count(n)) return; seen.insert(n); out.push_back({ n, ds });
            };
        auto fromEffect = [&](const CardEffectData& e) {
            if (!e.buffType.empty()) { BuffType bt = StringToBuffType(e.buffType); push(BuffInfo::Get(bt).name, BuffInfo::GetDescription(bt, e.value)); }
            if (!e.trapType.empty())
            {
                push(L"罠", L"床に罠を置く。踏むと発動する");
                const TerrainDef* t = TerrainDataBase::Get(e.trapType);
                if (t && !t->buffType.empty())   // 罠が付与するデバフ（毒/Burn等）
                {
                    BuffType bt = StringToBuffType(t->buffType);
                    push(BuffInfo::Get(bt).name, BuffInfo::GetDescription(bt, t->value));
                }
            }
            std::wstring n, ds; if (TypeInfo(e.type, n, ds)) push(n, ds);
            };
        fromEffect(d->mainEffect); fromEffect(d->onHitEffect); fromEffect(d->onHitEffect2);
        fromEffect(d->onDiscardEffect); fromEffect(d->subEffect); fromEffect(d->subEffect2); fromEffect(d->allEnemyEffect);
        if (d->exhaust)       push(L"廃棄", L"使うと除外され、この戦闘中は戻らない");
        if (d->dash)          push(L"移動", L"使用時に移動もできる");
        if (d->pierce)        push(L"貫通", L"敵を貫通して攻撃できる");
        if (d->hits > 1)      push(L"連撃", L"複数回ヒットする");
        if (d->selfDamage > 0)push(L"自傷", L"自分もダメージを受ける");
        if (!d->endTurnAddCard.empty()) push(L"増殖", L"ターン終了時に手札にあると、横のカードを捨て札に加える");
        if (d->hideIntent)    push(L"目くらまし", L"手札にある間、敵の行動予告が見えなくなる");
        if (d->permanent)     push(L"削除不可", L"ショップやイベントで削除・変化できない");
        return out;
    }

    // 罠の効果の説明（カードの横の表示とマスのホバーで共通）
    static std::vector<std::wstring> TrapLines(const TerrainDef* t, int value)
    {
        std::vector<std::wstring> out;
        wchar_t buf[64];
        if (t->effect == "Damage")
        {
            swprintf_s(buf, L"ダメージ: %d", value + (t->persistent ? 0 : RelicManager::SumValue("trapDamage")));
            out.push_back(buf);
            if (t->aoe)
            {
                swprintf_s(buf, L"周囲のマス: %d", value / 2);
                out.push_back(buf);
            }
        }
        else if (t->effect == "Slide")
            out.push_back(L"移動方向に滑る");
        else if (t->effect == "WhileOn")
        {
            swprintf_s(buf, L"立っている間 移動カードのコスト+%d", value);
            out.push_back(buf);
        }
        else
        {
            int extra = t->persistent ? 0 : RelicManager::SumValue("trapDebuff");
            BuffType bt = StringToBuffType(t->buffType);
            const auto& info = BuffInfo::Get(bt);
            if (BuffInfo::IsDurationBased(bt))
                swprintf_s(buf, L"%s: %dT", info.name.c_str(), value + extra);
            else
                swprintf_s(buf, L"%s: %d (%dT)", info.name.c_str(), value + extra, t->buffDuration + extra);
            out.push_back(buf);
        }
        out.push_back(t->persistent ? L"踏んでも消えない" : L"踏むと消える");
        return out;
    }

    static inline bool s_debugHide = false;   // 録画用：カード・敵・マスの説明を出さない（Debug 窓で切り替え）

    // ---- 説明を残す（敵の詳細ウィンドウと同じ：前のフレームで出ていた説明の上にマウスがある間は出し続ける）----
    static void SetMouse(float x, float y, bool click) { s_mx = x; s_my = y; s_click = click; }
    static bool MouseInHold()    // 説明・すき間・カードのどこか（ここにある間は説明を残す）
    {
        return s_hasHeld && s_mx >= s_hold[0] && s_mx <= s_hold[2] && s_my >= s_hold[1] && s_my <= s_hold[3];
    }
    static bool MouseInPanel()   // 説明のウィンドウの上（ここのクリックは説明が使う）
    {
        return s_hasHeld && s_mx >= s_panel[0] && s_mx <= s_panel[2] && s_my >= s_panel[1] && s_my <= s_panel[3];
    }
    static void ClearHeld() { s_hasHeld = false; s_drawn = false; s_heldSrc = nullptr; }
    // フレームの最後に呼ぶ：このフレームで出ていなくても、マウスが説明の上なら出し続ける
    static void EndFrame(SpriteRenderer* sr, TextRenderer* tr, ID3D11ShaderResourceView* white)
    {
        if (!s_drawn && MouseInHold())
            Draw(sr, tr, white, &s_held, s_args[0], s_args[1], s_args[2], s_args[3],
                (int)s_args[4], (int)s_args[5], s_args[6]);
        else if (!s_drawn) s_hasHeld = false;
        s_drawn = false;
        s_click = false;
    }

    // カード上端中央あたりに解説パネルを出す。シーンDrawの最後（パス外）で呼ぶ
    static void Draw(SpriteRenderer* sr, TextRenderer* tr, ID3D11ShaderResourceView* white,
        const CardData* d, float ccx, float cty, float cardW, float cardH, int sw, int sh,
        float topLimit = 46.0f)
    {
        if (s_debugHide) return;                  // 録画用に隠す
        auto es = Entries(d);
        if (es.empty()) { s_hasHeld = false; return; }   // 説明のないカード：前のカードの説明を残さない
        const float pw = 320.0f, textW = pw - 24.0f;
        std::vector<float> rowH;                                  // 行ごとの高さ（説明の折り返しに合わせる）
        float ph = 12.0f;
        for (auto& [n, ds] : es)
        {
            float h = 20.0f + tr->Measure(ds.c_str(), 12.0f, FontRole::Body, textW).height + 8.0f;
            rowH.push_back(h); ph += h;
        }
        float px = ccx - pw / 2.0f;
        float py = cty - ph - 12.0f;
        if (px < 6.0f) px = 6.0f;
        if (px + pw > sw - 6.0f) px = sw - 6.0f - pw;
        if (py < topLimit) py = cty + cardH + 12.0f;           // 上に入らなければカードの下へ
        if (py + ph > sh - 6.0f) py = sh - 6.0f - ph;          // 下からもはみ出すなら画面内へ

        // 説明を残すための記録（カードとの間のすき間も「上にある」とみなす）
        if (d != &s_held && (d != s_heldSrc || d->id != s_held.id)) { s_held = *d; s_heldSrc = d; }   // 別のカードになった時だけコピー
        s_hasHeld = true; s_drawn = true;
        float args[7] = { ccx, cty, cardW, cardH, (float)sw, (float)sh, topLimit };
        for (int i = 0; i < 7; i++) s_args[i] = args[i];
        float cl = ccx - cardW / 2.0f, cr = ccx + cardW / 2.0f;     // カード自体も含める（浮いたカードの上でも消えない）
        float top = (py < cty) ? py : cty, bottom = (py + ph > cty + cardH) ? py + ph : cty + cardH;
        s_hold[0] = ((px < cl) ? px : cl) - 8.0f; s_hold[1] = top - 8.0f;
        s_hold[2] = ((px + pw > cr) ? px + pw : cr) + 8.0f; s_hold[3] = bottom + 8.0f;
        s_panel[0] = px; s_panel[1] = py; s_panel[2] = px + pw; s_panel[3] = py + ph;
        bool inPanel = s_mx >= px && s_mx <= px + pw && s_my >= py && s_my <= py + ph;

        // 生成先カード（ホバー中カードの横に、少し大きめ）
        const CardData* gen = GeneratedCard(d);
        const float gs = 0.85f;
        float gw = CardVisual::CARD_W * gs;
        float gx = ccx + cardW / 2.0f + 14.0f;   // カードの右
        float gy = cty;
        if (gen && gx + gw > sw - 6.0f) gx = ccx - cardW / 2.0f - gw - 14.0f;   // 右に入らなければ左

        // 罠カード：置かれる罠の見た目と効果（カードの横）
        const TerrainDef* trap = d->mainEffect.trapType.empty() ? nullptr : TerrainDataBase::Get(d->mainEffect.trapType);
        std::vector<std::wstring> trapLines;
        if (trap) trapLines = TrapLines(trap, d->mainEffect.value);
        const float tw = 170.0f, th = 146.0f + trapLines.size() * 16.0f;
        float tx = ccx + cardW / 2.0f + 14.0f;
        if (tx + tw > sw - 6.0f) tx = ccx - cardW / 2.0f - tw - 14.0f;
        float ty = cty;
        if (ty + th > sh - 6.0f) ty = sh - 6.0f - th;

        sr->Begin();
        UiWindow::Draw(sr, white, px, py, pw, ph);
        if (gen) CardVisual::DrawBase(sr, white, gx, gy, gs, 0.0f, CardVisual::GetCardColor(gen->type), gen, 0.0f);
        if (trap)
        {
            UiWindow::Draw(sr, white, tx, ty, tw, th);
            if (auto tex = TextureManager::Get(trap->texture))
                sr->DrawSprite(tex, tx + (tw - 96.0f) / 2.0f, ty + 36.0f, 96.0f, 96.0f, 0.0f, XMFLOAT4(1, 1, 1, 1));
        }
        sr->End();

        tr->Begin();
        float y = py + 10.0f;
        for (int row = 0; row < (int)es.size(); row++)
        {
            auto& [n, ds] = es[row];
            tr->DrawText(n.c_str(), px + 12.0f, y, 16.0f, D2D1::ColorF(1.0f, 0.85f, 0.4f));
            auto hits = GlossaryLink::Find(ds, n);                    // 用語は色付き
            int hov = inPanel ? tr->HitTestSpans(ds.c_str(), px + 12.0f, y + 20.0f, textW, 12.0f,
                GlossaryLink::Spans(hits), s_mx, s_my) : -1;
            if (hov >= 0 && s_click && GlossaryLink::open)           // クリックで用語集のそのページへ
            {
                GlossaryLink::open(hits[hov].id);
                s_click = false;
            }
            tr->DrawTextSpans(ds.c_str(), px + 12.0f, y + 20.0f, textW, 12.0f, D2D1::ColorF(0.9f, 0.9f, 0.9f),
                GlossaryLink::Spans(hits, hov));
            y += rowH[row];
        }
        if (gen) CardVisual::DrawTexts(tr, gen, nullptr, gx, gy, gs, 0.0f, 1.0f);
        if (trap)
        {
            tr->DrawText(trap->name.c_str(), tx + 14.0f, ty + 10.0f, 16.0f, D2D1::ColorF(1.0f, 0.85f, 0.4f));
            for (int i = 0; i < (int)trapLines.size(); i++)
                tr->DrawText(trapLines[i].c_str(), tx + 14.0f, ty + 140.0f + i * 16.0f, 12.0f, D2D1::ColorF(0.9f, 0.9f, 0.9f));
        }
        tr->End();

    }

    static const CardData* GeneratedCard(const CardData* d)
    {
        if (!d) return nullptr;
        if (!d->endTurnAddCard.empty()) return CardDataBase::Get(d->endTurnAddCard);   // 呪い・増殖などが加えるカード
        const CardEffectData* es[] = { &d->mainEffect, &d->onHitEffect, &d->onHitEffect2,
                                       &d->onDiscardEffect, &d->subEffect, &d->subEffect2, &d->allEnemyEffect };
        for (auto* e : es)
            if (e->type == CardEffectType::CreateCard && !e->cardId.empty())
                return CardDataBase::Get(e->cardId);
        return nullptr;
    }
private:
    static inline float s_mx = -1.0f, s_my = -1.0f;
    static inline bool s_click = false;
    static inline CardData s_held;                  // 残している説明のカード（コピー）
    static inline const CardData* s_heldSrc = nullptr;   // コピー元（同じカードなら写し直さない）
    static inline bool s_hasHeld = false, s_drawn = false;
    static inline float s_args[7] = {};             // Draw の位置の引数
    static inline float s_hold[4] = {};             // 説明を残す範囲（左上・右下）
    static inline float s_panel[4] = {};            // 説明のウィンドウ（左上・右下）
};