#include "SceneManager.h"
#include "EffectDataBase.h"
#include "EnemyDataBase.h"
#include "CardDataBase.h"
#include "CardVisual.h"
#include "PlayerDataManager.h"
#include "EncounterDataBase.h"
#include "TerrainDataBase.h"

SceneManager::SceneManager() : m_currentScene(nullptr)
{

}

SceneManager::~SceneManager()
{
	delete m_currentScene;
	delete m_textRenderer;
	TextureManager::Shutdown();
}

bool SceneManager::Init(ID3D11Device* device, ID3D11DeviceContext* context, int screenWidth, int screenHeight, HWND hWnd, IDXGISwapChain* swapChain)
{
	m_device = device;
	m_context = context;
	m_screenWidth = screenWidth;
	m_screenHeight = screenHeight;
	m_hWnd = hWnd;
	m_swapChain = swapChain;

	TextureManager::Init(device);
	EnemyDataBase::Init();
	EncounterDataBase::Init();
	CardDataBase::Init();
	TerrainDataBase::Load("Assets/Data/terrains.json");
	EffectDataBase::Load("Assets/Data/effects.json");
	PlayerDataManager::Init();

	TextureManager::Load("white", L"Assets/Test/White.png");
	TextureManager::Load("title", L"Assets/Test/Title.png");
	TextureManager::Load("battle_bg", L"Assets/Field/GrassField.jpg");
	TextureManager::Load("map_bg", L"Assets/Field/Map.jpg");
	TextureManager::Load("cardSelect_bg", L"Assets/Field/CardSelect.jpeg");
	TextureManager::Load("player", L"Assets/Player/yuusya_game.png");
	TextureManager::Load("enemy_slime", L"Assets/Enemy/slime.png");
	TextureManager::Load("enemy_goblin", L"Assets/Enemy/goblin.png");
	TextureManager::Load("enemy_orc", L"Assets/Enemy/orc.png");
	TextureManager::Load("enemy_dragon_red", L"Assets/Enemy/dragon_red.png");
	TextureManager::Load("enemy_archer", L"Assets/Enemy/archer.png");

	m_textRenderer = new TextRenderer();
	if (!m_textRenderer->Init(device, context, swapChain))
		return false;

	m_uiSprite = new SpriteRenderer();
	m_uiSprite->Init(device, context, screenWidth, screenHeight);
	m_uiInput.SetWindowHandle(hWnd);

	ChangeScene(SceneType::Title);
	return true;
}

void SceneManager::ChangeScene(SceneType type)
{
	// 削除前に必要な情報を取得
	std::string battleEnemyId;
	if (type == SceneType::Battle)
	{
		if (auto field = dynamic_cast<FieldScene*>(m_currentScene))
			battleEnemyId = field->GetCurrentBattleEnemyId();
	}

	// 古いシーンを削除
	delete m_currentScene;
	m_currentScene = nullptr;
	m_currentType = type;

	// 新しいシーンを作成
	switch (type)
	{
		case SceneType::Title:
		{
			auto scene = new TitleScene();
			scene->onChangeScene = [this](SceneType type) {ChangeScene(type); };
			m_currentScene = scene;
			break;
		}
		case SceneType::Battle:
		{
			auto scene = new BattleScene();
			if (!battleEnemyId.empty())
				scene->SetEnemyId(battleEnemyId);

			scene->onChangeScene = [this](SceneType type) { ChangeScene(type); };
			m_currentScene = scene;
			break;
		}
		case SceneType::CardSelect:
		{
			auto scene = new CardSelectScene();
			scene->onChangeScene = [this](SceneType type) {ChangeScene(type); };
			m_currentScene = scene;
			break;
		}
		case SceneType::Field:
		{
			auto scene = new FieldScene();
			scene->onChangeScene = [this](SceneType type) { ChangeScene(type); };
			m_currentScene = scene;
			break;
		}
		
	}


	if (m_currentScene)
	{
		m_currentScene->Init(m_device, m_context, m_screenWidth, m_screenHeight, m_hWnd, m_swapChain);
	}
}

void SceneManager::Draw()
{
	if (m_currentScene) m_currentScene->Draw();
	if (m_currentType != SceneType::Title) DrawOverlay();
}

