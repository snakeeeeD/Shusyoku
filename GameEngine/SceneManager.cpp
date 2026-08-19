#include "SceneManager.h"
#include "EffectDataBase.h"
#include "EnemyDataBase.h"
#include "CardDataBase.h"
#include "CardVisual.h"
#include "CardTooltip.h"
#include "PlayerDataManager.h"
#include "EncounterDataBase.h"
#include "TerrainDataBase.h"
#include "MaterialDataBase.h"
#include "RelicManager.h"
#include "UiWindow.h"
#include "Audio.h"
#include "Settings.h"

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
	case FieldNodeType::Treasure: return XMFLOAT4(0.95f, 0.78f, 0.2f, 1.0f);
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
	case FieldNodeType::Treasure: return L"Tr";
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

	TextureManager::Load("white", L"Assets/Test/White.png");

	// パーティクル
	{
		TextureManager::Load("particle", L"Assets/Particles/circle.png");
		TextureManager::Load("particle_poison", L"Assets/Particles/circle_01.png");
		TextureManager::Load("fire", L"Assets/Particles/fire.png");
		TextureManager::Load("magic", L"Assets/Particles/magic.png");
		TextureManager::Load("smoke", L"Assets/Particles/smoke.png");
		TextureManager::Load("star", L"Assets/Particles/star.png");
		TextureManager::Load("skull", L"Assets/Particles/skull.png");
		TextureManager::Load("crosshair", L"Assets/Particles/crosshair.png");
	}

	// フィールド
	{
		TextureManager::Load("title", L"Assets/Test/Title.png");
		TextureManager::Load("battle_bg", L"Assets/Field/GrassField.jpg");
		TextureManager::Load("map_bg", L"Assets/Field/Map.jpg");
		TextureManager::Load("cardSelect_bg", L"Assets/Field/CardSelect.jpeg");
	}

	// プレイヤー
	{
		TextureManager::Load("player", L"Assets/Player/yuusya_game.png");
		TextureManager::Load("kakashi", L"Assets/Player/kakashi.png");
	}

	// エネミー
	{
		TextureManager::Load("enemy_slime", L"Assets/Enemy/slime.png");
		TextureManager::Load("enemy_zako", L"Assets/Enemy/zako.png");
		TextureManager::Load("enemy_goblin", L"Assets/Enemy/goblin.png");
		TextureManager::Load("enemy_orc", L"Assets/Enemy/orc.png");
		TextureManager::Load("enemy_fire", L"Assets/Enemy/fireEnemy.png");
		TextureManager::Load("enemy_bug", L"Assets/Enemy/bug.png");
		TextureManager::Load("enemy_spore", L"Assets/Enemy/spore.png");
		TextureManager::Load("enemy_hound", L"Assets/Enemy/hound.png");
		TextureManager::Load("enemy_dragon_red", L"Assets/Enemy/dragon_red.png");
		TextureManager::Load("enemy_archer", L"Assets/Enemy/archer.png");
		TextureManager::Load("enemy_reaper", L"Assets/Enemy/reaper.png");
		TextureManager::Load("enemy_tentacle", L"Assets/Enemy/cyaegha.png");
		TextureManager::Load("enemy_bear", L"Assets/Enemy/bear.png");
		TextureManager::Load("enemy_slimeboss", L"Assets/Enemy/shoggoth.png");
		TextureManager::Load("enemy_golem", L"Assets/Enemy/golem.png");
		TextureManager::Load("enemy_obake", L"Assets/Enemy/obake.png");
		TextureManager::Load("enemy_mimic", L"Assets/Enemy/mimic.png");
	}

	// UI
	{
		TextureManager::Load("ui_map", L"Assets/UI/ui_map.png");
		TextureManager::Load("ui_items", L"Assets/UI/ui_items.png");
		TextureManager::Load("ui_deck", L"Assets/UI/ui_deck.png");
		TextureManager::Load("ui_turnend", L"Assets/UI/ui_turnend.png");
		TextureManager::Load("ui_draw", L"Assets/UI/ui_draw.png");
		TextureManager::Load("ui_discard", L"Assets/UI/ui_discard.png");
		TextureManager::Load("ui_exhaust", L"Assets/UI/ui_exhaust.png");
		TextureManager::Load("ui_arrowhead", L"Assets/UI/ui_arrowhead.png");
		TextureManager::Load("ui_window", L"Assets/UI/ui_window.png");
		TextureManager::Load("ui_banner", L"Assets/UI/ui_banner.png");
		TextureManager::Load("ui_card", L"Assets/UI/ui_card.png");
		TextureManager::Load("ui_cost", L"Assets/UI/ui_cost.png");
		TextureManager::Load("ui_hazard", L"Assets/UI/ui_hazard.png");
		TextureManager::Load("ui_threat", L"Assets/UI/ui_threat.png");
		TextureManager::Load("mat_core", L"Assets/UI/mat_core.png");
		TextureManager::Load("mat_material", L"Assets/UI/mat_material.png");
		TextureManager::Load("ui_reach", L"Assets/UI/ui_reach.png");
		TextureManager::Load("ui_hitmark", L"Assets/UI/ui_hitmark.png");
		TextureManager::Load("ui_hitring", L"Assets/UI/ui_hitring.png");

		TextureManager::Load("node_battle", L"Assets/UI/node_battle.png");
		TextureManager::Load("node_rest", L"Assets/UI/node_rest.png");
		TextureManager::Load("node_shop", L"Assets/UI/node_shop.png");
		TextureManager::Load("node_elite", L"Assets/UI/node_elite.png");
		TextureManager::Load("node_boss", L"Assets/UI/node_boss.png");
		TextureManager::Load("node_event", L"Assets/UI/node_event.png");
		TextureManager::Load("node_treasure", L"Assets/UI/node_treasure.png");
		TextureManager::Load("node_start", L"Assets/UI/node_start.png");
		TextureManager::Load("node_player", L"Assets/UI/node_player.png");
		TextureManager::Load("ui_node", L"Assets/UI/ui_node.png");

		// バフデバフアイコン
		{
			TextureManager::Load("icon_attack", L"Assets/UI/icon_attack.png");
			TextureManager::Load("icon_block", L"Assets/UI/icon_block.png");
			TextureManager::Load("icon_buff", L"Assets/UI/icon_buff.png");
			TextureManager::Load("icon_debuff", L"Assets/UI/icon_debuff.png");
			TextureManager::Load("icon_move", L"Assets/UI/icon_move.png");
<<<<<<< HEAD
			TextureManager::Load("icon_summon", L"Assets/UI/icon_summon.png");
=======
>>>>>>> 35c672055e94719f7657b4dc1ec63ef5eae2eba0
			TextureManager::Load("buff_attackup", L"Assets/UI/buff_attackup.png");
			TextureManager::Load("buff_defenseup", L"Assets/UI/buff_defenseup.png");
			TextureManager::Load("buff_weak", L"Assets/UI/buff_weak.png");
			TextureManager::Load("buff_vulnerable", L"Assets/UI/buff_vulnerable.png");
			TextureManager::Load("buff_poison", L"Assets/UI/buff_poison.png");
			TextureManager::Load("buff_burn", L"Assets/UI/buff_burn.png");
			TextureManager::Load("buff_attackup", L"Assets/UI/buff_attackup.png");
			TextureManager::Load("buff_rangeup", L"Assets/UI/buff_rangeup.png");
			TextureManager::Load("buff_defenseup", L"Assets/UI/buff_defenseup.png");
			TextureManager::Load("buff_barricade", L"Assets/UI/buff_barricade.png");
			TextureManager::Load("buff_thorns", L"Assets/UI/buff_thorns.png");
			TextureManager::Load("buff_momentum", L"Assets/UI/buff_momentum.png");
			TextureManager::Load("buff_moveup", L"Assets/UI/buff_moveup.png");
			TextureManager::Load("buff_charge", L"Assets/UI/buff_charge.png");
			TextureManager::Load("buff_hitandrun", L"Assets/UI/buff_hitandrun.png");
			TextureManager::Load("buff_reposition", L"Assets/UI/buff_reposition.png");
			TextureManager::Load("buff_attackdown", L"Assets/UI/buff_attackdown.png");
			TextureManager::Load("buff_weak", L"Assets/UI/buff_weak.png");
			TextureManager::Load("buff_defensedown", L"Assets/UI/buff_defensedown.png");
			TextureManager::Load("buff_frail", L"Assets/UI/buff_frail.png");
			TextureManager::Load("buff_vulnerable", L"Assets/UI/buff_vulnerable.png");
			TextureManager::Load("buff_root", L"Assets/UI/buff_root.png");
			TextureManager::Load("buff_slow", L"Assets/UI/buff_slow.png");
			TextureManager::Load("buff_burn", L"Assets/UI/buff_burn.png");
			TextureManager::Load("buff_poison", L"Assets/UI/buff_poison.png");
			TextureManager::Load("buff_attackupturn", L"Assets/UI/buff_attackupturn.png");
			TextureManager::Load("buff_attackgrowth", L"Assets/UI/buff_attackgrowth.png");
			TextureManager::Load("buff_frenzy", L"Assets/UI/buff_frenzy.png");
			TextureManager::Load("buff_noxiousfumes", L"Assets/UI/buff_noxiousfumes.png");
			TextureManager::Load("buff_toxicrhythm", L"Assets/UI/buff_toxicrhythm.png");
			TextureManager::Load("buff_movelock", L"Assets/UI/buff_movelock.png");
			TextureManager::Load("buff_riposte", L"Assets/UI/buff_riposte.png");
			TextureManager::Load("buff_knifepower", L"Assets/UI/buff_knifepower.png");
			TextureManager::Load("buff_knifethrow", L"Assets/UI/buff_knifethrow.png");
			TextureManager::Load("buff_cardblock", L"Assets/UI/buff_cardblock.png");
			TextureManager::Load("buff_knifegen", L"Assets/UI/buff_knifegen.png");
			TextureManager::Load("buff_laststand", L"Assets/UI/buff_laststand.png");
			TextureManager::Load("buff_deepstand", L"Assets/UI/buff_deepstand.png");
			TextureManager::Load("buff_embertrail", L"Assets/UI/buff_embertrail.png");
		}
	}

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
				m_restOpen = true; m_restActive = true;
				Audio::PlayBGM("Assets/Sound/bgm/Rest.mp3");
				auto& pd = PlayerDataManager::GetData();
				if (!pd.tutorialRest)
				{
					pd.tutorialRest = true; PlayerDataManager::Save();
					ShowTutorial({
						{ L"休憩マス。3つのうち1つを選べる",          510, 240, 260, 244 },  // ← 3ボタン全体
						{ L"Heal … HPを回復する",                     510, 250, 260,  60 },  // ← ボタン0
						{ L"Upgrade … 手持ちカード1枚を強化する",     510, 330, 260,  60 },  // ← ボタン1
						{ L"Craft … コア＋素材で新しいカードを作る",   510, 410, 260,  60 },  // ← ボタン2
						});
				}
				};
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
			{ L"歩数(Steps)が尽きて進むと敵が強くなる",                                 130,   5, 110,  28, -1, true },  // ← stepsTip=true
			{ L"左上のレリック\n常時発動する特殊効果。今回は戦闘に勝つとHP回復",		8.0f, (float)BAR_H + 2.0f, 52.0f, 38.0f },
			{ L"レリックはエリート撃破・\nイベント・ショップで入手できる",				8.0f, (float)BAR_H + 2.0f, 52.0f, 38.0f },
			{ L"右端のボスを倒すと次の層へ",                       1050, 130, 120, 540 },
					});
	}
	if (type == SceneType::Battle && !pd.tutorialBattle)
	{
		pd.tutorialBattle = true; PlayerDataManager::Save();
		ShowTutorialDelayed({
			{ L"手札のカードをクリックし\n対象を選んで使う",         340, 580, 600, 220 },
			{ L"一番左は移動カード\nマスを移動して敵の攻撃を避ける", 340, 630, 130, 170, 0 },
			{ L"2番目はアタック\n隣接する敵にダメージを与える",       340, 630, 130, 170, 1 },
			{ L"3番目はブロック\nそのターン受けるダメージを防ぐ",     340, 630, 130, 170, 2 },
			{ L"左上のエナジーでカードのコストを払う\n毎ターン回復する", 15, 185,  70,  70 },
					{ L"ターンエンドで相手の番\nここを押して自分の番を終える",
			  (float)m_screenWidth - 168.0f, (float)m_screenHeight - 66.0f, 156.0f, 52.0f },
			{ L"HPが0になると敗北",                                   20,  85, 220,  90 },
			{ L"カメラ操作\nマウスホイール：ズーム / 右クリックドラッグ：カメラ移動\nホイールをクリック：自分中心にリセット", 170, 130, 940, 540 },
			{ L"敵をクリック（右上の敵ウィンドウでもOK）で\nその敵の攻撃範囲が表示される", 1028, 46, 246, 200 },
						{ L"頭上に赤い警告が出ている敵は\n今の位置に攻撃が当たる。移動して避けよう", 170, 130, 940, 540, -1, false, true },
			}, 1.2f);
	}

	if (type == SceneType::CardSelect && !pd.tutorialCardSelect)
	{
		pd.tutorialCardSelect = true; PlayerDataManager::Save();
		ShowTutorial({
					{ L"戦闘の報酬カード。1枚選ぶとデッキに加わる", 376, 224, 490, 224 }, // ← カード3枚を明るく
			{ L"欲しいカードが無ければスキップもできる",     566, 596, 148,  52 },  // ← スキップボタン
			});
	}
	if (type == SceneType::Shop && !pd.tutorialShop)
	{
		pd.tutorialShop = true; PlayerDataManager::Save();
		ShowTutorial({
			{ L"ショップ。カード・素材・レリックをクリックで購入", 180, 120, 920, 470 },
			{ L"購入にはゴールド（G）が必要",                     704,   4,  82,  30 },
			{ L"左下：カードをデッキから削除できる（有料・1回）",  36, 646, 228,  52 },
			{ L"SELL：いらない素材を売ってゴールドにできる",      276, 646, 168,  52 },  // ← 追加（売却ボタン）
			{ L"終わったら LEAVE でフィールドへ戻る",            556, 646, 168,  52 },
			});
	}
}

