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
#include "CardConfirm.h"
#define PL_MPEG_IMPLEMENTATION
#include "VideoClip.h"
extern ID3D11Device* g_pd3dDevice;
extern ID3D11DeviceContext* g_pImmediateContext;

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

	TextureManager::Load("white", L"Assets/Test/White.png");

	// パーティクル
	{
		TextureManager::Load("particle", L"Assets/Particles/circle.png");
		TextureManager::Load("particle_poison", L"Assets/Particles/circle_01.png");
		TextureManager::Load("fire", L"Assets/Particles/fire.png");
		TextureManager::Load("magic", L"Assets/Particles/magic.png");
		TextureManager::Load("smoke", L"Assets/Particles/smoke.png");
		TextureManager::Load("star", L"Assets/Particles/star.png");
		TextureManager::Load("arrow_up", L"Assets/Particles/arrow_up.png");
		TextureManager::Load("buff_trappower", L"Assets/UI/relic_trap_master.png");   // 罠術バフ（仮：罠の達人のアイコン）   // バフの上向き矢印（tools/particles/make_arrow_up.py）
		TextureManager::Load("cross", L"Assets/Particles/cross.png");   // 回復の十字
		TextureManager::Load("skull", L"Assets/Particles/skull.png");
		TextureManager::Load("crosshair", L"Assets/Particles/crosshair.png");

		TextureManager::Load("fx_test", L"Assets/Particles/fx_test.png");
		TextureManager::Load("fx_slash", L"Assets/Particles/Slash.png");
		TextureManager::Load("fx_blood", L"Assets/Particles/blood.png");
		TextureManager::Load("fx_defend", L"Assets/Particles/defend.png");
		TextureManager::Load("fx_knife", L"Assets/Particles/knife.png");
		TextureManager::Load("fx_smash", L"Assets/Particles/smash.png");
		TextureManager::Load("fx_meteor", L"Assets/Particles/meteor.png");
		TextureManager::Load("fx_meteor2", L"Assets/Particles/meteor2.png");
	}

	// フィールド
	{
		TextureManager::Load("title", L"Assets/Test/Title.png");
		TextureManager::Load("battle_bg", L"Assets/Field/GrassField.jpg");
		// 1層 草原
		TextureManager::Load("ground_grass", L"Assets/Field/ground_grass.png");
		// 盤面のマス（白っぽい板。層ごとの色を掛けて使う）
		for (int i = 0; i < 4; i++)
		{
			std::string key = "cell_stone_" + std::to_string(i);
			std::wstring path = L"Assets/Field/cell_stone_" + std::to_wstring(i) + L".png";
			TextureManager::Load(key, path.c_str());
		}
		TextureManager::Load("cell_panel", L"Assets/Field/cell_panel.png");
		TextureManager::Load("deco_tree", L"Assets/Field/deco_tree.png");
		TextureManager::Load("deco_rock", L"Assets/Field/deco_rock.png");
		TextureManager::Load("deco_bush", L"Assets/Field/deco_bush.png");
		// 2層 洞窟
		TextureManager::Load("ground_cave", L"Assets/Field/ground_cave.png");
		TextureManager::Load("deco_stalagmite", L"Assets/Field/deco_stalagmite.png");
		TextureManager::Load("deco_crystal", L"Assets/Field/deco_crystal.png");
		TextureManager::Load("deco_caverock", L"Assets/Field/deco_caverock.png");

		TextureManager::Load("ground_snakepit", L"Assets/Field/ground_snakepit.png");
		TextureManager::Load("deco_darkspike", L"Assets/Field/deco_darkspike.png");
		TextureManager::Load("deco_bones", L"Assets/Field/deco_bones.png");
		TextureManager::Load("deco_egg", L"Assets/Field/deco_egg.png");
		// 3層 近未来
		TextureManager::Load("ground_future", L"Assets/Field/ground_future.png");
		TextureManager::Load("deco_pillar", L"Assets/Field/deco_pillar.png");
		TextureManager::Load("deco_terminal", L"Assets/Field/deco_terminal.png");
		TextureManager::Load("deco_techblock", L"Assets/Field/deco_techblock.png");
		TextureManager::Load("ground_scorched", L"Assets/Field/ground_scorched.png");
		TextureManager::Load("deco_deadtree", L"Assets/Field/deco_deadtree.png");
		TextureManager::Load("deco_darkrock", L"Assets/Field/deco_darkrock.png");
		TextureManager::Load("deco_ember", L"Assets/Field/deco_ember.png");
		TextureManager::Load("deco_shadow", L"Assets/Field/deco_shadow.png");
		TextureManager::Load("map_bg1", L"Assets/Field/Map1.png");
		TextureManager::Load("map_bg2", L"Assets/Field/Map2.png");
		TextureManager::Load("map_bg3", L"Assets/Field/Map3.png");
		TextureManager::Load("reward_bg", L"Assets/Field/CardSelect.png");
		TextureManager::Load("shop_rug", L"Assets/Field/shop.png");

		TextureManager::Load("ground_void", L"Assets/Field/ground_void.png");
		TextureManager::Load("ground_void_awake", L"Assets/Field/ground_void_awake.png");
		TextureManager::Load("deco_rift", L"Assets/Field/deco_rift.png");
		TextureManager::Load("deco_debris", L"Assets/Field/deco_debris.png");
		TextureManager::Load("deco_throne", L"Assets/Field/deco_throne.png");
	}

	// プレイヤー
	{
		TextureManager::Load("player", L"Assets/Player/player.png");
		TextureManager::Load("kakashi", L"Assets/Player/kakashi.png");

		TextureManager::Load("player_front", L"Assets/Player/player_front.png");
		TextureManager::Load("player_back", L"Assets/Player/player_back.png");
		TextureManager::Load("player_side", L"Assets/Player/player_side.png");

		TextureManager::Load("player_side_attack", L"Assets/Player/player_side_attack.png");
		TextureManager::Load("player_front_attack", L"Assets/Player/player_front_attack.png");
		TextureManager::Load("player_back_attack", L"Assets/Player/player_back_attack.png");
		TextureManager::Load("player_side_idle", L"Assets/Player/player_side_idle.png");
		TextureManager::Load("player_front_idle", L"Assets/Player/player_front_idle.png");
		TextureManager::Load("player_back_idle", L"Assets/Player/player_back_idle.png");
		TextureManager::Load("player_side_skill", L"Assets/Player/player_side_skill.png");
		TextureManager::Load("player_front_skill", L"Assets/Player/player_front_skill.png");
		TextureManager::Load("player_back_skill", L"Assets/Player/player_back_skill.png");
		TextureManager::Load("player_side_hurt", L"Assets/Player/player_side_hurt.png");
		TextureManager::Load("player_front_hurt", L"Assets/Player/player_front_hurt.png");
		TextureManager::Load("player_back_hurt", L"Assets/Player/player_back_hurt.png");
	}

	// エネミー
	{
		TextureManager::Load("enemy_slime", L"Assets/Enemy/slime.png");
		TextureManager::Load("enemy_zako", L"Assets/Enemy/zako.png");
		TextureManager::Load("enemy_goblin", L"Assets/Enemy/goblin.png");
		TextureManager::Load("enemy_orc", L"Assets/Enemy/orc.png");
		TextureManager::Load("enemy_bigfoot", L"Assets/Enemy/bigfoot.png");
		TextureManager::Load("enemy_fire", L"Assets/Enemy/fireEnemy.png");
		TextureManager::Load("enemy_bug", L"Assets/Enemy/bug.png");
		TextureManager::Load("enemy_spore", L"Assets/Enemy/spore.png");
		TextureManager::Load("enemy_hound", L"Assets/Enemy/hound.png");
		TextureManager::Load("enemy_dragon_red", L"Assets/Enemy/dragon_red.png");
		TextureManager::Load("enemy_archer", L"Assets/Enemy/archer.png");
		TextureManager::Load("enemy_reaper", L"Assets/Enemy/reaper.png");
		TextureManager::Load("enemy_tentacle", L"Assets/Enemy/cyaegha.png");
		TextureManager::Load("enemy_bear", L"Assets/Enemy/bear.png");
		TextureManager::Load("enemy_slimeking", L"Assets/Enemy/shoggoth.png");
		TextureManager::Load("enemy_golem", L"Assets/Enemy/golem.png");
		TextureManager::Load("enemy_obake", L"Assets/Enemy/obake.png");
		TextureManager::Load("enemy_mimic", L"Assets/Enemy/mimic.png");
		TextureManager::Load("enemy_bat", L"Assets/Enemy/bat.png");
		TextureManager::Load("enemy_caveslime", L"Assets/Enemy/caveslime.png");
		TextureManager::Load("enemy_crawler", L"Assets/Enemy/crawler.png");
		TextureManager::Load("enemy_spider", L"Assets/Enemy/spider.png");
		TextureManager::Load("enemy_shrieker", L"Assets/Enemy/shrieker.png");
		TextureManager::Load("enemy_myconid", L"Assets/Enemy/myconid.png");
		TextureManager::Load("enemy_cavegolem", L"Assets/Enemy/cavegolem.png");
		TextureManager::Load("enemy_venomancer", L"Assets/Enemy/venomancer.png");
		TextureManager::Load("enemy_crystalguard", L"Assets/Enemy/crystalguard.png");
		TextureManager::Load("enemy_broodmother", L"Assets/Enemy/broodmother.png");
		TextureManager::Load("enemy_stonelord", L"Assets/Enemy/stonelord.png");
		TextureManager::Load("enemy_gloomwing", L"Assets/Enemy/gloomwing.png");
		TextureManager::Load("snake_head", L"Assets/Enemy/snake_head.png");
		TextureManager::Load("snake_body", L"Assets/Enemy/snake_body.png");
		TextureManager::Load("snake_corner", L"Assets/Enemy/snake_corner.png");
		TextureManager::Load("enemy_mutant_stalker", L"Assets/Enemy/mutant_stalker.png");
		TextureManager::Load("enemy_plasma_wraith", L"Assets/Enemy/plasma_wraith.png");
		TextureManager::Load("enemy_toxic_bloom", L"Assets/Enemy/toxic_bloom.png");
		TextureManager::Load("enemy_augmented_soldier", L"Assets/Enemy/augmented_soldier.png");
		TextureManager::Load("enemy_nanite_swarm", L"Assets/Enemy/nanite_swarm.png");
		TextureManager::Load("enemy_psi_watcher", L"Assets/Enemy/psi_watcher.png");
		TextureManager::Load("enemy_grav_anomaly", L"Assets/Enemy/grav_anomaly.png");
		TextureManager::Load("enemy_chem_hound", L"Assets/Enemy/chem_hound.png");
		TextureManager::Load("enemy_glitch_phantom", L"Assets/Enemy/glitch_phantom.png");
		TextureManager::Load("enemy_chimera", L"Assets/Enemy/chimera.png");
		TextureManager::Load("enemy_corrupted_core", L"Assets/Enemy/corrupted_core.png");
		TextureManager::Load("enemy_void_weaver", L"Assets/Enemy/void_weaver.png");
		TextureManager::Load("enemy_boss3_piller1", L"Assets/Enemy/boss3_piller1.png");
		TextureManager::Load("enemy_boss3_piller2", L"Assets/Enemy/boss3_piller2.png");
		TextureManager::Load("enemy_boss3_piller3", L"Assets/Enemy/boss3_piller3.png");
		TextureManager::Load("enemy_boss3_piller4", L"Assets/Enemy/boss3_piller4.png");
		TextureManager::Load("darkload", L"Assets/Enemy/finai_boss.png");
		TextureManager::Load("darkload_awaken", L"Assets/Enemy/final_boss_awaken.png");
		for (const char* n : { "sword", "venom", "shield", "hex" })   // 3層ボスの鎖（柱ごと）
		{
			std::string key = std::string("boss_chain_") + n;
			std::wstring path = L"Assets/Enemy/" + std::wstring(key.begin(), key.end()) + L".png";
			TextureManager::Load(key, path.c_str());
		}
	}

	// UI
	{
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
			for (int i = 1; i <= 10; i++)   // 信仰の紋章（外周10区切りのうち i 個が光る）
			{
				wchar_t path[64]; swprintf_s(path, L"Assets/UI/faith_%d.png", i);
				char key[16]; sprintf_s(key, "faith_%d", i);
				TextureManager::Load(key, path);
			}
			TextureManager::Load("faith_aura", L"Assets/UI/faith_aura.png");       // 聖地の四角いオーラ
			TextureManager::Load("faith_pillar", L"Assets/UI/faith_pillar.png");   // 聖地に立つ敵の前の光
			TextureManager::Load("ui_threat", L"Assets/UI/ui_threat.png");
			TextureManager::Load("mat_core", L"Assets/UI/mat_core.png");
			TextureManager::Load("mat_material", L"Assets/UI/mat_material.png");
			TextureManager::Load("ui_reach", L"Assets/UI/ui_reach.png");
			TextureManager::Load("ui_hitmark", L"Assets/UI/ui_hitmark.png");
			TextureManager::Load("ui_hitring", L"Assets/UI/ui_hitring.png");
			TextureManager::Load("icon_curse", L"Assets/UI/icon_curse.png");
			TextureManager::Load("icon_hp", L"Assets/UI/icon_hp.png");
			TextureManager::Load("icon_steps", L"Assets/UI/icon_steps.png");
			TextureManager::Load("icon_gold", L"Assets/UI/icon_gold.png");
			TextureManager::Load("icon_map", L"Assets/UI/icon_map.png");
			TextureManager::Load("icon_layer", L"Assets/UI/icon_layer.png");
			TextureManager::Load("mouse_rdrag", L"Assets/UI/Cursor/mouse_rdrag.png");     // カメラ操作のデモ
			TextureManager::Load("mouse_wheel", L"Assets/UI/Cursor/mouse_wheel.png");
			TextureManager::Load("mouse_mclick", L"Assets/UI/Cursor/mouse_mclick.png");
			RelicManager::LoadIcons();

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
		}

		// 罠
		{
			TextureManager::Load("trap_fire", L"Assets/UI/trap_fire.png");
			TextureManager::Load("trap_poison", L"Assets/UI/trap_poison.png");
			TextureManager::Load("trap_landmine", L"Assets/UI/trap_landmine.png");
			TextureManager::Load("trap_thorn", L"Assets/UI/trap_thorn.png");
			TextureManager::Load("trap_swamp", L"Assets/UI/trap_swamp.png");
			TextureManager::Load("trap_ice", L"Assets/UI/trap_ice.png");
			TextureManager::Load("trap_root", L"Assets/UI/trap_root.png");
			TextureManager::Load("trap_explosion", L"Assets/UI/trap_explosion.png");
			TextureManager::Load("trap_expose", L"Assets/UI/trap_expose.png");
			TextureManager::Load("trap_weaken", L"Assets/UI/trap_weaken.png");
		}

		// バフデバフアイコン
		{
			TextureManager::Load("icon_attack", L"Assets/UI/icon_attack.png");
			TextureManager::Load("icon_multihit", L"Assets/UI/icon_multihit.png");
			TextureManager::Load("icon_block", L"Assets/UI/icon_block.png");
			TextureManager::Load("icon_buff", L"Assets/UI/icon_buff.png");
			TextureManager::Load("icon_debuff", L"Assets/UI/icon_debuff.png");
			TextureManager::Load("icon_move", L"Assets/UI/icon_move.png");
			TextureManager::Load("icon_summon", L"Assets/UI/icon_summon.png");
			TextureManager::Load("icon_coil", L"Assets/UI/icon_coil.png");
			TextureManager::Load("icon_hazard", L"Assets/UI/icon_hazard.png");
TextureManager::Load("intent_attack", L"Assets/UI/intent_attack.png");
TextureManager::Load("intent_multihit", L"Assets/UI/intent_multihit.png");
TextureManager::Load("intent_block", L"Assets/UI/intent_block.png");
TextureManager::Load("intent_buff", L"Assets/UI/intent_buff.png");
TextureManager::Load("intent_debuff", L"Assets/UI/intent_debuff.png");
TextureManager::Load("intent_move", L"Assets/UI/intent_move.png");
TextureManager::Load("intent_pull", L"Assets/UI/intent_pull.png");
TextureManager::Load("intent_knockback", L"Assets/UI/intent_knockback.png");
TextureManager::Load("intent_summon", L"Assets/UI/intent_summon.png");
TextureManager::Load("intent_coil", L"Assets/UI/intent_coil.png");
TextureManager::Load("intent_curse", L"Assets/UI/intent_curse.png");
TextureManager::Load("intent_hazard", L"Assets/UI/intent_hazard.png");
TextureManager::Load("intent_heal", L"Assets/UI/intent_heal.png");
			TextureManager::Load("buff_attackup", L"Assets/UI/buff_attackup.png");
			TextureManager::Load("buff_defenseup", L"Assets/UI/buff_defenseup.png");
			TextureManager::Load("buff_weak", L"Assets/UI/buff_weak.png");
			TextureManager::Load("buff_vulnerable", L"Assets/UI/buff_vulnerable.png");
			TextureManager::Load("buff_poison", L"Assets/UI/buff_poison.png");
			TextureManager::Load("buff_holyground", L"Assets/UI/buff_holyground.png");
			TextureManager::Load("buff_sanctuary", L"Assets/UI/buff_sanctuary.png");
			TextureManager::Load("buff_molt", L"Assets/UI/buff_molt.png");
			TextureManager::Load("buff_guardianvow", L"Assets/UI/buff_guardianvow.png");
			TextureManager::Load("buff_cathedral", L"Assets/UI/buff_cathedral.png");
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
TextureManager::Load("buff_drawperturn", L"Assets/UI/buff_drawperturn.png");
TextureManager::Load("buff_invincible", L"Assets/UI/buff_invincible.png");
TextureManager::Load("buff_mire", L"Assets/UI/buff_mire.png");
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
			TextureManager::Load("buff_headweak", L"Assets/UI/buff_headweak.png");
			TextureManager::Load("buff_immovable", L"Assets/UI/buff_immovable.png");
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
			scene->onOpenSettings = [this]() { m_settingsOpen = true; };
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
				m_restOpen = true; m_restActive = true; m_restArmed = false;
				Audio::PlayBGM("Assets/Sound/bgm/Rest.mp3");
				auto& pd = PlayerDataManager::GetData();
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

	auto& pd = PlayerDataManager::GetData();
	if (type == SceneType::Field && !pd.tutorialField)
	{
		pd.tutorialField = true; PlayerDataManager::Save();
		ShowTutorial({
			{ L"マスをクリックして進む\n右端のボスを倒すと次の層へ", 170, 130, 940, 540 },
			});
	}
	if (type == SceneType::Battle && !pd.tutorialBattle)
	{
		pd.tutorialBattle = true; PlayerDataManager::Save();
		ShowTutorialDelayed({
	{ L"カードを掴んで、使いたいマスの上で離す\n左から 移動・アタック・ブロック", 340, 580, 600, 220 },
	{ L"色付きのマスは敵の攻撃範囲\n移動で避けて、アタックで倒そう",         170, 130, 940, 540 },
			}, 1.2f);
	}

	if (type == SceneType::CardSelect && !pd.tutorialCardSelect)
	{
		pd.tutorialCardSelect = true; PlayerDataManager::Save();
		ShowTutorial({
			{ L"1枚選ぶとデッキに加わる（スキップも可）", 376, 224, 490, 224 },
			});
	}
	if (type == SceneType::Shop && !pd.tutorialShop)
	{
		pd.tutorialShop = true; PlayerDataManager::Save();
		ShowTutorial({
			{ L"クリックで購入（ゴールドを使う）\n左下でカード削除、SELLで素材を売れる", 180, 120, 920, 470 },
			});
	}
}

