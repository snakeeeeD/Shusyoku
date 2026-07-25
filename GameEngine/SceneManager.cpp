#include "SceneManager.h"
#include "EffectDataBase.h"
#include "EnemyDataBase.h"
#include "CardDataBase.h"
#include "CardVisual.h"
#include "PlayerDataManager.h"
#include "EncounterDataBase.h"
#include "TerrainDataBase.h"
#include "MaterialDataBase.h"

#include "External/imgui/imgui.h"

struct ItemInfo { std::wstring name, desc; std::string category; bool valid = false; };

static ItemInfo GetItemInfo(const std::string& id)
{
	if (auto b = MaterialDataBase::GetBase(id))
		return { ToWString(b->name), ToWString(b->desc), "Core", true };
	if (auto m = MaterialDataBase::GetMaterial(id))
		return { ToWString(m->name), ToWString(m->desc), "Material", true };
	// 将来：レリック・ポーション等はここに分岐を足すだけ
	return {};
}

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
	MaterialDataBase::Load("Assets/Data/materials.json");
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
	TextureManager::Load("enemy_tentacle", L"Assets/Enemy/cyaegha.png");
	TextureManager::Load("enemy_bear", L"Assets/Enemy/bear.png");

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
	EncCategory battleCategory = EncCategory::Normal;
	if (type == SceneType::Battle)
	{
		if (auto field = dynamic_cast<FieldScene*>(m_currentScene))
		{
			battleEnemyId = field->GetCurrentBattleEnemyId();
			battleSeed = field->GetCurrentBattleSeed();
			battleOverflow = field->GetCurrentBattleOverflow();
			battleCategory = field->GetCurrentBattleCategory();
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
			scene->SetCategory(battleCategory);

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
	if (m_craftOpen) DrawCraft();
	if (m_invOpen) DrawInventory();
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
	// 合成ボタン
	m_uiSprite->DrawSprite(white, m_screenWidth - 360.0f, 5.0f, 90.0f, 30.0f, 0.0f,
		XMFLOAT4(0.4f, 0.3f, 0.5f, 1.0f));
	m_uiSprite->DrawSprite(white, m_screenWidth - 470.0f, 5.0f, 90.0f, 30.0f, 0.0f, XMFLOAT4(0.3f, 0.4f, 0.35f, 1.0f));

	m_uiSprite->DrawSprite(white, btnX, 5.0f, 100.0f, 30.0f, 0.0f,
		XMFLOAT4(0.2f, 0.3f, 0.5f, 1.0f));                    // デッキボタン
	if (m_deckOpen)
	{
		m_uiSprite->DrawSprite(white, 0.0f, 0.0f, (float)m_screenWidth, (float)m_screenHeight,
			0.0f, XMFLOAT4(0.0f, 0.0f, 0.0f, 0.8f));          // 暗幕
		m_uiSprite->DrawSprite(white, 20.0f, 45.0f, 100.0f, 28.0f, 0.0f,
			m_deckRemoveMode ? XMFLOAT4(0.7f, 0.2f, 0.2f, 1.0f)
			: XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f));   // 削除トグル
		m_uiSprite->DrawSprite(white, 130.0f, 45.0f, 100.0f, 28.0f, 0.0f,
			m_deckUpgradeMode ? XMFLOAT4(0.8f, 0.7f, 0.2f, 1.0f)
			: XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f));
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
	// ゴールド
	swprintf_s(gbuf, L"G:%d", PlayerDataManager::GetData().gold);
	m_textRenderer->DrawText(gbuf, m_screenWidth - 560.0f, 10.0f, 16.0f, D2D1::ColorF(1, 0.9f, 0.3f));
	m_textRenderer->DrawText(L"Craft", m_screenWidth - 350.0f, 10.0f, 16.0f, D2D1::ColorF(1, 1, 1));
	m_textRenderer->DrawText(L"Items", m_screenWidth - 460.0f, 10.0f, 16.0f, D2D1::ColorF(1, 1, 1));
	if (m_deckOpen)
		m_textRenderer->DrawText(L"Upgrade", 140.0f, 50.0f, 16.0f, D2D1::ColorF(1, 1, 1));
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
			CardVisual::DrawBase(m_uiSprite, white, bx, by, DECK_SCALE, 0.0f, col, d, m_uiTime);
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
				// 強化トグル
				if (m.x >= 130.0f && m.x <= 230.0f && m.y >= 45.0f && m.y <= 73.0f)
				{
					m_deckUpgradeMode = !m_deckUpgradeMode;
					m_deckRemoveMode = false;
					return;
				}
				if (m_deckUpgradeMode)
				{
					int idx = GetDeckCardAt(m);
					if (idx >= 0) { PlayerDataManager::UpgradeCard(idx); return; }
					m_deckUpgradeMode = false; return;
				}
				m_deckOpen = false;                                               // 通常は空白で閉じる
			}
			return;
		}
	}

	if (m_currentType != SceneType::Title && m_currentType != SceneType::Battle)
	{
		POINT m = m_uiInput.GetMousePos();
		bool click = m_uiInput.GetMouseButtonTrigger(0);

		// Items を開く
		if (click && !m_invOpen && !m_craftOpen
			&& m.x >= m_screenWidth - 470.0f && m.x <= m_screenWidth - 380.0f
			&& m.y >= 5.0f && m.y <= 35.0f)
		{
			m_invOpen = true; return;
		}

		// Craft を開く
		if (click && !m_craftOpen && !m_invOpen
			&& m.x >= m_screenWidth - 360.0f && m.x <= m_screenWidth - 270.0f
			&& m.y >= 5.0f && m.y <= 35.0f)
		{
			m_craftOpen = true; m_craftBase.clear(); m_craftMods.clear(); return;
		}

		if (m_invOpen) { if (click) m_invOpen = false; return; }
		if (m_craftOpen) { if (click) HandleCraftClick(m); return; }
	}

	// m_uiInputが消費したホイールをシーンへ戻す（ズーム等が効くように）
	Input::SetWheelDelta(m_uiInput.GetMouseWheelDelta());
	if (m_currentScene) m_currentScene->HandleInput();
}

