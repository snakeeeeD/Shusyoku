#include "GridMap.h"
#include "SpriteRenderer.h"

GridMap::GridMap()
    : m_cols(0), m_rows(0), m_cellSize(0)
    , m_offsetX(0), m_offsetY(0)
{
}

GridMap::~GridMap() {}

void GridMap::Init(int cols, int rows, float cellSize)
{
    m_cols = cols;
    m_rows = rows;
    m_cellSize = cellSize;

    m_offsetX = (1280.0f - cols * cellSize) / 2.0f;
    m_offsetY = (720.0f - rows * cellSize) / 2.0f;

    m_cells.resize(rows, std::vector<Cell>(cols));

    // 各マスのGameObjectを初期化
    for (int row = 0; row < rows; row++)
    {
        for (int col = 0; col < cols; col++)
        {
            auto& cell = m_cells[row][col];
            cell.type = CellType::Empty;
            cell.gameObject.x = m_offsetX + col * cellSize;
            cell.gameObject.y = m_offsetY + row * cellSize;
            cell.gameObject.width = cellSize - 2.0f;
            cell.gameObject.height = cellSize - 2.0f;
            cell.gameObject.color = XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
        }
    }
}

void GridMap::SetCellType(int col, int row, CellType type)
{
    auto& cell = m_cells[row][col];
    cell.type = type;

    switch (type)
    {
    case CellType::Empty:  cell.gameObject.color = XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f); break;
    case CellType::Player: cell.gameObject.color = XMFLOAT4(0.2f, 0.6f, 1.0f, 1.0f); break;
    case CellType::Enemy:  cell.gameObject.color = XMFLOAT4(1.0f, 0.3f, 0.3f, 1.0f); break;
    case CellType::Boss:   cell.gameObject.color = XMFLOAT4(0.8f, 0.0f, 0.8f, 1.0f);
        break;
    case CellType::Wall:   cell.gameObject.color = XMFLOAT4(0.1f, 0.1f, 0.1f, 1.0f); break;
    }
}

void GridMap::Draw(SpriteRenderer* renderer)
{
    for (int row = 0; row < m_rows; row++)
        for (int col = 0; col < m_cols; col++)
            m_cells[row][col].gameObject.Draw(renderer);
}

GridMap::ClickResult GridMap::GetClickedCell(POINT mousePos)
{
    for (int row = 0; row < m_rows; row++)
    {
        for (int col = 0; col < m_cols; col++)
        {
            auto& obj = m_cells[row][col].gameObject;
            if (mousePos.x >= obj.x && mousePos.x <= obj.x + obj.width &&
                mousePos.y >= obj.y && mousePos.y <= obj.y + obj.height)
            {
                return { &m_cells[row][col], col, row };
            }
        }
    }
    return { nullptr, -1, -1 };
}

GridMap::ClickResult GridMap::GetClickedCell3D(POINT mousePos,
    const XMMATRIX& viewMatrix,
    const XMMATRIX& projMatrix,
    int screenWidth, int screenHeight)
{
    for (int row = 0; row < m_rows; row++)
    {
        for (int col = 0; col < m_cols; col++)
        {
            // 3Dワールド座標
            float worldX = (col - m_cols / 2.0f) * 1.1f;
            float worldZ = (row - m_rows / 2.0f) * 1.1f;

            // タイルの4隅の座標（ワールド空間）
             XMFLOAT3 corners[4] = {
                XMFLOAT3(worldX - 0.55f, 0.0f, worldZ - 0.55f),
                XMFLOAT3(worldX + 0.55f, 0.0f, worldZ - 0.55f),
                XMFLOAT3(worldX + 0.55f, 0.0f, worldZ + 0.55f),
                XMFLOAT3(worldX - 0.55f, 0.0f, worldZ + 0.55f)
            };

            // 4隅をスクリーン座標に変換（順序を保持）
            XMFLOAT2 sc[4];
            bool allFront = true;
            for (int i = 0; i < 4; i++)
            {
                XMVECTOR worldPos = XMLoadFloat3(&corners[i]);
                XMVECTOR viewPos = XMVector3Transform(worldPos, viewMatrix);
                XMVECTOR projPos = XMVector3Transform(viewPos, projMatrix);

                XMFLOAT4 proj;
                XMStoreFloat4(&proj, projPos);

                if (proj.w <= 0.0f) { allFront = false; break; }

                sc[i].x = (proj.x / proj.w + 1.0f) * 0.5f * screenWidth;
                sc[i].y = (1.0f - proj.y / proj.w) * 0.5f * screenHeight;
            }
            if (!allFront) continue;

            // 凸四角形の内側判定（各辺の外積の符号がすべて一致すれば内側）
            float mx = (float)mousePos.x, my = (float)mousePos.y;
            int posCnt = 0, negCnt = 0;
            for (int i = 0; i < 4; i++)
            {
                XMFLOAT2 a = sc[i];
                XMFLOAT2 b = sc[(i + 1) % 4];
                float cross = (b.x - a.x) * (my - a.y) - (b.y - a.y) * (mx - a.x);
                if (cross > 0.0f) posCnt++;
                else if (cross < 0.0f) negCnt++;
            }
            if (posCnt == 0 || negCnt == 0)
            {
                return { &m_cells[row][col], col, row };
            }
        }
    }

    return { nullptr, -1, -1 };
}