void SceneManager::Draw()
{
	if (auto field = dynamic_cast<FieldScene*>(m_currentScene))
	{
		bool overlay = m_settingsOpen || m_glossaryOpen || m_craftOpen || m_restOpen || m_mapOpen
			|| m_invOpen || m_deckOpen || m_tutorialOpen || BossRelicPickOpen();
		field->SetHoverEnabled(!overlay);
	}
	if (m_currentScene) m_currentScene->Draw();
	if (m_invOpen) DrawInventory();
	if (m_mapOpen) DrawMap();
	bool fading = (m_fadeState != Fade::None);
	if (HasTopBar() && !fading) DrawOverlay();
	if (m_eventOpen && !m_mapOpen && !m_invOpen && !m_deckOpen)
	{
		if (m_eventPickType.empty()) DrawEvent();
		else if (m_eventPickType == "transformRelic") DrawEventRelicPicker();
		else DrawEventPicker();
	}
	if (m_craftOpen) DrawCraft();
	if (m_restOpen && !m_mapOpen && !m_invOpen && !m_deckOpen && !m_craftOpen) DrawRest();
	if (m_deckOpen || m_mapOpen || m_invOpen || m_restOpen || m_craftOpen) DrawBackButton();
	if (m_craftFxTimer > 0.0f) DrawCraftFx();  
	if (HasTopBar() && !fading) { DrawRelicBar(); DrawBarTips(); }
	if (BossRelicPickOpen() && !fading) DrawBossRelicPick();
	if (!TutorialActive() && !fading && !CamDemoActive()) DrawHint();
	if (auto b = dynamic_cast<BattleScene*>(m_currentScene))   // カメラ操作のデモの説明
	{
		const wchar_t* t; const char* ic; float a;
		if (!fading && b->CamDemoCaption(t, ic, a)) DrawGuideBox(t, a, ic, L"クリックで飛ばす");
	}
	if (m_settingsOpen) DrawSettings();
	if (m_glossaryOpen) DrawGlossary();

	if (m_tutorialOpen) DrawTutorial();

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

	if (m_deckOpen)
	{
		m_uiSprite->DrawSprite(white, 0.0f, BAR_H, (float)m_screenWidth, (float)m_screenHeight - BAR_H,
			0.0f, XMFLOAT4(0.0f, 0.0f, 0.0f, 0.8f));          // 暗幕
		DrawDeckCards(false);

		// 強化後表示トグル
		float ubX = 20.0f, ubY = m_screenHeight - 46.0f, ubW = 190.0f, ubH = 34.0f;
		UiWindow::Button(m_uiSprite, white, ubX, ubY, ubW, ubH,
			m_deckShowUpgrade ? XMFLOAT4(0.30f, 0.55f, 0.30f, 1.0f)
			: XMFLOAT4(0.30f, 0.30f, 0.35f, 1.0f));

		m_textRenderer->Begin();

			DrawDeckCards(true);
			m_textRenderer->DrawText(m_deckShowUpgrade ? L"強化後表示: ON" : L"強化後表示: OFF",
				36.0f, m_screenHeight - 38.0f, 18.0f, D2D1::ColorF(1, 1, 1));

		m_textRenderer->End();
	}

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
		SettingsUI u = SettingsLayout();
		URect g = Lifted(u.gear);
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
		SettingsUI u = SettingsLayout(); URect g = Lifted(u.gear);
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

	if (m_deckOpen) DrawDeckPreview();   // ← 全グリッド描画の後＝最前面

	// デッキのキーワード解説（パス外で描く）
	if (m_deckOpen && m_deckPreviewIdx < 0 && m_craftFxTimer <= 0.0f)   // 強化の演出中は出さない
	{
		int hk = DeckHoverIndex();
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

// デッキ確認のホバー中のカード。説明のウィンドウの上にいる間は、隣のカードに切り替えない
int SceneManager::DeckHoverIndex()
{
	if (CardTooltip::MouseInPanel()) return m_deckTipIdx;
	m_deckTipIdx = GetDeckCardAt(m_uiInput.GetMousePos());
	return m_deckTipIdx;
}

void SceneManager::DrawDeckCards(bool textPass)
{
	auto& deck = PlayerDataManager::GetData().deck;
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	auto vis = VisibleDeckIndices();

	int hovIdx = (m_deckPreviewIdx >= 0) ? -1 : DeckHoverIndex(); // ホバー中のデッキindex

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
		by -= HoverLift::Get(bx, by, vis[slot] == hovIdx, 10.0f);   // ← ホバーで浮く（滑らかに）

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
		float bDy = -HoverLift::Get(bX, bY, bHov, 4.0f);
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

// イベントのカード選択で、このカードを選べるか（強化：強化できるもの／削除・変化：外せるもの）
static bool EventPickable(const std::string& type, const std::string& id)
{
	if (type == "upgradeCard") return CanUpgrade(id);
	if (type == "removeCard" || type == "transformCard") return CardDataBase::CanRemove(id);
	return true;
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

	// イベントエディタの「Save & Open in Game」
	std::string editorEvent;
	if (EditorHub::ConsumeOpenEvent(editorEvent))
	{
		m_eventOpen = true; m_eventId = editorEvent; m_eventResult = -1; m_eventPickType.clear();
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

void SceneManager::HandleInput()
{
	m_uiInput.Update();
	if (CardConfirm::Active())   // 強化・削除の確認（一番手前。他の操作は止める）
	{
		if (m_uiInput.GetMouseButtonTrigger(0)) m_eatRelease = true;    // 押した→この後の「離す」は窓のもの
		if (m_uiInput.GetMouseButtonRelease(0)) m_eatRelease = false;   // 窓の中で離し終えた
		CardConfirm::HandleInput(m_uiInput.GetMousePos(), m_uiInput.GetMouseButtonTrigger(0), m_screenWidth, m_screenHeight);
		m_swallowSceneClick = true;
		return;
	}
	if (m_eatRelease)   // 窓を閉じたクリックの「離す」：後ろの一覧に渡さない（離した瞬間に選ぶ一覧が、下のカードを選んでしまうため）
	{
		if (m_uiInput.GetMouseButtonRelease(0)) m_eatRelease = false;
		return;
	}
	{
		// カードの説明の上のクリックは説明が使う（シーンには渡さない）
		POINT tm = m_uiInput.GetMousePos();
		bool tclick = m_uiInput.GetMouseButtonTrigger(0);
		CardTooltip::SetMouse((float)tm.x, (float)tm.y, tclick);
		if (tclick && CardTooltip::MouseInPanel() && !TooltipBlocked()) { m_swallowSceneClick = true; return; }
	}
	if (AnyModalOpen()) m_swallowSceneClick = true;   // 開いている間に押されたクリックは、閉じた後もシーンに渡さない
	if (m_fadeState != Fade::None) return;   // フェード中は入力無効

	{
		// ホイールでもスクロール（デッキと同じ m_uiInput 経由・ドラッグと同じターゲットを共有）
		if (m_craftOpen || m_invOpen)
			m_matScrollTarget -= m_uiInput.GetMouseWheelDelta() * 0.5f;

		bool held = m_uiInput.GetMouseButtonPress(0);
		POINT mp = m_uiInput.GetMousePos();
		if (held && (m_deckOpen || m_craftOpen || m_invOpen))
		{
			if (!m_mDrag) { m_mDrag = true; m_mDragStartY = mp.y; m_mDragLastY = mp.y; m_mDragMoved = false; }
			int dy = mp.y - m_mDragLastY;
			if (dy != 0)
			{
				if (m_deckOpen) m_deckScrollTarget -= (float)dy;
				else            m_matScrollTarget -= (float)dy;   // クラフト / インベントリ
				m_mDragLastY = mp.y;
			}
			int md = mp.y - m_mDragStartY; if (md < 0) md = -md;
			if (md > 6) m_mDragMoved = true;   // 6px超で「ドラッグ」確定
		}
		else m_mDrag = false;
	}

	{
		static int _c = 0;
		if (++_c % 60 == 0) {
			char b[300];
			sprintf_s(b, "[IN] type=%d fade=%d tutO=%d tutD=%.2f cfx=%.2f rest=%d craft=%d ev=%d map=%d inv=%d deck=%d set=%d\n",
				(int)m_currentType, (int)m_fadeState, (int)m_tutorialOpen, m_tutorialDelay, m_craftFxTimer,
				(int)m_restOpen, (int)m_craftOpen, (int)m_eventOpen, (int)m_mapOpen, (int)m_invOpen, (int)m_deckOpen, (int)m_settingsOpen);
			OutputDebugStringA(b);
		}
	}

	if (m_tutorialOpen)
	{
		if (m_uiInput.GetMouseButtonTrigger(0))
		{
			Audio::PlaySE("Assets/Sound/se/click.mp3");
			m_tutorialPage++;
			if (m_tutorialPage >= (int)m_tutorialPages.size())
			{
				m_tutorialOpen = false;
				if (auto b = dynamic_cast<BattleScene*>(m_currentScene)) b->OnTutorialClosed();   // 保留していたレリックを発動
			}
		}
		return;
	}

	if (m_tutorialDelay > 0.0f) return;   // チュートリアル表示待ち中は入力を止める（勝利連打で飛ぶのを防ぐ）

	if (m_craftFxTimer > 0.0f) return;           // 演出中は入力停止

	if (BossRelicPickOpen())   // ボスレリックの選択（選ぶか「受け取らない」まで他の操作はしない）
	{
		if (m_uiInput.GetMouseButtonTrigger(0))
		{
			auto& pd = PlayerDataManager::GetData();
			POINT m = m_uiInput.GetMousePos();
			int n = (int)pd.bossRelicChoices.size();
			for (int i = 0; i < n; i++)
				if (BossRelicCardRect(i, n).has(m))
				{
					Audio::PlaySE("Assets/Sound/se/click.mp3");
					std::string offered;
					for (auto& c : pd.bossRelicChoices) { if (!offered.empty()) offered += ","; offered += c; }
					Telemetry::Instance().Log("boss_relic_pick", { {"chosen", pd.bossRelicChoices[i]}, {"offered", offered} });
					std::string id = pd.bossRelicChoices[i];
					pd.bossRelicChoices.clear();
					PlayerDataManager::AddRelic(id);   // 取得時の効果（最大HP・歩数、代償も）を適用して保存
					return;
				}
			if (BossRelicSkipRect().has(m))
			{
				Audio::PlaySE("Assets/Sound/se/click.mp3");
				Telemetry::Instance().Log("boss_relic_pick", { {"chosen", "(skip)"} });
				pd.bossRelicChoices.clear();
				PlayerDataManager::Save();
			}
		}
		return;
	}

	// 用語集（キーワードのクリックなど、どこからでも開ける）
	std::string glossId;
	if (Glossary::TakeRequest(glossId)) OpenGlossary(glossId);
	if (m_glossaryOpen) { HandleGlossaryInput(); return; }

	// 設定パネル（帯のある画面でもタイトルでも、開いている間は他を止める）
	if (m_settingsOpen) { HandleSettingsInput(); return; }

	if (HasTopBar())
	{
		POINT m = m_uiInput.GetMousePos();
		bool click = m_uiInput.GetMouseButtonTrigger(0);
		bool clickRel = m_uiInput.GetMouseButtonRelease(0) && !m_mDragMoved;  // タップ(離した瞬間・非ドラッグ)]::]
		float btnX = m_screenWidth - 220.0f, btnY = 5.0f, btnW = 100.0f, btnH = 30.0f;
		bool onDeckBtn = click && BarButtonRect(2).has(m);

		SettingsUI su = SettingsLayout();
		// ギアで開閉
		if (click && su.gear.has(m)) { Audio::PlaySE("Assets/Sound/se/click.mp3"); m_settingsOpen = !m_settingsOpen; return; }
		

		// Map（どの画面でも開ける。フィールドでもイベント・休憩中に先の道を確認できる）
		if (m_mapOpen) { if (click && BackBtnHit(m)) m_mapOpen = false; return; }
		{
			if (click && !m_deckOpen && !m_craftOpen && !m_invOpen
				&& BarButtonRect(0).has(m))
			{
				Audio::PlaySE("Assets/Sound/se/click.mp3");
				m_mapOpen = true; return;
			}
		}

		// Items（いつでも）
		if (m_invOpen)
		{
			if (clickRel && BackBtnHit(m)) { m_invOpen = false; return; }
			float x, y, w, h; GetCraftBtnRect(x, y, w, h);              // 「合成する」（戦闘中は押せない）
			if (clickRel && m_currentType != SceneType::Battle && m.x >= x && m.x <= x + w && m.y >= y && m.y <= y + h)
			{
				Audio::PlaySE("Assets/Sound/se/click.mp3");
				m_invOpen = false; m_craftOpen = true;
				m_craftBase.clear(); m_craftMods.clear(); m_craftEditIdx = -1; m_craftLocked = 0;
				m_matScroll = 0.0f; m_matScrollTarget = 0.0f;
			}
			return;
		}
		{
			if (click && !m_deckOpen && !m_craftOpen && !m_mapOpen
				&& BarButtonRect(1).has(m))
			{
				Audio::PlaySE("Assets/Sound/se/click.mp3");
				m_invOpen = true; m_matScroll = 0.0f; m_matScrollTarget = 0.0f; return;
			}
		}

		if (onDeckBtn && !m_deckOpen && !m_invOpen && !m_mapOpen && !m_craftOpen)
		{
			Audio::PlaySE("Assets/Sound/se/click.mp3");
			m_deckOpen = true;
			m_deckScroll = 0.0f; m_deckScrollTarget = 0.0f;
			return;
		}
		if (m_deckOpen)
		{
			m_deckScrollTarget -= m_uiInput.GetMouseWheelDelta() * 0.5f;
			const float OVER = 60.0f;
			float maxS = DeckMaxScroll();
			if (m_deckScrollTarget < 0.0f)        m_deckScrollTarget = 0.0f;
			if (m_deckScrollTarget > maxS + OVER) m_deckScrollTarget = maxS + OVER;
			if (clickRel)
			{
				// 強化後表示トグル（デッキ閲覧のみ・拡大中でない時）
				float ubX = 20.0f, ubY = m_screenHeight - 46.0f, ubW = 190.0f, ubH = 34.0f;
				if (m_deckPreviewIdx < 0 &&
					m.x >= ubX && m.x <= ubX + ubW && m.y >= ubY && m.y <= ubY + ubH)
				{
					m_deckShowUpgrade = !m_deckShowUpgrade; Audio::PlaySE("Assets/Sound/se/click.mp3"); return;
				}

				if (BackBtnHit(m))
				{
					Audio::PlaySE("Assets/Sound/se/click.mp3");
					m_deckPreviewIdx = -1; m_deckOpen = false; m_deckUpgradeMode = false;
					if (m_restActive) { m_restOpen = true; m_restArmed = false; }   // 休憩から来ていたら休憩へ戻す
					return;
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
								if (after != before) 
								{
									const std::string& after = PlayerDataManager::GetData().deck[m_deckPreviewIdx];
									if (after != before) { m_craftFxCard = after; m_craftFxTimer = CRAFT_FX_DURATION; Se::Play(Se::Upgrade); }
								}
								m_deckPreviewIdx = -1;
								return;
							}
						}
						m_deckPreviewIdx = -1; return;   // ボタン外で閉じる
					}
					int idx = GetDeckCardAt(m);
					if (idx >= 0)
					{
						Audio::PlaySE("Assets/Sound/se/click.mp3");
						std::string baseId = PlayerDataManager::GetData().deck[idx];
						if (!CanUpgrade(baseId)) { m_deckPreviewIdx = idx; return; }   // 強化できないカードは今まで通り拡大
						CardConfirm::Open(ConfirmKind::Upgrade, baseId, [this, idx, baseId]() {   // 強化前と強化後を見せて確認
							auto& deck = PlayerDataManager::GetData().deck;
							if (idx >= (int)deck.size() || deck[idx] != baseId) return;
							PlayerDataManager::UpgradeCard(idx);
							const std::string& after = PlayerDataManager::GetData().deck[idx];
							if (after != baseId) { m_craftFxCard = after; m_craftFxTimer = CRAFT_FX_DURATION; Se::Play(Se::Upgrade); }
							});
						return;
					}
					/*if (m_restActive) { m_deckOpen = false; m_deckUpgradeMode = false; m_restOpen = true; return; }
					m_deckUpgradeMode = false; return;*/
					return;
				}

				// 通常デッキ：拡大中はクリックで閉じる（ボタンなし）
				if (m_deckPreviewIdx >= 0) { m_deckPreviewIdx = -1; return; }
				int idx = GetDeckCardAt(m);
				if (idx >= 0) { m_deckPreviewIdx = idx; m_previewShowUpgrade = m_deckShowUpgrade; Audio::PlaySE("Assets/Sound/se/click.mp3"); return; }
			}
			return;
		}
	}

	if (HasTopBar() && m_currentType != SceneType::Battle)
	{
		POINT m = m_uiInput.GetMousePos();
		bool click = m_uiInput.GetMouseButtonTrigger(0);
		bool clickRel = m_uiInput.GetMouseButtonRelease(0) && !m_mDragMoved;
		if (m_craftOpen) { if (clickRel) HandleCraftClick(m); return; }
	}

	if (m_restOpen)
	{
		if (m_uiInput.GetMouseButtonTrigger(0)) m_restArmed = true;   // 押下が休憩画面内で始まった時だけ武装
		if (m_restArmed && m_uiInput.GetMouseButtonRelease(0))
		{
			m_restArmed = false;
			if (BackBtnHit(m_uiInput.GetMousePos())) { FinishRest(); return; }  // 戻る＝休憩を終える
			HandleRestClick(m_uiInput.GetMousePos());
		}
		return;
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
						Audio::PlaySE("Assets/Sound/se/click.mp3");
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
						auto start = [this, idx, cur]() {
							if (m_eventPickType == "removeCard") m_eventPickAnimTo = "";
							else if (m_eventPickType == "transformCard") m_eventPickAnimTo = CardDataBase::RandomCard();
							else if (m_eventPickType == "upgradeCard") m_eventPickAnimTo = CanUpgrade(cur) ? (cur + "+") : cur;
							m_eventPickAnimIdx = idx;
							m_eventPickAnimTimer = EVENT_PICK_ANIM_DUR;
							if (m_eventPickType == "upgradeCard" && m_eventPickAnimTo != cur) Se::Play(Se::Upgrade);   // イベントでの強化
							};
						if (m_eventPickType == "removeCard") CardConfirm::Open(ConfirmKind::Remove, cur, start);   // 確認してから
						else if (m_eventPickType == "upgradeCard" && CanUpgrade(cur)) CardConfirm::Open(ConfirmKind::Upgrade, cur, start);
						else start();
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
					m_eventId = "treasure_field";   // 財宝マスの中身を開く（呪いを払った後なので、デメリットなしで選ばせる）
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
						Telemetry::Instance().Log("event_choice", { {"event", m_eventId}, {"choice", i} });
						ApplyOutcomes(e->choices[i]);
						std::string pick = EventPickerType(e->choices[i]);
						if (!pick.empty()) { m_eventPickType = pick; m_eventPickChoice = i; m_eventPickScroll.Reset(); }
						else m_eventResult = i;
						break;
					}
				}
			}
		}
		return;
	}

	// m_uiInputが消費したホイールをシーンへ戻す（ズーム等が効くように）
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
	if (pg.enemyPanel)
		if (auto battle = dynamic_cast<BattleScene*>(m_currentScene))
		{
			float x, y, w, h;
			if (battle->GetEnemyPanelRect(x, y, w, h)) { hx = x; hy = y; hw = w; hh = h; }
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
		DrawFrame(x, y, w, h, XMFLOAT4(1.0f, 0.85f, 0.2f, 1.0f));
	}
	else m_uiSprite->DrawSprite(white, 0, 0, sw, sh, 0.0f, dim);

	// 攻撃範囲の説明ページ：危険マークを中央に大きく見せる
	if (pg.showHitmark)
	{
		auto hm = TextureManager::Get("ui_hitmark");
		if (hm)
		{
			float sz = 120.0f;
			m_uiSprite->DrawSprite(hm, sw / 2.0f - sz / 2.0f, sh * 0.40f - sz / 2.0f,
				sz, sz, 0.0f, XMFLOAT4(1, 1, 1, 1));
		}
	}

	// テキストボックス（対象が上なら下側、下なら上側に置く）
	float bw = 560.0f, tw = bw - 50.0f, bx = sw / 2.0f - bw / 2.0f;
	float bh = max(150.0f, 25.0f + m_textRenderer->Measure(pg.text.c_str(), 22.0f, FontRole::Body, tw).height + 50.0f);   // 本文＋「クリックで次へ」
	float by = (pg.hy + pg.hh * 0.5f < sh * 0.5f) ? sh - bh - 60.0f : 80.0f;
	UiWindow::Draw(m_uiSprite, white, bx - 6, by - 6, bw + 12, bh + 12);
	m_uiSprite->End();

	m_textRenderer->Begin();
	m_textRenderer->DrawTextBox(pg.text.c_str(), bx + 25.0f, by + 25.0f, tw, 22.0f, D2D1::ColorF(1, 1, 1));
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

