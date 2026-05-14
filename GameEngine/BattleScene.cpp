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
    , m_highlightTimer(0.0f)
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

    m_hoveredCardIndex  = -1;

    m_prevHoveredCardIndex = -1;

    m_enemyTurnTimer = ENEMY_TURN_DELAY;

    m_hoveredCell = { -1, -1 };

    m_battleResult = BattleResult::None;

    // PlayerDataManagerからデッキを取得
    auto& playerData = PlayerDataManager::GetData();
    for (auto& cardId : playerData.deck)
        m_deck.AddCard(cardId);

    m_deck.ShuffleDrawPile();

    // 最初の手札を引く
    std::vector<std::string> moveCardIds;
    for (auto& cardId : m_deck.GetDrawPile())
    {
        const CardData* data = CardDataBase::Get(cardId);
        if (data && data->type == CardType::Move)
            moveCardIds.push_back(cardId);
    }

    if (!moveCardIds.empty())
    {
        int idx = rand() % (int)moveCardIds.size();
        std::string moveId = m_deck.DrawSpecificCard(moveCardIds[idx]);
        if (!moveId.empty()) m_hand.AddCard(moveId);
    }

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

            for (auto enemy : m_enemies)
                enemy->DecideNextAction();

            // 手札を捨て札に
            for (auto card : m_hand.GetCards())
                m_deck.DiscardCard(card->GetId());
            m_hand.Clear();

            // 山札と捨て札から移動カードを探す
            std::vector<std::string> moveCardIds;
            auto collectMoveCards = [&](const std::vector<std::string>& pile)
                {
                    for (auto& cardId : pile)
                    {
                        const CardData* data = CardDataBase::Get(cardId);
                        if (data && data->type == CardType::Move)
                            moveCardIds.push_back(cardId);
                    }
                };
            collectMoveCards(m_deck.GetDrawPile());
            collectMoveCards(m_deck.GetDiscardPile());

            // 移動カードを1枚確定で引く
            if (!moveCardIds.empty())
            {
                int idx = rand() % (int)moveCardIds.size();
                std::string moveId = m_deck.DrawSpecificCard(moveCardIds[idx]);
                if (!moveId.empty()) m_hand.AddCard(moveId);
            }

            // 残りを引く
            for (int i = 0; i < HAND_SIZE - 1; i++)
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

    // ハイライト明滅タイマーを更新
    m_highlightTimer += deltaTime * 0.1f;

    if (m_highlightTimer > 3.14159f)
    {
        m_highlightTimer = 0.0f;
    }


    if (m_battleResult != BattleResult::None) return;   // 勝敗決定後は何もしない

    // 勝利判定
    if (m_enemies.empty())
    {
        m_battleResult = BattleResult::Win;
        OutputDebugStringW(L"★ バトル勝利！\n");

        // TODO: カード選択画面へ

        return;
    }

    // 敗北判定
    if (m_player->GetHp() <= 0)
    {
        m_battleResult = BattleResult::Lose;
        OutputDebugStringW(L"★ ゲームオーバー\n");

        // TODO: タイトルへ

        return;
    }

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

    // カード表示
    const float cardHideY = m_screenHeight - CARD_HIDE_Y_OFFSET;
    const float cardHoverY = m_screenHeight - CARD_HEIGHT - CARD_HOVER_Y_OFFSET;

    const auto& cards = m_hand.GetCards();

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

    // ホバー中または選択中のカードを最後に描画
    int frontCardIndex = (m_hoveredCardIndex >= 0) ? m_hoveredCardIndex : m_selectedCardIndex;
    // ホバーしていないカードを先に描画
    for (int i = 0; i < (int)cards.size(); i++)
    {
        if (i == m_hoveredCardIndex) continue;
        if (i == m_selectedCardIndex) continue;

        float cardX = m_screenWidth / 2.0f
            - (cards.size() * (CARD_WIDTH + 10.0f)) / 2.0f
            + i * (CARD_WIDTH + 10.0f);

        float drawY = (i == m_selectedCardIndex)
            ? m_screenHeight - CARD_HEIGHT - 20.0f
            : cardHideY;

        XMFLOAT4 color = (i == m_selectedCardIndex)
            ? XMFLOAT4(1.0f, 1.0f, 0.0f, 0.5f)
            : XMFLOAT4(0.2f, 0.4f, 0.8f, 1.0f);

        m_spriteRenderer->DrawSprite(m_whiteTexture, cardX, drawY,
            CARD_WIDTH, CARD_HEIGHT, 0.0f, color);
    }

    // ホバー中のカードを描画
    if (m_hoveredCardIndex >= 0 && m_hoveredCardIndex < (int)cards.size())
    {
        int i = m_hoveredCardIndex;
        float cardX = m_screenWidth / 2.0f
            - (cards.size() * (CARD_WIDTH + 10.0f)) / 2.0f
            + i * (CARD_WIDTH + 10.0f);

        float drawX = cardX - (CARD_HOVER_W - CARD_WIDTH) / 2.0f;

        // 選択中かつホバー中 → ホバー位置
        // 選択中だけ → 少し下の位置
        float drawY = cardHoverY; // ホバー時は通常のホバー位置

        XMFLOAT4 color = (i == m_selectedCardIndex)
            ? XMFLOAT4(0.8f, 0.8f, 0.0f, 0.7f)
            : XMFLOAT4(0.2f, 0.4f, 0.8f, 1.0f);

        m_spriteRenderer->DrawSprite(m_whiteTexture, drawX, drawY,
            CARD_HOVER_W, CARD_HOVER_H, 0.0f, color);
    }

    // 選択中だがホバーしていないカードを描画
    if (m_selectedCardIndex >= 0 && m_selectedCardIndex != m_hoveredCardIndex
        && m_selectedCardIndex < (int)cards.size())
    {
        int i = m_selectedCardIndex;
        float cardX = m_screenWidth / 2.0f
            - (cards.size() * (CARD_WIDTH + 10.0f)) / 2.0f
            + i * (CARD_WIDTH + 10.0f);

        float drawX = cardX - (CARD_HOVER_W - CARD_WIDTH) / 2.0f;
        float drawY = cardHoverY + 40.0f; // ← ホバー位置より少し下

        m_spriteRenderer->DrawSprite(m_whiteTexture, drawX, drawY,
            CARD_HOVER_W, CARD_HOVER_H, 0.0f,
            XMFLOAT4(0.8f, 0.8f, 0.0f, 0.7f));
    }

    m_spriteRenderer->End();

    // テキスト描画
    m_textRenderer->Begin();

    // 通常カードの名前（ホバー中と選択中以外）
    for (int i = 0; i < (int)cards.size(); i++)
    {
        if (i == m_hoveredCardIndex) continue;
        if (i == m_selectedCardIndex) continue; // 選択中も別で描画

        float cardX = m_screenWidth / 2.0f
            - (cards.size() * (CARD_WIDTH + 10.0f)) / 2.0f
            + i * (CARD_WIDTH + 10.0f);

        m_textRenderer->DrawText(cards[i]->GetData()->name.c_str(),
            cardX, cardHideY + 5.0f, 12.0f,
            D2D1::ColorF(D2D1::ColorF::White));
    }

    // 選択中だがホバーしていないカードの詳細
    if (m_selectedCardIndex >= 0 && m_selectedCardIndex != m_hoveredCardIndex
        && m_selectedCardIndex < (int)cards.size())
    {
        int i = m_selectedCardIndex;
        float cardX = m_screenWidth / 2.0f
            - (cards.size() * (CARD_WIDTH + 10.0f)) / 2.0f
            + i * (CARD_WIDTH + 10.0f);
        float drawX = cardX - (CARD_HOVER_W - CARD_WIDTH) / 2.0f;
        float drawY = cardHoverY + 40.0f;

        m_textRenderer->DrawText(cards[i]->GetData()->name.c_str(),
            drawX + 5.0f, drawY + 10.0f, 16.0f,
            D2D1::ColorF(D2D1::ColorF::White));

        wchar_t costText[32];
        swprintf_s(costText, L"Cost: %d", cards[i]->GetData()->cost);
        m_textRenderer->DrawText(costText,
            drawX + 5.0f, drawY + 32.0f, 13.0f,
            D2D1::ColorF(D2D1::ColorF::Yellow));

        m_textRenderer->DrawText(cards[i]->GetData()->description.c_str(),
            drawX + 5.0f, drawY + 55.0f, 12.0f,
            D2D1::ColorF(D2D1::ColorF::LightGray));
    }

    // ホバー中のカードは詳細表示
    if (m_hoveredCardIndex >= 0 && m_hoveredCardIndex < (int)cards.size())
    {
        int i = m_hoveredCardIndex;
        float cardX = m_screenWidth / 2.0f
            - (cards.size() * (CARD_WIDTH + 10.0f)) / 2.0f
            + i * (CARD_WIDTH + 10.0f);
        float drawX = cardX - (CARD_HOVER_W - CARD_WIDTH) / 2.0f;
        float drawY = cardHoverY;

        m_textRenderer->DrawText(cards[i]->GetData()->name.c_str(),
            drawX + 5.0f, drawY + 10.0f, 16.0f,
            D2D1::ColorF(D2D1::ColorF::White));

        wchar_t costText[32];
        swprintf_s(costText, L"Cost: %d", cards[i]->GetData()->cost);
        m_textRenderer->DrawText(costText,
            drawX + 5.0f, drawY + 32.0f, 13.0f,
            D2D1::ColorF(D2D1::ColorF::Yellow));

        m_textRenderer->DrawText(cards[i]->GetData()->description.c_str(),
            drawX + 5.0f, drawY + 55.0f, 12.0f,
            D2D1::ColorF(D2D1::ColorF::LightGray));
    }

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

 
    //// カード名をテキストで表示
    //for (int i = 0; i < (int)cards.size(); i++)
    //{
    //    float cardX = m_screenWidth / 2.0f
    //        - (cards.size() * (cardWidth + 10.0f)) / 2.0f
    //        + i * (cardWidth + 10.0f);
    //    float offsetY = (i == m_selectedCardIndex) ? -20.0f : 0.0f;

    //    std::wstring name(cards[i]->GetData()->name.begin(),
    //        cards[i]->GetData()->name.end());
    //    m_textRenderer->DrawText(cards[i]->GetData()->name.c_str(),
    //        cardX, cardY + offsetY + 10.0f, 14.0f,
    //        D2D1::ColorF(D2D1::ColorF::White));
    //}

    // 勝敗表示
    if (m_battleResult == BattleResult::Win)
    {
        m_textRenderer->DrawText(L"Victory!",
            m_screenWidth / 2.0f - 80.0f, m_screenHeight / 2.0f - 30.0f,
            60.0f, D2D1::ColorF(D2D1::ColorF::Gold));
        m_textRenderer->DrawText(L"Enterキーで次へ",
            m_screenWidth / 2.0f - 80.0f, m_screenHeight / 2.0f + 40.0f,
            24.0f, D2D1::ColorF(D2D1::ColorF::White));
    }
    else if (m_battleResult == BattleResult::Lose)
    {
        m_textRenderer->DrawText(L"Game Over...",
            m_screenWidth / 2.0f - 100.0f, m_screenHeight / 2.0f - 30.0f,
            60.0f, D2D1::ColorF(D2D1::ColorF::Red));
        m_textRenderer->DrawText(L"Enterキーでタイトルへ",
            m_screenWidth / 2.0f - 100.0f, m_screenHeight / 2.0f + 40.0f,
            24.0f, D2D1::ColorF(D2D1::ColorF::White));
    }


    m_textRenderer->End();
}

