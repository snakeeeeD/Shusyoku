#pragma once
#include "CardData.h"
#include "Player.h"
#include "SpriteRenderer.h"
#include "TextRenderer.h"
#include "RelicManager.h"
#include "Faith.h"
#include "RangeShape.h"
#include "TerrainDataBase.h"
#include "TextureManager.h"
#include "BuffInfo.h"
#include "GameUtils.h" 
#include "CardDamage.h"

#include <DirectXMath.h>
#include <string>
#include <set>
#include <algorithm>
#include <climits>
#include <unordered_map>

using namespace DirectX;

class CardVisual
{
public:
    static constexpr float CARD_W = 100.0f;
    static constexpr float CARD_H = 140.0f;

    static constexpr float CARD_SPACING = 75.0f;   // 手札の間隔
    static constexpr float CARD_HIDE_Y_OFFSET = 120.0f;  // 非ホバー時の手札の見える高さ
    static constexpr float CARD_HOVER_Y_OFFSET = 60.0f;   // ホバー時の追加リフト
    static constexpr float PLAY_LINE_OFFSET = 190.0f;    // 画面下からこの高さより上で離すとカードを使う（見た目と判定で共用）
    static constexpr float RESHUFFLE_FX_DUR = 0.6f;

    // 手札 index 番目の基準X（中央揃え）
    static float HandSlotX(int index, int handSize, float screenWidth)
    {
        return screenWidth / 2.0f - (handSize * CARD_SPACING) / 2.0f + index * CARD_SPACING;
    }

    static XMFLOAT4 GetCardColor(CardType type, bool hovered = false)
    {
        XMFLOAT4 color;
        switch (type)
        {
        case CardType::Attack: color = XMFLOAT4(0.9f, 0.2f, 0.2f, 1.0f); break;
        case CardType::Skill:  color = XMFLOAT4(0.2f, 0.4f, 1.0f, 1.0f); break;
        case CardType::Move:   color = XMFLOAT4(0.2f, 0.6f, 0.3f, 1.0f); break;
        case CardType::Power:  color = XMFLOAT4(0.8f, 0.2f, 1.0f, 1.0f); break;
        case CardType::Status: color = XMFLOAT4(0.45f, 0.40f, 0.50f, 1.0f); break;
        case CardType::Curse:  color = XMFLOAT4(0.30f, 0.08f, 0.14f, 1.0f); break;
        default:               color = XMFLOAT4(0.7f, 0.7f, 0.7f, 1.0f); break;
        }
        if (hovered) color = XMFLOAT4(0.2f, 0.4f, 0.8f, 1.0f);
        return color;
    }

    // カードの数字の色：元の値より上がった＝緑、下がった＝赤（バフ・デバフ・レリック、何で変わっても同じ）
    static inline const D2D1_COLOR_F NUM_UP = { 0.4f, 1.0f, 0.4f, 1.0f };
    static inline const D2D1_COLOR_F NUM_DOWN = { 1.0f, 0.4f, 0.3f, 1.0f };
    static D2D1_COLOR_F NumColor(int shown, int base, const D2D1_COLOR_F& normal, float alpha)
    {
        D2D1_COLOR_F c = (shown > base) ? NUM_UP : (shown < base) ? NUM_DOWN : normal;
        c.a = alpha;
        return c;
    }

    // 説明の数字1つ：見せる値（バフ・レリック込み）と、元の値（カードに書いてある値）
    struct ShownNum { int shown; int base; };
    struct ShownValues { ShownNum value, sub, onhit, hits; };
    // {value} {sub} {onhit} {hits} の見せる値と元の値。説明文と効果の札の両方がここを使う（同じ数字を出すため）
    static ShownValues GetShownValues(const CardData* d, const Player* player)
    {
        ShownValues v;
        v.value = { ActualMainValue(d, player), d->mainEffect.value };
        int sub = d->subEffect.value;
        v.sub = { sub + (d->subEffect.type == CardEffectType::Engrave ? Faith::EngraveBonus() : 0), sub };
        int oh = d->onHitEffect.value, ohShown = oh;
        if (d->onHitEffect.buffType == "Poison") ohShown += RelicManager::SumValue("poisonAdd");   // 毒の心得
        if (d->onHitEffect.type == CardEffectType::Engrave) ohShown += Faith::EngraveBonus();     // 聖印
        v.onhit = { ohShown, oh };
        int hits = (std::max)(1, d->hits);
        int hitsShown = (std::max)(1, d->hits + (d->type == CardType::Attack ? RelicManager::SumValue("multiHit") : 0));   // 連撃の刃など
        v.hits = { hitsShown, hits };
        return v;
    }

