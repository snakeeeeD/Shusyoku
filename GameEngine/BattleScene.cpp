#include "BattleScene.h"
#include "TextureLoader.h"
#include <algorithm>

#ifdef _DEBUG
#include "External/imgui/imgui.h"
#endif

BattleScene::BattleScene()
    : m_battleUI(nullptr)
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
    delete m_battleUI;
    delete m_gridMap;
    delete m_renderer3D;
    delete m_spriteRenderer;
    delete m_player;
    for (auto enemy : m_enemies)
        delete enemy;
    m_enemies.clear();
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

    m_showDrawPile = false;

    m_showDiscardPile = false;

    m_enemyTurnTimer = ENEMY_TURN_DELAY;

    m_hoveredCell = { -1, -1 };

    m_rightClickDragged = false;

    m_cameraZoom = ZOOM_MAX;

    m_isDraggingCamera = false;
    m_dragStartPos = { 0, 0 };

    m_debugRank = 1;

    m_debugEncounterIndex = -1;  // -1 = ランダム

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

    m_renderer3D = new Renderer3D();
    if (!m_renderer3D->Init(device, context, screenWidth, screenHeight))
        return false;

    m_spriteRenderer = new SpriteRenderer();
    m_spriteRenderer->Init(device, context, screenWidth, screenHeight);

    m_battleUI = new BattleUI();
    if (!m_battleUI->Init(device, context, screenWidth, screenHeight, swapChain))
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

    m_cameraOffsetX = m_player->worldX;
    m_cameraOffsetZ = m_player->worldZ;

    // PlayerDataManagerからHPを引き継ぐ
    m_player->SetHp(playerData.hp);

    // プレイヤーターン開始時にエネルギー回復
    m_turnManager.onPlayerTurnStart = [this]()
        {
            m_player->RestoreEnergy();
            m_player->ResetBlock();
            m_player->GetBuffManager().OnTurnEnd();

            // デバフダメージ
            auto dmg = m_player->GetBuffManager().GetTurnEndDamage();
            if (dmg.total() > 0)
                m_player->TakeDamage(dmg.total());

            for (auto enemy : m_enemies)
                enemy->DecideNextAction();

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
                if (!id.empty())
                {
                    m_hand.AddCard(id);
                    m_battleUI->StartDrawCardEffect(id);
                }
            }
        };
    m_turnManager.onEnemyTurnStart = [this]()
        {
            m_enemyTurnTimer = ENEMY_TURN_DELAY;
            for (auto enemy : m_enemies) {
                enemy->ResetBlock();
            }
            m_battleUI->StartDiscardEffects();
            for (auto card : m_hand.GetCards())
                m_deck.DiscardCard(card->GetId());
            m_hand.Clear();
            m_battleUI->ClearCardAnimations();
        };

    const EncounterData* encounter = EncounterDataBase::GetRandom(1);
    if (encounter)
    {
        for (auto& ee : encounter->enemies)
            AddEnemy(ee.col, ee.row, ee.id);
    }

    for (auto enemy : m_enemies)
        enemy->DecideNextAction();

     m_input.SetWindowHandle(hWnd);

    return true;
}