void SceneManager::DrawOverlay()
{
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	int deckCount = (int)PlayerDataManager::GetData().deck.size();

	float btnX = m_screenWidth - 220.0f;

	// === スプライト ===
	m_uiSprite->Begin();
	m_uiSprite->DrawSprite(white, 0.0f, 0.0f, (float)m_screenWidth, BAR_H, 0.0f,
		XMFLOAT4(0.08f, 0.08f, 0.12f, 0.9f));                 // 帯
	m_uiSprite->DrawSprite(white, btnX, 5.0f, 100.0f, 30.0f, 0.0f,
		XMFLOAT4(0.2f, 0.3f, 0.5f, 1.0f));                    // デッキボタン
	if (m_deckOpen)
	{
		m_uiSprite->DrawSprite(white, 0.0f, 0.0f, (float)m_screenWidth, (float)m_screenHeight,
			0.0f, XMFLOAT4(0.0f, 0.0f, 0.0f, 0.8f));          // 暗幕
		DrawDeckCards(false);
	}
	m_uiSprite->End();

	// === テキスト ===
	m_textRenderer->Begin();
	m_textRenderer->DrawText(L"Deck", btnX + 8.0f, 10.0f, 16.0f, D2D1::ColorF(1, 1, 1));  // 仮ラベル（後でテクスチャ）
	wchar_t buf[32];
	swprintf_s(buf, L"%d", deckCount);
	m_textRenderer->DrawText(buf, btnX + 108.0f, 10.0f, 16.0f, D2D1::ColorF(1, 1, 1));    // ボタンの右横に枚数
	if (m_deckOpen) DrawDeckCards(true);
	m_textRenderer->End();
}

void SceneManager::DrawDeckCards(bool textPass)
{
	auto& deck = PlayerDataManager::GetData().deck;
	ID3D11ShaderResourceView* white = TextureManager::Get("white");

	float scale = 1.1f;
	float cw = CardVisual::CARD_W * scale;
	float ch = CardVisual::CARD_H * scale;
	int perRow = 6;
	float gapX = 15.0f, gapY = 20.0f;
	float totalW = perRow * cw + (perRow - 1) * gapX;
	float startX = (m_screenWidth - totalW) / 2.0f;
	float startY = 70.0f;

	// スクロール量をクランプ（レイアウトを知っているここで）
	int rows = ((int)deck.size() + perRow - 1) / perRow;
	float maxScroll = startY + rows * (ch + gapY) - (m_screenHeight - 20.0f);
	if (maxScroll < 0) maxScroll = 0;
	if (m_deckScroll < 0) m_deckScroll = 0;
	if (m_deckScroll > maxScroll) m_deckScroll = maxScroll;

	for (int i = 0; i < (int)deck.size(); i++)
	{
		const CardData* d = CardDataBase::Get(deck[i]);
		if (!d) continue;
		float x = startX + (i % perRow) * (cw + gapX);
		float y = startY + (i / perRow) * (ch + gapY) - m_deckScroll;   // スクロール適用

		if (!textPass)
			CardVisual::DrawBase(m_uiSprite, white, x, y, scale, 0.0f,
				CardVisual::GetCardColor(d->type));
		else
			CardVisual::DrawTexts(m_textRenderer, d, nullptr, x, y, scale, 0.0f, 1.0f);
	}
}

void SceneManager::DrawImGui()
{
	if (m_currentScene)
		m_currentScene->DrawImGui();
}

void SceneManager::HandleInput()
{
	m_uiInput.Update();

	if (m_currentType != SceneType::Title)
	{
		POINT m = m_uiInput.GetMousePos();
		bool click = m_uiInput.GetMouseButtonTrigger(0);
		float btnX = m_screenWidth - 220.0f, btnY = 5.0f, btnW = 100.0f, btnH = 30.0f;
		bool onDeckBtn = click && m.x >= btnX && m.x <= btnX + btnW
			&& m.y >= btnY && m.y <= btnY + btnH;

		if (onDeckBtn && !m_deckOpen) { m_deckOpen = true; m_deckScroll = 0.0f; return; }
		if (m_deckOpen)
		{
			m_deckScroll -= m_uiInput.GetMouseWheelDelta() * 0.5f;
			if (click) m_deckOpen = false;      // ボタンでも空白でもクリックで閉じる
			return;
		}
	}

	// m_uiInputが消費したホイールをシーンへ戻す（ズーム等が効くように）
	Input::SetWheelDelta(m_uiInput.GetMouseWheelDelta());
	if (m_currentScene) m_currentScene->HandleInput();
}

void SceneManager::Update(float deltaTime)
{
	if (m_deckOpen) return;                                    // オーバーレイ中はシーンを止める
	if (m_currentScene) m_currentScene->Update(deltaTime);
}
