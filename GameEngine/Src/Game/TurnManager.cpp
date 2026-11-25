#include "TurnManager.h"
#include <Windows.h>

TurnManager::TurnManager()
	:m_state(TurnState::PlayerTurn)
	, m_turnCount(1)
{
}

void TurnManager::StartPlayerTurn()
{
	m_state = TurnState::PlayerTurn;
    if (onPlayerTurnStart) onPlayerTurnStart();
	OutputDebugStringW(L"プレイヤーターン開始\n");
}

void TurnManager::StartEnemyTurn()
{
	m_state = TurnState::EnemyTurn;
    if (onEnemyTurnStart) onEnemyTurnStart();
	OutputDebugStringW(L"敵ターン開始\n");
}

void TurnManager::EndTurn()
{
    if (IsPlayerTurn())
    {
        m_turnCount++;
        StartEnemyTurn();
    }
    else
    {
        StartPlayerTurn();
    }
}