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
#include "backends/platform/nspire/osystem-nspire.h"
#include "backends/saves/default/default-saves.h"
#include "backends/timer/default/default-timer.h"
#include "backends/events/default/default-events.h"
#include "backends/mutex/null/null-mutex.h"
#include "backends/mixer/null/null-mixer.h"
#include "backends/fs/nspire/nspire-fs-factory.h"
#include "backends/platform/nspire/nspire-graphics.h"
#include "common/config-manager.h"
#include "backends/keymapper/hardware-input.h"
#include "backends/keymapper/keymapper-defaults.h"
#include "backends/keymapper/keymapper.h"
#include "backends/keymapper/keymap.h"
#include "backends/keymapper/standard-actions.h"
#include "common/events.h"

#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>

#include <SDL/SDL.h>

// Ndless SDK headers
extern "C" {
#include <keys.h>
#include <libndls.h>
}

OSystem_NSPIRE::OSystem_NSPIRE() :
	_startTicks(0),
	_screen(nullptr),
	_quitRequested(false),
	_mouseX(NSPIRE_SCREEN_WIDTH / 2),
	_mouseY(NSPIRE_SCREEN_HEIGHT / 2),
	_mouseVisible(false),
	_tpInited(false),
	_hasTouchpad(false),
	_tpCenterX(0),
	_tpCenterY(0),
	_tpMinX(0), _tpMaxX(0), _tpMinY(0), _tpMaxY(0),
	_vkbVisible(false),
	_vkbSelected(0),
	_vkbNeedsInit(false) {

	memset(_vkbBlockedKeys, 0, sizeof(_vkbBlockedKeys));
	_fsFactory = new NspireFilesystemFactory();
}

OSystem_NSPIRE::~OSystem_NSPIRE() {
	deinitSDL();
}

void OSystem_NSPIRE::initSDL() {
	// Only init VIDEO - no timer, no audio (like Ultima 4 Nspire port)
	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		return;
	}

	_screen = SDL_SetVideoMode(NSPIRE_SCREEN_WIDTH, NSPIRE_SCREEN_HEIGHT, 16, SDL_SWSURFACE);
	SDL_ShowCursor(SDL_DISABLE);
}

void OSystem_NSPIRE::deinitSDL() {
	if (_screen) {
		SDL_Quit();
		_screen = nullptr;
	}
}

void OSystem_NSPIRE::initBackend() {
	initSDL();

	_startTicks = SDL_GetTicks();

	// Create save directory
	mkdir("saves", 0755);

	_timerManager = new DefaultTimerManager();
	_eventManager = new DefaultEventManager(this);
	_savefileManager = new DefaultSaveFileManager("saves");
	_graphicsManager = new NspireGraphicsManager(_screen);
	_mixerManager = new NullMixerManager();
	_mixerManager->init();

	// Longer timeout for key remapping (30 seconds instead of 3)
	ConfMan.setInt("remap_timeout_delay_ms", 30000);

	// No audio on Nspire - mute everything to suppress warnings
	ConfMan.setBool("speech_mute", true);
	ConfMan.setBool("sfx_mute", true);
	ConfMan.setBool("music_mute", true);
	ConfMan.setInt("music_volume", 0);
	ConfMan.setInt("sfx_volume", 0);
	ConfMan.setInt("speech_volume", 0);

	BaseBackend::initBackend();
}

bool OSystem_NSPIRE::pollEvent(Common::Event &event) {
	((DefaultTimerManager *)getTimerManager())->checkTimers();
	((NullMixerManager *)_mixerManager)->update(1);

	if (_quitRequested) {
		event.type = Common::EVENT_QUIT;
		return true;
	}

	// Virtual keyboard mode
	static int vkbDebounce = 0;
	if (_vkbVisible) {
		bool result = pollVirtualKeyboard(event);
		NspireGraphicsManager *gfx = (NspireGraphicsManager *)_graphicsManager;
		if (!_vkbVisible) {
			gfx->setVkbState(false, 0);
			vkbDebounce = 100; // Skip key processing for ~100 cycles after VKB closes
		} else {
			gfx->setVkbState(true, _vkbSelected);
		}
		return result;
	}

	// After VKB closes, wait for all keys to be released before processing
	if (vkbDebounce > 0) {
		vkbDebounce--;
		return false;
	}

	bool hasEvent = false;
	pollKeys(event, hasEvent);

	// Sync cursor and convert mouse coordinates
	if (hasEvent) {
		if (event.type == Common::EVENT_MOUSEMOVE ||
		    event.type == Common::EVENT_LBUTTONDOWN ||
		    event.type == Common::EVENT_LBUTTONUP ||
		    event.type == Common::EVENT_RBUTTONDOWN ||
		    event.type == Common::EVENT_RBUTTONUP) {
			NspireGraphicsManager *gfx = (NspireGraphicsManager *)_graphicsManager;
			// Set cursor at screen position (no conversion)
			gfx->setCursorScreenPos(_mouseX, _mouseY);
			// Convert screen coordinates to virtual (game/overlay) for the event
			int virtX, virtY;
			gfx->screenToVirtual(_mouseX, _mouseY, virtX, virtY);
			event.mouse.x = virtX;
			event.mouse.y = virtY;
		}
	}

	return hasEvent;
}

