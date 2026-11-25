#include "RunSetup.h"
#include "PlayerDataManager.h"
#include "CardDataBase.h"
#include "RelicManager.h"
#include <cstdlib>
#include <vector>

void RunSetup::BuildArchetypeDeck(const std::string& tag, int archN, int suppN, int randN)
{
    auto& pd = PlayerDataManager::GetData();
    pd.deck.clear();
    pd.deck.push_back("ATK_strike"); pd.deck.push_back("ATK_strike");
    pd.deck.push_back("MOV_move");   pd.deck.push_back("MOV_move");
    bool isBulwark = (tag == "Bulwark");

    // 条件に合うカードからランダムにn枚追加
    auto pickCards = [&](auto pred, int n) {
        std::vector<std::string> pool;
        for (auto& [id, data] : CardDataBase::GetAll()) {
            if (data.generated || data.starter || data.unplayable) continue;
            if (id.rfind("CRAFT:", 0) == 0) continue;
            if (pred(id, data)) pool.push_back(id);
        }
        for (int i = 0; i < n && !pool.empty(); i++)
            pd.deck.push_back(pool[rand() % (int)pool.size()]);
        };

    // 軸カード
    pickCards([&](const std::string& id, const CardData& d) {
        for (auto& t : d.tags) if (t == tag) return true;
        return false; }, archN);

    // 補助カード
    if (isBulwark)
        // Bulwarkはブロックが軸 → 補助は「他軸」のランダム
        pickCards([&](const std::string& id, const CardData& d) {
        for (auto& t : d.tags) if (t == "Bulwark") return false;
        return true; }, suppN);
    else
        // それ以外は移動＋ブロックを補助に
        pickCards([&](const std::string& id, const CardData& d) {
        if (d.type == CardType::Move) return true;
        if (d.mainEffect.type == CardEffectType::Block) return true;
        for (auto& t : d.tags) if (t == "Bulwark") return true;
        return false; }, suppN);

    // 完全ランダム
    pickCards([&](const std::string& id, const CardData& d) { return true; }, randN);
}

void RunSetup::GrantRelics(const std::vector<std::string>& rarities)
{
    for (auto& r : rarities)
    {
        std::string id = RelicManager::RandomUnowned(r);
        if (!id.empty()) PlayerDataManager::AddRelic(id);
    }
}

void RunSetup::GrantRandomRelics(int perRarity)
{
    for (const char* r : { "common", "uncommon", "rare", "boss" })
        GrantRelics(std::vector<std::string>(perRarity, r));
}