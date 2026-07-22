#pragma once
#include <vector>
#include <string>

struct EncounterEnemy { std::string id; int col; int row; };

enum class EscalationKind { HpUp, AtkUp, AddAction, AddEnemy };
struct EscalationTier
{
    EscalationKind kind = EscalationKind::HpUp;
    int value = 0;              // HpUp/AtkUp: %,  それ以外: 未使用
    std::string id;             // AddEnemy: 敵ID
    int col = 0, row = 0;       // AddEnemy: 位置
};

struct EncounterData
{
    std::vector<EncounterEnemy> enemies;
    int rank;
    int weight;
    std::vector<EscalationTier> escalation;   // 省略時はデフォルト梯子
};