void OSystem_NSPIRE::pollTouchpad(Common::Event &event, bool &hasEvent) {
	if (!_tpInited) {
		_hasTouchpad = (_is_touchpad() != 0);
		if (_hasTouchpad) {
			touchpad_info_t *info = touchpad_getinfo();
			if (info) {
				fprintf(stderr, "NSPIRE: Touchpad detected, size=%dx%d\n",
				        info->width, info->height);
				// Initialize calibration: start with a reduced range (20% margin on each side)
				// to compensate for dead zones at touchpad edges.
				// Dynamic calibration will further refine these bounds.
				_tpMinX = info->width / 5;
				_tpMaxX = info->width - info->width / 5;
				_tpMinY = info->height / 5;
				_tpMaxY = info->height - info->height / 5;
			} else {
				_hasTouchpad = false;
			}
		}
		_tpInited = true;
	}

	if (!_hasTouchpad)
		return;

	touchpad_report_t report;
	if (touchpad_scan(&report) != 0)
		return;

	// Touchpad click -> left click
	static bool lastTpClick = false;
	bool tpClick = (report.pressed != 0);

	if (tpClick && !lastTpClick) {
		event.type = Common::EVENT_LBUTTONDOWN;
		event.mouse.x = _mouseX;
		event.mouse.y = _mouseY;
		hasEvent = true;
		lastTpClick = tpClick;
		return;
	} else if (!tpClick && lastTpClick) {
		event.type = Common::EVENT_LBUTTONUP;
		event.mouse.x = _mouseX;
		event.mouse.y = _mouseY;
		hasEvent = true;
		lastTpClick = tpClick;
		return;
	}
	lastTpClick = tpClick;

	// Touchpad absolute position -> screen coordinates (dynamic calibration)
	if (report.contact) {
		int rx = (int)report.x;
		int ry = (int)report.y;

		// Update dynamic min/max calibration
		if (rx < _tpMinX) _tpMinX = rx;
		if (rx > _tpMaxX) _tpMaxX = rx;
		if (ry < _tpMinY) _tpMinY = ry;
		if (ry > _tpMaxY) _tpMaxY = ry;

		// Map calibrated range to screen coordinates
		int rangeX = _tpMaxX - _tpMinX;
		int rangeY = _tpMaxY - _tpMinY;
		if (rangeX < 1) rangeX = 1;
		if (rangeY < 1) rangeY = 1;

		int newX = (rx - _tpMinX) * NSPIRE_SCREEN_WIDTH / rangeX;
		int newY = (_tpMaxY - ry) * NSPIRE_SCREEN_HEIGHT / rangeY;

		if (newX < 0) newX = 0;
		if (newX >= NSPIRE_SCREEN_WIDTH) newX = NSPIRE_SCREEN_WIDTH - 1;
		if (newY < 0) newY = 0;
		if (newY >= NSPIRE_SCREEN_HEIGHT) newY = NSPIRE_SCREEN_HEIGHT - 1;

		if (newX != _mouseX || newY != _mouseY) {
			_mouseX = newX;
			_mouseY = newY;

			event.type = Common::EVENT_MOUSEMOVE;
			event.mouse.x = _mouseX;
			event.mouse.y = _mouseY;
			hasEvent = true;
		}
	}
}

// Physical key -> ScummVM keycode mapping for generic key handler
// These keys generate KEYDOWN/KEYUP events for the keymapper to process
struct NspireKeyEntry {
	const t_key *nspireKey;
	Common::KeyCode keycode;
	int ascii;
};

// Note: isKeyPressed(*ptr) expands via macro to isKeyPressed(&(*ptr)) = isKeyPressed(ptr)
static const NspireKeyEntry s_gameKeys[] = {
	// Calculator-specific keys (mapped to F-keys)
	{ &KEY_NSPIRE_DOC,      Common::KEYCODE_F2,         0 },
	{ &KEY_NSPIRE_VAR,      Common::KEYCODE_F3,         0 },
	{ &KEY_NSPIRE_TRIG,     Common::KEYCODE_F4,         0 },
	{ &KEY_NSPIRE_SQU,      Common::KEYCODE_F6,         0 },
	{ &KEY_NSPIRE_EXP,      Common::KEYCODE_F7,         0 },
	{ &KEY_NSPIRE_TENX,     Common::KEYCODE_F8,         0 },
	{ &KEY_NSPIRE_EE,       Common::KEYCODE_F9,         0 },
	{ &KEY_NSPIRE_PI,       Common::KEYCODE_F10,        0 },
	{ &KEY_NSPIRE_FLAG,     Common::KEYCODE_F11,        0 },
	// Standard keys already handled but needed for keymapper
	{ &KEY_NSPIRE_ESC,      Common::KEYCODE_ESCAPE,    27 },
	{ &KEY_NSPIRE_TAB,      Common::KEYCODE_TAB,      '\t'},
	{ &KEY_NSPIRE_SPACE,    Common::KEYCODE_SPACE,     ' '},
	{ &KEY_NSPIRE_DEL,      Common::KEYCODE_BACKSPACE,   8 },
	{ &KEY_NSPIRE_MENU,     Common::KEYCODE_F5,         0 },
	{ &KEY_NSPIRE_SHIFT,    Common::KEYCODE_LSHIFT,     0 },
	{ &KEY_NSPIRE_ENTER,    Common::KEYCODE_RETURN,   '\r'},
	// Numbers
	{ &KEY_NSPIRE_0,        Common::KEYCODE_0,         '0'},
	{ &KEY_NSPIRE_1,        Common::KEYCODE_1,         '1'},
	{ &KEY_NSPIRE_2,        Common::KEYCODE_2,         '2'},
	{ &KEY_NSPIRE_3,        Common::KEYCODE_3,         '3'},
	{ &KEY_NSPIRE_4,        Common::KEYCODE_4,         '4'},
	{ &KEY_NSPIRE_5,        Common::KEYCODE_5,         '5'},
	{ &KEY_NSPIRE_6,        Common::KEYCODE_6,         '6'},
	{ &KEY_NSPIRE_7,        Common::KEYCODE_7,         '7'},
	{ &KEY_NSPIRE_8,        Common::KEYCODE_8,         '8'},
	{ &KEY_NSPIRE_9,        Common::KEYCODE_9,         '9'},
	// Symbols
	{ &KEY_NSPIRE_EQU,      Common::KEYCODE_EQUALS,   '='},
	{ &KEY_NSPIRE_CAT,      Common::KEYCODE_CARET,    '^'},
	{ &KEY_NSPIRE_MULTIPLY, Common::KEYCODE_ASTERISK,  '*'},
	{ &KEY_NSPIRE_DIVIDE,   Common::KEYCODE_SLASH,     '/'},
	{ &KEY_NSPIRE_PLUS,     Common::KEYCODE_PLUS,      '+'},
	{ &KEY_NSPIRE_MINUS,    Common::KEYCODE_MINUS,     '-'},
	{ &KEY_NSPIRE_LP,       Common::KEYCODE_LEFTPAREN, '('},
	{ &KEY_NSPIRE_RP,       Common::KEYCODE_RIGHTPAREN,')'},
	{ &KEY_NSPIRE_PERIOD,   Common::KEYCODE_PERIOD,    '.'},
	// Letters
	{ &KEY_NSPIRE_A,        Common::KEYCODE_a,         'a'},
	{ &KEY_NSPIRE_B,        Common::KEYCODE_b,         'b'},
	{ &KEY_NSPIRE_C,        Common::KEYCODE_c,         'c'},
	{ &KEY_NSPIRE_D,        Common::KEYCODE_d,         'd'},
	{ &KEY_NSPIRE_E,        Common::KEYCODE_e,         'e'},
	{ &KEY_NSPIRE_F,        Common::KEYCODE_f,         'f'},
	{ &KEY_NSPIRE_G,        Common::KEYCODE_g,         'g'},
	{ &KEY_NSPIRE_H,        Common::KEYCODE_h,         'h'},
	{ &KEY_NSPIRE_I,        Common::KEYCODE_i,         'i'},
	{ &KEY_NSPIRE_J,        Common::KEYCODE_j,         'j'},
	{ &KEY_NSPIRE_K,        Common::KEYCODE_k,         'k'},
	{ &KEY_NSPIRE_L,        Common::KEYCODE_l,         'l'},
	{ &KEY_NSPIRE_M,        Common::KEYCODE_m,         'm'},
	{ &KEY_NSPIRE_N,        Common::KEYCODE_n,         'n'},
	{ &KEY_NSPIRE_O,        Common::KEYCODE_o,         'o'},
	{ &KEY_NSPIRE_P,        Common::KEYCODE_p,         'p'},
	{ &KEY_NSPIRE_Q,        Common::KEYCODE_q,         'q'},
	{ &KEY_NSPIRE_R,        Common::KEYCODE_r,         'r'},
	{ &KEY_NSPIRE_S,        Common::KEYCODE_s,         's'},
	{ &KEY_NSPIRE_T,        Common::KEYCODE_t,         't'},
	{ &KEY_NSPIRE_U,        Common::KEYCODE_u,         'u'},
	{ &KEY_NSPIRE_V,        Common::KEYCODE_v,         'v'},
	{ &KEY_NSPIRE_W,        Common::KEYCODE_w,         'w'},
	{ &KEY_NSPIRE_X,        Common::KEYCODE_x,         'x'},
	{ &KEY_NSPIRE_Y,        Common::KEYCODE_y,         'y'},
	{ &KEY_NSPIRE_Z,        Common::KEYCODE_z,         'z'},
};

