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

#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include "common/scummsys.h"
#include "backends/platform/nspire/nspire-graphics.h"
#include "common/textconsole.h"

#include <SDL/SDL.h>
#include <string.h>

static const OSystem::GraphicsMode s_supportedGraphicsModes[] = {
	{ "default", "Default", 0 },
	{ nullptr, nullptr, 0 }
};

NspireGraphicsManager::NspireGraphicsManager(SDL_Surface *screen) :
	_sdlScreen(screen),
	_gameWidth(NSPIRE_SCREEN_WIDTH),
	_gameHeight(NSPIRE_SCREEN_HEIGHT),
	_screenChangeID(0),
	_overlayVisible(false),
	_cursorHotspotX(0), _cursorHotspotY(0),
	_cursorKeycolor(0),
	_cursorVisible(false),
	_cursorX(0), _cursorY(0),
	_cursorPaletteEnabled(false),
	_shakeXOffset(0), _shakeYOffset(0),
	_fullRedraw(true),
	_overlayDirty(true),
	_cursorSaveX(-1), _cursorSaveY(-1),
	_cursorSaved(false),
	_lineCacheSrcY(-1),
	_vkbPrevSelected(-1),
	_vkbActive(false), _vkbSelected(0) {
	memset(_vkbCurrent, 0, sizeof(_vkbCurrent));
	memset(_vkbBlocked, 0, sizeof(_vkbBlocked));
	memset(_dirtyBlocks, 1, sizeof(_dirtyBlocks));
	memset(_cursorSave, 0, sizeof(_cursorSave));
	memset(_lineCache, 0, sizeof(_lineCache));

	_gameFormat = Graphics::PixelFormat::createFormatCLUT8();
	memset(_palette, 0, sizeof(_palette));
	memset(_palette565, 0, sizeof(_palette565));
	memset(_cursorPalette, 0, sizeof(_cursorPalette));
	memset(_scaleX, 0, sizeof(_scaleX));
	memset(_scaleY, 0, sizeof(_scaleY));

	// Initialize the overlay surface (RGB565)
	_overlay.create(NSPIRE_SCREEN_WIDTH, NSPIRE_SCREEN_HEIGHT,
	                Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0));
}

NspireGraphicsManager::~NspireGraphicsManager() {
	_gameScreen.free();
	_overlay.free();
	_cursor.free();
}

bool NspireGraphicsManager::hasFeature(OSystem::Feature f) const {
	return false;
}

void NspireGraphicsManager::setFeatureState(OSystem::Feature f, bool enable) {
}

bool NspireGraphicsManager::getFeatureState(OSystem::Feature f) const {
	return false;
}

const OSystem::GraphicsMode *NspireGraphicsManager::getSupportedGraphicsModes() const {
	return s_supportedGraphicsModes;
}

int NspireGraphicsManager::getDefaultGraphicsMode() const {
	return 0;
}

bool NspireGraphicsManager::setGraphicsMode(int mode, uint flags) {
	return true;
}

int NspireGraphicsManager::getGraphicsMode() const {
	return 0;
}

#ifdef USE_RGB_COLOR
Graphics::PixelFormat NspireGraphicsManager::getScreenFormat() const {
	return _gameFormat;
}

Common::List<Graphics::PixelFormat> NspireGraphicsManager::getSupportedFormats() const {
	Common::List<Graphics::PixelFormat> list;
	// RGB565 native format
	list.push_back(Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0));
	// CLUT8
	list.push_back(Graphics::PixelFormat::createFormatCLUT8());
	return list;
}
#endif

void NspireGraphicsManager::initSize(uint width, uint height, const Graphics::PixelFormat *format) {
	_gameWidth = width;
	_gameHeight = height;

	if (format) {
		_gameFormat = *format;
	} else {
		_gameFormat = Graphics::PixelFormat::createFormatCLUT8();
	}

	_gameScreen.free();
	_gameScreen.create(width, height, _gameFormat);

	computeScaleTables();
	_screenChangeID++;
	_fullRedraw = true;
	_lineCacheSrcY = -1;
}

int NspireGraphicsManager::getScreenChangeID() const {
	return _screenChangeID;
}

void NspireGraphicsManager::beginGFXTransaction() {
}

OSystem::TransactionError NspireGraphicsManager::endGFXTransaction() {
	return OSystem::kTransactionSuccess;
}

int16 NspireGraphicsManager::getHeight() const {
	return _gameHeight;
}

int16 NspireGraphicsManager::getWidth() const {
	return _gameWidth;
}

void NspireGraphicsManager::setPalette(const byte *colors, uint start, uint num) {
	memcpy(_palette + start * 3, colors, num * 3);
	updatePalette565();
	_fullRedraw = true; // Palette change affects all pixels
}

void NspireGraphicsManager::grabPalette(byte *colors, uint start, uint num) const {
	memcpy(colors, _palette + start * 3, num * 3);
}

void NspireGraphicsManager::updatePalette565() {
	for (int i = 0; i < 256; i++) {
		_palette565[i] = rgb888ToRgb565(_palette[i * 3], _palette[i * 3 + 1], _palette[i * 3 + 2]);
	}
}

void NspireGraphicsManager::markDirtyGame(int x, int y, int w, int h) {
	// Convert game coordinates to screen coordinates, then mark dirty blocks
	if (_gameWidth == 0 || _gameHeight == 0) return;

	int sx0 = x * NSPIRE_SCREEN_WIDTH / (int)_gameWidth;
	int sy0 = y * NSPIRE_SCREEN_HEIGHT / (int)_gameHeight;
	int sx1 = (x + w) * NSPIRE_SCREEN_WIDTH / (int)_gameWidth;
	int sy1 = (y + h) * NSPIRE_SCREEN_HEIGHT / (int)_gameHeight;

	int bc0 = sx0 / DIRTY_BLOCK_W;
	int br0 = sy0 / DIRTY_BLOCK_H;
	int bc1 = sx1 / DIRTY_BLOCK_W;
	int br1 = sy1 / DIRTY_BLOCK_H;

	if (bc0 < 0) bc0 = 0;
	if (br0 < 0) br0 = 0;
	if (bc1 >= DIRTY_COLS) bc1 = DIRTY_COLS - 1;
	if (br1 >= DIRTY_ROWS) br1 = DIRTY_ROWS - 1;

	for (int r = br0; r <= br1; r++)
		for (int c = bc0; c <= bc1; c++)
			_dirtyBlocks[r][c] = true;
}

