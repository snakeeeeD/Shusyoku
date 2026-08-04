#include "CardDataBase.h"
#include "MaterialDataBase.h" 
#include "GameUtils.h"
#include "RelicManager.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <windows.h>

using json = nlohmann::json;

std::unordered_map<std::string, CardData> CardDataBase::m_data;

void CardDataBase::Init()
{
    std::ifstream file("Assets/Data/cards.json");
    if (!file.is_open())
    {
        OutputDebugStringW(L"★ cards.json 読み込み失敗\n");
        return;
    }

    OutputDebugStringW(L"★ cards.json 読み込み成功\n");

    try
    {
        json j;
        file >> j;

        for (const auto& c : j["cards"])
        {
            CardData data;
            data.id = c["id"];
            data.name = ToWString(c["name"]);
            data.type = StringToCardType(c["type"]);
            data.cost = c["cost"];
            data.range = c["range"];
            data.rangeType = StringToRangeType(c["rangeType"]);
            data.description = ToWString(c["description"]);
            data.generated = c.value("generated", false);
            data.starter = c.value("starter", false);
            data.vfx = c.value("vfx", std::string(""));

            std::string rarityStr = c.value("rarity", "Common");
            if (rarityStr == "Uncommon") data.rarity = CardRarity::Uncommon;
            else if (rarityStr == "Rare") data.rarity = CardRarity::Rare;
            else data.rarity = CardRarity::Common;
            if (c.contains("tags"))
            {
                for (auto& t : c["tags"])
                    data.tags.push_back(t);
            }

            data.exhaust = c.value("exhaust", false);
            data.pierce = c.value("pierce", false);
            data.dash = c.value("dash", false);
            data.selfDamage = c.value("selfDamage", 0);
            data.hits = c.value("hits", 1);

            // mainEffect
            data.mainEffect.hasEffect = true;
            data.mainEffect.type = StringToCardEffectType(c["mainEffect"]["type"]);
            data.mainEffect.value = c["mainEffect"]["value"];
            data.mainEffect.duration = c["mainEffect"].value("duration", 0);
            data.mainEffect.buffType = c["mainEffect"].value("buffType", "");
            data.mainEffect.cardId = c["mainEffect"].value("cardId", "");
            data.mainEffect.trapType = c["mainEffect"].value("trapType", "");


            if (c.contains("onHitEffect"))
            {
                data.onHitEffect.hasEffect = true;
                data.onHitEffect.type = StringToCardEffectType(c["onHitEffect"]["type"]);
                data.onHitEffect.value = c["onHitEffect"]["value"];
                data.onHitEffect.duration = c["onHitEffect"].value("duration", 0);
                data.onHitEffect.buffType = c["onHitEffect"].value("buffType", "");
                data.onHitEffect.cardId = c["onHitEffect"].value("cardId", "");
            }
            else
            {
                data.onHitEffect.hasEffect = false;
            }

            if (c.contains("onHitEffect2"))
            {
                data.onHitEffect2.hasEffect = true;
                data.onHitEffect2.type = StringToCardEffectType(c["onHitEffect2"]["type"]);
                data.onHitEffect2.value = c["onHitEffect2"]["value"];
                data.onHitEffect2.duration = c["onHitEffect2"].value("duration", 0);
                data.onHitEffect2.buffType = c["onHitEffect2"].value("buffType", "");
            }
            else data.onHitEffect2.hasEffect = false;

            if (c.contains("subEffect"))
            {
                data.subEffect.hasEffect = true;
                data.subEffect.type = StringToCardEffectType(c["subEffect"]["type"]);
                data.subEffect.value = c["subEffect"]["value"];
                data.subEffect.duration = c["subEffect"].value("duration", 0);
                data.subEffect.buffType = c["subEffect"].value("buffType", "");
                data.subEffect.cardId = c["subEffect"].value("cardId", "");
            }
            else
            {
                data.subEffect.hasEffect = false;
            }

            if (c.contains("allEnemyEffect"))
            {
                data.allEnemyEffect.hasEffect = true;
                data.allEnemyEffect.value = c["allEnemyEffect"]["value"];
                data.allEnemyEffect.duration = c["allEnemyEffect"].value("duration", 0);
                data.allEnemyEffect.buffType = c["allEnemyEffect"].value("buffType", "");
            }
            else
            {
                data.allEnemyEffect.hasEffect = false;
            }

            m_data[data.id] = data;

            // アップグレード版を自動生成（id + "+"）
            if (c.contains("upgrade"))
            {
                const auto& u = c["upgrade"];
                CardData up = data;
                up.id = data.id + "+";
                up.name = data.name + L"+";

                up.cost += u.value("cost", 0);
                up.hits += u.value("hits", 0);
                up.range += u.value("range", 0);
                up.mainEffect.value += u.value("mainValue", 0);
                up.mainEffect.duration += u.value("duration", 0);
                if (up.onHitEffect.hasEffect) up.onHitEffect.value += u.value("onHitValue", 0);
                if (up.onHitEffect.hasEffect)  up.onHitEffect.duration += u.value("onHitDuration", 0);
                if (up.onHitEffect2.hasEffect) up.onHitEffect2.duration += u.value("onHitDuration", 0);
                if (up.subEffect.hasEffect)   up.subEffect.value += u.value("subValue", 0);
                if (up.cost < 0) up.cost = 0;
                if (u.contains("rangeType"))
                    up.rangeType = StringToRangeType(u["rangeType"]);
                if (u.contains("description"))
                    up.description = ToWString(u["description"]);

                m_data[up.id] = up;
            }

            wchar_t buf[256];
            swprintf_s(buf, L"★ カード読み込み: %s\n", data.name.c_str());
            OutputDebugStringW(buf);
        }

        OutputDebugStringW(L"★ CardDataBase 初期化完了\n");
    }
    catch (const json::exception& e)
    {
        char buf[512];
        sprintf_s(buf, "★ JSONエラー: %s\n", e.what());
        OutputDebugStringA(buf);
    }
}

