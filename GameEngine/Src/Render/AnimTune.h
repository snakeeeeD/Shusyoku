#pragma once

// カード使用演出のライブ調整値（Debugでいじる／Releaseは既定値のまま＝挙動不変）

struct AnimTuneData
{
    float playCenterY = 100.0f;  // 中央到達位置の下オフセット
    float playApproach = 0.22f;   // 接近フェーズ割合
    float playHold = 0.72f;   // タメ終わり割合
    float playScale = 1.45f;   // 中央での拡大
    float playArc = 70.0f;   // 退場の弧の高さ
};
extern AnimTuneData g_animTune;