void SceneManager::Draw()
{
	if (m_currentScene) m_currentScene->Draw();
	if (m_invOpen) DrawInventory();
	if (m_mapOpen) DrawMap();
	if (m_currentType != SceneType::Title) DrawOverlay();
	if (m_eventOpen && !m_mapOpen && !m_invOpen && !m_deckOpen)
	{
		if (m_eventPickType.empty()) DrawEvent();
		else if (m_eventPickType == "transformRelic") DrawEventRelicPicker();
		else DrawEventPicker();
	}
	if (m_craftOpen) DrawCraft();
	if (m_restOpen && !m_mapOpen && !m_invOpen && !m_deckOpen && !m_craftOpen) DrawRest();
	if (m_craftFxTimer > 0.0f) DrawCraftFx();  
	if (m_currentType != SceneType::Title) { DrawRelicBar(); DrawBarTips(); }
	if (m_settingsOpen) DrawSettings();

	if (m_tutorialOpen) DrawTutorial();

	// ドロップ説明ウィンドウは暗幕の上に出す（勝利画面のホバー）
	if (auto b = dynamic_cast<BattleScene*>(m_currentScene)) b->DrawDropTooltipTop();

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
	m_uiSprite->DrawSprite(TextureManager::Get("ui_items"), m_screenWidth - 470.0f, 5.0f, 90.0f, 30.0f, 0.0f, XMFLOAT4(1, 1, 1, 1));

	m_uiSprite->DrawSprite(TextureManager::Get("ui_deck"), btnX, 5.0f, 100.0f, 30.0f, 0.0f, XMFLOAT4(1, 1, 1, 1));             // デッキボタン
	m_uiSprite->DrawSprite(TextureManager::Get("particle"),
		btnX + 100.0f - 24.0f, 5.0f + 30.0f - 22.0f, 24.0f, 24.0f, 0.0f, XMFLOAT4(0.10f, 0.09f, 0.13f, 1.0f));

	if (m_currentType != SceneType::Field)
		m_uiSprite->DrawSprite(TextureManager::Get("ui_map"), m_screenWidth - 680.0f, 5.0f, 90.0f, 30.0f, 0.0f, XMFLOAT4(1, 1, 1, 1));   // Mapボタン（フィールドは自身がマップなので非表示）

	{
		SettingsUI u = SettingsLayout();
		bool hov = u.gear.has(m_uiInput.GetMousePos());
		UiWindow::Button(m_uiSprite, white, u.gear.x, u.gear.y, u.gear.w, u.gear.h,
			hov ? XMFLOAT4(0.4f, 0.4f, 0.5f, 1) : XMFLOAT4(0.22f, 0.22f, 0.3f, 1));
	}

	if (m_deckOpen)
	{
		m_uiSprite->DrawSprite(white, 0.0f, 0.0f, (float)m_screenWidth, (float)m_screenHeight,
			0.0f, XMFLOAT4(0.0f, 0.0f, 0.0f, 0.8f));          // 暗幕
		DrawDeckCards(false);

		// 強化後表示トグル
		float ubX = 20.0f, ubY = m_screenHeight - 46.0f, ubW = 190.0f, ubH = 34.0f;
		UiWindow::Button(m_uiSprite, white, ubX, ubY, ubW, ubH,
			m_deckShowUpgrade ? XMFLOAT4(0.30f, 0.55f, 0.30f, 1.0f)
			: XMFLOAT4(0.30f, 0.30f, 0.35f, 1.0f));
	}

	m_uiSprite->End();

	// === テキスト ===
	m_textRenderer->Begin();
	{
		wchar_t t[16]; swprintf_s(t, L"%d", (int)PlayerDataManager::GetData().deck.size());
		m_textRenderer->DrawOutlinedText(t, btnX + 100.0f - 18.0f, 5.0f + 30.0f - 20.0f, 17.0f,
			D2D1::ColorF(1, 1, 1), D2D1::ColorF(0, 0, 0), 2.0f);
	}

	{ SettingsUI u = SettingsLayout(); m_textRenderer->DrawText(L"設定", u.gear.x + 6.0f, u.gear.y + 7.0f, 16.0f, D2D1::ColorF(1, 1, 1)); }


	if (m_deckOpen)
	{
		DrawDeckCards(true);
		m_textRenderer->DrawText(m_deckShowUpgrade ? L"強化後表示: ON" : L"強化後表示: OFF",
			36.0f, m_screenHeight - 38.0f, 18.0f, D2D1::ColorF(1, 1, 1));
	}
	
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

	m_textRenderer->End();

	{
		POINT mp = m_uiInput.GetMousePos();
		const wchar_t* tip = nullptr;
		if (mp.y >= 5 && mp.y <= 35) {
			if (m_currentType != SceneType::Field && mp.x >= m_screenWidth - 680 && mp.x <= m_screenWidth - 590) tip = L"マップを開く";
			else if (mp.x >= m_screenWidth - 470 && mp.x <= m_screenWidth - 380) tip = L"アイテムを見る";
			else if (mp.x >= btnX && mp.x <= btnX + 100)                         tip = L"デッキを見る";
		}
		if (tip) {
			float tw = 150.0f, th = 30.0f, tx = (float)mp.x + 14.0f, ty = (float)mp.y + 18.0f;
			m_uiSprite->Begin();
			UiWindow::Draw(m_uiSprite, TextureManager::Get("white"), tx - 6, ty - 6, tw + 12, th + 12);
			m_uiSprite->End();
			m_textRenderer->Begin();
			m_textRenderer->DrawText(tip, tx + 10.0f, ty + 6.0f, 16.0f, D2D1::ColorF(1, 1, 1));
			m_textRenderer->End();
		}
	}

	if (m_deckOpen) DrawDeckPreview();   // ← 全グリッド描画の後＝最前面

	// デッキのキーワード解説（パス外で描く）
	if (m_deckOpen && m_deckPreviewIdx < 0)
	{
		int hk = GetDeckCardAt(m_uiInput.GetMousePos());
		auto vis = VisibleDeckIndices();
		for (int slot = 0; hk >= 0 && slot < (int)vis.size(); slot++)
			if (vis[slot] == hk)
			{
				const CardData* d = CardDataBase::Get(PlayerDataManager::GetData().deck[hk]);
				float bx, by; GetDeckCardBase(slot, bx, by);
				float rx, ry, rw, rh; CardVisual::GetRect(bx, by - 10.0f, DECK_SCALE, rx, ry, rw, rh);
				CardTooltip::Draw(m_uiSprite, m_textRenderer, TextureManager::Get("white"), d,
					rx + rw / 2.0f, ry, rw, rh, m_screenWidth, m_screenHeight);
				break;
			}
	}
}