void SceneManager::DrawFrame(float x, float y, float w, float h, const XMFLOAT4& c)
{
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	UiWindow::Frame(m_uiSprite, TextureManager::Get("white"), x, y, w, h, c);
}

bool SceneManager::CamDemoActive() const
{
	auto b = dynamic_cast<BattleScene*>(m_currentScene);
	return b && b->CamDemoActive();
}

void SceneManager::DrawHint()
{
	const HintDef* h = Hint::Current();
	if (!h) return;
	float a = Hint::Alpha();
	if (h->hw > 0.0f)   // 指している場所を金枠で脈動
	{
		m_uiSprite->Begin();
		DrawFrame(h->hx, h->hy, h->hw, h->hh,
			XMFLOAT4(1.0f, 0.85f, 0.2f, a * (0.6f + 0.4f * sinf(m_uiTime * 6.0f))));
		m_uiSprite->End();
	}
	DrawGuideBox(h->text, a, nullptr, nullptr);
}

// 説明の窓（ヒント・カメラのデモで共通）：画面上寄りの中央、左の金帯が目印。icon は文の左、note は右下に小さく
void SceneManager::DrawGuideBox(const wchar_t* text, float a, const char* icon, const wchar_t* note)
{
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	const float ICON = 56.0f, NOTE_H = 18.0f;
	float w = 520.0f;
	float tx = 22.0f + (icon ? ICON + 12.0f : 0.0f);   // 文の左端（アイコンがあればその右）
	float tw = w - tx - 12.0f;                         // 文の幅。長い文はこの幅で折り返す
	float th = m_textRenderer->Measure(text, 18.0f, FontRole::Body, tw).height;
	float body = 26.0f + th;                           // 注記を除いた高さ
	if (icon && body < ICON + 20.0f) body = ICON + 20.0f;
	float bh = body + (note ? NOTE_H : 0.0f);
	float x = (m_screenWidth - w) / 2.0f, y = 110.0f;

	m_uiSprite->Begin();
	UiWindow::Draw(m_uiSprite, white, x, y, w, bh, XMFLOAT4(1, 1, 1, a));
	m_uiSprite->DrawSprite(white, x + 8, y + 8, 4, bh - 16, 0.0f, XMFLOAT4(1.0f, 0.85f, 0.2f, a));   // 左の金帯＝ヒントの目印
	if (icon)
		m_uiSprite->DrawSprite(TextureManager::Get(icon), x + 22.0f, y + (body - ICON) / 2.0f, ICON, ICON, 0.0f, XMFLOAT4(1, 1, 1, a));
	m_uiSprite->End();

	m_textRenderer->Begin();
	m_textRenderer->DrawTextBox(text, x + tx, y + (body - th) / 2.0f, tw, 18.0f, D2D1::ColorF(1, 1, 1, a));
	if (note)
	{
		float nw = m_textRenderer->Measure(note, 12.0f).width;
		m_textRenderer->DrawText(note, x + w - 16.0f - nw, y + bh - NOTE_H - 6.0f, 12.0f, D2D1::ColorF(0.75f, 0.72f, 0.65f, a));
	}
	m_textRenderer->End();
}

void SceneManager::HandleSettingsInput()
{
	POINT m = m_uiInput.GetMousePos();
	bool click = m_uiInput.GetMouseButtonTrigger(0);
	SettingsUI su = SettingsLayout();

	// 確認ダイアログ表示中は はい/いいえ だけ受け付ける
	if (m_confirmAction != 0)
	{
		if (click)
		{
			if (su.cfmYes.has(m))
			{
				Audio::PlaySE("Assets/Sound/se/click.mp3");
				if (m_confirmAction == 1) { PostQuitMessage(0); }
				else if (m_confirmAction == 2)
				{
					m_settingsOpen = false; m_confirmAction = 0; Settings::Save();
					ChangeScene(SceneType::Title); return;
				}
				m_confirmAction = 0;
			}
			else if (su.cfmNo.has(m)) { Audio::PlaySE("Assets/Sound/se/click.mp3"); m_confirmAction = 0; }
		}
		return;
	}
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
		else if (su.aimCam.has(m)) { s.aimCamera = !s.aimCamera; Settings::Save(); Audio::PlaySE("Assets/Sound/se/click.mp3"); }
		else if (su.glossary.has(m)) { Audio::PlaySE("Assets/Sound/se/click.mp3"); OpenGlossary(""); }
		else if (su.hasTitle && su.title.has(m)) { Audio::PlaySE("Assets/Sound/se/click.mp3"); m_confirmAction = 2; }
		else if (su.gameEnd.has(m)) { Audio::PlaySE("Assets/Sound/se/click.mp3"); m_confirmAction = 1; }
		else if (su.close.has(m) || !su.panel.has(m)) { m_settingsOpen = false; m_dragSlider = -1; Settings::Save(); Audio::PlaySE("Assets/Sound/se/click.mp3"); }
	}
}

// ---- 用語集 ----
struct GlossUI { URect panel, close, back, cat0, list, detail; float catStep, rowH; int rows; };
static GlossUI GlossLayout(int sw, int sh)
{
	GlossUI g;
	float pw = 960.0f, ph = 650.0f, px = (sw - pw) / 2.0f, py = (sh - ph) / 2.0f;
	g.panel = { px, py, pw, ph };
	g.cat0 = { px + 20.0f, py + 64.0f, 150.0f, 38.0f }; g.catStep = 44.0f;
	g.list = { px + 186.0f, py + 64.0f, 262.0f, 510.0f }; g.rowH = 35.0f; g.rows = (int)(g.list.h / g.rowH);
	g.detail = { px + 464.0f, py + 64.0f, 476.0f, 510.0f };
	g.close = { px + pw / 2.0f - 70.0f, py + ph - 52.0f, 140.0f, 38.0f };
	g.back  = { g.close.x - 150.0f, g.close.y, 140.0f, 38.0f };   // 閉じるの左
	return g;
}

// 今のカテゴリの用語（Glossary::All() の添字）
std::vector<int> SceneManager::GlossaryItems() const
{
	std::vector<int> v;
	auto& cats = Glossary::Categories();
	auto& all = Glossary::All();
	if (m_glossCat < 0 || m_glossCat >= (int)cats.size()) return v;
	for (int i = 0; i < (int)all.size(); i++)
		if (all[i].category == cats[m_glossCat] && !Glossary::Locked(all[i])) v.push_back(i);   // まだ出会っていない敵の用語は隠す
	return v;
}

// 説明欄の並び（動画・説明文の上端・関連ボタン）。描画とクリック判定で共有
// 説明文が長くて関連ボタンまで入らない時は、動画を16:9のまま小さくして場所を空ける
SceneManager::GlossDetail SceneManager::GlossDetailLayout(const GlossaryEntry& e, const URect& d)
{
	GlossDetail L;
	auto& out = L.related;
	float x = d.x + 12.0f, y = 0.0f;   // 関連ボタン：まず上端0で並べて、行数（全体の高さ）を出す
	for (auto& id : e.related)
	{
		const GlossaryEntry* r = Glossary::Get(id);
		if (!r || Glossary::Locked(*r)) continue;
		float w = m_textRenderer->Measure(r->name.c_str(), 16.0f, FontRole::Body).width + 28.0f;
		if (x + w > d.x + d.w - 12.0f) { x = d.x + 12.0f; y += 36.0f; }
		out.push_back({ { x, y, w, 32.0f }, id });
		x += w + 8.0f;
	}
	float relH  = out.empty() ? 0.0f : y + 32.0f + 40.0f;   // ボタン＋「関連」の文字ぶん
	float descH = m_textRenderer->Measure(e.desc.c_str(), 17.0f, FontRole::Body, d.w - 24.0f).height;
	float bottom = d.y + d.h - 6.0f;
	if (!e.clip.empty())
	{
		float room = bottom - relH - descH - 12.0f - (d.y + 54.0f);   // 動画に使える高さ
		float vh = (std::max)(150.0f, (std::min)(252.0f, room));
		float vw = vh * 16.0f / 9.0f;
		L.video   = { d.x + 14.0f + (448.0f - vw) / 2.0f, d.y + 54.0f, vw, vh };   // 元の幅の中で中央
		L.descTop = L.video.y + vh + 12.0f;
	}
	else L.descTop = d.y + (e.icon.empty() ? 52.0f : 96.0f);
	// 関連ボタンを置く高さ：説明文の下。動画が最小でも入らない時は、下の端に合わせて上げる（保険）
	float below = L.descTop + descH + 40.0f;
	float fit = bottom - (y + 32.0f);
	float y0 = (below < fit) ? below : fit;
	for (auto& o : out) o.first.y += y0;
	return L;
}

// id のページを開く（空なら先頭）
void SceneManager::OpenGlossary(const std::string& id)
{
	m_glossaryOpen = true;
	m_glossCat = 0; m_glossSel = 0; m_glossScroll = 0;
	const GlossaryEntry* e = id.empty() ? nullptr : Glossary::Get(id);
	if (!e || Glossary::Locked(*e)) return;
	auto& cats = Glossary::Categories();
	for (int c = 0; c < (int)cats.size(); c++) if (cats[c] == e->category) m_glossCat = c;
	auto items = GlossaryItems();
	for (int i = 0; i < (int)items.size(); i++) if (Glossary::All()[items[i]].id == id) m_glossSel = i;
	GlossUI g = GlossLayout(m_screenWidth, m_screenHeight);
	if (m_glossSel >= g.rows) m_glossScroll = m_glossSel - g.rows + 1;
}

void SceneManager::HandleGlossaryInput()
{
	POINT m = m_uiInput.GetMousePos();
	bool click = m_uiInput.GetMouseButtonTrigger(0);
	GlossUI g = GlossLayout(m_screenWidth, m_screenHeight);
	auto items = GlossaryItems();
	int maxScroll = (int)items.size() - g.rows; if (maxScroll < 0) maxScroll = 0;

	int wheel = m_uiInput.GetMouseWheelDelta();
	if (wheel != 0 && g.list.has(m))
	{
		m_glossScroll += (wheel > 0) ? -1 : 1;
		if (m_glossScroll < 0) m_glossScroll = 0;
		if (m_glossScroll > maxScroll) m_glossScroll = maxScroll;
	}
	auto closeGlossary = [&]() { m_glossaryOpen = false; m_glossBack.clear(); if (m_glossVideo) m_glossVideo->Close(); };   // 閉じる時は動画も止め、戻るの記録も消す
	auto pushPage = [&]() {                                    // ページを切り替える直前に、今のページを記録
		if (m_glossBack.size() >= 50) m_glossBack.erase(m_glossBack.begin());
		m_glossBack.push_back({ m_glossCat, m_glossSel, m_glossScroll });
	};
	auto goBack = [&]() {
		if (m_glossBack.empty()) return false;
		GlossPage p = m_glossBack.back(); m_glossBack.pop_back();
		m_glossCat = p.cat; m_glossSel = p.sel; m_glossScroll = p.scroll;
		Audio::PlaySE("Assets/Sound/se/click.mp3");
		return true;
	};
	if (m_uiInput.GetKeyTrigger(VK_ESCAPE)) { closeGlossary(); return; }
	if (m_uiInput.GetKeyTrigger(VK_BACK) && goBack()) return;
	if (!click) return;
	if (g.back.has(m) && goBack()) return;

	if (g.close.has(m) || !g.panel.has(m))
	{
		Audio::PlaySE("Assets/Sound/se/click.mp3");
		closeGlossary();
		return;
	}
	auto& cats = Glossary::Categories();
	for (int c = 0; c < (int)cats.size(); c++)
	{
		URect r = { g.cat0.x, g.cat0.y + c * g.catStep, g.cat0.w, g.cat0.h };
		if (r.has(m)) { Audio::PlaySE("Assets/Sound/se/click.mp3"); if (c != m_glossCat) pushPage(); m_glossCat = c; m_glossSel = 0; m_glossScroll = 0; return; }
	}
	for (int row = 0; row < g.rows; row++)
	{
		int i = m_glossScroll + row;
		if (i >= (int)items.size()) break;
		URect r = { g.list.x, g.list.y + row * g.rowH, g.list.w, g.rowH - 3.0f };
		if (r.has(m)) { Audio::PlaySE("Assets/Sound/se/click.mp3"); if (i != m_glossSel) pushPage(); m_glossSel = i; return; }
	}
	if (m_glossSel >= 0 && m_glossSel < (int)items.size())
	{
		const GlossaryEntry& e = Glossary::All()[items[m_glossSel]];
		GlossDetail L = GlossDetailLayout(e, g.detail);
		for (auto& [r, id] : L.related)
			if (r.has(m)) { Audio::PlaySE("Assets/Sound/se/click.mp3"); pushPage(); OpenGlossary(id); return; }
		auto hits = Glossary::FindTerms(e.desc, e.name);             // 説明文の中の用語
		float top = L.descTop;
		int h = m_textRenderer->HitTestSpans(e.desc.c_str(), g.detail.x + 12.0f, top, g.detail.w - 24.0f, 17.0f,
			GlossaryLink::Spans(hits), (float)m.x, (float)m.y);
		if (h >= 0) { Audio::PlaySE("Assets/Sound/se/click.mp3"); pushPage(); OpenGlossary(hits[h].id); return; }
	}
}

