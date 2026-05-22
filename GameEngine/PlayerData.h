#pragma once
#include <vector>
#include <string>

struct PlayerData
{
    std::vector<std::string> deck; // カードIDのリスト
    int maxHp;
    int hp;

    // フィールド進行状況を追加
    int currentNodeIndex;  // 現在いるノードのインデックス
    std::vector<bool> clearedNodes;  // クリア済みノードのリスト
};