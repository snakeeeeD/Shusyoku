#pragma once
#include <vector>
#include <string>
#include <algorithm>
#include <random>

class Deck
{
public:
    Deck();

    void AddCard(const std::string& id);
    std::string DrawCard();
    std::string DrawSpecificCard(const std::string& id);
    void DiscardCard(const std::string& id);
    void ShuffleDrawPile();
    void Reset();

    int GetDrawPileCount()    const { return (int)m_drawPile.size(); }
    int GetDiscardPileCount() const { return (int)m_discardPile.size(); }

    const std::vector<std::string>& GetDrawPile()    const { return m_drawPile; }    // Å© í«â¡
    const std::vector<std::string>& GetDiscardPile() const { return m_discardPile; } // Å© í«â¡

private:
    std::vector<std::string> m_drawPile;
    std::vector<std::string> m_discardPile;
};