static const int GAME_KEY_COUNT = sizeof(s_gameKeys) / sizeof(s_gameKeys[0]);

void OSystem_NSPIRE::pollKeys(Common::Event &event, bool &hasEvent) {
	// ON+ESC combo for quit (standard Ndless convention)
	if (on_key_pressed() && isKeyPressed(KEY_NSPIRE_ESC)) {
		event.type = Common::EVENT_QUIT;
		hasEvent = true;
		_quitRequested = true;
		return;
	}

	// Touchpad for mouse movement and click
	pollTouchpad(event, hasEvent);
	if (hasEvent) return;

	// Directional keys -> mouse movement (also works on clickpad models)
	int dx = 0, dy = 0;
	const int MOUSE_SPEED = 3;

	if (isKeyPressed(KEY_NSPIRE_UP))    dy = -MOUSE_SPEED;
	if (isKeyPressed(KEY_NSPIRE_DOWN))  dy = MOUSE_SPEED;
	if (isKeyPressed(KEY_NSPIRE_LEFT))  dx = -MOUSE_SPEED;
	if (isKeyPressed(KEY_NSPIRE_RIGHT)) dx = MOUSE_SPEED;

	if (dx != 0 || dy != 0) {
		_mouseX += dx;
		_mouseY += dy;

		if (_mouseX < 0) _mouseX = 0;
		if (_mouseX >= NSPIRE_SCREEN_WIDTH) _mouseX = NSPIRE_SCREEN_WIDTH - 1;
		if (_mouseY < 0) _mouseY = 0;
		if (_mouseY >= NSPIRE_SCREEN_HEIGHT) _mouseY = NSPIRE_SCREEN_HEIGHT - 1;

		event.type = Common::EVENT_MOUSEMOVE;
		event.mouse.x = _mouseX;
		event.mouse.y = _mouseY;
		hasEvent = true;
		return;
	}

	// Ctrl -> left click (Enter handled by generic key handler as KEYCODE_RETURN)
	static bool lastClickState = false;
	bool clickState = isKeyPressed(KEY_NSPIRE_CTRL);

	if (clickState && !lastClickState) {
		event.type = Common::EVENT_LBUTTONDOWN;
		event.mouse.x = _mouseX;
		event.mouse.y = _mouseY;
		hasEvent = true;
	} else if (!clickState && lastClickState) {
		event.type = Common::EVENT_LBUTTONUP;
		event.mouse.x = _mouseX;
		event.mouse.y = _mouseY;
		hasEvent = true;
	}
	lastClickState = clickState;
	if (hasEvent) return;

	// Generic keyboard event handler for all assignable keys
	static bool lastKeyState[GAME_KEY_COUNT] = {};

	for (int i = 0; i < GAME_KEY_COUNT; i++) {
		bool pressed = isKeyPressed(*s_gameKeys[i].nspireKey);

		if (pressed && !lastKeyState[i]) {
			event.type = Common::EVENT_KEYDOWN;
			event.kbd.keycode = s_gameKeys[i].keycode;
			event.kbd.ascii = s_gameKeys[i].ascii;
			event.kbd.flags = 0;
			lastKeyState[i] = pressed;
			hasEvent = true;
			return;
		} else if (!pressed && lastKeyState[i]) {
			event.type = Common::EVENT_KEYUP;
			event.kbd.keycode = s_gameKeys[i].keycode;
			event.kbd.ascii = s_gameKeys[i].ascii;
			event.kbd.flags = 0;
			lastKeyState[i] = pressed;
			hasEvent = true;
			return;
		}
		lastKeyState[i] = pressed;
	}
}

