#include "Hand.h"
#include "Deck.h"
#include "UiNotice.h"
#include "CardDataBase.h"

Hand::Hand() {}

Hand::~Hand()
{
	Clear();
}

void Hand::AddCard(const std::string& id)
{
	if ((int)m_cards.size() - (m_reserved ? 1 : 0) >= MAX_CARDS)
	{
		if (m_deck) m_deck->DiscardCard(id);   // èDãŒÀ‚ğ’´‚¦‚½•ª‚ÍÌ‚ÄD‚Ö
		UiNotice::Show(NoticeType::HandFull);
		return;
	}
	m_cards.push_back(new Card(id));
}

void Hand::RemoveCard(int index)
{
	if (index < 0 || index >= (int) m_cards.size()) return;
	delete m_cards[index];
	m_cards.erase(m_cards.begin() + index);
}

void Hand::Clear()
{
	for (auto card : m_cards)
		delete card;
	m_cards.clear();
}

void Hand::UpgradeAll()
{
	for (auto& card : m_cards)
	{
		std::string id = card->GetId();
		if (!id.empty() && id.back() == '+') continue;      // Šù‚É‹­‰»Ï‚İ
		if (!CardDataBase::Get(id + "+")) continue;         // ‹­‰»”Å‚ª–³‚¢ƒJ[ƒh
		delete card;
		card = new Card(id + "+");
	}
}

void Hand::DiscardRandom(int count)
{
	for (int n = 0; n < count && !m_cards.empty(); n++)
	{
		int idx = rand() % (int)m_cards.size();
		if (m_deck) m_deck->DiscardCard(m_cards[idx]->GetId());
		delete m_cards[idx];
		m_cards.erase(m_cards.begin() + idx);
	}
}

void Hand::DiscardAt(int index)
{
	if (index < 0 || index >= (int)m_cards.size()) return;
	if (m_deck) m_deck->DiscardCard(m_cards[index]->GetId());
	delete m_cards[index];
	m_cards.erase(m_cards.begin() + index);
}