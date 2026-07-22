#include "SceneManager.h"
#include "EffectDataBase.h"
#include "EnemyDataBase.h"
#include "CardDataBase.h"
#include "CardVisual.h"
#include "PlayerDataManager.h"
#include "EncounterDataBase.h"
#include "TerrainDataBase.h"

#include "External/imgui/imgui.h"

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
	TextureManager::Load("enemy_reaper", L"Assets/Enemy/reaper.png");

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
	int battleSeed = 0;
	int battleOverflow = 0;
	bool battleIsElite = false;
	if (type == SceneType::Battle)
	{
		if (auto field = dynamic_cast<FieldScene*>(m_currentScene))
		{
			battleEnemyId = field->GetCurrentBattleEnemyId();
			battleSeed = field->GetCurrentBattleSeed();
			battleOverflow = field->GetCurrentBattleOverflow();
			battleIsElite = field->GetCurrentBattleIsElite();
		}
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
			if (!battleEnemyId.empty()) scene->SetEnemyId(battleEnemyId);
			scene->SetBattleSeed(battleSeed);
			scene->SetOverflow(battleOverflow);
			scene->SetElite(battleIsElite);

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
		case SceneType::Shop:
		{
			auto scene = new ShopScene();
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
		m_uiSprite->DrawSprite(white, 20.0f, 45.0f, 100.0f, 28.0f, 0.0f,
			m_deckRemoveMode ? XMFLOAT4(0.7f, 0.2f, 0.2f, 1.0f)
			: XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f));   // 削除トグル
		DrawDeckCards(false);
	}

	m_uiSprite->End();

	// === テキスト ===
	m_textRenderer->Begin();
	m_textRenderer->DrawText(L"Deck", btnX + 8.0f, 10.0f, 16.0f, D2D1::ColorF(1, 1, 1));  // 仮ラベル（後でテクスチャ）
	wchar_t buf[32];
	swprintf_s(buf, L"%d", deckCount);
	m_textRenderer->DrawText(buf, btnX + 108.0f, 10.0f, 16.0f, D2D1::ColorF(1, 1, 1));    // ボタンの右横に枚数
	if (m_deckOpen)
		m_textRenderer->DrawText(L"Remove", 30.0f, 50.0f, 16.0f, D2D1::ColorF(1, 1, 1));
	if (m_deckOpen) DrawDeckCards(true);
	wchar_t gbuf[32];
	swprintf_s(gbuf, L"G:%d", PlayerDataManager::GetData().gold);
	m_textRenderer->DrawText(gbuf, m_screenWidth - 330.0f, 10.0f, 16.0f,
		D2D1::ColorF(1.0f, 0.9f, 0.3f));         // 金色、デッキボタンの左
	m_textRenderer->End();
}

void SceneManager::DrawDeckCards(bool textPass)
{
	auto& deck = PlayerDataManager::GetData().deck;
	ID3D11ShaderResourceView* white = TextureManager::Get("white");

	// スクロールのクランプ
	int perRow = 6;
	float ch = CardVisual::CARD_H * DECK_SCALE;
	int rows = ((int)deck.size() + perRow - 1) / perRow;
	float maxScroll = 70.0f + rows * (ch + 20.0f) - (m_screenHeight - 20.0f);
	if (maxScroll < 0) maxScroll = 0;
	if (m_deckScroll < 0) m_deckScroll = 0;
	if (m_deckScroll > maxScroll) m_deckScroll = maxScroll;

	for (int i = 0; i < (int)deck.size(); i++)
	{
		const CardData* d = CardDataBase::Get(deck[i]);
		if (!d) continue;
		float bx, by; GetDeckCardBase(i, bx, by);

		if (!textPass)
		{
			XMFLOAT4 col = m_deckRemoveMode
				? XMFLOAT4(0.6f, 0.15f, 0.15f, 0.9f)          // 削除モードは赤
				: CardVisual::GetCardColor(d->type);
			CardVisual::DrawBase(m_uiSprite, white, bx, by, DECK_SCALE, 0.0f, col);
		}
		else
			CardVisual::DrawTexts(m_textRenderer, d, nullptr, bx, by, DECK_SCALE, 0.0f, 1.0f);
	}
}

void SceneManager::DrawImGui()
{
#ifdef _DEBUG
	ImGui::Begin("Global Debug");
	int gold = PlayerDataManager::GetData().gold;
	if (ImGui::InputInt("Gold", &gold))
	{
		PlayerDataManager::GetData().gold = gold;
		PlayerDataManager::Save();
	}
	ImGui::End();
#endif
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
			if (click)
			{
				// 削除トグルボタン
				if (m.x >= 20.0f && m.x <= 120.0f && m.y >= 45.0f && m.y <= 73.0f)
				{
					m_deckRemoveMode = !m_deckRemoveMode; return;
				}
				if (m_deckRemoveMode)
				{
					int idx = GetDeckCardAt(m);
					if (idx >= 0) { PlayerDataManager::RemoveCard(idx); return; }  // 削除、開いたまま
					m_deckRemoveMode = false; return;                              // 空白で削除モード解除
				}
				m_deckOpen = false;                                               // 通常は空白で閉じる
			}
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

bool SceneManager::GetDeckCardBase(int i, float& baseX, float& baseY) const
{
	int n = (int)PlayerDataManager::GetData().deck.size();
	if (i < 0 || i >= n) return false;

	float cw = CardVisual::CARD_W * DECK_SCALE;
	float ch = CardVisual::CARD_H * DECK_SCALE;
	int perRow = 6;
	float gapX = 15.0f, gapY = 20.0f;
	float totalW = perRow * cw + (perRow - 1) * gapX;
	float startX = (m_screenWidth - totalW) / 2.0f;
	float startY = 70.0f;

	baseX = startX + (i % perRow) * (cw + gapX);
	baseY = startY + (i / perRow) * (ch + gapY) - m_deckScroll;
	return true;
}

int SceneManager::GetDeckCardAt(POINT p) const
{
	int n = (int)PlayerDataManager::GetData().deck.size();
	for (int i = 0; i < n; i++)
	{
		float bx, by; GetDeckCardBase(i, bx, by);
		float x, y, w, h; CardVisual::GetRect(bx, by, DECK_SCALE, x, y, w, h);
		if (p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h)
			return i;
	}
	return -1;
}