void BattleScene::AddEnemy(int col, int row, const std::string& id)
{
    auto enemy = new Enemy();
    enemy->Init(id);
    enemy->gridCol = col;
    enemy->gridRow = row;

    // 形の中心を計算してワールド座標を設定
    float sumCol = 0, sumRow = 0;
    for (auto& [dc, dr] : enemy->GetGridShape())
    {
        sumCol += col + dc;
        sumRow += row + dr;
    }
    float centerCol = sumCol / enemy->GetGridShape().size();
    float centerRow = sumRow / enemy->GetGridShape().size();
    enemy->worldX = (centerCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
    enemy->worldZ = (centerRow - m_gridMap->GetRows() / 2.0f) * 1.1f;

    // 占有マスをセット
    CellType cellType = enemy->IsBoss() ? CellType::Boss : CellType::Enemy;
    for (auto& [dc, dr] : enemy->GetGridShape())
        m_gridMap->SetCellType(col + dc, row + dr, cellType);

    m_enemies.push_back(enemy);
}

void BattleScene::Update(float deltaTime)
{
    m_input.Update();

#ifdef _DEBUG
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;
#endif


    // カメラズーム（マウスホイール）
    int wheelDelta = m_input.GetMouseWheelDelta();
    if (wheelDelta != 0)
    {
        m_cameraZoom -= wheelDelta > 0 ? ZOOM_SPEED : -ZOOM_SPEED;
        m_cameraZoom = max(ZOOM_MIN, min(ZOOM_MAX, m_cameraZoom));
    }

    // カメラパン（右ドラッグ）
    if (m_input.GetMouseButtonPress(2))
    {
        POINT mousePos = m_input.GetMousePos();
        if (!m_isDraggingCamera)
        {
            m_isDraggingCamera = true;
            m_dragStartPos = mousePos;
            m_rightClickDragged = false;
        }
        else
        {
            float dx = (float)(mousePos.x - m_dragStartPos.x);
            float dy = (float)(mousePos.y - m_dragStartPos.y);

            if (abs(dx) > 1.0f || abs(dy) > 1.0f)
                m_rightClickDragged = true;

            m_cameraOffsetX += dx * 0.02f * m_cameraZoom;
            m_cameraOffsetZ -= dy * 0.02f * m_cameraZoom;

            // クランプはオフセット加算の後
            float gridHalfW = (m_gridMap->GetCols() / 2.0f) * 1.1f;
            float gridHalfH = (m_gridMap->GetRows() / 2.0f) * 1.1f;
            float zoomFactor = (m_cameraZoom > 1.0f) ? 1.0f / m_cameraZoom : 1.0f;  // ズームアウト時に制限が狭くなる
            m_cameraOffsetX = max((-gridHalfW + 2.0f) * zoomFactor, min((gridHalfW - 3.0f) * zoomFactor, m_cameraOffsetX));
            m_cameraOffsetZ = max((-gridHalfH + 1.0f) * zoomFactor, min((gridHalfH - 2.0f) * zoomFactor, m_cameraOffsetZ));

            m_dragStartPos = mousePos;
        }
    }
    else
    {
        if (m_isDraggingCamera && !m_rightClickDragged)
        {
            // ドラッグせずに離した＝単押し→カード選択解除
            if (m_selectedCardIndex >= 0)
                m_selectedCardIndex = -1;
        }
        m_isDraggingCamera = false;
    }

    // カメラリセット（ミドルクリック）
    if (m_input.GetMouseButtonTrigger(2))
    {
        m_cameraZoom = ZOOM_MAX;
        m_cameraOffsetX = m_player->worldX;
        m_cameraOffsetZ = m_player->worldZ;
    }

    // カメラ更新（ズーム or パンが変わったら毎フレーム適用）
    {
        XMFLOAT3 target(
            m_cameraOffsetX,
            -2.0f,
            m_cameraOffsetZ
        );
        XMFLOAT3 zoomedPos(
            m_cameraOffsetX,
            target.y + 17.0f * m_cameraZoom,
            m_cameraOffsetZ + 6.0f * m_cameraZoom
        );
        m_renderer3D->SetCamera(zoomedPos, target, XMFLOAT3(0.0f, 1.0f, 0.0f));

        // ハイライト更新
        if (m_selectedCardIndex >= 0 && m_selectedCardIndex < (int)m_hand.GetCards().size())
        {
            const CardData* data = m_hand.GetCards()[m_selectedCardIndex]->GetData();

            RECT cardArea = { 0, 0, 0, 0 };
            if (!m_hand.GetCards().empty())
            {
                int numCards = (int)m_hand.GetCards().size();
                float totalW = numCards * (CARD_WIDTH + 10.0f);
                float leftX = m_screenWidth / 2.0f - totalW / 2.0f;
                float rightX = leftX + totalW;
                float topY = m_screenHeight - CARD_HIDE_Y_OFFSET;
                cardArea = { (LONG)leftX, (LONG)topY, (LONG)rightX, (LONG)m_screenHeight };
            }

            m_highlighter.UpdatePlayerHighlight(
                m_playerCol, m_playerRow, data,
                m_enemies, m_gridMap, m_player,
                m_highlightTimer, m_hoveredCell,
                m_renderer3D, m_screenWidth, m_screenHeight,
                cardArea);
        }

        // ハイライト明滅タイマーを更新
        m_highlightTimer += deltaTime * 0.1f; // 点滅速度調整
        if (m_highlightTimer > 3.14159f * 2.0f)
            m_highlightTimer = 0.0f;



        if (m_battleResult != BattleResult::None) return;   // 勝敗決定後は何もしない
        m_highlighter.UpdateEnemyHighlight(
            m_enemies, m_gridMap, m_player,
            m_playerCol, m_playerRow, m_highlightTimer);

        m_battleUI->UpdateDrawCardEffects(deltaTime);
        m_battleUI->UpdatePlayCardEffects(deltaTime);
        m_battleUI->UpdateCardAnimations(deltaTime, (int)m_hand.GetCards().size(), m_hoveredCardIndex, m_selectedCardIndex);
        m_battleUI->UpdateDiscardEffects(deltaTime);

        // 勝利判定
        if (m_enemies.empty())
        {
            m_battleResult = BattleResult::Win;

            // HPを保存
            PlayerDataManager::GetData().hp = m_player->GetHp();
            PlayerDataManager::Save();
            return;
        }

        // 敗北判定
        if (m_player->GetHp() <= 0)
        {
            m_battleResult = BattleResult::Lose;

            // HPを0で保存
            PlayerDataManager::GetData().hp = 0;
            PlayerDataManager::Save();
            return;
        }

        if (m_turnManager.IsEnemyTurn())
        {
            m_highlighter.ClearPlayerHighlight(m_gridMap);
            m_highlighter.ClearEnemyHighlight(m_gridMap);
            m_enemyTurnTimer -= deltaTime;
            if (m_enemyTurnTimer <= 0.0f)
            {
                for (auto enemy : m_enemies)
                {
                    auto dmg = enemy->GetBuffManager().GetTurnEndDamage();
                    if (dmg.total() > 0)
                    {
                        enemy->TakeDamage(dmg.total());
                        if (enemy->GetHp() <= 0)
                            continue;
                    }

                    int damage = enemy->Think(m_playerCol, m_playerRow, m_gridMap);
                    if (damage > 0)
                    {
                        m_player->TakeDamage(damage);
                    }
                }

                ProcessDeadEnemies();

                // 敵のバフ更新
                for (auto enemy : m_enemies)
                    enemy->GetBuffManager().OnTurnEnd();

                m_enemyTurnTimer = ENEMY_TURN_DELAY;
                m_turnManager.EndTurn();
            }
        }

        if (m_selectedCardIndex >= 0)
        {
            const CardData* data =
                m_hand.GetCards()[m_selectedCardIndex]->GetData();

            // 手札エリアの範囲を計算
            RECT cardArea = { 0, 0, 0, 0 };
            if (!m_hand.GetCards().empty())
            {
                int numCards = (int)m_hand.GetCards().size();
                float totalW = numCards * (CARD_WIDTH + 10.0f);
                float leftX = m_screenWidth / 2.0f - totalW / 2.0f;
                float rightX = leftX + totalW;
                float topY = m_screenHeight - CARD_HEIGHT - CARD_HOVER_Y_OFFSET;
                cardArea = { (LONG)leftX, (LONG)topY, (LONG)rightX, (LONG)m_screenHeight };
            }

            m_highlighter.UpdatePlayerHighlight(
                m_playerCol, m_playerRow, data,
                m_enemies, m_gridMap, m_player,
                m_highlightTimer, m_hoveredCell,
                m_renderer3D, m_screenWidth, m_screenHeight,
                cardArea);
        }
    }
}

void BattleScene::Draw()
{
    // 背景
    m_spriteRenderer->Begin();
    m_spriteRenderer->DrawSprite(
        TextureManager::Get("battle_bg"),
        0.0f, 0.0f,
        (float)m_screenWidth, (float)m_screenHeight,
        0.0f, XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f));
    m_spriteRenderer->End();

    // 3D描画
    m_renderer3D->Begin();

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

    m_player->Draw3D(m_renderer3D);
    for (auto enemy : m_enemies)
        enemy->Draw3D(m_renderer3D);

    m_renderer3D->End();

    // 2D UI描画
    BattleUIContext ctx;
    ctx.player = m_player;
    ctx.enemies = &m_enemies;
    ctx.hand = &m_hand;
    ctx.deck = &m_deck;
    ctx.turnManager = &m_turnManager;
    ctx.renderer3D = m_renderer3D;
    ctx.gridMap = m_gridMap;
    ctx.playerCol = m_playerCol;
    ctx.playerRow = m_playerRow;
    ctx.selectedCardIndex = m_selectedCardIndex;
    ctx.hoveredCardIndex = m_hoveredCardIndex;
    ctx.hoveredCell = m_hoveredCell;
    ctx.highlightTimer = m_highlightTimer;
    ctx.battleResult = m_battleResult;
    ctx.showDrawPile = m_showDrawPile;
    ctx.showDiscardPile = m_showDiscardPile;
    ctx.screenWidth = m_screenWidth;
    ctx.screenHeight = m_screenHeight;
    ctx.cameraZoom = m_cameraZoom;
    ctx.isPlayerTurn = m_turnManager.IsPlayerTurn();
    ctx.mousePos = m_input.GetMousePos();

    m_battleUI->Draw(ctx);
}

