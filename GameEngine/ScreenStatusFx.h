#pragma once
#include <vector>

class SpriteRenderer;
class Renderer3D;

// 画面の演出に渡す、今の状態
struct ScreenStatus
{
    int hp = 0, maxHp = 0;    // プレイヤーのHP
    bool poisoned = false;    // プレイヤーが毒か
};

// 状態に合わせて出し続ける、画面（スクリーン）の演出。
// 毎フレーム「今どの状態か」を見て、濃さを目標へ少しずつ近づける。
// 始まった・終わったを追いかけないので、回復やバフの付け外しがどこで起きても漏れない。
//  今あるもの：HPが低いとき、画面の端が赤くなる（3割以下に入った直後だけ、鼓動に合わせて大きく明滅する）
//             毒のとき、画面の端に紫のもや（エフェクト用シェーダー）＋上の帯からときどきしずくが垂れる
class ScreenStatusFx
{
public:
    // 毎フレーム
    void Update(float dt, const ScreenStatus& s);
    // 3Dを描き終えた後、UIより前に呼ぶ（手札や数字にはかからない）。毒のもや → しずく → HPの赤み、の順に重ねる
    void Draw(Renderer3D* r, SpriteRenderer* sr, float screenW, float screenH) const;

private:
    float m_lowHp = 0.0f;   // HPが低いときの赤みの濃さ（0～1。目標へ少しずつ近づける）
    bool  m_wasLow = false; // 前のフレームでHPが低かったか（入った瞬間を見つける用）
    float m_beatLeft = 0.0f;   // 鼓動の残り秒数（入った瞬間に3回分を入れて、0になったら明滅をやめる）

    // 毒
    struct Drip { float x, age, grow, size; };   // しずく1つ（横の位置 0～1、出てからの秒数、ふくらむ秒数、大きさの倍率）
    void UpdateDrips(float dt, bool spawn);       // しずくを進める。spawn＝新しいしずくを出すか
    void DrawDrips(SpriteRenderer* sr, float screenW) const;
    float m_poison = 0.0f;      // 毒のもやの濃さ（0～1。目標へ少しずつ近づける）
    float m_time = 0.0f;        // もやの模様を流す時間（0に戻さず増やし続ける）
    float m_dripTimer = 0.0f;   // 次のしずくまでの秒数
    std::vector<Drip> m_drips;
};