void BattleScene::HandleInput()
{
    if (!m_turnManager.IsPlayerTurn()) return; // プレイヤーターン以外は無視

    const float cardHideY = m_screenHeight - CARD_HIDE_Y_OFFSET;
    const float cardHoverY = m_screenHeight - CARD_HEIGHT - CARD_HOVER_Y_OFFSET;

    const auto& cards = m_hand.GetCards();

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
                    if (data->rangeType == RangeType::Area)
                    {
                        // 範囲内の全敵にダメージ
                        bool hit = false;
                        std::vector<Enemy*> toDelete;

                        for (auto enemy : m_enemies)
                        {
                            int dc = abs(m_playerCol - enemy->gridCol);
                            int dr = abs(m_playerRow - enemy->gridRow);
                            if (max(dc, dr) <= data->range)
                            {
                                hit = true;
                                enemy->TakeDamage(data->value);
                                if (enemy->GetHp() <= 0)
                                    toDelete.push_back(enemy);
                            }
                        }

                        if (hit)
                        {
                            if (!m_player->UseEnergy(data->cost)) return;

                            for (auto enemy : toDelete)
                            {
                                m_gridMap->SetCellType(enemy->gridCol, enemy->gridRow, CellType::Empty);
                                m_enemies.erase(
                                    std::remove(m_enemies.begin(), m_enemies.end(), enemy),
                                    m_enemies.end()
                                );
                                delete enemy;
                            }

                            m_deck.DiscardCard(card->GetId());
                            m_hand.RemoveCard(m_selectedCardIndex);
                            m_selectedCardIndex = -1;
                        }
                    }
                    else
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

    POINT mousePos = m_input.GetMousePos();

    for (int i = 0; i < (int)cards.size(); i++)
    {
        float cardX = m_screenWidth / 2.0f
            - (cards.size() * (CARD_WIDTH + 10.0f)) / 2.0f
            + i * (CARD_WIDTH + 10.0f);

        float hitX, hitY, hitW, hitH;

        if (i == m_selectedCardIndex)
        {
            hitX = cardX - (CARD_HOVER_W - CARD_WIDTH) / 2.0f;
            hitY = cardHoverY + 40.0f;
            hitW = CARD_HOVER_W;
            hitH = CARD_HOVER_H;
        }
        else if (i == m_prevHoveredCardIndex)
        {
            hitX = cardX - (CARD_HOVER_W - CARD_WIDTH) / 2.0f;
            hitY = cardHoverY;
            hitW = CARD_HOVER_W;
            hitH = CARD_HOVER_H;
        }
        else
        {
            hitX = cardX;
            hitY = cardHideY;
            hitW = CARD_WIDTH;
            hitH = CARD_HEIGHT;
        }

        bool hover = mousePos.x >= hitX && mousePos.x <= hitX + hitW
            && mousePos.y >= hitY && mousePos.y <= hitY + hitH;

        if (hover && m_input.GetMouseButtonTrigger(0))
        {
            if (m_selectedCardIndex == i)
                m_selectedCardIndex = -1;
            else
                m_selectedCardIndex = i;
            break;
        }
    }

    m_prevHoveredCardIndex = m_hoveredCardIndex;
    m_hoveredCardIndex = -1;

    for (int i = 0; i < (int)cards.size(); i++)
    {
        float cardX = m_screenWidth / 2.0f
            - (cards.size() * (CARD_WIDTH + 10.0f)) / 2.0f
            + i * (CARD_WIDTH + 10.0f);

        // 通常位置の判定
        bool hoverNormal = mousePos.x >= cardX && mousePos.x <= cardX + CARD_WIDTH
            && mousePos.y >= cardHideY && mousePos.y <= cardHideY + CARD_HEIGHT;

        // 大きいカードの位置の判定
        float hoverDrawX = cardX - (CARD_HOVER_W - CARD_WIDTH) / 2.0f;
        bool hoverBig = mousePos.x >= hoverDrawX && mousePos.x <= hoverDrawX + CARD_HOVER_W
            && mousePos.y >= cardHoverY && mousePos.y <= cardHoverY + CARD_HOVER_H;

        // 選択中の判定
        float selectedDrawX = cardX - (CARD_HOVER_W - CARD_WIDTH) / 2.0f;
        bool hoverSelected = (i == m_selectedCardIndex)
            && mousePos.x >= selectedDrawX && mousePos.x <= selectedDrawX + CARD_HOVER_W
            && mousePos.y >= cardHoverY + 40.0f && mousePos.y <= cardHoverY + 40.0f + CARD_HOVER_H;

        if (hoverNormal || hoverBig || hoverSelected)
        {
            m_hoveredCardIndex = i;
            break;
        }
    }
    
    if (m_input.GetKeyTrigger(VK_RETURN))
        m_turnManager.EndTurn();

    // 勝敗後の入力
    if (m_battleResult == BattleResult::Win)
    {
        if (m_input.GetKeyTrigger(VK_RETURN))
        {
            // TODO: カード選択画面へ
            OutputDebugStringW(L"★ 次のシーンへ\n");
        }
        return;
    }
    if (m_battleResult == BattleResult::Lose)
    {
        if (m_input.GetKeyTrigger(VK_RETURN))
        {
            // TODO: タイトルへ
            OutputDebugStringW(L"★ タイトルへ\n");
        }
        return;
    }
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
        {
            for (int dc = -data->range; dc <= data->range; dc++)
            {
                if (max(abs(dc), abs(dr)) <= data->range)
                {
                    candidates.push_back({ centerCol + dc, centerRow + dr });
                }
            }
        }
        break;
    case RangeType::Diamond:
        for (int dr = -data->range; dr <= data->range; dr++)
        {
            for (int dc = -data->range; dc <= data->range; dc++)
            {
                if (abs(dc) + abs(dr) <= data->range && (dc != 0 || dr != 0))
                {
                    candidates.push_back({ centerCol + dc, centerRow + dr });
                }
            }
        }
        break;
    case RangeType::None:
        candidates.push_back({ centerCol, centerRow });
    default:
        return;
    }

    // ゆっくりした明滅の計算（ホバー中のマス用）
    float pulse = sin(m_highlightTimer);
    float hoverBrightness = 0.5f + 0.5f * pulse;

    bool isAreaHovered = false;

    if (data->rangeType == RangeType::Area)
    {
        for (auto& [col, row] : candidates)
        {
            if (col == m_hoveredCell.first &&
                row == m_hoveredCell.second)
            {
                isAreaHovered = true;
                break;
            }
        }
    }

    for (auto& [col, row] : candidates)
    {
        if (col < 0 || col >= m_gridMap->GetCols()) continue;
        if (row < 0 || row >= m_gridMap->GetRows()) continue;

        auto& cell = m_gridMap->GetCell(col, row);

        // 移動カードの場合、敵がいるマスはスキップ
        if (data->type == CardType::Move && cell.type == CellType::Enemy)
            continue;

        m_highlightCells.push_back({ col, row });

        // マウスが乗っているマスかどうか
        bool isHovered = (col == m_hoveredCell.first && row == m_hoveredCell.second);

        bool isEnemy = (cell.type == CellType::Enemy);

        if (data->rangeType == RangeType::Area)
        {
            // Enemyマスは常に最大輝度
            if (isEnemy)
            {
                if (data->type == CardType::Attack)
                    cell.gameObject.color = XMFLOAT4(1.0f, 0.2f, 0.2f, 1.0f);
                else if (data->type == CardType::Move)
                    cell.gameObject.color = XMFLOAT4(0.2f, 1.0f, 0.4f, 1.0f);
            }
            else
            {
                // Area内すべて点滅
                if (data->type == CardType::Attack)
                {
                    float brightness = isAreaHovered ? hoverBrightness : 0.6f;
                    cell.gameObject.color = XMFLOAT4(brightness, 0.2f, 0.2f, 1.0f);
                }
                else if (data->type == CardType::Move)
                {
                    float brightness = isAreaHovered ? hoverBrightness : 0.6f;
                    cell.gameObject.color = XMFLOAT4(0.2f, brightness, 0.4f, 1.0f);
                }
            }
        }
        else
        {
            if (isHovered)
            {
                if (data->type == CardType::Attack)
                    cell.gameObject.color = XMFLOAT4(hoverBrightness, 0.2f, 0.2f, 1.0f);
                else if (data->type == CardType::Move)
                    cell.gameObject.color = XMFLOAT4(0.2f, hoverBrightness, 0.2f, 1.0f);
                else if (data->type == CardType::Skill)
                    cell.gameObject.color = XMFLOAT4(0.2f, hoverBrightness, 0.2f, 1.0f);
            }
            else
            {
                int dc = abs(col - centerCol);
                int dr = abs(row - centerRow);
                int dist = dc + dr;

                float brightness = 0.7f - (float)(dist - 1) / (float)max(1, data->range) * 0.3f;
                brightness = max(0.4f, min(0.7f, brightness));

                if (data->type == CardType::Attack)
                    cell.gameObject.color = XMFLOAT4(brightness, 0.2f, 0.2f, 1.0f);
                else if (data->type == CardType::Move)
                    cell.gameObject.color = XMFLOAT4(0.2f, brightness, 0.4f, 1.0f);
                else if (data->type == CardType::Skill)
                    cell.gameObject.color = XMFLOAT4(0.2f, hoverBrightness, 0.2f, 1.0f);
            }
        }
    }
}