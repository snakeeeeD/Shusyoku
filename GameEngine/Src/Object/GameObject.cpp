#include "GameObject.h"
#include "SpriteRenderer.h"
#include "Renderer3D.h"

GameObject::GameObject()
    : x(0), y(0)
    , width(64), height(64)
    , rotation(0)
    , worldX(0), worldY(0), worldZ(0)
    , gridCol(0), gridRow(0)
    , texture(nullptr)
    , color(1, 1, 1, 1)
    , isActive(true)
{
}

void GameObject::Draw(SpriteRenderer* renderer)
{
    if (!isActive || !texture) return;
    renderer->DrawSprite(texture, x, y, width, height, rotation, color);
}

void GameObject::Draw3D(Renderer3D* renderer)
{
    if (!isActive || !texture) return;
    renderer->DrawBillboard(texture, worldX, worldY, worldZ, width, height, rotation, color);
}