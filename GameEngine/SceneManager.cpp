#include "SceneManager.h"
#include "EffectDataBase.h"
#include "EnemyDataBase.h"
#include "CardDataBase.h"
#include "CardVisual.h"
#include "PlayerDataManager.h"
#include "EncounterDataBase.h"
#include "TerrainDataBase.h"
#include "MaterialDataBase.h"
#include "RelicManager.h"
#include "Audio.h"

#include <cmath>

#include "External/imgui/imgui.h"

struct ItemInfo { std::wstring name, desc; std::string category; bool valid = false; };

static ItemInfo GetItemInfo(const std::string& id)
{
	if (auto b = MaterialDataBase::GetBase(id))
		return { ToWString(b->name), ToWString(b->desc), "Core", true };
	if (auto m = MaterialDataBase::GetMaterial(id))
		return { ToWString(m->name), ToWString(m->desc), "Material", true };
	if (auto r = RelicManager::Get(id))
		return { ToWString(r->name), ToWString(r->desc), "Relic", true };
	return {};
}

static XMFLOAT4 MapNodeColor(FieldNodeType t, bool visited, bool isPlayer)
{
	if (isPlayer) return XMFLOAT4(1.0f, 1.0f, 0.0f, 1.0f);
	if (visited)  return XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
	switch (t)
	{
	case FieldNodeType::Battle: return XMFLOAT4(0.7f, 0.2f, 0.2f, 1.0f);
	case FieldNodeType::Rest:   return XMFLOAT4(0.2f, 0.7f, 0.2f, 1.0f);
	case FieldNodeType::Event:  return XMFLOAT4(0.6f, 0.3f, 0.8f, 1.0f);
	case FieldNodeType::Boss:   return XMFLOAT4(0.7f, 0.2f, 0.7f, 1.0f);
	case FieldNodeType::Start:  return XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
	case FieldNodeType::Shop:   return XMFLOAT4(0.2f, 0.7f, 0.7f, 1.0f);
	case FieldNodeType::Elite:  return XMFLOAT4(0.9f, 0.5f, 0.1f, 1.0f);
	default:                    return XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
	}
}
static const wchar_t* MapNodeLabel(FieldNodeType t, bool isPlayer)
{
	if (isPlayer) return L"YOU";
	switch (t)
	{
	case FieldNodeType::Start:  return L"S";
	case FieldNodeType::Battle: return L"B";
	case FieldNodeType::Rest:   return L"R";
	case FieldNodeType::Event:  return L"Ev";
	case FieldNodeType::Boss:   return L"BOSS";
	case FieldNodeType::Shop:   return L"Sh";
	case FieldNodeType::Elite:  return L"El";
	default:                    return L"";
	}
}

static std::string EventPickerType(const EventChoice& c);

static std::string s_lastHoverKey;
static bool UiHover(float x, float y, float w, float h, POINT mp, const char* key)
{
	bool over = mp.x >= x && mp.x <= x + w && mp.y >= y && mp.y <= y + h;
	if (over && s_lastHoverKey != key) { Audio::PlaySE("Assets/Sound/se/hover.mp3"); s_lastHoverKey = key; }
	else if (!over && s_lastHoverKey == key) s_lastHoverKey.clear();
	return over;   // これで「浮かせるか」を判定
}

SceneManager::SceneManager() : m_currentScene(nullptr)
{

}

SceneManager::~SceneManager()
{
	delete m_currentScene;
	delete m_textRenderer;
	delete m_uiSprite;
	TextureManager::Shutdown();
	Audio::Shutdown();
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
	Audio::Init();
	EnemyDataBase::Init();
	EncounterDataBase::Init();
	CardDataBase::Init();
	TerrainDataBase::Load("Assets/Data/terrains.json");
	EffectDataBase::Load("Assets/Data/effects.json");
	MaterialDataBase::Load("Assets/Data/materials.json");
	RelicManager::Load("Assets/Data/relics.json");
	EventDataBase::Load("Assets/Data/events.json");
	PlayerDataManager::Init();

	Audio::SetMasterVolume(PlayerDataManager::GetData().masterVolume);
	Audio::SetBgmVolume(PlayerDataManager::GetData().bgmVolume);

	Audio::SetBgmTrackVolume("Assets/Sound/bgm/Field1.mp3", 0.5f);
	Audio::SetBgmTrackVolume("Assets/Sound/bgm/Field2.mp3", 0.5f);
	Audio::SetBgmTrackVolume("Assets/Sound/bgm/Field3.mp3", 0.3f);
	Audio::SetSeVolume("Assets/Sound/se/hover.mp3", 0.4f);   // ホバーは控えめ
	Audio::SetSeVolume("Assets/Sound/se/click.mp3", 0.7f);
	Audio::SetSeVolume("Assets/Sound/se/hit.mp3", 1.0f);
	// 未設定は 1.0

	TextureManager::Load("white", L"Assets/Test/White.png");
	TextureManager::Load("particle", L"Assets/Particles/circle.png");
	TextureManager::Load("particle_poison", L"Assets/Particles/circle_01.png");
	TextureManager::Load("fire", L"Assets/Particles/fire.png");
	TextureManager::Load("magic", L"Assets/Particles/magic.png");
	TextureManager::Load("smoke", L"Assets/Particles/smoke.png");
	TextureManager::Load("star", L"Assets/Particles/star.png");
	TextureManager::Load("title", L"Assets/Test/Title.png");
	TextureManager::Load("battle_bg", L"Assets/Field/GrassField.jpg");
	TextureManager::Load("map_bg", L"Assets/Field/Map.jpg");
	TextureManager::Load("cardSelect_bg", L"Assets/Field/CardSelect.jpeg");
	TextureManager::Load("player", L"Assets/Player/yuusya_game.png");
	TextureManager::Load("kakashi", L"Assets/Player/kakashi.png");
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

	DoChangeScene(SceneType::Title);
	m_fadeState = Fade::In;
	m_fadeAlpha = 1.0f;   // 真っ黒から明転
	return true;
}

void SceneManager::ChangeScene(SceneType type)
{
	if (m_fadeState == Fade::Out) return;   // 二重予約防止
	m_pendingScene = type;
	m_fadeState = Fade::Out;                 // まず暗転
}

