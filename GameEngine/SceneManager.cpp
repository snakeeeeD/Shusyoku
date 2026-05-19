#include "SceneManager.h"
#include "EnemyDataBase.h"
#include "CardDataBase.h"
#include "PlayerDataManager.h"

#include "TitleScene.h"
#include "BattleScene.h"
#include "CardSelectScene.h"
//#include "FieldScene.h"

SceneManager::SceneManager() : m_currentScene(nullptr)
{

}

SceneManager::~SceneManager()
{
	delete m_currentScene;
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
	CardDataBase::Init();
	PlayerDataManager::Init();

	// テクスチャ読み込み
	TextureManager::Load("white", L"Assets/White.png");
	TextureManager::Load("title", L"Assets/Title.png");
	TextureManager::Load("player", L"Assets/yuusya_game.png");
	TextureManager::Load("enemy", L"Assets/enemy.png");
	TextureManager::Load("enemy_slime", L"Assets/slime.png");
	TextureManager::Load("enemy_goblin", L"Assets/goblin.png");
	TextureManager::Load("enemy_orc", L"Assets/orc.png");

	m_textRenderer = new TextRenderer();
	if (!m_textRenderer->Init(device, context, swapChain))
		return false;

	// 最初はタイトルから
	ChangeScene(SceneType::Title);
	return true;
}

void SceneManager::ChangeScene(SceneType type)
{
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
			scene->onChangeScene = [this](SceneType type) { ChangeScene(type); };
			m_currentScene = scene;
			break;
		}
		//case SceneType::Field:  m_currentScene = new FieldScene();  break;
		case SceneType::CardSelect:
		{
			auto scene = new CardSelectScene();
			scene->onChangeScene = [this](SceneType type) {ChangeScene(type); };
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

void SceneManager::HandleInput()
{
	if (m_currentScene) m_currentScene->HandleInput();
}
