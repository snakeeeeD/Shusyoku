#include "BattleScene.h"
#include "TextureLoader.h"
#include <algorithm>

BattleScene::BattleScene()
    : m_spriteRenderer(nullptr)
    , m_gridMap(nullptr)
    , m_whiteTexture(nullptr)
    , m_playerCol(0)
    , m_playerRow(0)
    , m_player(nullptr)
    , m_renderer3D(nullptr)
{
}

BattleScene::~BattleScene()
{
    delete m_spriteRenderer;
    delete m_gridMap;
    delete m_renderer3D;
    for (auto enemy : m_enemies)
        delete enemy;
    m_enemies.clear();
    if (m_whiteTexture) m_whiteTexture->Release();
}

bool BattleScene::Init(ID3D11Device* device, ID3D11DeviceContext* context, 
                       int screenWidth, int screenHeight, HWND hWnd, IDXGISwapChain* swapChain)
{
    m_device = device;
    m_context = context;
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;
    m_hWnd = hWnd;

    m_selectedCardIndex = -1;

    m_enemyTurnTimer = ENEMY_TURN_DELAY;

    m_hoveredCell = { -1, -1 };

    // デッキ構築(仮)
    m_deck.AddCard("strike");
    m_deck.AddCard("strike");
    m_deck.AddCard("strike");
    m_deck.AddCard("strike");
    m_deck.AddCard("defend");
    m_deck.AddCard("defend");
    m_deck.AddCard("move");
    m_deck.AddCard("move");
    m_deck.AddCard("move");
    m_deck.ShuffleDrawPile();

    // 最初の手札を引く
    m_hand.AddCard("move");     // 一枚は移動用カードを引く
    for (int i = 0; i < HAND_SIZE - 1; i++)
    {
        std::string id = m_deck.DrawCard();
        if (!id.empty()) m_hand.AddCard(id);
    }

    // スプライトレンダラー
    m_spriteRenderer = new SpriteRenderer();
    if (!m_spriteRenderer->Init(device, context, m_screenWidth, screenHeight))
        return false;

    m_renderer3D = new Renderer3D();
    if (!m_renderer3D->Init(device, context, screenWidth, screenHeight))
        return false;

    // テキストレンダラー
    m_textRenderer = new TextRenderer();
    if (!m_textRenderer->Init(device, context, swapChain))
        return false;

    m_whiteTexture = TextureManager::Get("white");

    // グリッドマップ
    m_gridMap = new GridMap();
    m_gridMap->Init(9, 7, 72.0f);

    // 各マスに白いテクスチャをセット
    for (int row = 0; row < m_gridMap->GetRows(); row++)
        for (int col = 0; col < m_gridMap->GetCols(); col++)
            m_gridMap->GetCell(col, row).gameObject.texture = m_whiteTexture;

    // プレイヤー初期位置
    m_playerCol = m_gridMap->GetCols() / 2;
    m_playerRow = m_gridMap->GetRows() / 2;
    m_gridMap->SetCellType(m_playerCol, m_playerRow, CellType::Player);


    m_player = new Player();
    m_player->gridCol = m_playerCol;
    m_player->gridRow = m_playerRow;
    m_player->worldX = (m_playerCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
    m_player->worldZ = (m_playerRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
    // プレイヤーターン開始時にエネルギー回復
    m_turnManager.onPlayerTurnStart = [this]()
        {
            m_player->RestoreEnergy();
            m_player->ResetBlock();

            // 敵の次の行動を決定
            for (auto enemy : m_enemies)
                enemy->DecideNextAction();

            // 手札を捨て札に
            for (auto card : m_hand.GetCards())
                m_deck.DiscardCard(card->GetId());
            m_hand.Clear();

            // 新しい手札を引く
            m_hand.AddCard("move");     // 一枚は移動用カードを引くように
            for (int i = 0; i < HAND_SIZE; i++)
            {
                std::string id = m_deck.DrawCard();
                if (!id.empty()) m_hand.AddCard(id);
            }
        };

    m_turnManager.onEnemyTurnStart = [this]()
        {
            m_enemyTurnTimer = ENEMY_TURN_DELAY;
            for (auto enemy : m_enemies) {
                enemy->ResetBlock();
            }
        };

    // 敵の初期配置
    auto addEnemy = [&](int col, int row, const std::string& id)
        {
            auto enemy = new Enemy();
            enemy->Init(id);
            enemy->gridCol = col;
            enemy->gridRow = row;
            enemy->worldX = (col - m_gridMap->GetCols() / 2.0f) * 1.1f;
            enemy->worldZ = (row - m_gridMap->GetRows() / 2.0f) * 1.1f;
            m_enemies.push_back(enemy);
            m_gridMap->SetCellType(col, row, CellType::Enemy);
        };

    addEnemy(2, 1, "slime");
    addEnemy(6, 4, "goblin");
    addEnemy(0, 0, "orc");

    for (auto enemy : m_enemies)
        enemy->DecideNextAction();

     m_input.SetWindowHandle(hWnd);

    return true;
}

void BattleScene::Update(float deltaTime)
{
    m_input.Update();

    if (m_turnManager.IsEnemyTurn())
    {
        m_enemyTurnTimer -= deltaTime;
        OutputDebugStringW(L"★ 敵ターン待機中\n");
        if (m_enemyTurnTimer <= 0.0f)
        {
            OutputDebugStringW(L"★ 敵ターン実行\n");
            for (auto enemy : m_enemies)
            {
                int damage = enemy->Think(m_playerCol, m_playerRow, m_gridMap);
                if (damage > 0)
                    m_player->TakeDamage(damage);
            }
            m_enemyTurnTimer = ENEMY_TURN_DELAY;
            m_turnManager.EndTurn();
        }
    }
}

void BattleScene::Draw()
{
    // 3D描画（深度テストあり）
    m_renderer3D->Begin();

    // グリッド描画
    for (int row = 0; row < m_gridMap->GetRows(); row++)
    {
        for (int col = 0; col < m_gridMap->GetCols(); col++)
        {
            float x = (col - m_gridMap->GetCols() / 2.0f) * 1.1f;
            float z = (row - m_gridMap->GetRows() / 2.0f) * 1.1f;
            auto& cell = m_gridMap->GetCell(col, row);
            m_renderer3D->DrawTile(m_whiteTexture, x, z, 1.0f, cell.gameObject.color);
        }
    }

    // キャラクター描画
    m_player->Draw3D(m_renderer3D);
    for (auto enemy : m_enemies)
        enemy->Draw3D(m_renderer3D);

    m_renderer3D->End();

    //左上UI仮
    {
        // 2D描画（深度テストなし）
        //m_spriteRenderer->Begin();
        //DrawHPBar(20.0f, 20.0f, 200.0f, 20.0f, m_player->GetHp(), m_player->GetMaxHp());
        //for (auto enemy : m_enemies)
        //    DrawEnemyHPBar(enemy);
        //m_spriteRenderer->End();

        //// テキスト描画
        //m_textRenderer->Begin();
        //wchar_t hpText[64];
        //swprintf_s(hpText, L"HP: %d / %d", m_player->GetHp(), m_player->GetMaxHp());
        //m_textRenderer->DrawText(hpText, 20.0f, 44.0f, 20.0f,
        //    D2D1::ColorF(D2D1::ColorF::White));
        //m_textRenderer->End();
    }

    // 2D描画
    m_spriteRenderer->Begin();

    // プレイヤーHPバー
    DrawHPBar(20.0f, 60.0f, 200.0f, 30.0f, m_player->GetHp(), m_player->GetMaxHp());

    // 敵のHPバー
    for (auto enemy : m_enemies)
        DrawEnemyHPBar(enemy);

    // カード表示
    const float cardWidth = 80.0f;
    const float cardHeight = 110.0f;
    const float cardY = m_screenHeight - cardHeight - 20.0f;
    const auto& cards = m_hand.GetCards();

    // 手札描画
    for (int i = 0; i < (int)cards.size(); i++)
    {
        float cardX = m_screenWidth / 2.0f
            - (cards.size() * (cardWidth + 10.0f)) / 2.0f
            + i * (cardWidth + 10.0f);

        // 選択中は少し上に
        float offsetY = (i == m_selectedCardIndex) ? -20.0f : 0.0f;

        XMFLOAT4 color = (i == m_selectedCardIndex)
            ? XMFLOAT4(1.0f, 1.0f, 0.0f, 1.0f)  // 選択中は黄色
            : XMFLOAT4(0.2f, 0.4f, 0.8f, 1.0f); // 通常は青

        m_spriteRenderer->DrawSprite(m_whiteTexture, cardX, cardY + offsetY,
            cardWidth, cardHeight, 0.0f, color);
    }

    m_spriteRenderer->End();

    // テキスト描画
    m_textRenderer->Begin();

    // プレイヤーHP数字
    wchar_t hpText[64];
    swprintf_s(hpText, L"%d / %d", m_player->GetHp(), m_player->GetMaxHp());
    m_textRenderer->DrawText(hpText, 20.0f, 2.0f, 45.0f,
        D2D1::ColorF(D2D1::ColorF::White));

    wchar_t energyText[64];
    swprintf_s(energyText, L"Energy: %d / %d", m_player->GetEnergy(), m_player->GetMaxEnergy());
    m_textRenderer->DrawText(energyText, 20.0f, 100.0f, 20.0f,
        D2D1::ColorF(D2D1::ColorF::Yellow));

    if (m_player->GetBlock() > 0)
    {
        wchar_t blockText[64];
        swprintf_s(blockText, L"Block: %d", m_player->GetBlock());
        m_textRenderer->DrawText(blockText, 20.0f, 125.0f, 20.0f,
            D2D1::ColorF(D2D1::ColorF::LightBlue));
    }

    // 敵の表示
    for (auto enemy : m_enemies)
    {
        // 敵のスクリーン座標を取得
        float pitch = XMConvertToRadians(-Renderer3D::BILLBOARD_PITCH);
        XMVECTOR worldPos = XMVectorSet(
            enemy->worldX,
            enemy->worldY + enemy->height * cos(pitch),
            enemy->worldZ + 0.5f - enemy->height * sin(pitch),
            1.0f
        );
        XMMATRIX view = m_renderer3D->GetViewMatrix();
        XMMATRIX proj = m_renderer3D->GetProjectionMatrix();
        XMVECTOR clipPos = XMVector4Transform(worldPos, view * proj);
        XMFLOAT4 clip;
        XMStoreFloat4(&clip, clipPos);

        if (clip.w > 0.0f)
        {
            float screenX = (clip.x / clip.w + 1.0f) * 0.5f * m_screenWidth;
            float screenY = (1.0f - clip.y / clip.w) * 0.5f * m_screenHeight;

            float barWidth = 80.0f;
            float barHeight = 10.0f;

            wchar_t enemyHpText[64];
            swprintf_s(enemyHpText, L"%d / %d", enemy->GetHp(), enemy->GetMaxHp());
            m_textRenderer->DrawText(enemyHpText,
                screenX - barWidth / 2.0f,
                screenY - barHeight - 20.0f, // バーの上に表示
                16.0f,
                D2D1::ColorF(D2D1::ColorF::White));

            // 行動予告
            if (enemy->GetNextAction())
            {
                m_textRenderer->DrawText(
                    enemy->GetNextAction()->description.c_str(),
                    screenX - barWidth / 2.0f,
                    screenY - barHeight - 40.0f,
                    14.0f,
                    D2D1::ColorF(D2D1::ColorF::Orange)
                );
            }

            if (enemy->GetBlock() > 0)
            {
                wchar_t blockText[64];
                swprintf_s(blockText, L" %d", enemy->GetBlock());
                m_textRenderer->DrawText(blockText,
                    screenX + barWidth / 2.0f + 5.0f,
                    screenY - barHeight - 20.0f,
                    16.0f,
                    D2D1::ColorF(D2D1::ColorF::LightBlue));
            }
        }

    }

    // ターン表示
    if (m_turnManager.IsPlayerTurn())
        m_textRenderer->DrawText(L"プレイヤーターン", 500.0f, 20.0f, 24.0f,
            D2D1::ColorF(D2D1::ColorF::White));
    else
        m_textRenderer->DrawText(L"敵ターン", 500.0f, 20.0f, 24.0f,
            D2D1::ColorF(D2D1::ColorF::Red));

 
    // カード名をテキストで表示
    for (int i = 0; i < (int)cards.size(); i++)
    {
        float cardX = m_screenWidth / 2.0f
            - (cards.size() * (cardWidth + 10.0f)) / 2.0f
            + i * (cardWidth + 10.0f);
        float offsetY = (i == m_selectedCardIndex) ? -20.0f : 0.0f;

        std::wstring name(cards[i]->GetData()->name.begin(),
            cards[i]->GetData()->name.end());
        m_textRenderer->DrawText(cards[i]->GetData()->name.c_str(),
            cardX, cardY + offsetY + 10.0f, 14.0f,
            D2D1::ColorF(D2D1::ColorF::White));
    }


    m_textRenderer->End();
}

void BattleScene::HandleInput()
{
    if (!m_turnManager.IsPlayerTurn()) return; // プレイヤーターン以外は無視

    // カード選択中はマウスのマスにハイライト
    if (m_selectedCardIndex >= 0)
    {
        POINT mousePos = m_input.GetMousePos();
        auto result = m_gridMap->GetClickedCell3D(
            mousePos,
            m_renderer3D->GetViewMatrix(),
            m_renderer3D->GetProjectionMatrix(),
            m_screenWidth,
            m_screenHeight
        );

        const CardData* data = m_hand.GetCards()[m_selectedCardIndex]->GetData();

        if (result.cell)
            m_hoveredCell = { result.col, result.row };
        else
            m_hoveredCell = { -1, -1 };

        // プレイヤー位置を基準にハイライト
        UpdateHighlight(m_playerCol, m_playerRow, data);
    }
    else
    {
        m_hoveredCell = { -1, -1 };
        ClearHighlight();
    }


    if (m_input.GetMouseButtonTrigger(0))
    {
        POINT mousePos = m_input.GetMousePos();
        auto result = m_gridMap->GetClickedCell3D(
            mousePos,
            m_renderer3D->GetViewMatrix(),
            m_renderer3D->GetProjectionMatrix(),
            m_screenWidth,
            m_screenHeight
        );

    
        if (result.cell)
        {
            if (m_selectedCardIndex >= 0)
            {
                // カード使用
                const Card* card = m_hand.GetCards()[m_selectedCardIndex];
                const CardData* data = card->GetData();

                if (data->type == CardType::Attack)
                {
                    // 攻撃カード：隣接した敵に使用
                    Enemy* targetEnemy = nullptr;
                    for (auto enemy : m_enemies)
                    {
                        if (enemy->gridCol == result.col && enemy->gridRow == result.row)
                        {
                            targetEnemy = enemy;
                            break;
                        }
                    }

                    if (targetEnemy)
                    {
                        int dc = abs(m_playerCol - result.col);
                        int dr = abs(m_playerRow - result.row);
                        if ((dc + dr) <= data->range)
                        {
                            if (!m_player->UseEnergy(data->cost)) return; // エネルギー不足

                            targetEnemy->TakeDamage(data->value);

                            if (targetEnemy->GetHp() <= 0)
                            {
                                m_gridMap->SetCellType(targetEnemy->gridCol, targetEnemy->gridRow, CellType::Empty);
                                m_enemies.erase(
                                    std::remove(m_enemies.begin(), m_enemies.end(), targetEnemy),
                                    m_enemies.end()
                                );
                                delete targetEnemy;
                            }

                            m_deck.DiscardCard(card->GetId());
                            m_hand.RemoveCard(m_selectedCardIndex);
                            m_selectedCardIndex = -1;
    
                        }
                    }
                }
                else if (data->type == CardType::Move)
                {
                    // 移動カード：空マスに移動
                    if (result.cell->type == CellType::Empty)
                    {
                        if (!m_player->UseEnergy(data->cost)) return; // エネルギー不足

                        // 範囲内か確認
                        int dc = abs(m_playerCol - result.col);
                        int dr = abs(m_playerRow - result.row);
                        if ((dc + dr) > data->range) return; // 範囲外なら無視

                        m_gridMap->SetCellType(m_playerCol, m_playerRow, CellType::Empty);
                        m_playerCol = result.col;
                        m_playerRow = result.row;
                        m_gridMap->SetCellType(m_playerCol, m_playerRow, CellType::Player);
                        m_player->gridCol = m_playerCol;
                        m_player->gridRow = m_playerRow;
                        m_player->worldX = (m_playerCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
                        m_player->worldZ = (m_playerRow - m_gridMap->GetRows() / 2.0f) * 1.1f;

                        m_deck.DiscardCard(card->GetId());
                        m_hand.RemoveCard(m_selectedCardIndex);
                        m_selectedCardIndex = -1;
                    }
                }
                else if (data->type == CardType::Skill)
                {
                    if (!m_player->UseEnergy(data->cost)) return;

                    m_player->AddBlock(data->value);

                    m_deck.DiscardCard(card->GetId());
                    m_hand.RemoveCard(m_selectedCardIndex);
                    m_selectedCardIndex = -1;
                }
            }
            else
            {
             
            }
        }
    }

    // カードのクリック判定
    const float cardWidth = 80.0f;
    const float cardHeight = 110.0f;
    const float cardY = m_screenHeight - cardHeight - 20.0f;
    const auto& cards = m_hand.GetCards();

    POINT mousePos = m_input.GetMousePos();

    for (int i = 0; i < (int)cards.size(); i++)
    {
        float cardX = m_screenWidth / 2.0f
            - (cards.size() * (cardWidth + 10.0f)) / 2.0f
            + i * (cardWidth + 10.0f);
        float offsetY = (i == m_selectedCardIndex) ? -20.0f : 0.0f;

        bool hover = mousePos.x >= cardX && mousePos.x <= cardX + cardWidth
            && mousePos.y >= cardY + offsetY && mousePos.y <= cardY + offsetY + cardHeight;

        if (hover && m_input.GetMouseButtonTrigger(0))
        {
            if (m_selectedCardIndex == i)
                m_selectedCardIndex = -1; // 選択解除
            else
                m_selectedCardIndex = i;  // 選択
            break;
        }
    }

    if (m_input.GetKeyTrigger(VK_RETURN))
        m_turnManager.EndTurn();
}

void BattleScene::DrawHPBar(float x, float y, float width, float height,
    int currentHP, int maxHP)
{
    // 背景（暗い赤）
    m_spriteRenderer->DrawSprite(m_whiteTexture, x, y, width, height,
        0.0f, XMFLOAT4(0.3f, 0.0f, 0.0f, 1.0f));

    // HPバー（緑）
    float ratio = (float)currentHP / (float)maxHP;
    m_spriteRenderer->DrawSprite(m_whiteTexture, x, y, width * ratio, height,
        0.0f, XMFLOAT4(0.0f, 0.8f, 0.0f, 1.0f));
}

void BattleScene::DrawEnemyHPBar(Enemy* enemy)
{
    // 3D座標をスクリーン座標に変換
    float pitch = XMConvertToRadians(-Renderer3D::BILLBOARD_PITCH);
    XMVECTOR worldPos = XMVectorSet(
        enemy->worldX,
        enemy->worldY + enemy->height * cos(pitch),
        enemy->worldZ + 0.5f - enemy->height * sin(pitch),
        1.0f
    );

    XMMATRIX view = m_renderer3D->GetViewMatrix();
    XMMATRIX proj = m_renderer3D->GetProjectionMatrix();

    XMVECTOR clipPos = XMVector4Transform(worldPos, view * proj);

    XMFLOAT4 clip;
    XMStoreFloat4(&clip, clipPos);

    if (clip.w <= 0.0f) return; // カメラの後ろにある場合はスキップ

    float screenX = (clip.x / clip.w + 1.0f) * 0.5f * m_screenWidth;
    float screenY = (1.0f - clip.y / clip.w) * 0.5f * m_screenHeight;

    // HPバーの幅と高さ
    float barWidth = 80.0f;
    float barHeight = 10.0f;
    float barX = screenX - barWidth / 2.0f;
    float barY = screenY - barHeight;

    // 背景（暗い赤）
    m_spriteRenderer->DrawSprite(m_whiteTexture, barX, barY, barWidth, barHeight,
        0.0f, XMFLOAT4(0.3f, 0.0f, 0.0f, 1.0f));

    // HPバー（緑）
    float ratio = (float)enemy->GetHp() / (float)enemy->GetMaxHp();
    m_spriteRenderer->DrawSprite(m_whiteTexture, barX, barY, barWidth * ratio, barHeight,
        0.0f, XMFLOAT4(0.0f, 0.8f, 0.0f, 1.0f));
}

void BattleScene::ClearHighlight()
{
    for (auto& [col, row] : m_highlightCells)
    {
        // 元のセルタイプに応じた色に戻す
        auto& cell = m_gridMap->GetCell(col, row);
        m_gridMap->SetCellType(col, row, cell.type);
    }
    m_highlightCells.clear();
}

void BattleScene::UpdateHighlight(int centerCol, int centerRow, const CardData* data)
{
    ClearHighlight();
    if (!data) return;

    std::vector<std::pair<int, int>> candidates;

    switch (data->rangeType)
    {
    case RangeType::Adjacent:
        candidates = {
            {centerCol,     centerRow - 1},
            {centerCol,     centerRow + 1},
            {centerCol - 1, centerRow    },
            {centerCol + 1, centerRow    },
        };
        break;
    case RangeType::Cross:
        for (int i = 1; i <= data->range; i++)
        {
            candidates.push_back({ centerCol,     centerRow - i });
            candidates.push_back({ centerCol,     centerRow + i });
            candidates.push_back({ centerCol - i, centerRow });
            candidates.push_back({ centerCol + i, centerRow });
        }
        break;
    case RangeType::Area:
        for (int dr = -data->range; dr <= data->range; dr++)
            for (int dc = -data->range; dc <= data->range; dc++)
                if (abs(dc) + abs(dr) <= data->range)
                    candidates.push_back({ centerCol + dc, centerRow + dr });
        break;
    case RangeType::None:
    default:
        return;
    }

    for (auto& [col, row] : candidates)
    {
        if (col < 0 || col >= m_gridMap->GetCols()) continue;
        if (row < 0 || row >= m_gridMap->GetRows()) continue;

        m_highlightCells.push_back({ col, row });

        auto& cell = m_gridMap->GetCell(col, row);

        // マウスが乗っているマスは強調色
        if (col == m_hoveredCell.first && row == m_hoveredCell.second)
        {
            if (data->type == CardType::Attack)
                cell.gameObject.color = XMFLOAT4(1.0f, 0.2f, 0.2f, 1.0f); // 強い赤
            else if (data->type == CardType::Move)
                cell.gameObject.color = XMFLOAT4(0.2f, 1.0f, 0.2f, 1.0f); // 強い緑
        }
        else
        {
            // 範囲内は薄い色
            if (data->type == CardType::Attack)
                cell.gameObject.color = XMFLOAT4(1.0f, 0.5f, 0.5f, 1.0f); // 薄い赤
            else if (data->type == CardType::Move)
                cell.gameObject.color = XMFLOAT4(0.5f, 1.0f, 0.7f, 1.0f); // 薄い緑
        }
    }
}