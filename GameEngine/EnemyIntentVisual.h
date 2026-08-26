#pragma once
#include "EnemyAction.h"
#include "BuffManager.h"
#include "TextureManager.h"
#include "SpriteRenderer.h"
#include "BuffInfo.h"
#include "GameUtils.h"

#include <DirectXMath.h>
#include <set>
#include <vector>
#include <utility>

using namespace DirectX;

// 敵の行動予告の「見せ方」を1箇所に集約
class EnemyIntentVisual
{
public:
    // 予告に出す効果か
    // 予告に出す効果か（アイコンを出す）
    static bool ShouldShow(const Effect& e)
    {
        return e.value > 0 || !e.buff.empty()
            || e.kind == EffectKind::MoveToward
            || e.kind == EffectKind::MoveAway
            || e.kind == EffectKind::PullPlayer
            || e.kind == EffectKind::KnockbackPlayer
            || e.kind == EffectKind::AddCard;
    }

    // 数値を出す効果か
    static bool HasValue(const Effect& e)
    {
        if (e.kind == EffectKind::MoveToward || e.kind == EffectKind::MoveAway) return false;
        if (e.kind == EffectKind::AddCard) return e.value > 0;
        return e.value > 0;
    }

    static XMFLOAT4 GetIconColor(const Effect& e)
    {
        switch (e.kind)
        {
        case EffectKind::Damage:          return XMFLOAT4(0.9f, 0.2f, 0.2f, 1.0f);
        case EffectKind::Block:           return XMFLOAT4(0.2f, 0.4f, 0.9f, 1.0f);
        case EffectKind::Buff:            return XMFLOAT4(1.0f, 0.8f, 0.0f, 1.0f);
        case EffectKind::Debuff:          return XMFLOAT4(0.6f, 0.0f, 0.8f, 1.0f);
        case EffectKind::MoveToward:
        case EffectKind::MoveAway:        return XMFLOAT4(0.2f, 0.8f, 0.3f, 1.0f);
        case EffectKind::PullPlayer:      return XMFLOAT4(0.2f, 0.8f, 0.3f, 1.0f);
        case EffectKind::KnockbackPlayer: return XMFLOAT4(0.2f, 0.8f, 0.3f, 1.0f);
        case EffectKind::Summon:          return XMFLOAT4(0.4f, 0.85f, 0.4f, 1.0f);
        case EffectKind::AddCard:         return XMFLOAT4(0.6f, 0.4f, 0.8f, 1.0f);
        default:                          return XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
        }
    }

    // バフ適用後の表示値
    static int GetDisplayValue(const Effect& e, const BuffManager& buffs)
    {
        if (e.kind == EffectKind::Damage) return buffs.GetFinalAttack(e.value);
        if (e.kind == EffectKind::Block)  return buffs.GetFinalBlock(e.value);
        return e.value;
    }

    // 説明文の {0} {1} ... を effects[i] のバフ適用後の値に差し替える
    static std::wstring GetActionText(const EnemyAction& a, const BuffManager& buffs)
    {
        std::wstring result = a.description;
        for (int i = 0; i < (int)a.effects.size(); i++)
        {
            std::wstring key = L"{" + std::to_wstring(i) + L"}";
            std::wstring val = std::to_wstring(GetDisplayValue(a.effects[i], buffs));
            size_t pos;
            while ((pos = result.find(key)) != std::wstring::npos)
                result.replace(pos, key.size(), val);
        }
        return result;
    }

