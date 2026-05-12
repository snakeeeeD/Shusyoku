#pragma once
#include "Card.h"
#include <vector>

class Hand
{
public:
	Hand();
	~Hand();

	void AddCard(const std::string& id);
	void RemoveCard(int index);
	void Clear();

	const std::vector<Card*>& GetCards() const { return m_cards; }
	int GetCardCount() const { return (int)m_cards.size(); }

private:
	std::vector<Card*> m_cards;
};

