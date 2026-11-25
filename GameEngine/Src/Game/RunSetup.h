#pragma once
#include <string>
#include <vector>

// ランの初期状態を組む処理（デバッグの層ジャンプとクイックプレイで共有）
namespace RunSetup
{
    // 基本4枚＋軸タグのカードarchN枚＋補助suppN枚＋完全ランダムrandN枚で pd.deck を作り直す
    void BuildArchetypeDeck(const std::string& tag, int archN, int suppN, int randN);
    // rarities の各要素につき1個ずつ、未所持のレリックをランダムに付与（例: {"common","rare"}）
    void GrantRelics(const std::vector<std::string>& rarities);
    // 全レア度（ボス含む）を perRarity 個ずつ付与（デバッグの層ジャンプ用）
    void GrantRandomRelics(int perRarity);
}