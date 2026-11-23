#pragma once
#include <vector>
#include <string>
class GridMap;

// 床・罠が置かれた時の見せ方：上から落ちてきて、着地でぺたっと広がり、周りに土煙（effects.json の tile_land）。
// どこで置かれても拾えるよう、毎フレーム盤面の床を前のフレームと比べて「新しく置かれたマス」に気づく
//（カードの罠・敵の床・炎の足跡など、置く所に何も書かなくていい）
class TileDropFx
{
public:
    void Update(float dt, GridMap* grid);   // 最初の1回は今ある床を覚えるだけ（戦闘の始めからある床は落とさない）。盤面は読むだけ

    // そのマスの床の見た目
    struct Look
    {
        bool  hidden = false;   // まだ落ち始めていない（同じ時に置かれた他のマスを待っている）＝描かない
        float lift = 0.0f;      // 持ち上げる高さ（ワールド）
        float scale = 1.0f;     // 大きさの倍率（着地でぺたっと広がる）
        float alpha = 1.0f;     // 濃さ（落ちてくる間、だんだん濃く）
        float shadow = 0.0f;    // マスに落とす影の濃さ（0から1。落ちている間だけ）
    };
    Look Get(int col, int row, const std::string& nowId) const;   // nowId＝今そのマスにある床（まだ気づいていない新しい床なら、見せない）

private:
    std::vector<std::string> m_snap;   // 前のフレームのマスの床（空＝無し）
    std::vector<float> m_t;            // マスごとの、落ち始めてからの秒数（マイナス＝待っている。大きい値＝落ち終わった）
    int m_cols = 0, m_rows = 0;
    bool m_ready = false;
};
