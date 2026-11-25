#include "Hand.h"
#include "Deck.h"
#include "UiNotice.h"
#include "CardDataBase.h"

Hand::Hand() {}

Hand::~Hand()
{
	Clear();
}

Card* Hand::AddCard(const std::string& id)
{
	if (!CardDataBase::Get(id)) return nullptr;

	if ((int)m_cards.size() - (m_reserved ? 1 : 0) >= MAX_CARDS)
	{
		if (m_deck) m_deck->DiscardCard(id);   // 手札上限を超えた分は捨て札へ
		UiNotice::Show(NoticeType::HandFull);
		return nullptr;
	}
	m_cards.push_back(new Card(id));
	return m_cards.back();
}

bool Hand::AddCardFront(const std::string& id)
{
	if (!CardDataBase::Get(id)) return false;
	if ((int)m_cards.size() - (m_reserved ? 1 : 0) >= MAX_CARDS)
	{
		if (m_deck) m_deck->DiscardCard(id);   // 手札上限を超えた分は捨て札へ（AddCard と同じ）
		UiNotice::Show(NoticeType::HandFull);
		return false;
	}
	m_cards.insert(m_cards.begin(), new Card(id));
	return true;
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
		if (!id.empty() && id.back() == '+') continue;      // 既に強化済み
		if (id.rfind("CRAFT:", 0) == 0) continue;           // 合成カードは強化不可（+でIDが壊れる）
		if (!CardDataBase::Get(id + "+")) continue;         // 強化版が無いカード
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