void SceneManager::Update(float deltaTime)
{
	m_uiTime += deltaTime;
	if (m_deckOpen || m_craftOpen || m_invOpen) return; // オーバーレイ/合成中はシーンを止める
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

std::vector<std::pair<std::string, int>> SceneManager::CraftInventory() const
{
	std::vector<std::pair<std::string, int>> v;
	for (auto& kv : PlayerDataManager::GetData().materials)
		if (kv.second > 0) v.push_back({ kv.first, kv.second });
	return v;
}

std::string SceneManager::CraftRecipeId() const
{
	if (m_craftBase.empty()) return "";
	std::string id = "CRAFT:" + m_craftBase;
	for (auto& m : m_craftMods) id += "|" + m;
	return id;
}

void SceneManager::GetCraftSlotRect(int i, float& x, float& y, float& w, float& h) const
{
	w = 150.0f; h = 60.0f; y = 130.0f;
	x = m_screenWidth / 2.0f - 250.0f + i * 170.0f;   // 0=土台,1,2=修飾
}
void SceneManager::GetCraftInvRect(int i, float& x, float& y, float& w, float& h) const
{
	w = 190.0f; h = 38.0f;
	float gap = 8.0f; int cols = 3;
	float startX = m_screenWidth / 2.0f - (cols * w + (cols - 1) * gap) / 2.0f;
	x = startX + (i % cols) * (w + gap);
	y = 380.0f + (i / cols) * (h + gap);
}
void SceneManager::GetCraftBtnRect(float& x, float& y, float& w, float& h) const
{
	w = 160.0f; h = 48.0f;
	x = m_screenWidth / 2.0f - 80.0f; y = m_screenHeight - 100.0f;
}

void SceneManager::DrawCraft()
{
	ID3D11ShaderResourceView* white = TextureManager::Get("white");

	auto matName = [](const std::string& id) -> std::wstring {
		if (auto b = MaterialDataBase::GetBase(id)) return ToWString(b->name);
		if (auto m = MaterialDataBase::GetMaterial(id)) return ToWString(m->name);
		return L"?";
		};

	// --- スプライト ---
	m_uiSprite->Begin();
	m_uiSprite->DrawSprite(white, 0, 0, (float)m_screenWidth, (float)m_screenHeight, 0.0f,
		XMFLOAT4(0.0f, 0.0f, 0.05f, 0.9f));

	// 枠
	for (int i = 0; i < 3; i++)
	{
		float x, y, w, h; GetCraftSlotRect(i, x, y, w, h);
		bool filled = (i == 0) ? !m_craftBase.empty() : (int)m_craftMods.size() > i - 1;
		m_uiSprite->DrawSprite(white, x, y, w, h, 0.0f,
			filled ? XMFLOAT4(0.25f, 0.35f, 0.5f, 1.0f) : XMFLOAT4(0.15f, 0.15f, 0.2f, 1.0f));
	}
	// 所持素材
	
		auto bs = CraftBases();
		for (int i = 0; i < (int)bs.size(); i++) {
			float x, y, w, h; GetCraftBaseRect(i, x, y, w, h);
			m_uiSprite->DrawSprite(white, x, y, w, h, 0.0f, XMFLOAT4(0.4f, 0.3f, 0.2f, 1.0f));
		}
		auto md = CraftMods();
		for (int i = 0; i < (int)md.size(); i++) {
			float x, y, w, h; GetCraftModRect(i, x, y, w, h);
			m_uiSprite->DrawSprite(white, x, y, w, h, 0.0f, XMFLOAT4(0.2f, 0.3f, 0.3f, 1.0f));
		}
	
	// 作成ボタン
	{
		float x, y, w, h; GetCraftBtnRect(x, y, w, h);
		bool ready = !m_craftBase.empty();
		m_uiSprite->DrawSprite(white, x, y, w, h, 0.0f,
			ready ? XMFLOAT4(0.3f, 0.55f, 0.3f, 1.0f) : XMFLOAT4(0.25f, 0.25f, 0.25f, 1.0f));
	}
	// プレビューカード（本体）
	std::string rid = CraftRecipeId();
	const CardData* prev = rid.empty() ? nullptr : CardDataBase::Get(rid);
	if (prev)
	{
		float bx = m_screenWidth / 2.0f - CardVisual::CARD_W / 2.0f;
		float by = 210.0f;
		CardVisual::DrawBase(m_uiSprite, white, bx, by, 1.2f, 0.0f,
			CardVisual::GetCardColor(prev->type), prev, m_uiTime);
	}
	m_uiSprite->End();

	// --- テキスト ---
	m_textRenderer->Begin();
	m_textRenderer->DrawText(L"Craft", m_screenWidth / 2.0f - 30.0f, 70.0f, 28.0f, D2D1::ColorF(1, 1, 1));

	const wchar_t* labels[3] = { L"Base", L"Mod1", L"Mod2" };
	for (int i = 0; i < 3; i++)
	{
		float x, y, w, h; GetCraftSlotRect(i, x, y, w, h);
		std::wstring t = labels[i];
		if (i == 0 && !m_craftBase.empty()) t = matName(m_craftBase);
		if (i > 0 && (int)m_craftMods.size() >= i) t = matName(m_craftMods[i - 1]);
		m_textRenderer->DrawText(t.c_str(), x + 8.0f, y + 18.0f, 18.0f, D2D1::ColorF(1, 1, 1));
	}
	// 見出し
	float hdrX = m_screenWidth / 2.0f - (4 * 150.0f + 3 * 8.0f) / 2.0f;   // 箱の左端に揃える
	m_textRenderer->DrawText(L"CORE", hdrX, 356.0f, 18.0f, D2D1::ColorF(1, 0.85f, 0.5f));
	m_textRenderer->DrawText(L"MATERIAL", hdrX, 456.0f, 18.0f, D2D1::ColorF(0.6f, 0.9f, 0.9f));

	auto bases = CraftBases();
	for (int i = 0; i < (int)bases.size(); i++)
	{
		float x, y, w, h; GetCraftBaseRect(i, x, y, w, h);
		wchar_t buf[64];
		swprintf_s(buf, L"%s x%d", matName(bases[i].first).c_str(), bases[i].second);
		m_textRenderer->DrawText(buf, x + 6.0f, y + 8.0f, 15.0f, D2D1::ColorF(1, 1, 1));
	}
	auto mods = CraftMods();
	for (int i = 0; i < (int)mods.size(); i++)
	{
		float x, y, w, h; GetCraftModRect(i, x, y, w, h);
		wchar_t buf[64];
		swprintf_s(buf, L"%s x%d", matName(mods[i].first).c_str(), mods[i].second);
		m_textRenderer->DrawText(buf, x + 6.0f, y + 8.0f, 15.0f, D2D1::ColorF(1, 1, 1));
	}
	{
		float x, y, w, h; GetCraftBtnRect(x, y, w, h);
		m_textRenderer->DrawText(L"Make", x + 55.0f, y + 12.0f, 20.0f, D2D1::ColorF(1, 1, 1));
	}
	if (prev)
		CardVisual::DrawTexts(m_textRenderer, prev, nullptr,
			m_screenWidth / 2.0f - CardVisual::CARD_W / 2.0f, 210.0f, 1.2f, 0.0f, 1.0f);
	m_textRenderer->End();

	std::string hid = HoveredItem(m_uiInput.GetMousePos());
	if (!hid.empty()) DrawItemTooltip(hid, m_uiInput.GetMousePos());
}

void SceneManager::HandleCraftClick(POINT m)
{
	// 枠クリックで解除（最初に判定）
	for (int i = 0; i < 3; i++)
	{
		float x, y, w, h; GetCraftSlotRect(i, x, y, w, h);
		if (m.x >= x && m.x <= x + w && m.y >= y && m.y <= y + h)
		{
			if (i == 0) m_craftBase.clear();
			else if ((int)m_craftMods.size() >= i) m_craftMods.erase(m_craftMods.begin() + (i - 1));
			return;
		}
	}
	// 作成ボタン
	{
		float x, y, w, h; GetCraftBtnRect(x, y, w, h);
		if (m.x >= x && m.x <= x + w && m.y >= y && m.y <= y + h) { DoCraft(); return; }
	}
  // 現在スロットで使っている数を数えるヘルパー
	auto usedCount = [&](const std::string& id) {
		int n = (m_craftBase == id) ? 1 : 0;
		for (auto& mm : m_craftMods) if (mm == id) n++;
		return n;
		};

	// 核クリック → 土台へ
	auto bases = CraftBases();
	for (int i = 0; i < (int)bases.size(); i++) {
		float x, y, w, h; GetCraftBaseRect(i, x, y, w, h);
		if (m.x >= x && m.x <= x + w && m.y >= y && m.y <= y + h) {
			const std::string& id = bases[i].first;
			// 今の土台を外した状態で、所持数に空きがあるか
			int used = usedCount(id) - (m_craftBase == id ? 1 : 0);
			if (used < PlayerDataManager::MaterialCount(id))
				m_craftBase = id;
			return;
		}
	}
	// 素材クリック → 修飾へ
	auto mods = CraftMods();
	for (int i = 0; i < (int)mods.size(); i++) {
		float x, y, w, h; GetCraftModRect(i, x, y, w, h);
		if (m.x >= x && m.x <= x + w && m.y >= y && m.y <= y + h) {
			const std::string& id = mods[i].first;
			if ((int)m_craftMods.size() < 2 && usedCount(id) < PlayerDataManager::MaterialCount(id))
				m_craftMods.push_back(id);
			return;
		}
	}
	// どれでもない所（枠外）→ 閉じる
	m_craftOpen = false;
}

void SceneManager::DoCraft()
{
	if (m_craftBase.empty()) return;

	// 必要数を集計して所持チェック
	std::map<std::string, int> need;
	need[m_craftBase]++;
	for (auto& mod : m_craftMods) need[mod]++;
	for (auto& kv : need)
		if (PlayerDataManager::MaterialCount(kv.first) < kv.second) return;   // 足りない

	// 消費
	for (auto& kv : need)
		PlayerDataManager::AddMaterial(kv.first, -kv.second);

	// デッキに合成カードを追加
	PlayerDataManager::GetData().deck.push_back(CraftRecipeId());
	PlayerDataManager::Save();

	m_craftBase.clear(); m_craftMods.clear();
}

void SceneManager::GetCraftBaseRect(int i, float& x, float& y, float& w, float& h) const
{
	w = 150.0f; h = 38.0f; float gap = 8.0f; int cols = 4;
	float startX = m_screenWidth / 2.0f - (cols * w + (cols - 1) * gap) / 2.0f;
	x = startX + (i % cols) * (w + gap);
	y = 380.0f + (i / cols) * (h + gap);
}
void SceneManager::GetCraftModRect(int i, float& x, float& y, float& w, float& h) const
{
	w = 150.0f; h = 38.0f; float gap = 8.0f; int cols = 4;
	float startX = m_screenWidth / 2.0f - (cols * w + (cols - 1) * gap) / 2.0f;
	x = startX + (i % cols) * (w + gap);
	y = 480.0f + (i / cols) * (h + gap);
}

std::vector<std::pair<std::string, int>> SceneManager::CraftBases() const
{
	std::vector<std::pair<std::string, int>> v;
	for (auto& kv : PlayerDataManager::GetData().materials)
		if (kv.second > 0 && MaterialDataBase::GetBase(kv.first)) v.push_back({ kv.first, kv.second });
	return v;
}
std::vector<std::pair<std::string, int>> SceneManager::CraftMods() const
{
	std::vector<std::pair<std::string, int>> v;
	for (auto& kv : PlayerDataManager::GetData().materials)
		if (kv.second > 0 && !MaterialDataBase::GetBase(kv.first)) v.push_back({ kv.first, kv.second });
	return v;
}

std::string SceneManager::HoveredItem(POINT mp) const
{
	auto bases = CraftBases();
	for (int i = 0; i < (int)bases.size(); i++) {
		float x, y, w, h; GetCraftBaseRect(i, x, y, w, h);
		if (mp.x >= x && mp.x <= x + w && mp.y >= y && mp.y <= y + h) return bases[i].first;
	}
	auto mods = CraftMods();
	for (int i = 0; i < (int)mods.size(); i++) {
		float x, y, w, h; GetCraftModRect(i, x, y, w, h);
		if (mp.x >= x && mp.x <= x + w && mp.y >= y && mp.y <= y + h) return mods[i].first;
	}
	return "";
}

void SceneManager::DrawItemTooltip(const std::string& id, POINT mp)
{
	ItemInfo info = GetItemInfo(id);
	if (!info.valid) return;
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	float w = 320.0f, h = 84.0f;
	float x = (float)mp.x + 16.0f, y = (float)mp.y + 8.0f;
	if (x + w > m_screenWidth) x = m_screenWidth - w - 4.0f;

	m_uiSprite->Begin();
	m_uiSprite->DrawSprite(white, x, y, w, h, 0.0f, XMFLOAT4(0.05f, 0.05f, 0.1f, 0.98f));
	m_uiSprite->End();
	m_textRenderer->Begin();
	m_textRenderer->DrawText(info.name.c_str(), x + 10.0f, y + 8.0f, 18.0f, D2D1::ColorF(1, 0.9f, 0.6f));
	m_textRenderer->DrawText(info.desc.c_str(), x + 10.0f, y + 36.0f, 13.0f, D2D1::ColorF(0.9f, 0.9f, 0.9f));
	m_textRenderer->End();
}

void SceneManager::DrawInventory()
{
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	m_uiSprite->Begin();
	m_uiSprite->DrawSprite(white, 0, 0, (float)m_screenWidth, (float)m_screenHeight, 0.0f, XMFLOAT4(0.05f, 0.05f, 0.08f, 0.85f));
	auto bases = CraftBases();
	for (int i = 0; i < (int)bases.size(); i++) {
		float x, y, w, h; GetCraftBaseRect(i, x, y, w, h);
		m_uiSprite->DrawSprite(white, x, y, w, h, 0.0f, XMFLOAT4(0.4f, 0.3f, 0.2f, 0.85f));
	}
	auto mods = CraftMods();
	for (int i = 0; i < (int)mods.size(); i++) {
		float x, y, w, h; GetCraftModRect(i, x, y, w, h);
		m_uiSprite->DrawSprite(white, x, y, w, h, 0.0f, XMFLOAT4(0.2f, 0.3f, 0.3f, 0.85f));
	}
	m_uiSprite->End();

	m_textRenderer->Begin();
	m_textRenderer->DrawText(L"Items", m_screenWidth / 2.0f - 40.0f, 70.0f, 28.0f, D2D1::ColorF(1, 1, 1));
	float hdrX = m_screenWidth / 2.0f - (4 * 150.0f + 3 * 8.0f) / 2.0f;
	m_textRenderer->DrawText(L"CORE", hdrX, 356.0f, 18.0f, D2D1::ColorF(1, 0.85f, 0.5f));
	if (bases.empty())
		m_textRenderer->DrawText(L"(none)", hdrX + 70.0f, 356.0f, 16.0f, D2D1::ColorF(0.6f, 0.6f, 0.6f));
	m_textRenderer->DrawText(L"MATERIAL", hdrX, 456.0f, 18.0f, D2D1::ColorF(0.6f, 0.9f, 0.9f));
	if (mods.empty())
		m_textRenderer->DrawText(L"(none)", hdrX + 100.0f, 456.0f, 16.0f, D2D1::ColorF(0.6f, 0.6f, 0.6f));
	for (int i = 0; i < (int)bases.size(); i++) {
		float x, y, w, h; GetCraftBaseRect(i, x, y, w, h);
		wchar_t b[64]; swprintf_s(b, L"%s x%d", GetItemInfo(bases[i].first).name.c_str(), bases[i].second);
		m_textRenderer->DrawText(b, x + 6.0f, y + 8.0f, 15.0f, D2D1::ColorF(1, 1, 1));
	}
	for (int i = 0; i < (int)mods.size(); i++) {
		float x, y, w, h; GetCraftModRect(i, x, y, w, h);
		wchar_t b[64]; swprintf_s(b, L"%s x%d", GetItemInfo(mods[i].first).name.c_str(), mods[i].second);
		m_textRenderer->DrawText(b, x + 6.0f, y + 8.0f, 15.0f, D2D1::ColorF(1, 1, 1));
	}
	m_textRenderer->End();

	std::string hid = HoveredItem(m_uiInput.GetMousePos());
	if (!hid.empty()) DrawItemTooltip(hid, m_uiInput.GetMousePos());
}