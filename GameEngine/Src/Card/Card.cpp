#include "Card.h"
#include "CardDataBase.h"

Card::Card(const std::string& id)
{
	m_data = CardDataBase::Get(id);
}