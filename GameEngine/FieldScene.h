#pragma once
#include "Scene.h"
#include "SceneType.h"
#include "SpriteRenderer.h"
#include "TextRenderer.h"
#include "TextureManager.h"
#include "input.h"
#include "FieldNode.h"
#include "PlayerDataManager.h"
#include <vector>
#include <functional>

class FieldScene : public Scene
{
public:
    FieldScene();
    ~FieldScene();

    bool Init(ID3D11Device* device, ID3D11DeviceContext* context,
        int screenWidth, int screenHeight, HWND hWnd,
        IDXGISwapChain* swapChain) override;
    void Update(float deltaTime) override;
    void Draw() override;
    void HandleInput() override;

    std::function<void(SceneType)> onChangeScene;
    const std::string& GetCurrentBattleEnemyId() const { return m_currentEnemyId; }

    int m_currentBattleSeed = 0;
    int GetCurrentBattleSeed() const { return m_currentBattleSeed; }

private:
    void GenerateMap();
    void SaveProgress();
    bool CanMove(int col, int row) const;
    int  GetNodeIndex(int col, int row) const;
    XMFLOAT2 GetNodeScreenPos(int col, int row) const;

    SpriteRenderer* m_spriteRenderer;
    TextRenderer* m_textRenderer;
    Input           m_input;
    ID3D11ShaderResourceView* m_whiteTexture;

    int m_screenWidth;
    int m_screenHeight;
    HWND m_hWnd;

    std::vector<FieldNode> m_nodes; // グリッド上の全マス（col * GRID_ROWS + row）

    int m_playerCol;
    int m_playerRow;

    int m_steps;
    int m_maxSteps;

    std::string m_currentEnemyId;
    float m_highlightTimer;

    static constexpr int   GRID_COLS = 12;
    static constexpr int   GRID_ROWS = 7;
    static constexpr int   INITIAL_STEPS = 20;
    static constexpr float CELL_SIZE = 60.0f;
    static constexpr float CELL_GAP = 20.0f;

    bool m_resumeBattle = false;
};