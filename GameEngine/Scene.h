#pragma once
#include<d3d11.h>

class Scene
{
public:
	virtual ~Scene() {};
	virtual bool Init(ID3D11Device* device, ID3D11DeviceContext* context,
		int screenWidth, int screenHeight, HWND hWnd,
		IDXGISwapChain* swapChain) = 0;
	virtual void Update(float deltaTime) = 0;
	virtual void Draw() = 0;
	virtual void HandleInput() = 0;
};
	