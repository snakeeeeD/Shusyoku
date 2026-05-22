#pragma once
#include "CardData.h"
#include <DirectXMath.h>

using namespace DirectX;

class CardVisual
{
public:
    static XMFLOAT4 GetCardColor(CardType type, bool hovered = false)
    {
        XMFLOAT4 color;

        switch (type)
        {
        case CardType::Attack:
            color = XMFLOAT4(0.9f, 0.2f, 0.2f, 0.8f);
            break;

        case CardType::Skill:
            color = XMFLOAT4(0.2f, 0.4f, 1.0f, 0.8f);
            break;

        case CardType::Move:
            color = XMFLOAT4(0.2f, 0.9f, 0.3f, 0.8f);
            break;

        case CardType::Power:
            color = XMFLOAT4(0.8f, 0.2f, 1.0f, 0.8f);
            break;

        default:
            color = XMFLOAT4(0.7f, 0.7f, 0.7f, 1.0f);
            break;
        }

        // ÉzÉoÅ[éûÇÕè≠ÇµñæÇÈÇ≠
        if (hovered)
        {
            color = XMFLOAT4(0.2f, 0.4f, 0.8f, 1.0f);

        }

        return color;
    }
};