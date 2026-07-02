#include "FieldScene.h"
#include "FieldMapConfig.h"
#include "SceneType.h"
#include <cstdlib>
#include <algorithm>
#include <ctime>
#include <map>
#include <ctime>

FieldScene::FieldScene()
    : m_spriteRenderer(nullptr)
    , m_textRenderer(nullptr)
    , m_whiteTexture(nullptr)
    , m_screenWidth(0)
    , m_screenHeight(0)
    , m_hWnd(nullptr)
    , m_playerCol(0)
    , m_playerRow(0)
    , m_steps(0)
    , m_maxSteps(0)
    , m_highlightTimer(0.0f)
{
}

FieldScene::~FieldScene()
{
    delete m_spriteRenderer;
    delete m_textRenderer;
}

bool FieldScene::Init(ID3D11Device* device, ID3D11DeviceContext* context,
    int screenWidth, int screenHeight, HWND hWnd, IDXGISwapChain* swapChain)
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

    auto& playerData = PlayerDataManager::GetData();

    // ノード情報が保存されていれば復元
    if ((int)playerData.fieldNodeTypes.size() == GRID_COLS * GRID_ROWS)
    {
        for (int i = 0; i < GRID_COLS * GRID_ROWS; i++)
        {
            m_nodes[i].type = (FieldNodeType)playerData.fieldNodeTypes[i];
            m_nodes[i].enemyId = playerData.fieldNodeEnemyIds[i];
            m_nodes[i].visited = playerData.fieldNodeVisited[i];
        }
        m_playerCol = playerData.fieldPlayerCol;
        m_playerRow = playerData.fieldPlayerRow;
        m_steps = playerData.fieldSteps;
    }

    return true;
}

void FieldScene::GenerateMap()
{
    m_nodes.clear();
    m_nodes.resize(GRID_COLS * GRID_ROWS);

    m_steps = INITIAL_STEPS;
    m_maxSteps = INITIAL_STEPS;

    srand((unsigned int)time(nullptr));

    // マップ設定
    FieldMapConfig config;
    config.typeLimits = {
        { FieldNodeType::Battle, -1, 7 },
        { FieldNodeType::Rest,    4,  3 },
    };

    // 全マスをEmptyで初期化
    for (int row = 0; row < GRID_ROWS; row++)
    {
        for (int col = 0; col < GRID_COLS; col++)
        {
            int idx = GetNodeIndex(col, row);
            m_nodes[idx].type = FieldNodeType::Empty;
            m_nodes[idx].col = col;
            m_nodes[idx].row = row;
            m_nodes[idx].visited = false;
            m_nodes[idx].enemyId = "";
        }
    }

    // スタートは左端中央
    m_playerCol = 0;
    m_playerRow = GRID_ROWS / 2;
    m_nodes[GetNodeIndex(0, m_playerRow)].type = FieldNodeType::Start;
    m_nodes[GetNodeIndex(0, m_playerRow)].visited = true;

    // ボスは右端中央
    m_nodes[GetNodeIndex(GRID_COLS - 1, GRID_ROWS / 2)].type = FieldNodeType::Boss;

    // 固定配置マスを先に配置
    for (auto& fixed : config.fixedNodes)
    {
        int idx = GetNodeIndex(fixed.col, fixed.row);
        m_nodes[idx].type = fixed.type;
        m_nodes[idx].enemyId = fixed.enemyId;
    }

    std::vector<std::string> enemyIds = { "slime", "goblin", "orc" };

    // 各タイプの配置数を管理
    std::map<FieldNodeType, int> typeCounts;
    for (auto& limit : config.typeLimits)
        typeCounts[limit.type] = 0;

    // 中間マスの座標をシャッフル
    std::vector<std::pair<int, int>> positions;
    for (int row = 0; row < GRID_ROWS; row++)
        for (int col = 1; col < GRID_COLS - 1; col++)
            positions.push_back({ col, row });

    for (int i = (int)positions.size() - 1; i > 0; i--)
    {
        int j = rand() % (i + 1);
        std::swap(positions[i], positions[j]);
    }

    // シャッフルした順に配置
    for (auto& [col, row] : positions)
    {
        int idx = GetNodeIndex(col, row);
        if (m_nodes[idx].type != FieldNodeType::Empty) continue;

        // 配置可能なタイプを重みで選ぶ
        std::vector<std::pair<FieldNodeType, int>> available;
        for (auto& limit : config.typeLimits)
        {
            if (limit.maxCount >= 0 && typeCounts[limit.type] >= limit.maxCount)
                continue;
            available.push_back({ limit.type, limit.weight });
        }

        FieldNodeType chosen;
        if (available.empty())
        {
            // 全タイプが上限に達したらBattleで埋める
            chosen = FieldNodeType::Battle;
        }
        else
        {
            // 重みに基づいてランダム選択
            int totalWeight = 0;
            for (auto& a : available) totalWeight += a.second;

            int roll = rand() % totalWeight;
            int cum = 0;
            chosen = available[0].first;
            for (auto& a : available)
            {
                cum += a.second;
                if (roll < cum) { chosen = a.first; break; }
            }
        }

        m_nodes[idx].type = chosen;
        typeCounts[chosen]++;

        if (chosen == FieldNodeType::Battle)
            m_nodes[idx].enemyId = enemyIds[rand() % enemyIds.size()];
    }
}
int FieldScene::GetNodeIndex(int col, int row) const
{
    return col * GRID_ROWS + row;
}

