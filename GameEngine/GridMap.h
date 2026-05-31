#pragma once
#include <d3d11.h>
#include <DirectXMath.h>
#include <vector>
#include "GameObject.h"

using namespace DirectX;

// ƒ}ƒX‚ÌŽí—Þ
enum class CellType
{
	Empty,
	Player,
	Enemy,
	Boss,
	Wall
};

struct Cell
{
	CellType type;
	GameObject gameObject;
};

class GridMap
{
public:
	GridMap();
	~GridMap();


	struct ClickResult
	{
		Cell* cell;
		int col;
		int row;
	};


	void Init(int cols, int rows, float cellSize);
	void Draw(class SpriteRenderer* renderer);
	void SetCellType(int col, int row, CellType type);
	

	int GetCols() const { return m_cols; }
	int GetRows() const { return m_rows; }
	Cell& GetCell(int col, int row) { return m_cells[row][col]; }

	ClickResult GetClickedCell(POINT mousePos);

	ClickResult GetClickedCell3D(POINT mousePos,
		const XMMATRIX& viewMatrix,
		const XMMATRIX& projMatrix,
		int screenWidth, int screenHeight);

private:
	int m_cols;
	int m_rows;
	int m_cellSize;
	int m_offsetX;
	int m_offsetY;

	std::vector<std::vector<Cell>> m_cells;


};