    // 説明の印（{value} など）を数字に置きかえる。元の値から変わった数字は、その範囲を覚える（上がった＝緑、下がった＝赤）
    static void ReplaceNum(std::wstring& s, const wchar_t* ph, int shown, int base, std::vector<TextSpan>& spans)
    {
        size_t p = s.find(ph);
        if (p == std::wstring::npos) return;
        size_t phLen = wcslen(ph);
        std::wstring num = std::to_wstring(shown);
        s.replace(p, phLen, num);
        int delta = (int)num.size() - (int)phLen;
        for (auto& sp : spans) if (sp.start > p) sp.start = (UINT32)((int)sp.start + delta);   // 後ろの範囲は、ずれた分だけ動かす
        if (shown != base)
            spans.push_back({ (UINT32)p, (UINT32)num.size(), shown > base ? NUM_UP : NUM_DOWN });
    }

    // N文字ごとに改行を差し込む（既存の\nはそのまま尊重）。spans があれば、改行を入れてずれた分だけ範囲も動かす
    static std::wstring WrapText(const std::wstring& s, int n, std::vector<TextSpan>* spans = nullptr)
    {
        if (n <= 0) return s;
        std::wstring out;
        std::vector<UINT32> at(s.size() + 1);   // 元の i 文字目が、折り返した後の何文字目になったか
        int lineLen = 0;
        for (size_t i = 0; i < s.size(); i++)
        {
            wchar_t c = s[i];
            if (c == L'\n') { at[i] = (UINT32)out.size(); out += c; lineLen = 0; continue; }
            bool noLineHead = (c == L'、' || c == L'。');   // 行頭に来させない文字（前の行にぶら下げる）
            if (lineLen >= n && !noLineHead) { out += L'\n'; lineLen = 0; }
            at[i] = (UINT32)out.size();
            out += c;
            lineLen++;
        }
        at[s.size()] = (UINT32)out.size();
        if (spans)
            for (auto& sp : *spans)
            {
                UINT32 e = (std::min)((UINT32)s.size(), sp.start + sp.length);
                sp.length = at[e] - at[sp.start]; sp.start = at[sp.start];
            }
        return out;
    }

    // 説明の {value} と効果の札に出す「実際の値」（攻撃UP・ブロックUP・レリックなどを足した値）
    static int ActualMainValue(const CardData* data, const Player* player)
    {
        int actualValue = data->mainEffect.value;
        if (player)
        {
            if (data->type == CardType::Attack)
            {
                // 罠の数を掛けるカードは、説明が「1つにつき」なので元の値のまま（上乗せは掛けた後の合計に1回だけ足す）
                if (!data->scaleByTrapCount)
                    actualValue = CardDamage::Compute(*data, *player, data->mainEffect.value);   // 実際の攻撃と同じ計算
            }
            else if (data->mainEffect.type == CardEffectType::Block)
                actualValue = player->GetBuffManager().GetFinalBlock(data->mainEffect.value);
            else if (data->mainEffect.type == CardEffectType::Engrave
                || data->mainEffect.type == CardEffectType::EngraveArea)
                actualValue += Faith::EngraveBonus();   // 聖印ぶんも表示
            else if (data->mainEffect.type == CardEffectType::PlaceTrap
                || data->mainEffect.type == CardEffectType::PlaceTrapArea)
            {
                const TerrainDef* def = TerrainDataBase::Get(data->mainEffect.trapType);
                if (def)
                {
                    if (def->effect == "Damage")
                        actualValue += RelicManager::SumValue("trapDamage");
                    else if (def->effect == "ApplyDebuff")
                        actualValue += RelicManager::SumValue("trapDebuff");
                }
            }
        }

        return actualValue;
    }

    // outSpans があれば、元の値から変わった数字の範囲（色つき）を返す
    static std::wstring GetEffectText(const CardData* data, const Player* player = nullptr, int wrapChars = 8,
        std::vector<TextSpan>* outSpans = nullptr)
    {
        if (!data) return L"";
        if (data->description.empty()) return L"(no desc)";

        ShownValues sv = GetShownValues(data, player);
        std::wstring result = data->description;
        std::vector<TextSpan> spans;
        auto put = [&](const wchar_t* ph, int shown, int base) { ReplaceNum(result, ph, shown, base, spans); };
        put(L"{value}", sv.value.shown, sv.value.base);
        put(L"{sub}", sv.sub.shown, sv.sub.base);
        put(L"{onhit}", sv.onhit.shown, sv.onhit.base);
        put(L"{onhitdur}", data->onHitEffect.duration, data->onHitEffect.duration);
        put(L"{onhit2}", data->onHitEffect2.value, data->onHitEffect2.value);
        put(L"{onhit2dur}", data->onHitEffect2.duration, data->onHitEffect2.duration);
        put(L"{hits}", sv.hits.shown, sv.hits.base);
        put(L"{ondiscard}", data->onDiscardEffect.value, data->onDiscardEffect.value);

        if (data->exhaust)
            result += L" \n[廃棄]";

        std::wstring wrapped = WrapText(result, wrapChars, &spans);
        if (outSpans) *outSpans = spans;
        return wrapped;
    }

