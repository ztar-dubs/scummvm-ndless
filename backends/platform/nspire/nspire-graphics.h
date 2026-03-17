/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef BACKENDS_GRAPHICS_NSPIRE_H
#define BACKENDS_GRAPHICS_NSPIRE_H

#include "backends/graphics/graphics.h"
#include "graphics/surface.h"
#include "graphics/paletteman.h"
#include "common/mutex.h"

struct SDL_Surface;

#define NSPIRE_SCREEN_WIDTH  320
#define NSPIRE_SCREEN_HEIGHT 240

class NspireGraphicsManager : public GraphicsManager {
public:
	NspireGraphicsManager(SDL_Surface *screen);
	virtual ~NspireGraphicsManager();

	bool hasFeature(OSystem::Feature f) const override;
	void setFeatureState(OSystem::Feature f, bool enable) override;
	bool getFeatureState(OSystem::Feature f) const override;

	const OSystem::GraphicsMode *getSupportedGraphicsModes() const override;
	int getDefaultGraphicsMode() const override;
	bool setGraphicsMode(int mode, uint flags = OSystem::kGfxModeNoFlags) override;
	int getGraphicsMode() const override;

#ifdef USE_RGB_COLOR
	Graphics::PixelFormat getScreenFormat() const override;
	Common::List<Graphics::PixelFormat> getSupportedFormats() const override;
#endif

	void initSize(uint width, uint height, const Graphics::PixelFormat *format = nullptr) override;
	int getScreenChangeID() const override;

	void beginGFXTransaction() override;
	OSystem::TransactionError endGFXTransaction() override;

	int16 getHeight() const override;
	int16 getWidth() const override;

	void setPalette(const byte *colors, uint start, uint num) override;
	void grabPalette(byte *colors, uint start, uint num) const override;

	void copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) override;
	Graphics::Surface *lockScreen() override;
	void unlockScreen() override;
	void fillScreen(uint32 col) override;
	void fillScreen(const Common::Rect &r, uint32 col) override;
	void updateScreen() override;

	void setShakePos(int shakeXOffset, int shakeYOffset) override;
	void setFocusRectangle(const Common::Rect &rect) override;
	void clearFocusRectangle() override;

	// Overlay
	void showOverlay(bool inGUI) override;
	void hideOverlay() override;
	bool isOverlayVisible() const override;
	Graphics::PixelFormat getOverlayFormat() const override;
	void clearOverlay() override;
	void grabOverlay(Graphics::Surface &surface) const override;
	void copyRectToOverlay(const void *buf, int pitch, int x, int y, int w, int h) override;
	int16 getOverlayHeight() const override;
	int16 getOverlayWidth() const override;

	// Mouse cursor
	bool showMouse(bool visible) override;
	void warpMouse(int x, int y) override;
	void setMouseCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY, uint32 keycolor,
	                    bool dontScale = false, const Graphics::PixelFormat *format = nullptr,
	                    const byte *mask = nullptr) override;
	void setCursorPalette(const byte *colors, uint start, uint num) override;

private:
	SDL_Surface *_sdlScreen;    // The SDL hardware surface (320x240 RGB565)

	// Game screen buffer (CLUT8 or RGB565 depending on game)
	Graphics::Surface _gameScreen;
	uint _gameWidth, _gameHeight;
	Graphics::PixelFormat _gameFormat;
	int _screenChangeID;

	// Palette for CLUT8 mode
	byte _palette[256 * 3];
	uint16 _palette565[256]; // Pre-converted RGB565 palette

	// Overlay (RGB565)
	Graphics::Surface _overlay;
	bool _overlayVisible;

	// Mouse cursor
	Graphics::Surface _cursor;
	int _cursorHotspotX, _cursorHotspotY;
	uint32 _cursorKeycolor;
	bool _cursorVisible;
	int _cursorX, _cursorY;
	byte _cursorPalette[256 * 3];
	bool _cursorPaletteEnabled;

	// Shake
	int _shakeXOffset, _shakeYOffset;

	// Scaling helpers
	int _scaleX[NSPIRE_SCREEN_WIDTH];
	int _scaleY[NSPIRE_SCREEN_HEIGHT];
	void computeScaleTables();

	// Dirty rectangle tracking
	static const int DIRTY_BLOCK_W = 16;
	static const int DIRTY_BLOCK_H = 16;
	static const int DIRTY_COLS = (NSPIRE_SCREEN_WIDTH + DIRTY_BLOCK_W - 1) / DIRTY_BLOCK_W;   // 20
	static const int DIRTY_ROWS = (NSPIRE_SCREEN_HEIGHT + DIRTY_BLOCK_H - 1) / DIRTY_BLOCK_H;  // 15
	bool _dirtyBlocks[DIRTY_ROWS][DIRTY_COLS];
	bool _fullRedraw;

	// Overlay dirty tracking
	bool _overlayDirty;

	// Cursor save/restore
	uint16 _cursorSave[5 * 5];   // pixels under crosshair (5x5 max)
	int _cursorSaveX, _cursorSaveY;
	bool _cursorSaved;

	// Converted line cache for vertical scaling duplicate detection
	uint16 _lineCache[NSPIRE_SCREEN_WIDTH];
	int _lineCacheSrcY;

	// VKB previous selected key for partial redraw
	int _vkbPrevSelected;

	void blitToSDLSurface();
	void markDirtyGame(int x, int y, int w, int h);
	bool _vkbActive;
	int _vkbSelected;
	bool _vkbCurrent[80];  // Key assigned to current action (blue)
	bool _vkbBlocked[80];  // Keys assigned to other actions (blocked)
	void drawVkbOverlay(uint16 *dst, int dstPitch);
	void drawVkbKey(uint16 *dst, int dstPitch, int keyIdx, bool selected);

public:
	void setVkbState(bool active, int selected) { _vkbActive = active; _vkbSelected = selected; }
	void setVkbKeyCurrent(int idx, bool current) { if (idx >= 0 && idx < 80) _vkbCurrent[idx] = current; }
	void setVkbKeyBlocked(int idx, bool blocked) { if (idx >= 0 && idx < 80) _vkbBlocked[idx] = blocked; }
	void clearVkbMarkers() { memset(_vkbCurrent, 0, sizeof(_vkbCurrent)); memset(_vkbBlocked, 0, sizeof(_vkbBlocked)); }

	// Set cursor position directly from screen coordinates (for internal use by OSystem_NSPIRE)
	void setCursorScreenPos(int screenX, int screenY) { _cursorX = screenX; _cursorY = screenY; }

	// Convert screen coordinates to virtual (game/overlay) coordinates for mouse events
	void screenToVirtual(int screenX, int screenY, int &virtX, int &virtY) const {
		if (_overlayVisible || _gameWidth == 0 || _gameHeight == 0) {
			virtX = screenX;
			virtY = screenY;
		} else {
			virtX = screenX * (int)_gameWidth / NSPIRE_SCREEN_WIDTH;
			virtY = screenY * (int)_gameHeight / NSPIRE_SCREEN_HEIGHT;
		}
	}

private:
	inline uint16 rgb888ToRgb565(byte r, byte g, byte b) const {
		return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
	}
	void updatePalette565();
};

#endif // BACKENDS_GRAPHICS_NSPIRE_H
