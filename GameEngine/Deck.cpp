#include "Deck.h"

Deck::Deck() {}

void Deck::AddCard(const std::string& id)
{
    m_drawPile.push_back(id);
}

std::string Deck::DrawCard()
{
    // ŽRŽD‚ª‹ó‚È‚çŽÌ‚ÄŽD‚ðƒVƒƒƒbƒtƒ‹‚µ‚ÄŽRŽD‚É–ß‚·
    if (m_drawPile.empty())
    {
        if (m_discardPile.empty()) return ""; // —¼•û‹ó‚È‚ç‰½‚à‚È‚µ
        Reset();
    }

    std::string card = m_drawPile.back();
    m_drawPile.pop_back();
    return card;
}

void Deck::DiscardCard(const std::string& id)
{
    m_discardPile.push_back(id);
}

void Deck::ShuffleDrawPile()
{
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(m_drawPile.begin(), m_drawPile.end(), g);
}

void Deck::Reset()
{
    m_drawPile = m_discardPile;
    m_discardPile.clear();
    ShuffleDrawPile();
}