    // 中心基準スケールの矩形
    static void GetRect(float baseX, float baseY, float scale,
        float& x, float& y, float& w, float& h)
    {
        w = CARD_W * scale; h = CARD_H * scale;
        x = baseX - (w - CARD_W) / 2.0f;
        y = baseY - (h - CARD_H) / 2.0f;
    }

    // ===== 効果の札（図の枠の左に縦に並べる。見た目が似たカードを見分けるため） =====
    // アイコンは敵の行動予告と同じ作り。value<0＝数字なし（hits>1 なら「×hits」だけ出す）
    static constexpr int NO_BASE = INT_MIN;   // 札の「元の値」が無い（数字が変わらない札）
    struct EffectBadge { const char* icon; int value; int hits; int base = NO_BASE; };   // base＝元の値（value と違えば、上がった＝緑・下がった＝赤）
    static constexpr float BADGE_X = 11.0f, BADGE_TOP = 35.0f, BADGE_AREA_H = 39.0f;   // 1倍のカードでの位置（図の枠の左側）
    static constexpr float BADGE_W = 25.0f, BADGE_H = 12.0f, BADGE_GAP = 1.5f;
    static float BadgeRowH(int n) { return n > 0 ? (std::min)(BADGE_H + BADGE_GAP, BADGE_AREA_H / (float)n) : 0.0f; }   // 多い時は縦に詰める