Common::MutexInternal *OSystem_NSPIRE::createMutex() {
	return new NullMutexInternal();
}

uint32 OSystem_NSPIRE::getMillis(bool skipRecord) {
	return SDL_GetTicks() - _startTicks;
}

void OSystem_NSPIRE::delayMillis(uint msecs) {
	SDL_Delay(msecs);
}

void OSystem_NSPIRE::getTimeAndDate(TimeDate &td, bool skipRecord) const {
	time_t curTime = time(nullptr);
	struct tm t = *localtime(&curTime);
	td.tm_sec = t.tm_sec;
	td.tm_min = t.tm_min;
	td.tm_hour = t.tm_hour;
	td.tm_mday = t.tm_mday;
	td.tm_mon = t.tm_mon;
	td.tm_year = t.tm_year;
	td.tm_wday = t.tm_wday;
}

void OSystem_NSPIRE::quit() {
	// Don't call exit(0) - let the normal shutdown path run
	// so all destructors are called (OSystem::destroy -> delete this -> ~OSystem_NSPIRE)
	_quitRequested = true;
}

void OSystem_NSPIRE::logMessage(LogMessageType::Type type, const char *message) {
	FILE *output = nullptr;

	if (type == LogMessageType::kInfo || type == LogMessageType::kDebug)
		output = stdout;
	else
		output = stderr;

	fputs(message, output);
	fflush(output);
}

void OSystem_NSPIRE::addSysArchivesToSearchSet(Common::SearchSet &s, int priority) {
	// Add data directory relative to executable
	struct stat st;
	if (stat("data", &st) == 0) {
		s.addDirectory("nspire-data", "data", priority);
	}
}

Common::Path OSystem_NSPIRE::getDefaultConfigFileName() {
	return Common::Path("scummvm.ini.tns");
}

Common::Path OSystem_NSPIRE::getDefaultLogFileName() {
	return Common::Path("scummvm.log.tns");
}

bool OSystem_NSPIRE::hasFeature(Feature f) {
	if (f == kFeatureVirtualKeyboard)
		return true;
	return ModularGraphicsBackend::hasFeature(f);
}

void OSystem_NSPIRE::setFeatureState(Feature f, bool enable) {
	if (f == kFeatureVirtualKeyboard) {
		if (enable) {
			// Only open VKB for key remapping, not for text input (save name, etc.)
			// RemapWidget sets remap_active=1 before calling setFeatureState(VKB, true)
			if (!ConfMan.hasKey("remap_active") || ConfMan.getInt("remap_active") != 1) {
				fprintf(stderr, "NSPIRE: VKB request ignored (not remapping)\n");
				return;
			}
			_vkbVisible = true;
			_vkbSelected = 0;
			_vkbNeedsInit = true;
		} else {
			_vkbVisible = false;
		}
		NspireGraphicsManager *gfx = (NspireGraphicsManager *)_graphicsManager;
		gfx->setVkbState(_vkbVisible, _vkbSelected);
		return;
	}
	ModularGraphicsBackend::setFeatureState(f, enable);
}

bool OSystem_NSPIRE::getFeatureState(Feature f) {
	if (f == kFeatureVirtualKeyboard)
		return _vkbVisible;
	return ModularGraphicsBackend::getFeatureState(f);
}

// VKB key mapping table (must match s_vkbLayout order in nspire-graphics.cpp)
struct VkbKey {
	Common::KeyCode keycode;
	int ascii;
	int16_t x, w;
	int8_t row;
};

