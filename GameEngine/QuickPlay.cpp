#include "QuickPlay.h"
#include "PlayerDataManager.h"
#include "EncounterDataBase.h"
#include "RunSetup.h"
#include "GameUtils.h"
#include <nlohmann/json.hpp>
#include <fstream>
using json = nlohmann::json;

void QuickPlay::Load(const std::string& path)
{
    s_decks.clear(); s_battles.clear();
    std::ifstream f(path);
    if (!f.is_open()) return;
    json j; f >> j;
    for (auto& d : j["decks"])
    {
        QuickDeck q;
        q.tag = d.value("tag", "");
        q.name = ToWString(d.value("name", ""));
        q.desc = ToWString(d.value("desc", ""));
        q.archN = d.value("archN", 5);
        q.suppN = d.value("suppN", 3);
        q.randN = d.value("randN", 2);
        q.relics = d.value("relics", std::vector<std::string>{});
        q.cards = d.value("cards", std::vector<std::string>{});
        s_decks.push_back(q);
    }
    for (auto& b : j["battles"]) s_battles.push_back(b.get<std::string>());
}

void QuickPlay::Enter()
{
    if (s_entered) return;
    s_entered = true;
    s_before = PlayerDataManager::GetData();   // 終わったら元に戻す（本編の状態を汚さない）
    PlayerDataManager::SetSaveEnabled(false);  // 本編のセーブに触らない
    PlayerDataManager::StartNewGame();
    PlayerDataManager::GetData().tutorialCraft = true;   // クラフトが無いので、初勝利のコア確定ドロップと説明を出さない
    s_base = PlayerDataManager::GetData();
}

void QuickPlay::BuildDeck(int i)
{
    if (!s_entered || i < 0 || i >= (int)s_decks.size()) return;
    auto& pd = PlayerDataManager::GetData();
    pd = s_base;                         // 選び直しても前のデッキ・レリックが残らない
    const QuickDeck& d = s_decks[i];
    if (!d.cards.empty())
        pd.deck = d.cards;               // 固定デッキ（JSONの並び）
    else
    {
        RunSetup::BuildArchetypeDeck(d.tag, d.archN, d.suppN, d.randN);   // 指定が無ければタグから自動で組む
        pd.deck.push_back("SKL_defend");   // 初戦チュートリアルの「移動・アタック・ブロック」を揃える
    }
    for (auto& id : d.relics) PlayerDataManager::AddRelic(id);   // デッキに合った決まったレリック
}

void QuickPlay::Begin()
{
    if (!s_entered) return;
    s_active = true;
    s_stage = 0;
}

void QuickPlay::End()
{
    if (!s_entered) return;
    PlayerDataManager::GetData() = s_before;
    PlayerDataManager::SetSaveEnabled(true);
    s_entered = s_active = false;
}

SceneType QuickPlay::NextScene()
{
    return (s_stage < (int)s_battles.size()) ? SceneType::Battle : SceneType::Result;
}

void QuickPlay::TakeBattle(std::string& encounter, EncCategory& cat)
{
    if (s_stage >= (int)s_battles.size()) return;
    encounter = s_battles[s_stage++];
    const EncounterData* e = EncounterDataBase::GetById(encounter);
    cat = e ? e->category : EncCategory::Normal;   // 通常/エリート/ボスはエンカウント側のデータを使う
}