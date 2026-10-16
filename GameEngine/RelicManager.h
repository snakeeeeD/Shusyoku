#pragma once
#include "RelicData.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <DirectXMath.h>

class RelicManager {
public:
    static void Load(const std::string& path);
    static void Reload(const std::string& path);   // 全部消してから読み直す（エディタの保存後）
    static const RelicDef* Get(const std::string& id);
    static bool Owns(const std::string& id);
    static int  SumValue(const std::string& kind);   // 所持中でkind一致のvalue合計
    static bool HasKind(const std::string& kind);
    static std::string RandomUnowned(const std::string& tier = "");
    static std::string RandomDrop();
    static std::vector<std::string> ShopPool();
    static std::vector<std::string> AllIds();
    static std::vector<const RelicDef*> OwnedByKind(const std::string& kind);

    static void NotifyFired(const std::string& id);   // 発動した瞬間を記録（バー演出用）
    static float FlashAmount(const std::string& id);  // 0..1（発動直後=1 → 0.6秒で0）

    static int   BarPerRow(int screenW);       // 1行に並ぶレリック数
    static float BarExtraHeight(int screenW);  // 2行目以降ぶんの高さ(0=はみ出さない)
    static float BarTotalHeight(int screenW);  // レリック帯の全高(最低1行分。敵パネル等を常にずらす用)

    static std::vector<std::string> RandomUnownedMany(const std::string& tier, int n);   // 重複なしでn個（足りなければある分だけ）
    static void LoadIcons();   // 全レリックのアイコンを読み込む（起動時とエディタ保存時。読み込み済みは飛ばす）
    static DirectX::XMFLOAT4 RarityColor(const std::string& rarity);   // レア度の色（帯の枠・アイコンの染め色）

private:
    static std::unordered_map<std::string, RelicDef> s_defs;
    static std::unordered_map<std::string, unsigned long long> s_firedAt;
};