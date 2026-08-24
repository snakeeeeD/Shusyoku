#include "FieldScene.h"
#include "FieldMapConfig.h"
#include "EventDataBase.h"
#include "EncounterDataBase.h"
#include "SceneType.h"
#include "Audio.h"
#include "UiWindow.h"

#include <cstdlib>
#include <algorithm>
#include <ctime>
#include <map>
#include <ctime>

#ifdef _DEBUG
#include "External/imgui/imgui.h"
#endif

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
    if ((int)playerData.fieldNodeTypes.size() == GRID_COLS * GRID_ROWS
        && (int)playerData.fieldNodeVisited.size() == GRID_COLS * GRID_ROWS
        && (int)playerData.fieldNodeEnemyIds.size() == GRID_COLS * GRID_ROWS)
    {
        // 既存の読み込み（m_nodes[i] = ...）
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

    // 戦闘中に終了した場合：未クリアの戦闘マス上にいるなら再戦させる
    int cur = GetNodeIndex(m_playerCol, m_playerRow);
    auto& curNode = m_nodes[cur];
    if (!curNode.visited &&
        (curNode.type == FieldNodeType::Battle || curNode.type == FieldNodeType::Boss
            || curNode.type == FieldNodeType::Elite))
    {
        m_currentEnemyId = (curNode.type == FieldNodeType::Boss)
            ? (PlayerDataManager::GetData().layer == 2 ? "l2boss" : "dragon")
            : curNode.enemyId;
        m_currentBattleSeed = cur;
        m_currentBattleOverflow = (m_steps < 0) ? -m_steps : 0;
        m_currentBattleCategory =
            (curNode.type == FieldNodeType::Elite) ? EncCategory::Elite :
            (curNode.type == FieldNodeType::Boss) ? EncCategory::Boss : EncCategory::Normal;
        m_currentBattleTier = 1 + m_playerCol * 3 / GRID_COLS;
        if (m_currentBattleTier > 3) m_currentBattleTier = 3;
        m_resumeBattle = true;
    }

    PlayerDataManager::GetData().fieldSteps = m_steps;

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
        { FieldNodeType::Event,   12,  4 },
        { FieldNodeType::Rest,    5,  4 },
        { FieldNodeType::Shop, 3, 4 },
        { FieldNodeType::Elite, 4, 5 },
        { FieldNodeType::Treasure, 2, 2 },
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

        // 中央行(スタート〜ボスの直線)以外は一定確率で空白＝通行不可にする
        if (row != GRID_ROWS / 2 && rand() % 100 < 20)
            continue;   // Emptyのまま残す（CanMoveがfalseを返す）

        // 配置可能なタイプを重みで選ぶ
        std::vector<std::pair<FieldNodeType, int>> available;
        for (auto& limit : config.typeLimits)
        {
            if (limit.maxCount >= 0 && typeCounts[limit.type] >= limit.maxCount) continue;
            // スタート付近(左3列)はRest/Shopを置かない
            if (col < 3 && (limit.type == FieldNodeType::Rest
                || limit.type == FieldNodeType::Shop
                || limit.type == FieldNodeType::Elite
                || limit.type == FieldNodeType::Treasure))
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

    }

    // スタートから到達できないマスを空白化
    std::vector<bool> reachable(GRID_COLS * GRID_ROWS, false);
    std::vector<std::pair<int, int>> stack;
    stack.push_back({ 0, GRID_ROWS / 2 });
    reachable[GetNodeIndex(0, GRID_ROWS / 2)] = true;

    const int dir[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
    while (!stack.empty())
    {
        auto [c, r] = stack.back(); stack.pop_back();
        for (auto& d : dir)
        {
            int nc = c + d[0], nr = r + d[1];
            if (nc < 0 || nc >= GRID_COLS || nr < 0 || nr >= GRID_ROWS) continue;
            int ni = GetNodeIndex(nc, nr);
            if (reachable[ni] || m_nodes[ni].type == FieldNodeType::Empty) continue;
            reachable[ni] = true;
            stack.push_back({ nc, nr });
        }
    }
    for (int i = 0; i < GRID_COLS * GRID_ROWS; i++)
        if (!reachable[i] && m_nodes[i].type != FieldNodeType::Empty)
            m_nodes[i].type = FieldNodeType::Empty;

    // 行き止まり（隣接1つ以下）を除去。Start/Bossは除く
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (int c = 0; c < GRID_COLS; c++)
            for (int r = 0; r < GRID_ROWS; r++)
            {
                int idx = GetNodeIndex(c, r);
                FieldNodeType t = m_nodes[idx].type;
                if (t == FieldNodeType::Empty || t == FieldNodeType::Start || t == FieldNodeType::Boss)
                    continue;

                int neighbors = 0;
                for (auto& d : dir)
                {
                    int nc = c + d[0], nr = r + d[1];
                    if (nc < 0 || nc >= GRID_COLS || nr < 0 || nr >= GRID_ROWS) continue;
                    if (m_nodes[GetNodeIndex(nc, nr)].type != FieldNodeType::Empty) neighbors++;
                }
                if (neighbors <= 1) { m_nodes[idx].type = FieldNodeType::Empty; changed = true; }
            }
    }

    // 1列目（最初のマス）は必ず戦闘に
    for (int row = 0; row < GRID_ROWS; row++)
    {
        int idx = GetNodeIndex(1, row);
        if (m_nodes[idx].type != FieldNodeType::Empty)
            m_nodes[idx].type = FieldNodeType::Battle;
    }

    // エンカウントを生成時に確定→ノードに保存（＝コンティニューで不変、かつ連続同一を回避）
    EncounterDataBase::ResetLastPick();
    int encLayer = PlayerDataManager::GetData().layer;
    for (int c = 0; c < GRID_COLS; c++)
        for (int r = 0; r < GRID_ROWS; r++)
        {
            int idx = GetNodeIndex(c, r);
            EncCategory cat;
            switch (m_nodes[idx].type)
            {
            case FieldNodeType::Battle: cat = EncCategory::Normal; break;
            case FieldNodeType::Elite:  cat = EncCategory::Elite;  break;
            case FieldNodeType::Boss:   cat = EncCategory::Boss;   break;
            default: continue;
            }
            int tier = 1 + c * 3 / GRID_COLS; if (tier > 3) tier = 3;
            const EncounterData* enc = EncounterDataBase::GetEncounter(encLayer, cat, tier, idx);
            if (enc) m_nodes[idx].enemyId = enc->id;
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

    // 自分のマスには移動不可
    if (col == m_playerCol && row == m_playerRow) return false;

    int dc = abs(col - m_playerCol);
    int dr = abs(row - m_playerRow);
    if (dc + dr != 1) return false;

    int idx = GetNodeIndex(col, row);
    if (m_nodes[idx].type == FieldNodeType::Empty) return false;
    if (m_nodes[idx].type == FieldNodeType::Start) return false;

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
    if (m_resumeBattle)
    {
        m_resumeBattle = false;
        if (onChangeScene) { onChangeScene(SceneType::Battle); return; }
    }

    m_input.Update();

    // 移動可能マスのホバーSE
    POINT mp = m_input.GetMousePos();
    int hov = -1;
    for (int row = 0; row < GRID_ROWS; row++)
        for (int col = 0; col < GRID_COLS; col++)
        {
            XMFLOAT2 p = GetNodeScreenPos(col, row);
            if (mp.x >= p.x && mp.x <= p.x + CELL_SIZE && mp.y >= p.y && mp.y <= p.y + CELL_SIZE)
            {
                bool movable = m_freeMove
                    ? (m_nodes[GetNodeIndex(col, row)].type != FieldNodeType::Empty && !(col == m_playerCol && row == m_playerRow))
                    : CanMove(col, row);
                if (movable) hov = GetNodeIndex(col, row);
            }
        }
    if (hov != m_hoverIdxPrev)
    {
        if (hov >= 0) Audio::PlaySE("Assets/Sound/se/hover.mp3");
        m_hoverIdxPrev = hov;
    }

    m_highlightTimer += deltaTime * 2.0f;
    if (m_highlightTimer > 3.14159f * 2.0f)
        m_highlightTimer = 0.0f;
}

void FieldScene::Draw()
{
    POINT mp = m_input.GetMousePos();

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
                case FieldNodeType::Event:  
                    color = XMFLOAT4(blink, 0.3f, blink, 1.0f); break;   // 通常色スイッチ
                case FieldNodeType::Boss:
                    color = XMFLOAT4(blink, 0.2f, blink, 1.0f); break;
                case FieldNodeType::Shop:
                    color = XMFLOAT4(0.2f, blink, blink, 1.0f); break;
                case FieldNodeType::Elite:
                    color = XMFLOAT4(0.9f, blink * 0.6f, 0.1f, 1.0f); break;
                case FieldNodeType::Treasure:
                    color = XMFLOAT4(blink, blink * 0.8f, 0.15f, 1.0f); break;   // 金
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
                case FieldNodeType::Event: 
                    color = XMFLOAT4(0.6f, 0.3f, 0.8f, 1.0f); break;
                case FieldNodeType::Boss:
                    color = XMFLOAT4(0.7f, 0.2f, 0.7f, 1.0f); break;
                case FieldNodeType::Start:
                    color = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f); break;
                case FieldNodeType::Shop:
                    color = XMFLOAT4(0.2f, 0.7f, 0.7f, 1.0f); break;
                case FieldNodeType::Elite:
                    color = XMFLOAT4(0.9f, 0.5f, 0.1f, 1.0f); break;
                case FieldNodeType::Treasure:
                    color = XMFLOAT4(0.95f, 0.78f, 0.2f, 1.0f); break; 
                default:
                    color = XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f); break;
                }
            }

            bool hovMove = m_hoverEnabled && CanMove(col, row)
                && mp.x >= pos.x && mp.x <= pos.x + CELL_SIZE
                && mp.y >= pos.y && mp.y <= pos.y + CELL_SIZE;
            float ny = hovMove ? pos.y - 8.0f : pos.y;   // ホバーで浮く
            m_spriteRenderer->DrawSprite(TextureManager::Get("ui_node"), pos.x, ny, CELL_SIZE, CELL_SIZE, 0.0f, color);

            // 種類アイコン（現在地はプレイヤー駒）
            const char* icon = (col == m_playerCol && row == m_playerRow)
                ? "node_player" : NodeIconName(node.type);
            if (icon[0])
            {
                float isz = 36.0f;
                float ix = pos.x + (CELL_SIZE - isz) / 2.0f;
                float iy = ny + (CELL_SIZE - isz) / 2.0f;
                m_spriteRenderer->DrawSprite(TextureManager::Get(icon), ix, iy, isz, isz, 0.0f, XMFLOAT4(1, 1, 1, 1));
            }
        }
    }

    m_spriteRenderer->End();

    // ===== 凡例（左端）：アイコン＋名前、ホバーで説明ウィンドウ =====
    {
        struct Key { FieldNodeType type; const wchar_t* name; const wchar_t* desc; };
        static const Key keys[] = {
            { FieldNodeType::Battle,   L"バトル",   L"敵と戦闘。勝つとカード報酬。" },
            { FieldNodeType::Elite,    L"エリート", L"強敵。倒すとより多くの報酬がもらえる" },
            { FieldNodeType::Boss,     L"ボス",     L"層の主。倒すと次の層へ進む。" },
            { FieldNodeType::Rest,     L"休憩",     L"回復／カード強化／クラフトから1つ。" },
            { FieldNodeType::Shop,     L"ショップ", L"ゴールドで購入・カード削除・素材売却。" },
            { FieldNodeType::Event,    L"イベント", L"選択で結果が変わる出来事。" },
            { FieldNodeType::Treasure, L"財宝",     L"レリックや素材が手に入る。" },
        };
        const int N = (int)(sizeof(keys) / sizeof(keys[0]));
        const float px = 12.0f, py = 96.0f, pw = 150.0f, rowH = 40.0f, isz = 26.0f;
        const float ph = 34.0f + N * rowH;
        int hover = -1;

        m_spriteRenderer->Begin();
        UiWindow::Draw(m_spriteRenderer, m_whiteTexture, px, py, pw, ph);
        for (int i = 0; i < N; i++)
        {
            float ry = py + 30.0f + i * rowH;
            if (mp.x >= px && mp.x <= px + pw && mp.y >= ry && mp.y <= ry + rowH) hover = i;
            m_spriteRenderer->DrawSprite(TextureManager::Get(NodeIconName(keys[i].type)),
                px + 10.0f, ry + (rowH - isz) / 2.0f, isz, isz, 0.0f, XMFLOAT4(1, 1, 1, 1));
        }
        m_spriteRenderer->End();

        m_textRenderer->Begin();
        m_textRenderer->DrawText(L"マップの記号", px + 12.0f, py + 6.0f, 16.0f, D2D1::ColorF(1, 0.9f, 0.6f));
        for (int i = 0; i < N; i++)
        {
            float ry = py + 30.0f + i * rowH;
            m_textRenderer->DrawText(keys[i].name, px + 46.0f, ry + rowH / 2.0f - 10.0f, 16.0f, D2D1::ColorF(D2D1::ColorF::White));
        }
        m_textRenderer->End();

        if (hover >= 0)   // ホバー説明ウィンドウ（右側）
        {
            float tw = 300.0f, th = 40.0f;
            float tx = px + pw + 8.0f;
            float ty = py + 30.0f + hover * rowH + (rowH - th) / 2.0f;
            m_spriteRenderer->Begin();
            UiWindow::Draw(m_spriteRenderer, m_whiteTexture, tx, ty, tw, th);
            m_spriteRenderer->End();
            m_textRenderer->Begin();
            m_textRenderer->DrawText(keys[hover].desc, tx + 12.0f, ty + 11.0f, 14.0f, D2D1::ColorF(0.95f, 0.95f, 0.95f));
            m_textRenderer->End();
        }
    }

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
                if (m_freeMove)
                {
                    if (m_nodes[GetNodeIndex(col, row)].type == FieldNodeType::Empty) return;  // 空白は不可
                    if (col == m_playerCol && row == m_playerRow) return;                       // 現在地は不可
                }
                else if (!CanMove(col, row)) return;

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
                        m_currentEnemyId = (node.type == FieldNodeType::Boss)
                            ? (PlayerDataManager::GetData().layer == 2 ? "l2boss" : "dragon")
                            : node.enemyId;
                        m_currentBattleSeed = idx;
                        m_currentBattleTier = 1 + node.col * 3 / GRID_COLS; if (m_currentBattleTier > 3) m_currentBattleTier = 3;
                        m_currentBattleOverflow = (m_steps < 0) ? -m_steps : 0;
                        m_currentBattleCategory = (node.type == FieldNodeType::Boss)
                            ? EncCategory::Boss : EncCategory::Normal;
                        SaveProgress();
                        if (onChangeScene) onChangeScene(SceneType::Battle);
                        return;

                    case FieldNodeType::Elite:
                        m_currentEnemyId = node.enemyId;
                        m_currentBattleSeed = idx;
                        m_currentBattleTier = 1 + node.col * 3 / GRID_COLS; if (m_currentBattleTier > 3) m_currentBattleTier = 3;
                        m_currentBattleOverflow = (m_steps < 0) ? -m_steps : 0;
                        m_currentBattleCategory = EncCategory::Elite;
                        SaveProgress();
                        if (onChangeScene) onChangeScene(SceneType::Battle);
                        return;

                    case FieldNodeType::Rest:
                    {
                        node.visited = true;
                        SaveProgress();
                        if (onRest) onRest();           // 回復/強化/合成の3択はSceneManager側で
                        break;
                    }
                    case FieldNodeType::Event:
                    {
                        node.visited = true;
                        SaveProgress();

                        if (rand() % 100 < 15)   // 15%で即戦闘（?マスがモンスターに化ける）
                        {
                            m_currentEnemyId = "";                        // encounterはcategory+tierで決まる
                            m_currentBattleSeed = idx;
                            m_currentBattleTier = 1 + node.col * 3 / GRID_COLS; if (m_currentBattleTier > 3) m_currentBattleTier = 3;
                            m_currentBattleOverflow = (m_steps < 0) ? -m_steps : 0;
                            m_currentBattleCategory = EncCategory::Normal;
                            if (onChangeScene) onChangeScene(SceneType::Battle);
                            return;
                        }

                        if (onEvent) onEvent(EventDataBase::RandomId(PlayerDataManager::GetData().layer));
                        break;
                    }
                    case FieldNodeType::Shop:
                        node.visited = true;
                        SaveProgress();
                        if (onChangeScene) onChangeScene(SceneType::Shop);
                        return;
                    case FieldNodeType::Treasure:
                    {
                        node.visited = true;
                        SaveProgress();
                        if (onEvent) onEvent("treasure_field");  // 財宝イベントを開くだけ
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

void FieldScene::SetupEventBattle(EncCategory cat, const std::string& param)
{
    int idx = GetNodeIndex(m_playerCol, m_playerRow);
    bool specific = (!param.empty() && param != "event" && param != "normal");
    m_currentEnemyId = specific ? param : "";   // ← 指名idをbattleへ運ぶ（m_battleEnemyId経由）
    m_currentBattleSeed = idx;
    m_currentBattleTier = 1 + m_playerCol * 3 / GRID_COLS; if (m_currentBattleTier > 3) m_currentBattleTier = 3;
    m_currentBattleOverflow = (m_steps < 0) ? -m_steps : 0;
    m_currentBattleCategory = cat;
}

void FieldScene::DrawImGui()
{
#ifdef _DEBUG
    ImGui::Begin("Field Debug");
    ImGui::InputInt("Steps", &m_steps);
    ImGui::Checkbox("Free Move (teleport)", &m_freeMove);
    ImGui::Text("Overflow: %d", (m_steps < 0) ? -m_steps : 0);

    ImGui::Separator();
    ImGui::Text("Events (click to open)");
    for (const auto& id : EventDataBase::AllIds())
        if (ImGui::Button(id.c_str()) && onEvent)
            onEvent(id);

    ImGui::End();
#endif
}