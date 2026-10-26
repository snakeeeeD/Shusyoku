#pragma once
#include "GridMap.h"
#include "RelicManager.h"

// 聖地型（信仰）のルール。数値と判定はここに集める
namespace Faith
{
    constexpr int MAX = 10;                                      // 1マスの上限。ここに達したマスは聖域
    inline bool IsSanctuary(int f) { return f >= MAX; }
    inline int  AttackBonus(int f) { return f / 2; }             // 自分が立っている聖地の攻撃加算

    // マスの信仰（盤面の外は0。GetCell は範囲チェックが無いのでここで見る）
    inline int At(GridMap* g, int col, int row)
    {
        if (!g || col < 0 || row < 0 || col >= g->GetCols() || row >= g->GetRows()) return 0;
        return g->GetCell(col, row).faith;
    }
    // 刻む量への加算（聖印）。カードの数値表示もこれを使う
    inline int EngraveBonus() { return RelicManager::SumValue("engraveAdd"); }

    // マスに信仰を刻む（上限まで）。刻むものは全部ここを通るので、加算もここで一括
    inline void Engrave(GridMap* g, int col, int row, int n)
    {
        if (!g || n <= 0 || col < 0 || row < 0 || col >= g->GetCols() || row >= g->GetRows()) return;
        n += EngraveBonus();
        int& f = g->GetCell(col, row).faith;
        f = (f + n > MAX) ? MAX : f + n;
    }
}
