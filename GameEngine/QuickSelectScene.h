#pragma once
#include "Scene.h"
#include "Scenetype.h"
#include "SpriteRenderer.h"
#include "TextRenderer.h"
#include "input.h"
#include "URect.h"
#include <functional>
#include <string>
#include <vector>

// クイックプレイのデッキ選択：左でデッキを選ぶ → 右に中身（ホバーで拡大＋詳細）→ 開始
class QuickSelectScene : public Scene
{
public:
    ~QuickSelectScene();
    bool Init(ID3D11Device* device, ID3D11DeviceContext* context,
        int screenWidth, int screenHeight, HWND hWnd, IDXGISwapChain* swapChain) override;
    void Update(float deltaTime) override;
    void Draw() override;
    void HandleInput() override;

    std::function<void(SceneType)> onChangeScene;

private:
    void Select(int i);   // デッキを組んで一覧を作り直す

    // レイアウト（描画と判定で共有）
    URect DeckBtn(int i) const { return { 40.0f, 140.0f + i * 74.0f, 300.0f, 60.0f }; }
    URect InfoRect() const { return { 40.0f, 372.0f, 300.0f, 250.0f }; }
    URect GridRect() const { return { 370.0f, 130.0f, 870.0f, 482.0f }; }
    URect BackBtn() const { return { 40.0f, 640.0f, 160.0f, 50.0f }; }
    URect StartBtn() const { return { 960.0f, 634.0f, 280.0f, 60.0f }; }
    URect RelicRow(int i) const { URect r = InfoRect(); return { r.x + 20.0f, r.y + 142.0f + i * 22.0f, r.w - 40.0f, 20.0f }; }
    void CardSlot(int i, float& x, float& y) const;   // 一覧のi枚目の左上

    SpriteRenderer* m_sprite = nullptr;
    TextRenderer* m_text = nullptr;
    ID3D11ShaderResourceView* m_white = nullptr;
    Input m_input;
    int m_screenWidth = 0, m_screenHeight = 0;

    int   m_selected = -1;              // 選んだデッキ（-1=未選択）
    int   m_hoverCard = -1;             // マウスが乗っているカード
    int   m_hoverKey = -1;              // ホバー音のエッジ判定用（ボタン番号）
    float m_time = 0.0f;
    bool  m_readyForInput = false;
    std::vector<std::string> m_cards;   // 表示用（種類順に並べたデッキ）

    static constexpr float CARD_SCALE = 0.75f;   // 一覧のカードの大きさ
    static constexpr float ZOOM_SCALE = 1.7f;    // ホバー時の拡大
    static constexpr int   GRID_COLS = 10;
};