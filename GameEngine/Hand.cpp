#include "Hand.h"

Hand::Hand() {}

Hand::~Hand()
{
	Clear();
}

void Hand::AddCard(const std::string& id)
{
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