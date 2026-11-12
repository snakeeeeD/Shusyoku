#include "SceneManager.h"
#include "EffectDataBase.h"
#include "EnemyDataBase.h"
#include "CardDataBase.h"
#include "CardVisual.h"
#include "CardTooltip.h"
#include "ItemTooltip.h"
#include "PlayerDataManager.h"
#include "EncounterDataBase.h"
#include "EditorHub.h" 
#include "TerrainDataBase.h"
#include "MaterialDataBase.h"
#include "RelicManager.h"
#include "UiWindow.h"
#include "Audio.h"
#include "Se.h"
#include "Hint.h"
#include "ScreenShake.h"
#include "HpBar.h"
#include "Settings.h"
#include "QuickPlay.h"
#include "QuickSelectScene.h"
#include "Glossary.h"
#include "Tutorials.h"
#include "GuideBox.h"
#include "CardConfirm.h"
#define PL_MPEG_IMPLEMENTATION
#include "VideoClip.h"
extern ID3D11Device* g_pd3dDevice;
extern ID3D11DeviceContext* g_pImmediateContext;

#include <cmath>

#include "External/imgui/imgui.h"


static std::string s_lastHoverKey;

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
	Glossary::All();   // 用語集を読む（カードの説明の色付けにも使う）
	EffectDataBase::Load("Assets/Data/effects.json");
	MaterialDataBase::Load("Assets/Data/materials.json");
	RelicManager::Load("Assets/Data/relics.json");
	EventDataBase::Load("Assets/Data/events.json");
	QuickPlay::Load("Assets/Data/quickplay.json");
	PlayerDataManager::Init();

	Audio::SetMasterVolume(1.0f);
	Audio::SetBgmVolume(Settings::Get().bgmVolume);
	Audio::SetSeMasterVolume(Settings::Get().seVolume);

	Audio::SetBgmTrackVolume("Assets/Sound/bgm/Field1.mp3", 0.5f);
	Audio::SetBgmTrackVolume("Assets/Sound/bgm/Field2.mp3", 0.5f);
	Audio::SetBgmTrackVolume("Assets/Sound/bgm/Field3.mp3", 0.3f);
	Audio::SetSeVolume("Assets/Sound/se/hover.mp3", 0.4f);   // ホバーは控えめ
	Audio::SetSeVolume("Assets/Sound/se/click.mp3", 0.7f);
	Audio::SetSeVolume("Assets/Sound/se/hit.mp3", 1.0f);
	// 未設定は 1.0

	// 画像（一覧は Assets/Data/textures.json。足す時はそこに1行）
	TextureManager::LoadList("Assets/Data/textures.json");
	RelicManager::LoadIcons();   // レリックのアイコン（relics.json の icon から）

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
	if (m_fadeState == Fade::Out) return;
	m_pendingScene = type;
	m_onBlack = nullptr;                      // 通常遷移はonBlackなし
	m_fadeState = Fade::Out;
}