static const VkbKey s_vkbKeys[] = {
	// Row 0: esc, up, on  (must match s_vkbLayout in nspire-graphics.cpp)
	{ Common::KEYCODE_ESCAPE,    27,   4, 44, 0 },    // esc
	{ Common::KEYCODE_UP,         0, 130, 40, 0 },    // up
	{ Common::KEYCODE_F1,         0, 252, 64, 0 },    // on -> F1
	// Row 1: pad, left, right, doc
	{ Common::KEYCODE_INVALID,    0,   4, 44, 1 },    // pad -> LClk
	{ Common::KEYCODE_LEFT,       0, 110, 42, 1 },    // left
	{ Common::KEYCODE_RIGHT,      0, 158, 42, 1 },    // right
	{ Common::KEYCODE_F2,         0, 252, 64, 1 },    // doc -> F2
	// Row 2: tab, down, menu
	{ Common::KEYCODE_TAB,      '\t',  4, 44, 2 },    // tab
	{ Common::KEYCODE_DOWN,       0, 130, 40, 2 },    // down
	{ Common::KEYCODE_F5,         0, 252, 64, 2 },    // menu
	// Row 3: ctrl, shift, var, del
	{ Common::KEYCODE_LCTRL,      0,   4, 44, 3 },    // ctrl
	{ Common::KEYCODE_LSHIFT,     0,  72, 52, 3 },    // shift
	{ Common::KEYCODE_F3,         0, 148, 48, 3 },    // var -> F3
	{ Common::KEYCODE_BACKSPACE,   8, 252, 48, 3 },   // del
	// Row 4: =, trg, 7, 8, 9
	{ Common::KEYCODE_EQUALS,   '=',   4, 30, 4 },    // =
	{ Common::KEYCODE_F4,         0,  38, 36, 4 },    // trg -> F4
	{ Common::KEYCODE_7,        '7', 112, 28, 4 },    // 7
	{ Common::KEYCODE_8,        '8', 144, 28, 4 },    // 8
	{ Common::KEYCODE_9,        '9', 176, 28, 4 },    // 9
	// Row 5: ^, x^2, 4, 5, 6, *, /
	{ Common::KEYCODE_CARET,    '^',   4, 30, 5 },    // ^
	{ Common::KEYCODE_F6,         0,  38, 36, 5 },    // x^2 -> F6
	{ Common::KEYCODE_4,        '4', 112, 28, 5 },    // 4
	{ Common::KEYCODE_5,        '5', 144, 28, 5 },    // 5
	{ Common::KEYCODE_6,        '6', 176, 28, 5 },    // 6
	{ Common::KEYCODE_ASTERISK, '*', 244, 28, 5 },    // *
	{ Common::KEYCODE_SLASH,    '/', 276, 28, 5 },    // /
	// Row 6: e^x, 10^x, 1, 2, 3, +, -
	{ Common::KEYCODE_F7,         0,   4, 30, 6 },    // e^x -> F7
	{ Common::KEYCODE_F8,         0,  38, 36, 6 },    // 10^x -> F8
	{ Common::KEYCODE_1,        '1', 112, 28, 6 },    // 1
	{ Common::KEYCODE_2,        '2', 144, 28, 6 },    // 2
	{ Common::KEYCODE_3,        '3', 176, 28, 6 },    // 3
	{ Common::KEYCODE_PLUS,     '+', 244, 28, 6 },    // +
	{ Common::KEYCODE_MINUS,    '-', 276, 28, 6 },    // -
	// Row 7: (, ), 0, ., (-), enter
	{ Common::KEYCODE_LEFTPAREN, '(',  4, 30, 7 },    // (
	{ Common::KEYCODE_RIGHTPAREN,')', 38, 30, 7 },    // )
	{ Common::KEYCODE_0,        '0', 112, 28, 7 },    // 0
	{ Common::KEYCODE_PERIOD,   '.', 144, 28, 7 },    // .
	{ Common::KEYCODE_MINUS,    '-', 176, 34, 7 },    // (-)
	{ Common::KEYCODE_RETURN,  '\r', 244, 60, 7 },    // enter
	// Row 8: EE, a-g
	{ Common::KEYCODE_F9,         0,   4, 40, 8 },    // EE -> F9
	{ Common::KEYCODE_a,        'a',  56, 22, 8 },
	{ Common::KEYCODE_b,        'b',  80, 22, 8 },
	{ Common::KEYCODE_c,        'c', 104, 22, 8 },
	{ Common::KEYCODE_d,        'd', 128, 22, 8 },
	{ Common::KEYCODE_e,        'e', 152, 22, 8 },
	{ Common::KEYCODE_f,        'f', 176, 22, 8 },
	{ Common::KEYCODE_g,        'g', 200, 22, 8 },
	// Row 9: pi, h-n, flag
	{ Common::KEYCODE_F10,        0,   4, 40, 9 },    // pi -> F10
	{ Common::KEYCODE_h,        'h',  56, 22, 9 },
	{ Common::KEYCODE_i,        'i',  80, 22, 9 },
	{ Common::KEYCODE_j,        'j', 104, 22, 9 },
	{ Common::KEYCODE_k,        'k', 128, 22, 9 },
	{ Common::KEYCODE_l,        'l', 152, 22, 9 },
	{ Common::KEYCODE_m,        'm', 176, 22, 9 },
	{ Common::KEYCODE_n,        'n', 200, 22, 9 },
	{ Common::KEYCODE_F11,        0, 260, 36, 9 },    // flag -> F11
	// Row 10: o-u
	{ Common::KEYCODE_o,        'o',  56, 22, 10 },
	{ Common::KEYCODE_p,        'p',  80, 22, 10 },
	{ Common::KEYCODE_q,        'q', 104, 22, 10 },
	{ Common::KEYCODE_r,        'r', 128, 22, 10 },
	{ Common::KEYCODE_s,        's', 152, 22, 10 },
	{ Common::KEYCODE_t,        't', 176, 22, 10 },
	{ Common::KEYCODE_u,        'u', 200, 22, 10 },
	// Row 11: v-z, space
	{ Common::KEYCODE_v,        'v',  56, 22, 11 },
	{ Common::KEYCODE_w,        'w',  80, 22, 11 },
	{ Common::KEYCODE_x,        'x', 104, 22, 11 },
	{ Common::KEYCODE_y,        'y', 128, 22, 11 },
	{ Common::KEYCODE_z,        'z', 152, 22, 11 },
	{ Common::KEYCODE_SPACE,    ' ', 178, 56, 11 },
	{ Common::KEYCODE_INVALID,   -2, 244, 60, 11 },  // CLR (clear mapping)
};

static const int VKB_KEY_COUNT = sizeof(s_vkbKeys) / sizeof(s_vkbKeys[0]);
static const int VKB_Y0 = 16;
static const int VKB_ROW_H = 15;