void SceneManager::DrawGlossary()
{
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	GlossUI g = GlossLayout(m_screenWidth, m_screenHeight);
	POINT mp = m_uiInput.GetMousePos();
	auto& cats = Glossary::Categories();
	auto& all = Glossary::All();
	auto items = GlossaryItems();
	if (m_glossSel >= (int)items.size()) m_glossSel = 0;
	const GlossaryEntry* sel = items.empty() ? nullptr : &all[items[m_glossSel]];
	GlossDetail lay;
	if (sel) lay = GlossDetailLayout(*sel, g.detail);
	auto& related = lay.related;

	m_uiSprite->Begin();
	m_uiSprite->DrawSprite(white, 0, 0, (float)m_screenWidth, (float)m_screenHeight, 0.0f, XMFLOAT4(0, 0, 0, 0.6f)); // 暗幕
	UiWindow::Draw(m_uiSprite, white, g.panel.x, g.panel.y, g.panel.w, g.panel.h);
	for (int c = 0; c < (int)cats.size(); c++)
	{
		URect r = { g.cat0.x, g.cat0.y + c * g.catStep, g.cat0.w, g.cat0.h };
		XMFLOAT4 col = (c == m_glossCat) ? XMFLOAT4(0.55f, 0.45f, 0.25f, 1)
			: r.has(mp) ? XMFLOAT4(0.32f, 0.36f, 0.5f, 1) : XMFLOAT4(0.22f, 0.24f, 0.34f, 1);
		UiWindow::Button(m_uiSprite, white, r.x, r.y, r.w, r.h, col);
	}
	UiWindow::Draw(m_uiSprite, white, g.list.x - 6.0f, g.list.y - 6.0f, g.list.w + 12.0f, g.list.h + 12.0f);
	for (int row = 0; row < g.rows; row++)
	{
		int i = m_glossScroll + row;
		if (i >= (int)items.size()) break;
		URect r = { g.list.x, g.list.y + row * g.rowH, g.list.w, g.rowH - 3.0f };
		if (i == m_glossSel)   m_uiSprite->DrawSprite(white, r.x, r.y, r.w, r.h, 0.0f, XMFLOAT4(0.55f, 0.45f, 0.25f, 0.55f));
		else if (r.has(mp))    m_uiSprite->DrawSprite(white, r.x, r.y, r.w, r.h, 0.0f, XMFLOAT4(0.4f, 0.45f, 0.6f, 0.35f));
	}
	UiWindow::Draw(m_uiSprite, white, g.detail.x, g.detail.y, g.detail.w, g.detail.h);
	if (sel && sel->clip.empty() && !sel->icon.empty())
		if (auto tex = TextureManager::Get(sel->icon))
			m_uiSprite->DrawSprite(tex, g.detail.x + 14.0f, g.detail.y + 14.0f, 64.0f, 64.0f, 0.0f, XMFLOAT4(1, 1, 1, 1));

	// 動画：選んでいる用語の分だけ開いてループ再生
	std::string clipPath = sel ? sel->clip : "";
	if (!m_glossVideo) m_glossVideo = std::make_unique<VideoClip>();
	if (m_glossVideo->Path() != clipPath)
	{
		m_glossVideo->Close();
		if (!clipPath.empty()) m_glossVideo->Open(g_pd3dDevice, clipPath);
		m_glossVideoTick = GetTickCount64();
	}
	if (!clipPath.empty())
	{
		ULONGLONG now = GetTickCount64();
		m_glossVideo->Update(g_pImmediateContext, (now - m_glossVideoTick) / 1000.0);
		m_glossVideoTick = now;
		URect v = lay.video;
		m_uiSprite->DrawSprite(white, v.x - 2.0f, v.y - 2.0f, v.w + 4.0f, v.h + 4.0f, 0.0f, XMFLOAT4(0.8f, 0.66f, 0.32f, 1));   // 金の縁
		if (auto srv = m_glossVideo->SRV())
			m_uiSprite->DrawSprite(srv, v.x, v.y, v.w, v.h, 0.0f, XMFLOAT4(1, 1, 1, 1));
		else
			m_uiSprite->DrawSprite(white, v.x, v.y, v.w, v.h, 0.0f, XMFLOAT4(0.05f, 0.05f, 0.08f, 1));
	}
	for (auto& [r, id] : related)
		UiWindow::Button(m_uiSprite, white, r.x, r.y, r.w, r.h,
			r.has(mp) ? XMFLOAT4(0.35f, 0.5f, 0.7f, 1) : XMFLOAT4(0.25f, 0.28f, 0.4f, 1));
	UiWindow::Button(m_uiSprite, white, g.close.x, g.close.y, g.close.w, g.close.h,
		g.close.has(mp) ? XMFLOAT4(0.5f, 0.35f, 0.35f, 1) : XMFLOAT4(0.35f, 0.25f, 0.25f, 1));
	bool canBack = !m_glossBack.empty();                    // 戻る（記録がない時は暗く）
	UiWindow::Button(m_uiSprite, white, g.back.x, g.back.y, g.back.w, g.back.h,
		!canBack ? XMFLOAT4(0.18f, 0.18f, 0.2f, 1) : g.back.has(mp) ? XMFLOAT4(0.35f, 0.5f, 0.7f, 1) : XMFLOAT4(0.25f, 0.28f, 0.4f, 1));
	m_uiSprite->End();

	m_textRenderer->Begin();
	const D2D1_COLOR_F W = D2D1::ColorF(1, 1, 1), GOLD = D2D1::ColorF(1.0f, 0.85f, 0.4f);
	m_textRenderer->DrawTextCentered(L"用語集", g.panel.x + g.panel.w / 2.0f, g.panel.y + 12.0f, g.panel.w, 26.0f,
		D2D1::ColorF(1, 0.9f, 0.6f), 40.0f);
	for (int c = 0; c < (int)cats.size(); c++)
		m_textRenderer->DrawTextCentered(cats[c].c_str(), g.cat0.x + g.cat0.w / 2.0f, g.cat0.y + c * g.catStep,
			g.cat0.w, 18.0f, W, g.cat0.h);
	for (int row = 0; row < g.rows; row++)
	{
		int i = m_glossScroll + row;
		if (i >= (int)items.size()) break;
		float y = g.list.y + row * g.rowH;
		m_textRenderer->DrawText(all[items[i]].name.c_str(), g.list.x + 12.0f, y + 5.0f, 17.0f, i == m_glossSel ? GOLD : W);
	}
	if ((int)items.size() > g.rows)   // まだ下（上）にある時の目印
	{
		if (m_glossScroll > 0)
			m_textRenderer->DrawText(L"▲", g.list.x + g.list.w - 22.0f, g.list.y - 2.0f, 14.0f, GOLD);
		if (m_glossScroll + g.rows < (int)items.size())
			m_textRenderer->DrawText(L"▼", g.list.x + g.list.w - 22.0f, g.list.y + g.list.h - 18.0f, 14.0f, GOLD);
	}
	if (sel)
	{
		float tx = g.detail.x + ((sel->icon.empty() || !sel->clip.empty()) ? 14.0f : 90.0f);
		{
			TextRenderer::RoleScope heading(*m_textRenderer, FontRole::Heading);
			m_textRenderer->DrawText(sel->name.c_str(), tx, g.detail.y + 18.0f, 26.0f, GOLD);
		}
		float top = lay.descTop;
		auto hits = Glossary::FindTerms(sel->desc, sel->name);       // 用語は色付き、マウスが乗ると明るく
		int hov = m_textRenderer->HitTestSpans(sel->desc.c_str(), g.detail.x + 12.0f, top, g.detail.w - 24.0f, 17.0f,
			GlossaryLink::Spans(hits), (float)mp.x, (float)mp.y);
		m_textRenderer->DrawTextSpans(sel->desc.c_str(), g.detail.x + 12.0f, top, g.detail.w - 24.0f, 17.0f, W,
			GlossaryLink::Spans(hits, hov));
		if (!related.empty())
			m_textRenderer->DrawText(L"関連", related[0].first.x, related[0].first.y - 22.0f, 14.0f,
				D2D1::ColorF(0.7f, 0.75f, 0.85f));
		for (auto& [r, id] : related)
			if (const GlossaryEntry* re = Glossary::Get(id))
				m_textRenderer->DrawTextCentered(re->name.c_str(), r.x + r.w / 2.0f, r.y, r.w, 16.0f, W, r.h);
	}
	m_textRenderer->DrawTextCentered(L"閉じる", g.close.x + g.close.w / 2.0f, g.close.y, g.close.w, 20.0f, W, g.close.h);
	m_textRenderer->DrawTextCentered(L"◀ 戻る", g.back.x + g.back.w / 2.0f, g.back.y, g.back.w, 20.0f,
		canBack ? W : D2D1::ColorF(0.5f, 0.5f, 0.5f), g.back.h);
	m_textRenderer->End();
}

void SceneManager::DrawSettings()
{
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	SettingsUI u = SettingsLayout();
	// 描く位置はホバーで少し持ち上げる（判定と色は元の u のまま）
	SettingsUI d = u;
	for (URect* r : { &d.disp, &d.shake, &d.aimCam, &d.glossary, &d.title, &d.gameEnd, &d.close, &d.cfmYes, &d.cfmNo })
		*r = Lifted(*r);
	POINT mp = m_uiInput.GetMousePos();
	auto& s = Settings::Get();

	m_uiSprite->Begin();
	m_uiSprite->DrawSprite(white, 0, 0, (float)m_screenWidth, (float)m_screenHeight, 0.0f, XMFLOAT4(0, 0, 0, 0.6f)); // 暗幕
	UiWindow::Draw(m_uiSprite, white, u.panel.x, u.panel.y, u.panel.w, u.panel.h);

	UiWindow::Button(m_uiSprite, white, d.disp.x, d.disp.y, d.disp.w, d.disp.h,
		u.disp.has(mp) ? XMFLOAT4(0.35f, 0.5f, 0.7f, 1) : XMFLOAT4(0.25f, 0.28f, 0.4f, 1));

	auto slider = [&](const URect& t, float val) {
		m_uiSprite->DrawSprite(white, t.x, t.y, t.w, t.h, 0.0f, XMFLOAT4(0.15f, 0.15f, 0.2f, 1));      // 溝
		m_uiSprite->DrawSprite(white, t.x, t.y, t.w * val, t.h, 0.0f, XMFLOAT4(0.3f, 0.6f, 0.9f, 1));  // 塗り
		m_uiSprite->DrawSprite(white, t.x + t.w * val - 5.0f, t.y - 5.0f, 10.0f, t.h + 10.0f, 0.0f, XMFLOAT4(0.9f, 0.9f, 1.0f, 1)); // つまみ
		};
	slider(u.bgmTrack, s.bgmVolume);
	slider(u.seTrack, s.seVolume);

	UiWindow::Button(m_uiSprite, white, d.shake.x, d.shake.y, d.shake.w, d.shake.h,
		s.screenShake ? XMFLOAT4(0.3f, 0.55f, 0.3f, 1) : XMFLOAT4(0.45f, 0.3f, 0.3f, 1));
	UiWindow::Button(m_uiSprite, white, d.aimCam.x, d.aimCam.y, d.aimCam.w, d.aimCam.h,
		s.aimCamera ? XMFLOAT4(0.3f, 0.55f, 0.3f, 1) : XMFLOAT4(0.45f, 0.3f, 0.3f, 1));
	UiWindow::Button(m_uiSprite, white, d.glossary.x, d.glossary.y, d.glossary.w, d.glossary.h,
		u.glossary.has(mp) ? XMFLOAT4(0.55f, 0.45f, 0.25f, 1) : XMFLOAT4(0.4f, 0.33f, 0.2f, 1));
	UiWindow::Button(m_uiSprite, white, d.title.x, d.title.y, d.title.w, d.title.h,
		u.title.has(mp) ? XMFLOAT4(0.35f, 0.5f, 0.7f, 1) : XMFLOAT4(0.25f, 0.28f, 0.4f, 1));
	UiWindow::Button(m_uiSprite, white, d.gameEnd.x, d.gameEnd.y, d.gameEnd.w, d.gameEnd.h,
		u.gameEnd.has(mp) ? XMFLOAT4(0.75f, 0.30f, 0.30f, 1) : XMFLOAT4(0.50f, 0.24f, 0.24f, 1));
	UiWindow::Button(m_uiSprite, white, d.close.x, d.close.y, d.close.w, d.close.h,
		u.close.has(mp) ? XMFLOAT4(0.5f, 0.35f, 0.35f, 1) : XMFLOAT4(0.35f, 0.25f, 0.25f, 1));
	m_uiSprite->End();

	m_textRenderer->Begin();
	// 文字はボタンの枠の中央に（手打ちのオフセットだと文字の幅でずれる）
	auto inBtn = [&](const wchar_t* t, const URect& r, float size, D2D1_COLOR_F c) {
		m_textRenderer->DrawTextCentered(t, r.x + r.w / 2.0f, r.y, r.w, size, c, r.h);
		};
	// 左の項目名は左揃えのまま、その行の縦の中央に
	auto rowLabel = [&](const wchar_t* t, const URect& row, float size) {
		float cy = row.y + row.h / 2.0f;
		float maxW = row.x - (u.panel.x + 40.0f) - 12.0f;            // ボタンの手前まで
		float w = m_textRenderer->Measure(t, size, FontRole::Body).width;
		if (w > maxW) size *= maxW / w;                               // 入りきらない時だけ縮める
		m_textRenderer->DrawText(t, u.panel.x + 40.0f, cy - size * 0.62f, size, D2D1::ColorF(1, 1, 1));
		};
	const D2D1_COLOR_F W = D2D1::ColorF(1, 1, 1);

	inBtn(L"設定", { u.panel.x, u.panel.y + 10.0f, u.panel.w, 36.0f }, 26, D2D1::ColorF(1, 0.9f, 0.6f));

	rowLabel(L"表示", u.disp, 20);
	inBtn(s.displayMode == DisplayMode::Borderless ? L"全画面" : L"ウィンドウ", d.disp, 20, W);
	rowLabel(L"BGM", u.bgmTrack, 20);
	rowLabel(L"SE", u.seTrack, 20);
	rowLabel(L"画面シェイク", u.shake, 18);
	inBtn(s.screenShake ? L"ON" : L"OFF", d.shake, 20, W);
	rowLabel(L"移動のカメラ追従", u.aimCam, 18);
	inBtn(s.aimCamera ? L"ON" : L"OFF", d.aimCam, 20, W);
	inBtn(L"用語集", d.glossary, 20, W);
	inBtn(L"タイトルに戻る", d.title, 20, W);
	inBtn(L"ゲーム終了", d.gameEnd, 20, D2D1::ColorF(1, 0.9f, 0.9f));
	inBtn(L"閉じる", d.close, 20, W);

	// 音量の%（つまみの右、行の縦の中央）
	wchar_t b[16];
	auto pct = [&](float v, const URect& t) {
		swprintf_s(b, L"%d%%", (int)(v * 100 + 0.5f));
		m_textRenderer->DrawText(b, t.x + t.w + 14.0f, t.y + t.h / 2.0f - 16 * 0.62f, 16, D2D1::ColorF(0.8f, 0.9f, 1));
		};
	pct(s.bgmVolume, u.bgmTrack);
	pct(s.seVolume, u.seTrack);
	m_textRenderer->End();

	if (m_confirmAction != 0)
	{
		m_uiSprite->Begin();
		m_uiSprite->DrawSprite(white, 0, 0, (float)m_screenWidth, (float)m_screenHeight, 0.0f, XMFLOAT4(0, 0, 0, 0.5f));
		UiWindow::Draw(m_uiSprite, white, u.cfmPanel.x, u.cfmPanel.y, u.cfmPanel.w, u.cfmPanel.h);
		UiWindow::Button(m_uiSprite, white, d.cfmYes.x, d.cfmYes.y, d.cfmYes.w, d.cfmYes.h,
			u.cfmYes.has(mp) ? XMFLOAT4(0.75f, 0.30f, 0.30f, 1) : XMFLOAT4(0.50f, 0.24f, 0.24f, 1));
		UiWindow::Button(m_uiSprite, white, d.cfmNo.x, d.cfmNo.y, d.cfmNo.w, d.cfmNo.h,
			u.cfmNo.has(mp) ? XMFLOAT4(0.35f, 0.45f, 0.6f, 1) : XMFLOAT4(0.25f, 0.30f, 0.42f, 1));
		m_uiSprite->End();

		m_textRenderer->Begin();
		const wchar_t* msg = (m_confirmAction == 1) ? L"ゲームを終了しますか？" : L"タイトルに戻りますか？";
		m_textRenderer->DrawTextCentered(msg, u.cfmPanel.x + u.cfmPanel.w / 2.0f, u.cfmPanel.y + 30.0f, u.cfmPanel.w, 22, D2D1::ColorF(1, 1, 1), 50.0f);
		m_textRenderer->DrawTextCentered(L"はい", d.cfmYes.x + d.cfmYes.w / 2.0f, d.cfmYes.y, d.cfmYes.w, 20, D2D1::ColorF(1, 0.9f, 0.9f), d.cfmYes.h);
		m_textRenderer->DrawTextCentered(L"いいえ", d.cfmNo.x + d.cfmNo.w / 2.0f, d.cfmNo.y, d.cfmNo.w, 20, D2D1::ColorF(1, 1, 1), d.cfmNo.h);
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
			{ L"【コア】カードの土台。何を作るかが決まる",                    482, 262, 316, 54 },
			{ L"【素材】コアに足す強化部品\n休憩マスのクラフトで組み合わせる", 486, 318, 308, 50 },
				}, 1.0f);
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

	if (!m_tutorialOpen && !CamDemoActive()) Hint::Update(deltaTime);   // ページ型の説明中・カメラのデモ中は止める

	if (m_eventOpen)   // イベント中の揺れと表示HP（戦闘中は BattleScene が自前で回す）  ← ここに追加
	{
		ScreenShake::Update(deltaTime);
		if (m_eventHpDisplay >= 0.0f)
			m_eventHpDisplay = HpBar::StepDisplay(m_eventHpDisplay, PlayerDataManager::GetData().hp, deltaTime);
	}

	if (m_craftFxTimer > 0.0f)
	{
		m_craftFxTimer -= deltaTime;
		if (m_craftFxTimer <= 0.0f)
		{
			m_craftFxTimer = 0.0f;
			if (!m_craftFxQueue.empty()) PlayNextCardFx();   // 続きのカード
			else if (m_restActive) FinishRest();             // 休憩でのクラフトは演出後に終了
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
					m_eventGains.push_back({ "relic", m_eventPickAnimTo, 1 });   // 結果画面に「何に変わったか」を出す
				}
			}
			else
			{
				auto& deck = PlayerDataManager::GetData().deck;
				if (m_eventPickType == "removeCard") PlayerDataManager::RemoveCard(m_eventPickAnimIdx);
				else if (m_eventPickAnimIdx < (int)deck.size())
				{
					deck[m_eventPickAnimIdx] = m_eventPickAnimTo;
					PlayerDataManager::Save();
					m_eventGains.push_back({ "card", m_eventPickAnimTo, 1 });   // 変化・強化した後のカードを結果に出す
				}
			}
			m_eventPickAnimIdx = -1;
			m_eventPickType.clear();
			m_eventResult = m_eventPickChoice;
		}
	}

	if (m_deckOpen)
	{
		m_deckScrollTarget -= Input::GetPendingWheel() * 0.5f;   // シーンが消費する前に読む（1回だけ）
		Input::ClearWheel();
		float maxS = DeckMaxScroll();
		if (m_deckScrollTarget < 0.0f)      m_deckScrollTarget = 0.0f;
		else if (m_deckScrollTarget > maxS) m_deckScrollTarget = maxS;
		m_deckScroll += (m_deckScrollTarget - m_deckScroll) * min(1.0f, 15.0f * deltaTime);
	}

	if (m_eventOpen && !m_eventPickType.empty() && m_eventPickType != "transformRelic")   // イベントのカード選択：ホイールでスクロール
	{
		URect pn = EventPickerPanel();
		const float ch = CardVisual::CARD_H * 0.85f;
		int rows = ((int)PlayerDataManager::GetData().deck.size() + 7) / 8;
		m_eventPickScroll.top = 150.0f;
		m_eventPickScroll.bottom = pn.y + pn.h - 8.0f;
		float listH = rows * (ch + 16.0f);
		m_eventPickScroll.contentH = (listH <= m_eventPickScroll.bottom - m_eventPickScroll.top)
			? listH                          // 全部収まる時はスクロールさせない（最後の段が薄くならない）
			: listH + ScrollList::FADE;      // 収まらない時だけ、最後の段の下に薄くなる分の余白
		m_eventPickScroll.Wheel(Input::GetPendingWheel());   // シーンが消費する前に読む
		Input::ClearWheel();
		m_eventPickScroll.Update(deltaTime);
	}

	if (m_craftOpen || m_invOpen)
	{
		float maxS = MatMaxScroll();
		const float OVER = 60.0f;
		if (m_matScrollTarget < -OVER)       m_matScrollTarget = -OVER;
		if (m_matScrollTarget > maxS + OVER) m_matScrollTarget = maxS + OVER;
		if (!m_mDrag)
		{
			if (m_matScrollTarget < 0.0f)      m_matScrollTarget += (0.0f - m_matScrollTarget) * min(1.0f, 12.0f * deltaTime);
			else if (m_matScrollTarget > maxS) m_matScrollTarget += (maxS - m_matScrollTarget) * min(1.0f, 12.0f * deltaTime);
		}
		m_matScroll += (m_matScrollTarget - m_matScroll) * min(1.0f, 15.0f * deltaTime);
	}

	if (auto battle = dynamic_cast<BattleScene*>(m_currentScene))
		battle->SetUiBlocked(AnyModalOpen() && !m_tutorialOpen);   // チュートリアルは自前でホバーを動かすので除く
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