void NspireGraphicsManager::copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) {
	if (!_gameScreen.getPixels())
		return;

	const byte *src = (const byte *)buf;
	byte *dst = (byte *)_gameScreen.getBasePtr(x, y);
	int dstPitch = _gameScreen.pitch;
	int copyWidth = w * _gameFormat.bytesPerPixel;

	for (int row = 0; row < h; row++) {
		memcpy(dst, src, copyWidth);
		src += pitch;
		dst += dstPitch;
	}

	markDirtyGame(x, y, w, h);
}

Graphics::Surface *NspireGraphicsManager::lockScreen() {
	return &_gameScreen;
}

void NspireGraphicsManager::unlockScreen() {
	// After lockScreen/unlockScreen, assume full screen dirty
	_fullRedraw = true;
}

void NspireGraphicsManager::fillScreen(uint32 col) {
	if (_gameScreen.getPixels()) {
		if (_gameFormat.bytesPerPixel == 1) {
			memset(_gameScreen.getPixels(), col, _gameWidth * _gameHeight);
		} else {
			_gameScreen.fillRect(Common::Rect(_gameWidth, _gameHeight), col);
		}
		_fullRedraw = true;
	}
}

void NspireGraphicsManager::fillScreen(const Common::Rect &r, uint32 col) {
	if (_gameScreen.getPixels()) {
		_gameScreen.fillRect(r, col);
		markDirtyGame(r.left, r.top, r.width(), r.height());
	}
}

