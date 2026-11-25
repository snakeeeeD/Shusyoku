#include "GuideBox.h"
#include "TextureManager.h"
#include "UiWindow.h"
#include "Hint.h"
#include <cmath>

void GuideBox::Draw(UiContext& ui, const wchar_t* text, float a, const char* icon, const wchar_t* note)
{
	ID3D11ShaderResourceView* white = TextureManager::Get("white");
	const float ICON = 56.0f, NOTE_H = 18.0f;
	float w = 520.0f;
	float tx = 22.0f + (icon ? ICON + 12.0f : 0.0f);   // 文の左端（アイコンがあればその右）
	float tw = w - tx - 12.0f;                         // 文の幅。長い文はこの幅で折り返す
	float th = ui.text->Measure(text, 18.0f, FontRole::Body, tw).height;
	float body = 26.0f + th;                           // 注記を除いた高さ
	if (icon && body < ICON + 20.0f) body = ICON + 20.0f;
	float bh = body + (note ? NOTE_H : 0.0f);
	float x = (ui.screenW - w) / 2.0f, y = 110.0f;

	ui.sprite->Begin();
	UiWindow::Draw(ui.sprite, white, x, y, w, bh, XMFLOAT4(1, 1, 1, a));
	ui.sprite->DrawSprite(white, x + 8, y + 8, 4, bh - 16, 0.0f, XMFLOAT4(1.0f, 0.85f, 0.2f, a));   // 左の金帯＝ヒントの目印
	if (icon)
		ui.sprite->DrawSprite(TextureManager::Get(icon), x + 22.0f, y + (body - ICON) / 2.0f, ICON, ICON, 0.0f, XMFLOAT4(1, 1, 1, a));
	ui.sprite->End();

	ui.text->Begin();
	ui.text->DrawTextBox(text, x + tx, y + (body - th) / 2.0f, tw, 18.0f, D2D1::ColorF(1, 1, 1, a));
	if (note)
	{
		float nw = ui.text->Measure(note, 12.0f).width;
		ui.text->DrawText(note, x + w - 16.0f - nw, y + bh - NOTE_H - 6.0f, 12.0f, D2D1::ColorF(0.75f, 0.72f, 0.65f, a));
	}
	ui.text->End();
}


void GuideBox::DrawHint(UiContext& ui)
{
	const HintDef* h = Hint::Current();
	if (!h) return;
	float a = Hint::Alpha();
	if (h->hw > 0.0f)   // 指している場所を金枠で脈動
	{
		ui.sprite->Begin();
		UiWindow::Frame(ui.sprite, TextureManager::Get("white"), h->hx, h->hy, h->hw, h->hh,
			XMFLOAT4(1.0f, 0.85f, 0.2f, a * (0.6f + 0.4f * sinf(ui.time * 6.0f))));
		ui.sprite->End();
	}
	Draw(ui, h->text, a, nullptr, nullptr);
}