float SceneManager::DeckMaxScroll() const
{
	int n = (int)PlayerDataManager::GetData().deck.size();
	int perRow = 6;
	int rows = (n + perRow - 1) / perRow;
	if (rows <= 0) return 0.0f;

	float ch = CardVisual::CARD_H * DECK_SCALE;
	float gapY = 20.0f;
	float startY = 70.0f;

	// scroll=0 のときの「最下段カードの実描画下端」(GetRectの拡大分込み)
	float lastBaseY = startY + (rows - 1) * (ch + gapY);
	float lastBottom = lastBaseY + (ch + CardVisual::CARD_H) / 2.0f;

	float wantBottom = (float)m_screenHeight - 70.0f;   // ここまで見せたい(下UIの余白)
	float m = lastBottom - wantBottom;
	return m > 0.0f ? m : 0.0f;                          // 収まるデッキは0(スクロール無し)
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

// 合成カードの ID「CRAFT:コア|素材|素材」をコアと素材に分ける
static bool ParseCraftId(const std::string& id, std::string& core, std::vector<std::string>& mods)
{
	core.clear(); mods.clear();
	if (id.rfind("CRAFT:", 0) != 0) return false;
	std::string rest = id.substr(6);
	size_t p = 0;
	while (true)
	{
		size_t q = rest.find('|', p);
		std::string s = rest.substr(p, q == std::string::npos ? std::string::npos : q - p);
		if (core.empty()) core = s; else mods.push_back(s);
		if (q == std::string::npos) break;
		p = q + 1;
	}
	return !core.empty();
}

// 枠に空きがある合成カード（デッキの番号）＝素材を足せるもの
std::vector<int> SceneManager::CraftEditables() const
{
	std::vector<int> v;
	auto& deck = PlayerDataManager::GetData().deck;
	for (int i = 0; i < (int)deck.size(); i++)
	{
		std::string core; std::vector<std::string> mods;
		if (!ParseCraftId(deck[i], core, mods)) continue;
		const BaseDef* b = MaterialDataBase::GetBase(core);
		int slots = (b ? b->modSlots : 2) + RelicManager::SumValue("modSlots");
		if ((int)mods.size() < slots) v.push_back(i);
	}
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
	int nSlots = 1 + CraftModSlots();
	float sw = 150.0f, gap = 20.0f;
	if (nSlots > 4) { sw = 110.0f; gap = 12.0f; }   // 枠が多い時は縮小
	w = sw; h = 60.0f; y = 130.0f;
	float totalW = nSlots * sw + (nSlots - 1) * gap;
	float startX = m_screenWidth / 2.0f - totalW / 2.0f;
	x = startX + i * (sw + gap);
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

// ===== アイテムのマス（インベントリ・合成で共通） =====
static constexpr float ITEM_BADGE_W = 30.0f, ITEM_BADGE_H = 18.0f;
static void ItemBadgePos(float x, float y, float w, float& bx, float& by)   // マスの右上の角（上の行との隙間8pxに収まる高さ）
{
	bx = x + w - ITEM_BADGE_W + 6.0f; by = y - ITEM_BADGE_H / 2.0f + 2.0f;
}
static float ItemNameLeft(float x, int n, float size)   // アイコン n 個（少し重ねて詰める）の右
{
	return x + 8.0f + (n ? size + (n - 1) * (size - 2.0f) + 6.0f : 0.0f);
}

float SceneManager::DrawItemIcons(const std::vector<const char*>& icons, float x, float y, float h, float size, float a)
{
	for (int k = 0; k < (int)icons.size(); k++)
		if (icons[k] && icons[k][0])
			m_uiSprite->DrawSprite(TextureManager::Get(icons[k]),
				x + 8.0f + k * (size - 2.0f), y + (h - size) / 2.0f, size, size, 0.0f, XMFLOAT4(1, 1, 1, a));
	return ItemNameLeft(x, (int)icons.size(), size);
}

void SceneManager::DrawItemBadge(float x, float y, float w, float a)
{
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	float bx, by; ItemBadgePos(x, y, w, bx, by);
	m_uiSprite->DrawSprite(white, bx, by, ITEM_BADGE_W, ITEM_BADGE_H, 0.0f, XMFLOAT4(0.10f, 0.08f, 0.06f, a));
	UiWindow::Frame(m_uiSprite, white, bx, by, ITEM_BADGE_W, ITEM_BADGE_H, XMFLOAT4(0.85f, 0.65f, 0.25f, a));
}

void SceneManager::DrawItemBadgeText(int count, float x, float y, float w, float a)
{
	float bx, by; ItemBadgePos(x, y, w, bx, by);
	wchar_t c[16]; swprintf_s(c, L"×%d", count);
	m_textRenderer->DrawTextCentered(c, bx + ITEM_BADGE_W / 2.0f, by, ITEM_BADGE_W, 12.0f, D2D1::ColorF(1.0f, 0.85f, 0.4f, a), ITEM_BADGE_H);
}

// 名前：入りきらない時だけ縮める（最小11px）。縦は中央。matFade＝合成画面の素材欄のスクロールの端で薄くする
void SceneManager::DrawItemName(const wchar_t* s, float left, float right, float y, float h, D2D1_COLOR_F c, bool matFade)
{
	float sz = 15.0f, tw = m_textRenderer->Measure(s, sz).width, room = right - left;
	if (tw > room && tw > 0.0f) sz = (std::max)(11.0f, sz * room / tw);
	float ty = y + (h - m_textRenderer->Measure(s, sz).height) / 2.0f;
	float aT = matFade ? MatAlphaAt(ty) : 1.0f, aB = matFade ? MatAlphaAt(ty + sz) : 1.0f;
	if (aT <= 0.0f && aB <= 0.0f) return;
	m_textRenderer->DrawTextGradientV(s, left, ty, sz, c, aT, aB);
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
		bool locked = (i > 0 && i - 1 < m_craftLocked);   // もう付いている素材（外せない）
		UiWindow::Button(m_uiSprite, white, x, y, w, h,
			locked ? XMFLOAT4(0.5f, 0.42f, 0.2f, 1.0f) : filled ? XMFLOAT4(0.25f, 0.35f, 0.5f, 1.0f) : XMFLOAT4(0.15f, 0.15f, 0.2f, 1.0f));
	}
	// 所持素材
	
	auto bs = CraftBases();
	for (int i = 0; i < (int)bs.size(); i++) {
		float x, y, w, h; GetCraftBaseRect(i, x, y, w, h);
		UiWindow::Button(m_uiSprite, white, x, y, w, h, XMFLOAT4(0.4f, 0.3f, 0.2f, 1.0f));
		if (auto b = MaterialDataBase::GetBase(bs[i].first)) DrawItemIcons({ CoreTypeIcon(b->type) }, x, y, h, 22.0f, 1.0f);
		DrawItemBadge(x, y, w, 1.0f);
	}
	{	// 素材を足せる合成カード（コアの後ろに紫で）
		auto eds = CraftEditables();
		for (int k = 0; k < (int)eds.size(); k++) {
			float x, y, w, h; GetCraftBaseRect((int)bs.size() + k, x, y, w, h);
			bool sel = (m_craftEditIdx == eds[k]);
			UiWindow::Button(m_uiSprite, white, x, y, w, h, sel ? XMFLOAT4(0.55f, 0.35f, 0.65f, 1.0f) : XMFLOAT4(0.35f, 0.22f, 0.42f, 1.0f));
		}
	}
	auto md = CraftMods();
	const BaseDef* cb = m_craftBase.empty() ? nullptr : MaterialDataBase::GetBase(m_craftBase);
	for (int i = 0; i < (int)md.size(); i++) {
		float x, y, w, h; GetCraftModRect(i, x, y, w, h);
		float a = MatAlphaAt(y + h * 0.5f); if (a <= 0.0f) continue;
		auto mdef = MaterialDataBase::GetMaterial(md[i].first);
		bool compat = cb && mdef && mdef->entryFor(cb->type);
		if (compat && mdef->entryFor(cb->type)->slot == "none" && CraftModSlots() == 1) compat = false;
		XMFLOAT4 bc = compat ? XMFLOAT4(0.2f, 0.3f, 0.3f, 1.0f) : XMFLOAT4(0.12f, 0.12f, 0.14f, 1.0f);
		bc.w *= a;
		UiWindow::Button(m_uiSprite, white, x, y, w, h, bc);
		DrawItemIcons(MatFitIcons(md[i].first), x, y, h, 16.0f, a);
		float ba = MatAlphaAt(y + 2.0f); if (ba > 0.0f) DrawItemBadge(x, y, w, ba);   // バッジの高さで薄くする
	}
	
		// 作成ボタン
		{
			float x, y, w, h; GetCraftBtnRect(x, y, w, h);
			bool ready = !m_craftBase.empty() && (int)m_craftMods.size() > m_craftLocked;   // 新しい素材が1つ以上
			bool hov = UiHover(x, y, w, h, m_uiInput.GetMousePos(), "craftmake", s_lastHoverKey);   // ホバーSE＋判定
			float dy = -HoverLift::Get(x, y, hov, 4.0f);
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

	// 見出しの下地ウィンドウ
	{
		float hx = m_screenWidth / 2.0f - (6 * 150.0f + 5 * 8.0f) / 2.0f;
		UiWindow::Draw(m_uiSprite, white, hx - 10.0f, CoreTop() - 30.0f, 120.0f, 28.0f);
		UiWindow::Draw(m_uiSprite, white, hx - 10.0f, MatTop() - 32.0f, 150.0f, 28.0f);
	}

	m_uiSprite->End();

	// --- テキスト ---
	m_textRenderer->Begin();
	// 枠の高さの中で、文字の上下を真ん中に（左寄せの文字用）
	auto midY = [&](const wchar_t* s, float size, float y, float h) {
		return y + (h - m_textRenderer->Measure(s, size, FontRole::Body).height) / 2.0f; };
	m_textRenderer->DrawText(L"Craft", m_screenWidth / 2.0f - 30.0f, 70.0f, 28.0f, D2D1::ColorF(1, 1, 1));

	for (int i = 0; i < nSlots; i++)
	{
		float x, y, w, h; GetCraftSlotRect(i, x, y, w, h);
		std::wstring t = (i == 0) ? L"Base" : (L"Mod" + std::to_wstring(i));
		if (i == 0 && !m_craftBase.empty()) t = matName(m_craftBase);
		if (i > 0 && (int)m_craftMods.size() >= i) t = matName(m_craftMods[i - 1]);
		m_textRenderer->DrawText(t.c_str(), x + 10.0f, midY(t.c_str(), 18.0f, y, h), 18.0f, D2D1::ColorF(1, 1, 1));
	}
	// 見出し
	float hdrX = m_screenWidth / 2.0f - (6 * 150.0f + 5 * 8.0f) / 2.0f;   // 箱の左端に揃える
	m_textRenderer->DrawTextCentered(L"CORE", hdrX - 10.0f + 60.0f, CoreTop() - 30.0f, 120.0f, 18.0f, D2D1::ColorF(1, 0.85f, 0.5f), 28.0f);
	m_textRenderer->DrawTextCentered(L"MATERIAL", hdrX - 10.0f + 75.0f, MatTop() - 32.0f, 150.0f, 18.0f, D2D1::ColorF(0.6f, 0.9f, 0.9f), 28.0f);

	auto bases = CraftBases();
	for (int i = 0; i < (int)bases.size(); i++)
	{
		float x, y, w, h; GetCraftBaseRect(i, x, y, w, h);
		DrawItemName(matName(bases[i].first).c_str(), ItemNameLeft(x, 1, 22.0f), x + w - 8.0f, y, h, D2D1::ColorF(1, 1, 1), false);
		DrawItemBadgeText(bases[i].second, x, y, w, 1.0f);
	}
	{	// 素材を足せる合成カード：「コア名 付いている数/枠」
		auto eds = CraftEditables();
		for (int k = 0; k < (int)eds.size(); k++) {
			float x, y, w, h; GetCraftBaseRect((int)bases.size() + k, x, y, w, h);
			std::string core; std::vector<std::string> ms;
			ParseCraftId(PlayerDataManager::GetData().deck[eds[k]], core, ms);
			const BaseDef* b = MaterialDataBase::GetBase(core);
			int slots = (b ? b->modSlots : 2) + RelicManager::SumValue("modSlots");
			wchar_t buf[64]; swprintf_s(buf, L"%s %d/%d", matName(core).c_str(), (int)ms.size(), slots);
			DrawItemName(buf, x + 8.0f, x + w - 8.0f, y, h, D2D1::ColorF(1, 0.9f, 1), false);
		}
	}
	auto mods = CraftMods();
	for (int i = 0; i < (int)mods.size(); i++) 
	{
		float x, y, w, h; GetCraftModRect(i, x, y, w, h);
		float nx = ItemNameLeft(x, (int)MatFitIcons(mods[i].first).size(), 16.0f);
		DrawItemName(matName(mods[i].first).c_str(), nx, x + w - 8.0f, y, h, D2D1::ColorF(1, 1, 1), true);
		float ba = MatAlphaAt(y + 2.0f); if (ba > 0.0f) DrawItemBadgeText(mods[i].second, x, y, w, ba);
	}
	{
		float x, y, w, h; GetCraftBtnRect(x, y, w, h);
		POINT mp = m_uiInput.GetMousePos();
		float dy = (mp.x >= x && mp.x <= x + w && mp.y >= y && mp.y <= y + h) ? -4.0f : 0.0f;
		m_textRenderer->DrawTextCentered(L"Make", x + w / 2.0f, y + dy, w, 20.0f, D2D1::ColorF(1, 1, 1), h);
	}
	if (prev)
		CardVisual::DrawTexts(m_textRenderer, prev, nullptr,
			m_screenWidth / 2.0f - CardVisual::CARD_W / 2.0f, 210.0f, 1.2f, 0.0f, 1.0f);
	// プレビューの右に、コストを変えているレリック（例：錬成の触媒：コスト-1）
	if (prev)
	{
		float nx = m_screenWidth / 2.0f - CardVisual::CARD_W / 2.0f + CardVisual::CARD_W * 1.2f + 16.0f, ny = 214.0f;
		for (auto& rid2 : PlayerDataManager::GetData().relics)
			if (auto r = RelicManager::Get(rid2))
				if (int v = r->ValueOf("craftCostDown"))
				{
					std::wstring s = ToWString(r->name) + (v > 0 ? L"：コスト-" : L"：コスト+") + std::to_wstring(v > 0 ? v : -v);
					m_textRenderer->DrawText(s.c_str(), nx, ny, 13.0f, v > 0 ? D2D1::ColorF(0.6f, 0.95f, 0.6f) : D2D1::ColorF(1.0f, 0.6f, 0.5f));
					ny += 18.0f;
				}
	}
	m_textRenderer->End();

	std::string hid = HoveredItem(m_uiInput.GetMousePos());
	if (!hid.empty()) DrawItemTooltip(hid, m_uiInput.GetMousePos());
}

void SceneManager::HandleCraftClick(POINT m)
{
	if (BackBtnHit(m)) { m_craftOpen = false; m_invOpen = true; m_craftEditIdx = -1; m_craftLocked = 0; return; }  // 戻る＝アイテム画面へ

	// 枠クリックで解除（最初に判定）
	int nSlots = 1 + CraftModSlots();
	for (int i = 0; i < nSlots; i++)
	{
		float x, y, w, h; GetCraftSlotRect(i, x, y, w, h);
		if (m.x >= x && m.x <= x + w && m.y >= y && m.y <= y + h)
		{
			if (i == 0) { if (m_craftEditIdx >= 0) { m_craftMods.clear(); m_craftEditIdx = -1; m_craftLocked = 0; } m_craftBase.clear(); }   // 足している途中なら全部やめる
			else if (i - 1 >= m_craftLocked && (int)m_craftMods.size() >= i) m_craftMods.erase(m_craftMods.begin() + (i - 1));   // もう付いている素材は外せない
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
		int n = (m_craftBase == id && m_craftEditIdx < 0) ? 1 : 0;   // 足す時はコアを使わない
		for (int k = m_craftLocked; k < (int)m_craftMods.size(); k++) if (m_craftMods[k] == id) n++;   // もう付いている素材は数えない
		return n;
		};

	// 核クリック → 土台へ
	auto bases = CraftBases();
	for (int i = 0; i < (int)bases.size(); i++) {
		float x, y, w, h; GetCraftBaseRect(i, x, y, w, h);
		if (m.x >= x && m.x <= x + w && m.y >= y && m.y <= y + h)
		{
			const std::string& id = bases[i].first;
			if (m_craftEditIdx >= 0) { m_craftEditIdx = -1; m_craftLocked = 0; m_craftMods.clear(); m_craftBase.clear(); }   // 足すのをやめて新しく作る
			int used = usedCount(id) - (m_craftBase == id ? 1 : 0);
			if (used < PlayerDataManager::MaterialCount(id)) {
				m_craftBase = id;
				// 新しい核に効果のない素材は外す
				if (const BaseDef* nb = MaterialDataBase::GetBase(id))
					m_craftMods.erase(std::remove_if(m_craftMods.begin(), m_craftMods.end(),
						[&](const std::string& mid) {
							auto mm = MaterialDataBase::GetMaterial(mid);
							return !mm || !mm->entryFor(nb->type);
						}), m_craftMods.end());
				while ((int)m_craftMods.size() > CraftModSlots()) m_craftMods.pop_back();  // 枠が減ったら余りを外す
			}
			return;
		}
	}
	// 合成カードクリック → 素材を足す（コアの一覧の後ろに並ぶ）
	{
		auto eds = CraftEditables();
		for (int k = 0; k < (int)eds.size(); k++) {
			float x, y, w, h; GetCraftBaseRect((int)bases.size() + k, x, y, w, h);
			if (m.x >= x && m.x <= x + w && m.y >= y && m.y <= y + h)
			{
				std::string core; std::vector<std::string> ms;
				if (ParseCraftId(PlayerDataManager::GetData().deck[eds[k]], core, ms))
				{ m_craftEditIdx = eds[k]; m_craftBase = core; m_craftMods = ms; m_craftLocked = (int)ms.size(); }
				return;
			}
		}
	}
	// 素材クリック → 修飾へ
	auto mods = CraftMods();
	for (int i = 0; i < (int)mods.size(); i++) {
		float x, y, w, h; GetCraftModRect(i, x, y, w, h);
		if (y + h <= MatTop() || y >= MatBottom()) continue;   // 上に完全に出た/下端以降の行だけ除外（箱は帯の下に潜る）
		if (m.x >= x && m.x <= x + w && m.y >= y && m.y <= y + h) {
			const std::string& id = mods[i].first;
			if (m_craftBase.empty()) return;                         // 先にBaseを選ぶ
			const BaseDef* b = MaterialDataBase::GetBase(m_craftBase);
			const MaterialDef* mdef = MaterialDataBase::GetMaterial(id);
			if (!b || !mdef || !mdef->entryFor(b->type)) return;     // この核に効果なし→弾く
			if (mdef->entryFor(b->type)->slot == "none" && CraftModSlots() == 1) return;  // 効果なし素材は1枠コア不可
			if ((int)m_craftMods.size() < CraftModSlots() && usedCount(id) < PlayerDataManager::MaterialCount(id))
				m_craftMods.push_back(id);
			return;
		}
	}
}

void SceneManager::DoCraft()
{
	if (m_craftBase.empty()) return;
	if ((int)m_craftMods.size() <= m_craftLocked) return;   // 新しい素材が1つ以上あれば作れる（枠は全部埋まっていなくていい）

	// 必要数を集計して所持チェック（足す時は、コアともう付いている素材は使わない）
	std::map<std::string, int> need;
	if (m_craftEditIdx < 0) need[m_craftBase]++;
	for (int k = m_craftLocked; k < (int)m_craftMods.size(); k++) need[m_craftMods[k]]++;
	for (auto& kv : need)
		if (PlayerDataManager::MaterialCount(kv.first) < kv.second) return;   // 足りない

	std::string rid = CraftRecipeId();           // 消費前に確定

	// 消費
	for (auto& kv : need)
		PlayerDataManager::AddMaterial(kv.first, -kv.second);

	// デッキに合成カードを追加（足した時は、そのカードを素材が増えた ID に置き換える）
	auto& deck = PlayerDataManager::GetData().deck;
	if (m_craftEditIdx >= 0 && m_craftEditIdx < (int)deck.size()) deck[m_craftEditIdx] = rid;
	else deck.push_back(rid);
	std::string mats;
	for (auto& kv : need)
		for (int k = 0; k < kv.second; k++) { if (!mats.empty()) mats += ","; mats += kv.first; }
	Telemetry::Instance().Log("craft", { {"id", rid}, {"mats", mats} });
	PlayerDataManager::Save();

	m_craftBase.clear(); m_craftMods.clear(); m_craftEditIdx = -1; m_craftLocked = 0;

	m_craftFxCard = rid;                          // 演出開始
	m_craftFxTimer = CRAFT_FX_DURATION;
}

void SceneManager::GetCraftBaseRect(int i, float& x, float& y, float& w, float& h) const
{
	w = 150.0f; h = 38.0f; float gap = 8.0f; int cols = 4;
	float startX = m_screenWidth / 2.0f - (cols * w + (cols - 1) * gap) / 2.0f;
	x = startX + (i % cols) * (w + gap);
	y = CoreTop() - 16.0f + (i / cols) * (h + gap);  // ← スクロール反映
}

void SceneManager::GetCraftModRect(int i, float& x, float& y, float& w, float& h) const
{
	w = 150.0f; h = 38.0f; float gap = 8.0f; int cols = 6;
	float startX = m_screenWidth / 2.0f - (cols * w + (cols - 1) * gap) / 2.0f;
	x = startX + (i % cols) * (w + gap);
	y = MatTop() + (i / cols) * (h + gap) - m_matScroll;
}

float SceneManager::MatMaxScroll() const
{
	int cols = 4; float h = 38.0f, gap = 8.0f;
	int rows = ((int)CraftMods().size() + cols - 1) / cols;
	float contentH = rows * (h + gap);
	float visibleH = MatBottom() - MatTop();	float maxS = contentH - visibleH;
	return maxS > 0.0f ? maxS : 0.0f;
}

float SceneManager::MatTop() const
{
	int coreCols = m_craftOpen ? 6 : 4;   // インベントリはコアの並び(GetCraftBaseRect=4列)に合わせる
	int coreRows = ((int)CraftBases().size() + coreCols - 1) / coreCols;
	if (coreRows < 1) coreRows = 1;
	return CoreTop() + coreRows * (38.0f + 8.0f) + (m_craftOpen ? 34.0f : 56.0f);   // インベントリは見出し・説明の分だけ広く
}

float SceneManager::MatBottom() const
{
	return m_screenHeight - (m_craftOpen ? 100.0f : 122.0f);   // Makeボタンの上に余白を確保（インベントリは窓の下に合成ボタン）
}

float SceneManager::MatAlphaAt(float y) const
{
	float top = MatTop(), bot = MatBottom();
	const float FT = 34.0f;   // 上：バンドより上に出た分だけフェード
	const float FB = 40.0f;   // 下フェード
	if (y < top - FT || y > bot) return 0.0f;
	float a = 1.0f;
	if (y < top)      a = (y - (top - FT)) / FT;   // top以上は薄くしない＝最上段が反応
	if (bot - y < FB) a = min(a, (bot - y) / FB);
	return a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
}

float SceneManager::CoreTop() const
{
	return m_craftOpen ? 380.0f : 210.0f + RelicManager::BarExtraHeight(m_screenWidth);   // クラフトはプレビューの下／インベントリは見出しと窓の分だけ下げる
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
		if (MatAlphaAt(y + h * 0.5f) < 0.6f) continue;   // 端で薄い行はホバー対象外（詳細を出さない）
		if (mp.x >= x && mp.x <= x + w && mp.y >= y && mp.y <= y + h) return mods[i].first;
	}
	return "";
}

void SceneManager::DrawItemTooltip(const std::string& id, POINT mp)
{
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	ItemTooltip::Draw(m_uiSprite, m_textRenderer, white, id,
		(float)mp.x + 16.0f, (float)mp.y + 8.0f, m_screenWidth, m_screenHeight, false);
}

void SceneManager::DrawInventory()
{
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	POINT mp = m_uiInput.GetMousePos();
	std::string hid = HoveredItem(mp);
	auto bases = CraftBases();
	auto mods = CraftMods();

	const float gridW = 6 * 150.0f + 5 * 8.0f;                         // 素材の6列が一番広い
	const float px = m_screenWidth / 2.0f - gridW / 2.0f - 24.0f, pw = gridW + 48.0f;
	const float py = CoreTop() - 66.0f, ph = MatBottom() + 10.0f - py;
	const float bnW = 360.0f, bnH = 64.0f, bnY = py - bnH + 8.0f;     // 見出しの布は窓の上の縁に少し重ねる
	const XMFLOAT4 gold(1.0f, 0.85f, 0.2f, 1.0f);

	// マスを1つ描く（ホバーで浮いて金の縁）。戻り値＝浮いた分
	auto tile = [&](float x, float y, float w, float h, XMFLOAT4 col, bool hov) {
		float lift = hov ? 3.0f : 0.0f;
		UiWindow::Button(m_uiSprite, white, x, y - lift, w, h, col);
		if (hov) UiWindow::Frame(m_uiSprite, white, x, y - lift, w, h, gold);
		return lift;
	};

	float bx, by, bw, bh; GetCraftBtnRect(bx, by, bw, bh);
	bool canCraft = (m_currentType != SceneType::Battle);
	bool btnHov = canCraft && mp.x >= bx && mp.x <= bx + bw && mp.y >= by && mp.y <= by + bh;

	m_uiSprite->Begin();
	m_uiSprite->DrawSprite(white, 0, BAR_H, (float)m_screenWidth, (float)m_screenHeight - BAR_H, 0.0f,
		XMFLOAT4(0.05f, 0.03f, 0.09f, 0.96f));                            // マップが透けない濃さ（イベントのカード選択と同じ）
	UiWindow::Draw(m_uiSprite, white, px, py, pw, ph);
	m_uiSprite->DrawSprite(TextureManager::Get("ui_banner"), m_screenWidth / 2.0f - bnW / 2.0f, bnY, bnW, bnH, 0.0f, XMFLOAT4(1, 1, 1, 1));
	m_uiSprite->DrawSprite(white, px + 24.0f, CoreTop() - 24.0f, pw - 48.0f, 1.5f, 0.0f, XMFLOAT4(1.0f, 0.85f, 0.5f, 0.35f));   // 区切り線
	m_uiSprite->DrawSprite(white, px + 24.0f, MatTop() - 10.0f, pw - 48.0f, 1.5f, 0.0f, XMFLOAT4(0.6f, 0.9f, 0.9f, 0.35f));

	std::vector<float> baseLift(bases.size(), 0.0f), modLift(mods.size(), 0.0f);
	for (int i = 0; i < (int)bases.size(); i++) {
		float x, y, w, h; GetCraftBaseRect(i, x, y, w, h);
		baseLift[i] = tile(x, y, w, h, XMFLOAT4(0.45f, 0.33f, 0.2f, 1.0f), hid == bases[i].first);
		if (auto b = MaterialDataBase::GetBase(bases[i].first)) {
			const char* ic = CoreTypeIcon(b->type);
			if (ic[0]) DrawItemIcons({ ic }, x, y - baseLift[i], h, 22.0f, 1.0f);
		}
		DrawItemBadge(x, y - baseLift[i], w, 1.0f);
	}
	for (int i = 0; i < (int)mods.size(); i++) {
		float x, y, w, h; GetCraftModRect(i, x, y, w, h);
		if (y + h <= MatTop() || y >= MatBottom()) continue;   // 上に完全に出た/下端以降の行だけ除外
		modLift[i] = tile(x, y, w, h, XMFLOAT4(0.2f, 0.33f, 0.36f, 1.0f), hid == mods[i].first);
		DrawItemIcons(MatFitIcons(mods[i].first), x, y - modLift[i], h, 16.0f, 1.0f);
		if (y + h <= MatBottom()) DrawItemBadge(x, y - modLift[i], w, 1.0f);   // 文字と同じく、帯に潜る行は出さない
	}

	// 合成する（戦闘中は押せない）
	UiWindow::Button(m_uiSprite, white, bx, by - (btnHov ? 4.0f : 0.0f), bw, bh,
		canCraft ? XMFLOAT4(0.4f, 0.3f, 0.5f, 1.0f) : XMFLOAT4(0.22f, 0.22f, 0.24f, 1.0f));
	m_uiSprite->End();

	m_textRenderer->Begin();
	{
		TextRenderer::RoleScope heading(*m_textRenderer, FontRole::Heading);
		m_textRenderer->DrawOutlinedTextCentered(L"アイテム", m_screenWidth / 2.0f, bnY, bnW, bnH, 26.0f,
			D2D1::ColorF(1.0f, 0.95f, 0.8f), D2D1::ColorF(0.25f, 0.04f, 0.06f), 3.0f);
	}
	m_textRenderer->DrawText(L"コア", px + 28.0f, CoreTop() - 50.0f, 18.0f, D2D1::ColorF(1, 0.85f, 0.5f));
	m_textRenderer->DrawText(L"カードの土台。合成に1つ使う", px + 76.0f, CoreTop() - 47.0f, 13.0f, D2D1::ColorF(0.7f, 0.68f, 0.62f));
	m_textRenderer->DrawText(L"素材", px + 28.0f, MatTop() - 36.0f, 18.0f, D2D1::ColorF(0.6f, 0.9f, 0.9f));
	m_textRenderer->DrawText(L"コアに足して効果を加える", px + 76.0f, MatTop() - 33.0f, 13.0f, D2D1::ColorF(0.62f, 0.7f, 0.7f));
	if (bases.empty())
		m_textRenderer->DrawTextCentered(L"まだ持っていない", m_screenWidth / 2.0f, CoreTop() - 8.0f, 300.0f, 15.0f,
			D2D1::ColorF(0.55f, 0.55f, 0.55f), 22.0f);
	if (mods.empty())
		m_textRenderer->DrawTextCentered(L"まだ持っていない", m_screenWidth / 2.0f, MatTop() + 8.0f, 300.0f, 15.0f,
			D2D1::ColorF(0.55f, 0.55f, 0.55f), 22.0f);
	for (int i = 0; i < (int)bases.size(); i++) {   // 名前（アイコンの右）と個数（右上のバッジ）
		float x, y, w, h; GetCraftBaseRect(i, x, y, w, h); y -= baseLift[i];
		DrawItemName(GetItemInfo(bases[i].first).name.c_str(), ItemNameLeft(x, 1, 22.0f), x + w - 8.0f, y, h, D2D1::ColorF(1, 1, 1), false);
		DrawItemBadgeText(bases[i].second, x, y, w, 1.0f);
	}
	for (int i = 0; i < (int)mods.size(); i++) {
		float x, y, w, h; GetCraftModRect(i, x, y, w, h);
		if (y + h <= MatTop() || y + h > MatBottom()) continue;   // 帯に潜る行は文字を描かない（D2D文字は帯で隠せないため）
		y -= modLift[i];
		float nx = ItemNameLeft(x, (int)MatFitIcons(mods[i].first).size(), 16.0f);
		DrawItemName(GetItemInfo(mods[i].first).name.c_str(), nx, x + w - 8.0f, y, h, D2D1::ColorF(1, 1, 1), false);
		DrawItemBadgeText(mods[i].second, x, y, w, 1.0f);
	}
	m_textRenderer->DrawTextCentered(L"合成する", bx + bw / 2.0f, by - (btnHov ? 4.0f : 0.0f), bw, 20.0f,
		canCraft ? D2D1::ColorF(1, 1, 1) : D2D1::ColorF(0.55f, 0.55f, 0.55f), bh);
	m_textRenderer->DrawTextCentered(canCraft ? L"コア＋素材でカードを合成できる" : L"戦闘中は合成できない",
		bx + bw / 2.0f, by + bh + 4.0f, 320.0f, 14.0f, D2D1::ColorF(0.75f, 0.75f, 0.75f), 20.0f);
	m_textRenderer->End();

	if (!hid.empty()) DrawItemTooltip(hid, mp);
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
	for (int i = 0; i < 2; i++)
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
	for (int i = 0; i < 2; i++)
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
		for (int i = 0; i < 2; i++)
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
	for (int i = 0; i < 2; i++)
	{
		float x, y, w, h; GetRestBtnRect(i, x, y, w, h);
		if (m.x >= x && m.x <= x + w && m.y >= y && m.y <= y + h)
		{
			Audio::PlaySE("Assets/Sound/se/click.mp3");
			Telemetry::Instance().Log("rest_choice", {
				{"choice", i == 0 ? "heal" : (i == 1 ? "upgrade" : "craft")}
				});
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
				m_deckOpen = true; m_deckUpgradeMode = true; m_deckScroll = 0.0f; m_deckScrollTarget = 0.0f;
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

void SceneManager::GetEventChoiceRect(int i, float& x, float& y, float& w, float& h) const
{
	w = 560.0f; h = 58.0f;   // 2段（行動／結果）が入る高さ
	x = m_screenWidth / 2.0f - w / 2.0f;
	y = 280.0f + i * 72.0f;
}

void SceneManager::ApplyOutcomes(const EventChoice& c)
{
	auto& pd = PlayerDataManager::GetData();
	m_eventGains.clear();
	m_eventHpDisplay = -1.0f;
	for (auto& o : c.outcomes)
	{
		if (o.chance < 100 && rand() % 100 >= o.chance) continue;   // 確率つきの結果（省略＝100＝必ず）

		if (o.type == "hp")
		{
			int before = pd.hp;
			pd.hp += o.value; if (pd.hp > pd.maxHp) pd.hp = pd.maxHp; if (pd.hp < 0) pd.hp = 0;
			if (pd.hp != before) m_eventGains.push_back({ "hp", "", pd.hp - before });
			if (pd.hp < before)   // ダメージ：画面を揺らし、減った分をバーで徐々に縮める
			{
				m_eventHpDisplay = (float)before;
				ScreenShake::Add(ScreenShake::PowerForDamage(before - pd.hp));
			}
		}
		else if (o.type == "maxHp")
		{
			pd.maxHp += o.value; pd.hp += o.value; if (pd.hp < 1) pd.hp = 1;
			m_eventGains.push_back({ "maxHp", "", o.value });
		}
		else if (o.type == "gold")
		{
			int before = pd.gold;
			if (o.value > 0) PlayerDataManager::AddGold(o.value);
			else { pd.gold += o.value; if (pd.gold < 0) pd.gold = 0; }
			if (pd.gold != before) m_eventGains.push_back({ "gold", "", pd.gold - before });
		}
		else if (o.type == "material")
		{
			int n = o.value > 0 ? o.value : 1;
			PlayerDataManager::AddMaterial(o.param, n);
			m_eventGains.push_back({ "material", o.param, n });
		}
		else if (o.type == "randomMaterial") {
			std::string mid = MaterialDataBase::RandomId(o.value);   // value=層(0=全)
			if (!mid.empty()) { PlayerDataManager::AddMaterial(mid, 1); m_eventGains.push_back({ "material", mid, 1 }); }
		}
		else if (o.type == "relic")
		{
			std::string rid = o.param.empty() ? RelicManager::RandomUnowned("event")     // 空＝イベント用のレリック
				: o.param == "random" ? RelicManager::RandomDrop()                       // random＝普通のレリック（レア度ごとの確率）
				: o.param;
			if (rid.empty()) rid = RelicManager::RandomDrop();
			if (!rid.empty()) { PlayerDataManager::AddRelic(rid); m_eventGains.push_back({ "relic", rid, 1 }); }
		}
		else if (o.type == "addCard")
		{
			PlayerDataManager::AddCard(o.param);
			m_eventGains.push_back({ "card", o.param, 1 });
			if (const CardData* d = CardDataBase::Get(o.param); d && d->type == CardType::Curse)
				m_craftFxQueue.push_back(o.param);   // 呪いは手に入る瞬間を見せる
		}
		else if (o.type == "battle") { if (m_eventPending == EventPending::None) { m_eventPending = EventPending::Battle; m_eventBattleParam = o.param; } }
		else if (o.type == "shop") { if (m_eventPending == EventPending::None) m_eventPending = EventPending::Shop; }
		else if (o.type == "treasure") { if (m_eventPending == EventPending::None) m_eventPending = EventPending::Treasure; }
		else if (o.type == "pickCard") { if (m_eventPending == EventPending::None) { m_eventPending = EventPending::CardSelect; m_cardSelectMode = o.param; } }
	}
	PlayerDataManager::Save();
	if (!m_craftFxQueue.empty()) PlayNextCardFx();
}

void SceneManager::PlayNextCardFx()
{
	m_craftFxCard = m_craftFxQueue.front();
	m_craftFxQueue.erase(m_craftFxQueue.begin());
	m_craftFxTimer = CRAFT_FX_DURATION;
	Se::Play("debuff.wav");
}

void SceneManager::DrawEvent()
{
	const EventDef* e = EventDataBase::Get(m_eventId);
	if (!e) { m_eventOpen = false; return; }
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	POINT mp = m_uiInput.GetMousePos();
	int n = (int)e->choices.size();
	bool result = (m_eventResult >= 0);
	URect p = EventPanelRect(n, result ? (int)m_eventGains.size() : 0);
	{   // ダメージを受けた時の揺れ（ScreenShakeはワールド単位なのでピクセルに換算）
		float sx, sz; ScreenShake::GetOffset(sx, sz);
		p.x += sx * 40.0f; p.y += sz * 40.0f;
	}
	float cx = p.x + p.w / 2.0f;
	const float bnW = 440.0f, bnH = 80.0f, bnY = p.y - 40.0f;   // タイトルの布バナー（窓の上端に乗せる）

	// 選択肢の文を「行動」と「結果（…）」に分ける（events.jsonは書き換えずに2段表示）
	auto splitLabel = [](const std::wstring& s, std::wstring& act, std::wstring& eff) {
		size_t k = s.find(L'（');
		if (k == std::wstring::npos) k = s.find(L'(');
		act = (k == std::wstring::npos) ? s : s.substr(0, k);
		eff = (k == std::wstring::npos) ? L"" : s.substr(k);
		};

	// ---- スプライト ----
	m_uiSprite->Begin();
	m_uiSprite->DrawSprite(white, 0, BAR_H, (float)m_screenWidth, (float)m_screenHeight - BAR_H, 0.0f,
		XMFLOAT4(0.05f, 0.03f, 0.09f, 0.96f));
	UiWindow::Draw(m_uiSprite, white, p.x, p.y, p.w, p.h);
	m_uiSprite->DrawSprite(TextureManager::Get("ui_banner"), cx - bnW / 2.0f, bnY, bnW, bnH, 0.0f, XMFLOAT4(1, 1, 1, 1));
	m_uiSprite->DrawSprite(white, cx - 280.0f, p.y + 166.0f, 560.0f, 2.0f, 0.0f, XMFLOAT4(0.85f, 0.7f, 0.3f, 0.5f));   // 説明と選択肢の区切り

	if (m_eventResult < 0)
	{
		for (int i = 0; i < n; i++)
		{
			float x, y, w, h; GetEventChoiceRect(i, x, y, w, h);
			bool en = ChoiceEnabled(e->choices[i]);
			char key[16]; sprintf_s(key, "evc%d", i);
			bool hov = en && UiHover(x, y, w, h, mp, key, s_lastHoverKey);   // ホバーSE＋判定
			float yy = hov ? y - 4.0f : y;                                    // 浮く
			UiWindow::Draw(m_uiSprite, white, x, yy, w, h);
			if (!en)
				m_uiSprite->DrawSprite(white, x + 6, yy + 6, w - 12, h - 12, 0.0f, XMFLOAT4(0.0f, 0.0f, 0.0f, 0.45f));   // 無効=暗く
			else if (hov)
				m_uiSprite->DrawSprite(white, x + 6, yy + 6, w - 12, h - 12, 0.0f, XMFLOAT4(0.55f, 0.45f, 0.75f, 0.45f)); // ホバー強調
		}
	}
	else   // 結果：受け取った物の一覧＋「クリックで進む」のピル
	{
		for (int i = 0; i < (int)m_eventGains.size(); i++)
		{
			const auto& g = m_eventGains[i];
			URect r = EventGainRect(i, p);
			bool canHover = (g.kind == "material" || g.kind == "relic" || g.kind == "card");
			UiWindow::Draw(m_uiSprite, white, r.x, r.y, r.w, r.h,
				canHover && r.has(mp) ? XMFLOAT4(1.0f, 0.95f, 0.75f, 1.0f) : XMFLOAT4(1, 1, 1, 1));
			float ix = r.x + 10.0f, iy = r.y + 5.0f, is = 34.0f;   // 左端のアイコン
			if (g.kind == "material")
				m_uiSprite->DrawSprite(TextureManager::Get(MaterialDataBase::GetBase(g.id) ? "mat_core" : "mat_material"),
					ix, iy, is, is, 0.0f, XMFLOAT4(1, 1, 1, 1));
			else if (g.kind == "gold")
				m_uiSprite->DrawSprite(TextureManager::Get("icon_gold"), ix, iy, is, is, 0.0f, XMFLOAT4(1.0f, 0.9f, 0.3f, 1));
			else if (g.kind == "hp" || g.kind == "maxHp")
				m_uiSprite->DrawSprite(TextureManager::Get("icon_hp"), ix, iy, is, is, 0.0f, XMFLOAT4(1.0f, 0.5f, 0.5f, 1));
			else if (g.kind == "relic")
				if (const RelicDef* rd = RelicManager::Get(g.id))
					if (auto tex = TextureManager::Get(rd->icon))
						m_uiSprite->DrawSprite(tex, ix, iy, is, is, 0.0f, RelicManager::RarityColor(rd->rarity));
			else if (g.kind == "card")
				if (const CardData* d = CardDataBase::Get(g.id))
				{
					const float s = 0.3f;   // 小さいカードの絵
					CardVisual::DrawBase(m_uiSprite, white, ix + 2.0f - CardVisual::CARD_W * (1 - s) / 2.0f,
						r.y + 1.0f - CardVisual::CARD_H * (1 - s) / 2.0f, s, 0.0f, CardVisual::GetCardColor(d->type), d, 0.0f);
				}

			if (g.kind == "hp" && g.value < 0)   // 減ったHPをバトルと同じバーで（オレンジが徐々に縮む）
			{
				const auto& pdh = PlayerDataManager::GetData();
				HPBarInfo bar;
				bar.currentHP = pdh.hp; bar.maxHP = pdh.maxHp;
				bar.displayHP = (m_eventHpDisplay >= 0.0f) ? m_eventHpDisplay : (float)pdh.hp;
				HpBar::Draw(m_uiSprite, white, r.x + 150.0f, r.y + 14.0f, r.w - 164.0f, 16.0f, bar, m_uiTime);
			}
		}
		UiWindow::Draw(m_uiSprite, white, cx - 120.0f, p.y + p.h - 64.0f, 240.0f, 40.0f);
	}
	m_uiSprite->End();

	// ---- 文字 ----
	m_textRenderer->Begin();
	m_textRenderer->DrawOutlinedTextCentered(ToWString(e->title).c_str(), cx, bnY, bnW, bnH, 28.0f,
		D2D1::ColorF(1.0f, 0.95f, 0.8f), D2D1::ColorF(0.25f, 0.04f, 0.06f), 3.0f);

	if (m_eventResult < 0)
	{
		m_textRenderer->DrawTextCentered(ToWString(e->desc).c_str(), cx, p.y + 60.0f, 560.0f, 19.0f,
			D2D1::ColorF(0.92f, 0.92f, 0.92f), 90.0f);
		for (int i = 0; i < n; i++)
		{
			float x, y, w, h; GetEventChoiceRect(i, x, y, w, h);
			bool en = ChoiceEnabled(e->choices[i]);
			bool hov = en && mp.x >= x && mp.x <= x + w && mp.y >= y && mp.y <= y + h;
			float yy = hov ? y - 4.0f : y;
			std::wstring act, eff; splitLabel(ToWString(e->choices[i].label), act, eff);
			if (eff.empty())   // 結果の書き無し＝1段で中央
				m_textRenderer->DrawText(act.c_str(), x + 18.0f, yy + 17.0f, 20.0f,
					en ? D2D1::ColorF(1, 1, 1) : D2D1::ColorF(0.4f, 0.4f, 0.4f));
			else
			{
				m_textRenderer->DrawText(act.c_str(), x + 18.0f, yy + 7.0f, 20.0f,
					en ? D2D1::ColorF(1, 1, 1) : D2D1::ColorF(0.4f, 0.4f, 0.4f));
				m_textRenderer->DrawText(eff.c_str(), x + 22.0f, yy + 33.0f, 14.0f,
					en ? D2D1::ColorF(1.0f, 0.85f, 0.45f) : D2D1::ColorF(0.4f, 0.4f, 0.4f));
			}
		}
	}
	else
	{
		m_textRenderer->DrawTextCentered(ToWString(e->choices[m_eventResult].result).c_str(), cx, p.y + 60.0f,
			560.0f, 21.0f, D2D1::ColorF(0.8f, 1.0f, 0.8f), 90.0f);
		for (int i = 0; i < (int)m_eventGains.size(); i++)
		{
			const auto& g = m_eventGains[i];
			URect r = EventGainRect(i, p);
			std::wstring label, tag;
			D2D1::ColorF col(1, 1, 1);
			std::wstring sign = (g.value >= 0) ? L"+" : L"";
			if (g.kind == "gold") { label = sign + std::to_wstring(g.value) + L" ゴールド"; col = D2D1::ColorF(1.0f, 0.9f, 0.3f); }
			else if (g.kind == "hp") { label = sign + std::to_wstring(g.value) + L" HP"; col = g.value >= 0 ? D2D1::ColorF(0.5f, 1.0f, 0.5f) : D2D1::ColorF(1.0f, 0.45f, 0.45f); }
			else if (g.kind == "maxHp") { label = sign + std::to_wstring(g.value) + L" 最大HP"; col = D2D1::ColorF(1.0f, 0.6f, 0.6f); }
			else if (g.kind == "card") { const CardData* d = CardDataBase::Get(g.id); label = d ? d->name : ToWString(g.id); tag = L"カード"; }
			else { std::wstring ds; ItemTooltip::Info(g.id, label, ds, tag); if (g.value > 1) label += L" ×" + std::to_wstring(g.value); }
			m_textRenderer->DrawText(label.c_str(), r.x + 54.0f, r.y + 11.0f, 18.0f, col);
			if (!tag.empty())
				m_textRenderer->DrawText(tag.c_str(), r.x + r.w - 72.0f, r.y + 14.0f, 13.0f, D2D1::ColorF(0.7f, 0.8f, 1.0f));
			if (g.kind == "hp" && g.value < 0)   // バーの上に今のHP
			{
				const auto& pdh = PlayerDataManager::GetData();
				wchar_t hb[32]; swprintf_s(hb, L"%d / %d", pdh.hp, pdh.maxHp);
				m_textRenderer->DrawOutlinedTextCentered(hb, r.x + 150.0f + (r.w - 164.0f) / 2.0f, r.y + 14.0f, r.w - 164.0f, 16.0f,
					13.0f, D2D1::ColorF(1, 1, 1), D2D1::ColorF(0, 0, 0), 1.5f);
			}
		}
		m_textRenderer->DrawTextCentered(L"クリックで進む", cx, p.y + p.h - 64.0f, 240.0f, 17.0f,
			D2D1::ColorF(0.9f, 0.9f, 0.85f), 40.0f);
	}
	m_textRenderer->End();
	// 受け取った物の説明（ホバー）：素材・レリックはゲーム中と同じ説明窓、カードは拡大＋キーワード
	if (result)
		for (int i = 0; i < (int)m_eventGains.size(); i++)
		{
			const auto& g = m_eventGains[i];
			URect r = EventGainRect(i, p);
			if (!r.has(mp)) continue;
			if (g.kind == "material" || g.kind == "relic")
				ItemTooltip::Draw(m_uiSprite, m_textRenderer, white, g.id, r.x + r.w + 12.0f, r.y, m_screenWidth, m_screenHeight);
			else if (g.kind == "card")
				if (const CardData* d = CardDataBase::Get(g.id))
				{
					const float s = 1.4f;
					float w = CardVisual::CARD_W * s, h = CardVisual::CARD_H * s;
					float tx = r.x + r.w + 16.0f, ty = r.y + r.h / 2.0f - h / 2.0f;
					if (tx + w > m_screenWidth - 6.0f) tx = r.x - 16.0f - w;   // 右に入らなければ左
					if (ty < 46.0f) ty = 46.0f;
					float bx = tx - CardVisual::CARD_W * (1 - s) / 2.0f, by = ty - CardVisual::CARD_H * (1 - s) / 2.0f;
					m_uiSprite->Begin();  DrawBigCard(d, bx, by, s, false); m_uiSprite->End();
					m_textRenderer->Begin(); DrawBigCard(d, bx, by, s, true); m_textRenderer->End();
					CardTooltip::Draw(m_uiSprite, m_textRenderer, white, d, tx + w / 2.0f, ty, w, h, m_screenWidth, m_screenHeight);
				}
			break;
		}

	// 選択肢で手に入るカードを、窓の右に実物で見せる（隣の文字より手前に出すので最後に描く）
	if (m_eventResult < 0)
		for (int i = 0; i < n; i++)
		{
			float x, y, w, h; GetEventChoiceRect(i, x, y, w, h);
			if (!ChoiceEnabled(e->choices[i]) || mp.x < x || mp.x > x + w || mp.y < y || mp.y > y + h) continue;

			std::vector<const CardData*> cards;
			for (auto& o : e->choices[i].outcomes)
				if (o.type == "addCard")
					if (const CardData* d = CardDataBase::Get(o.param)) cards.push_back(d);
			if (cards.empty()) break;

			const float s = 1.4f, cw = CardVisual::CARD_W * s, ch = CardVisual::CARD_H * s, gap = 12.0f;
			float tx = p.x + p.w + 16.0f, ty = y + h / 2.0f - ch / 2.0f;
			if (ty < 46.0f) ty = 46.0f;
			bool curse = false;
			for (int k = 0; k < (int)cards.size(); k++)
			{
				float bx = tx + k * (cw + gap) - CardVisual::CARD_W * (1 - s) / 2.0f;
				float by = ty - CardVisual::CARD_H * (1 - s) / 2.0f;
				m_uiSprite->Begin();  DrawBigCard(cards[k], bx, by, s, false); m_uiSprite->End();
				m_textRenderer->Begin(); DrawBigCard(cards[k], bx, by, s, true); m_textRenderer->End();
				if (cards[k]->type == CardType::Curse) curse = true;
			}
			m_textRenderer->Begin();
			m_textRenderer->DrawOutlinedTextCentered(curse ? L"呪いが加わる" : L"デッキに加わる",
				tx + cw / 2.0f, ty - 30.0f, cw + 40.0f, 24.0f, 16.0f,
				curse ? D2D1::ColorF(1.0f, 0.45f, 0.45f) : D2D1::ColorF(1.0f, 0.9f, 0.6f), D2D1::ColorF(0, 0, 0), 1.5f);
			m_textRenderer->End();
			break;
		}
}

void SceneManager::DrawBossRelicPick()
{
	auto& choices = PlayerDataManager::GetData().bossRelicChoices;
	int n = (int)choices.size();
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	POINT mp = m_uiInput.GetMousePos();
	const float bnW = 520.0f, bnH = 90.0f, bnX = m_screenWidth / 2.0f - bnW / 2.0f, bnY = 64.0f;

	// ---- スプライト ----
	m_uiSprite->Begin();
	m_uiSprite->DrawSprite(white, 0, BAR_H, (float)m_screenWidth, (float)m_screenHeight - BAR_H, 0.0f,
		XMFLOAT4(0.05f, 0.03f, 0.09f, 0.92f));
	m_uiSprite->DrawSprite(TextureManager::Get("ui_banner"), bnX, bnY, bnW, bnH, 0.0f, XMFLOAT4(1, 1, 1, 1));
	int hovIdx = -1;
	for (int i = 0; i < n; i++)
	{
		URect r = BossRelicCardRect(i, n);
		char key[16]; sprintf_s(key, "boss%d", i);
		bool hov = UiHover(r.x, r.y, r.w, r.h, mp, key, s_lastHoverKey);
		if (hov) hovIdx = i;
		float y = hov ? r.y - 6.0f : r.y;
		UiWindow::Draw(m_uiSprite, white, r.x, y, r.w, r.h);
		if (hov) UiWindow::Frame(m_uiSprite, white, r.x, y, r.w, r.h, XMFLOAT4(1.0f, 0.85f, 0.2f, 1.0f));
		// アイコン（個別アイコンがあればボスの色で染める。無ければ台座の円）
		const RelicDef* rdef = RelicManager::Get(choices[i]);
		auto icon = (rdef && !rdef->icon.empty()) ? TextureManager::Get(rdef->icon) : nullptr;
		m_uiSprite->DrawSprite(icon ? icon : TextureManager::Get("particle"), r.x + r.w / 2.0f - 44.0f, y + 22.0f, 88.0f, 88.0f, 0.0f,
			icon ? XMFLOAT4(0.95f, 0.4f, 0.4f, 1.0f) : XMFLOAT4(0.9f, 0.3f, 0.3f, 0.35f));
	}
	URect sk = BossRelicSkipRect();
	bool skHov = UiHover(sk.x, sk.y, sk.w, sk.h, mp, "bossSkip", s_lastHoverKey);
	UiWindow::Button(m_uiSprite, white, sk.x, skHov ? sk.y - 3.0f : sk.y, sk.w, sk.h,
		skHov ? XMFLOAT4(0.4f, 0.4f, 0.5f, 1) : XMFLOAT4(0.25f, 0.28f, 0.4f, 1));
	m_uiSprite->End();

	// ---- 文字 ----
	m_textRenderer->Begin();
	m_textRenderer->DrawOutlinedTextCentered(L"ボスのレリックを1つ選ぶ", bnX + bnW / 2.0f, bnY, bnW, bnH, 28.0f,
		D2D1::ColorF(1.0f, 0.95f, 0.8f), D2D1::ColorF(0.25f, 0.04f, 0.06f), 3.0f);
	for (int i = 0; i < n; i++)
	{
		const RelicDef* d = RelicManager::Get(choices[i]);
		if (!d) continue;
		URect r = BossRelicCardRect(i, n);
		float y = r.has(mp) ? r.y - 6.0f : r.y;
		m_textRenderer->DrawTextCentered(ToWString(d->name).c_str(), r.x + r.w / 2.0f, y + 124.0f, r.w, 24.0f,
			D2D1::ColorF(1.0f, 0.8f, 0.75f));

		// 説明：行ごとに折り返し、「代償」で始まる行は赤
		std::wstring desc = ToWString(d->desc);
		float ly = y + 168.0f;
		size_t s0 = 0;
		while (true)
		{
			size_t e = desc.find(L'\n', s0);
			std::wstring line = desc.substr(s0, e == std::wstring::npos ? std::wstring::npos : e - s0);
			bool cost = (line.rfind(L"代償", 0) == 0);
			std::wstring wrapped = CardVisual::WrapText(line, 15);
			int lines = 1; for (wchar_t c : wrapped) if (c == L'\n') lines++;
			m_textRenderer->DrawTextCentered(wrapped.c_str(), r.x + r.w / 2.0f, ly, r.w - 30.0f, 16.0f,
				cost ? D2D1::ColorF(1.0f, 0.45f, 0.45f) : D2D1::ColorF(0.92f, 0.92f, 0.92f), lines * 22.0f);
			ly += lines * 22.0f + 8.0f;
			if (e == std::wstring::npos) break;
			s0 = e + 1;
		}
	}
	m_textRenderer->DrawTextCentered(L"受け取らない", sk.x + sk.w / 2.0f, skHov ? sk.y - 3.0f : sk.y, sk.w, 18.0f,
		D2D1::ColorF(1, 1, 1), sk.h);
	m_textRenderer->End();

	// ホバー中のレリックが代償でカードを加えるなら、その実物を窓の右に並べる
	// （隣の窓の文字より手前に出したいので、全部描いた後に別のパスで描く）
	if (hovIdx >= 0)
	{
		const RelicDef* hd = RelicManager::Get(choices[hovIdx]);
		const CardData* ac = (hd && !hd->AddedCard().empty()) ? CardDataBase::Get(hd->AddedCard()) : nullptr;
		if (ac)
		{
			const float S = 1.2f;
			URect r = BossRelicCardRect(hovIdx, n);
			float cx = r.x + r.w + 24.0f, cy = r.y + 60.0f;   // カードの基準位置
			m_uiSprite->Begin();
			CardVisual::DrawBase(m_uiSprite, white, cx, cy, S, 0.0f, CardVisual::GetCardColor(ac->type, false), ac, m_uiTime);
			m_uiSprite->End();
			m_textRenderer->Begin();
			CardVisual::DrawTexts(m_textRenderer, ac, nullptr, cx, cy, S, 0.0f, 1.0f);
			float x, y, w, h; CardVisual::GetRect(cx, cy, S, x, y, w, h);
			m_textRenderer->DrawOutlinedTextCentered(L"代償で加わる", x + w / 2.0f, y - 30.0f, w + 40.0f, 24.0f, 16.0f,
				D2D1::ColorF(1.0f, 0.45f, 0.45f), D2D1::ColorF(0, 0, 0), 1.5f);
			m_textRenderer->End();
		}
	}
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
	y = 150.0f + (i / perRow) * (ch + gapY) - m_eventPickScroll.pos;   // スクロール分ずらす
}

URect SceneManager::EventPickerPanel() const
{
	const float s = 0.85f, cw = CardVisual::CARD_W * s, ch = CardVisual::CARD_H * s;
	int n = (int)PlayerDataManager::GetData().deck.size();
	int rows = (n + 7) / 8; if (rows < 1) rows = 1;     // 1行8枚（GetEventCardSlotと同じ）
	float gridW = 8 * cw + 7 * 12.0f;
	float h = rows * (ch + 16.0f) + 32.0f, maxH = m_screenHeight - 126.0f - 14.0f;   // 画面に収まらない分はスクロール
	return { (m_screenWidth - gridW) / 2.0f - 24.0f, 126.0f, gridW + 48.0f, h < maxH ? h : maxH };
}

int SceneManager::EventCardAt(POINT p) const
{
	auto& deck = PlayerDataManager::GetData().deck;
	for (int i = 0; i < (int)deck.size(); i++)
	{
		if (!EventPickable(m_eventPickType, deck[i])) continue;   // 強化できない／外せないカードは選べない
		float bx, by; GetEventCardSlot(i, bx, by);
		float x, y, w, h; CardVisual::GetRect(bx, by, 0.85f, x, y, w, h);
		if (m_eventPickScroll.AlphaOfRect(y, h) < 0.5f) continue;   // 端で薄くなっているカードは押せない
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
	auto pickAlpha = [&](float bx, float by) {   // スクロールの端で薄くする濃さ
		float x, y, w, h; CardVisual::GetRect(bx, by, 0.85f, x, y, w, h);
		return m_eventPickScroll.AlphaOfRect(y, h); };
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
	URect pn = EventPickerPanel();
	UiWindow::Draw(m_uiSprite, white, pn.x, pn.y, pn.w, pn.h);   // 一覧を囲む窓
	if (!anim)                                                     // 見出しの布バナー
		m_uiSprite->DrawSprite(TextureManager::Get("ui_banner"), m_screenWidth / 2.0f - 240.0f, pn.y - 86.0f,
			480.0f, 84.0f, 0.0f, XMFLOAT4(1, 1, 1, 1));
	for (int i = 0; i < (int)deck.size(); i++)
	{
		if (i == m_eventPickAnimIdx) continue;
		const CardData* d = CardDataBase::Get(deck[i]);
		if (!d) continue;
		float bx, by; GetEventCardSlot(i, bx, by);
		float a = pickAlpha(bx, by); if (a <= 0.0f) continue;   // 見せる範囲の外
		by -= HoverLift::Get(bx, by, i == hov, 12.0f);
		if (i == hov)   // ホバー：色は変えず、浮かせて金の縁
		{
			float rx, ry, rw, rh; CardVisual::GetRect(bx, by, 0.85f, rx, ry, rw, rh);
			UiWindow::Frame(m_uiSprite, white, rx, ry, rw, rh, XMFLOAT4(1.0f, 0.85f, 0.2f, 1.0f));
		}
		XMFLOAT4 col = CardVisual::GetCardColor(d->type); col.w = a;
		CardVisual::DrawBase(m_uiSprite, white, bx, by, 0.85f, 0.0f, col, d, m_uiTime);
		if (!EventPickable(m_eventPickType, deck[i]))   // 選べないカードは暗く
		{
			float rx, ry, rw, rh; CardVisual::GetRect(bx, by, 0.85f, rx, ry, rw, rh);
			m_uiSprite->DrawSprite(white, rx, ry, rw, rh, 0.0f, XMFLOAT4(0, 0, 0, 0.55f * a));
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
		m_textRenderer->DrawOutlinedTextCentered(prompt.c_str(), m_screenWidth / 2.0f, pn.y - 86.0f, 480.0f, 84.0f, 26.0f,
			D2D1::ColorF(1.0f, 0.95f, 0.8f), D2D1::ColorF(0.25f, 0.04f, 0.06f), 3.0f);
	}
	for (int i = 0; i < (int)deck.size(); i++)
	{
		if (i == m_eventPickAnimIdx) continue;
		const CardData* d = CardDataBase::Get(deck[i]);
		if (!d) continue;
		float bx, by; GetEventCardSlot(i, bx, by);
		float a = pickAlpha(bx, by); if (a <= 0.0f) continue;
		by -= HoverLift::Get(bx, by, i == hov, 12.0f);
		CardVisual::DrawTexts(m_textRenderer, d, nullptr, bx, by, 0.85f, 0.0f, a);
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
		CardTooltip::Draw(m_uiSprite, m_textRenderer, white, d,
			rx + rw / 2.0f, ry, rw, rh, m_screenWidth, m_screenHeight, pn.y);   // 見出しのバナーより上には出さない

		// 強化：強化後の姿を横に並べて見せる（右に入らなければ左）
		if (m_eventPickType == "upgradeCard")
			if (const CardData* up = CardDataBase::Get(deck[hov] + "+"))
			{
				bool right = (rx + rw + 34.0f + CardVisual::CARD_W <= m_screenWidth - 6.0f);
				float px = right ? rx + rw + 34.0f : rx - 34.0f - CardVisual::CARD_W;
				float py = ry - 8.0f;
				m_uiSprite->Begin(); DrawBigCard(up, px, py, 1.0f, false); m_uiSprite->End();
				m_textRenderer->Begin();
				DrawBigCard(up, px, py, 1.0f, true);
				m_textRenderer->DrawOutlinedTextCentered(right ? L"▶" : L"◀", right ? rx + rw + 17.0f : rx - 17.0f,
					ry + rh / 2.0f - 14.0f, 30.0f, 28.0f, 22.0f,
					D2D1::ColorF(1.0f, 0.85f, 0.3f), D2D1::ColorF(0, 0, 0), 2.0f);
				m_textRenderer->End();
			}
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

URect SceneManager::EventRelicPanel() const
{
	int n = (int)PlayerDataManager::GetData().relics.size();
	int rows = (n + 5) / 6; if (rows < 1) rows = 1;   // 1行6個（EventRelicRectと同じ）
	URect r0 = EventRelicRect(0);
	float gridW = 6 * r0.w + 5 * 14.0f;
	return { r0.x - 24.0f, r0.y - 24.0f, gridW + 48.0f, rows * (r0.h + 14.0f) - 14.0f + 48.0f };
}

int SceneManager::EventRelicAt(POINT p) const
{
	int n = (int)PlayerDataManager::GetData().relics.size();
	for (int i = 0; i < n; i++)
		if (EventRelicRect(i).has(p)) return i;
	return -1;
}

void SceneManager::DrawEventRelicPicker()
{
	auto& relics = PlayerDataManager::GetData().relics;
	int n = (int)relics.size();
	bool anim = (m_eventPickAnimIdx >= 0);
	int hov = anim ? -1 : EventRelicAt(m_uiInput.GetMousePos());
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	float t = anim ? (1.0f - m_eventPickAnimTimer / EVENT_PICK_ANIM_DUR) : 0.0f;
	URect pn = EventRelicPanel();
	const float bnW = 480.0f, bnH = 84.0f, bnX = m_screenWidth / 2.0f - bnW / 2.0f, bnY = pn.y - 86.0f;

	// 入れ替え中の枠：前半＝元のレリックが消える／後半＝新しいレリックが現れる
	auto slotInfo = [&](int i, std::string& id, float& a) {
		id = relics[i]; a = 1.0f;
		if (i == m_eventPickAnimIdx)
		{
			if (t < 0.5f) a = 1.0f - t * 2.0f;
			else { a = (t - 0.5f) * 2.0f; id = m_eventPickAnimTo; }
		}
		};

	// ---- スプライト ----
	m_uiSprite->Begin();
	m_uiSprite->DrawSprite(white, 0, BAR_H, (float)m_screenWidth, (float)m_screenHeight - BAR_H, 0.0f, XMFLOAT4(0.05f, 0.03f, 0.09f, 0.96f));
	UiWindow::Draw(m_uiSprite, white, pn.x, pn.y, pn.w, pn.h);
	if (!anim)
		m_uiSprite->DrawSprite(TextureManager::Get("ui_banner"), bnX, bnY, bnW, bnH, 0.0f, XMFLOAT4(1, 1, 1, 1));
	for (int i = 0; i < n; i++)
	{
		URect r = EventRelicRect(i);
		std::string id; float a; slotInfo(i, id, a);
		float y = (i == hov) ? r.y - 6.0f : r.y;
		UiWindow::Draw(m_uiSprite, white, r.x, y, r.w, r.h);
		if (i == hov) UiWindow::Frame(m_uiSprite, white, r.x, y, r.w, r.h, XMFLOAT4(1.0f, 0.85f, 0.2f, 1.0f));

		const RelicDef* d = RelicManager::Get(id);
		if (!d) continue;
		auto tex = TextureManager::Get(d->icon);
		if (!tex) continue;
		const float is = 64.0f;
		float ix = r.x + (r.w - is) / 2.0f, iy = y + 14.0f;
		XMFLOAT4 c = RelicManager::RarityColor(d->rarity); c.w = a;
		m_uiSprite->DrawSprite(tex, ix + 2.0f, iy + 2.0f, is, is, 0.0f, XMFLOAT4(0, 0, 0, 0.65f * a));   // 影
		m_uiSprite->DrawSprite(tex, ix, iy, is, is, 0.0f, c);
		if (i == m_eventPickAnimIdx)   // 入れ替わる瞬間に白く光って膨らむ
		{
			float flash = 1.0f - fabsf(t - 0.5f) * 4.0f;
			if (flash > 0.0f)
			{
				float g = is * (1.0f + 0.4f * flash);
				m_uiSprite->DrawSprite(tex, ix + (is - g) / 2.0f, iy + (is - g) / 2.0f, g, g, 0.0f, XMFLOAT4(1, 1, 1, flash));
			}
		}
	}
	m_uiSprite->End();

	// ---- 文字 ----
	m_textRenderer->Begin();
	if (!anim)
		m_textRenderer->DrawOutlinedTextCentered(L"入れ替えるレリックを選ぶ", bnX + bnW / 2.0f, bnY, bnW, bnH, 26.0f,
			D2D1::ColorF(1.0f, 0.95f, 0.8f), D2D1::ColorF(0.25f, 0.04f, 0.06f), 3.0f);
	for (int i = 0; i < n; i++)
	{
		URect r = EventRelicRect(i);
		std::string id; float a; slotInfo(i, id, a);
		const RelicDef* d = RelicManager::Get(id);
		float y = (i == hov) ? r.y - 6.0f : r.y;
		m_textRenderer->DrawTextCentered((d ? ToWString(d->name) : L"?").c_str(), r.x + r.w / 2.0f, y + 86.0f,
			r.w - 12.0f, 15.0f, D2D1::ColorF(1, 1, 1, a), 24.0f);
	}
	m_textRenderer->End();

	// ---- ホバー中の説明（タイルの下に、ゲーム中と同じ説明窓） ----
	if (hov >= 0)
	{
		URect r = EventRelicRect(hov);
		ItemTooltip::Draw(m_uiSprite, m_textRenderer, white, relics[hov], r.x, r.y + r.h + 8.0f, m_screenWidth, m_screenHeight);
	}
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

// カードの説明を残せない画面（説明より手前に出るもの）
bool SceneManager::TooltipBlocked() const
{
	return m_glossaryOpen || m_settingsOpen || m_tutorialOpen || m_confirmAction != 0 || CardConfirm::Active()
		|| m_craftFxTimer > 0.0f || m_eventPickAnimIdx >= 0;   // 強化・合成・イベントのカード演出中も（演出後に閉じた画面の説明を残さない）
}

bool SceneManager::AnyModalOpen() const
{
	return m_tutorialOpen || m_deckOpen || m_craftOpen || m_invOpen
		|| m_mapOpen || m_restOpen || m_eventOpen || m_settingsOpen || m_glossaryOpen || CardConfirm::Active()
		|| m_confirmAction != 0 || BossRelicPickOpen();
}

bool SceneManager::BossRelicPickOpen() const
{
	return m_currentType == SceneType::Field && !PlayerDataManager::GetData().bossRelicChoices.empty();
}