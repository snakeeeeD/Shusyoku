#pragma once
#include <functional>

enum class TurnState
{
	PlayerTurn,
	EnemyTurn,
};


class TurnManager
{
public:
	TurnManager();

	void StartPlayerTurn();
	void StartEnemyTurn();
	void EndTurn();

	bool IsPlayerTurn() const { return m_state == TurnState::PlayerTurn; }
	bool IsEnemyTurn() const { return m_state == TurnState::EnemyTurn; }
	int GetTurnCount() const { return m_turnCount; }

	std::function<void()> onPlayerTurnStart;
	std::function<void()> onEnemyTurnStart;

private:
	TurnState m_state;
	int m_turnCount;
}; 