bool OSystem_NSPIRE::pollVirtualKeyboard(Common::Event &event) {
	if (_vkbSelected < 0 || _vkbSelected >= VKB_KEY_COUNT)
		_vkbSelected = 0;

	// On first poll after VKB opens, scan keymapper for assigned keys
	if (_vkbNeedsInit) {
		_vkbNeedsInit = false;
		NspireGraphicsManager *gfx = (NspireGraphicsManager *)_graphicsManager;
		gfx->clearVkbMarkers();
		memset(_vkbBlockedKeys, 0, sizeof(_vkbBlockedKeys));

		// Get the current action's keycode (set by RemapWidget via ConfMan, -1 = none)
		int currentKeycode = ConfMan.hasKey("remap_current_keycode") ? ConfMan.getInt("remap_current_keycode") : -1;
		int currentMouse = ConfMan.hasKey("remap_current_mouse") ? ConfMan.getInt("remap_current_mouse") : -1;

		// Mark current action's key in blue
		for (int v = 0; v < VKB_KEY_COUNT; v++) {
			if (s_vkbKeys[v].ascii == -1) continue;
			if (currentKeycode >= 0 && s_vkbKeys[v].keycode == (Common::KeyCode)currentKeycode) {
				gfx->setVkbKeyCurrent(v, true);
			}
			if (currentMouse >= 0 && s_vkbKeys[v].keycode == Common::KEYCODE_INVALID) {
				if ((currentMouse == Common::MOUSE_BUTTON_LEFT && s_vkbKeys[v].ascii == 0) ||
				    (currentMouse == Common::MOUSE_BUTTON_RIGHT && s_vkbKeys[v].ascii == 1)) {
					gfx->setVkbKeyCurrent(v, true);
				}
			}
		}

		// Scan all keymaps to find keys assigned to OTHER actions (blocked)
		Common::Keymapper *keymapper = getEventManager()->getKeymapper();
		if (keymapper) {
			const Common::KeymapArray &keymaps = keymapper->getKeymaps();
			for (uint km = 0; km < keymaps.size(); km++) {
				Common::Keymap *keymap = keymaps[km];
				// Skip GUI keymaps - they bind keys like Backspace internally
				// and would incorrectly block them in the VKB
				if (keymap->getType() == Common::Keymap::kKeymapTypeGui) continue;
				const Common::Keymap::ActionArray &actions = keymap->getActions();
				for (uint a = 0; a < actions.size(); a++) {
					Common::Array<Common::HardwareInput> inputs = keymap->getActionMapping(actions[a]);
					for (uint inp = 0; inp < inputs.size(); inp++) {
						const Common::HardwareInput &hw = inputs[inp];
						if (hw.type == Common::kHardwareInputTypeKeyboard) {
							// Skip if this is the current action's key
							if ((int)hw.key.keycode == currentKeycode) continue;
							for (int v = 0; v < VKB_KEY_COUNT; v++) {
								if (s_vkbKeys[v].ascii == -1) continue;
								if (s_vkbKeys[v].keycode == hw.key.keycode) {
									gfx->setVkbKeyBlocked(v, true);
									_vkbBlockedKeys[v] = true;
								}
							}
						} else if (hw.type == Common::kHardwareInputTypeMouse) {
							if ((int)hw.inputCode == currentMouse) continue;
							for (int v = 0; v < VKB_KEY_COUNT; v++) {
								if (s_vkbKeys[v].keycode == Common::KEYCODE_INVALID && s_vkbKeys[v].ascii >= 0 && s_vkbKeys[v].ascii <= 2) {
									if ((hw.inputCode == Common::MOUSE_BUTTON_LEFT && s_vkbKeys[v].ascii == 0) ||
									    (hw.inputCode == Common::MOUSE_BUTTON_RIGHT && s_vkbKeys[v].ascii == 1)) {
										gfx->setVkbKeyBlocked(v, true);
										_vkbBlockedKeys[v] = true;
									}
								}
							}
						}
					}
				}
			}
		}
		// Position cursor on first selectable key (skip blocked and invalid)
		for (int i = 0; i < VKB_KEY_COUNT; i++) {
			if (s_vkbKeys[i].ascii == -1) continue;
			if (i < VKB_KEY_COUNT && _vkbBlockedKeys[i]) continue;
			_vkbSelected = i;
			break;
		}
		gfx->setVkbState(true, _vkbSelected);
	}

	// Touchpad: touch to select key (skip blocked keys)
	if (_hasTouchpad) {
		touchpad_report_t report;
		if (touchpad_scan(&report) == 0 && report.contact) {
			int rx = (int)report.x;
			int ry = (int)report.y;
			if (rx < _tpMinX) _tpMinX = rx;
			if (rx > _tpMaxX) _tpMaxX = rx;
			if (ry < _tpMinY) _tpMinY = ry;
			if (ry > _tpMaxY) _tpMaxY = ry;
			int rangeX = _tpMaxX - _tpMinX;
			int rangeY = _tpMaxY - _tpMinY;
			if (rangeX < 1) rangeX = 1;
			if (rangeY < 1) rangeY = 1;
			int mx = (rx - _tpMinX) * NSPIRE_SCREEN_WIDTH / rangeX;
			int my = (_tpMaxY - ry) * NSPIRE_SCREEN_HEIGHT / rangeY;

			for (int i = 0; i < VKB_KEY_COUNT; i++) {
				if (s_vkbKeys[i].ascii == -1) continue;
				if (i < VKB_KEY_COUNT && _vkbBlockedKeys[i]) continue;
				int kx = s_vkbKeys[i].x;
				int ky = VKB_Y0 + s_vkbKeys[i].row * VKB_ROW_H;
				int kw = s_vkbKeys[i].w;

				if (mx >= kx && mx < kx + kw && my >= ky && my < ky + 13) {
					_vkbSelected = i;
					break;
				}
			}
		}
	}

	// Navigation with 2/4/6/8 (these keys are also assignable - nav only while VKB is open)
	static bool lastUp = false, lastDown = false, lastLeft = false, lastRight = false;

	bool curUp = isKeyPressed(KEY_NSPIRE_8) || isKeyPressed(KEY_NSPIRE_UP);
	bool curDown = isKeyPressed(KEY_NSPIRE_2) || isKeyPressed(KEY_NSPIRE_DOWN);
	bool curLeft = isKeyPressed(KEY_NSPIRE_4) || isKeyPressed(KEY_NSPIRE_LEFT);
	bool curRight = isKeyPressed(KEY_NSPIRE_6) || isKeyPressed(KEY_NSPIRE_RIGHT);

	if (curUp && !lastUp) {
		int curRow = s_vkbKeys[_vkbSelected].row;
		int curCenterX = s_vkbKeys[_vkbSelected].x + s_vkbKeys[_vkbSelected].w / 2;
		int bestIdx = -1, bestDist = 9999;
		for (int i = 0; i < VKB_KEY_COUNT; i++) {
			if (s_vkbKeys[i].ascii == -1) continue;
			if (i < VKB_KEY_COUNT && _vkbBlockedKeys[i]) continue;
			if (s_vkbKeys[i].row < curRow) {
				int cx = s_vkbKeys[i].x + s_vkbKeys[i].w / 2;
				int dist = (curRow - s_vkbKeys[i].row) * 100 + abs(cx - curCenterX);
				if (s_vkbKeys[i].row == curRow - 1) dist -= 50;
				if (dist < bestDist) { bestDist = dist; bestIdx = i; }
			}
		}
		if (bestIdx >= 0) _vkbSelected = bestIdx;
	}
	if (curDown && !lastDown) {
		int curRow = s_vkbKeys[_vkbSelected].row;
		int curCenterX = s_vkbKeys[_vkbSelected].x + s_vkbKeys[_vkbSelected].w / 2;
		int bestIdx = -1, bestDist = 9999;
		for (int i = 0; i < VKB_KEY_COUNT; i++) {
			if (s_vkbKeys[i].ascii == -1) continue;
			if (i < VKB_KEY_COUNT && _vkbBlockedKeys[i]) continue;
			if (s_vkbKeys[i].row > curRow) {
				int cx = s_vkbKeys[i].x + s_vkbKeys[i].w / 2;
				int dist = (s_vkbKeys[i].row - curRow) * 100 + abs(cx - curCenterX);
				if (s_vkbKeys[i].row == curRow + 1) dist -= 50;
				if (dist < bestDist) { bestDist = dist; bestIdx = i; }
			}
		}
		if (bestIdx >= 0) _vkbSelected = bestIdx;
	}
	if (curLeft && !lastLeft) {
		int curRow = s_vkbKeys[_vkbSelected].row;
		int bestIdx = -1;
		for (int i = _vkbSelected - 1; i >= 0; i--) {
			if (s_vkbKeys[i].ascii == -1) continue;
			if (i < VKB_KEY_COUNT && _vkbBlockedKeys[i]) continue;
			if (s_vkbKeys[i].row == curRow) { bestIdx = i; break; }
		}
		if (bestIdx >= 0) _vkbSelected = bestIdx;
	}
	if (curRight && !lastRight) {
		int curRow = s_vkbKeys[_vkbSelected].row;
		int bestIdx = -1;
		for (int i = _vkbSelected + 1; i < VKB_KEY_COUNT; i++) {
			if (s_vkbKeys[i].ascii == -1) continue;
			if (i < VKB_KEY_COUNT && _vkbBlockedKeys[i]) continue;
			if (s_vkbKeys[i].row == curRow) { bestIdx = i; break; }
		}
		if (bestIdx >= 0) _vkbSelected = bestIdx;
	}
	lastUp = curUp; lastDown = curDown; lastLeft = curLeft; lastRight = curRight;

	// Enter or 5 -> confirm selection
	static bool lastVkbEnter = false;
	bool vkbEnter = isKeyPressed(KEY_NSPIRE_ENTER) || isKeyPressed(KEY_NSPIRE_5);

	if (vkbEnter && !lastVkbEnter) {
		const VkbKey &key = s_vkbKeys[_vkbSelected];

		// CLR: clear current mapping
		if (key.ascii == -2) {
			_vkbVisible = false;
			NspireGraphicsManager *gfxClr = (NspireGraphicsManager *)_graphicsManager;
			gfxClr->setVkbState(false, 0);
			ConfMan.setInt("remap_clear_request", 1);
			fprintf(stderr, "VKB: CLEAR mapping\n");
			lastVkbEnter = vkbEnter;
			return false;
		}

		if (key.ascii != -1 && !(_vkbSelected < VKB_KEY_COUNT && _vkbBlockedKeys[_vkbSelected])) {
			_vkbVisible = false;
			NspireGraphicsManager *gfxConfirm = (NspireGraphicsManager *)_graphicsManager;
			gfxConfirm->setVkbState(false, 0);

			if (key.keycode == Common::KEYCODE_INVALID) {
				// Mouse button: pass via ConfMan (ascii 0=left, 1=right, 2=middle)
				ConfMan.setInt("remap_confirm_mouse", key.ascii);
			} else {
				// Keyboard key: pass via ConfMan
				ConfMan.setInt("remap_confirm_keycode", (int)key.keycode);
			}

			fprintf(stderr, "VKB: CONFIRM sel=%d keycode=%d\n", _vkbSelected, (int)key.keycode);
			lastVkbEnter = vkbEnter;
			return false;
		}
	}
	lastVkbEnter = vkbEnter;

	// ESC -> cancel
	static bool lastVkbEsc = false;
	bool vkbEsc = isKeyPressed(KEY_NSPIRE_ESC);
	if (vkbEsc && !lastVkbEsc) {
		_vkbVisible = false;
		NspireGraphicsManager *gfxCancel = (NspireGraphicsManager *)_graphicsManager;
		gfxCancel->setVkbState(false, 0);
		ConfMan.setInt("remap_cancel_request", 1);
		fprintf(stderr, "VKB: CANCEL\n");
		lastVkbEsc = vkbEsc;
		return false;
	}
	lastVkbEsc = vkbEsc;

	// Update VKB display
	NspireGraphicsManager *gfx = (NspireGraphicsManager *)_graphicsManager;
	gfx->setVkbState(true, _vkbSelected);

	return false;
}

