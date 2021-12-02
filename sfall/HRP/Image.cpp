﻿/*
 *    sfall
 *    Copyright (C) 2008 - 2021 Timeslip and sfall team
 *
 */

#include "..\FalloutEngine\Fallout2.h"

#include "Image.h"

namespace HRP
{

long Image::GetDarkColor(fo::PALETTE* palette) {
	long minValue = (palette->B + palette->G + palette->R);
	if (minValue == 0) return 0;

	long index = 0;

	// search index of the darkest color in the palette
	for (size_t i = 1; i < 256; i++)
	{
		long rgbVal = palette[i].B + palette[i].G + palette[i].R;
		if (rgbVal < minValue) {
			minValue = rgbVal;
			index = i;
		}
	}
	return index;
}

long Image::GetAspectSize(long sW, long sH, long* x, long* y, long &dW, long &dH) {
	float sWf = (float)sW;
	float sHf = (float)sH;

	float width = (float)dW;
	float height = (float)dH;
	float aspectD = (float)dW / dH;
	float aspectS = sWf / sHf;

	if (aspectD < aspectS) {
		dH = (long)((sHf / sWf) * dW);         // set new height
		long cy = (long)((height - dH) / 2); // shift y-offset center
		if (y) *y = cy;
		return cy * width;
	}
	else if (aspectD > aspectS) {
		dW = (long)((dH / sHf) * sWf);      // set new width
		long cx = (long)((width - dW) / 2); // shift x-offset center
		if (x) *x = cx;
		return cx;
	}
	return 0;
}

// Resizes src image to dWidth/dHeight size
void Image::Scale(BYTE* src, long sWidth, long sHeight, BYTE* dst, long dWidth, long dHeight, long dPitch, long sPitch) {
	if (dWidth <= 0 || dHeight <= 0) return;

	float sw = (float)sWidth;
	float sh = (float)sHeight;
	float xStep = sw / dWidth;
	float hStep = sh / dHeight;

	if (sPitch > 0) sWidth = sPitch;
	if (dPitch > 0) dPitch -= dWidth;

	float sy = 0.0f;
	do {
		float sx = 0.0f;
		int x = 0, w = dWidth;
		do {
			*dst++ = *(src + x); // copy pixel
			if (x < sw) {
				sx += xStep;
				if (sx >= 1.0f) {
					x += (int)sx;
					sx -= (int)sx;
				}
			}
		} while (--w);
		dst += dPitch;

		if (sHeight) {
			sy += hStep;
			if (sy >= 1.0f && --sHeight) {
				src += (sWidth * (int)sy);
				sy -= (int)sy;
			}
		}
	}
	while (--dHeight);
}

// Simplified implementation of the engine function FMtext_to_buff_ with text scaling
void Image::ScaleText(BYTE* dstSurf, const char* text, long txtWidth, long dstWidth, long colorFlags, float scaleFactor) {
	if (*text) {
		if (scaleFactor < 0.5f) scaleFactor = 0.5f;

		txtWidth = std::lround(txtWidth * scaleFactor);
		float step = 1.0f / scaleFactor;

		fo::BlendColorTableData* blendTable = fo::func::getColorBlendTable(colorFlags);
		fo::FontData* font = (fo::FontData*)fo::var::getInt(FO_VAR_gCurrentFont);

		BYTE* surface = dstSurf;
		do {
			unsigned char charText = *text;
			signed int fWidth;
			if (charText == ' ') {
				fWidth = *(DWORD*)&font->field2;
			} else {
				fWidth = *((DWORD*)&font->fieldA + 2 * charText);
			}
			fWidth >>= 16; // div 65536

			int fontWidth = std::lround(fWidth * scaleFactor);

			BYTE* _surface = &surface[fontWidth] + (*((DWORD*)&font->field0) >> 16); // position of the next character
			if ((_surface - dstSurf) > txtWidth) break; // something went wrong (out of size)

			int fontHeight = *((DWORD*)&font->fieldC + 2 * charText) >> 16;

			BYTE* dstPixels = &surface[dstWidth * (font->field0 - fontHeight)]; //
			BYTE* fontPixels = (BYTE*) (*(DWORD*)&font->eUnkArray[8 * charText + 2] + font->field80C);

			fontHeight = std::lround(fontHeight * scaleFactor);
			float fh = 0.0f;

			// copying character pixels
			for (int h = 0; h < fontHeight; h++)
			{
				float fw = 0.0f;
				int count = 0;
				for (int w = 0; w < fontWidth; w++) {
					// replaces the pixel of the image with the color from the blending table
					*dstPixels++ = blendTable->data[*fontPixels].colors[*dstPixels]; // pixel of the color index

					fw += step;
					if (fw >= 1.0f) {
						fw -= 1.0f;
						fontPixels++;
						count++;
					}
				}
				fh += step;
				if (fh < 1.0f) {
					fontPixels -= count;
				} else {
					fh -= 1.0f;
					fontPixels += (fWidth - count);
				}
				dstPixels += dstWidth - fontWidth;
			}
			surface = _surface;

			++text;
		} while (*text);

		fo::func::freeColorBlendTable(colorFlags);
	}
}

}