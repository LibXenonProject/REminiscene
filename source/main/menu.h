/* REminiscence - Flashback interpreter
 * Copyright (C) 2005-2011 Gregory Montoir
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
 */

#ifndef MENU_H__
#define MENU_H__

#include "intern.h"

struct ModPlayer;
struct Resource;
struct SystemStub;
struct Video;

struct Menu {
	enum {
		MENU_OPTION_ITEM_START,
		MENU_OPTION_ITEM_SKILL,
		MENU_OPTION_ITEM_PASSWORD,
		MENU_OPTION_ITEM_LEVEL,
		MENU_OPTION_ITEM_INFO,
		MENU_OPTION_ITEM_QUIT
	};

	enum {
		EVENTS_DELAY = 80
	};

	static const char *_passwords[8][3];

	ModPlayer *_ply;
	Resource *_res;
	SystemStub *_stub;
	Video *_vid;

	const char **_textOptions;
	uint8 _charVar1;
	uint8 _charVar2;
	uint8 _charVar3;
	uint8 _charVar4;
	uint8 _charVar5;

	Menu(ModPlayer *ply, Resource *res, SystemStub *stub, Video *vid);

	void drawString(const char *str, int16 y, int16 x, uint8 color);
	void drawString2(const char *str, int16 y, int16 x);
	void loadPicture(const char *prefix);
	void handleInfoScreen();
	void handleSkillScreen(uint8 &new_skill);
	bool handlePasswordScreen(uint8 &new_skill, uint8 &new_level);
	bool handleLevelScreen(uint8 &new_skill, uint8 &new_level);
	bool handleTitleScreen(uint8 &new_skill, uint8 &new_level);
};

#endif // MENU_H__