const CardData* CardDataBase::Get(const std::string& id)
{
    auto it = m_data.find(id);
    if (it != m_data.end()) return &it->second;

    if (id.rfind("CRAFT:", 0) == 0)
    {
        m_data[id] = BuildCrafted(id);
        return &m_data[id];
    }
    return nullptr;
}

static void applyEntry(CardData& c, const MaterialDef& m, const std::string& baseType)
{
    const MatEntry* e = m.entryFor(baseType);
    if (!e) return;
    if (e->slot == "amplifyMain")
        c.mainEffect.value += e->value;
    else if (e->slot == "sub") {
        c.subEffect.hasEffect = true;
        c.subEffect.type = StringToCardEffectType(e->type);
        c.subEffect.value = e->value; c.subEffect.duration = e->duration;
        c.subEffect.buffType = e->buffType;
    }
    else if (e->slot == "onHit") {
        c.onHitEffect.hasEffect = true;
        c.onHitEffect.type = StringToCardEffectType(e->type);
        c.onHitEffect.value = e->value; c.onHitEffect.duration = e->duration;
        c.onHitEffect.buffType = e->buffType;
    }
    else if (e->slot == "main") {
        c.mainEffect.type = StringToCardEffectType(e->type);
        c.mainEffect.value = e->value; c.mainEffect.duration = e->duration;
        c.mainEffect.buffType = e->buffType;
    }
    else if (e->slot == "hits")
        c.hits += e->value;
    // none / onArrival は v1 では無視（onArrivalは移動核実装時）
}

