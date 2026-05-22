#include "FieldScene.h"
#include "SceneType.h"
#include <cstdlib>

FieldScene::FieldScene()
    : m_spriteRenderer(nullptr)
    , m_textRenderer(nullptr)
    , m_whiteTexture(nullptr)
    , m_screenWidth(0)
    , m_screenHeight(0)
    , m_hWnd(nullptr)
    , m_currentNodeIndex(0)
    , m_hoveredNodeIndex(-1)
{
}

FieldScene::~FieldScene()
{
    delete m_spriteRenderer;
    delete m_textRenderer;
}

bool FieldScene::Init(ID3D11Device* device, ID3D11DeviceContext* context,
    int screenWidth, int screenHeight, HWND hWnd,
    IDXGISwapChain* swapChain)
{
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;
    m_hWnd = hWnd;

    m_spriteRenderer = new SpriteRenderer();
    if (!m_spriteRenderer->Init(device, context, screenWidth, screenHeight))
        return false;

    m_textRenderer = new TextRenderer();
    if (!m_textRenderer->Init(device, context, swapChain))
        return false;

    m_whiteTexture = TextureManager::Get("white");
    m_input.SetWindowHandle(hWnd);

    GenerateMap();

    // セーブデータから進行状況を復元
    auto& playerData = PlayerDataManager::GetData();
    m_currentNodeIndex = playerData.currentNodeIndex;

    // クリア済みノードを復元
    if (playerData.clearedNodes.size() == m_nodes.size())
    {
        for (int i = 0; i < (int)m_nodes.size(); i++)
            m_nodes[i].cleared = playerData.clearedNodes[i];
    }

    return true;
}

void FieldScene::GenerateMap()
{
    m_nodes.clear();

    // 固定マップ（後でランダム生成に変更）
    // 列(col)が進むほど奥に進む
    // row は分岐の位置

    // スタート
    m_nodes.push_back({ FieldNodeType::Start,  0, 1, true,  {1, 2} });

    // 1列目（分岐）
    m_nodes.push_back({ FieldNodeType::Battle, 1, 0, false, {3} });
    m_nodes.push_back({ FieldNodeType::Rest,   1, 2, false, {3} });

    // 2列目（合流）
    m_nodes.push_back({ FieldNodeType::Battle, 2, 1, false, {4, 5} });

    // 3列目（分岐）
    m_nodes.push_back({ FieldNodeType::Battle, 3, 0, false, {6} });
    m_nodes.push_back({ FieldNodeType::Rest,   3, 2, false, {6} });

    // 4列目（ボス）
    m_nodes.push_back({ FieldNodeType::Boss,   4, 1, false, {} });

    m_currentNodeIndex = 0;
}

XMFLOAT2 FieldScene::GetNodeScreenPos(const FieldNode& node) const
{
    const float colSpacing = 200.0f;
    const float rowSpacing = 160.0f;
    const float offsetX = 150.0f;
    const float offsetY = m_screenHeight / 2.0f - rowSpacing;

    return XMFLOAT2(
        offsetX + node.col * colSpacing,
        offsetY + node.row * rowSpacing
    );
}

bool FieldScene::CanMove(int nodeIndex) const
{
    const auto& current = m_nodes[m_currentNodeIndex];
    for (int next : current.nextNodeIndices)
        if (next == nodeIndex) return true;
    return false;
}

void FieldScene::DrawNode(const FieldNode& node, bool isHovered)
{
    XMFLOAT2 pos = GetNodeScreenPos(node);
    float size = NODE_RADIUS * 2.0f;

    XMFLOAT4 color;
    switch (node.type)
    {
    case FieldNodeType::Start:  color = XMFLOAT4(0.5f, 0.5f, 0.5f, 0.7f); break;
    case FieldNodeType::Battle: color = XMFLOAT4(0.8f, 0.2f, 0.2f, 0.7f); break;
    case FieldNodeType::Rest:   color = XMFLOAT4(0.2f, 0.8f, 0.2f, 0.7f); break;
    case FieldNodeType::Boss:   color = XMFLOAT4(0.8f, 0.2f, 0.8f, 0.7f); break;
    default:                    color = XMFLOAT4(0.3f, 0.3f, 0.3f, 0.7f); break;
    }

    // 現在地は明るく
    if (&node == &m_nodes[m_currentNodeIndex])
        color = XMFLOAT4(1.0f, 1.0f, 0.0f, 1.0f);

    // ホバー中は少し明るく
    if (isHovered && CanMove((int)(&node - &m_nodes[0])))
        color.w = min(1.0f, color.x + 0.5f);

    // クリア済みは暗く
    if (node.cleared && &node != &m_nodes[m_currentNodeIndex])
        color = XMFLOAT4(color.x * 0.5f, color.y * 0.5f, color.z * 0.5f, 1.0f);

    m_spriteRenderer->DrawSprite(m_whiteTexture,
        pos.x - NODE_RADIUS, pos.y - NODE_RADIUS,
        size, size, 0.0f, color);
}

