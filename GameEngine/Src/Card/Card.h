#pragma once
#include "CardData.h"

class Card
{
public:
	Card(const std::string& id);

	const CardData* GetData() const { return m_data; }
	const std::string& GetId() const { return m_data->id; }

	int  CostThisTurn() const { return m_costThisTurn; }   // この1枚の、このターンだけのコスト（-1＝いつものコスト）
	void SetCostThisTurn(int c) { m_costThisTurn = c; }

private:
	const CardData* m_data;
	int m_costThisTurn = -1;
};