// Nspire keyboard keys available for mapping
static const Common::KeyTableEntry nspireKeys[] = {
	// Special keys
	{ "ENTER",      Common::KEYCODE_RETURN,     "Enter" },
	{ "SPACE",      Common::KEYCODE_SPACE,      "Space" },
	{ "ESCAPE",     Common::KEYCODE_ESCAPE,     "Esc" },
	{ "TAB",        Common::KEYCODE_TAB,        "Tab" },
	{ "BACKSPACE",  Common::KEYCODE_BACKSPACE,  "Del" },
	{ "F1",         Common::KEYCODE_F1,         "On" },
	{ "F2",         Common::KEYCODE_F2,         "Doc" },
	{ "F3",         Common::KEYCODE_F3,         "Var" },
	{ "F4",         Common::KEYCODE_F4,         "Trg" },
	{ "F5",         Common::KEYCODE_F5,         "Menu" },
	{ "F6",         Common::KEYCODE_F6,         "x^2" },
	{ "F7",         Common::KEYCODE_F7,         "e^x" },
	{ "F8",         Common::KEYCODE_F8,         "10^x" },
	{ "F9",         Common::KEYCODE_F9,         "EE" },
	{ "F10",        Common::KEYCODE_F10,        "Pi" },
	{ "F11",        Common::KEYCODE_F11,        "Flag" },
	{ "LCTRL",      Common::KEYCODE_LCTRL,      "Ctrl" },
	{ "LSHIFT",     Common::KEYCODE_LSHIFT,     "Shift" },
	// Arrow keys
	{ "UP",         Common::KEYCODE_UP,         "Up" },
	{ "DOWN",       Common::KEYCODE_DOWN,       "Down" },
	{ "LEFT",       Common::KEYCODE_LEFT,       "Left" },
	{ "RIGHT",      Common::KEYCODE_RIGHT,      "Right" },
	// Numbers
	{ "0", Common::KEYCODE_0, "0" },
	{ "1", Common::KEYCODE_1, "1" },
	{ "2", Common::KEYCODE_2, "2" },
	{ "3", Common::KEYCODE_3, "3" },
	{ "4", Common::KEYCODE_4, "4" },
	{ "5", Common::KEYCODE_5, "5" },
	{ "6", Common::KEYCODE_6, "6" },
	{ "7", Common::KEYCODE_7, "7" },
	{ "8", Common::KEYCODE_8, "8" },
	{ "9", Common::KEYCODE_9, "9" },
	// Operators and symbols
	{ "EQUALS",     Common::KEYCODE_EQUALS,     "=" },
	{ "PLUS",       Common::KEYCODE_PLUS,       "+" },
	{ "MINUS",      Common::KEYCODE_MINUS,      "-" },
	{ "ASTERISK",   Common::KEYCODE_ASTERISK,   "*" },
	{ "SLASH",      Common::KEYCODE_SLASH,      "/" },
	{ "CARET",      Common::KEYCODE_CARET,      "^" },
	{ "LEFTPAREN",  Common::KEYCODE_LEFTPAREN,  "(" },
	{ "RIGHTPAREN", Common::KEYCODE_RIGHTPAREN, ")" },
	{ "PERIOD",     Common::KEYCODE_PERIOD,     "." },
	// Letters
	{ "a", Common::KEYCODE_a, "A" },
	{ "b", Common::KEYCODE_b, "B" },
	{ "c", Common::KEYCODE_c, "C" },
	{ "d", Common::KEYCODE_d, "D" },
	{ "e", Common::KEYCODE_e, "E" },
	{ "f", Common::KEYCODE_f, "F" },
	{ "g", Common::KEYCODE_g, "G" },
	{ "h", Common::KEYCODE_h, "H" },
	{ "i", Common::KEYCODE_i, "I" },
	{ "j", Common::KEYCODE_j, "J" },
	{ "k", Common::KEYCODE_k, "K" },
	{ "l", Common::KEYCODE_l, "L" },
	{ "m", Common::KEYCODE_m, "M" },
	{ "n", Common::KEYCODE_n, "N" },
	{ "o", Common::KEYCODE_o, "O" },
	{ "p", Common::KEYCODE_p, "P" },
	{ "q", Common::KEYCODE_q, "Q" },
	{ "r", Common::KEYCODE_r, "R" },
	{ "s", Common::KEYCODE_s, "S" },
	{ "t", Common::KEYCODE_t, "T" },
	{ "u", Common::KEYCODE_u, "U" },
	{ "v", Common::KEYCODE_v, "V" },
	{ "w", Common::KEYCODE_w, "W" },
	{ "x", Common::KEYCODE_x, "X" },
	{ "y", Common::KEYCODE_y, "Y" },
	{ "z", Common::KEYCODE_z, "Z" },
	{ nullptr, Common::KEYCODE_INVALID, nullptr }
};

