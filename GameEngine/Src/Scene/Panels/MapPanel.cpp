#include "MapPanel.h"
#include "PlayerDataManager.h"
#include "FieldScene.h"
#include "FieldNodeType.h"
#include "TextureManager.h"
#include "UiWindow.h"

// マスの色（現在地・通った所・種類）
static XMFLOAT4 MapNodeColor(FieldNodeType t, bool visited, bool isPlayer)
{
	if (isPlayer) return XMFLOAT4(1.0f, 1.0f, 0.0f, 1.0f);
	if (visited)  return XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
	switch (t)
	{
	case FieldNodeType::Battle: return XMFLOAT4(0.7f, 0.2f, 0.2f, 1.0f);
	case FieldNodeType::Rest:   return XMFLOAT4(0.2f, 0.7f, 0.2f, 1.0f);
	case FieldNodeType::Event:  return XMFLOAT4(0.6f, 0.3f, 0.8f, 1.0f);
	case FieldNodeType::Boss:   return XMFLOAT4(0.7f, 0.2f, 0.7f, 1.0f);
	case FieldNodeType::Start:  return XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
	case FieldNodeType::Shop:   return XMFLOAT4(0.2f, 0.7f, 0.7f, 1.0f);
	case FieldNodeType::Elite:  return XMFLOAT4(0.9f, 0.5f, 0.1f, 1.0f);
	case FieldNodeType::Treasure: return XMFLOAT4(0.95f, 0.78f, 0.2f, 1.0f);
	default:                    return XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
	}
}

void MapPanel::Draw(UiContext& ui)
{
	const int sw = ui.screenW, sh = ui.screenH;
	auto& pd = PlayerDataManager::GetData();
	ID3D11ShaderResourceView* white = TextureManager::Get("white");

	const int ROWS = FieldScene::GRID_ROWS, COLS = FieldScene::GRID_COLS;   // フィールドのグリッドと同じ
	bool haveMap = (int)pd.fieldNodeTypes.size() >= ROWS * COLS;

	const float CELL = 64.0f, GAP = 16.0f;
	float totalW = COLS * (CELL + GAP) - GAP;
	float totalH = ROWS * (CELL + GAP) - GAP;
	float offX = (sw - totalW) / 2.0f;
	float offY = (sh - totalH) / 2.0f + 20.0f;

	auto idxOf = [&](int c, int r) { return c * ROWS + r; };
	auto typeOf = [&](int c, int r) { return (FieldNodeType)pd.fieldNodeTypes[idxOf(c, r)]; };
	auto posOf = [&](int c, int r, float& x, float& y) { x = offX + c * (CELL + GAP); y = offY + r * (CELL + GAP); };

	POINT mp = ui.input->GetMousePos();
	FieldNodeType hoverType = FieldNodeType::Empty;
	float hoverX = 0.0f, hoverY = 0.0f;

	ui.sprite->Begin();
	ui.sprite->DrawSprite(white, 0, 0, (float)sw, (float)sh, 0.0f,
		XMFLOAT4(0.0f, 0.0f, 0.05f, 0.92f));

	if (haveMap)
	{
		// 接続線
		for (int r = 0; r < ROWS; r++)
			for (int c = 0; c < COLS; c++)
			{
				if (typeOf(c, r) == FieldNodeType::Empty) continue;
				float x, y; posOf(c, r, x, y);
				float cx1 = x + CELL / 2, cy1 = y + CELL / 2;
				if (c + 1 < COLS && typeOf(c + 1, r) != FieldNodeType::Empty)
					ui.sprite->DrawSprite(white, cx1, cy1 - 2, CELL + GAP, 4, 0.0f, XMFLOAT4(0.5f, 0.5f, 0.5f, 1));
				if (r + 1 < ROWS && typeOf(c, r + 1) != FieldNodeType::Empty)
					ui.sprite->DrawSprite(white, cx1 - 2, cy1, 4, CELL + GAP, 0.0f, XMFLOAT4(0.5f, 0.5f, 0.5f, 1));
			}
		// ノード
		for (int r = 0; r < ROWS; r++)
			for (int c = 0; c < COLS; c++)
			{
				FieldNodeType t = typeOf(c, r);
				if (t == FieldNodeType::Empty) continue;
				float x, y; posOf(c, r, x, y);
				bool isPlayer = (c == pd.fieldPlayerCol && r == pd.fieldPlayerRow);
				if (mp.x >= x && mp.x <= x + CELL && mp.y >= y && mp.y <= y + CELL)
				{
					hoverType = t; hoverX = x + CELL + 8.0f; hoverY = y;
				}
				ui.sprite->DrawSprite(TextureManager::Get("ui_node"), x, y, CELL, CELL, 0.0f,
					MapNodeColor(t, pd.fieldNodeVisited[idxOf(c, r)], isPlayer));
				const char* icon = isPlayer ? "node_player" : NodeIconName(t);
				if (icon[0])
				{
					float isz = CELL * 0.60f;
					ui.sprite->DrawSprite(TextureManager::Get(icon),
						x + (CELL - isz) / 2.0f, y + (CELL - isz) / 2.0f, isz, isz, 0.0f, XMFLOAT4(1, 1, 1, 1));
				}
			}
	}
	ui.sprite->End();

	ui.text->Begin();
	ui.text->DrawTextCentered(L"マップ", sw / 2.0f, offY - 44.0f, 300.0f, 26.0f, D2D1::ColorF(1, 1, 1));
	if (!haveMap)
		ui.text->DrawTextCentered(L"まだマップがない", sw / 2.0f, sh / 2.0f, 400.0f, 22.0f, D2D1::ColorF(0.7f, 0.7f, 0.7f));
	ui.text->DrawTextCentered(L"「戻る」で閉じる", sw / 2.0f, offY + totalH + 16.0f, 400.0f, 16.0f, D2D1::ColorF(0.7f, 0.7f, 0.7f));
	ui.text->End();

	// ホバーでマスの説明（フィールドの凡例と同じ内容）
	if (haveMap && hoverType != FieldNodeType::Empty)
	{
		const float tw = 320.0f, th = 56.0f;
		float tx = hoverX, ty = hoverY;
		if (tx + tw > sw - 8.0f) tx = sw - 8.0f - tw;   // 右端で左に寄せる
		if (ty + th > sh - 8.0f) ty = sh - 8.0f - th;
		ui.sprite->Begin();
		UiWindow::Draw(ui.sprite, white, tx, ty, tw, th);
		ui.sprite->End();
		ui.text->Begin();
		ui.text->DrawText(NodeDisplayName(hoverType), tx + 12.0f, ty + 8.0f, 18.0f, D2D1::ColorF(1, 0.9f, 0.6f));
		ui.text->DrawText(NodeDesc(hoverType), tx + 12.0f, ty + 32.0f, 14.0f, D2D1::ColorF(0.95f, 0.95f, 0.95f));
		ui.text->End();
	}
}

