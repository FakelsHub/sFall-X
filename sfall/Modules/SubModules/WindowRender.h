/*
 *    sfall
 *    Copyright (C) 2008 - 2020  The sfall team
 *
 */

#pragma once

namespace sfall
{

class WindowRender {
public:
	static void init();

	static void CreateOverlaySurface(fo::Window* win, long winType);
	static void DestroyOverlaySurface(fo::Window* win);
	static void ClearOverlay(fo::Window* win);
	static void ClearOverlay(fo::Window* win, Rectangle &rect);
	static BYTE* GetOverlaySurface(fo::Window* win);
};

}