    static std::vector<EffectBadge> GetEffectBadges(const CardData* d, const Player* player)
    {
        std::vector<EffectBadge> v;
        if (!d || d->type == CardType::Status || d->type == CardType::Curse) return v;
        auto add = [&](const CardEffectData& e, int value, int hits, int base) {   // base＝元の値（バフ・レリックを足す前）
            if (!e.hasEffect) return;
            switch (e.type)
            {
            case CardEffectType::Damage:       v.push_back({ hits > 1 ? "intent_multihit" : "intent_attack", value, hits, base }); break;
            case CardEffectType::Block:        v.push_back({ "intent_block", value, 1, base }); break;
            case CardEffectType::Heal:         v.push_back({ "intent_heal", value, 1, base }); break;
            case CardEffectType::ApplyBuff:
            case CardEffectType::ApplyDebuff:
            {
                if (e.buffType.empty()) break;
                BuffType bt = StringToBuffType(e.buffType);
                int n = BuffInfo::IsDurationBased(bt) ? e.duration : value;      // 弱体・脱力などはターン数
                int nb = BuffInfo::IsDurationBased(bt) ? e.duration : base;      // 元の値
                const char* ic = (e.type == CardEffectType::ApplyBuff) ? "intent_buff" : "intent_debuff";
                if (bt == BuffType::Poison) { ic = "intent_poison"; n += RelicManager::SumValue("poisonAdd"); }   // 毒の心得（元の値には足さない）
                v.push_back({ ic, BuffInfo::HidesValue(bt) ? -1 : n, 1, BuffInfo::HidesValue(bt) ? NO_BASE : nb });
                break;
            }
            // 毒を使う
            case CardEffectType::Catalyst:     v.push_back({ "intent_poison", -1, value }); break;   // 毒を×倍
            case CardEffectType::DrainPoison:  v.push_back({ "intent_poison", -1, 1 }); v.push_back({ "intent_heal", -1, 1 }); break;
            case CardEffectType::PoisonBurst:  v.push_back({ "intent_poison", value + RelicManager::SumValue("poisonAdd"), 1, base }); break;
            // カードを動かす
            case CardEffectType::BlockDamage:  v.push_back({ "intent_blockdamage", -1, 1 }); break;
            case CardEffectType::Draw:         v.push_back({ "intent_draw", value, 1 }); break;
            case CardEffectType::Discard:      v.push_back({ "intent_discard", value, 1 }); break;
            case CardEffectType::AddEnergy:    v.push_back({ "intent_energy", value, 1 }); break;
            case CardEffectType::CreateCard:   v.push_back({ "intent_create", value, 1 }); break;
            case CardEffectType::Search:
            case CardEffectType::Salvage:      v.push_back({ "intent_search", -1, 1 }); break;
            case CardEffectType::UpgradeHand:  v.push_back({ "intent_upgrade", -1, 1 }); break;
            case CardEffectType::PlaceDecoy:   v.push_back({ "intent_decoy", -1, 1 }); break;
            // 罠・床（罠を置く札は、その罠の盤面の絵）
            case CardEffectType::PlaceTrap:
            case CardEffectType::PlaceTrapArea:
                if (const TerrainDef* t = TerrainDataBase::Get(e.trapType); t && !t->texture.empty())
                    v.push_back({ t->texture.c_str(), value, 1 });
                break;
            case CardEffectType::Detonate: case CardEffectType::DetonateAt:
            case CardEffectType::DetonateChain: case CardEffectType::RecallTraps:
            case CardEffectType::ClearFloors:  v.push_back({ "intent_hazard", -1, 1 }); break;
            // 信仰
            case CardEffectType::Engrave:
            case CardEffectType::EngraveArea:
            case CardEffectType::Purify:       v.push_back({ "intent_buff", value > 0 ? value : -1, 1, value > 0 ? base : NO_BASE }); break;
            case CardEffectType::Judgement:
            case CardEffectType::Miracle:      v.push_back({ "intent_buff", -1, value }); break;   // 信仰×倍
            case CardEffectType::Knockback:    v.push_back({ "intent_knockback", -1, 1 }); break;
            case CardEffectType::Pull:         v.push_back({ "intent_pull", -1, 1 }); break;
            default: break;
            }
        };
        auto addMove = [&]() { v.push_back({ "intent_move", d->range > 0 ? d->range : -1, 1 }); };   // 移動：何マスまで動けるか（敵の「移動」と同じ絵）
        if (d->type == CardType::Move) addMove();                // 移動カード：まず移動、その後ろにブロック・ドローなど
        ShownValues sv = GetShownValues(d, player);
        add(d->mainEffect, sv.value.shown, d->hits, sv.value.base);
        if (d->type != CardType::Move && d->dash) addMove();     // 突進する攻撃（突撃猛進・駆け抜け斬り）：攻撃の後ろに移動
        add(d->onHitEffect, d->onHitEffect.value, 1, d->onHitEffect.value);   // 毒の心得は add の中で足す（元の値は足す前）
        add(d->onHitEffect2, d->onHitEffect2.value, 1, d->onHitEffect2.value);
        add(d->subEffect, sv.sub.shown, 1, sv.sub.base);
        add(d->subEffect2, d->subEffect2.value, 1, d->subEffect2.value);
        add(d->allEnemyEffect, d->allEnemyEffect.value, 1, d->allEnemyEffect.value);
        if (d->selfDamage > 0) v.push_back({ "intent_selfdamage", d->selfDamage, 1 });   // 自傷（代償なので効果の後ろ）
        if (d->onDiscardEffect.hasEffect)   // 捨てられた時：目印の後ろに、その効果の札
        {
            v.push_back({ "intent_ondiscard", -1, 1 });
            add(d->onDiscardEffect, d->onDiscardEffect.value, 1, d->onDiscardEffect.value);
        }
        // 数値が0の札（毒塗りの「0ダメージ」など）は出さない。数字を出さない札（-1）は残す
        v.erase(std::remove_if(v.begin(), v.end(), [](const EffectBadge& b) { return b.value == 0; }), v.end());
        return v;
    }

