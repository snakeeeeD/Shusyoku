#include "SceneManager.h"
#include "EnemyDataBase.h"
#include "CardDataBase.h"
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

	m_textRenderer = new TextRenderer();
	if (!m_textRenderer->Init(device, context, swapChain))
		return false;

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

void SceneManager::Update(float deltaTime)
{
	if (m_currentScene) m_currentScene->Update(deltaTime);
}

void SceneManager::Draw()
{
	if (m_currentScene) m_currentScene->Draw();
}

void SceneManager::DrawImGui()
{
	if (m_currentScene)
		m_currentScene->DrawImGui();
}

void SceneManager::HandleInput()
{
	if (m_currentScene) m_currentScene->HandleInput();
}
