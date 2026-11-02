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
        fromEffect(d->onDiscardEffect); fromEffect(d->subEffect); fromEffect(d->allEnemyEffect);
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

    // カード上端中央あたりに解説パネルを出す。シーンDrawの最後（パス外）で呼ぶ
    static void Draw(SpriteRenderer* sr, TextRenderer* tr, ID3D11ShaderResourceView* white,
        const CardData* d, float ccx, float cty, float cardW, float cardH, int sw, int sh,
        float topLimit = 46.0f)
    {
        auto es = Entries(d);
        if (es.empty()) return;
        const float pw = 320.0f, rowH = 42.0f;
        float ph = 12.0f + es.size() * rowH;
        float px = ccx - pw / 2.0f;
        float py = cty - ph - 12.0f;
        if (px < 6.0f) px = 6.0f;
        if (px + pw > sw - 6.0f) px = sw - 6.0f - pw;
        if (py < topLimit) py = cty + cardH + 12.0f;           // 上に入らなければカードの下へ
        if (py + ph > sh - 6.0f) py = sh - 6.0f - ph;          // 下からもはみ出すなら画面内へ

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
        for (auto& [n, ds] : es)
        {
            tr->DrawText(n.c_str(), px + 12.0f, y, 16.0f, D2D1::ColorF(1.0f, 0.85f, 0.4f));
            tr->DrawText(ds.c_str(), px + 12.0f, y + 20.0f, 12.0f, D2D1::ColorF(0.9f, 0.9f, 0.9f));
            y += rowH;
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
                                       &d->onDiscardEffect, &d->subEffect, &d->allEnemyEffect };
        for (auto* e : es)
            if (e->type == CardEffectType::CreateCard && !e->cardId.empty())
                return CardDataBase::Get(e->cardId);
        return nullptr;
    }
};