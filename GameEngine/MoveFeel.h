#pragma once
#include <string>
#include <vector>

// 移動の動き方（enemies.json の "moveStyle"。書かなければ Walk）
//  Walk ：1マス動き始めるたびに、後ろへ土ぼこり（足のあるキャラ・プレイヤー）
//  Hop  ：1マスごとにぴょんと跳ねて、着地でつぶれて戻る＋着地の土ぼこり（足がなく地面にいるキャラ）
//  Float：何も出さない（浮いているキャラ）
//  Glide：通った跡が光る（三層のくさび）
enum class MoveStyle { Walk, Hop, Float, Glide };

// 移動の手応え（キャラが動くときの見た目）。プレイヤーと敵の共通の親 GameObject が1つ持つ。
// 2マス以上続けて動いたら、Walk・Hop・Float には残像も付ける
class MoveFeel
{
public:
    struct Ghost { float x, z, alpha; };   // 残像1枚（ワールドの位置と濃さ）

    static MoveStyle StyleFromName(const std::string& name);
    void SetStyle(MoveStyle s) { m_style = s; }

    // 1回の移動（1マス、または一気に数マス）を始めたとき。plannedCells＝歩きの経路なら、残りの経路のマス数（無ければ0）
    void OnMoveStart(float fromX, float fromZ, float toX, float toZ, float plannedCells);
    // 毎フレーム。moving＝動いている途中か、progress＝今の移動の進み具合（0→1、緩急込み）、x/z＝今の位置
    void Update(float dt, bool moving, float progress, float x, float z);

    // 描くときに使う
    float Lift(float height) const;   // 跳ねて浮いている高さ（キャラの高さ height に対する割合で決める）
    float ScaleX() const;             // 横の大きさの倍率（着地で横に広がる）
    float ScaleY() const;             // 縦の大きさの倍率（空中で伸び、着地でつぶれる）
    const std::vector<Ghost>& Ghosts() const { return m_ghosts; }

private:
    void Land(float x, float z);      // 着地：つぶれ始め＋左右へ土ぼこり

    MoveStyle m_style = MoveStyle::Walk;
    // 今の移動
    float m_dirX = 0.0f, m_dirZ = 0.0f;   // 進む向き（長さ1）
    int   m_hops = 0;                     // 今の移動で跳ねる回数（＝マス数）
    int   m_landed = 0;                   // 今の移動で着地した回数
    float m_hop = 0.0f;                   // 跳ねている高さ（0～1）
    float m_squash = 0.0f;                // 着地のつぶれの残り（1→0）
    // 続けて動いた距離（残像を出すかどうか）
    float m_runCells = 0.0f;              // 間をあけずに続けて動いたマス数
    float m_idle = 1.0f;                  // 止まってからの秒数
    bool  m_ghostOn = false;
    float m_ghostTimer = 0.0f, m_trailTimer = 0.0f;
    std::vector<Ghost> m_ghosts;
};