void SceneManager::ChangeScene(SceneType type, std::function<void()> onBlack)
{
	CardTooltip::ClearHeld();
	if (m_fadeState == Fade::Out) return;
	m_pendingScene = type;
	m_onBlack = std::move(onBlack);
	m_fadeState = Fade::Out;
}
void SceneManager::DoChangeScene(SceneType type)
{
	Hint::Clear();
	CardTooltip::ClearHeld();   // 暗転中に前のシーンが覚え直した説明も、ここで消す（次のシーンに残さない）

	if (type == SceneType::Title) QuickPlay::End();   // タイトルへ戻る時は必ず終了（本編の状態に戻す）
	if (QuickPlay::Active() && type == SceneType::Field)
		type = QuickPlay::NextScene();                // 「フィールドへ戻る」を次の戦闘か結果画面に読み替える

	// 削除前に必要な情報を取得
	std::string battleEnemyId;
	int battleSeed = 0;
	int battleOverflow = 0;
	int battleTier = 1;
	EncCategory battleCategory = EncCategory::Normal;
	bool resultCleared = false;
	if (type == SceneType::Result)
	{
		std::string causeEnemy;
		int hpLeft = PlayerDataManager::GetData().hp;
		if (auto battle = dynamic_cast<BattleScene*>(m_currentScene))
		{
			resultCleared = (battle->GetBattleResult() == BattleResult::Win);
			hpLeft = battle->GetPlayerHp();
			causeEnemy = battle->GetEnemyId();
		}
		auto& pd = PlayerDataManager::GetData();
		std::string deckStr, relicStr;
		for (auto& c : pd.deck) { if (!deckStr.empty())  deckStr += ","; deckStr += c; }
		for (auto& r : pd.relics) { if (!relicStr.empty()) relicStr += ","; relicStr += r; }
		Telemetry::Instance().Log("run_end", {
			{"result", resultCleared ? "clear" : "gameover"},
			{"layer",  pd.layer},
			{"node",   pd.fieldSteps},
			{"hpLeft", hpLeft},
			{"maxHp",  pd.maxHp},
			{"cause",  causeEnemy},
			{"sec",    Telemetry::Instance().RunSeconds()},
			{"deckSize", (int)pd.deck.size()},   // ← 平均デッキ枚数（健康診断）
			{"gold",     pd.gold},               // ← 経済（将来用・安価）
			{"deck",     deckStr},               // ← 最終デッキ全体（アーキタイプ分析）
			{"relics",   relicStr},              // ← 最終レリック（レリック×勝率）
			});
		Telemetry::Instance().Upload(true);   // ← 非同期でこのセッションを送信
		PlayerDataManager::DeleteSave();   // ランを終えたらセーブ削除
	}
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
		if (QuickPlay::Active()) QuickPlay::TakeBattle(battleEnemyId, battleCategory);
	}

	// 古いシーンを削除
	delete m_currentScene;
	m_currentScene = nullptr;
	m_currentType = type;

	// テレメトリ: シーン・現在地を更新して入場を記録
	{
		auto& pd = PlayerDataManager::GetData();
		Telemetry::Instance().SetLocation(pd.layer, pd.fieldSteps);
		Telemetry::Instance().SetScene(SceneName(type));
		Telemetry::Instance().Log("scene_enter", { {"hp", pd.hp} });
	}

	// 新しいシーンを作成
	switch (type)
	{
		case SceneType::Title:
		{
			auto scene = new TitleScene();
			scene->onChangeScene = [this](SceneType type) {ChangeScene(type); };
			m_currentScene = scene;
			scene->onChangeSceneBlack = [this](SceneType t, std::function<void()> fn) { ChangeScene(t, std::move(fn)); };
			scene->onOpenSettings = [this]() { m_settings.Open(false); };   // タイトル画面では「タイトルに戻る」なし
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
			if (m_cardSelectMode == "rare")          scene->SetMode(CardSelectScene::RewardMode::Rare);
			else if (m_cardSelectMode == "upgraded") scene->SetMode(CardSelectScene::RewardMode::Upgraded);
			m_cardSelectMode.clear();   // 使い切り
			scene->onChangeScene = [this](SceneType type) {ChangeScene(type); };
			m_currentScene = scene;
			break;
		}
		case SceneType::Field:
		{
			auto scene = new FieldScene();
			scene->onChangeScene = [this](SceneType type) { ChangeScene(type); };
			scene->onRest = [this]() {
				m_rest.Open();
				Audio::PlayBGM("Assets/Sound/bgm/Rest.mp3");
				auto& pd = PlayerDataManager::GetData();
				};
			scene->onEvent = [this](const std::string& id) { m_event.Open(id); };
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
			scene->onChangeSceneBlack = [this](SceneType t, std::function<void()> fn) { ChangeScene(t, std::move(fn)); };
			break;
		}
		case SceneType::QuickSelect:
		{
			auto scene = new QuickSelectScene();
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

	// 初めての画面のチュートリアル（1回だけ。中身は Tutorials.h）
	if (type == SceneType::Field)      ShowTutorialOnce(TutorialId::Field);
	if (type == SceneType::Battle)     ShowTutorialOnce(TutorialId::Battle);
	if (type == SceneType::CardSelect) ShowTutorialOnce(TutorialId::CardSelect);
	if (type == SceneType::Shop)       ShowTutorialOnce(TutorialId::Shop);
}

void SceneManager::Draw()
{
	if (auto field = dynamic_cast<FieldScene*>(m_currentScene))
		field->SetHoverEnabled(!AnyModalOpen());   // 窓が開いている間は後ろのマスを反応させない（イベント中も）
	UiContext ui = Ui();
	if (m_currentScene) m_currentScene->Draw();
	if (m_items.IsInventory()) m_items.Draw(ui, m_currentType != SceneType::Battle);
	if (m_map.IsOpen()) m_map.Draw(ui);
	bool fading = (m_fadeState != Fade::None);
	if (HasTopBar() && !fading) DrawOverlay();
	if (m_event.IsOpen() && !m_map.IsOpen() && !m_items.IsInventory() && !m_deck.IsOpen()) m_event.Draw(ui);
	if (m_items.IsCraft()) m_items.Draw(ui, true);
	if (m_rest.IsShown() && !m_map.IsOpen() && !m_items.IsInventory() && !m_deck.IsOpen() && !m_items.IsCraft()) m_rest.Draw(ui, !m_tutorial.IsOpen());   // チュートリアル中は説明なし
	if (m_deck.IsOpen() || m_map.IsOpen() || m_items.IsInventory() || m_rest.IsShown() || m_items.IsCraft()) DrawBackButton();
	if (m_craftFxTimer > 0.0f) DrawCraftFx();  
	if (HasTopBar() && !fading) { DrawRelicBar(); DrawBarTips(); }
	if (BossRelicPickOpen() && !fading) m_bossRelic.Draw(ui);
	if (!m_tutorial.Active() && !fading && !CamDemoActive()) GuideBox::DrawHint(ui);
	if (auto b = dynamic_cast<BattleScene*>(m_currentScene))   // カメラ操作のデモの説明
	{
		const wchar_t* t; const char* ic; float a;
		if (!fading && b->CamDemoCaption(t, ic, a)) GuideBox::Draw(ui, t, a, ic, L"クリックで飛ばす");
	}
	if (m_settings.IsOpen()) m_settings.Draw(ui);
	if (m_glossary.IsOpen()) m_glossary.Draw(ui);

	if (m_tutorial.IsOpen()) m_tutorial.Draw(ui, TutorialSpot(m_tutorial.Current()));

	// ドロップ説明ウィンドウは暗幕の上に出す（勝利画面のホバー）
	if (auto b = dynamic_cast<BattleScene*>(m_currentScene)) b->DrawDropTooltipTop();

	if (!TooltipBlocked()) CardTooltip::EndFrame(m_uiSprite, m_textRenderer, TextureManager::Get("white"));   // 説明を残す
	else CardTooltip::ClearHeld();
	CardConfirm::Draw(m_uiSprite, m_textRenderer, TextureManager::Get("white"), m_uiInput.GetMousePos(),
		m_screenWidth, m_screenHeight);   // 強化・削除の確認（一番手前）

	if (m_fadeAlpha > 0.0f)
	{
		m_uiSprite->Begin();
		m_uiSprite->DrawSprite(TextureManager::Get("white"), 0, 0,
			(float)m_screenWidth, (float)m_screenHeight, 0.0f, XMFLOAT4(0, 0, 0, m_fadeAlpha));
		m_uiSprite->End();
	}
	EditorHub::DrawGameOverlay(m_uiSprite, m_textRenderer);   // エディタのプレビュー（Release では何もしない）
}

void SceneManager::DrawOverlay()
{
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	int deckCount = (int)PlayerDataManager::GetData().deck.size();

	float btnX = m_screenWidth - 220.0f;

	// === スプライト ===
	m_uiSprite->Begin();

	if (m_deck.IsOpen()) { UiContext ui = Ui(); m_deck.DrawGrid(ui); }   // 暗幕・一覧（帯より先に）

	// 帯：窓と同じ質感（上と左右の枠は画面外に逃がし、下の縁だけ見せる）＋下端の金のライン
	UiWindow::Draw(m_uiSprite, white, -16.0f, -16.0f, (float)m_screenWidth + 32.0f, BAR_H + 16.0f);
	m_uiSprite->DrawSprite(white, 0.0f, BAR_H - 2.0f, (float)m_screenWidth, 2.0f, 0.0f, XMFLOAT4(0.85f, 0.7f, 0.3f, 0.8f));

	// 数値欄（HP / Steps / Layer / G）：暗いくぼみ＋左端にその数値の色の印
	const XMFLOAT4 statCol[] = { { 1.0f, 0.5f, 0.5f, 1 }, { 0.6f, 0.9f, 0.6f, 1 }, { 0.75f, 0.8f, 1.0f, 1 }, { 1.0f, 0.9f, 0.3f, 1 } };
	for (int i = 0; i < 4; i++)
	{
		URect r = BarStatRect(i);
		m_uiSprite->DrawSprite(white, r.x, r.y, r.w, r.h, 0.0f, XMFLOAT4(0, 0, 0, 0.35f));
		m_uiSprite->DrawSprite(white, r.x, r.y, 3.0f, r.h, 0.0f, statCol[i]);
	}

	// 数値欄のアイコン（白い画像を欄の色で染める）
	const char* statIcon[] = { "icon_hp", "icon_steps", "icon_layer", "icon_gold" };
	for (int i = 0; i < 4; i++)
		if (statIcon[i])
			if (auto tex = TextureManager::Get(statIcon[i]))
			{
				URect r = BarStatRect(i);
				m_uiSprite->DrawSprite(tex, r.x + 8.0f, r.y + 3.0f, 22.0f, 22.0f, 0.0f, statCol[i]);
			}

	URect ib = Lifted(BarButtonRect(1)), db = Lifted(BarButtonRect(2));
	m_uiSprite->DrawSprite(TextureManager::Get("ui_items"), ib.x, ib.y, ib.w, ib.h, 0.0f, XMFLOAT4(1, 1, 1, 1));
	m_uiSprite->DrawSprite(TextureManager::Get("ui_deck"), db.x, db.y, db.w, db.h, 0.0f, XMFLOAT4(1, 1, 1, 1));   // デッキボタン
	m_uiSprite->DrawSprite(TextureManager::Get("particle"),
		db.x + db.w - 24.0f, db.y + db.h - 22.0f, 24.0f, 24.0f, 0.0f, XMFLOAT4(0.10f, 0.09f, 0.13f, 1.0f));

	{   // Mapボタン：小さな正方形＋地図のアイコン（フィールドでも出す）
		URect mbBase = BarButtonRect(0), mb = Lifted(mbBase);
		UiWindow::Button(m_uiSprite, white, mb.x, mb.y, mb.w, mb.h, XMFLOAT4(0.22f, 0.22f, 0.3f, 1));   // ホバーは浮くだけ（色は変えない）
		m_uiSprite->DrawSprite(TextureManager::Get("icon_map"), mb.x + (mb.w - 24.0f) / 2.0f, mb.y + 3.0f, 24.0f, 24.0f,
			0.0f, XMFLOAT4(0.95f, 0.88f, 0.7f, 1));   // 羊皮紙の色
	}

	{
		URect g = Lifted(BarGearRect());
		UiWindow::Button(m_uiSprite, white, g.x, g.y, g.w, g.h, XMFLOAT4(0.22f, 0.22f, 0.3f, 1));   // ホバーは浮くだけ（色は変えない）
	}

	m_uiSprite->End();

	// === テキスト ===
	m_textRenderer->Begin();
	// 数値をアイコンの右側の中央に
	auto statText = [&](int i, const wchar_t* s, D2D1::ColorF c) {
		URect r = BarStatRect(i);
		float left = r.x + 32.0f;
		m_textRenderer->DrawOutlinedTextCentered(s, left + (r.x + r.w - left) / 2.0f, r.y, r.x + r.w - left, r.h,
			16.0f, c, D2D1::ColorF(0, 0, 0), 1.5f);
		};
	{
		wchar_t t[16]; swprintf_s(t, L"%d", (int)PlayerDataManager::GetData().deck.size());
		m_textRenderer->DrawOutlinedText(t, btnX + 100.0f - 18.0f, 5.0f + 30.0f - 20.0f, 17.0f,
			D2D1::ColorF(1, 1, 1), D2D1::ColorF(0, 0, 0), 2.0f);
	}

	{
		URect db2 = Lifted(BarButtonRect(2));
		wchar_t t[16]; swprintf_s(t, L"%d", (int)PlayerDataManager::GetData().deck.size());
		m_textRenderer->DrawOutlinedText(t, db2.x + db2.w - 18.0f, db2.y + db2.h - 20.0f, 17.0f,
			D2D1::ColorF(1, 1, 1), D2D1::ColorF(0, 0, 0), 2.0f);
	}

	{
		URect g = Lifted(BarGearRect());
		m_textRenderer->DrawTextCentered(L"設定", g.x + g.w / 2.0f, g.y, g.w, 16.0f, D2D1::ColorF(1, 1, 1), g.h);
	}

	
	wchar_t gbuf[32];
	swprintf_s(gbuf, L"%d", PlayerDataManager::GetData().gold);
	statText(3, gbuf, D2D1::ColorF(1, 0.9f, 0.3f));          // ゴールド

	auto& pd = PlayerDataManager::GetData();
	int hp = pd.hp, mhp = pd.maxHp;
	if (auto battle = dynamic_cast<BattleScene*>(m_currentScene))   // バトル中は生HP
	{
		hp = battle->GetPlayerHp(); mhp = battle->GetPlayerMaxHp();
	}

	wchar_t hbuf[48];
	swprintf_s(hbuf, L"%d/%d", hp, mhp);
	statText(0, hbuf, D2D1::ColorF(1.0f, 0.5f, 0.5f));       // HP

	wchar_t sbuf[48];
	swprintf_s(sbuf, L"%d", pd.fieldSteps);
	statText(1, sbuf, D2D1::ColorF(0.6f, 0.9f, 0.6f));       // Steps

	wchar_t lbuf[32]; swprintf_s(lbuf, L"%d/3", pd.layer);
	statText(2, lbuf, D2D1::ColorF(0.75f, 0.8f, 1.0f));      // Layer

	m_textRenderer->End();

	if (m_deck.IsOpen()) { UiContext ui = Ui(); m_deck.DrawTop(ui, m_craftFxTimer <= 0.0f); }   // 拡大表示・説明（一番手前。強化の演出中は説明なし）
}


void SceneManager::DrawImGui()
{
#ifdef _DEBUG

	// イベントエディタの「Save & Open in Game」
	std::string editorEvent;
	if (EditorHub::ConsumeOpenEvent(editorEvent))
	{
		m_event.Open(editorEvent);
	}

	ImGui::Begin("Debug");
	if (ImGui::BeginTabBar("DebugTabs"))
	{
		if (ImGui::BeginTabItem("Global"))
		{
			int gold = PlayerDataManager::GetData().gold;
			if (ImGui::InputInt("Gold", &gold))
			{
				PlayerDataManager::GetData().gold = gold;
				PlayerDataManager::Save();
			}
			ImGui::Checkbox(u8"録画用：説明とターン表示を隠す", &CardTooltip::s_debugHide);   // カード・敵・マスの説明

			auto& pd = PlayerDataManager::GetData();
			float mv = pd.masterVolume, bv = pd.bgmVolume;
			if (ImGui::SliderFloat("Master", &mv, 0.0f, 1.0f)) { pd.masterVolume = mv; Audio::SetMasterVolume(mv); PlayerDataManager::Save(); }
			if (ImGui::SliderFloat("BGM", &bv, 0.0f, 1.0f)) { pd.bgmVolume = bv;    Audio::SetBgmVolume(bv);    PlayerDataManager::Save(); }

			ImGui::Separator();
			{
				// チェックON=チュートリアルを表示する（＝各フラグを未表示に戻す）
				bool showTut = !(pd.tutorialField && pd.tutorialBattle && pd.tutorialCardSelect
					&& pd.tutorialShop && pd.tutorialRest && pd.tutorialCraft && pd.tutorialCamera);
				if (ImGui::Checkbox("Show Tutorials", &showTut))
				{
					bool seen = !showTut;   // 表示する→フラグfalse / 出さない→true
					pd.tutorialField = pd.tutorialBattle = pd.tutorialCardSelect =
						pd.tutorialShop = pd.tutorialRest = pd.tutorialCraft = pd.tutorialCamera = seen;
					if (showTut) pd.seenHints.clear();   // ON：ヒントを未読に戻す
					else Hint::MarkAllSeen();            // OFF：ヒントも全部既読に（録画などで出したくない時）
					PlayerDataManager::Save();
				}

				EditorHub::DrawMenu();

			}

						if (ImGui::CollapsingHeader("Relics"))
			{
				auto ids = RelicManager::AllIds();
				std::sort(ids.begin(), ids.end());     // アルファベット順
				for (auto& id : ids)
					if (ImGui::Button(id.c_str()))
						PlayerDataManager::AddRelic(id);
			}                                   

			ImGui::EndTabItem();
		}

		const char* sceneTab = m_currentScene ? m_currentScene->DebugName() : nullptr;
		if (sceneTab && ImGui::BeginTabItem(sceneTab))
		{
			m_currentScene->DrawImGui();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
	EditorHub::DrawAll();
#endif
}

// 入力は上から順に、受け取った所で止まる（手前の窓ほど先）
void SceneManager::HandleInput()
{
	m_uiInput.Update();
	if (HandleConfirmAndTooltip()) return;   // 強化・削除の確認／閉じたクリックの「離す」／説明の上のクリック
	if (m_fadeState != Fade::None) return;   // フェード中は入力無効
	HandleScrollDrag();                      // デッキ・持ち物のホイールとドラッグ（ドラッグしたらクリック扱いしない）
	LogInputState();
	UiContext ui = Ui();
	if (HandleOverlayInput(ui)) return;      // チュートリアル → 演出中 → ボスレリック → 用語集 → 設定
	if (HandleTopBarInput(ui)) return;       // 帯のボタンと、帯から開く窓（マップ・持ち物・デッキ・合成）
	if (HandleFieldPanelInput(ui)) return;   // 休憩・イベント
	PassInputToScene();                      // 閉じたクリックを渡さない／ホイールをシーンへ戻す
}

// 強化・削除の確認（一番手前）／確認の窓を閉じたクリックの「離す」／カードの説明の上のクリック。受け取ったら true
bool SceneManager::HandleConfirmAndTooltip()
{
	if (CardConfirm::Active())   // 強化・削除の確認（一番手前。他の操作は止める）
	{
		if (m_uiInput.GetMouseButtonTrigger(0)) m_eatRelease = true;    // 押した→この後の「離す」は窓のもの
		if (m_uiInput.GetMouseButtonRelease(0)) m_eatRelease = false;   // 窓の中で離し終えた
		CardConfirm::HandleInput(m_uiInput.GetMousePos(), m_uiInput.GetMouseButtonTrigger(0), m_screenWidth, m_screenHeight);
		m_swallowSceneClick = true;
		return true;
	}
	if (m_eatRelease)   // 窓を閉じたクリックの「離す」：後ろの一覧に渡さない（離した瞬間に選ぶ一覧が、下のカードを選んでしまうため）
	{
		if (m_uiInput.GetMouseButtonRelease(0)) m_eatRelease = false;
		return true;
	}
	{
		// カードの説明の上のクリックは説明が使う（シーンには渡さない）
		POINT tm = m_uiInput.GetMousePos();
		bool tclick = m_uiInput.GetMouseButtonTrigger(0);
		CardTooltip::SetMouse((float)tm.x, (float)tm.y, tclick);
		if (tclick && CardTooltip::MouseInPanel() && !TooltipBlocked()) { m_swallowSceneClick = true; return true; }
	}
	if (AnyModalOpen()) m_swallowSceneClick = true;   // 開いている間に押されたクリックは、閉じた後もシーンに渡さない
	return false;
}

// デッキ・持ち物のホイールとドラッグ（ドラッグの判定は共通。6px 超えたらクリック扱いしない）
void SceneManager::HandleScrollDrag()
{
	{
		// ホイールでもスクロール（デッキと同じ m_uiInput 経由・ドラッグと同じターゲットを共有）
		if (m_items.IsOpen())
			m_items.ScrollBy(-m_uiInput.GetMouseWheelDelta() * 0.5f);

		bool held = m_uiInput.GetMouseButtonPress(0);
		POINT mp = m_uiInput.GetMousePos();
		if (held && (m_deck.IsOpen() || m_items.IsOpen()))
		{
			if (!m_mDrag) { m_mDrag = true; m_mDragStartY = mp.y; m_mDragLastY = mp.y; m_mDragMoved = false; }
			int dy = mp.y - m_mDragLastY;
			if (dy != 0)
			{
				if (m_deck.IsOpen()) m_deck.ScrollBy(-(float)dy);
				else            m_items.ScrollBy(-(float)dy);     // 持ち物 / 合成
				m_mDragLastY = mp.y;
			}
			int md = mp.y - m_mDragStartY; if (md < 0) md = -md;
			if (md > 6) m_mDragMoved = true;   // 6px超で「ドラッグ」確定
		}
		else m_mDrag = false;
	}
}

// 入力の状態をデバッグ出力に（1秒ごと）
void SceneManager::LogInputState()
{
	{
		static int _c = 0;
		if (++_c % 60 == 0) {
			char b[300];
			sprintf_s(b, "[IN] type=%d fade=%d tutO=%d tutW=%d cfx=%.2f rest=%d craft=%d ev=%d map=%d inv=%d deck=%d set=%d\n",
				(int)m_currentType, (int)m_fadeState, (int)m_tutorial.IsOpen(), (int)m_tutorial.Waiting(), m_craftFxTimer,
				(int)m_rest.IsShown(), (int)m_items.IsCraft(), (int)m_event.IsOpen(), (int)m_map.IsOpen(), (int)m_items.IsInventory(), (int)m_deck.IsOpen(), (int)m_settings.IsOpen());
			OutputDebugStringA(b);
		}
	}
}

// 一番上の窓：チュートリアル → 演出中は止める → ボスレリック → 用語集 → 設定。受け取ったら true
bool SceneManager::HandleOverlayInput(UiContext& ui)
{
	if (m_tutorial.IsOpen())
	{
		if (m_tutorial.HandleInput(ui))   // 最後のページを閉じた
			if (auto b = dynamic_cast<BattleScene*>(m_currentScene)) b->OnTutorialClosed();   // 保留していたレリックを発動
		return true;
	}

	if (m_tutorial.Waiting()) return true;   // チュートリアル表示待ち中は入力を止める（勝利連打で飛ぶのを防ぐ）

	if (m_craftFxTimer > 0.0f) return true;           // 演出中は入力停止

	if (BossRelicPickOpen())   // ボスレリックの選択（選ぶか「受け取らない」まで他の操作はしない）
	{
		m_bossRelic.HandleInput(ui);
		return true;
	}

	// 用語集（キーワードのクリックなど、どこからでも開ける）
	std::string glossId;
	if (Glossary::TakeRequest(glossId)) m_glossary.Open(glossId);
	if (m_glossary.IsOpen()) { m_glossary.HandleInput(ui); return true; }

	// 設定パネル（帯のある画面でもタイトルでも、開いている間は他を止める）
	if (m_settings.IsOpen())
	{
		switch (m_settings.HandleInput(ui))   // 窓の外でやること（窓は頼むだけ）
		{
		case SettingsPanel::Action::OpenGlossary:  m_glossary.Open(""); break;
		case SettingsPanel::Action::ReturnToTitle: ChangeScene(SceneType::Title); break;
		case SettingsPanel::Action::Quit:          PostQuitMessage(0); break;
		default: break;
		}
		return true;
	}
	return false;
}

// 帯のボタンと、帯から開く窓（マップ・持ち物・デッキ、戦闘以外なら合成）。受け取ったら true
bool SceneManager::HandleTopBarInput(UiContext& ui)
{
	if (HasTopBar())
	{
		POINT m = m_uiInput.GetMousePos();
		bool click = m_uiInput.GetMouseButtonTrigger(0);
		bool clickRel = m_uiInput.GetMouseButtonRelease(0) && !m_mDragMoved;  // タップ(離した瞬間・非ドラッグ)]::]
		bool onDeckBtn = click && BarButtonRect(2).has(m);

		// ギアで開閉
		if (click && BarGearRect().has(m)) { Audio::PlaySE("Assets/Sound/se/click.mp3"); m_settings.Open(m_currentType != SceneType::Title); return true; }
		

		// Map（どの画面でも開ける。フィールドでもイベント・休憩中に先の道を確認できる）
		if (m_map.IsOpen()) { if (click && BackBtnHit(m)) m_map.Close(); return true; }
		{
			if (click && !m_deck.IsOpen() && !m_items.IsCraft() && !m_items.IsInventory()
				&& BarButtonRect(0).has(m))
			{
				Audio::PlaySE("Assets/Sound/se/click.mp3");
				m_map.Open(); return true;
			}
		}

		// Items（いつでも）
		if (m_items.IsInventory())
		{
			if (clickRel && BackBtnHit(m)) { m_items.Close(); return true; }
			if (clickRel) m_items.HandleClick(ui, m, m_currentType != SceneType::Battle);   // 「合成する」で合成へ（戦闘中は押せない）
			return true;
		}
		{
			if (click && !m_deck.IsOpen() && !m_items.IsCraft() && !m_map.IsOpen()
				&& BarButtonRect(1).has(m))
			{
				Audio::PlaySE("Assets/Sound/se/click.mp3");
				m_items.OpenInventory(); return true;
			}
		}

		if (onDeckBtn && !m_deck.IsOpen() && !m_items.IsInventory() && !m_map.IsOpen() && !m_items.IsCraft())
		{
			Audio::PlaySE("Assets/Sound/se/click.mp3");
			m_deck.Open(DeckPanel::Mode::View);
			return true;
		}
		if (m_deck.IsOpen())
		{
			if (m_deck.HandleInput(ui, m, clickRel, BackBtnHit(m)) && m_rest.Active())
				m_rest.Show();   // 戻る：休憩から来ていたら休憩へ戻す
			return true;
		}
	}

	if (HasTopBar() && m_currentType != SceneType::Battle)
	{
		POINT m = m_uiInput.GetMousePos();
		bool click = m_uiInput.GetMouseButtonTrigger(0);
		bool clickRel = m_uiInput.GetMouseButtonRelease(0) && !m_mDragMoved;
		if (m_items.IsCraft())
		{
			if (clickRel)
			{
				if (BackBtnHit(m)) m_items.BackToInventory();   // 戻る＝持ち物へ
				else if (std::string made = m_items.HandleClick(ui, m, true); !made.empty())
				{ m_craftFxCard = made; m_craftFxTimer = CRAFT_FX_DURATION; }   // 作ったカードの出現演出（休憩の強化・イベントと共通）
			}
			return true;
		}
	}
	return false;
}

// フィールドから開く窓：休憩・イベント。受け取ったら true
bool SceneManager::HandleFieldPanelInput(UiContext& ui)
{
	if (m_rest.IsShown())
	{
		switch (m_rest.HandleInput(ui, BackBtnHit(m_uiInput.GetMousePos())))   // 窓の外でやること（窓は頼むだけ）
		{
		case RestPanel::Action::Finish:  FinishRest(); break;   // 回復した・戻る
		case RestPanel::Action::Upgrade: m_rest.Hide(); m_deck.Open(DeckPanel::Mode::Upgrade); break;
		default: break;
		}
		return true;
	}

	if (m_event.IsOpen())
	{
		EventPanel::Request rq = m_event.HandleInput(ui);   // 窓の外でやること（窓は頼むだけ）
		if (!rq.showCards.empty())                          // 呪いなど、手に入る瞬間を見せる
		{
			for (auto& c : rq.showCards) m_craftFxQueue.push_back(c);
			PlayNextCardFx();
		}
		switch (rq.go)
		{
		case EventPanel::Request::Go::Battle:
		{
			EncCategory cat = (rq.param == "event") ? EncCategory::Event : EncCategory::Normal;
			if (auto field = dynamic_cast<FieldScene*>(m_currentScene)) field->SetupEventBattle(cat, rq.param);
			ChangeScene(SceneType::Battle);
			break;
		}
		case EventPanel::Request::Go::Shop:       ChangeScene(SceneType::Shop); break;
		case EventPanel::Request::Go::CardSelect: m_cardSelectMode = rq.param; ChangeScene(SceneType::CardSelect); break;
		default: break;
		}
		return true;
	}
	return false;
}

// シーンへ：窓を閉じたクリックは渡さない（ボタンを離したら解除）／窓で読んだホイールをシーンへ戻す
void SceneManager::PassInputToScene()
{
	// モーダルを閉じたクリックはシーンに渡さない（ボタンを離したら解除）
	if (m_swallowSceneClick)
	{
		if (m_uiInput.GetMouseButtonPress(0)) return;
		m_swallowSceneClick = false;
	}

	// m_uiInputが消費したホイールをシーンへ戻す（ズーム等が効くように）
	Input::SetWheelDelta(m_uiInput.GetMouseWheelDelta());
	if (m_currentScene) m_currentScene->HandleInput();
}

// 初めての時に1回だけチュートリアルを出す（既読なら出さない → 既読にして保存 → 出す／遅らせる）
void SceneManager::ShowTutorialOnce(TutorialId id)
{
	for (auto& d : TutorialDefs())
	{
		if (d.id != id) continue;
		auto& pd = PlayerDataManager::GetData();
		if (pd.*d.seen) return;
		pd.*d.seen = true;
		PlayerDataManager::Save();
		if (d.delay > 0.0f) m_tutorial.ShowDelayed(d.pages, d.delay);
		else                m_tutorial.Show(d.pages);
		return;
	}
}

// チュートリアルで照らす場所：ページの指定。戦闘の物（手札のカード・当たりマーカー・敵のパネル）ならその今の位置
URect SceneManager::TutorialSpot(const TutorialPanel::Page& pg) const
{
	URect r{ pg.hx, pg.hy, pg.hw, pg.hh };
	if (auto battle = dynamic_cast<BattleScene*>(m_currentScene))
	{
		float x, y, w, h;
		if (pg.hoverCard >= 0 && battle->GetCardRect(pg.hoverCard, x, y, w, h)) r = { x, y, w, h };
		if (pg.showHitmark && battle->GetHitmarkRect(x, y, w, h)) r = { x, y, w, h };
		if (pg.enemyPanel && battle->GetEnemyPanelRect(x, y, w, h)) r = { x, y, w, h };
	}
	return r;
}

bool SceneManager::CamDemoActive() const
{
	auto b = dynamic_cast<BattleScene*>(m_currentScene);
	return b && b->CamDemoActive();
}


void SceneManager::DrawBarTips()
{
	if (AnyModalOpen()) return;   // モーダル表示中はバー説明を出さない

	POINT mp = m_uiInput.GetMousePos();
	if (mp.y < 5 || mp.y > 35) return;

	struct Tip { float x0, x1; const wchar_t* title; const wchar_t* desc; bool hideOnField; };
	URect hs = BarStatRect(0), ss = BarStatRect(1), ls = BarStatRect(2), gs = BarStatRect(3);
	URect mb = BarButtonRect(0), ib = BarButtonRect(1), db = BarButtonRect(2);
	Tip tips[] = {
		{ hs.x, hs.x + hs.w, L"体力", L"0でゲームオーバー", false },
		{ ss.x, ss.x + ss.w, L"歩数", L"0を超えて進むと敵が強くなる", false },
		{ ls.x, ls.x + ls.w, L"層",   L"ダンジョンの階層(全3層)。深いほど敵が強い", false },
		{ gs.x, gs.x + gs.w, L"ゴールド", L"ショップやイベントで消費する", false },
		{ mb.x, mb.x + mb.w, L"マップ",   L"マップを開く",   false },
		{ ib.x, ib.x + ib.w, L"アイテム", L"アイテムを見る", false },
		{ db.x, db.x + db.w, L"デッキ",   L"デッキを見る",   false },
	};

	const Tip* hit = nullptr;
	for (auto& t : tips)
	{
		if (mp.x < t.x0 || mp.x > t.x1) continue;
		if (t.hideOnField && m_currentType == SceneType::Field) continue;   // マップはフィールドで非表示
		hit = &t; break;
	}
	if (!hit) return;

	float w = 360.0f, h = 56.0f, ty = BAR_H + 44.0f + RelicManager::BarExtraHeight(m_screenWidth);
	float x = hit->x0;
	if (x + w > m_screenWidth - 6.0f) x = m_screenWidth - 6.0f - w;   // 右端はみ出し防止

	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	m_uiSprite->Begin();
	UiWindow::Draw(m_uiSprite, white, x, ty, w, h);
	m_uiSprite->End();
	m_textRenderer->Begin();
	m_textRenderer->DrawText(hit->title, x + 10.0f, ty + 6.0f, 16.0f, D2D1::ColorF(1.0f, 0.9f, 0.6f));
	m_textRenderer->DrawText(hit->desc, x + 10.0f, ty + 30.0f, 14.0f, D2D1::ColorF(0.95f, 0.8f, 0.6f));
	m_textRenderer->End();
}

void SceneManager::Update(float deltaTime)
{
	HoverLift::Tick(deltaTime);   // ホバーで浮く動きの時計
	if (m_fadeState == Fade::Out)
	{
		m_fadeAlpha += FADE_SPEED * deltaTime;
		if (m_fadeAlpha >= 1.0f) {
			m_fadeAlpha = 1.0f;
			if (m_onBlack) { m_onBlack(); m_onBlack = nullptr; }   // ← 真っ黒で書き換え
			DoChangeScene(m_pendingScene);
			m_fadeState = Fade::In;
		}
		return;   // 暗転中はシーンを止める
	}

	// 初勝利で初コアを得た"瞬間"に合成の説明（勝利画面を少し見せてから。中身は Tutorials.h）
	{
		auto& pd = PlayerDataManager::GetData();
		if (!m_tutorial.IsOpen() && pd.materials.count("core_slash") && pd.materials["core_slash"] > 0)
			ShowTutorialOnce(TutorialId::Craft);
	}

	m_tutorial.Update(deltaTime);   // 遅れて出すチュートリアルの待ち時間

	if (m_fadeState == Fade::In)
	{
		m_fadeAlpha -= FADE_SPEED * deltaTime;
		if (m_fadeAlpha <= 0.0f) { m_fadeAlpha = 0.0f; m_fadeState = Fade::None; }
	}

	m_uiTime += deltaTime;

	if (!m_tutorial.IsOpen() && !CamDemoActive()) Hint::Update(deltaTime);   // ページ型の説明中・カメラのデモ中は止める

	if (std::string up = m_deck.TakeUpgraded(); !up.empty()) { m_craftFxCard = up; m_craftFxTimer = CRAFT_FX_DURATION; }   // デッキで強化したカードの出現演出
	if (m_craftFxTimer > 0.0f)
	{
		m_craftFxTimer -= deltaTime;
		if (m_craftFxTimer <= 0.0f)
		{
			m_craftFxTimer = 0.0f;
			if (!m_craftFxQueue.empty()) PlayNextCardFx();   // 続きのカード
			else if (m_rest.Active()) FinishRest();             // 休憩でのクラフトは演出後に終了
		}
	}

	UiContext ui = Ui();
	m_deck.Update(ui, deltaTime);   // デッキ：スクロール（ホイールを読むのはイベントより先）

	m_event.Update(ui, deltaTime);   // イベント：揺れ・表示HP・変わる演出・カード選択の一覧（デッキ閲覧の後でホイールを読む）

	m_items.Update(ui, deltaTime, m_mDrag);   // 持ち物・合成の素材の一覧（ドラッグ中ははみ出しを戻さない）

	if (auto battle = dynamic_cast<BattleScene*>(m_currentScene))
		battle->SetUiBlocked(AnyModalOpen() && !m_tutorial.IsOpen());   // チュートリアルは自前でホバーを動かすので除く
	if (PanelPausesScene()) return;

	// チュートリアル中：カード強制ホバー＋戦闘はフリールックで駆動
	if (m_tutorial.IsOpen())
	{
		if (auto battle = dynamic_cast<BattleScene*>(m_currentScene))
		{
			battle->SetHoveredCard(m_tutorial.Current().hoverCard);
			battle->SetFreeLook(true);
			battle->SetForceHitmark(m_tutorial.Current().showHitmark);
			battle->FreeLookStep(m_uiInput, deltaTime);
		}
	}
	else if (auto battle = dynamic_cast<BattleScene*>(m_currentScene))
	{
		battle->SetFreeLook(false);
		battle->SetForceHitmark(false);
	}

	if (m_currentScene) m_currentScene->Update(deltaTime);   // 戦闘はfreeLook中は早期return
}

void SceneManager::DrawItemTooltip(const std::string& id, POINT mp)
{
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	ItemTooltip::Draw(m_uiSprite, m_textRenderer, white, id,
		(float)mp.x + 16.0f, (float)mp.y + 8.0f, m_screenWidth, m_screenHeight, false);
}

void SceneManager::FinishRest()
{
	m_rest.End();
	m_deck.Close();
	if (m_items.IsCraft()) m_items.Close();   // 合成だけ閉じる（今まで通り）
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
	bool curse = card && card->type == CardType::Curse;    // 呪いは暗い紫に光らせる
	float flash = 1.0f - t * 2.0f;
	if (flash > 0.0f)
		m_uiSprite->DrawSprite(white, 0, 0, (float)m_screenWidth, (float)m_screenHeight, 0.0f,
			curse ? XMFLOAT4(0.22f, 0.04f, 0.32f, flash * 0.85f) : XMFLOAT4(1, 1, 1, flash * 0.7f));

	// パーティクル（中心から放射）
	XMFLOAT4 pcol = curse ? XMFLOAT4(0.7f, 0.3f, 1.0f, 1.0f)
		: card ? CardVisual::GetCardColor(card->type) : XMFLOAT4(1, 1, 1, 1);
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

void SceneManager::GetBackBtnRect(float& x, float& y, float& w, float& h) const
{
	w = 160.0f; h = 50.0f;
	x = m_screenWidth - w - 20.0f;      // 右下（各画面で被りにくい位置）
	y = m_screenHeight - h - 16.0f;
}

bool SceneManager::BackBtnHit(POINT m) const
{
	float x, y, w, h; GetBackBtnRect(x, y, w, h);
	return m.x >= x && m.x <= x + w && m.y >= y && m.y <= y + h;
}

void SceneManager::DrawBackButton()
{
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	float x, y, w, h; GetBackBtnRect(x, y, w, h);
	float dy = BackBtnHit(m_uiInput.GetMousePos()) ? -3.0f : 0.0f;
	m_uiSprite->Begin();
	UiWindow::Button(m_uiSprite, white, x, y + dy, w, h, XMFLOAT4(0.35f, 0.30f, 0.30f, 0.95f));
	m_uiSprite->End();
	m_textRenderer->Begin();
	m_textRenderer->DrawTextCentered(L"戻る", x + w / 2.0f, y + dy, w, 22.0f, D2D1::ColorF(1, 1, 1), h);
	m_textRenderer->End();
}

void SceneManager::GetRelicRect(int i, float& x, float& y, float& w, float& h) const
{
	w = 48.0f; h = 34.0f;
	int per = RelicManager::BarPerRow(m_screenWidth);
	int col = i % per, row = i / per;
	x = 10.0f + col * (w + 4.0f);
	y = BAR_H + 4.0f + row * (h + 4.0f);   // はみ出したら下の行へ
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

	BattleScene* battle = (m_currentType == SceneType::Battle)
		? dynamic_cast<BattleScene*>(m_currentScene) : nullptr;
	auto& counters = PlayerDataManager::GetData().relicCounters;
	float pulse = 0.5f + 0.5f * sinf((float)GetTickCount64() * 0.004f);

	m_uiSprite->Begin();
	for (int i = 0; i < (int)relics.size(); i++)
	{
		float x, y, w, h; GetRelicRect(i, x, y, w, h);
		auto rd = RelicManager::Get(relics[i]);
		XMFLOAT4 frame = rd ? RelicManager::RarityColor(rd->rarity) : XMFLOAT4(0.55f, 0.55f, 0.55f, 1);

		bool spent = battle && battle->IsRelicSpent(relics[i]);
		bool aboutToFire = false;
		if (rd && rd->count > 0)
		{
			auto it = counters.find(relics[i]);
			if (it != counters.end() && it->second == rd->count - 1) aboutToFire = true;
		}

		if (!spent && aboutToFire)   // 発動直前：金色に脈動する縁
		{
			float p = 4.0f + 4.0f * pulse;
			m_uiSprite->DrawSprite(white, x - p, y - p, w + p * 2, h + p * 2, 0.0f,
				XMFLOAT4(1.0f, 0.95f, 0.4f, 0.45f + 0.4f * pulse));
		}
		// 窓の枠（レア度の色を薄く乗せる）。使い切りは暗く沈める
		XMFLOAT4 winTint = spent ? XMFLOAT4(0.2f, 0.2f, 0.22f, 1.0f)
			: XMFLOAT4(0.45f + 0.55f * frame.x, 0.45f + 0.55f * frame.y, 0.45f + 0.55f * frame.z, 1.0f);
		UiWindow::Draw(m_uiSprite, white, x, y, w, h, winTint);

		ID3D11ShaderResourceView* icon = (rd && !rd->icon.empty()) ? TextureManager::Get(rd->icon) : nullptr;
		if (icon)   // アイコン：影で輪郭を立たせ、レア度の色で染める
		{
			float is = h - 8.0f, ix = x + (w - is) / 2.0f, iy = y + 4.0f;
			m_uiSprite->DrawSprite(icon, ix + 1.5f, iy + 1.5f, is, is, 0.0f, XMFLOAT4(0, 0, 0, 0.65f));
			m_uiSprite->DrawSprite(icon, ix, iy, is, is, 0.0f,
				spent ? XMFLOAT4(0.22f, 0.22f, 0.24f, 0.8f) : frame);
		}

		float flash = RelicManager::FlashAmount(relics[i]);
		if (flash > 0.0f)
		{
			float e = 8.0f * flash;
			m_uiSprite->DrawSprite(white, x - e, y - e, w + e * 2, h + e * 2, 0.0f,
				XMFLOAT4(1.0f, 1.0f, 0.7f, 0.75f * flash));   // 金色の膨張フラッシュ
		}
	}
	m_uiSprite->End();

	m_textRenderer->Begin();
	for (int i = 0; i < (int)relics.size(); i++)
	{
		float x, y, w, h; GetRelicRect(i, x, y, w, h);
		auto d = RelicManager::Get(relics[i]);
		std::wstring lb = d ? ToWString(d->name).substr(0, 2) : L"?";
		bool spentT = (m_currentType == SceneType::Battle)
			&& dynamic_cast<BattleScene*>(m_currentScene)
			&& dynamic_cast<BattleScene*>(m_currentScene)->IsRelicSpent(relics[i]);
		D2D1::ColorF nameCol = spentT ? D2D1::ColorF(0.45f, 0.45f, 0.45f) : D2D1::ColorF(1, 1, 1);
		bool hasIcon = d && !d->icon.empty() && TextureManager::Get(d->icon);
		if (!hasIcon) m_textRenderer->DrawText(lb.c_str(), x + 6.0f, y + 8.0f, 18.0f, nameCol);
		if (d && d->count > 0)
		{
			auto& rc = PlayerDataManager::GetData().relicCounters;
			auto it = rc.find(relics[i]);
			int cur = (it != rc.end()) ? it->second : 0;
			wchar_t cnt[16]; swprintf_s(cnt, L"%d/%d", cur, d->count);
			const float fs = 12.0f;   // 枠の中の右下に収める（枠の下に出すと、下の段のレリックや左上のバフ欄と重なる）
			D2D1_SIZE_F sz = m_textRenderer->Measure(cnt, fs);
			m_textRenderer->DrawOutlinedText(cnt, x + w - sz.width - 3.0f, y + h - sz.height - 1.0f, fs,
				D2D1::ColorF(1.0f, 0.9f, 0.4f), D2D1::ColorF(0, 0, 0), 1.0f);
		}
	}

	// 発動したレリック名を画面中央付近にフワッと表示（複数は縦に積む）
	int flashSlot = 0;
	for (int i = 0; i < (int)relics.size(); i++)
	{
		auto dd = RelicManager::Get(relics[i]);
		float fl = RelicManager::FlashAmount(relics[i]);
		if (fl <= 0.0f || !dd) continue;
		std::wstring full = ToWString(dd->name);
		float rise = (1.0f - fl) * 24.0f;                        // 消えながら上へ
		float cx = m_screenWidth * 0.5f - full.size() * 11.0f;   // ざっくり中央寄せ
		float cy = m_screenHeight * 0.5f - flashSlot * 28.0f - rise;
		m_textRenderer->DrawOutlinedText(full.c_str(), cx, cy, 22.0f,
			D2D1::ColorF(1.0f, 1.0f, 0.6f, fl),
			D2D1::ColorF(0, 0, 0, fl), 1.5f);
		flashSlot++;
	}

	m_textRenderer->End();

	std::string rid = HoveredRelic(m_uiInput.GetMousePos());
	if (!rid.empty()) DrawItemTooltip(rid, m_uiInput.GetMousePos());
}

void SceneManager::PlayNextCardFx()
{
	m_craftFxCard = m_craftFxQueue.front();
	m_craftFxQueue.erase(m_craftFxQueue.begin());
	m_craftFxTimer = CRAFT_FX_DURATION;
	Se::Play("debuff.wav");
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

// カードの説明を残せない画面（説明より手前に出るもの）
bool SceneManager::TooltipBlocked() const
{
	return m_glossary.IsOpen() || m_settings.IsOpen() || m_tutorial.IsOpen() || CardConfirm::Active()
		|| m_craftFxTimer > 0.0f || m_event.Animating();   // 強化・合成・イベントのカード演出中も（演出後に閉じた画面の説明を残さない）
}

// シーンの Update を止める窓（チュートリアル・設定などは止めない：戦闘はフリールックで動かす／タイトルは裏で動く）
bool SceneManager::PanelPausesScene() const
{
	return m_deck.IsOpen() || m_items.IsOpen() || m_rest.IsShown() || m_map.IsOpen() || m_event.IsOpen() || m_craftFxTimer > 0.0f;
}

bool SceneManager::AnyModalOpen() const
{
	return m_tutorial.IsOpen() || m_deck.IsOpen() || m_items.IsOpen()
		|| m_map.IsOpen() || m_rest.IsShown() || m_event.IsOpen() || m_settings.IsOpen() || m_glossary.IsOpen() || CardConfirm::Active()
		|| BossRelicPickOpen();
}

bool SceneManager::BossRelicPickOpen() const
{
	return m_currentType == SceneType::Field && !PlayerDataManager::GetData().bossRelicChoices.empty();
}