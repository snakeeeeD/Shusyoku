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

    m_showDrawPile = false;

    m_showDiscardPile = false;

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

    // PlayerDataManagerからHPを引き継ぐ
    m_player->SetHp(playerData.hp);

    // プレイヤーターン開始時にエネルギー回復
    m_turnManager.onPlayerTurnStart = [this]()
        {
            m_player->RestoreEnergy();
            m_player->ResetBlock();
            m_player->GetBuffManager().OnTurnEnd();

            // 毒ダメージ
            int poison = m_player->GetBuffManager().GetPoisonValue();
            if (poison > 0)
            {
                m_player->TakeDamage(poison);
                OutputDebugStringW(L"毒ダメージ\n");
            }

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
                if (!id.empty())
                {
                    m_hand.AddCard(id);
                    StartDrawCardEffect(id);
                }
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
        };

    addEnemy(2, 1, "slime");
    addEnemy(6, 4, "goblin");
    //addEnemy(0, 0, "orc");
    //addEnemy(4, 0, "dragon");

    for (auto enemy : m_enemies)
        enemy->DecideNextAction();

     m_input.SetWindowHandle(hWnd);

    return true;
}

void BattleScene::Update(float deltaTime)
{
    m_input.Update();

    // ハイライト明滅タイマーを更新
    m_highlightTimer += deltaTime * 0.1f; // 点滅速度調整
    if (m_highlightTimer > 3.14159f * 2.0f)
        m_highlightTimer = 0.0f;



    if (m_battleResult != BattleResult::None) return;   // 勝敗決定後は何もしない
    m_highlighter.UpdateEnemyHighlight(
        m_enemies, m_gridMap, m_player,
        m_playerCol, m_playerRow, m_highlightTimer);

    UpdateDrawCardEffects(deltaTime);

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
                int poison = enemy->GetBuffManager().GetPoisonValue();

                if (poison > 0)
                {
                    enemy->TakeDamage(poison);

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

            m_highlighter.UpdatePlayerHighlight(
                m_playerCol, m_playerRow, data,
                m_enemies, m_gridMap, m_player,
                m_highlightTimer, m_hoveredCell);
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

        XMFLOAT4 color = CardVisual::GetCardColor(
            cards[i]->GetData()->type,
            false
        );

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
            : CardVisual::GetCardColor(
                cards[i]->GetData()->type,
                false
            );

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
        float drawY = cardHoverY + 40.0f; // ホバー位置より少し下

        m_spriteRenderer->DrawSprite(m_whiteTexture, drawX, drawY,
            CARD_HOVER_W, CARD_HOVER_H, 0.0f,
            XMFLOAT4(0.8f, 0.8f, 0.0f, 0.7f));
    }

    // 山札ボタン
    float drawPileX = 20.0f;
    float drawPileY = m_screenHeight - 60.0f;
    m_spriteRenderer->DrawSprite(m_whiteTexture, drawPileX, drawPileY,
        50.0f, 40.0f,
        0.0f, XMFLOAT4(0.2f, 0.2f, 0.6f, 1.0f));

    // 捨て札ボタン
    float discardX = 80.0f;
    float discardY = m_screenHeight - 60.0f;
    m_spriteRenderer->DrawSprite(m_whiteTexture, discardX, discardY, 50.0f, 40.0f,
        0.0f, XMFLOAT4(0.5f, 0.2f, 0.2f, 1.0f));

    // ドローアニメーション
    DrawCardEffects();

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

  
        D2D1_COLOR_F descColor = IsCardBoosted(cards[i]->GetData())
            ? D2D1::ColorF(0.4f, 1.0f, 0.4f)
            : D2D1::ColorF(D2D1::ColorF::LightGray);

        m_textRenderer->DrawText(
            GetCardEffectText(cards[i]->GetData()).c_str(),
            drawX + 5.0f, drawY + 55.0f, 12.0f,
            descColor);
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

     
        D2D1_COLOR_F descColor = IsCardBoosted(cards[i]->GetData())
            ? D2D1::ColorF(0.4f, 1.0f, 0.4f)
            : D2D1::ColorF(D2D1::ColorF::LightGray);

        m_textRenderer->DrawText(
            GetCardEffectText(cards[i]->GetData()).c_str(),
            drawX + 5.0f, drawY + 55.0f, 12.0f,
            descColor);
    }

    wchar_t drawText[32];
    swprintf_s(drawText, L"山:%d", m_deck.GetDrawPileCount());
    m_textRenderer->DrawText(drawText, drawPileX + 5.0f, drawPileY + 8.0f, 18.0f,
        D2D1::ColorF(D2D1::ColorF::White));

    wchar_t discardText[32];
    swprintf_s(discardText, L"捨:%d", m_deck.GetDiscardPileCount());
    m_textRenderer->DrawText(discardText, discardX + 5.0f, discardY + 8.0f, 18.0f,
        D2D1::ColorF(D2D1::ColorF::White));

    // 中身表示
    if (m_showDrawPile || m_showDiscardPile)
        DrawPileViewer();

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
    // 敵の表示（山札・捨て札表示中は非表示）
    if (!m_showDrawPile && !m_showDiscardPile)
    {
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
                // 敵のデバフ表示
                if (enemy->GetBuffManager().HasBuff(BuffType::Poison))
                {
                    wchar_t poisonText[32];
                    swprintf_s(poisonText, L"毒%d", enemy->GetBuffManager().GetPoisonValue());
                    m_textRenderer->DrawText(poisonText,
                        screenX - barWidth / 2.0f,
                        screenY - barHeight - 60.0f,
                        14.0f,
                        D2D1::ColorF(0.5f, 0.0f, 0.8f)); // 紫
                }
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

    // バフ表示
    const auto& buffs = m_player->GetBuffManager().GetBuffs();
    float buffY = 155.0f;
    for (auto& buff : buffs)
    {
        wchar_t buffText[64];
        if (buff.isPermanent())
            swprintf_s(buffText, L"%s +%d (永続)", buff.name.c_str(), buff.value);
        else
            swprintf_s(buffText, L"%s +%d (%dターン)", buff.name.c_str(), buff.value, buff.duration);

        m_textRenderer->DrawText(buffText, 20.0f, buffY, 16.0f,
            D2D1::ColorF(D2D1::ColorF::LightGreen));
        buffY += 22.0f;
    }

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

    // 山札・捨て札ボタン判定
    float drawPileX = 20.0f;
    float drawPileY = m_screenHeight - 60.0f;
    float discardX = 80.0f;
    float discardY = m_screenHeight - 60.0f;

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

        // プレイヤー位置を基準にハイライト
        m_highlighter.UpdatePlayerHighlight(
            m_playerCol, m_playerRow, data,
            m_enemies, m_gridMap, m_player,
            m_highlightTimer, m_hoveredCell);
    }
    else
    {
        m_hoveredCell = { -1, -1 };
        m_highlighter.ClearPlayerHighlight(m_gridMap);
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

    
        if (result.cell)
        {
            if (m_selectedCardIndex >= 0)
            {
                const Card* card = m_hand.GetCards()[m_selectedCardIndex];
                std::string cardId = card->GetId();
                CardData dataCopy = *card->GetData();

                int newPlayerCol = m_playerCol;
                int newPlayerRow = m_playerRow;

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

                    ProcessDeadEnemies();
                    m_selectedCardIndex = -1;
                }
            }
        }
    }

    // ESCで閉じる
    if (m_input.GetKeyTrigger(VK_ESCAPE))
    {
        m_showDrawPile = false;
        m_showDiscardPile = false;
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
            if (onChangeScene)
                onChangeScene(SceneType::CardSelect);
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

    // ボスはHPバーを大きく
    float barWidth = enemy->IsBoss() ? 150.0f : 80.0f;
    float barHeight = enemy->IsBoss() ? 16.0f : 10.0f;

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

std::wstring BattleScene::GetCardEffectText(const CardData* data) const
{
    if (!data) return L"";

    // 実際の値を計算
    int actualValue = data->mainEffect.value;
    bool isBoosted = false;

    if (data->type == CardType::Attack)
    {
        actualValue = m_player->GetBuffManager().ApplyAttackBuff(data->mainEffect.value);
        isBoosted = m_player->GetBuffManager().HasBuff(BuffType::AttackUp);
    }
    else if (data->type == CardType::Skill)
    {
        actualValue = m_player->GetBuffManager().ApplyBlockBuff(data->mainEffect.value);
        isBoosted = m_player->GetBuffManager().HasBuff(BuffType::DefenseUp);
    }

    // {value} を実際の数値に置換
    std::wstring result = data->description;
    std::wstring placeholder = L"{value}";
    size_t pos = result.find(placeholder);
    if (pos != std::wstring::npos)
        result.replace(pos, placeholder.size(), std::to_wstring(actualValue));

    return result;
}

bool BattleScene::IsCardBoosted(const CardData* data) const
{
    if (!data) return false;
    if (data->type == CardType::Attack)
        return m_player->GetBuffManager().HasBuff(BuffType::AttackUp);
    if (data->type == CardType::Skill)
        return m_player->GetBuffManager().HasBuff(BuffType::DefenseUp);
    return false;
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

void BattleScene::DrawPileViewer()
{
    const auto& pile = m_showDrawPile
        ? m_deck.GetDrawPile()
        : m_deck.GetDiscardPile();

    const wchar_t* title = m_showDrawPile ? L"山札" : L"捨て札";

    // 背景
    float bgX = m_screenWidth / 2.0f - 300.0f;
    float bgY = 50.0f;
    float bgW = 600.0f;
    float bgH = 580.0f;

    m_spriteRenderer->Begin();
    m_spriteRenderer->DrawSprite(m_whiteTexture, bgX, bgY, bgW, bgH,
        0.0f, XMFLOAT4(0.1f, 0.1f, 0.1f, 0.95f));
    m_spriteRenderer->End();


    // タイトル
    m_textRenderer->DrawText(title, bgX + 20.0f, bgY + 10.0f, 24.0f,
        D2D1::ColorF(D2D1::ColorF::White));

    // 閉じる
    m_textRenderer->DrawText(L"[ESCで閉じる]", bgX + bgW - 150.0f, bgY + 10.0f, 16.0f,
        D2D1::ColorF(D2D1::ColorF::Gray));

    // カード一覧
    // 山札はシャッフル順がわからないようにカード名のみアルファベット順で表示
    // 捨て札は捨てた順で表示
    std::vector<std::string> displayPile = pile;

    if (m_showDrawPile)
    {
        // 山札はソートして順番をわからなくする
        std::sort(displayPile.begin(), displayPile.end());
    }

    const float cardW = 160.0f;
    const float cardH = 50.0f;
    const float padding = 10.0f;
    const int   cols = 3;

    for (int i = 0; i < (int)displayPile.size(); i++)
    {
        const CardData* data = CardDataBase::Get(displayPile[i]);
        if (!data) continue;

        int col = i % cols;
        int row = i / cols;

        float cx = bgX + 20.0f + col * (cardW + padding);
        float cy = bgY + 50.0f + row * (cardH + padding);

        // カード背景
        XMFLOAT4 cardColor;
        switch (data->type)
        {
        case CardType::Attack: cardColor = XMFLOAT4(0.5f, 0.1f, 0.1f, 1.0f); break;
        case CardType::Skill:  cardColor = XMFLOAT4(0.1f, 0.3f, 0.5f, 1.0f); break;
        case CardType::Move:   cardColor = XMFLOAT4(0.1f, 0.4f, 0.2f, 1.0f); break;
        case CardType::Power:  cardColor = XMFLOAT4(0.4f, 0.1f, 0.4f, 1.0f); break;
        default:               cardColor = XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f); break;
        }

        m_spriteRenderer->Begin();
        m_spriteRenderer->DrawSprite(m_whiteTexture, cx, cy, cardW, cardH,
            0.0f, cardColor);
        m_spriteRenderer->End();

        // カード名とコスト
        wchar_t cardText[64];
        swprintf_s(cardText, L"%s  Cost:%d", data->name.c_str(), data->cost);
        m_textRenderer->DrawText(cardText, cx + 5.0f, cy + 5.0f, 14.0f,
            D2D1::ColorF(D2D1::ColorF::White));

        // 説明文
        m_textRenderer->DrawText(
            GetCardEffectText(data).c_str(),
            cx + 5.0f, cy + 26.0f, 11.0f,
            D2D1::ColorF(D2D1::ColorF::LightGray));
    }

}

void BattleScene::StartDrawCardEffect(const std::string& cardId)
{
    DrawCardEffect effect;
    effect.cardId = cardId;
    effect.x = m_screenWidth - 120.0f; // 山札の位置から
    effect.y = m_screenHeight - 60.0f;
    effect.targetX = m_screenWidth / 2.0f;   // 手札の中央へ
    effect.targetY = m_screenHeight - CARD_HIDE_Y_OFFSET;
    effect.alpha = 1.0f;
    effect.timer = 0.0f;
    effect.done = false;
    m_drawCardEffects.push_back(effect);
}

void BattleScene::UpdateDrawCardEffects(float deltaTime)
{
    for (auto& effect : m_drawCardEffects)
    {
        if (effect.done) continue;

        effect.timer += deltaTime;
        float t = min(1.0f, effect.timer / DRAW_EFFECT_DURATION);

        // イージング（ease out）
        float ease = 1.0f - (1.0f - t) * (1.0f - t);

        effect.x = m_screenWidth - 120.0f + (effect.targetX - (m_screenWidth - 120.0f)) * ease;
        effect.y = m_screenHeight - 60.0f + (effect.targetY - (m_screenHeight - 60.0f)) * ease;
        effect.alpha = 1.0f - t * 0.5f;

        if (t >= 1.0f) effect.done = true;
    }

    // 完了したエフェクトを削除
    m_drawCardEffects.erase(
        std::remove_if(m_drawCardEffects.begin(), m_drawCardEffects.end(),
            [](const DrawCardEffect& e) { return e.done; }),
        m_drawCardEffects.end()
    );
}

void BattleScene::DrawCardEffects()
{
    for (auto& effect : m_drawCardEffects)
    {
        m_spriteRenderer->DrawSprite(m_whiteTexture,
            effect.x, effect.y, CARD_WIDTH, CARD_HEIGHT, 0.0f,
            XMFLOAT4(0.2f, 0.4f, 0.8f, effect.alpha));
    }
}