#pragma once
#include "Card.h"
#include <vector>

class Deck;                       // 前方宣言

class Hand
{
public:
	Hand();
	~Hand();

	void SetDeck(Deck* deck) { m_deck = deck; }   // 溢れた分の行き先

	Card* AddCard(const std::string& id);          // 満杯なら捨て札へ（入れたカードを返す。捨て札へ行ったら nullptr）
	bool AddCardFront(const std::string& id);     // 一番左に入れる（敵に入れられたお邪魔カード）。満杯なら捨て札へ行き false
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