    // スプライトパスで呼ぶ
    static void DrawBase(SpriteRenderer* sr, ID3D11ShaderResourceView* white,
        float baseX, float baseY, float scale, float rot, const XMFLOAT4& color,
        const CardData* data = nullptr, float time = 0.0f)
    {
        float x, y, w, h; GetRect(baseX, baseY, scale, x, y, w, h);

        float fw = 0.0f;
        XMFLOAT4 frameCol;
        bool hasFrame = (data != nullptr);
        if (hasFrame)
        {
            switch (data->rarity)
            {
            case CardRarity::Uncommon: frameCol = XMFLOAT4(0.1f, 0.8f, 0.85f, 1.0f); break;     // 水色

            case CardRarity::Rare:     frameCol = XMFLOAT4(1.0f, 0.85f, 0.2f, 1.0f); break;

            default:                   frameCol = XMFLOAT4(0.6f, 0.6f, 0.58f, 1.0f); break; // 白っぽい灰
            }
            frameCol.w = color.w;
            fw = 4.0f * scale;
        }

        if (hasFrame && rot == 0.0f)
        {
            // 回転なし（演出中など）：リング状に描いて中央を重ねない
            sr->DrawSprite(white, x, y, w, fw, 0.0f, frameCol);
            sr->DrawSprite(white, x, y + h - fw, w, fw, 0.0f, frameCol);
            sr->DrawSprite(white, x, y + fw, fw, h - fw * 2, 0.0f, frameCol);
            sr->DrawSprite(white, x + w - fw, y + fw, fw, h - fw * 2, 0.0f, frameCol);
        }
        else if (hasFrame)
        {
            sr->DrawSprite(white, x, y, w, h, rot, frameCol);   // 回転時は背面に全面（不透明なので問題なし）
        }

        // 本体（カードテクスチャをタイプ色でtint。未ロード時は従来の単色）
        auto cardTex = TextureManager::Get("ui_card");
        sr->DrawSprite(cardTex ? cardTex : white, x + fw, y + fw, w - fw * 2, h - fw * 2, rot, color);

        // レアはキラキラ
        if (data && data->rarity == CardRarity::Rare)
        {
            for (int i = 0; i < 5; i++)
            {
                float ph = time * 3.0f + i * 1.3f;
                float a = 0.5f + 0.5f * sinf(ph); 
                float sx = x + w * (0.12f + 0.19f * i);
                float sy = y + h * (0.15f + 0.6f * (0.5f + 0.5f * sinf(ph * 0.8f + i)));
                float ss = 5.0f * scale * a;
                sr->DrawSprite(white, sx, sy, ss, ss, 0.0f, XMFLOAT4(1.0f, 0.95f, 0.6f, a));
            }
        }

        // コストのオーブ
        if (data)
        {
            float px = x + w / 2.0f, py = y + h / 2.0f;
            float os = 30.0f * scale;
            float ocx = x + 4.0f * scale + os / 2.0f;   // 未回転時のオーブ中心
            float ocy = y + 4.0f * scale + os / 2.0f;
            float cs = cosf(rot), sn = sinf(rot);        // カード中心まわりに回す（回転手札対応）
            float rx = px + (ocx - px) * cs - (ocy - py) * sn;
            float ry = py + (ocx - px) * sn + (ocy - py) * cs;
            auto orb = TextureManager::Get("ui_cost");
            sr->DrawSprite(orb ? orb : white, rx - os / 2.0f, ry - os / 2.0f, os, os, rot,
                orb ? XMFLOAT4(1, 1, 1, color.w) : XMFLOAT4(0.15f, 0.13f, 0.25f, color.w));
        }
       
         // 状態異常/呪いは範囲無関係 → 専用画像を置く
        if (data && (data->type == CardType::Status || data->type == CardType::Curse))
        {
            auto mark = TextureManager::Get(data->type == CardType::Curse ? "card_mark_curse" : "card_mark_status");
            float foot = 34.0f * scale;
            float px = x + w / 2.0f, py = y + h / 2.0f;
            float ux = x + w / 2.0f;                      // 中央
            float uy = y + 39.0f * scale + foot / 2.0f;   // グリッドと同じ縦位置
            float cs = cosf(rot), sn = sinf(rot);
            float rcx = px + (ux - px) * cs - (uy - py) * sn;
            float rcy = py + (ux - px) * sn + (uy - py) * cs;
            if (mark)
                sr->DrawSprite(mark, rcx - foot / 2.0f, rcy - foot / 2.0f, foot, foot, rot, XMFLOAT4(1, 1, 1, color.w));
            else   // 画像未ロード時の保険（暗いパネル）
                sr->DrawSprite(white, rcx - foot / 2.0f, rcy - foot / 2.0f, foot, foot, rot, XMFLOAT4(0, 0, 0, 0.35f * color.w));
        }
        // 攻撃範囲のミニ盤面図
        else if (data)
        {
            bool selfOnly = (data->rangeType == RangeType::None);   // 効果が自分だけ
            bool hitAll = (data->rangeType == RangeType::Area       // 範囲全部に当たる（回転切り等）
                || data->rangeType == RangeType::Cone
                || data->pierce);
            float px = x + w / 2.0f, py = y + h / 2.0f;                       // カード中心
            int R = selfOnly ? 1 : (data->range < 1 ? 1 : (data->range > 4 ? 4 : data->range)); // 表示半径（射程に合わせる/最大4）
            int n = 2 * R + 1;
            float foot = 34.0f * scale;                                       // グリッド全体の幅（固定）
            float step = foot / n;
            float cell = step * 0.78f;                                        // セル（隙間を残す）
            float ux0 = x + (w - foot) / 2.0f;                                // 未回転グリッド左上
            float uy0 = y + 39.0f * scale;   
            float cs = cosf(rot), sn = sinf(rot);
            auto put = [&](float ucx, float ucy, float sz, const XMFLOAT4& col) {
                float rcx = px + (ucx - px) * cs - (ucy - py) * sn;          // カード中心まわりに回転
                float rcy = py + (ucx - px) * sn + (ucy - py) * cs;
                sr->DrawSprite(white, rcx - sz / 2.0f, rcy - sz / 2.0f, sz, sz, rot, col);
                };
            auto putR = [&](float ucx, float ucy, float sw, float sh, const XMFLOAT4& col) {
                float rcx = px + (ucx - px) * cs - (ucy - py) * sn;
                float rcy = py + (ucx - px) * sn + (ucy - py) * cs;
                sr->DrawSprite(white, rcx - sw / 2.0f, rcy - sh / 2.0f, sw, sh, rot, col);
                };
            auto inR = [&](int ddc, int ddr) {
                if (selfOnly) return false;
                if (data->type == CardType::Move)   // 移動＝マンハッタン距離R以内（BFSハイライトに合わせる）
                    return (ddc != 0 || ddr != 0) && (abs(ddc) + abs(ddr) <= R);
                return RangeShape::Contains(0, 0, ddc, ddr, data->rangeType, R, 0, 0, -1);
                };
            put(ux0 + foot / 2.0f, uy0 + foot / 2.0f, foot + 4.0f * scale,
                XMFLOAT4(0.0f, 0.0f, 0.0f, 0.35f * color.w));                 // 背景パネル
            for (int dr = -R; dr <= R; dr++)
                for (int dc = -R; dc <= R; dc++)
                {
                    float ucx = ux0 + (dc + R + 0.5f) * step;
                    float ucy = uy0 + (dr + R + 0.5f) * step;
                    XMFLOAT4 cc;
                    if (dc == 0 && dr == 0)
                        cc = XMFLOAT4(0.9f, 0.95f, 1.0f, color.w);           // プレイヤー
                    else if (inR(dc, dr))
                        cc = XMFLOAT4(1.0f, 0.85f, 0.30f, color.w);          // 攻撃範囲マス
                    else
                        cc = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.12f * color.w);   // 空マス
                    put(ucx, ucy, cell, cc);
                }
            if (hitAll)   // 全体攻撃：隣り合う範囲マスをバーで繋ぐ
            {
                const XMFLOAT4 link(1.0f, 0.85f, 0.30f, color.w);   // マスと同色
                for (int dr = -R; dr <= R; dr++)
                    for (int dc = -R; dc <= R; dc++)
                    {
                        if (!inR(dc, dr)) continue;
                        float cx = ux0 + (dc + R + 0.5f) * step;
                        float cy = uy0 + (dr + R + 0.5f) * step;
                        if (inR(dc + 1, dr))   // 右へ連結
                            putR(cx + step * 0.5f, cy, step * 0.30f, cell * 0.42f, link);
                        if (inR(dc, dr + 1))   // 下へ連結
                            putR(cx, cy + step * 0.5f, cell * 0.42f, step * 0.30f, link);
                    }
            }
        }
        // 効果の札：下地とアイコン（数値は DrawTexts で）。手札の傾きに合わせて、カードの中心まわりに回す
        if (data)
        {
            auto badges = GetEffectBadges(data, nullptr);
            float rowH = BadgeRowH((int)badges.size());
            float bcs = cosf(rot), bsn = sinf(rot), ccx = x + w / 2.0f, ccy = y + h / 2.0f;
            auto place = [&](float ux, float uy, float bw, float bh, ID3D11ShaderResourceView* tex, const XMFLOAT4& col) {
                float cx = ux + bw / 2.0f, cy = uy + bh / 2.0f;
                float rx = ccx + (cx - ccx) * bcs - (cy - ccy) * bsn, ry = ccy + (cx - ccx) * bsn + (cy - ccy) * bcs;
                sr->DrawSprite(tex, rx - bw / 2.0f, ry - bh / 2.0f, bw, bh, rot, col);
            };
            for (int i = 0; i < (int)badges.size(); i++)
            {
                float bx = x + BADGE_X * scale, by = y + (BADGE_TOP + i * rowH) * scale, bh = (rowH - BADGE_GAP) * scale;
                place(bx, by, BADGE_W * scale, bh, white, XMFLOAT4(0.0f, 0.0f, 0.0f, 0.55f * color.w));   // 下地
                float is = bh - 2.0f * scale;
                if (auto tex = TextureManager::Get(badges[i].icon))
                    place(bx + 1.0f * scale, by + 1.0f * scale, is, is, tex, XMFLOAT4(1, 1, 1, color.w));
            }
        }
    }

    // テキストパスで呼ぶ
    static void DrawTexts(TextRenderer* tr, const CardData* data, const Player* player,
        float baseX, float baseY, float scale, float rot, float alpha = 1.0f)
    {
        if (!data) return;
        float x, y, w, h; GetRect(baseX, baseY, scale, x, y, w, h);
        float px = x + w / 2.0f, py = y + h / 2.0f;
        float s = scale;
        const D2D1_COLOR_F ol = D2D1::ColorF(0.0f, 0.0f, 0.0f, alpha * 0.85f);   // 縁取り（カードが消える時は一緒に薄く）
        const float olT = 1.0f * s;                                              // 縁取りの太さ（カードの拡大に合わせる）

        bool upgraded = !data->id.empty() && data->id.back() == '+';
        D2D1_COLOR_F nameCol = upgraded
            ? D2D1::ColorF(0.4f, 1.0f, 0.5f, alpha)     // 強化：緑
            : D2D1::ColorF(1, 1, 1, alpha);
        // 名前はオーブの右へ
        // 名前（長いと自動縮小）。全角≒0.95 / 半角≒0.5 で概算し、カード幅に収める
        // 合成カードの名前「コア名【素材の略】」は2段に分ける（1段だとカードからはみ出すため）
        std::wstring nameMain = data->name, nameSub;
        if (size_t k = nameMain.find(L'【'); k != std::wstring::npos) { nameSub = nameMain.substr(k); nameMain.resize(k); }
        const float avail = w - 40.0f * s;          // オーブ右〜カード右端
        auto fitFont = [&](const std::wstring& str, float f, float minF) {   // 全角≒1.0 / 半角≒0.5 で概算して、幅に収まる大きさに（下限 minF）
            float est = 0.0f;
            for (wchar_t c : str) est += (c < 0x100 ? 0.5f : 1.0f) * f;
            if (est > avail && est > 0.0f) f *= avail / est;
            return f < minF ? minF : f;
        };
        {
            TextRenderer::RoleScope heading(*tr, FontRole::Heading);   // カード名は見出しのフォント
            if (nameSub.empty())
                tr->DrawOutlinedText(nameMain.c_str(), x + 34 * s, y + 11 * s, fitFont(nameMain, 13.0f * s, 7.0f * s), nameCol, ol, olT, rot, px, py);
            else
            {
                tr->DrawOutlinedText(nameMain.c_str(), x + 34 * s, y + 4 * s,  fitFont(nameMain, 11.0f * s, 7.0f * s), nameCol, ol, olT, rot, px, py);
                // 【素材の略】は補足なので、小さくなっても入りきるほうを優先（素材6つ＝13文字まで）
                tr->DrawOutlinedText(nameSub.c_str(),  x + 34 * s, y + 20 * s, fitFont(nameSub, 9.0f * s, 4.5f * s), nameCol, ol, olT, rot, px, py);
            }
        }

        // コストは左上オーブの上に「数字だけ」
        wchar_t cost[16];
        int shownCost = player ? player->CostOf(*data) : data->cost;   // 沼の上は増える
        swprintf_s(cost, L"%d", shownCost);
        float os = 30.0f * s;
        float ocx = x + 4.0f * s + os / 2.0f;
        float ocy = y + 4.0f * s + os / 2.0f;
        float fh = 20.0f * s;
        D2D1_SIZE_F cs = tr->Measure(cost, fh, FontRole::Body);          // 実際の数字の大きさで中央に
        tr->DrawOutlinedText(cost, ocx - cs.width / 2.0f, ocy - cs.height / 2.0f, fh,
            NumColor(data->cost, shownCost, D2D1::ColorF(1, 1, 1), alpha),   // コストは低い方が得：下がった＝緑・上がった＝赤（引数の順を逆にしている）
            ol, olT, rot, px, py);

        D2D1_COLOR_F dc = D2D1::ColorF(0.8f, 0.8f, 0.8f, alpha);   // 説明文はいつもの色。変わった数字だけ、下の範囲で緑・赤
        // 説明文：説明欄（幅約80・高さ約49、カード1倍の単位）に収まるまで文字を小さくする。
        // 小さくするたびに1行の文字数も増やして折り返し直す（行数そのものを減らす）
        const float DESC_W = 80.0f, DESC_H = 49.0f, LINE = 1.25f, MIN_FONT = 7.0f;
        const bool perm = data->permanent;   // 削除不可は説明の1行目に色を変えて出す（説明文のデータには書かない）
        // 決まった文字サイズは覚えておく（毎フレーム測り直さない）。キー＝最初の説明文（数値が変われば別のキー）
        static std::unordered_map<std::wstring, float> s_fit;
        std::wstring key = GetEffectText(data, player, (int)(DESC_W / 10.0f)) + (perm ? L"|p" : L"");
        float df = 10.0f;
        std::wstring desc;
        if (auto it = s_fit.find(key); it != s_fit.end())
        {
            df = it->second;
            desc = GetEffectText(data, player, (int)(DESC_W / df));
        }
        else
        {
            while (true)
            {
                desc = GetEffectText(data, player, (int)(DESC_W / df));   // 全角1文字≒文字サイズの幅
                // 高さは実際のフォントで測る（行の高さはフォントで決まり、文字サイズの1.25倍より大きい）
                float need = tr->Measure(desc.c_str(), df).height + (perm ? tr->Measure(L"削除不可", df).height : 0.0f);
                if (need <= DESC_H || df <= MIN_FONT) break;
                df -= 0.5f;
            }
            s_fit[key] = df;
        }
        std::vector<TextSpan> numSpans;   // 元の値から変わった数字（上がった＝緑・下がった＝赤）
        GetEffectText(data, player, (int)(DESC_W / df), &numSpans);
        for (auto& sp : numSpans) sp.color.a = alpha;
        float ty = y + 82 * s;
        if (perm)
        {
            tr->DrawOutlinedText(L"削除不可", x + 12 * s, ty, df * s,
                D2D1::ColorF(1.0f, 0.45f, 0.4f, alpha), ol, olT, rot, px, py);   // 赤系で目立たせる
            ty += tr->Measure(L"削除不可", df).height * s;
        }
        tr->DrawOutlinedText(desc.c_str(),
            x + 12 * s, ty, df * s, dc, ol, olT, rot, px, py, &numSpans);

        // 効果の札の数値（アイコンの右）。主な効果が増えている時は説明と同じく緑
        {
            auto badges = GetEffectBadges(data, player);
            float rowH = BadgeRowH((int)badges.size());
            for (int i = 0; i < (int)badges.size(); i++)
            {
                if (badges[i].value < 0 && badges[i].hits <= 1) continue;   // 数字なし
                float bh = rowH - BADGE_GAP, fs = bh * 0.8f * s;
                float tx = x + (BADGE_X + bh) * s, ty = y + (BADGE_TOP + i * rowH) * s + (bh * s - fs * 1.45f) / 2.0f;   // 行の高さ≒文字の1.45倍
                D2D1_COLOR_F nc = (badges[i].base == NO_BASE) ? D2D1::ColorF(1.0f, 0.96f, 0.8f, alpha)
                    : NumColor(badges[i].value, badges[i].base, D2D1::ColorF(1.0f, 0.96f, 0.8f), alpha);   // 上がった＝緑・下がった＝赤
                if (badges[i].value < 0)   // 「×倍」だけ（触媒・審判など）
                {
                    std::wstring hx = L"×" + std::to_wstring(badges[i].hits);
                    tr->DrawOutlinedText(hx.c_str(), tx, ty, fs, nc, ol, olT, rot, px, py);
                    continue;
                }
                std::wstring t = std::to_wstring(badges[i].value);
                tr->DrawOutlinedText(t.c_str(), tx, ty, fs, nc, ol, olT, rot, px, py);
                if (badges[i].hits > 1)   // 連撃：数値の右に小さく「×回数」
                {
                    std::wstring hx = L"×" + std::to_wstring(badges[i].hits);
                    tr->DrawOutlinedText(hx.c_str(), tx + tr->Measure(t.c_str(), fs).width, ty + fs * 0.35f, fs * 0.6f, nc, ol, olT, rot, px, py);
                }
            }
        }
    }

    // カードが参照するバフ/デバフのキーワード（名前, 説明）を重複なしで
    static std::vector<std::pair<std::wstring, std::wstring>> GetKeywords(const CardData* d)
    {
        std::vector<std::pair<std::wstring, std::wstring>> out;
        if (!d) return out;
        std::set<std::string> seen;
        auto add = [&](const CardEffectData& e) {
            if (e.buffType.empty() || seen.count(e.buffType)) return;
            seen.insert(e.buffType);
            BuffType bt = StringToBuffType(e.buffType);
            out.push_back({ BuffInfo::Get(bt).name, BuffInfo::GetDescription(bt, e.value) });
            };
        add(d->mainEffect); add(d->onHitEffect); add(d->onHitEffect2);
        add(d->onDiscardEffect); add(d->subEffect); add(d->subEffect2); add(d->allEnemyEffect);
        if (d->onDiscardEffect.hasEffect)   // 捨てられた時の効果：いつ発動するかを説明する
            out.push_back({ L"捨てられた時", L"このカードが、カードの効果で手札から選んで捨てられた時に発動する。\nターン終了で捨て札に行く時は発動しない。" });
        return out;
    }
};