    // 攻撃範囲/接近/必中を日本語に
    static std::wstring RangeText(const TargetSpec& tg) 
    {
        // 突進は「経路の直線」が脅威。rangeTypeは使われないので専用表記にする
        if (tg.approach == ApproachType::Dash) {
            std::wstring s = L"突進";
            if (tg.moveRange > 0) s += L" 最大" + std::to_wstring(tg.moveRange) + L"マス";
            if (tg.unavoidable) s += L" / 必中";
            return s;
        }

        std::wstring s;
        switch (tg.rangeType) {
        case RangeType::Adjacent: s = L"隣接"; break;
        case RangeType::Cross:         s = L"十字"; break;
        case RangeType::Area:          s = L"範囲"; break;
        case RangeType::Diamond:       s = L"ひし形"; break;
        case RangeType::DiagonalCross: s = L"八方"; break;
        case RangeType::Line:          s = L"直線"; break;
        case RangeType::Cone:          s = L"扇"; break;
        default:                       return L"";   // None
        }
        if (tg.range > 0) s += L" 射程" + std::to_wstring(tg.range);
        if (tg.approach == ApproachType::Toward) s += L" / 接近";
        if (tg.unavoidable) s += L" / 必中";
        return s;
    }

    // アイコン1個ぶんの説明（タイトル＋本文。本文は最大2行、区切りは\n）
    static void GetEffectDesc(const Effect& e, const EnemyAction& a, const BuffManager& buffs,
        std::wstring& title, std::wstring& body)
    {
        int v = GetDisplayValue(e, buffs);
        switch (e.kind)
        {
        case EffectKind::Damage:
            title = (e.hits > 1) ? L"連続攻撃" : L"攻撃";
            if (e.hits > 1)
                body = std::to_wstring(v) + L" ダメージ ×" + std::to_wstring(e.hits)
                + L"\n合計 " + std::to_wstring(v * e.hits) + L" ダメージ";
            else
                body = std::to_wstring(v) + L" ダメージ";
            { std::wstring r = RangeText(a.target); if (!r.empty()) body += L"\n範囲: " + r; }
            break;
        case EffectKind::Block:
            title = L"防御"; body = L"自身に " + std::to_wstring(v) + L" ブロック"; break;
        case EffectKind::Buff:
        {
            BuffType bt = StringToBuffType(e.buff);
            title = BuffInfo::Get(bt).name;
            body = L"自身: " + BuffInfo::GetDescription(bt, e.value);
            break;
        }
        case EffectKind::Debuff:
        {
            BuffType bt = StringToBuffType(e.buff);
            title = BuffInfo::Get(bt).name;
            body = L"プレイヤーに付与\n" + BuffInfo::GetDescription(bt, e.value);
            break;
        }
        case EffectKind::MoveToward:      title = L"接近"; body = std::to_wstring(e.value) + L" マス近づく"; break;
        case EffectKind::MoveAway:        title = L"後退"; body = std::to_wstring(e.value) + L" マス離れる"; break;
        case EffectKind::PullPlayer:      title = L"引き寄せ"; body = L"プレイヤーを " + std::to_wstring(e.value) + L" マス引き寄せる"; break;
        case EffectKind::KnockbackPlayer: title = L"突き飛ばし"; body = L"プレイヤーを " + std::to_wstring(e.value) + L" マス突き飛ばす"; break;
        case EffectKind::Summon:          title = L"召喚"; body = L"仲間を " + std::to_wstring(e.value) + L" 体呼ぶ"; break;
        case EffectKind::Coil:
            title = L"とぐろ";
            body = L"中央へ巻きつつ " + std::to_wstring(e.value) + L" マス伸びる\n体に押し込まれると轢かれる";
            break;
        case EffectKind::AddCard:
        {
            const wchar_t* where = (e.buff == "hand") ? L"手札" : (e.buff == "discard") ? L"捨て札" : L"山札";
            title = L"お邪魔生成";
            body = std::wstring(where) + L"に お邪魔カードを " + std::to_wstring(e.value) + L" 枚追加";
            break;
        }
        default: title = L""; body = L""; break;
        }
    }

