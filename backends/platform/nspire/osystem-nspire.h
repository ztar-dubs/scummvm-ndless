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

#ifndef BACKENDS_PLATFORM_NSPIRE_H
#define BACKENDS_PLATFORM_NSPIRE_H

#include "backends/modular-backend.h"
#include "backends/events/default/default-events.h"
#include "common/events.h"

#define NSPIRE_SCREEN_WIDTH  320
#define NSPIRE_SCREEN_HEIGHT 240

struct SDL_Surface;

class OSystem_NSPIRE : public ModularMixerBackend, public ModularGraphicsBackend, Common::EventSource {
public:
	OSystem_NSPIRE();
	virtual ~OSystem_NSPIRE();

	void initBackend() override;

	bool pollEvent(Common::Event &event) override;

	Common::MutexInternal *createMutex() override;
	uint32 getMillis(bool skipRecord = false) override;
	void delayMillis(uint msecs) override;
	void getTimeAndDate(TimeDate &td, bool skipRecord = false) const override;

	void quit() override;

	void logMessage(LogMessageType::Type type, const char *message) override;

	void addSysArchivesToSearchSet(Common::SearchSet &s, int priority) override;

	Common::Path getDefaultConfigFileName() override;
	Common::Path getDefaultLogFileName() override;

	Common::HardwareInputSet *getHardwareInputSet() override;
	Common::KeymapperDefaultBindings *getKeymapperDefaultBindings() override;

	bool hasFeature(Feature f) override;
	void setFeatureState(Feature f, bool enable) override;
	bool getFeatureState(Feature f) override;

private:
	uint32 _startTicks;
	SDL_Surface *_screen;

	bool _quitRequested;

	// Touchpad emulated mouse
	int _mouseX, _mouseY;
	bool _mouseVisible;

	// Touchpad state
	bool _tpInited;
	bool _hasTouchpad;
	int _tpCenterX, _tpCenterY;
	int _tpMinX, _tpMaxX, _tpMinY, _tpMaxY; // Dynamic touchpad calibration

	// Virtual keyboard
	bool _vkbVisible;
	int _vkbSelected;
	bool _vkbNeedsInit;
	bool _vkbBlockedKeys[80]; // Keys blocked (assigned to other actions)
	bool pollVirtualKeyboard(Common::Event &event);

	void initSDL();
	void deinitSDL();

	void pollKeys(Common::Event &event, bool &hasEvent);
	void pollTouchpad(Common::Event &event, bool &hasEvent);
};

#endif // BACKENDS_PLATFORM_NSPIRE_H
