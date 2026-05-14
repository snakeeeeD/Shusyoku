#pragma once
#include <vector>
#include <string>

struct PlayerData
{
    std::vector<std::string> deck; // カードIDのリスト
    int maxHp;
    int hp;
};