void FieldScene::Update(float deltaTime)
{
    m_input.Update();
}

void FieldScene::Draw()
{
    m_spriteRenderer->Begin();

    // ノード間の線を描画
    for (int i = 0; i < (int)m_nodes.size(); i++)
    {
        auto& node = m_nodes[i];
        XMFLOAT2 from = GetNodeScreenPos(node);

        for (int nextIdx : node.nextNodeIndices)
        {
            XMFLOAT2 to = GetNodeScreenPos(m_nodes[nextIdx]);

            // 線の中点と長さを計算
            float cx = (from.x + to.x) / 2.0f;
            float cy = (from.y + to.y) / 2.0f;
            float dx = to.x - from.x;
            float dy = to.y - from.y;
            float len = sqrtf(dx * dx + dy * dy);

            // 簡易的に細い矩形で線を描画
            m_spriteRenderer->DrawSprite(m_whiteTexture,
                cx - len / 2.0f, cy - 2.0f,
                len, 4.0f, 0.0f,
                XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f));
        }
    }

    // ノードを描画
    for (int i = 0; i < (int)m_nodes.size(); i++)
        DrawNode(m_nodes[i], i == m_hoveredNodeIndex);

    m_spriteRenderer->End();

    // テキスト描画
    m_textRenderer->Begin();

    for (int i = 0; i < (int)m_nodes.size(); i++)
    {
        auto& node = m_nodes[i];
        XMFLOAT2 pos = GetNodeScreenPos(node);

        const wchar_t* label = L"";
        switch (node.type)
        {
        case FieldNodeType::Start:  label = L"START";  break;
        case FieldNodeType::Battle: label = L"BATTLE";     break;
        case FieldNodeType::Rest:   label = L"REST";     break;
        case FieldNodeType::Boss:   label = L"BOSS";   break;
        }

        m_textRenderer->DrawText(label,
            pos.x - NODE_RADIUS + 5.0f,
            pos.y - 10.0f, 16.0f,
            D2D1::ColorF(D2D1::ColorF::White));
    }

    // HP表示
    auto& playerData = PlayerDataManager::GetData();
    wchar_t hpText[64];
    swprintf_s(hpText, L"HP: %d / %d", playerData.hp, playerData.maxHp);
    m_textRenderer->DrawText(hpText, 20.0f, 20.0f, 24.0f,
        D2D1::ColorF(D2D1::ColorF::White));

    m_textRenderer->End();
}

void FieldScene::HandleInput()
{
    POINT mousePos = m_input.GetMousePos();

    // ホバー判定
    m_hoveredNodeIndex = -1;
    for (int i = 0; i < (int)m_nodes.size(); i++)
    {
        XMFLOAT2 pos = GetNodeScreenPos(m_nodes[i]);
        float dx = mousePos.x - pos.x;
        float dy = mousePos.y - pos.y;
        if (sqrtf(dx * dx + dy * dy) <= NODE_RADIUS)
        {
            m_hoveredNodeIndex = i;
            break;
        }
    }

    // クリックで移動
    if (m_hoveredNodeIndex >= 0 && m_input.GetMouseButtonTrigger(0))
    {
        if (CanMove(m_hoveredNodeIndex))
        {
            auto& node = m_nodes[m_hoveredNodeIndex];

            switch (node.type)
            {
            case FieldNodeType::Battle:
            case FieldNodeType::Boss:
            {
                m_currentNodeIndex = m_hoveredNodeIndex;
                node.cleared = true;

                // 進行状況を保存
                SaveProgress();

                if (onChangeScene)
                    onChangeScene(SceneType::Battle);
                break;
            }

            case FieldNodeType::Rest:
            {
                m_currentNodeIndex = m_hoveredNodeIndex;
                node.cleared = true;

                // HP回復
                auto& playerData = PlayerDataManager::GetData();
                playerData.hp = min(playerData.hp + 20, playerData.maxHp);

                // 進行状況を保存
                SaveProgress();

                OutputDebugStringW(L"★ 休憩：HP+20\n");
                break;
            }
            default:
                break;
            }
        }

    }
    
}

    // 進行状況を保存する関数
    void FieldScene::SaveProgress()
    {
        std::vector<bool> clearedStatus;
        for (const auto& node : m_nodes)
            clearedStatus.push_back(node.cleared);

        PlayerDataManager::SaveFieldProgress(m_currentNodeIndex, clearedStatus);
    }