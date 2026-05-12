#pragma once
#include <vector>
#include <string>
#include <algorithm>
#include <random>

class Deck
{
public:
	Deck();

	void AddCard(const std::string& id);	 // デッキにカードを追加
	std::string DrawCard();					 // 山札から一枚引く
	void DiscardCard(const std::string& id); // 捨て札に追加
	void ShuffleDrawPile();					 // 山札をシャッフル
	void Reset();							 // 捨て札を山札に戻してシャッフル

	int GetDrawPileCount()    const { return (int)m_drawPile.size(); }
	int GetDiscardPileCount() const { return (int)m_discardPile.size(); }

private:
	std::vector < std::string> m_drawPile;	// 山札
	std::vector<std::string> m_discardPile; // 捨て札
};