bool FieldScene::CanMove(int col, int row) const
{
    if (col < 0 || col >= GRID_COLS) return false;
    if (row < 0 || row >= GRID_ROWS) return false;

    // 自分のマスには移動不可 ← 追加
    if (col == m_playerCol && row == m_playerRow) return false;

    int dc = abs(col - m_playerCol);
    int dr = abs(row - m_playerRow);
    if (dc + dr != 1) return false;

    int idx = GetNodeIndex(col, row);
    if (m_nodes[idx].type == FieldNodeType::Empty) return false;
    if (m_nodes[idx].type == FieldNodeType::Start) return false;

    if (m_steps <= 0) return false;

    return true;
}
XMFLOAT2 FieldScene::GetNodeScreenPos(int col, int row) const
{
    float totalW = GRID_COLS * (CELL_SIZE + CELL_GAP) - CELL_GAP;
    float totalH = GRID_ROWS * (CELL_SIZE + CELL_GAP) - CELL_GAP;
    float offsetX = (m_screenWidth - totalW) / 2.0f;
    float offsetY = (m_screenHeight - totalH) / 2.0f;

    return XMFLOAT2(
        offsetX + col * (CELL_SIZE + CELL_GAP),
        offsetY + row * (CELL_SIZE + CELL_GAP)
    );
}

void FieldScene::Update(float deltaTime)
{
    m_input.Update();
    m_highlightTimer += deltaTime * 0.5f;
    if (m_highlightTimer > 3.14159f * 2.0f)
        m_highlightTimer = 0.0f;
}