    // 行動に含まれるキーワード（名前, 説明）を重複なく集める（カードのGetKeywords相当）
    static std::vector<std::pair<std::wstring, std::wstring>> GetKeywords(const EnemyAction& a)
    {
        std::vector<std::pair<std::wstring, std::wstring>> out;
        std::set<std::wstring> seen;
        auto push = [&](const std::wstring& n, const std::wstring& d) {
            if (n.empty() || seen.count(n)) return; seen.insert(n); out.push_back({ n, d });
            };

        const TargetSpec& tg = a.target;
        if (tg.approach == ApproachType::Dash)
            push(L"突進", L"狙った方向へ突っ込み、経路上のマスを攻撃する");
        if (tg.unavoidable)
            push(L"必中", L"位置に関係なく必ず命中する");

        for (auto& e : a.effects)
        {
            if ((e.kind == EffectKind::Buff || e.kind == EffectKind::Debuff) && !e.buff.empty())
            {
                BuffType bt = StringToBuffType(e.buff);
                push(BuffInfo::Get(bt).name, BuffInfo::GetDescription(bt, e.value));
            }
            switch (e.kind)
            {
            case EffectKind::Damage:          if (e.hits > 1) push(L"連撃", L"1回の行動で複数回ヒットする"); break;
            case EffectKind::PullPlayer:      push(L"引き寄せ", L"プレイヤーを敵の方へ引き寄せる"); break;
            case EffectKind::KnockbackPlayer: push(L"ノックバック", L"プレイヤーを突き飛ばす"); break;
            case EffectKind::Summon:          push(L"召喚", L"新たな敵を呼び出す"); break;
            case EffectKind::Hazard:          push(L"地形", L"マスに危険地形を設置する。踏むとやけど"); break;
            case EffectKind::Coil:            push(L"とぐろ", L"体が伸び、通り道のプレイヤーを押し出す"); break;
            case EffectKind::AddCard:         push(L"お邪魔", L"デッキ・手札・捨て札にお邪魔カードを加えてくる"); break;
            default: break;
            }
        }
        return out;
    }

    // この行動はプレイヤーを害するか（危険表示の対象か）
    static bool IsHarmful(const EnemyAction& a)
    {
        for (auto& e : a.effects)
        {
            if (e.kind == EffectKind::Damage) return true;
            if (e.kind == EffectKind::Debuff && e.applyTo == ApplyTo::Player) return true;
        }
        return false;
    }

    // この行動がプレイヤーに与えるダメージ合計（バフ適用後）
    static int GetTotalDamage(const EnemyAction& a, const BuffManager& buffs)
    {
        int sum = 0;
        for (auto& e : a.effects)
            if (e.kind == EffectKind::Damage)
                sum += buffs.GetFinalAttack(e.value) * (e.hits > 1 ? e.hits : 1);
        return sum;
    }

    static constexpr float ICON_SIZE = 18.0f;
    static constexpr float STEP = ICON_SIZE + 24.0f;

    // 効果 → アイコンのテクスチャ名（未用意なら空でフォールバック）
    static std::string GetIconTexture(const Effect& e)
    {
        switch (e.kind)
        {
        case EffectKind::Damage:            return (e.hits > 1) ? "icon_multihit" : "icon_attack";
        case EffectKind::Block:             return "icon_block";
        case EffectKind::Buff:              return "icon_buff";
        case EffectKind::Debuff:            return "icon_debuff";
        case EffectKind::MoveToward:
        case EffectKind::MoveAway:          return "icon_move";
        case EffectKind::PullPlayer:        return "icon_attack";
        case EffectKind::KnockbackPlayer :  return "icon_attack";
        case EffectKind::Summon:            return "icon_summon";
        case EffectKind::Coil:              return "icon_coil";
        case EffectKind::AddCard:           return "icon_curse";
        default:                            return "";
        }
    }

    // アイコン1つを描く（テクスチャがあれば絵、無ければ色付き四角）
    static void DrawIcon(SpriteRenderer* sr, ID3D11ShaderResourceView* white,
        const Effect& e, float x, float y, float size)
    {
        auto tex = TextureManager::Get(GetIconTexture(e));
        if (tex)
        {
            sr->DrawSprite(tex, x, y, size, size, 0.0f, XMFLOAT4(1, 1, 1, 1));
        }
        else
        {
            sr->DrawSprite(white, x - 1.0f, y - 1.0f, size + 2.0f, size + 2.0f,
                0.0f, XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f));          // 枠
            sr->DrawSprite(white, x, y, size, size, 0.0f, GetIconColor(e));
        }
    }
};