static bool CanUpgrade(const std::string& id);   // 前方宣言（定義は下にある）

void SceneManager::DrawDeckCards(bool textPass)
{
	auto& deck = PlayerDataManager::GetData().deck;
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	auto vis = VisibleDeckIndices();

	int hovIdx = (m_deckPreviewIdx >= 0) ? -1 : GetDeckCardAt(m_uiInput.GetMousePos()); // ホバー中のデッキindex

	if (!textPass && hovIdx != m_deckHoveredPrev)           // SEは1回だけ（spriteパス）
	{
		if (hovIdx >= 0) Audio::PlaySE("Assets/Sound/se/hover.mp3");
		m_deckHoveredPrev = hovIdx;
	}

	for (int slot = 0; slot < (int)vis.size(); slot++)
	{
		std::string cid = deck[vis[slot]];
		if (m_deckShowUpgrade && CanUpgrade(cid)) cid += "+";   // ← トグルONなら+版
		const CardData* d = CardDataBase::Get(cid);
		if (!d) continue;
		float bx, by; GetDeckCardBase(slot, bx, by);
		if (vis[slot] == hovIdx) by -= 10.0f;              // ← ホバーで浮く

		if (!textPass)
		{
			XMFLOAT4 col = (m_deckRemoveMode && vis[slot] == hovIdx)
				? XMFLOAT4(0.6f, 0.15f, 0.15f, 0.9f)   // 削除モードはホバー中のカードだけ赤
				: CardVisual::GetCardColor(d->type);
			CardVisual::DrawBase(m_uiSprite, white, bx, by, DECK_SCALE, 0.0f, col, d, m_uiTime);
		}
		else
			CardVisual::DrawTexts(m_textRenderer, d, nullptr, bx, by, DECK_SCALE, 0.0f, 1.0f);
	}
}