void BattleScene::HandleInput()
{
    if (!m_turnManager.IsPlayerTurn()) return; // プレイヤーターン以外は無視

    const float cardHideY = m_screenHeight - CARD_HIDE_Y_OFFSET;
    const float cardHoverY = m_screenHeight - CARD_HEIGHT - CARD_HOVER_Y_OFFSET;

    const auto& cards = m_hand.GetCards();

    // 山札・捨て札ボタン判定
    float drawPileX = 20.0f;
    float drawPileY = m_screenHeight - 60.0f;
    float discardX = 80.0f;
    float discardY = m_screenHeight - 60.0f;

    // カード選択中はマウスのマスにハイライト
    if (m_selectedCardIndex >= 0)
    {
        POINT mousePos = m_input.GetMousePos();
        POINT gridMousePos = mousePos;
        if (gridMousePos.y > m_screenHeight + 200)
            gridMousePos.y = m_screenHeight + 200;
        auto result = m_gridMap->GetClickedCell3D(
            gridMousePos,
            m_renderer3D->GetViewMatrix(),
            m_renderer3D->GetProjectionMatrix(),
            m_screenWidth,
            m_screenHeight
        );

        if (result.cell)
            m_hoveredCell = { result.col, result.row };
    }
    else
    {
        m_hoveredCell = { -1, -1 };
        m_highlighter.ClearPlayerHighlight(m_gridMap);
    }

    bool isOnCard = false;

    if (m_input.GetMouseButtonTrigger(0))
    {
        POINT mousePos = m_input.GetMousePos();

        // カードエリアにマウスがあるかチェック
        isOnCard = false;
        if (m_selectedCardIndex < 0)
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

            if (mousePos.x >= hitX && mousePos.x <= hitX + hitW
                && mousePos.y >= hitY && mousePos.y <= hitY + hitH)
            {
                isOnCard = true;
                break;
            }
        }

        POINT gridMousePos2 = mousePos;
        if (gridMousePos2.y > m_screenHeight - 200)
            gridMousePos2.y = m_screenHeight - 200;
        auto result = m_gridMap->GetClickedCell3D(
            gridMousePos2,
            m_renderer3D->GetViewMatrix(),
            m_renderer3D->GetProjectionMatrix(),
            m_screenWidth,
            m_screenHeight
        );

        // 山札ボタン
        if (mousePos.x >= drawPileX && mousePos.x <= drawPileX + 50.0f
            && mousePos.y >= drawPileY && mousePos.y <= drawPileY + 40.0f)
        {
            m_showDrawPile = !m_showDrawPile;
            m_showDiscardPile = false;
        }

        // 捨て札ボタン
        else if (mousePos.x >= discardX && mousePos.x <= discardX + 50.0f
            && mousePos.y >= discardY && mousePos.y <= discardY + 40.0f)
        {
            m_showDiscardPile = !m_showDiscardPile;
            m_showDrawPile = false;
        }

        // ビューワー外クリックで閉じる
        else if (m_showDrawPile || m_showDiscardPile)
        {
            float bgX = m_screenWidth / 2.0f - 300.0f;
            float bgY = 50.0f;
            bool outsideBg = mousePos.x < bgX || mousePos.x > bgX + 600.0f
                || mousePos.y < bgY || mousePos.y > bgY + 580.0f;
            if (outsideBg)
            {
                m_showDrawPile = false;
                m_showDiscardPile = false;
            }
        }

    
        if (result.cell && !isOnCard)
        {
            if (m_selectedCardIndex >= 0)
            {
                const Card* card = m_hand.GetCards()[m_selectedCardIndex];
                std::string cardId = card->GetId();
                CardData dataCopy = *card->GetData();

                int newPlayerCol = m_playerCol;
                int newPlayerRow = m_playerRow;

                float playCardX = m_screenWidth / 2.0f
                    - (m_hand.GetCards().size() * (CARD_WIDTH + 10.0f)) / 2.0f
                    + m_selectedCardIndex * (CARD_WIDTH + 10.0f);
                float playCardY = m_screenHeight - 30.0f;

                auto execResult = m_cardExecutor.Execute(
                    dataCopy, cardId,
                    result.col, result.row,
                    m_player, m_enemies, m_gridMap,
                    m_playerCol, m_playerRow,
                    m_hand, m_selectedCardIndex, m_deck,
                    newPlayerCol, newPlayerRow
                );

                if (execResult.cardUsed)
                {
                    // プレイヤー座標を更新
                    if (newPlayerCol != m_playerCol || newPlayerRow != m_playerRow)
                    {
                        m_playerCol = newPlayerCol;
                        m_playerRow = newPlayerRow;
                        m_player->gridCol = m_playerCol;
                        m_player->gridRow = m_playerRow;
                        m_player->worldX = (m_playerCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
                        m_player->worldZ = (m_playerRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
                    }

                    m_battleUI->StartPlayCardEffect(dataCopy.type, playCardX, playCardY);
                    m_battleUI->OnCardRemoved(m_selectedCardIndex);

                    ProcessDeadEnemies();
                    m_selectedCardIndex = -1;
                }
            }
        }
        else if (m_selectedCardIndex >= 0 && !isOnCard)
        {
            m_selectedCardIndex = -1;
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

    if (m_selectedCardIndex < 0)
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
    
    
        // ターンエンドボタン（右下）
        if (m_battleResult == BattleResult::None)
        {
            float btnX = m_screenWidth - 160.0f;
            float btnY = m_screenHeight - 60.0f;
            float btnW = 140.0f;
            float btnH = 40.0f;

            if (m_input.GetMouseButtonTrigger(0))
            {
                POINT mp = m_input.GetMousePos();
                if (mp.x >= btnX && mp.x <= btnX + btnW
                    && mp.y >= btnY && mp.y <= btnY + btnH)
                {
                    m_turnManager.EndTurn();
                }
            }
        }

        // 敵パネルクリック → カメラ移動
        if (m_input.GetMouseButtonTrigger(0))
        {
            POINT mp = m_input.GetMousePos();
            float panelX = m_screenWidth - 250.0f;
            float panelY = 10.0f;
            float panelW = 240.0f;
            float entryH = 90.0f;

            for (int i = 0; i < (int)m_enemies.size(); i++)
            {
                float entryY = panelY + i * (entryH + 5.0f);
                if (mp.x >= panelX && mp.x <= panelX + panelW
                    && mp.y >= entryY && mp.y <= entryY + entryH)
                {
                    m_cameraOffsetX = m_enemies[i]->worldX;
                    m_cameraOffsetZ = m_enemies[i]->worldZ;
                    break;
                }
            }
        }

        // 勝敗後のクリック
        if (m_battleResult == BattleResult::Win)
        {
            if (m_input.GetMouseButtonTrigger(0))
            {
                if (onChangeScene)
                    onChangeScene(SceneType::CardSelect);
            }
            return;
        }
        if (m_battleResult == BattleResult::Lose)
        {
            if (m_input.GetMouseButtonTrigger(0))
            {
                if (onChangeScene)
                    onChangeScene(SceneType::Title);
            }
            return;
        }
}

void BattleScene::ProcessDeadEnemies()
{
    std::vector<Enemy*> toDelete;
    for (auto enemy : m_enemies)
        if (enemy->GetHp() <= 0)
            toDelete.push_back(enemy);

    for (auto enemy : toDelete)
    {
        for (auto& [dc, dr] : enemy->GetGridShape())
            m_gridMap->SetCellType(
                enemy->gridCol + dc,
                enemy->gridRow + dr,
                CellType::Empty);

        m_enemies.erase(
            std::remove(m_enemies.begin(), m_enemies.end(), enemy),
            m_enemies.end()
        );
        delete enemy;
    }
}