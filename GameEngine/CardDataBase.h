#pragma once
#include "CardData.h"
#include <unordered_map>
#include <vector>

struct BaseDef;
struct MaterialDef;

class CardDataBase
{
public:
    static void Init();
    static void Reload();
    // JSON1枚分の文字列から CardData を作る（エディタのプレビュー用）。upgrade があれば強化版も作り hasUpgrade=true
    static bool ParseCardText(const std::string& jsonText, CardData& base, CardData& upgraded, bool& hasUpgrade);
    static const CardData* Get(const std::string& id);
    // デッキから外せるか（削除・変化の対象にできるか）。permanent のカードは外せない
    static bool CanRemove(const std::string& id) { const CardData* d = Get(id); return !(d && d->permanent); }
    // 強化できるか：+版があるか（合成カード・強化済みは不可）。デッキ閲覧・イベントで共有
    static bool CanUpgrade(const std::string& id)
    {
        if (id.rfind("CRAFT:", 0) == 0) return false;
        if (!id.empty() && id.back() == '+') return false;
        return Get(id + "+") != nullptr;
    }

    static const std::unordered_map<std::string, CardData>& GetAll() { return m_data; }
    static CardData BuildCrafted(const std::string& id);

    // 土台と素材を直接渡して合成カードを作る（BuildCrafted の計算本体。エディタのプレビューでも使う）
    static CardData BuildCraftedFrom(const std::string& id, const BaseDef& baseDef, const std::vector<const MaterialDef*>& mats);

    static std::vector<std::string> RewardPool();
    static std::string RandomCard();

    // デッキのタグ構成に寄せて count 枚を重複なし抽選（報酬/ショップ共通）
    static std::vector<std::string> PickRewardCards(int count, bool rareBias,
        const std::vector<std::string>& deck, bool rareOnly = false, bool upgradableOnly = false);

private:
    static void LoadHardcodedData();
    static std::unordered_map<std::string, CardData> m_data;
    static int  s_craftDiscount;     // 合成カードのコストを計算した時の「合成カードのコスト-」の合計
    static void SyncCraftCosts();    // 割引が変わっていたら、覚えている合成カードのコストだけ計算し直す
};