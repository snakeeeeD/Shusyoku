#pragma once
#include "Scene.h"
#include "Scenetype.h"
#include "SceneManager.h"
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

    // バトルで使う敵IDを外部から取得できるように
    const std::string& GetCurrentBattleEnemyId() const { return m_currentEnemyId; }

private:
    void GenerateMap();
    void DrawNode(const FieldNode& node, bool isHovered);
    bool CanMove(int nodeIndex) const;
    XMFLOAT2 GetNodeScreenPos(const FieldNode& node) const;

    SpriteRenderer* m_spriteRenderer;
    TextRenderer* m_textRenderer;
    Input           m_input;

    ID3D11ShaderResourceView* m_whiteTexture;

    int m_screenWidth;
    int m_screenHeight;
    HWND m_hWnd;

    std::vector<FieldNode> m_nodes;
    int m_currentNodeIndex;   // 現在いるノード
    int m_hoveredNodeIndex;   // マウスが乗っているノード

    std::string m_currentEnemyId;

    void SaveProgress();

    static constexpr float NODE_RADIUS = 30.0f;
};