void FieldScene::Draw()
{
   
    m_spriteRenderer->Begin();

    m_spriteRenderer->DrawSprite(
        TextureManager::Get("map_bg"),  // シーンごとに変える
        0.0f, 0.0f,
        (float)m_screenWidth, (float)m_screenHeight,
        0.0f, XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f));

    // 隣接マス間の線を描画
    for (int row = 0; row < GRID_ROWS; row++)
    {
        for (int col = 0; col < GRID_COLS; col++)
        {
            int idx = GetNodeIndex(col, row);
            if (m_nodes[idx].type == FieldNodeType::Empty) continue;

            XMFLOAT2 from = GetNodeScreenPos(col, row);
            float cx1 = from.x + CELL_SIZE / 2.0f;
            float cy1 = from.y + CELL_SIZE / 2.0f;

            // 右のマスへの線
            if (col + 1 < GRID_COLS)
            {
                int rightIdx = GetNodeIndex(col + 1, row);
                if (m_nodes[rightIdx].type != FieldNodeType::Empty)
                {
                    XMFLOAT2 to = GetNodeScreenPos(col + 1, row);
                    float cx2 = to.x + CELL_SIZE / 2.0f;
                    float cy2 = to.y + CELL_SIZE / 2.0f;
                    float len = sqrtf((cx2 - cx1) * (cx2 - cx1) + (cy2 - cy1) * (cy2 - cy1));
                    float cx = (cx1 + cx2) / 2.0f;
                    float cy = (cy1 + cy2) / 2.0f;
                    m_spriteRenderer->DrawSprite(m_whiteTexture,
                        cx - len / 2.0f, cy - 2.0f,
                        len, 4.0f, 0.0f,
                        XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f));
                }
            }

            // 下のマスへの線
            if (row + 1 < GRID_ROWS)
            {
                int downIdx = GetNodeIndex(col, row + 1);
                if (m_nodes[downIdx].type != FieldNodeType::Empty)
                {
                    XMFLOAT2 to = GetNodeScreenPos(col, row + 1);
                    float cx2 = to.x + CELL_SIZE / 2.0f;
                    float cy2 = to.y + CELL_SIZE / 2.0f;
                    float len = sqrtf((cx2 - cx1) * (cx2 - cx1) + (cy2 - cy1) * (cy2 - cy1));
                    float cx = (cx1 + cx2) / 2.0f;
                    float cy = (cy1 + cy2) / 2.0f;
                    m_spriteRenderer->DrawSprite(m_whiteTexture,
                        cx - len / 2.0f, cy - 2.0f,
                        len, 4.0f, 1.5708f, // 90度
                        XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f));
                }
            }
        }
    }
    float pulse = sin(m_highlightTimer);
    float blink = 0.3f + 0.7f * ((pulse + 1.0f) / 2.0f);

    for (int row = 0; row < GRID_ROWS; row++)
    {
        for (int col = 0; col < GRID_COLS; col++)
        {
            int idx = GetNodeIndex(col, row);
            auto& node = m_nodes[idx];

            // Emptyは描画しない
            if (node.type == FieldNodeType::Empty) continue;

            XMFLOAT2 pos = GetNodeScreenPos(col, row);
            XMFLOAT4 color;

            bool isPlayer = (col == m_playerCol && row == m_playerRow);
            bool canMoveTo = CanMove(col, row);

            if (isPlayer)
            {
                color = XMFLOAT4(1.0f, 1.0f, 0.0f, 1.0f); // 黄色
            }
            else if (canMoveTo && node.visited)
            {
                // 訪問済みの移動可能マスは灰色で点滅
                color = XMFLOAT4(blink * 0.5f, blink * 0.5f, blink * 0.5f, 1.0f);
            }
            else if (canMoveTo)
            {
                // 未訪問の移動可能マスは点滅
                switch (node.type)
                {
                case FieldNodeType::Battle:
                    color = XMFLOAT4(blink, 0.2f, 0.2f, 1.0f); break;
                case FieldNodeType::Rest:
                    color = XMFLOAT4(0.2f, blink, 0.2f, 1.0f); break;
                case FieldNodeType::Boss:
                    color = XMFLOAT4(blink, 0.2f, blink, 1.0f); break;
                default:
                    color = XMFLOAT4(blink, blink, blink, 1.0f); break;
                }
            }
            else if (node.visited)
            {
                // 訪問済みは暗く
                color = XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
            }
            else
            {
                // 通常色
                switch (node.type)
                {
                case FieldNodeType::Battle:
                    color = XMFLOAT4(0.7f, 0.2f, 0.2f, 1.0f); break;
                case FieldNodeType::Rest:
                    color = XMFLOAT4(0.2f, 0.7f, 0.2f, 1.0f); break;
                case FieldNodeType::Boss:
                    color = XMFLOAT4(0.7f, 0.2f, 0.7f, 1.0f); break;
                case FieldNodeType::Start:
                    color = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f); break;
                default:
                    color = XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f); break;
                }
            }

            m_spriteRenderer->DrawSprite(m_whiteTexture,
                pos.x, pos.y, CELL_SIZE, CELL_SIZE, 0.0f, color);
        }
    }

    m_spriteRenderer->End();

    m_textRenderer->Begin();

    // マスのラベル
    for (int row = 0; row < GRID_ROWS; row++)
    {
        for (int col = 0; col < GRID_COLS; col++)
        {
            int idx = GetNodeIndex(col, row);
            auto& node = m_nodes[idx];
            if (node.type == FieldNodeType::Empty) continue;

            XMFLOAT2 pos = GetNodeScreenPos(col, row);

            const wchar_t* label = L"";
            if (col == m_playerCol && row == m_playerRow)
                label = L"YOU";
            else switch (node.type)
            {
            case FieldNodeType::Start:  label = L"START";  break;
            case FieldNodeType::Battle: label = L"BATTLE"; break;
            case FieldNodeType::Rest:   label = L"REST";   break;
            case FieldNodeType::Boss:   label = L"BOSS";   break;
            default: break;
            }

            m_textRenderer->DrawText(label,
                pos.x + 5.0f, pos.y + CELL_SIZE / 2.0f - 10.0f,
                14.0f, D2D1::ColorF(D2D1::ColorF::White));
        }
    }

    // HP・歩数表示
    auto& playerData = PlayerDataManager::GetData();
    wchar_t hpText[64];
    swprintf_s(hpText, L"HP: %d / %d", playerData.hp, playerData.maxHp);
    m_textRenderer->DrawText(hpText, 20.0f, 20.0f, 24.0f,
        D2D1::ColorF(D2D1::ColorF::Black));

    wchar_t stepsText[64];
    swprintf_s(stepsText, L"歩数: %d / %d", m_steps, m_maxSteps);
    m_textRenderer->DrawText(stepsText, 20.0f, 50.0f, 24.0f,
        m_steps <= 5
        ? D2D1::ColorF(D2D1::ColorF::Red)
        : D2D1::ColorF(D2D1::ColorF::Black));

    m_textRenderer->End();
}

