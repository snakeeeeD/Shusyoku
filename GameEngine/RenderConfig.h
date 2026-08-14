#pragma once
// 論理座標系（UI/入力/2D/3D射影の基準。常に固定）
constexpr int LOGICAL_WIDTH = 1280;
constexpr int LOGICAL_HEIGHT = 720;
// 実際に描くピクセル解像度（バックバッファ/ビューポート/深度）。main.cpp が起動時に設定
extern int g_renderWidth;
extern int g_renderHeight;