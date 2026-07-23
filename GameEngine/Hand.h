#pragma once
#include "Card.h"
#include <vector>

class Deck;                       // ‘O•ûéŒ¾

class Hand
{
public:
	Hand();
	~Hand();

	void SetDeck(Deck* deck) { m_deck = deck; }   // ˆì‚ê‚½•ª‚Ìs‚«æ

	void AddCard(const std::string& id);          // –”t‚È‚çÌ‚ÄD‚Ö
	void RemoveCard(int index);
	void Clear();
	void UpgradeAll();
	void DiscardRandom(int count);
	void DiscardAt(int index);

	const std::vector<Card*>& GetCards() const { return m_cards; }
	int GetCardCount() const { return (int)m_cards.size(); }
	void ReserveSlot(bool on) { m_reserved = on; }
	bool IsFull() const { return (int)m_cards.size() - (m_reserved ? 1 : 0) >= MAX_CARDS; }

	static constexpr int MAX_CARDS = 10;

private:
	std::vector<Card*> m_cards;
	Deck* m_deck = nullptr;
	bool m_reserved = false;
};