// 8x8 bitmap font (from MAME Nspire port)
static const uint8 font8x8[128][8] = {
	// 0-31: control chars (blank)
	{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
	{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
	// 32: space
	{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
	// 33-47: ! " # $ % & ' ( ) * + , - . /
	{0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00},
	{0x6C,0x6C,0x24,0x00,0x00,0x00,0x00,0x00},
	{0x24,0x7E,0x24,0x24,0x7E,0x24,0x00,0x00},
	{0x18,0x3E,0x58,0x3C,0x1A,0x7C,0x18,0x00},
	{0x62,0x64,0x08,0x10,0x26,0x46,0x00,0x00},
	{0x30,0x48,0x30,0x56,0x88,0x76,0x00,0x00},
	{0x18,0x18,0x10,0x00,0x00,0x00,0x00,0x00},
	{0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00},
	{0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00},
	{0x00,0x24,0x18,0x7E,0x18,0x24,0x00,0x00},
	{0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x10},
	{0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},
	{0x02,0x04,0x08,0x10,0x20,0x40,0x00,0x00},
	// 48-57: 0-9
	{0x3C,0x46,0x4A,0x52,0x62,0x3C,0x00,0x00},
	{0x18,0x38,0x18,0x18,0x18,0x7E,0x00,0x00},
	{0x3C,0x42,0x02,0x3C,0x40,0x7E,0x00,0x00},
	{0x3C,0x42,0x0C,0x02,0x42,0x3C,0x00,0x00},
	{0x08,0x18,0x28,0x48,0x7E,0x08,0x00,0x00},
	{0x7E,0x40,0x7C,0x02,0x42,0x3C,0x00,0x00},
	{0x1C,0x20,0x40,0x7C,0x42,0x3C,0x00,0x00},
	{0x7E,0x02,0x04,0x08,0x10,0x10,0x00,0x00},
	{0x3C,0x42,0x3C,0x42,0x42,0x3C,0x00,0x00},
	{0x3C,0x42,0x3E,0x02,0x04,0x38,0x00,0x00},
	// 58-64: : ; < = > ? @
	{0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00},
	{0x00,0x18,0x18,0x00,0x18,0x18,0x10,0x00},
	{0x06,0x18,0x60,0x60,0x18,0x06,0x00,0x00},
	{0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00},
	{0x60,0x18,0x06,0x06,0x18,0x60,0x00,0x00},
	{0x3C,0x42,0x04,0x08,0x00,0x08,0x00,0x00},
	{0x3C,0x42,0x5E,0x52,0x5E,0x40,0x3C,0x00},
	// 65-90: A-Z
	{0x18,0x24,0x42,0x7E,0x42,0x42,0x00,0x00},
	{0x7C,0x42,0x7C,0x42,0x42,0x7C,0x00,0x00},
	{0x3C,0x42,0x40,0x40,0x42,0x3C,0x00,0x00},
	{0x78,0x44,0x42,0x42,0x44,0x78,0x00,0x00},
	{0x7E,0x40,0x7C,0x40,0x40,0x7E,0x00,0x00},
	{0x7E,0x40,0x7C,0x40,0x40,0x40,0x00,0x00},
	{0x3C,0x42,0x40,0x4E,0x42,0x3C,0x00,0x00},
	{0x42,0x42,0x7E,0x42,0x42,0x42,0x00,0x00},
	{0x7E,0x18,0x18,0x18,0x18,0x7E,0x00,0x00},
	{0x1E,0x04,0x04,0x04,0x44,0x38,0x00,0x00},
	{0x44,0x48,0x70,0x48,0x44,0x42,0x00,0x00},
	{0x40,0x40,0x40,0x40,0x40,0x7E,0x00,0x00},
	{0x42,0x66,0x5A,0x42,0x42,0x42,0x00,0x00},
	{0x42,0x62,0x52,0x4A,0x46,0x42,0x00,0x00},
	{0x3C,0x42,0x42,0x42,0x42,0x3C,0x00,0x00},
	{0x7C,0x42,0x42,0x7C,0x40,0x40,0x00,0x00},
	{0x3C,0x42,0x42,0x4A,0x44,0x3A,0x00,0x00},
	{0x7C,0x42,0x42,0x7C,0x44,0x42,0x00,0x00},
	{0x3C,0x40,0x3C,0x02,0x42,0x3C,0x00,0x00},
	{0x7E,0x18,0x18,0x18,0x18,0x18,0x00,0x00},
	{0x42,0x42,0x42,0x42,0x42,0x3C,0x00,0x00},
	{0x42,0x42,0x42,0x24,0x24,0x18,0x00,0x00},
	{0x42,0x42,0x42,0x5A,0x66,0x42,0x00,0x00},
	{0x42,0x24,0x18,0x18,0x24,0x42,0x00,0x00},
	{0x42,0x42,0x24,0x18,0x18,0x18,0x00,0x00},
	{0x7E,0x04,0x08,0x10,0x20,0x7E,0x00,0x00},
	// 91-96: [ \ ] ^ _ `
	{0x3C,0x30,0x30,0x30,0x30,0x3C,0x00,0x00},
	{0x40,0x20,0x10,0x08,0x04,0x02,0x00,0x00},
	{0x3C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00,0x00},
	{0x10,0x28,0x44,0x00,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x00,0x7E,0x00},
	{0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00},
	// 97-122: a-z
	{0x00,0x00,0x3C,0x02,0x3E,0x42,0x3E,0x00},
	{0x40,0x40,0x7C,0x42,0x42,0x42,0x7C,0x00},
	{0x00,0x00,0x3C,0x40,0x40,0x40,0x3C,0x00},
	{0x02,0x02,0x3E,0x42,0x42,0x42,0x3E,0x00},
	{0x00,0x00,0x3C,0x42,0x7E,0x40,0x3C,0x00},
	{0x0C,0x10,0x3C,0x10,0x10,0x10,0x10,0x00},
	{0x00,0x00,0x3E,0x42,0x42,0x3E,0x02,0x3C},
	{0x40,0x40,0x7C,0x42,0x42,0x42,0x42,0x00},
	{0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00},
	{0x0C,0x00,0x1C,0x0C,0x0C,0x0C,0x4C,0x38},
	{0x40,0x40,0x44,0x48,0x70,0x48,0x44,0x00},
	{0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
	{0x00,0x00,0x64,0x5A,0x42,0x42,0x42,0x00},
	{0x00,0x00,0x7C,0x42,0x42,0x42,0x42,0x00},
	{0x00,0x00,0x3C,0x42,0x42,0x42,0x3C,0x00},
	{0x00,0x00,0x7C,0x42,0x42,0x7C,0x40,0x40},
	{0x00,0x00,0x3E,0x42,0x42,0x3E,0x02,0x02},
	{0x00,0x00,0x5C,0x60,0x40,0x40,0x40,0x00},
	{0x00,0x00,0x3E,0x40,0x3C,0x02,0x7C,0x00},
	{0x10,0x10,0x7C,0x10,0x10,0x10,0x0C,0x00},
	{0x00,0x00,0x42,0x42,0x42,0x42,0x3E,0x00},
	{0x00,0x00,0x42,0x42,0x24,0x24,0x18,0x00},
	{0x00,0x00,0x42,0x42,0x42,0x5A,0x24,0x00},
	{0x00,0x00,0x42,0x24,0x18,0x24,0x42,0x00},
	{0x00,0x00,0x42,0x42,0x42,0x3E,0x02,0x3C},
	{0x00,0x00,0x7E,0x04,0x18,0x20,0x7E,0x00},
	// 123-127: { | } ~ DEL
	{0x0E,0x18,0x18,0x30,0x18,0x18,0x0E,0x00},
	{0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00},
	{0x70,0x18,0x18,0x0C,0x18,0x18,0x70,0x00},
	{0x00,0x00,0x32,0x4C,0x00,0x00,0x00,0x00},
	{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

static const int CHAR_W = 8;
static const int CHAR_H = 8;

static void drawChar(uint16 *dst, int pitch, int x, int y, char c, uint16 color) {
	unsigned char uc = (unsigned char)c;
	if (uc > 127) uc = '?';
	const uint8 *glyph = font8x8[uc];
	for (int row = 0; row < 8; row++) {
		int py = y + row;
		if (py < 0 || py >= NSPIRE_SCREEN_HEIGHT) continue;
		uint8 bits = glyph[row];
		for (int col = 0; col < 8; col++) {
			if (bits & (0x80 >> col)) {
				int px = x + col;
				if (px >= 0 && px < NSPIRE_SCREEN_WIDTH)
					dst[py * pitch + px] = color;
			}
		}
	}
}

static void drawString(uint16 *dst, int pitch, int x, int y, const char *str, uint16 color) {
	while (*str) {
		if (x >= NSPIRE_SCREEN_WIDTH) break;
		drawChar(dst, pitch, x, y, *str, color);
		x += CHAR_W;
		str++;
	}
}

static void drawRect(uint16 *dst, int pitch, int x, int y, int w, int h, uint16 color) {
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > NSPIRE_SCREEN_WIDTH) w = NSPIRE_SCREEN_WIDTH - x;
	if (y + h > NSPIRE_SCREEN_HEIGHT) h = NSPIRE_SCREEN_HEIGHT - y;
	if (w <= 0 || h <= 0) return;
	for (int row = y; row < y + h; row++) {
		uint16 *line = &dst[row * pitch + x];
		for (int col = 0; col < w; col++)
			line[col] = color;
	}
}

// Virtual keyboard layout matching TI-Nspire CX CAS physical keyboard
struct VkbKeyLayout {
	const char *label;
	int16_t x, w;
	int8_t row;
};

static const VkbKeyLayout s_vkbLayout[] = {
	// Row 0: esc, up, on
	{ "esc",    4, 44, 0 },  { "up",   130, 40, 0 },  { "on",   252, 64, 0 },
	// Row 1: pad, left, right, doc
	{ "pad",    4, 44, 1 },  { "left", 110, 42, 1 },  { "right",158, 42, 1 },  { "doc",  252, 64, 1 },
	// Row 2: tab, down, menu
	{ "tab",    4, 44, 2 },  { "down", 130, 40, 2 },  { "menu", 252, 64, 2 },
	// Row 3: ctrl, shift, var, del
	{ "ctrl",   4, 44, 3 },  { "shift", 72, 52, 3 },  { "var",  148, 48, 3 },  { "del",  252, 48, 3 },
	// Row 4: =, trg, 7, 8, 9
	{ "=",      4, 30, 4 },  { "trg",   38, 36, 4 },  { "7",    112, 28, 4 },  { "8",    144, 28, 4 },  { "9",    176, 28, 4 },
	// Row 5: ^, x^2, 4, 5, 6, *, /
	{ "^",      4, 30, 5 },  { "x^2",   38, 36, 5 },  { "4",    112, 28, 5 },  { "5",    144, 28, 5 },  { "6",    176, 28, 5 },
	{ "*",    244, 28, 5 },  { "/",    276, 28, 5 },
	// Row 6: e^x, 10^x, 1, 2, 3, +, -
	{ "e^x",    4, 30, 6 },  { "10^x",  38, 36, 6 },  { "1",    112, 28, 6 },  { "2",    144, 28, 6 },  { "3",    176, 28, 6 },
	{ "+",    244, 28, 6 },  { "-",    276, 28, 6 },
	// Row 7: (, ), 0, ., (-), enter
	{ "(",      4, 30, 7 },  { ")",      38, 30, 7 },  { "0",    112, 28, 7 },  { ".",    144, 28, 7 },
	{ "(-)",  176, 34, 7 },  { "enter", 244, 60, 7 },
	// Row 8: EE, a-g
	{ "EE",     4, 40, 8 },
	{ "a",     56, 22, 8 },  { "b",      80, 22, 8 },  { "c",    104, 22, 8 },  { "d",    128, 22, 8 },
	{ "e",    152, 22, 8 },  { "f",     176, 22, 8 },  { "g",    200, 22, 8 },
	// Row 9: pi, h-n, flag
	{ "pi",     4, 40, 9 },
	{ "h",     56, 22, 9 },  { "i",      80, 22, 9 },  { "j",    104, 22, 9 },  { "k",    128, 22, 9 },
	{ "l",    152, 22, 9 },  { "m",     176, 22, 9 },  { "n",    200, 22, 9 },
	{ "flag", 260, 36, 9 },
	// Row 10: o-u
	{ "o",     56, 22,10 },  { "p",      80, 22,10 },  { "q",    104, 22,10 },  { "r",    128, 22,10 },
	{ "s",    152, 22,10 },  { "t",     176, 22,10 },  { "u",    200, 22,10 },
	// Row 11: v-z, space
	{ "v",     56, 22,11 },  { "w",      80, 22,11 },  { "x",    104, 22,11 },  { "y",    128, 22,11 },
	{ "z",    152, 22,11 },  { "space", 178, 56,11 },  { "CLEAR", 244, 60,11 },
};

static const int VKB_TOTAL = sizeof(s_vkbLayout) / sizeof(s_vkbLayout[0]);
static const int VKB_Y0 = 16;
static const int VKB_ROW_H = 15;
static const int VKB_KEY_H = 13;

// RGB565 helper
static inline uint16 rgb565(uint8 r, uint8 g, uint8 b) {
	return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

void NspireGraphicsManager::computeScaleTables() {
	for (int x = 0; x < NSPIRE_SCREEN_WIDTH; x++) {
		_scaleX[x] = (x * _gameWidth) / NSPIRE_SCREEN_WIDTH;
	}
	for (int y = 0; y < NSPIRE_SCREEN_HEIGHT; y++) {
		_scaleY[y] = (y * _gameHeight) / NSPIRE_SCREEN_HEIGHT;
	}
}

void NspireGraphicsManager::blitToSDLSurface() {
	if (!_sdlScreen || !_gameScreen.getPixels())
		return;

	if (SDL_MUSTLOCK(_sdlScreen))
		SDL_LockSurface(_sdlScreen);

	uint16 *dst = (uint16 *)_sdlScreen->pixels;
	int dstPitch = _sdlScreen->pitch / sizeof(uint16);

	// --- Restore pixels under previous cursor ---
	if (_cursorSaved && !_vkbActive) {
		int idx = 0;
		for (int i = -2; i <= 2; i++) {
			int px = _cursorSaveX + i;
			if (px >= 0 && px < NSPIRE_SCREEN_WIDTH && _cursorSaveY >= 0 && _cursorSaveY < NSPIRE_SCREEN_HEIGHT)
				dst[_cursorSaveY * dstPitch + px] = _cursorSave[idx];
			idx++;
		}
		for (int i = -2; i <= 2; i++) {
			if (i == 0) { idx++; continue; } // center already restored
			int py = _cursorSaveY + i;
			if (_cursorSaveX >= 0 && _cursorSaveX < NSPIRE_SCREEN_WIDTH && py >= 0 && py < NSPIRE_SCREEN_HEIGHT)
				dst[py * dstPitch + _cursorSaveX] = _cursorSave[idx];
			idx++;
		}
		_cursorSaved = false;
	}

	// --- VKB mode: partial or full redraw ---
	if (_vkbActive) {
		if (_vkbPrevSelected < 0) {
			// First frame or full redraw needed
			drawVkbOverlay(dst, dstPitch);
		} else if (_vkbSelected != _vkbPrevSelected) {
			// Partial redraw: only old and new selected keys
			drawVkbKey(dst, dstPitch, _vkbPrevSelected, false);
			drawVkbKey(dst, dstPitch, _vkbSelected, true);
			// Redraw help/selection text area at bottom
			uint16 colBg = ((0x10 >> 3) << 11) | ((0x10 >> 2) << 5) | (0x20 >> 3);
			uint16 colHelp = ((0xA0 >> 3) << 11) | ((0xA0 >> 2) << 5) | (0xA0 >> 3);
			uint16 colValue = ((0x80 >> 3) << 11) | ((0xFF >> 2) << 5) | (0x80 >> 3);
			drawRect(dst, dstPitch, 0, 196, NSPIRE_SCREEN_WIDTH, 44, colBg);
			const char *help = "Arrows/2468:Move Enter/5:OK Esc:Cancel";
			int hlen = 0; for (const char *p = help; *p; p++) hlen++;
			drawString(dst, dstPitch, (NSPIRE_SCREEN_WIDTH - hlen * CHAR_W) / 2, 200, help, colHelp);
			if (_vkbSelected >= 0 && _vkbSelected < VKB_TOTAL) {
				char selText[32];
				snprintf(selText, sizeof(selText), "-> %s", s_vkbLayout[_vkbSelected].label);
				int slen = 0; for (const char *p = selText; *p; p++) slen++;
				drawString(dst, dstPitch, (NSPIRE_SCREEN_WIDTH - slen * CHAR_W) / 2, 216, selText, colValue);
			}
		}
		_vkbPrevSelected = _vkbSelected;

		if (SDL_MUSTLOCK(_sdlScreen))
			SDL_UnlockSurface(_sdlScreen);
		SDL_Flip(_sdlScreen);
		return;
	}
	_vkbPrevSelected = -1; // Reset when VKB not active

	// --- Overlay mode ---
	if (_overlayVisible && _overlay.getPixels()) {
		if (_overlayDirty) {
			const uint16 *src = (const uint16 *)_overlay.getPixels();
			int srcPitch = _overlay.pitch / sizeof(uint16);
			for (int y = 0; y < NSPIRE_SCREEN_HEIGHT; y++) {
				memcpy(dst + y * dstPitch, src + y * srcPitch, NSPIRE_SCREEN_WIDTH * sizeof(uint16));
			}
			_overlayDirty = false;
		}
	} else {
		// --- Game screen blit with optimizations ---
		bool doFullRedraw = _fullRedraw;

		// Determine if we need dirty block checking
		if (doFullRedraw) {
			memset(_dirtyBlocks, 1, sizeof(_dirtyBlocks));
			_fullRedraw = false;
		}

		// Check if any block is dirty at all
		bool anyDirty = false;
		for (int r = 0; r < DIRTY_ROWS && !anyDirty; r++)
			for (int c = 0; c < DIRTY_COLS && !anyDirty; c++)
				if (_dirtyBlocks[r][c]) anyDirty = true;

		if (anyDirty) {
			const bool noShake = (_shakeXOffset == 0 && _shakeYOffset == 0);
			const bool noScaleX = ((int)_gameWidth == NSPIRE_SCREEN_WIDTH);
			int prevSrcY = -1;

			if (_gameFormat.bytesPerPixel == 1) {
				// CLUT8 mode
				const byte *srcPixels = (const byte *)_gameScreen.getPixels();
				int srcPitch = _gameScreen.pitch;

				for (int y = 0; y < NSPIRE_SCREEN_HEIGHT; y++) {
					int blockRow = y / DIRTY_BLOCK_H;
					int srcY = _scaleY[y] + _shakeYOffset;

					// Check if any block on this row is dirty
					bool rowDirty = false;
					if (doFullRedraw) {
						rowDirty = true;
					} else {
						for (int c = 0; c < DIRTY_COLS; c++) {
							if (_dirtyBlocks[blockRow][c]) { rowDirty = true; break; }
						}
					}
					if (!rowDirty) { prevSrcY = srcY; continue; }

					uint16 *dstRow = dst + y * dstPitch;

					if (srcY < 0 || srcY >= (int)_gameHeight) {
						memset(dstRow, 0, NSPIRE_SCREEN_WIDTH * sizeof(uint16));
						prevSrcY = srcY;
						continue;
					}

					// Duplicate line detection: if same source row as previous, memcpy
					if (srcY == prevSrcY && y > 0) {
						memcpy(dstRow, dst + (y - 1) * dstPitch, NSPIRE_SCREEN_WIDTH * sizeof(uint16));
						continue;
					}
					prevSrcY = srcY;

					const byte *srcRow = srcPixels + srcY * srcPitch;

					if (noShake && noScaleX) {
						// Fast path: no scaling on X, no shake
						// Process 4 pixels at a time
						int x = 0;
						for (; x + 3 < NSPIRE_SCREEN_WIDTH; x += 4) {
							dstRow[x]   = _palette565[srcRow[x]];
							dstRow[x+1] = _palette565[srcRow[x+1]];
							dstRow[x+2] = _palette565[srcRow[x+2]];
							dstRow[x+3] = _palette565[srcRow[x+3]];
						}
						for (; x < NSPIRE_SCREEN_WIDTH; x++) {
							dstRow[x] = _palette565[srcRow[x]];
						}
					} else if (noShake) {
						// No shake but need X scaling
						int x = 0;
						for (; x + 3 < NSPIRE_SCREEN_WIDTH; x += 4) {
							dstRow[x]   = _palette565[srcRow[_scaleX[x]]];
							dstRow[x+1] = _palette565[srcRow[_scaleX[x+1]]];
							dstRow[x+2] = _palette565[srcRow[_scaleX[x+2]]];
							dstRow[x+3] = _palette565[srcRow[_scaleX[x+3]]];
						}
						for (; x < NSPIRE_SCREEN_WIDTH; x++) {
							dstRow[x] = _palette565[srcRow[_scaleX[x]]];
						}
					} else {
						// General case with shake
						for (int x = 0; x < NSPIRE_SCREEN_WIDTH; x++) {
							int srcX = _scaleX[x] + _shakeXOffset;
							if (srcX >= 0 && srcX < (int)_gameWidth)
								dstRow[x] = _palette565[srcRow[srcX]];
							else
								dstRow[x] = 0;
						}
					}
				}
			} else if (_gameFormat.bytesPerPixel == 2) {
				// RGB565 mode
				const uint16 *srcPixels = (const uint16 *)_gameScreen.getPixels();
				int srcPitch = _gameScreen.pitch / sizeof(uint16);

				for (int y = 0; y < NSPIRE_SCREEN_HEIGHT; y++) {
					int blockRow = y / DIRTY_BLOCK_H;
					int srcY = _scaleY[y] + _shakeYOffset;

					bool rowDirty = false;
					if (doFullRedraw) {
						rowDirty = true;
					} else {
						for (int c = 0; c < DIRTY_COLS; c++) {
							if (_dirtyBlocks[blockRow][c]) { rowDirty = true; break; }
						}
					}
					if (!rowDirty) { prevSrcY = srcY; continue; }

					uint16 *dstRow = dst + y * dstPitch;

					if (srcY < 0 || srcY >= (int)_gameHeight) {
						memset(dstRow, 0, NSPIRE_SCREEN_WIDTH * sizeof(uint16));
						prevSrcY = srcY;
						continue;
					}

					if (srcY == prevSrcY && y > 0) {
						memcpy(dstRow, dst + (y - 1) * dstPitch, NSPIRE_SCREEN_WIDTH * sizeof(uint16));
						continue;
					}
					prevSrcY = srcY;

					const uint16 *srcRow = srcPixels + srcY * srcPitch;

					if (noShake && noScaleX) {
						memcpy(dstRow, srcRow, NSPIRE_SCREEN_WIDTH * sizeof(uint16));
					} else if (noShake) {
						int x = 0;
						for (; x + 3 < NSPIRE_SCREEN_WIDTH; x += 4) {
							dstRow[x]   = srcRow[_scaleX[x]];
							dstRow[x+1] = srcRow[_scaleX[x+1]];
							dstRow[x+2] = srcRow[_scaleX[x+2]];
							dstRow[x+3] = srcRow[_scaleX[x+3]];
						}
						for (; x < NSPIRE_SCREEN_WIDTH; x++) {
							dstRow[x] = srcRow[_scaleX[x]];
						}
					} else {
						for (int x = 0; x < NSPIRE_SCREEN_WIDTH; x++) {
							int srcX = _scaleX[x] + _shakeXOffset;
							if (srcX >= 0 && srcX < (int)_gameWidth)
								dstRow[x] = srcRow[srcX];
							else
								dstRow[x] = 0;
						}
					}
				}
			}

			// Clear dirty blocks
			memset(_dirtyBlocks, 0, sizeof(_dirtyBlocks));
		}
	}

	// --- Save pixels under cursor, then draw crosshair ---
	{
		int idx = 0;
		// Save horizontal pixels
		for (int i = -2; i <= 2; i++) {
			int px = _cursorX + i;
			if (px >= 0 && px < NSPIRE_SCREEN_WIDTH && _cursorY >= 0 && _cursorY < NSPIRE_SCREEN_HEIGHT)
				_cursorSave[idx] = dst[_cursorY * dstPitch + px];
			else
				_cursorSave[idx] = 0;
			idx++;
		}
		// Save vertical pixels (skip center, already saved)
		for (int i = -2; i <= 2; i++) {
			if (i == 0) { _cursorSave[idx] = _cursorSave[2]; idx++; continue; }
			int py = _cursorY + i;
			if (_cursorX >= 0 && _cursorX < NSPIRE_SCREEN_WIDTH && py >= 0 && py < NSPIRE_SCREEN_HEIGHT)
				_cursorSave[idx] = dst[py * dstPitch + _cursorX];
			else
				_cursorSave[idx] = 0;
			idx++;
		}
		_cursorSaveX = _cursorX;
		_cursorSaveY = _cursorY;
		_cursorSaved = true;

		// Draw crosshair
		const uint16 cursorColor = 0xFFFF;
		for (int i = -2; i <= 2; i++) {
			int px = _cursorX + i;
			if (px >= 0 && px < NSPIRE_SCREEN_WIDTH && _cursorY >= 0 && _cursorY < NSPIRE_SCREEN_HEIGHT)
				dst[_cursorY * dstPitch + px] = cursorColor;
			int py = _cursorY + i;
			if (_cursorX >= 0 && _cursorX < NSPIRE_SCREEN_WIDTH && py >= 0 && py < NSPIRE_SCREEN_HEIGHT)
				dst[py * dstPitch + _cursorX] = cursorColor;
		}
	}

	// Mark cursor area dirty for next frame (cursor may move)
	{
		int bc0 = (_cursorX - 2) / DIRTY_BLOCK_W;
		int bc1 = (_cursorX + 2) / DIRTY_BLOCK_W;
		int br0 = (_cursorY - 2) / DIRTY_BLOCK_H;
		int br1 = (_cursorY + 2) / DIRTY_BLOCK_H;
		if (bc0 < 0) bc0 = 0;
		if (br0 < 0) br0 = 0;
		if (bc1 >= DIRTY_COLS) bc1 = DIRTY_COLS - 1;
		if (br1 >= DIRTY_ROWS) br1 = DIRTY_ROWS - 1;
		for (int r = br0; r <= br1; r++)
			for (int c = bc0; c <= bc1; c++)
				_dirtyBlocks[r][c] = true;
	}

	if (SDL_MUSTLOCK(_sdlScreen))
		SDL_UnlockSurface(_sdlScreen);

	SDL_Flip(_sdlScreen);
}

void NspireGraphicsManager::updateScreen() {
	blitToSDLSurface();
}

void NspireGraphicsManager::setShakePos(int shakeXOffset, int shakeYOffset) {
	if (_shakeXOffset != shakeXOffset || _shakeYOffset != shakeYOffset) {
		_shakeXOffset = shakeXOffset;
		_shakeYOffset = shakeYOffset;
		_fullRedraw = true;
	}
}

void NspireGraphicsManager::setFocusRectangle(const Common::Rect &rect) {
}

void NspireGraphicsManager::clearFocusRectangle() {
}

// Overlay methods
void NspireGraphicsManager::showOverlay(bool inGUI) {
	_overlayVisible = true;
	_overlayDirty = true;
	_fullRedraw = true;
}

void NspireGraphicsManager::hideOverlay() {
	_overlayVisible = false;
	_fullRedraw = true;
}

bool NspireGraphicsManager::isOverlayVisible() const {
	return _overlayVisible;
}

Graphics::PixelFormat NspireGraphicsManager::getOverlayFormat() const {
	return Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0);
}

void NspireGraphicsManager::clearOverlay() {
	if (_overlay.getPixels()) {
		memset(_overlay.getPixels(), 0, _overlay.h * _overlay.pitch);
		_overlayDirty = true;
	}
}

void NspireGraphicsManager::grabOverlay(Graphics::Surface &surface) const {
	if (_overlay.getPixels()) {
		const byte *src = (const byte *)_overlay.getPixels();
		byte *dst = (byte *)surface.getPixels();
		int h = MIN(surface.h, _overlay.h);
		int w = MIN(surface.w, _overlay.w) * _overlay.format.bytesPerPixel;
		for (int y = 0; y < h; y++) {
			memcpy(dst, src, w);
			src += _overlay.pitch;
			dst += surface.pitch;
		}
	}
}

void NspireGraphicsManager::copyRectToOverlay(const void *buf, int pitch, int x, int y, int w, int h) {
	if (!_overlay.getPixels())
		return;

	const byte *src = (const byte *)buf;
	byte *dst = (byte *)_overlay.getBasePtr(x, y);
	int copyWidth = w * _overlay.format.bytesPerPixel;

	for (int row = 0; row < h; row++) {
		memcpy(dst, src, copyWidth);
		src += pitch;
		dst += _overlay.pitch;
	}
	_overlayDirty = true;
}

int16 NspireGraphicsManager::getOverlayHeight() const {
	return NSPIRE_SCREEN_HEIGHT;
}

int16 NspireGraphicsManager::getOverlayWidth() const {
	return NSPIRE_SCREEN_WIDTH;
}

// Mouse cursor
bool NspireGraphicsManager::showMouse(bool visible) {
	bool oldVisible = _cursorVisible;
	_cursorVisible = visible;
	return oldVisible;
}

void NspireGraphicsManager::warpMouse(int x, int y) {
	// Called by ScummVM framework with virtual (game/overlay) coordinates
	// Convert to screen coordinates for cursor drawing
	if (_overlayVisible || _gameWidth == 0 || _gameHeight == 0) {
		_cursorX = x;
		_cursorY = y;
	} else {
		_cursorX = x * NSPIRE_SCREEN_WIDTH / (int)_gameWidth;
		_cursorY = y * NSPIRE_SCREEN_HEIGHT / (int)_gameHeight;
	}
}

void NspireGraphicsManager::setMouseCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY,
                                           uint32 keycolor, bool dontScale,
                                           const Graphics::PixelFormat *format, const byte *mask) {
	_cursorHotspotX = hotspotX;
	_cursorHotspotY = hotspotY;
	_cursorKeycolor = keycolor;

	_cursor.free();
	if (buf && w && h) {
		Graphics::PixelFormat cursorFormat = format ? *format : Graphics::PixelFormat::createFormatCLUT8();
		_cursor.create(w, h, cursorFormat);
		memcpy(_cursor.getPixels(), buf, w * h * cursorFormat.bytesPerPixel);
	}
}

void NspireGraphicsManager::setCursorPalette(const byte *colors, uint start, uint num) {
	memcpy(_cursorPalette + start * 3, colors, num * 3);
	_cursorPaletteEnabled = true;
}

void NspireGraphicsManager::drawVkbOverlay(uint16 *dst, int dstPitch) {
	// Colors matching MAME style
	uint16 colBg        = rgb565(0x10, 0x10, 0x20);
	uint16 colTitle     = rgb565(0xFF, 0xFF, 0x00);
	uint16 colKeyGray   = rgb565(0x50, 0x50, 0x55);
	uint16 colKeyCursor = rgb565(0xFF, 0xC0, 0x00);
	uint16 colKeyCurrent= rgb565(0x20, 0x60, 0xA0); // Blue for current action's key
	uint16 colKeyBlock  = rgb565(0x35, 0x25, 0x25);  // Dark red for blocked (used by other)
	uint16 colKeyClear  = rgb565(0x80, 0x20, 0x20);  // Red for CLR button
	uint16 colBorder    = rgb565(0x70, 0x70, 0x78);
	uint16 colTextWhite = rgb565(0xFF, 0xFF, 0xFF);
	uint16 colTextDim   = rgb565(0x60, 0x60, 0x60);
	uint16 colTextDark  = rgb565(0x10, 0x10, 0x10);
	uint16 colHelp      = rgb565(0xA0, 0xA0, 0xA0);
	uint16 colValue     = rgb565(0x80, 0xFF, 0x80);

	// Dark background
	drawRect(dst, dstPitch, 0, 0, NSPIRE_SCREEN_WIDTH, NSPIRE_SCREEN_HEIGHT, colBg);

	// Title centered
	const char *title = "Select key:";
	int tlen = 0; for (const char *p = title; *p; p++) tlen++;
	drawString(dst, dstPitch, (NSPIRE_SCREEN_WIDTH - tlen * CHAR_W) / 2, 3, title, colTitle);

	for (int i = 0; i < VKB_TOTAL; i++) {
		const VkbKeyLayout &k = s_vkbLayout[i];
		int ky = VKB_Y0 + k.row * VKB_ROW_H;

		// Choose colors based on state
		uint16 bg, fg;
		bool isClearKey = (k.label[0] == 'C' && k.label[1] == 'L' && k.label[2] == 'E');
		if (i == _vkbSelected) {
			bg = colKeyCursor;
			fg = colTextDark;
		} else if (isClearKey) {
			bg = colKeyClear;
			fg = colTextWhite;
		} else if (i < 80 && _vkbCurrent[i]) {
			bg = colKeyCurrent;
			fg = colTextWhite;
		} else if (i < 80 && _vkbBlocked[i]) {
			bg = colKeyBlock;
			fg = colTextDim;
		} else {
			bg = colKeyGray;
			fg = colTextWhite;
		}

		// Draw key background
		drawRect(dst, dstPitch, k.x, ky, k.w, VKB_KEY_H, bg);
		// Draw 1-pixel border on all sides
		drawRect(dst, dstPitch, k.x, ky, k.w, 1, colBorder);
		drawRect(dst, dstPitch, k.x, ky + VKB_KEY_H - 1, k.w, 1, colBorder);
		drawRect(dst, dstPitch, k.x, ky, 1, VKB_KEY_H, colBorder);
		drawRect(dst, dstPitch, k.x + k.w - 1, ky, 1, VKB_KEY_H, colBorder);

		// Draw label centered in key
		if (k.label[0]) {
			int llen = 0; for (const char *p = k.label; *p; p++) llen++;
			int lx = k.x + (k.w - llen * CHAR_W) / 2;
			int ly = ky + (VKB_KEY_H - CHAR_H) / 2;
			drawString(dst, dstPitch, lx, ly, k.label, fg);
		}
	}

	// Help text centered at bottom
	const char *help = "Arrows/2468:Move Enter/5:OK Esc:Cancel";
	int hlen = 0; for (const char *p = help; *p; p++) hlen++;
	drawString(dst, dstPitch, (NSPIRE_SCREEN_WIDTH - hlen * CHAR_W) / 2, 200, help, colHelp);

	// Show selected key name
	if (_vkbSelected >= 0 && _vkbSelected < VKB_TOTAL) {
		char selText[32];
		snprintf(selText, sizeof(selText), "-> %s", s_vkbLayout[_vkbSelected].label);
		int slen = 0; for (const char *p = selText; *p; p++) slen++;
		drawString(dst, dstPitch, (NSPIRE_SCREEN_WIDTH - slen * CHAR_W) / 2, 216, selText, colValue);
	}
}

void NspireGraphicsManager::drawVkbKey(uint16 *dst, int dstPitch, int keyIdx, bool selected) {
	if (keyIdx < 0 || keyIdx >= VKB_TOTAL) return;

	uint16 colKeyGray   = rgb565(0x50, 0x50, 0x55);
	uint16 colKeyCursor = rgb565(0xFF, 0xC0, 0x00);
	uint16 colKeyCurrent= rgb565(0x20, 0x60, 0xA0);
	uint16 colKeyBlock  = rgb565(0x35, 0x25, 0x25);
	uint16 colKeyClear  = rgb565(0x80, 0x20, 0x20);
	uint16 colBorder    = rgb565(0x70, 0x70, 0x78);
	uint16 colTextWhite = rgb565(0xFF, 0xFF, 0xFF);
	uint16 colTextDim   = rgb565(0x60, 0x60, 0x60);
	uint16 colTextDark  = rgb565(0x10, 0x10, 0x10);

	const VkbKeyLayout &k = s_vkbLayout[keyIdx];
	int ky = VKB_Y0 + k.row * VKB_ROW_H;

	uint16 bg, fg;
	bool isClearKey = (k.label[0] == 'C' && k.label[1] == 'L' && k.label[2] == 'E');
	if (selected) {
		bg = colKeyCursor;
		fg = colTextDark;
	} else if (isClearKey) {
		bg = colKeyClear;
		fg = colTextWhite;
	} else if (keyIdx < 80 && _vkbCurrent[keyIdx]) {
		bg = colKeyCurrent;
		fg = colTextWhite;
	} else if (keyIdx < 80 && _vkbBlocked[keyIdx]) {
		bg = colKeyBlock;
		fg = colTextDim;
	} else {
		bg = colKeyGray;
		fg = colTextWhite;
	}

	drawRect(dst, dstPitch, k.x, ky, k.w, VKB_KEY_H, bg);
	drawRect(dst, dstPitch, k.x, ky, k.w, 1, colBorder);
	drawRect(dst, dstPitch, k.x, ky + VKB_KEY_H - 1, k.w, 1, colBorder);
	drawRect(dst, dstPitch, k.x, ky, 1, VKB_KEY_H, colBorder);
	drawRect(dst, dstPitch, k.x + k.w - 1, ky, 1, VKB_KEY_H, colBorder);

	if (k.label[0]) {
		int llen = 0; for (const char *p = k.label; *p; p++) llen++;
		int lx = k.x + (k.w - llen * CHAR_W) / 2;
		int ly = ky + (VKB_KEY_H - CHAR_H) / 2;
		drawString(dst, dstPitch, lx, ly, k.label, fg);
	}
}