void SceneManager::DrawDeckPreview()
{
	auto& deck = PlayerDataManager::GetData().deck;
	if (m_deckPreviewIdx < 0 || m_deckPreviewIdx >= (int)deck.size()) return;

	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	const float PS = 2.0f;
	float cw = CardVisual::CARD_W * PS, chh = CardVisual::CARD_H * PS;
	std::string baseId = deck[m_deckPreviewIdx];
	bool canUp = CanUpgrade(baseId);

	// 休憩：クリックで 現在→強化後（横並び）＋「強化する」
	if (m_deckUpgradeMode && canUp)
	{
		const CardData* cur = CardDataBase::Get(baseId);
		const CardData* upd = CardDataBase::Get(baseId + "+");
		float gap = 150.0f, x0 = m_screenWidth / 2.0f - (cw * 2 + gap) / 2.0f, y0 = m_screenHeight / 2.0f - chh / 2.0f;
		float bW = 200.0f, bH = 44.0f;
		float bX = m_screenWidth / 2.0f - bW / 2.0f, bY = y0 + chh - 30.0f;
		m_uiSprite->Begin();
		m_uiSprite->DrawSprite(white, 0, 0, (float)m_screenWidth, (float)m_screenHeight, 0.0f, XMFLOAT4(0, 0, 0, 0.7f));
		DrawBigCard(cur, x0, y0, PS, false); DrawBigCard(upd, x0 + cw + gap, y0, PS, false);
		// 強化する ボタン（カード下寄り、ホバーで浮く＋SE）
		POINT mp = m_uiInput.GetMousePos();
		bool bHov = (mp.x >= bX && mp.x <= bX + bW && mp.y >= bY && mp.y <= bY + bH);
		if (bHov && !m_upgradeBtnHov) Audio::PlaySE("Assets/Sound/se/hover.mp3");
		m_upgradeBtnHov = bHov;
		float bDy = bHov ? -4.0f : 0.0f;
		UiWindow::Button(m_uiSprite, white, bX, bY + bDy, bW, bH,
			bHov ? XMFLOAT4(0.70f, 0.58f, 0.28f, 1.0f) : XMFLOAT4(0.55f, 0.45f, 0.20f, 1.0f));
		m_uiSprite->End();

		m_textRenderer->Begin();
		DrawBigCard(cur, x0, y0, PS, true); DrawBigCard(upd, x0 + cw + gap, y0, PS, true);
		m_textRenderer->DrawOutlinedText(L"→",
			x0 + cw + gap / 2.0f - 70.0f, y0 + chh / 2.0f - 100.0f, 56.0f,
			D2D1::ColorF(1.0f, 0.9f, 0.4f), D2D1::ColorF(0, 0, 0), 3.0f);
		m_textRenderer->DrawOutlinedText(L"現在", x0, y0 - 100.0f, 30.0f,
			D2D1::ColorF(0.85f, 0.85f, 0.85f), D2D1::ColorF(0, 0, 0), 2.5f);
		m_textRenderer->DrawOutlinedText(L"強化後", x0 + cw + gap, y0 - 100.0f, 30.0f,
			D2D1::ColorF(1.0f, 0.9f, 0.4f), D2D1::ColorF(0, 0, 0), 2.5f);
		m_textRenderer->DrawText(L"強化する", bX + 55.0f, bY + bDy + 11.0f, 22.0f, D2D1::ColorF(1, 0.95f, 0.7f));
		m_textRenderer->End();
		return;
	}

	// 通常デッキ：単体拡大（ボタンなし）
	std::string showId = (m_previewShowUpgrade && canUp) ? baseId + "+" : baseId;
	const CardData* d = CardDataBase::Get(showId);
	if (!d) return;
	float x0 = m_screenWidth / 2.0f - cw / 2.0f, y0 = m_screenHeight / 2.0f - chh / 2.0f;
	m_uiSprite->Begin();
	m_uiSprite->DrawSprite(white, 0, (float)BAR_H, (float)m_screenWidth, (float)m_screenHeight - (float)BAR_H, 0.0f, XMFLOAT4(0, 0, 0, 0.7f));
	DrawBigCard(d, x0, y0, PS, false);
	m_uiSprite->End();
	m_textRenderer->Begin();
	DrawBigCard(d, x0, y0, PS, true);
	m_textRenderer->End();
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
	{
		auto ids = RelicManager::AllIds();
		std::sort(ids.begin(), ids.end());     // アルファベット順
		for (auto& id : ids)
			if (ImGui::Button(id.c_str()))
				PlayerDataManager::AddRelic(id);
	}

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

	if (m_tutorialDelay > 0.0f) return;   // チュートリアル表示待ち中は入力を止める（勝利連打で飛ぶのを防ぐ）

	if (m_craftFxTimer > 0.0f) return;           // 演出中は入力停止

	if (m_currentType != SceneType::Title)
	{
		POINT m = m_uiInput.GetMousePos();
		bool click = m_uiInput.GetMouseButtonTrigger(0);
		float btnX = m_screenWidth - 220.0f, btnY = 5.0f, btnW = 100.0f, btnH = 30.0f;
		bool onDeckBtn = click && m.x >= btnX && m.x <= btnX + btnW
			&& m.y >= btnY && m.y <= btnY + btnH;

		SettingsUI su = SettingsLayout();
		// ギアで開閉
		if (click && su.gear.has(m)) { Audio::PlaySE("Assets/Sound/se/click.mp3"); m_settingsOpen = !m_settingsOpen; return; }
		if (m_settingsOpen)
		{
			auto& s = Settings::Get();
			if (m_uiInput.GetMouseButtonTrigger(0))
			{
				URect bt = su.bgmTrack; bt.y -= 8; bt.h += 16;   // 掴みやすく
				URect st = su.seTrack;  st.y -= 8; st.h += 16;
				if (bt.has(m)) m_dragSlider = 0; else if (st.has(m)) m_dragSlider = 1;
			}
			if (m_uiInput.GetMouseButtonRelease(0)) { if (m_dragSlider != -1) Settings::Save(); m_dragSlider = -1; }
			if (m_dragSlider == 0 || m_dragSlider == 1)
			{
				const URect& t = (m_dragSlider == 0) ? su.bgmTrack : su.seTrack;
				float v = (m.x - t.x) / t.w; if (v < 0) v = 0; if (v > 1) v = 1;
				if (m_dragSlider == 0) { s.bgmVolume = v; Audio::SetBgmVolume(v); }
				else { s.seVolume = v;  Audio::SetSeMasterVolume(v); }
			}
			if (click)
			{
				if (su.disp.has(m))
				{
					DisplayMode nx = (s.displayMode == DisplayMode::Borderless) ? DisplayMode::Windowed : DisplayMode::Borderless;
					SetDisplayMode(nx); Audio::PlaySE("Assets/Sound/se/click.mp3");
				}
				else if (su.shake.has(m)) { s.screenShake = !s.screenShake; Settings::Save(); Audio::PlaySE("Assets/Sound/se/click.mp3"); }
				else if (su.close.has(m) || !su.panel.has(m)) { m_settingsOpen = false; m_dragSlider = -1; Settings::Save(); Audio::PlaySE("Assets/Sound/se/click.mp3"); }
			}
			return;   // 設定中は他のバー操作を止める
		}

		// Map（フィールド以外で開ける・クリックで閉じる）
		if (m_mapOpen) { if (click) m_mapOpen = false; return; }
		{
			float mapX = m_screenWidth - 680.0f;
			if (click && m_currentType != SceneType::Field && !m_deckOpen && !m_craftOpen && !m_invOpen
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
				// 強化後表示トグル（デッキ閲覧のみ・拡大中でない時）
				float ubX = 20.0f, ubY = m_screenHeight - 46.0f, ubW = 190.0f, ubH = 34.0f;
				if (m_deckPreviewIdx < 0 &&
					m.x >= ubX && m.x <= ubX + ubW && m.y >= ubY && m.y <= ubY + ubH)
				{
					m_deckShowUpgrade = !m_deckShowUpgrade; Audio::PlaySE("Assets/Sound/se/click.mp3"); return;
				}

				if (m_deckUpgradeMode)
				{
					if (m_deckPreviewIdx >= 0)   // 拡大中：強化ボタン or 閉じる
					{
						std::string baseId = PlayerDataManager::GetData().deck[m_deckPreviewIdx];
						if (CanUpgrade(baseId))
						{
							float cw = CardVisual::CARD_W * 2.0f, chh = CardVisual::CARD_H * 2.0f;
							float y0 = m_screenHeight / 2.0f - chh / 2.0f, bW = 200.0f, bH = 40.0f;
							float bX = m_screenWidth / 2.0f - bW / 2.0f, bY = y0 + chh - 30.0f;
							if (m.x >= bX && m.x <= bX + bW && m.y >= bY && m.y <= bY + bH)
							{
								std::string before = baseId;
								PlayerDataManager::UpgradeCard(m_deckPreviewIdx);
								const std::string& after = PlayerDataManager::GetData().deck[m_deckPreviewIdx];
								if (after != before) { m_craftFxCard = after; m_craftFxTimer = CRAFT_FX_DURATION; }
								m_deckPreviewIdx = -1;
								return;
							}
						}
						m_deckPreviewIdx = -1; return;   // ボタン外で閉じる
					}
					int idx = GetDeckCardAt(m);
					if (idx >= 0) { m_deckPreviewIdx = idx; Audio::PlaySE("Assets/Sound/se/click.mp3"); return; }   // クリックで拡大
					if (m_restActive) { m_deckOpen = false; m_deckUpgradeMode = false; m_restOpen = true; return; }
					m_deckUpgradeMode = false; return;
				}

				// 通常デッキ：拡大中はクリックで閉じる（ボタンなし）
				if (m_deckPreviewIdx >= 0) { m_deckPreviewIdx = -1; return; }
				int idx = GetDeckCardAt(m);
				if (idx >= 0) { m_deckPreviewIdx = idx; m_previewShowUpgrade = m_deckShowUpgrade; Audio::PlaySE("Assets/Sound/se/click.mp3"); return; }
				m_deckOpen = false;
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
				Audio::PlaySE("Assets/Sound/se/click.mp3");
				if (m_eventPending == EventPending::Battle)
				{
					m_eventPending = EventPending::None;
					m_eventOpen = false;
					EncCategory cat = (m_eventBattleParam == "event") ? EncCategory::Event : EncCategory::Normal;
					if (auto field = dynamic_cast<FieldScene*>(m_currentScene)) field->SetupEventBattle(cat, m_eventBattleParam);
					ChangeScene(SceneType::Battle);
				}
				else if (m_eventPending == EventPending::Shop)
				{
					m_eventPending = EventPending::None;
					m_eventOpen = false;
					ChangeScene(SceneType::Shop);
				}
				else if (m_eventPending == EventPending::CardSelect)
				{
					m_eventPending = EventPending::None;
					m_eventOpen = false;
					ChangeScene(SceneType::CardSelect);
				}
				else if (m_eventPending == EventPending::Treasure)
				{
					m_eventPending = EventPending::None;
					m_eventId = "treasure";      // 財宝イベントを開き直す
					m_eventResult = -1;
					m_eventPickType.clear();
					// m_eventOpen は true のまま
				}
				else
				{
					m_eventOpen = false;
				}
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
	if (pg.showHitmark)
		if (auto battle = dynamic_cast<BattleScene*>(m_currentScene))
		{
			float x, y, w, h;
			if (battle->GetHitmarkRect(x, y, w, h)) { hx = x; hy = y; hw = w; hh = h; }
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
	UiWindow::Draw(m_uiSprite, white, bx - 6, by - 6, bw + 12, bh + 12);
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

void SceneManager::DrawSettings()
{
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	SettingsUI u = SettingsLayout();
	POINT mp = m_uiInput.GetMousePos();
	auto& s = Settings::Get();

	m_uiSprite->Begin();
	m_uiSprite->DrawSprite(white, 0, 0, (float)m_screenWidth, (float)m_screenHeight, 0.0f, XMFLOAT4(0, 0, 0, 0.6f)); // 暗幕
	UiWindow::Draw(m_uiSprite, white, u.panel.x, u.panel.y, u.panel.w, u.panel.h);

	UiWindow::Button(m_uiSprite, white, u.disp.x, u.disp.y, u.disp.w, u.disp.h,
		u.disp.has(mp) ? XMFLOAT4(0.35f, 0.5f, 0.7f, 1) : XMFLOAT4(0.25f, 0.28f, 0.4f, 1));

	auto slider = [&](const URect& t, float val) {
		m_uiSprite->DrawSprite(white, t.x, t.y, t.w, t.h, 0.0f, XMFLOAT4(0.15f, 0.15f, 0.2f, 1));      // 溝
		m_uiSprite->DrawSprite(white, t.x, t.y, t.w * val, t.h, 0.0f, XMFLOAT4(0.3f, 0.6f, 0.9f, 1));  // 塗り
		m_uiSprite->DrawSprite(white, t.x + t.w * val - 5.0f, t.y - 5.0f, 10.0f, t.h + 10.0f, 0.0f, XMFLOAT4(0.9f, 0.9f, 1.0f, 1)); // つまみ
		};
	slider(u.bgmTrack, s.bgmVolume);
	slider(u.seTrack, s.seVolume);

	UiWindow::Button(m_uiSprite, white, u.shake.x, u.shake.y, u.shake.w, u.shake.h,
		s.screenShake ? XMFLOAT4(0.3f, 0.55f, 0.3f, 1) : XMFLOAT4(0.45f, 0.3f, 0.3f, 1));
	UiWindow::Button(m_uiSprite, white, u.close.x, u.close.y, u.close.w, u.close.h,
		u.close.has(mp) ? XMFLOAT4(0.5f, 0.35f, 0.35f, 1) : XMFLOAT4(0.35f, 0.25f, 0.25f, 1));
	m_uiSprite->End();

	m_textRenderer->Begin();
	m_textRenderer->DrawText(L"設定", u.panel.x + u.panel.w / 2 - 30, u.panel.y + 14, 26, D2D1::ColorF(1, 0.9f, 0.6f));
	m_textRenderer->DrawText(L"表示", u.panel.x + 40, u.disp.y + 6, 20, D2D1::ColorF(1, 1, 1));
	m_textRenderer->DrawText(s.displayMode == DisplayMode::Borderless ? L"全画面" : L"ウィンドウ", u.disp.x + 55, u.disp.y + 6, 20, D2D1::ColorF(1, 1, 1));
	m_textRenderer->DrawText(L"BGM", u.panel.x + 40, u.bgmTrack.y - 6, 20, D2D1::ColorF(1, 1, 1));
	m_textRenderer->DrawText(L"SE", u.panel.x + 40, u.seTrack.y - 6, 20, D2D1::ColorF(1, 1, 1));
	m_textRenderer->DrawText(L"画面シェイク", u.panel.x + 40, u.shake.y + 6, 18, D2D1::ColorF(1, 1, 1));
	m_textRenderer->DrawText(s.screenShake ? L"ON" : L"OFF", u.shake.x + 46, u.shake.y + 6, 20, D2D1::ColorF(1, 1, 1));
	m_textRenderer->DrawText(L"閉じる", u.close.x + 42, u.close.y + 8, 20, D2D1::ColorF(1, 1, 1));
	wchar_t b[16];
	swprintf_s(b, L"%d%%", (int)(s.bgmVolume * 100 + 0.5f)); m_textRenderer->DrawText(b, u.bgmTrack.x + u.bgmTrack.w + 14, u.bgmTrack.y - 6, 16, D2D1::ColorF(0.8f, 0.9f, 1));
	swprintf_s(b, L"%d%%", (int)(s.seVolume * 100 + 0.5f));  m_textRenderer->DrawText(b, u.seTrack.x + u.seTrack.w + 14, u.seTrack.y - 6, 16, D2D1::ColorF(0.8f, 0.9f, 1));
	m_textRenderer->End();
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
	else if (mp.x >= 274 && mp.x <= 380)
	{
		title = L"層"; desc = L"ダンジョンの階層(全3層)。深いほど敵が強い"; x = 274.0f;
	}
	if (!title) return;

	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	float ty = BAR_H + 44.0f, w = 360.0f, h = 56.0f;   // レリック帯の下
	m_uiSprite->Begin();
	UiWindow::Draw(m_uiSprite, white, x, ty, w, h);
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

	// 初勝利で初コアを得た"瞬間"にクラフト説明
	//（VICTORYを少し見せてから／コア・素材を個別にスポットライト）
	{
		auto& pd = PlayerDataManager::GetData();
		if (!pd.tutorialCraft && !m_tutorialOpen
			&& pd.materials.count("core_slash") && pd.materials["core_slash"] > 0)
		{
			pd.tutorialCraft = true; PlayerDataManager::Save();
			// スポットライトは勝利画面のドロップ行に一致（cx640 / rx490 / 行0=y270・行1=y322 / 300x42）
			ShowTutorialDelayed({
				{ L"【コア】カードの土台。何を作るかを決める部品（例：斬核＝近接ダメージ）。",
				  482, 262, 316, 54 },   // ← コア行だけを明るく
				{ L"【素材】コアに組み込んで強化する部品（例：剣の破片＝ダメージ+2）。",
				  486, 318, 308, 50 },   // ← 素材行だけを明るく
				{ L"休憩マスの「クラフト」で コア＋素材 を組み合わせ、新しいカードが作れる！" },
				}, 1.0f);   // ← VICTORYを1秒見せてから開始
		}
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

	// チュートリアル中：カード強制ホバー＋戦闘はフリールックで駆動
	if (m_tutorialOpen)
	{
		if (auto battle = dynamic_cast<BattleScene*>(m_currentScene))
		{
			battle->SetHoveredCard(m_tutorialPages[m_tutorialPage].hoverCard);
			battle->SetFreeLook(true);
			battle->SetForceHitmark(m_tutorialPages[m_tutorialPage].showHitmark);
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

static const char* CoreTypeIcon(const std::string& type)
{
	if (type == "Attack") return "icon_attack";   // 剣
	if (type == "Skill")  return "icon_block";    // 盾
	if (type == "Power")  return "icon_buff";     // ↑
	return "";
}
// 素材が付けられるコア型のアイコン一覧
static std::vector<const char*> MatFitIcons(const std::string& id)
{
	std::vector<const char*> v;
	auto m = MaterialDataBase::GetMaterial(id);
	if (!m) return v;
	bool all = m->entries.count("all") > 0;
	if (all || m->entries.count("Attack")) v.push_back("icon_attack");
	if (all || m->entries.count("Skill"))  v.push_back("icon_block");
	if (all || m->entries.count("Power"))  v.push_back("icon_buff");
	return v;
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
		UiWindow::Button(m_uiSprite, white, x, y, w, h,
			filled ? XMFLOAT4(0.25f, 0.35f, 0.5f, 1.0f) : XMFLOAT4(0.15f, 0.15f, 0.2f, 1.0f));
	}
	// 所持素材
	
	auto bs = CraftBases();
	for (int i = 0; i < (int)bs.size(); i++) {
		float x, y, w, h; GetCraftBaseRect(i, x, y, w, h);
		UiWindow::Button(m_uiSprite, white, x, y, w, h, XMFLOAT4(0.4f, 0.3f, 0.2f, 1.0f));
		if (auto b = MaterialDataBase::GetBase(bs[i].first)) {
			const char* ic = CoreTypeIcon(b->type);
			if (ic[0]) m_uiSprite->DrawSprite(TextureManager::Get(ic),
				x + w - 24.0f, y + (h - 18.0f) / 2.0f, 18.0f, 18.0f, 0.0f, XMFLOAT4(1, 1, 1, 1));
		}
	}
	auto md = CraftMods();
	for (int i = 0; i < (int)md.size(); i++) {
		float x, y, w, h; GetCraftModRect(i, x, y, w, h);
		UiWindow::Button(m_uiSprite, white, x, y, w, h, XMFLOAT4(0.2f, 0.3f, 0.3f, 1.0f));
		auto icons = MatFitIcons(md[i].first);
		for (int k = 0; k < (int)icons.size(); k++)
			m_uiSprite->DrawSprite(TextureManager::Get(icons[k]),
				x + w - 22.0f - k * 20.0f, y + (h - 18.0f) / 2.0f, 18.0f, 18.0f, 0.0f, XMFLOAT4(1, 1, 1, 1));
	}
	
		// 作成ボタン
		{
			float x, y, w, h; GetCraftBtnRect(x, y, w, h);
			bool ready = !m_craftBase.empty() && (int)m_craftMods.size() >= CraftModSlots();
			bool hov = UiHover(x, y, w, h, m_uiInput.GetMousePos(), "craftmake");   // ホバーSE＋判定
			float dy = hov ? -4.0f : 0.0f;
			UiWindow::Button(m_uiSprite, white, x, y + dy, w, h,
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
		POINT mp = m_uiInput.GetMousePos();
		float dy = (mp.x >= x && mp.x <= x + w && mp.y >= y && mp.y <= y + h) ? -4.0f : 0.0f;
		m_textRenderer->DrawText(L"Make", x + 55.0f, y + dy + 12.0f, 20.0f, D2D1::ColorF(1, 1, 1));
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
	UiWindow::Draw(m_uiSprite, white, x, y, w, h);
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
		if (auto b = MaterialDataBase::GetBase(bases[i].first)) {
			const char* ic = CoreTypeIcon(b->type);
			if (ic[0]) m_uiSprite->DrawSprite(TextureManager::Get(ic),
				x + w - 24.0f, y + (h - 18.0f) / 2.0f, 18.0f, 18.0f, 0.0f, XMFLOAT4(1, 1, 1, 1));
		}
	}
	auto mods = CraftMods();
	for (int i = 0; i < (int)mods.size(); i++) {
		float x, y, w, h; GetCraftModRect(i, x, y, w, h);
		m_uiSprite->DrawSprite(white, x, y, w, h, 0.0f, XMFLOAT4(0.2f, 0.3f, 0.3f, 0.85f));
		auto icons = MatFitIcons(mods[i].first);
		for (int k = 0; k < (int)icons.size(); k++)
			m_uiSprite->DrawSprite(TextureManager::Get(icons[k]),
				x + w - 22.0f - k * 20.0f, y + (h - 18.0f) / 2.0f, 18.0f, 18.0f, 0.0f, XMFLOAT4(1, 1, 1, 1));
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
		UiWindow::Draw(m_uiSprite, white, x, y, w, h);
		XMFLOAT4 c = base[i];
		m_uiSprite->DrawSprite(white, x + 6, y + 6, w - 12, h - 12, 0.0f,
			XMFLOAT4(c.x, c.y, c.z, hov ? 0.6f : 0.4f));   // 種類色を中に薄く
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

	// ホバーで各行動の説明ウィンドウ（チュートリアル中は出さない）
	if (!m_tutorialOpen)
	{
		const wchar_t* descs[3] = {
			L"HPを20回復する",
			L"手持ちカード1枚を選んで強化する",
			L"コア＋素材を組み合わせて新しいカードを作る",
		};
		for (int i = 0; i < 3; i++)
		{
			float x, y, w, h; GetRestBtnRect(i, x, y, w, h);
			if (mp.x >= x && mp.x <= x + w && mp.y >= y && mp.y <= y + h)
			{
				float tw = 300.0f, th = 34.0f, tx = x + w + 12.0f, ty = y + (h - th) / 2.0f;
				if (tx + tw > m_screenWidth - 6.0f) tx = x - tw - 12.0f;   // 右に出ないなら左へ
				m_uiSprite->Begin();
				UiWindow::Draw(m_uiSprite, white, tx, ty, tw, th);
				m_uiSprite->End();
				m_textRenderer->Begin();
				m_textRenderer->DrawText(descs[i], tx + 12.0f, ty + 8.0f, 15.0f, D2D1::ColorF(0.95f, 0.95f, 0.95f));
				m_textRenderer->End();
				break;
			}
		}
	}
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

	const float CELL = 64.0f, GAP = 16.0f;
	float totalW = COLS * (CELL + GAP) - GAP;
	float totalH = ROWS * (CELL + GAP) - GAP;
	float offX = (m_screenWidth - totalW) / 2.0f;
	float offY = (m_screenHeight - totalH) / 2.0f + 20.0f;

	auto idxOf = [&](int c, int r) { return c * ROWS + r; };
	auto typeOf = [&](int c, int r) { return (FieldNodeType)pd.fieldNodeTypes[idxOf(c, r)]; };
	auto posOf = [&](int c, int r, float& x, float& y) { x = offX + c * (CELL + GAP); y = offY + r * (CELL + GAP); };

	POINT mp = m_uiInput.GetMousePos();
	FieldNodeType hoverType = FieldNodeType::Empty;
	float hoverX = 0.0f, hoverY = 0.0f;

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
				if (mp.x >= x && mp.x <= x + CELL && mp.y >= y && mp.y <= y + CELL)
				{
					hoverType = t; hoverX = x + CELL + 8.0f; hoverY = y;
				}
				m_uiSprite->DrawSprite(TextureManager::Get("ui_node"), x, y, CELL, CELL, 0.0f,
					MapNodeColor(t, pd.fieldNodeVisited[idxOf(c, r)], isPlayer));
				const char* icon = isPlayer ? "node_player" : NodeIconName(t);
				if (icon[0])
				{
					float isz = CELL * 0.60f;
					m_uiSprite->DrawSprite(TextureManager::Get(icon),
						x + (CELL - isz) / 2.0f, y + (CELL - isz) / 2.0f, isz, isz, 0.0f, XMFLOAT4(1, 1, 1, 1));
				}
			}
	}
	m_uiSprite->End();

	m_textRenderer->Begin();
	m_textRenderer->DrawText(L"MAP", m_screenWidth / 2.0f - 30.0f, offY - 44.0f, 26.0f, D2D1::ColorF(1, 1, 1));
	if (!haveMap)
		m_textRenderer->DrawText(L"No map yet", m_screenWidth / 2.0f - 60.0f, m_screenHeight / 2.0f, 22.0f, D2D1::ColorF(0.7f, 0.7f, 0.7f));
	m_textRenderer->DrawText(L"click to close", m_screenWidth / 2.0f - 55.0f, offY + totalH + 16.0f, 16.0f, D2D1::ColorF(0.7f, 0.7f, 0.7f));
	m_textRenderer->End();

	// ホバーでマスの説明（フィールドの凡例と同じ内容）
	if (haveMap && hoverType != FieldNodeType::Empty)
	{
		const float tw = 320.0f, th = 56.0f;
		float tx = hoverX, ty = hoverY;
		if (tx + tw > m_screenWidth - 8.0f) tx = m_screenWidth - 8.0f - tw;   // 右端で左に寄せる
		if (ty + th > m_screenHeight - 8.0f) ty = m_screenHeight - 8.0f - th;
		m_uiSprite->Begin();
		UiWindow::Draw(m_uiSprite, white, tx, ty, tw, th);
		m_uiSprite->End();
		m_textRenderer->Begin();
		m_textRenderer->DrawText(NodeDisplayName(hoverType), tx + 12.0f, ty + 8.0f, 18.0f, D2D1::ColorF(1, 0.9f, 0.6f));
		m_textRenderer->DrawText(NodeDesc(hoverType), tx + 12.0f, ty + 32.0f, 14.0f, D2D1::ColorF(0.95f, 0.95f, 0.95f));
		m_textRenderer->End();
	}
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
		if (d && d->count > 0)
		{
			auto& rc = PlayerDataManager::GetData().relicCounters;
			auto it = rc.find(relics[i]);
			int cur = (it != rc.end()) ? it->second : 0;
			wchar_t cnt[16]; swprintf_s(cnt, L"%d/%d", cur, d->count);
			m_textRenderer->DrawOutlinedText(cnt, x, y + h + 2.0f, 15.0f,
				D2D1::ColorF(1.0f, 0.9f, 0.4f), D2D1::ColorF(0, 0, 0), 1.0f);   // 幅2.0→1.0
		}
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
		else if (o.type == "battle") { if (m_eventPending == EventPending::None) { m_eventPending = EventPending::Battle; m_eventBattleParam = o.param; } }
		else if (o.type == "shop") { if (m_eventPending == EventPending::None) m_eventPending = EventPending::Shop; }
		else if (o.type == "treasure") { if (m_eventPending == EventPending::None) m_eventPending = EventPending::Treasure; }
		else if (o.type == "pickCard") { if (m_eventPending == EventPending::None) { m_eventPending = EventPending::CardSelect; m_cardSelectMode = o.param; } }
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
			UiWindow::Draw(m_uiSprite, white, x, yy, w, h);
			if (!en)
				m_uiSprite->DrawSprite(white, x + 6, yy + 6, w - 12, h - 12, 0.0f, XMFLOAT4(0.0f, 0.0f, 0.0f, 0.45f));   // 無効=暗く
			else if (hov)
				m_uiSprite->DrawSprite(white, x + 6, yy + 6, w - 12, h - 12, 0.0f, XMFLOAT4(0.55f, 0.45f, 0.75f, 0.45f)); // ホバー強調
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

	const CardData* animCard = nullptr;
	float animScale = 0.85f, animAlpha = 1.0f, animYoff = 0.0f, animBX = 0, animBY = 0;
	float fxT = 0.0f; bool presenting = false;   // fxT=エフェクト段階の進行 / presenting=前に見せてる段階
	if (anim)
	{
		float sbx, sby; GetEventCardSlot(m_eventPickAnimIdx, sbx, sby);
		float pbx = m_screenWidth / 2.0f - CardVisual::CARD_W / 2.0f;
		float pby = m_screenHeight / 2.0f - CardVisual::CARD_H / 2.0f;
		const float P1 = 0.18f, P2 = 0.40f;   // 旧カードを見せる時間を短く
		if (t < P1)
		{
			presenting = true;
			float u = t / P1, e = u * u * (3.0f - 2.0f * u);
			animBX = sbx + (pbx - sbx) * e; animBY = sby + (pby - sby) * e;
			animScale = 0.85f + (1.3f - 0.85f) * e; animAlpha = 1.0f;
			if (m_eventPickAnimIdx < (int)deck.size()) animCard = CardDataBase::Get(deck[m_eventPickAnimIdx]);
		}
		else if (t < P2)   // ← ホールド（中央で拡大したまま見せる）
		{
			presenting = true;
			animBX = pbx; animBY = pby; animScale = 1.3f; animAlpha = 1.0f;
			if (m_eventPickAnimIdx < (int)deck.size()) animCard = CardDataBase::Get(deck[m_eventPickAnimIdx]);
		}
		else
		{
			fxT = (t - P2) / (1.0f - P2);
			animBX = pbx; animBY = pby; animScale = 1.3f;
			if (!morph)
			{
				if (m_eventPickAnimIdx < (int)deck.size()) animCard = CardDataBase::Get(deck[m_eventPickAnimIdx]);
				animScale = 1.3f * (1.0f - fxT * 0.4f); animAlpha = 1.0f - fxT; animYoff = -fxT * 40.0f;
			}
			else if (fxT < 0.2f)   // 旧カードが消える
			{
				if (m_eventPickAnimIdx < (int)deck.size()) animCard = CardDataBase::Get(deck[m_eventPickAnimIdx]);
				animAlpha = 1.0f - fxT / 0.2f;
			}
			else if (fxT < 0.32f)   // 新カードが現れる
			{
				animCard = CardDataBase::Get(m_eventPickAnimTo);
				animAlpha = (fxT - 0.2f) / 0.12f;
			}
			else                    // 変化後
			{
				animCard = CardDataBase::Get(m_eventPickAnimTo);
				animAlpha = 1.0f;
			}
		}
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
	m_textRenderer->End();

	// 提示カードは最後に「本体→文字」まとめて描く（背後の文字より後＝被った所だけ隠れる）
	if (anim && animCard)
	{
		m_uiSprite->Begin();
		XMFLOAT4 col = CardVisual::GetCardColor(animCard->type); col.w = animAlpha;
		CardVisual::DrawBase(m_uiSprite, white, animBX, animBY + animYoff, animScale, 0.0f, col, animCard, m_uiTime);
		if (!presenting && morph)
		{
			float flash = 1.0f - fabsf(fxT - 0.5f) * 4.0f;
			if (flash > 0.0f) { float x, y, w, h; CardVisual::GetRect(animBX, animBY, animScale, x, y, w, h); m_uiSprite->DrawSprite(white, x, y, w, h, 0.0f, XMFLOAT4(1, 1, 1, flash * 0.8f)); }
		}
		else if (!presenting)
		{
			float x, y, w, h; CardVisual::GetRect(animBX, animBY + animYoff, animScale, x, y, w, h);
			m_uiSprite->DrawSprite(white, x, y, w, h, 0.0f, XMFLOAT4(0.95f, 0.2f, 0.2f, animAlpha * 0.6f));
		}
		m_uiSprite->End();

		m_textRenderer->Begin();
		CardVisual::DrawTexts(m_textRenderer, animCard, nullptr, animBX, animBY + animYoff, animScale, 0.0f, animAlpha);
		m_textRenderer->End();
	}

	if (!anim && hov >= 0 && hov < (int)deck.size())
	{
		const CardData* d = CardDataBase::Get(deck[hov]);
		float bx, by; GetEventCardSlot(hov, bx, by);
		float rx, ry, rw, rh; CardVisual::GetRect(bx, by - 12.0f, 0.85f, rx, ry, rw, rh);
		CardTooltip::Draw(m_uiSprite, m_textRenderer, TextureManager::Get("white"), d,
			rx + rw / 2.0f, ry, rw, rh, m_screenWidth, m_screenHeight);
	}
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

void SceneManager::DrawBigCard(const CardData* d, float x, float y, float scale, bool textPass)
{
	if (!d) return;
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	if (!textPass)
		CardVisual::DrawBase(m_uiSprite, white, x, y, scale, 0.0f,
			CardVisual::GetCardColor(d->type), d, m_uiTime);
	else
		CardVisual::DrawTexts(m_textRenderer, d, nullptr, x, y, scale, 0.0f, 1.0f);
}