CardData CardDataBase::BuildCrafted(const std::string& id)
{
    CardData c;
    c.id = id;
    // "CRAFT:core|m1|m2" を分解
    std::string body = id.substr(6);
    std::vector<std::string> parts;
    size_t pos = 0, bar;
    while ((bar = body.find('|', pos)) != std::string::npos) {
        parts.push_back(body.substr(pos, bar - pos)); pos = bar + 1;
    }
    parts.push_back(body.substr(pos));
    if (parts.empty()) return c;

    const BaseDef* base = MaterialDataBase::GetBase(parts[0]);
    if (!base) return c;

    c.type = StringToCardType(base->type);
    c.rangeType = StringToRangeType(base->rangeType);
    c.range = base->range;
    c.cost = base->cost;
    c.mainEffect.hasEffect = true;
    c.mainEffect.type = StringToCardEffectType(base->mainType);
    c.mainEffect.value = base->mainValue;

    std::wstring nm = ToWString(base->name);
    std::wstring tags;
    for (size_t i = 1; i < parts.size(); i++) {
        const MaterialDef* m = MaterialDataBase::GetMaterial(parts[i]);
        if (!m) continue;
        c.cost += m->cost;
        applyEntry(c, *m, base->type);
        if (!tags.empty()) tags += L"・";
        tags += ToWString(m->tag);
    }
    if (!tags.empty()) nm += L"【" + tags + L"】";
    c.cost -= RelicManager::SumValue("craftCostDown");
    if (c.cost < 0) c.cost = 0;
    c.name = nm;

    // 仮の説明文
    std::wstring hitSuffix = (c.hits > 1) ? (L"×" + std::to_wstring(c.hits)) : L"";
    switch (c.mainEffect.type)
    {
    case CardEffectType::Damage:    c.description = L"{value}ダメージ" + hitSuffix; break;
    case CardEffectType::Block:     c.description = L"{value}ブロック"; break;
    case CardEffectType::ApplyBuff: c.description = L"攻撃力+{value}"; break;
    default:                        c.description = L"合成カード"; break;
    }
    if (c.subEffect.hasEffect)   c.description += L" / 自身強化";
    if (c.onHitEffect.hasEffect) c.description += L" / 命中時効果";
    return c;
}

std::vector<std::string> CardDataBase::RewardPool()
{
    std::vector<std::string> v;
    for (auto& kv : m_data)
    {
        const std::string& id = kv.first;
        if (!id.empty() && id.back() == '+') continue;   // 強化版は除外
        if (id.rfind("CRAFT:", 0) == 0) continue;         // 合成カード除外
        if (kv.second.generated) continue;                // 生成専用除外
        if (kv.second.starter && kv.second.type != CardType::Move) continue;   // 初期カード(移動以外)を除外
        v.push_back(id);
    }

    return v;
}

std::string CardDataBase::RandomCard()
{
    auto pool = RewardPool();
    if (pool.empty()) return "";
    return pool[rand() % (int)pool.size()];
}

static int RarityBaseWeight(CardRarity r, bool rareBias)
{
    if (rareBias)
        return (r == CardRarity::Rare) ? 12 : (r == CardRarity::Uncommon) ? 6 : 1;
    return (r == CardRarity::Rare) ? 2 : (r == CardRarity::Uncommon) ? 5 : 10;
}

std::vector<std::string> CardDataBase::PickRewardCards(int count, bool rareBias, const std::vector<std::string>& deck)
{
    // デッキのタグ枚数を集計
    std::unordered_map<std::string, int> tagCounts;
    for (const auto& id : deck)
    {
        const CardData* c = Get(id);
        if (!c) continue;
        for (const auto& t : c->tags) tagCounts[t]++;
    }

    const float K = 0.5f;   // タグ1枚あたりの出やすさ係数

    struct WC { std::string id; float w; };
    std::vector<WC> pool;
    for (const auto& id : RewardPool())
    {
        const CardData* c = Get(id);
        if (!c) continue;
        int aff = 0;
        for (const auto& t : c->tags)
        {
            auto it = tagCounts.find(t);
            if (it != tagCounts.end() && it->second > aff) aff = it->second;
        }
        float w = RarityBaseWeight(c->rarity, rareBias) * (1.0f + K * aff);
        pool.push_back({ id, w });
    }

    // 重み付き・重複なしで count 枚
    std::vector<std::string> result;
    for (int n = 0; n < count && !pool.empty(); ++n)
    {
        float total = 0.0f;
        for (const auto& p : pool) total += p.w;

        float roll = (float)rand() / (float)RAND_MAX * total;
        int picked = 0;
        for (int i = 0; i < (int)pool.size(); ++i)
        {
            roll -= pool[i].w;
            if (roll <= 0.0f) { picked = i; break; }
        }
        result.push_back(pool[picked].id);
        pool.erase(pool.begin() + picked);
    }
    return result;
}