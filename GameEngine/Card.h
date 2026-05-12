#pragma once
#include "CardData.h"

class Card
{
public:
	Card(const std::string& id);

	const CardData* GetData() const { return m_data; }
	const std::string& GetId() const { return m_data->id; }

private:
	const CardData* m_data;
};