void SceneManager::DoChangeScene(SceneType type)
{
	// 削除前に必要な情報を取得
	std::string battleEnemyId;
	int battleSeed = 0;
	int battleOverflow = 0;
	int battleTier = 1;
	EncCategory battleCategory = EncCategory::Normal;
	bool resultCleared = false;
	if (type == SceneType::Result)
		if (auto battle = dynamic_cast<BattleScene*>(m_currentScene))
			resultCleared = (battle->GetBattleResult() == BattleResult::Win);
	if (type == SceneType::Battle)
	{

		if (auto field = dynamic_cast<FieldScene*>(m_currentScene))
		{
			battleEnemyId = field->GetCurrentBattleEnemyId();
			battleSeed = field->GetCurrentBattleSeed();
			battleOverflow = field->GetCurrentBattleOverflow();
			battleCategory = field->GetCurrentBattleCategory();
			battleTier = field->GetCurrentBattleTier();
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
			scene->SetTier(battleTier);

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
			scene->onRest = [this]() { m_restOpen = true; 
			m_restActive = true; Audio::PlayBGM("Assets/Sound/bgm/Rest.mp3"); };
			scene->onEvent = [this](const std::string& id) { m_eventOpen = true; m_eventId = id; m_eventResult = -1; m_eventPickType.clear(); };
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
		case SceneType::Result:
		{
			auto scene = new ResultScene();
			scene->SetCleared(resultCleared);
			scene->onChangeScene = [this](SceneType t) { ChangeScene(t); };
			m_currentScene = scene;
			break;
		}
		
	}


	if (m_currentScene)
	{
		m_currentScene->Init(m_device, m_context, m_screenWidth, m_screenHeight, m_hWnd, m_swapChain);
	}

	switch (type)
	{
	case SceneType::Title:  Audio::PlayBGM("Assets/Sound/bgm/Title.mp3"); break;
	case SceneType::Battle:
		if (battleCategory == EncCategory::Boss) Audio::PlayBGM("Assets/Sound/bgm/boss.mp3");
		else PlayGeneralBGM();
		break;
	case SceneType::Field:
	case SceneType::Shop:
	case SceneType::CardSelect:
	case SceneType::Result:
		PlayGeneralBGM();
		break;
	default: break;
	}

	auto& pd = PlayerDataManager::GetData();
	if (type == SceneType::Field && !pd.tutorialField)
	{
		pd.tutorialField = true; PlayerDataManager::Save();
		ShowTutorial({
			{ L"マスをクリックして進もう",                         170, 130, 940, 540 },
			{ L"戦闘/休憩/ショップ/イベント\nエリート/ボスがある", 170, 130, 940, 540 },
			{ L"歩数(Steps)が尽きて進むと敵が強くなる",                                130,   5, 110,  28, -1, true },  // ← stepsTip=true
			{ L"右端のボスを倒すと次の層へ",                       1050, 130, 120, 540 },
					});
	}
	if (type == SceneType::Battle && !pd.tutorialBattle)
	{
		pd.tutorialBattle = true; PlayerDataManager::Save();
		ShowTutorialDelayed({
			{ L"手札のカードをクリックし対象を選んで使う", 340, 580, 600, 220 },
			{ L"コストはエナジーで払う",                    15, 185,  70,  70 },
			{ L"一番左は移動カード。マスを移動できる",     340, 630, 130, 170, 0 },   // ← hoverCard=0で浮く
			{ L"ターンエンドで相手の番",                  1100, 730, 160,  50 },
			{ L"HPが0になると敗北",                         20,  85, 220,  90 },
					}, 1.2f);
	}
}

void SceneManager::Draw()
{
	if (m_currentScene) m_currentScene->Draw();
	if (m_currentType != SceneType::Title) DrawOverlay();
	if (m_eventOpen && !m_mapOpen && !m_invOpen && !m_deckOpen)
	{
		if (m_eventPickType.empty()) DrawEvent();
		else if (m_eventPickType == "transformRelic") DrawEventRelicPicker();
		else DrawEventPicker();
	}
	if (m_craftOpen) DrawCraft();
	if (m_invOpen) DrawInventory();
	if (m_mapOpen) DrawMap();
	if (m_restOpen && !m_mapOpen && !m_invOpen && !m_deckOpen && !m_craftOpen) DrawRest();
	if (m_craftFxTimer > 0.0f) DrawCraftFx();  
	if (m_currentType != SceneType::Title) { DrawRelicBar(); DrawBarTips(); }

	if (m_tutorialOpen) DrawTutorial();
	// この後にフェードの黒幕

	if (m_fadeAlpha > 0.0f)
	{
		m_uiSprite->Begin();
		m_uiSprite->DrawSprite(TextureManager::Get("white"), 0, 0,
			(float)m_screenWidth, (float)m_screenHeight, 0.0f, XMFLOAT4(0, 0, 0, m_fadeAlpha));
		m_uiSprite->End();
	}

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
	m_uiSprite->DrawSprite(white, m_screenWidth - 470.0f, 5.0f, 90.0f, 30.0f, 0.0f, XMFLOAT4(0.3f, 0.4f, 0.35f, 1.0f));

	m_uiSprite->DrawSprite(white, btnX, 5.0f, 100.0f, 30.0f, 0.0f,
		XMFLOAT4(0.2f, 0.3f, 0.5f, 1.0f));                    // デッキボタン

	m_uiSprite->DrawSprite(white, m_screenWidth - 680.0f, 5.0f, 90.0f, 30.0f, 0.0f,
		XMFLOAT4(0.3f, 0.35f, 0.5f, 1.0f));         // Mapボタン

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
	m_textRenderer->DrawText(L"Map", m_screenWidth - 670.0f, 10.0f, 16.0f, D2D1::ColorF(1, 1, 1));
	wchar_t gbuf[32];
	// ゴールド
	swprintf_s(gbuf, L"G:%d", PlayerDataManager::GetData().gold);
	m_textRenderer->DrawText(gbuf, m_screenWidth - 560.0f, 10.0f, 16.0f, D2D1::ColorF(1, 0.9f, 0.3f));


	auto& pd = PlayerDataManager::GetData();
	int hp = pd.hp, mhp = pd.maxHp;
	if (auto battle = dynamic_cast<BattleScene*>(m_currentScene))   // バトル中は生HP
	{
		hp = battle->GetPlayerHp(); mhp = battle->GetPlayerMaxHp();
	}

	wchar_t hbuf[48];
	swprintf_s(hbuf, L"HP %d/%d", hp, mhp);
	m_textRenderer->DrawText(hbuf, 12.0f, 10.0f, 16.0f, D2D1::ColorF(1.0f, 0.5f, 0.5f));

	wchar_t sbuf[48];
	swprintf_s(sbuf, L"Steps %d", pd.fieldSteps);
	m_textRenderer->DrawText(sbuf, 140.0f, 10.0f, 16.0f, D2D1::ColorF(0.6f, 0.9f, 0.6f));

	wchar_t lbuf[32]; swprintf_s(lbuf, L"Layer %d/3", pd.layer);
	m_textRenderer->DrawText(lbuf, 280.0f, 10.0f, 16.0f, D2D1::ColorF(0.75f, 0.8f, 1.0f));

	m_textRenderer->DrawText(L"Items", m_screenWidth - 460.0f, 10.0f, 16.0f, D2D1::ColorF(1, 1, 1));
	if (m_deckOpen)
		m_textRenderer->DrawText(L"Upgrade", 140.0f, 50.0f, 16.0f, D2D1::ColorF(1, 1, 1));
	m_textRenderer->End();
}

void SceneManager::DrawDeckCards(bool textPass)
{
	auto& deck = PlayerDataManager::GetData().deck;
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	auto vis = VisibleDeckIndices();

	// スクロールのクランプ
	int perRow = 6;
	float ch = CardVisual::CARD_H * DECK_SCALE;
	int rows = ((int)vis.size() + perRow - 1) / perRow;
	float maxScroll = 70.0f + rows * (ch + 20.0f) - (m_screenHeight - 20.0f);
	if (maxScroll < 0) maxScroll = 0;
	if (m_deckScroll < 0) m_deckScroll = 0;
	if (m_deckScroll > maxScroll) m_deckScroll = maxScroll;

	for (int slot = 0; slot < (int)vis.size(); slot++)
	{
		const CardData* d = CardDataBase::Get(deck[vis[slot]]);
		if (!d) continue;
		float bx, by; GetDeckCardBase(slot, bx, by);

		if (!textPass)
		{
			XMFLOAT4 col = m_deckRemoveMode
				? XMFLOAT4(0.6f, 0.15f, 0.15f, 0.9f)
				: CardVisual::GetCardColor(d->type);
			CardVisual::DrawBase(m_uiSprite, white, bx, by, DECK_SCALE, 0.0f, col, d, m_uiTime);
		}
		else
			CardVisual::DrawTexts(m_textRenderer, d, nullptr, bx, by, DECK_SCALE, 0.0f, 1.0f);
	}
}

static bool CanUpgrade(const std::string& id)
{
	if (id.rfind("CRAFT:", 0) == 0) return false;        // 合成カードは強化不可
	if (!id.empty() && id.back() == '+') return false;   // 強化済み
	return CardDataBase::Get(id + "+") != nullptr;        // +版があるか
}

std::vector<int> SceneManager::VisibleDeckIndices() const
{
	std::vector<int> v;
	auto& deck = PlayerDataManager::GetData().deck;
	for (int i = 0; i < (int)deck.size(); i++)
	{
		if (m_deckUpgradeMode && !CanUpgrade(deck[i])) continue;  // 強化画面では強化不可を隠す
		v.push_back(i);
	}
	return v;
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

	auto& pd = PlayerDataManager::GetData();
	float mv = pd.masterVolume, bv = pd.bgmVolume;
	if (ImGui::SliderFloat("Master", &mv, 0.0f, 1.0f)) { pd.masterVolume = mv; Audio::SetMasterVolume(mv); PlayerDataManager::Save(); }
	if (ImGui::SliderFloat("BGM", &bv, 0.0f, 1.0f)) { pd.bgmVolume = bv;    Audio::SetBgmVolume(bv);    PlayerDataManager::Save(); }

	if (ImGui::CollapsingHeader("Relics"))
		for (auto& id : RelicManager::AllIds())
			if (ImGui::Button(id.c_str()))
				PlayerDataManager::AddRelic(id);

	ImGui::End();
#endif
	if (m_currentScene)
		m_currentScene->DrawImGui();
}

void SceneManager::HandleInput()
{
	m_uiInput.Update();
	if (m_fadeState != Fade::None) return;   // フェード中は入力無効

	if (m_tutorialOpen)
	{
		if (m_uiInput.GetMouseButtonTrigger(0))
		{
			Audio::PlaySE("Assets/Sound/se/click.mp3");
			m_tutorialPage++;
			if (m_tutorialPage >= (int)m_tutorialPages.size()) m_tutorialOpen = false;
		}
		return;
	}

	if (m_craftFxTimer > 0.0f) return;           // 演出中は入力停止

	if (m_currentType != SceneType::Title)
	{
		POINT m = m_uiInput.GetMousePos();
		bool click = m_uiInput.GetMouseButtonTrigger(0);
		float btnX = m_screenWidth - 220.0f, btnY = 5.0f, btnW = 100.0f, btnH = 30.0f;
		bool onDeckBtn = click && m.x >= btnX && m.x <= btnX + btnW
			&& m.y >= btnY && m.y <= btnY + btnH;

		// Map（全シーンで開ける・クリックで閉じる）
		if (m_mapOpen) { if (click) m_mapOpen = false; return; }
		{
			float mapX = m_screenWidth - 680.0f;
			if (click && !m_deckOpen && !m_craftOpen && !m_invOpen
				&& m.x >= mapX && m.x <= mapX + 90.0f && m.y >= 5.0f && m.y <= 35.0f)
			{
				Audio::PlaySE("Assets/Sound/se/click.mp3");
				m_mapOpen = true; return;
			}
		}

		// Items を開く
		if (click && !m_invOpen && !m_craftOpen
			&& m.x >= m_screenWidth - 470.0f && m.x <= m_screenWidth - 380.0f
			&& m.y >= 5.0f && m.y <= 35.0f)
		{
			Audio::PlaySE("Assets/Sound/se/click.mp3");
			m_invOpen = true; return;
		}

		// Items（いつでも）
		if (m_invOpen) { if (click) m_invOpen = false; return; }
		{
			float itemsX = m_screenWidth - 470.0f;
			if (click && !m_deckOpen && !m_craftOpen && !m_mapOpen
				&& m.x >= itemsX && m.x <= itemsX + 90.0f && m.y >= 5.0f && m.y <= 35.0f)
			{
				Audio::PlaySE("Assets/Sound/se/click.mp3");
				m_invOpen = true; return;
			}
		}

		if (onDeckBtn && !m_deckOpen)
		{ 
			m_deckOpen = true; 
			m_deckScroll = 0.0f; return;
			Audio::PlaySE("Assets/Sound/se/click.mp3");
		}
		if (m_deckOpen)
		{
			m_deckScroll -= m_uiInput.GetMouseWheelDelta() * 0.5f;
			if (click)
			{
				if (m_deckUpgradeMode)
				{
					int idx = GetDeckCardAt(m);
					if (idx >= 0)
					{
						std::string before = PlayerDataManager::GetData().deck[idx];
						PlayerDataManager::UpgradeCard(idx);
						const std::string& after = PlayerDataManager::GetData().deck[idx];
						if (after != before)                 // 実際に強化された時だけ演出
						{
							m_craftFxCard = after;
							m_craftFxTimer = CRAFT_FX_DURATION;
							// 休憩中ならUpdate()が演出終了時にFinishRestを呼ぶ（合成と同じ経路）
						}
						return;
					}
					// 空白クリック
					if (m_restActive) { m_deckOpen = false; m_deckUpgradeMode = false; m_restOpen = true; return; }
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
		if (m_craftOpen) { if (click) HandleCraftClick(m); return; }
	}

	if (m_restOpen)                              // 休憩の3択（バー操作より後で判定）
	{
		if (m_uiInput.GetMouseButtonTrigger(0))
			HandleRestClick(m_uiInput.GetMousePos());
		return;   // 休憩中はフィールドへ渡さない
	}

	if (m_eventOpen)
	{
		// カードピッカー中
		if (!m_eventPickType.empty())
		{
			if (m_eventPickAnimIdx < 0 && m_uiInput.GetMouseButtonTrigger(0))
			{
				if (m_eventPickType == "transformRelic")
				{
					int idx = EventRelicAt(m_uiInput.GetMousePos());
					if (idx >= 0)
					{
						std::string to = RelicManager::RandomDrop();   // 別レリック（未所持）
						if (!to.empty()) { m_eventPickAnimTo = to; m_eventPickAnimIdx = idx; m_eventPickAnimTimer = EVENT_PICK_ANIM_DUR; }
						else { m_eventPickType.clear(); m_eventResult = m_eventPickChoice; }
					}
				}
				else
				{
					int idx = EventCardAt(m_uiInput.GetMousePos());
					if (idx >= 0)
					{
						Audio::PlaySE("Assets/Sound/se/click.mp3");
						auto& deck = PlayerDataManager::GetData().deck;
						std::string cur = deck[idx];
						if (m_eventPickType == "removeCard") m_eventPickAnimTo = "";
						else if (m_eventPickType == "transformCard") m_eventPickAnimTo = CardDataBase::RandomCard();
						else if (m_eventPickType == "upgradeCard") m_eventPickAnimTo = CanUpgrade(cur) ? (cur + "+") : cur;
						m_eventPickAnimIdx = idx;
						m_eventPickAnimTimer = EVENT_PICK_ANIM_DUR;
					}
				}
			}
			return;
		}
		// 選択肢
		if (m_uiInput.GetMouseButtonTrigger(0))
		{
			if (m_eventResult >= 0) 
			{
				m_eventOpen = false; 
				Audio::PlaySE("Assets/Sound/se/click.mp3");
			}
			else
			{
				const EventDef* e = EventDataBase::Get(m_eventId);
				POINT m = m_uiInput.GetMousePos();
				if (e) for (int i = 0; i < (int)e->choices.size(); i++)
				{
					float x, y, w, h; GetEventChoiceRect(i, x, y, w, h);
					if (m.x >= x && m.x <= x + w && m.y >= y && m.y <= y + h && ChoiceEnabled(e->choices[i]))
					{
						Audio::PlaySE("Assets/Sound/se/click.mp3");
						ApplyOutcomes(e->choices[i]);
						std::string pick = EventPickerType(e->choices[i]);
						if (!pick.empty()) { m_eventPickType = pick; m_eventPickChoice = i; }
						else m_eventResult = i;
						break;
					}
				}
			}
		}
		return;
	}

	// m_uiInputが消費したホイールをシーンへ戻す（ズーム等が効くように）
	Input::SetWheelDelta(m_uiInput.GetMouseWheelDelta());
	if (m_currentScene) m_currentScene->HandleInput();
}

void SceneManager::ShowTutorial(const std::vector<TutorialPage>& pages)
{
	if (pages.empty()) return;
	m_tutorialPages = pages; 
	m_tutorialPage = 0; 
	m_tutorialOpen = true;
}

void SceneManager::DrawTutorial()
{
	if (m_tutorialPage >= (int)m_tutorialPages.size()) { m_tutorialOpen = false; return; }
	const auto& pg = m_tutorialPages[m_tutorialPage];
	float hx = pg.hx, hy = pg.hy, hw = pg.hw, hh = pg.hh;
	if (pg.hoverCard >= 0)
		if (auto battle = dynamic_cast<BattleScene*>(m_currentScene))
		{
			float x, y, w, h;
			if (battle->GetCardRect(pg.hoverCard, x, y, w, h)) { hx = x; hy = y; hw = w; hh = h; }
		}
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	float sw = (float)m_screenWidth, sh = (float)m_screenHeight;
	XMFLOAT4 dim(0, 0, 0, 0.7f);

	m_uiSprite->Begin();
	if (hw > 0.0f)
	{
		float x = hx, y = hy, w = hw, h = hh;
		m_uiSprite->DrawSprite(white, 0, 0, sw, y, 0.0f, dim);
		m_uiSprite->DrawSprite(white, 0, y + h, sw, sh - (y + h), 0.0f, dim);
		m_uiSprite->DrawSprite(white, 0, y, x, h, 0.0f, dim);
		m_uiSprite->DrawSprite(white, x + w, y, sw - (x + w), h, 0.0f, dim);
		XMFLOAT4 fr(1.0f, 0.85f, 0.2f, 1.0f);
		m_uiSprite->DrawSprite(white, x - 3, y - 3, w + 6, 3, 0.0f, fr);
		m_uiSprite->DrawSprite(white, x - 3, y + h, w + 6, 3, 0.0f, fr);
		m_uiSprite->DrawSprite(white, x - 3, y, 3, h + 3, 0.0f, fr);
		m_uiSprite->DrawSprite(white, x + w, y, 3, h + 3, 0.0f, fr);
	}
	else m_uiSprite->DrawSprite(white, 0, 0, sw, sh, 0.0f, dim);

	// テキストボックス（対象が上なら下側、下なら上側に置く）
	float bw = 560.0f, bh = 150.0f, bx = sw / 2.0f - bw / 2.0f;
	float by = (pg.hy + pg.hh * 0.5f < sh * 0.5f) ? sh - bh - 60.0f : 80.0f;
	m_uiSprite->DrawSprite(white, bx - 2, by - 2, bw + 4, bh + 4, 0.0f, XMFLOAT4(0.4f, 0.4f, 0.6f, 1.0f));
	m_uiSprite->DrawSprite(white, bx, by, bw, bh, 0.0f, XMFLOAT4(0.08f, 0.08f, 0.14f, 0.98f));
	m_uiSprite->End();

	m_textRenderer->Begin();
	m_textRenderer->DrawText(pg.text.c_str(), bx + 25.0f, by + 25.0f, 22.0f, D2D1::ColorF(1, 1, 1));
	wchar_t pgn[32]; swprintf_s(pgn, L"%d / %d", m_tutorialPage + 1, (int)m_tutorialPages.size());
	m_textRenderer->DrawText(pgn, bx + bw - 80.0f, by + 10.0f, 16.0f, D2D1::ColorF(0.7f, 0.7f, 0.7f));
	bool last = (m_tutorialPage == (int)m_tutorialPages.size() - 1);
	m_textRenderer->DrawText(last ? L"クリックで閉じる" : L"クリックで次へ",
		bx + bw / 2.0f - 70.0f, by + bh - 34.0f, 18.0f, D2D1::ColorF(0.85f, 0.85f, 0.6f));
	m_textRenderer->End();

	if (pg.stepsTip)
	{
		float tx = pg.hx - 20.0f, ty = pg.hy + pg.hh + 10.0f, tw = 320.0f, th = 60.0f;
		m_uiSprite->Begin();
		m_uiSprite->DrawSprite(white, tx, ty, tw, th, 0.0f, XMFLOAT4(0.05f, 0.05f, 0.1f, 0.98f));
		m_uiSprite->End();
		m_textRenderer->Begin();
		m_textRenderer->DrawText(L"歩数", tx + 10.0f, ty + 6.0f, 16.0f, D2D1::ColorF(1, 0.9f, 0.6f));
		m_textRenderer->DrawText(L"0を超えて進むと敵が強くなる", tx + 10.0f, ty + 30.0f, 13.0f, D2D1::ColorF(0.9f, 0.9f, 0.9f));
		m_textRenderer->End();
	}
}

void SceneManager::ShowTutorialDelayed(const std::vector<TutorialPage>& pages, float delay)
{
	m_pendingTutorial = pages;
	m_tutorialDelay = delay;
}

void SceneManager::DrawBarTips()
{
	POINT mp = m_uiInput.GetMousePos();
	if (mp.y < 5 || mp.y > 35) return;

	const wchar_t* title = nullptr; const wchar_t* desc = nullptr; float x = 0.0f;
	if (mp.x >= 12 && mp.x <= 130)
	{
		title = L"体力"; desc = L"0でゲームオーバー"; x = 12.0f;
	}
	else if (mp.x >= 140 && mp.x <= 260)
	{
		title = L"歩数"; desc = L"0を超えて進むと敵が強くなる"; x = 140.0f;
	}
	if (!title) return;

	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	float ty = BAR_H + 44.0f, w = 360.0f, h = 56.0f;   // レリック帯の下
	m_uiSprite->Begin();
	m_uiSprite->DrawSprite(white, x, ty, w, h, 0.0f, XMFLOAT4(0.05f, 0.05f, 0.1f, 0.97f));
	m_uiSprite->End();
	m_textRenderer->Begin();
	m_textRenderer->DrawText(title, x + 10.0f, ty + 6.0f, 16.0f, D2D1::ColorF(1.0f, 0.9f, 0.6f));
	m_textRenderer->DrawText(desc, x + 10.0f, ty + 30.0f, 14.0f, D2D1::ColorF(0.95f, 0.8f, 0.6f));
	m_textRenderer->End();
}

void SceneManager::Update(float deltaTime)
{
	if (m_fadeState == Fade::Out)
	{
		m_fadeAlpha += FADE_SPEED * deltaTime;
		if (m_fadeAlpha >= 1.0f) { m_fadeAlpha = 1.0f; DoChangeScene(m_pendingScene); m_fadeState = Fade::In; }
		return;   // 暗転中はシーンを止める
	}

	if (m_tutorialDelay > 0.0f)
	{
		m_tutorialDelay -= deltaTime;
		if (m_tutorialDelay <= 0.0f && !m_pendingTutorial.empty())
		{
			ShowTutorial(m_pendingTutorial);
			m_pendingTutorial.clear();
		}
	}

	if (m_fadeState == Fade::In)
	{
		m_fadeAlpha -= FADE_SPEED * deltaTime;
		if (m_fadeAlpha <= 0.0f) { m_fadeAlpha = 0.0f; m_fadeState = Fade::None; }
	}

	m_uiTime += deltaTime;

	if (m_craftFxTimer > 0.0f)
	{
		m_craftFxTimer -= deltaTime;
		if (m_craftFxTimer <= 0.0f)
		{
			m_craftFxTimer = 0.0f;
			if (m_restActive) FinishRest();      // 休憩でのクラフトは演出後に終了
		}
	}

	if (m_eventPickAnimIdx >= 0)
	{
		m_eventPickAnimTimer -= deltaTime;
		if (m_eventPickAnimTimer <= 0.0f)
		{
			if (m_eventPickType == "transformRelic")
			{
				auto& relics = PlayerDataManager::GetData().relics;
				if (m_eventPickAnimIdx < (int)relics.size())
				{
					std::string oldId = relics[m_eventPickAnimIdx];
					PlayerDataManager::RemoveRelic(oldId);
					PlayerDataManager::AddRelic(m_eventPickAnimTo);
				}
			}
			else
			{
				auto& deck = PlayerDataManager::GetData().deck;
				if (m_eventPickType == "removeCard") PlayerDataManager::RemoveCard(m_eventPickAnimIdx);
				else if (m_eventPickAnimIdx < (int)deck.size()) { deck[m_eventPickAnimIdx] = m_eventPickAnimTo; PlayerDataManager::Save(); }
			}
			m_eventPickAnimIdx = -1;
			m_eventPickType.clear();
			m_eventResult = m_eventPickChoice;
		}
	}

	if (m_deckOpen || m_craftOpen || m_invOpen || m_restOpen || m_mapOpen || m_eventOpen || m_craftFxTimer > 0.0f) return;

	// チュートリアルのカード強制ホバー
	if (m_tutorialOpen)
		if (auto battle = dynamic_cast<BattleScene*>(m_currentScene))
			battle->SetHoveredCard(m_tutorialPages[m_tutorialPage].hoverCard);

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
	auto vis = VisibleDeckIndices();
	for (int slot = 0; slot < (int)vis.size(); slot++)
	{
		float bx, by; GetDeckCardBase(slot, bx, by);
		float x, y, w, h; CardVisual::GetRect(bx, by, DECK_SCALE, x, y, w, h);
		if (p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h)
			return vis[slot];
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

int SceneManager::CraftModSlots() const
{
	if (m_craftBase.empty()) return 2;
	const BaseDef* b = MaterialDataBase::GetBase(m_craftBase);
	return (b ? b->modSlots : 2) + RelicManager::SumValue("modSlots");
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
	int nSlots = 1 + CraftModSlots();
	for (int i = 0; i < nSlots; i++)
	{
		float x, y, w, h; GetCraftSlotRect(i, x, y, w, h);
		bool filled = (i == 0) ? !m_craftBase.empty() : (int)m_craftMods.size() >= i;
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
		bool ready = !m_craftBase.empty() && (int)m_craftMods.size() >= CraftModSlots();
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

	for (int i = 0; i < nSlots; i++)
	{
		float x, y, w, h; GetCraftSlotRect(i, x, y, w, h);
		std::wstring t = (i == 0) ? L"Base" : (L"Mod" + std::to_wstring(i));
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
	int nSlots = 1 + CraftModSlots();
	for (int i = 0; i < nSlots; i++)
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
		if (m.x >= x && m.x <= x + w && m.y >= y && m.y <= y + h)
		{
			const std::string& id = bases[i].first;
			int used = usedCount(id) - (m_craftBase == id ? 1 : 0);
			if (used < PlayerDataManager::MaterialCount(id)) {
				m_craftBase = id;
				while ((int)m_craftMods.size() > CraftModSlots()) m_craftMods.pop_back();  // 枠が減ったら余りを外す
			}
			return;
		}
	}
	// 素材クリック → 修飾へ
	auto mods = CraftMods();
	for (int i = 0; i < (int)mods.size(); i++) {
		float x, y, w, h; GetCraftModRect(i, x, y, w, h);
		if (m.x >= x && m.x <= x + w && m.y >= y && m.y <= y + h) {
			const std::string& id = mods[i].first;
			if ((int)m_craftMods.size() < CraftModSlots() && usedCount(id) < PlayerDataManager::MaterialCount(id))
				m_craftMods.push_back(id);
			return;
		}
	}
	// どれでもない所（枠外）→ 閉じる
	m_craftOpen = false;
	if (m_restActive) m_restOpen = true;
}

void SceneManager::DoCraft()
{
	if (m_craftBase.empty()) return;
	if ((int)m_craftMods.size() < CraftModSlots()) return;   // 枠が全部埋まっていないと作れない

	// 必要数を集計して所持チェック
	std::map<std::string, int> need;
	need[m_craftBase]++;
	for (auto& mod : m_craftMods) need[mod]++;
	for (auto& kv : need)
		if (PlayerDataManager::MaterialCount(kv.first) < kv.second) return;   // 足りない

	std::string rid = CraftRecipeId();           // 消費前に確定

	// 消費
	for (auto& kv : need)
		PlayerDataManager::AddMaterial(kv.first, -kv.second);

	// デッキに合成カードを追加
	PlayerDataManager::GetData().deck.push_back(rid);
	PlayerDataManager::Save();

	m_craftBase.clear(); m_craftMods.clear();

	m_craftFxCard = rid;                          // 演出開始
	m_craftFxTimer = CRAFT_FX_DURATION;
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

void SceneManager::GetRestBtnRect(int i, float& x, float& y, float& w, float& h) const
{
	w = 260.0f; h = 60.0f;
	x = m_screenWidth / 2.0f - w / 2.0f;
	y = m_screenHeight / 2.0f - 110.0f + i * 80.0f;
}

void SceneManager::DrawRest()
{
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	POINT mp = m_uiInput.GetMousePos();

	m_uiSprite->Begin();
	m_uiSprite->DrawSprite(white, 0, BAR_H, (float)m_screenWidth, (float)m_screenHeight - BAR_H, 0.0f,
		XMFLOAT4(0.0f, 0.0f, 0.05f, 0.9f));
	XMFLOAT4 base[3] = {
		XMFLOAT4(0.25f, 0.45f, 0.30f, 1.0f),   // Heal
		XMFLOAT4(0.50f, 0.45f, 0.20f, 1.0f),   // Upgrade
		XMFLOAT4(0.40f, 0.30f, 0.50f, 1.0f) }; // Craft
	for (int i = 0; i < 3; i++)
	{
		float x, y, w, h; GetRestBtnRect(i, x, y, w, h);
		bool hov = mp.x >= x && mp.x <= x + w && mp.y >= y && mp.y <= y + h;
		XMFLOAT4 c = base[i];
		if (hov) { c.x += 0.1f; c.y += 0.1f; c.z += 0.1f; }
		m_uiSprite->DrawSprite(white, x, y, w, h, 0.0f, c);
	}
	m_uiSprite->End();

	m_textRenderer->Begin();
	m_textRenderer->DrawText(L"REST", m_screenWidth / 2.0f - 45.0f,
		m_screenHeight / 2.0f - 190.0f, 32.0f, D2D1::ColorF(1, 1, 1));
	const wchar_t* labels[3] = { L"Heal +20", L"Upgrade a card", L"Craft" };
	for (int i = 0; i < 3; i++)
	{
		float x, y, w, h; GetRestBtnRect(i, x, y, w, h);
		m_textRenderer->DrawText(labels[i], x + 20.0f, y + 18.0f, 22.0f, D2D1::ColorF(1, 1, 1));
	}
	m_textRenderer->End();
}

void SceneManager::HandleRestClick(POINT m)
{
	for (int i = 0; i < 3; i++)
	{
		float x, y, w, h; GetRestBtnRect(i, x, y, w, h);
		if (m.x >= x && m.x <= x + w && m.y >= y && m.y <= y + h)
		{
			Audio::PlaySE("Assets/Sound/se/click.mp3");
			if (i == 0)          // Heal
			{
				auto& pd = PlayerDataManager::GetData();
				pd.hp += REST_HEAL + RelicManager::SumValue("restHeal");
				if (pd.hp > pd.maxHp) pd.hp = pd.maxHp;
				FinishRest();
			}
			else if (i == 1)     // Upgrade（デッキ強化モードを流用）
			{
				m_restOpen = false;
				m_deckOpen = true; m_deckUpgradeMode = true; m_deckScroll = 0.0f;
			}
			else                 // Craft
			{
				m_restOpen = false;
				m_craftOpen = true; m_craftBase.clear(); m_craftMods.clear();
			}
			return;
		}
	}
}

void SceneManager::FinishRest()
{
	m_restActive = false;
	m_restOpen = false;
	m_deckOpen = false; m_deckUpgradeMode = false; m_deckRemoveMode = false;
	m_craftOpen = false;
	PlayerDataManager::Save();
}

void SceneManager::DrawCraftFx()
{
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	const CardData* card = CardDataBase::Get(m_craftFxCard);
	float t = 1.0f - m_craftFxTimer / CRAFT_FX_DURATION;   // 0→1
	if (t < 0) t = 0; if (t > 1) t = 1;

	float cx = m_screenWidth / 2.0f;
	float cy = 280.0f;                                     // プレビューと同じ中心

	m_uiSprite->Begin();

	// フラッシュ（前半で白く光ってフェード）
	float flash = 1.0f - t * 2.0f;
	if (flash > 0.0f)
		m_uiSprite->DrawSprite(white, 0, 0, (float)m_screenWidth, (float)m_screenHeight, 0.0f,
			XMFLOAT4(1, 1, 1, flash * 0.7f));

	// パーティクル（中心から放射）
	XMFLOAT4 pcol = card ? CardVisual::GetCardColor(card->type) : XMFLOAT4(1, 1, 1, 1);
	int N = 14;
	float r = t * 300.0f;
	float psize = 14.0f * (1.0f - t);
	float palpha = 1.0f - t;
	for (int k = 0; k < N; k++)
	{
		float ang = (float)k / N * 6.2831853f;
		float px = cx + cosf(ang) * r;
		float py = cy + sinf(ang) * r;
		m_uiSprite->DrawSprite(white, px - psize / 2, py - psize / 2, psize, psize, 0.0f,
			XMFLOAT4(pcol.x, pcol.y, pcol.z, palpha));
	}

	// カード出現（小→大→少し戻す＝オーバーシュート）
	float s = (t < 0.6f) ? 0.4f + (t / 0.6f) * 1.0f
		: 1.4f - ((t - 0.6f) / 0.4f) * 0.2f;
	if (card)
	{
		float bx = cx - CardVisual::CARD_W / 2.0f;
		float by = cy - CardVisual::CARD_H / 2.0f;
		CardVisual::DrawBase(m_uiSprite, white, bx, by, s, 0.0f,
			CardVisual::GetCardColor(card->type), card, m_uiTime);
	}
	m_uiSprite->End();

	if (card)
	{
		float bx = cx - CardVisual::CARD_W / 2.0f;
		float by = cy - CardVisual::CARD_H / 2.0f;
		m_textRenderer->Begin();
		CardVisual::DrawTexts(m_textRenderer, card, nullptr, bx, by, s, 0.0f, 1.0f);
		m_textRenderer->End();
	}
}

void SceneManager::DrawMap()
{
	auto& pd = PlayerDataManager::GetData();
	ID3D11ShaderResourceView* white = TextureManager::Get("white");

	const int ROWS = 7, COLS = 12;                  // FieldSceneのグリッドと一致
	bool haveMap = (int)pd.fieldNodeTypes.size() >= ROWS * COLS;

	const float CELL = 48.0f, GAP = 12.0f;
	float totalW = COLS * (CELL + GAP) - GAP;
	float totalH = ROWS * (CELL + GAP) - GAP;
	float offX = (m_screenWidth - totalW) / 2.0f;
	float offY = (m_screenHeight - totalH) / 2.0f + 20.0f;

	auto idxOf = [&](int c, int r) { return c * ROWS + r; };
	auto typeOf = [&](int c, int r) { return (FieldNodeType)pd.fieldNodeTypes[idxOf(c, r)]; };
	auto posOf = [&](int c, int r, float& x, float& y) { x = offX + c * (CELL + GAP); y = offY + r * (CELL + GAP); };

	m_uiSprite->Begin();
	m_uiSprite->DrawSprite(white, 0, 0, (float)m_screenWidth, (float)m_screenHeight, 0.0f,
		XMFLOAT4(0.0f, 0.0f, 0.05f, 0.92f));

	if (haveMap)
	{
		// 接続線
		for (int r = 0; r < ROWS; r++)
			for (int c = 0; c < COLS; c++)
			{
				if (typeOf(c, r) == FieldNodeType::Empty) continue;
				float x, y; posOf(c, r, x, y);
				float cx1 = x + CELL / 2, cy1 = y + CELL / 2;
				if (c + 1 < COLS && typeOf(c + 1, r) != FieldNodeType::Empty)
					m_uiSprite->DrawSprite(white, cx1, cy1 - 2, CELL + GAP, 4, 0.0f, XMFLOAT4(0.5f, 0.5f, 0.5f, 1));
				if (r + 1 < ROWS && typeOf(c, r + 1) != FieldNodeType::Empty)
					m_uiSprite->DrawSprite(white, cx1 - 2, cy1, 4, CELL + GAP, 0.0f, XMFLOAT4(0.5f, 0.5f, 0.5f, 1));
			}
		// ノード
		for (int r = 0; r < ROWS; r++)
			for (int c = 0; c < COLS; c++)
			{
				FieldNodeType t = typeOf(c, r);
				if (t == FieldNodeType::Empty) continue;
				float x, y; posOf(c, r, x, y);
				bool isPlayer = (c == pd.fieldPlayerCol && r == pd.fieldPlayerRow);
				m_uiSprite->DrawSprite(white, x, y, CELL, CELL, 0.0f,
					MapNodeColor(t, pd.fieldNodeVisited[idxOf(c, r)], isPlayer));
			}
	}
	m_uiSprite->End();

	m_textRenderer->Begin();
	m_textRenderer->DrawText(L"MAP", m_screenWidth / 2.0f - 30.0f, offY - 44.0f, 26.0f, D2D1::ColorF(1, 1, 1));
	if (!haveMap)
		m_textRenderer->DrawText(L"No map yet", m_screenWidth / 2.0f - 60.0f, m_screenHeight / 2.0f, 22.0f, D2D1::ColorF(0.7f, 0.7f, 0.7f));
	else
	{
		for (int r = 0; r < ROWS; r++)
			for (int c = 0; c < COLS; c++)
			{
				FieldNodeType t = typeOf(c, r);
				if (t == FieldNodeType::Empty) continue;
				float x, y; posOf(c, r, x, y);
				bool isPlayer = (c == pd.fieldPlayerCol && r == pd.fieldPlayerRow);
				m_textRenderer->DrawText(MapNodeLabel(t, isPlayer), x + 4.0f, y + CELL / 2 - 8.0f, 12.0f, D2D1::ColorF(1, 1, 1));
			}
	}
	m_textRenderer->DrawText(L"click to close", m_screenWidth / 2.0f - 55.0f, offY + totalH + 16.0f, 16.0f, D2D1::ColorF(0.7f, 0.7f, 0.7f));
	m_textRenderer->End();
}

void SceneManager::GetRelicRect(int i, float& x, float& y, float& w, float& h) const
{
	w = 48.0f; h = 34.0f;
	x = 10.0f + i * (w + 4.0f);
	y = BAR_H + 4.0f;                  // 帯（バー）の下
}

std::string SceneManager::HoveredRelic(POINT mp) const
{
	auto& relics = PlayerDataManager::GetData().relics;
	for (int i = 0; i < (int)relics.size(); i++)
	{
		float x, y, w, h; GetRelicRect(i, x, y, w, h);
		if (mp.x >= x && mp.x <= x + w && mp.y >= y && mp.y <= y + h) return relics[i];
	}
	return "";
}

void SceneManager::DrawRelicBar()
{
	auto& relics = PlayerDataManager::GetData().relics;
	if (relics.empty()) return;
	ID3D11ShaderResourceView* white = TextureManager::Get("white");

	auto rarColor = [](const std::string& r) -> XMFLOAT4 {
		if (r == "uncommon") return XMFLOAT4(0.2f, 0.6f, 0.9f, 1);
		if (r == "rare")     return XMFLOAT4(0.95f, 0.8f, 0.2f, 1);
		if (r == "boss")     return XMFLOAT4(0.9f, 0.3f, 0.3f, 1);
		if (r == "event")    return XMFLOAT4(0.3f, 0.8f, 0.4f, 1);
		if (r == "shop")     return XMFLOAT4(0.7f, 0.4f, 0.9f, 1);
		return XMFLOAT4(0.55f, 0.55f, 0.55f, 1);  // common
		};

	m_uiSprite->Begin();
	for (int i = 0; i < (int)relics.size(); i++)
	{
		float x, y, w, h; GetRelicRect(i, x, y, w, h);
		auto rd = RelicManager::Get(relics[i]);
		XMFLOAT4 frame = rd ? rarColor(rd->rarity) : XMFLOAT4(0.55f, 0.55f, 0.55f, 1);
		m_uiSprite->DrawSprite(white, x - 2, y - 2, w + 4, h + 4, 0.0f, frame);
		m_uiSprite->DrawSprite(white, x, y, w, h, 0.0f, XMFLOAT4(0.22f, 0.18f, 0.28f, 1));
	}
	m_uiSprite->End();

	m_textRenderer->Begin();
	for (int i = 0; i < (int)relics.size(); i++)
	{
		float x, y, w, h; GetRelicRect(i, x, y, w, h);
		auto d = RelicManager::Get(relics[i]);
		std::wstring lb = d ? ToWString(d->name).substr(0, 2) : L"?";
		m_textRenderer->DrawText(lb.c_str(), x + 6.0f, y + 8.0f, 18.0f, D2D1::ColorF(1, 1, 1));
	}
	m_textRenderer->End();

	std::string rid = HoveredRelic(m_uiInput.GetMousePos());
	if (!rid.empty()) DrawItemTooltip(rid, m_uiInput.GetMousePos());
}

void SceneManager::GetEventChoiceRect(int i, float& x, float& y, float& w, float& h) const
{
	w = 520.0f; h = 50.0f;
	x = m_screenWidth / 2.0f - w / 2.0f;
	y = 270.0f + i * 66.0f;
}

void SceneManager::ApplyOutcomes(const EventChoice& c)
{
	auto& pd = PlayerDataManager::GetData();
	for (auto& o : c.outcomes)
	{
		if (o.type == "hp") { pd.hp += o.value; if (pd.hp > pd.maxHp) pd.hp = pd.maxHp; if (pd.hp < 0) pd.hp = 0; }
		else if (o.type == "maxHp") { pd.maxHp += o.value; pd.hp += o.value; if (pd.hp < 1) pd.hp = 1; }
		else if (o.type == "gold") { pd.gold += o.value; if (pd.gold < 0) pd.gold = 0; }
		else if (o.type == "material") { PlayerDataManager::AddMaterial(o.param, o.value > 0 ? o.value : 1); }
		else if (o.type == "relic")
		{
			std::string rid = o.param.empty() ? RelicManager::RandomUnowned("event") : o.param;
			if (rid.empty()) rid = RelicManager::RandomDrop();
			if (!rid.empty()) PlayerDataManager::AddRelic(rid);
		}
		else if (o.type == "addCard") { PlayerDataManager::AddCard(o.param); }
	}
	PlayerDataManager::Save();
}

void SceneManager::DrawEvent()
{
	const EventDef* e = EventDataBase::Get(m_eventId);
	if (!e) { m_eventOpen = false; return; }
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	POINT mp = m_uiInput.GetMousePos();

	m_uiSprite->Begin();
	m_uiSprite->DrawSprite(white, 0, BAR_H, (float)m_screenWidth, (float)m_screenHeight - BAR_H, 0.0f,
		XMFLOAT4(0.05f, 0.03f, 0.09f, 0.96f));
	if (m_eventResult < 0)
		for (int i = 0; i < (int)e->choices.size(); i++)
		{
			float x, y, w, h; GetEventChoiceRect(i, x, y, w, h);
			bool en = ChoiceEnabled(e->choices[i]);
			char key[16]; sprintf_s(key, "evc%d", i);
			bool hov = en && UiHover(x, y, w, h, mp, key);   // ← ホバーSE＋判定
			float yy = hov ? y - 6.0f : y;                   // ← 浮く
			m_uiSprite->DrawSprite(white, x, yy, w, h, 0.0f,
				!en ? XMFLOAT4(0.12f, 0.12f, 0.14f, 1.0f)
				: hov ? XMFLOAT4(0.4f, 0.32f, 0.5f, 1.0f) : XMFLOAT4(0.2f, 0.17f, 0.28f, 1.0f));
		}
	m_uiSprite->End();

	m_textRenderer->Begin();
	m_textRenderer->DrawText(ToWString(e->title).c_str(), m_screenWidth / 2.0f - 150.0f, 90.0f, 30.0f, D2D1::ColorF(1, 0.9f, 0.6f));
	if (m_eventResult < 0)
	{
		m_textRenderer->DrawText(ToWString(e->desc).c_str(), m_screenWidth / 2.0f - 260.0f, 160.0f, 18.0f, D2D1::ColorF(0.9f, 0.9f, 0.9f));
		for (int i = 0; i < (int)e->choices.size(); i++)
		{
			float x, y, w, h; GetEventChoiceRect(i, x, y, w, h);
			bool en = ChoiceEnabled(e->choices[i]);
			bool hov = en && mp.x >= x && mp.x <= x + w && mp.y >= y && mp.y <= y + h;
			float yy = hov ? y - 6.0f : y;
			m_textRenderer->DrawText(ToWString(e->choices[i].label).c_str(), x + 16.0f, yy + 13.0f, 20.0f,
				en ? D2D1::ColorF(1, 1, 1) : D2D1::ColorF(0.4f, 0.4f, 0.4f));
		}
	}
	else
	{
		m_textRenderer->DrawText(ToWString(e->choices[m_eventResult].result).c_str(), m_screenWidth / 2.0f - 260.0f, 220.0f, 22.0f, D2D1::ColorF(0.8f, 1.0f, 0.8f));
		m_textRenderer->DrawText(L"クリックで進む", m_screenWidth / 2.0f - 70.0f, m_screenHeight - 100.0f, 20.0f, D2D1::ColorF(0.8f, 0.8f, 0.8f));
	}
	m_textRenderer->End();
}

static std::string EventPickerType(const EventChoice& c)
{
	for (auto& o : c.outcomes)
		if (o.type == "removeCard" || o.type == "upgradeCard" || o.type == "transformCard" || o.type == "transformRelic")
			return o.type;
	return "";
}

void SceneManager::GetEventCardSlot(int i, float& x, float& y) const
{
	const float s = 0.85f;
	float cw = CardVisual::CARD_W * s, ch = CardVisual::CARD_H * s;
	int perRow = 8; float gapX = 12.0f, gapY = 16.0f;
	float startX = (m_screenWidth - (perRow * cw + (perRow - 1) * gapX)) / 2.0f;
	x = startX + (i % perRow) * (cw + gapX);
	y = 150.0f + (i / perRow) * (ch + gapY);
}
int SceneManager::EventCardAt(POINT p) const
{
	auto& deck = PlayerDataManager::GetData().deck;
	for (int i = 0; i < (int)deck.size(); i++)
	{
		float bx, by; GetEventCardSlot(i, bx, by);
		float x, y, w, h; CardVisual::GetRect(bx, by, 0.85f, x, y, w, h);
		if (p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h) return i;
	}
	return -1;
}
void SceneManager::ApplyCardPick(int idx)
{
	auto& deck = PlayerDataManager::GetData().deck;
	if (idx < 0 || idx >= (int)deck.size()) return;
	if (m_eventPickType == "removeCard") PlayerDataManager::RemoveCard(idx);
	else if (m_eventPickType == "upgradeCard") PlayerDataManager::UpgradeCard(idx);
	else if (m_eventPickType == "transformCard")
	{
		static const char* pool[] = { "ATK_strike","SKL_defend","MOV_move","ATK_Spin Slash","MOV_dash","ATK_poison_blade","POW_power_attack","POW_buff_defense" };
		deck[idx] = pool[rand() % (int)(sizeof(pool) / sizeof(pool[0]))];
		PlayerDataManager::Save();
	}
}

void SceneManager::DrawEventPicker()
{
	auto& deck = PlayerDataManager::GetData().deck;
	bool anim = (m_eventPickAnimIdx >= 0);
	int hov = anim ? -1 : EventCardAt(m_uiInput.GetMousePos());
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	float t = anim ? (1.0f - m_eventPickAnimTimer / EVENT_PICK_ANIM_DUR) : 0.0f;
	bool morph = anim && !m_eventPickAnimTo.empty();

	const CardData* animCard = nullptr; float animScale = 0.85f, animAlpha = 1.0f, animYoff = 0.0f, animBX = 0, animBY = 0;
	if (anim)
	{
		GetEventCardSlot(m_eventPickAnimIdx, animBX, animBY);
		if (!morph) { if (m_eventPickAnimIdx < (int)deck.size()) animCard = CardDataBase::Get(deck[m_eventPickAnimIdx]); animScale = 0.85f * (1.0f - t * 0.7f); animAlpha = 1.0f - t; animYoff = -t * 40.0f; }
		else if (t < 0.5f) { if (m_eventPickAnimIdx < (int)deck.size()) animCard = CardDataBase::Get(deck[m_eventPickAnimIdx]); animAlpha = 1.0f - t * 2.0f; }
		else { animCard = CardDataBase::Get(m_eventPickAnimTo); animAlpha = (t - 0.5f) * 2.0f; }
	}

	m_uiSprite->Begin();
	m_uiSprite->DrawSprite(white, 0, BAR_H, (float)m_screenWidth, (float)m_screenHeight - BAR_H, 0.0f, XMFLOAT4(0.05f, 0.03f, 0.09f, 0.96f));
	for (int i = 0; i < (int)deck.size(); i++)
	{
		if (i == m_eventPickAnimIdx) continue;
		const CardData* d = CardDataBase::Get(deck[i]);
		if (!d) continue;
		float bx, by; GetEventCardSlot(i, bx, by);
		if (i == hov) by -= 12.0f;
		CardVisual::DrawBase(m_uiSprite, white, bx, by, 0.85f, 0.0f, i == hov ? XMFLOAT4(0.6f, 0.4f, 0.7f, 1.0f) : CardVisual::GetCardColor(d->type), d, m_uiTime);
	}
	if (anim && animCard)
	{
		XMFLOAT4 col = CardVisual::GetCardColor(animCard->type); col.w = animAlpha;
		CardVisual::DrawBase(m_uiSprite, white, animBX, animBY + animYoff, animScale, 0.0f, col, animCard, m_uiTime);
		if (morph)
		{
			float flash = 1.0f - fabsf(t - 0.5f) * 4.0f;
			if (flash > 0.0f) { float x, y, w, h; CardVisual::GetRect(animBX, animBY, 0.85f, x, y, w, h); m_uiSprite->DrawSprite(white, x, y, w, h, 0.0f, XMFLOAT4(1, 1, 1, flash * 0.8f)); }
		}
	}
	m_uiSprite->End();

	m_textRenderer->Begin();
	if (!anim)
	{
		std::wstring prompt = L"カードを選択";
		if (m_eventPickType == "removeCard") prompt = L"削除するカードを選択";
		else if (m_eventPickType == "upgradeCard") prompt = L"強化するカードを選択";
		else if (m_eventPickType == "transformCard") prompt = L"変化させるカードを選択";
		m_textRenderer->DrawText(prompt.c_str(), m_screenWidth / 2.0f - 150.0f, 90.0f, 26.0f, D2D1::ColorF(1, 0.9f, 0.6f));
	}
	for (int i = 0; i < (int)deck.size(); i++)
	{
		if (i == m_eventPickAnimIdx) continue;
		const CardData* d = CardDataBase::Get(deck[i]);
		if (!d) continue;
		float bx, by; GetEventCardSlot(i, bx, by);
		if (i == hov) by -= 12.0f;
		CardVisual::DrawTexts(m_textRenderer, d, nullptr, bx, by, 0.85f, 0.0f, 1.0f);
	}
	if (anim && animCard)
		CardVisual::DrawTexts(m_textRenderer, animCard, nullptr, animBX, animBY + animYoff, animScale, 0.0f, animAlpha);
	m_textRenderer->End();
}

bool SceneManager::ChoiceEnabled(const EventChoice& c) const
{
	int need = 0;
	for (auto& o : c.outcomes)
		if (o.type == "gold" && o.value < 0) need += -o.value;
	for (auto& o : c.outcomes)
		if (o.type == "transformRelic" && PlayerDataManager::GetData().relics.empty()) return false;
	return PlayerDataManager::GetData().gold >= need;
}

void SceneManager::GetEventRelicSlot(int i, float& x, float& y) const
{
	float w = 160.0f, h = 44.0f, gap = 12.0f; int cols = 6;
	float startX = (m_screenWidth - (cols * w + (cols - 1) * gap)) / 2.0f;
	x = startX + (i % cols) * (w + gap);
	y = 180.0f + (i / cols) * (h + gap);
}
int SceneManager::EventRelicAt(POINT p) const
{
	auto& relics = PlayerDataManager::GetData().relics;
	for (int i = 0; i < (int)relics.size(); i++)
	{
		float x, y; GetEventRelicSlot(i, x, y);
		if (p.x >= x && p.x <= x + 160.0f && p.y >= y && p.y <= y + 44.0f) return i;
	}
	return -1;
}
void SceneManager::DrawEventRelicPicker()
{
	auto& relics = PlayerDataManager::GetData().relics;
	bool anim = (m_eventPickAnimIdx >= 0);
	int hov = anim ? -1 : EventRelicAt(m_uiInput.GetMousePos());
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	float t = anim ? (1.0f - m_eventPickAnimTimer / EVENT_PICK_ANIM_DUR) : 0.0f;

	auto slotInfo = [&](int i, std::string& id, float& a) {
		id = relics[i]; a = 1.0f;
		if (i == m_eventPickAnimIdx)
		{
			if (t < 0.5f) a = 1.0f - t * 2.0f;
			else { a = (t - 0.5f) * 2.0f; id = m_eventPickAnimTo; }
		}
		};

	m_uiSprite->Begin();
	m_uiSprite->DrawSprite(white, 0, BAR_H, (float)m_screenWidth, (float)m_screenHeight - BAR_H, 0.0f, XMFLOAT4(0.05f, 0.03f, 0.09f, 0.96f));
	for (int i = 0; i < (int)relics.size(); i++)
	{
		float x, y; GetEventRelicSlot(i, x, y);
		std::string id; float a; slotInfo(i, id, a);
		m_uiSprite->DrawSprite(white, x, y, 160.0f, 44.0f, 0.0f, (i == hov) ? XMFLOAT4(0.55f, 0.4f, 0.7f, a) : XMFLOAT4(0.35f, 0.28f, 0.45f, a));
	}
	m_uiSprite->End();

	m_textRenderer->Begin();
	if (!anim) m_textRenderer->DrawText(L"入れ替えるレリックを選択", m_screenWidth / 2.0f - 160.0f, 100.0f, 26.0f, D2D1::ColorF(1, 0.9f, 0.6f));
	for (int i = 0; i < (int)relics.size(); i++)
	{
		float x, y; GetEventRelicSlot(i, x, y);
		std::string id; float a; slotInfo(i, id, a);
		auto d = RelicManager::Get(id);
		m_textRenderer->DrawText((d ? ToWString(d->name) : L"?").c_str(), x + 8.0f, y + 13.0f, 15.0f, D2D1::ColorF(1, 1, 1, a));
	}
	m_textRenderer->End();
}

void SceneManager::PlayGeneralBGM()
{
	int layer = PlayerDataManager::GetData().layer;
	if (m_generalBgmLayer != layer)   // 層が変わったら選び直し
	{
		static const std::vector<std::vector<const char*>> pool = {
			{ "Assets/Sound/bgm/Field1.mp3" },   // 層1
			{ "Assets/Sound/bgm/Field2.mp3" },   // 層2
			{ "Assets/Sound/bgm/Field3.mp3" },   // 層3
		};
		int li = (layer >= 1 && layer <= 3) ? layer - 1 : 0;
		auto& tracks = pool[li];
		m_generalBgm = tracks[rand() % tracks.size()];   // 層内に複数あればランダム
		m_generalBgmLayer = layer;
	}
	Audio::PlayBGM(m_generalBgm);
}