static const Common::ModifierTableEntry nspireModifiers[] = {
	{ 0, "",      "" },
	{ Common::KBD_CTRL,  "C+",  "Ctrl+" },
	{ Common::KBD_SHIFT, "S+",  "Shift+" },
	{ 0, nullptr, nullptr }
};

Common::HardwareInputSet *OSystem_NSPIRE::getHardwareInputSet() {
	using namespace Common;
	CompositeHardwareInputSet *inputSet = new CompositeHardwareInputSet();
	inputSet->addHardwareInputSet(new MouseHardwareInputSet(defaultMouseButtons));
	inputSet->addHardwareInputSet(new KeyboardHardwareInputSet(nspireKeys, nspireModifiers));
	return inputSet;
}

Common::KeymapperDefaultBindings *OSystem_NSPIRE::getKeymapperDefaultBindings() {
	Common::KeymapperDefaultBindings *bindings = new Common::KeymapperDefaultBindings();

	// Global bindings
	bindings->setDefaultBinding(Common::kGlobalKeymapName, Common::kStandardActionOpenMainMenu, "F5");
	bindings->setDefaultBinding(Common::kGlobalKeymapName, "QUIT", "F1");

	// Standard game actions
	bindings->setDefaultBinding(Common::kStandardActionsKeymapName, Common::kStandardActionLeftClick, "LCTRL");
	bindings->setDefaultBinding(Common::kStandardActionsKeymapName, Common::kStandardActionMiddleClick, "LSHIFT");
	bindings->setDefaultBinding(Common::kStandardActionsKeymapName, Common::kStandardActionRightClick, "F3");
	bindings->setDefaultBinding(Common::kStandardActionsKeymapName, Common::kStandardActionInteract, "LCTRL");
	bindings->setDefaultBinding(Common::kStandardActionsKeymapName, Common::kStandardActionSkip, "SPACE");
	bindings->setDefaultBinding(Common::kStandardActionsKeymapName, Common::kStandardActionPause, "SPACE");
	bindings->setDefaultBinding(Common::kStandardActionsKeymapName, Common::kStandardActionMoveUp, "8");
	bindings->setDefaultBinding(Common::kStandardActionsKeymapName, Common::kStandardActionMoveDown, "2");
	bindings->setDefaultBinding(Common::kStandardActionsKeymapName, Common::kStandardActionMoveLeft, "4");
	bindings->setDefaultBinding(Common::kStandardActionsKeymapName, Common::kStandardActionMoveRight, "6");

	return bindings;
}