void FieldScene::HandleInput()
{
    POINT mousePos = m_input.GetMousePos();

    if (!m_input.GetMouseButtonTrigger(0)) return;

    // クリックされたマスを判定
    for (int row = 0; row < GRID_ROWS; row++)
    {
        for (int col = 0; col < GRID_COLS; col++)
        {
            XMFLOAT2 pos = GetNodeScreenPos(col, row);

            if (mousePos.x >= pos.x && mousePos.x <= pos.x + CELL_SIZE
                && mousePos.y >= pos.y && mousePos.y <= pos.y + CELL_SIZE)
            {
                if (!CanMove(col, row)) return;

                int idx = GetNodeIndex(col, row);
                auto& node = m_nodes[idx];

                m_playerCol = col;
                m_playerRow = row;
                m_steps--;

                if (!node.visited)
                {
                    switch (node.type)
                    {
                    case FieldNodeType::Battle:
                    case FieldNodeType::Boss:
                        m_currentEnemyId = (node.type == FieldNodeType::Boss) ? "dragon" : node.enemyId;
                        SaveProgress();
                        if (onChangeScene)
                            onChangeScene(SceneType::Battle);
                        return;

                    case FieldNodeType::Rest:
                    {
                        node.visited = true;
                        auto& playerData = PlayerDataManager::GetData();
                        playerData.hp = min(playerData.hp + 20, playerData.maxHp);
                        SaveProgress();
                        break;
                    }
                    default:
                        node.visited = true;
                        break;
                    }
                }
                else
                {
                    SaveProgress(); // ← 訪問済みマスへの移動時も保存
                }
                return;
            }
        }
    }
}

void FieldScene::SaveProgress()
{
    auto& playerData = PlayerDataManager::GetData();
    playerData.fieldPlayerCol = m_playerCol;
    playerData.fieldPlayerRow = m_playerRow;
    playerData.fieldSteps = m_steps;

    // ノード情報を保存
    playerData.fieldNodeTypes.clear();
    playerData.fieldNodeEnemyIds.clear();
    playerData.fieldNodeVisited.clear();
    for (auto& node : m_nodes)
    {
        playerData.fieldNodeTypes.push_back((int)node.type);
        playerData.fieldNodeEnemyIds.push_back(node.enemyId);
        playerData.fieldNodeVisited.push_back(node.visited);
    }

    PlayerDataManager::Save();
}