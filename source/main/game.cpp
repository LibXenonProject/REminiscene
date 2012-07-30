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

#include <ctime>
#include "file.h"
#include "systemstub.h"
#include "unpack.h"
#include "game.h"
#include "seq_player.h"


Game::Game(SystemStub *stub, FileSystem *fs, const char *savePath, int level, ResourceType ver, Language lang)
	: _cut(&_modPly, &_res, stub, &_vid), _menu(&_modPly, &_res, stub, &_vid),
	_mix(stub), _modPly(&_mix, fs), _res(fs, ver, lang), _seqPly(stub, &_mix), _sfxPly(&_mix), _vid(&_res, stub),
	_stub(stub), _fs(fs), _savePath(savePath) {
	_stateSlot = 1;
	_inp_demo = 0;
	_inp_record = false;
	_inp_replay = false;
	_skillLevel = 1;
	_currentLevel = level;
}

void Game::run() {
	_stub->init("REminiscence", Video::GAMESCREEN_W, Video::GAMESCREEN_H);

	_randSeed = time(0);

	_res.load_TEXT();

	switch (_res._type) {
	case kResourceTypeAmiga:
		_res.load("FONT8", Resource::OT_FNT, "SPR");
		break;
	case kResourceTypePC:
		_res.load("FB_TXT", Resource::OT_FNT);
		_res._hasSeqData = File().open("INTRO.SEQ", "rb", _fs);
		break;
	}

#ifndef BYPASS_PROTECTION
	while (!handleProtectionScreen());
	if (_stub->_pi.quit) {
		return;
	}
#endif

	_mix.init();

	playCutscene(0x40);
	playCutscene(0x0D);
	if (!_cut._interrupted && _res._type == kResourceTypePC) {
		playCutscene(0x4A);
	}

	switch (_res._type) {
	case kResourceTypeAmiga:
		_res.load("ICONE", Resource::OT_ICN, "SPR");
		_res.load("ICON", Resource::OT_ICN, "SPR");
		_res.load("PERSO", Resource::OT_SPM);
		break;
	case kResourceTypePC:
		_res.load("GLOBAL", Resource::OT_ICN);
		_res.load("GLOBAL", Resource::OT_SPC);
		_res.load("PERSO", Resource::OT_SPR);
		_res.load_SPR_OFF("PERSO", _res._spr1);
		_res.load_FIB("GLOBAL");
		break;
	}

	while (!_stub->_pi.quit && (_res._type == kResourceTypeAmiga || _menu.handleTitleScreen(_skillLevel, _currentLevel))) {
		if (_currentLevel == 7) {
			_vid.fadeOut();
			_vid.setTextPalette();
			playCutscene(0x3D);
		} else {
			_vid.setTextPalette();
			_vid.setPalette0xF();
			_stub->setOverscanColor(0xE0);
			mainLoop();
		}
	}

	_res.free_TEXT();

	_mix.free();
	_stub->destroy();
}

void Game::resetGameState() {
	_animBuffers._states[0] = _animBuffer0State;
	_animBuffers._curPos[0] = 0xFF;
	_animBuffers._states[1] = _animBuffer1State;
	_animBuffers._curPos[1] = 0xFF;
	_animBuffers._states[2] = _animBuffer2State;
	_animBuffers._curPos[2] = 0xFF;
	_animBuffers._states[3] = _animBuffer3State;
	_animBuffers._curPos[3] = 0xFF;
	_currentRoom = _res._pgeInit[0].init_room;
	_cut._deathCutsceneId = 0xFFFF;
	_pge_opTempVar2 = 0xFFFF;
	_deathCutsceneCounter = 0;
	_saveStateCompleted = false;
	_loadMap = true;
	pge_resetGroups();
	_blinkingConradCounter = 0;
	_pge_processOBJ = false;
	_pge_opTempVar1 = 0;
	_textToDisplay = 0xFFFF;
}

void Game::mainLoop() {
	_vid._unkPalSlot1 = 0;
	_vid._unkPalSlot2 = 0;
	_score = 0;
	loadLevelData();
	resetGameState();
	while (!_stub->_pi.quit) {
		playCutscene();
		if (_cut._id == 0x3D) {
			showFinalScore();
			break;
		}
		if (_deathCutsceneCounter) {
			--_deathCutsceneCounter;
			if (_deathCutsceneCounter == 0) {
				playCutscene(_cut._deathCutsceneId);
				if (!handleContinueAbort()) {
					playCutscene(0x41);
					break;
				} else {
					if (_validSaveState) {
						if (!loadGameState(0)) {
							break;
						}
					} else {
						loadLevelData();
						resetGameState();
					}
					continue;
				}

			}
		}
		memcpy(_vid._frontLayer, _vid._backLayer, Video::GAMESCREEN_W * Video::GAMESCREEN_H);
		pge_getInput();
		pge_prepare();
		col_prepareRoomState();
		uint8 oldLevel = _currentLevel;
		for (uint16 i = 0; i < _res._pgeNum; ++i) {
			LivePGE *pge = _pge_liveTable2[i];
			if (pge) {
				_col_currentPiegeGridPosY = (pge->pos_y / 36) & ~1;
				_col_currentPiegeGridPosX = (pge->pos_x + 8) >> 4;
				pge_process(pge);
			}
		}
		if (oldLevel != _currentLevel) {
			changeLevel();
			_pge_opTempVar1 = 0;
			continue;
		}
		if (_loadMap) {
			if (_currentRoom == 0xFF) {
				_cut._id = 6;
				_deathCutsceneCounter = 1;
			} else {
				_currentRoom = _pgeLive[0].room_location;
				loadLevelMap();
				_loadMap = false;
				_vid.fullRefresh();
			}
		}
		prepareAnims();
		drawAnims();
		drawCurrentInventoryItem();
		drawLevelTexts();
		printLevelCode();
		if (_blinkingConradCounter != 0) {
			--_blinkingConradCounter;
		}
		_vid.updateScreen();
		updateTiming();
		drawStoryTexts();
		if (_stub->_pi.backspace) {
			_stub->_pi.backspace = false;
			handleInventory();
		}
		if (_stub->_pi.escape) {
			_stub->_pi.escape = false;
			if (handleConfigPanel()) {
				break;
			}
		}
		inp_handleSpecialKeys();
	}
}

void Game::updateTiming() {
	static uint32 tstamp = 0;
	int32 delay = _stub->getTimeStamp() - tstamp;
	int32 pause = (_stub->_pi.dbgMask & PlayerInput::DF_FASTMODE) ? 20 : 30;
	pause -= delay;
	if (pause > 0) {
		_stub->sleep(pause);
	}
	tstamp = _stub->getTimeStamp();
}

void Game::playCutscene(int id) {
	if (id != -1) {
		_cut._id = id;
	}
	if (_cut._id != 0xFFFF) {
		_sfxPly.stop();
		if (_res._hasSeqData) {
			int num = 0;
			switch (_cut._id) {
			case 0x03: {
					static const uint8 tab[] = { 1, 2, 1, 3, 3, 4, 4 };
					num = tab[_currentLevel];
				}
				break;
			case 0x05: {
					static const uint8 tab[] = { 1, 2, 3, 5, 5, 4, 4 };
					num = tab[_currentLevel];
				}
				break;
			case 0x0A: {
					static const uint8 tab[] = { 1, 2, 2, 2, 2, 2, 2 };
					num = tab[_currentLevel];
				}
				break;
			case 0x10: {
					static const uint8 tab[] = { 1, 1, 1, 2, 2, 3, 3 };
					num = tab[_currentLevel];
				}
				break;
			case 0x3B:
				return;
			case 0x3C: {
					static const uint8 tab[] = { 1, 1, 1, 1, 1, 2, 2 };
					num = tab[_currentLevel];
				}
				break;
			case 0x40:
				return;
			case 0x4A:
				return;
			}
			if (SeqPlayer::_namesTable[_cut._id]) {
			        char name[16];
			        snprintf(name, sizeof(name), "%s.SEQ", SeqPlayer::_namesTable[_cut._id]);
				char *p = strchr(name, '0');
				if (p) {
					*p += num;
				}
			        if (playCutsceneSeq(name)) {
					if (_cut._id == 0x3D) {
						playCutsceneSeq("CREDITS.SEQ");
						_cut._interrupted = false;
					} else {
						_cut._id = 0xFFFF;
					}
					return;
				}
			}
		}
		_cut.play();
	}
}

bool Game::playCutsceneSeq(const char *name) {
	File f;
	if (f.open(name, "rb", _fs)) {
		_seqPly.setBackBuffer(_res._memBuf);
		_seqPly.play(&f);
		_vid.fullRefresh();
		return true;
	}
	return false;
}

void Game::inp_handleSpecialKeys() {
	if (_stub->_pi.dbgMask & PlayerInput::DF_SETLIFE) {
		_pgeLive[0].life = 0x7FFF;
	}
	if (_stub->_pi.load) {
		loadGameState(_stateSlot);
		_stub->_pi.load = false;
	}
	if (_stub->_pi.save) {
		saveGameState(_stateSlot);
		_stub->_pi.save = false;
	}
	if (_stub->_pi.stateSlot != 0) {
		int8 slot = _stateSlot + _stub->_pi.stateSlot;
		if (slot >= 1 && slot < 100) {
			_stateSlot = slot;
			debug(DBG_INFO, "Current game state slot is %d", _stateSlot);
		}
		_stub->_pi.stateSlot = 0;
	}
	if (_stub->_pi.inpRecord || _stub->_pi.inpReplay) {
		bool replay = false;
		bool record = false;
		char demoFile[20];
		makeGameDemoName(demoFile);
		if (_inp_demo) {
			_inp_demo->close();
			delete _inp_demo;
		}
		_inp_demo = new File;
		if (_stub->_pi.inpRecord) {
			if (_inp_record) {
				debug(DBG_INFO, "Stop recording input keys");
			} else {
				if (_inp_demo->open(demoFile, "zwb", _savePath)) {
					debug(DBG_INFO, "Recording input keys");
					_inp_demo->writeUint32BE('FBDM');
					_inp_demo->writeUint16BE(0);
					_inp_demo->writeUint32BE(_randSeed);
					record = true;
				} else {
					warning("Unable to save demo file '%s'", demoFile);
				}
			}
		}
		if (_stub->_pi.inpReplay) {
			if (_inp_replay) {
				debug(DBG_INFO, "Stop replaying input keys");
			} else {
				if (_inp_demo->open(demoFile, "zrb", _savePath)) {
					debug(DBG_INFO, "Replaying input keys");
					_inp_demo->readUint32BE();
					_inp_demo->readUint16BE();
					_randSeed = _inp_demo->readUint32BE();
					replay = true;
				} else {
					warning("Unable to open demo file '%s'", demoFile);
				}
			}
		}
		_inp_record = record;
		_inp_replay = replay;
		_stub->_pi.inpReplay = false;
		_stub->_pi.inpRecord = false;
	}
}

void Game::drawCurrentInventoryItem() {
	uint16 src = _pgeLive[0].current_inventory_PGE;
	if (src != 0xFF) {
		_currentIcon = _res._pgeInit[src].icon_num;
		drawIcon(_currentIcon, 232, 8, 0xA);
	}
}

void Game::showFinalScore() {
	playCutscene(0x49);
	char buf[50];
	snprintf(buf, sizeof(buf), "SCORE %08u", _score);
	_vid.drawString(buf, (256 - strlen(buf) * 8) / 2, 40, 0xE5);
	strcpy(buf, _menu._passwords[7][_skillLevel]);
	_vid.drawString(buf, (256 - strlen(buf) * 8) / 2, 16, 0xE7);
	while (!_stub->_pi.quit) {
		_stub->copyRect(0, 0, Video::GAMESCREEN_W, Video::GAMESCREEN_H, _vid._frontLayer, 256);
		_stub->updateScreen(0);
		_stub->processEvents();
		if (_stub->_pi.enter) {
			_stub->_pi.enter = false;
			break;
		}
		_stub->sleep(100);
	}
}

bool Game::handleConfigPanel() {
	if (_res._type == kResourceTypeAmiga) {
		return true;
	}
	const int x = 7;
	const int y = 10;
	const int w = 17;
	const int h = 12;

	_vid._charShadowColor = 0xE2;
	_vid._charFrontColor = 0xEE;
	_vid._charTransparentColor = 0xFF;

	_vid.PC_drawChar(0x81, y, x);
	for (int i = 1; i < w; ++i) {
		_vid.PC_drawChar(0x85, y, x + i);
	}
	_vid.PC_drawChar(0x82, y, x + w);
	for (int j = 1; j < h; ++j) {
		_vid.PC_drawChar(0x86, y + j, x);
		for (int i = 1; i < w; ++i) {
			_vid._charTransparentColor = 0xE2;
			_vid.PC_drawChar(0x20, y + j, x + i);
		}
		_vid._charTransparentColor = 0xFF;
		_vid.PC_drawChar(0x87, y + j, x + w);
	}
	_vid.PC_drawChar(0x83, y + h, x);
	for (int i = 1; i < w; ++i) {
		_vid.PC_drawChar(0x88, y + h, x + i);
	}
	_vid.PC_drawChar(0x84, y + h, x + w);

	_menu._charVar3 = 0xE4;
	_menu._charVar4 = 0xE5;
	_menu._charVar1 = 0xE2;
	_menu._charVar2 = 0xEE;

	_vid.fullRefresh();
	enum { MENU_ITEM_LOAD = 1, MENU_ITEM_SAVE = 2, MENU_ITEM_ABORT = 3 };
	uint8 colors[] = { 2, 3, 3, 3 };
	int current = 0;
	while (!_stub->_pi.quit) {
		_menu.drawString(_res.getMenuString(LocaleData::LI_18_RESUME_GAME), y + 2, 9, colors[0]);
		_menu.drawString(_res.getMenuString(LocaleData::LI_20_LOAD_GAME), y + 4, 9, colors[1]);
		_menu.drawString(_res.getMenuString(LocaleData::LI_21_SAVE_GAME), y + 6, 9, colors[2]);
		_menu.drawString(_res.getMenuString(LocaleData::LI_19_ABORT_GAME), y + 8, 9, colors[3]);
		char buf[30];
		snprintf(buf, sizeof(buf), "%s : %d-%02d", _res.getMenuString(LocaleData::LI_22_SAVE_SLOT), _currentLevel + 1, _stateSlot);
		_menu.drawString(buf, y + 10, 9, 1);

		_vid.updateScreen();
		_stub->sleep(80);
		inp_update();

		int prev = current;
		if (_stub->_pi.dirMask & PlayerInput::DIR_UP) {
			_stub->_pi.dirMask &= ~PlayerInput::DIR_UP;
			current = (current + 3) % 4;
		}
		if (_stub->_pi.dirMask & PlayerInput::DIR_DOWN) {
			_stub->_pi.dirMask &= ~PlayerInput::DIR_DOWN;
			current = (current + 1) % 4;
		}
		if (_stub->_pi.dirMask & PlayerInput::DIR_LEFT) {
			_stub->_pi.dirMask &= ~PlayerInput::DIR_LEFT;
			--_stateSlot;
			if (_stateSlot < 1) {
				_stateSlot = 1;
			}
		}
		if (_stub->_pi.dirMask & PlayerInput::DIR_RIGHT) {
			_stub->_pi.dirMask &= ~PlayerInput::DIR_RIGHT;
			++_stateSlot;
			if (_stateSlot > 99) {
				_stateSlot = 99;
			}
		}
		if (prev != current) {
			SWAP(colors[prev], colors[current]);
		}
		if (_stub->_pi.enter) {
			_stub->_pi.enter = false;
			switch (current) {
			case MENU_ITEM_LOAD:
				_stub->_pi.load = true;
				break;
			case MENU_ITEM_SAVE:
				_stub->_pi.save = true;
				break;
			}
			break;
		}
	}
	_vid.fullRefresh();
	return (current == MENU_ITEM_ABORT);
}

bool Game::handleContinueAbort() {
	playCutscene(0x48);
	int timeout = 100;
	int current_color = 0;
	uint8 colors[] = { 0xE4, 0xE5 };
	uint8 color_inc = 0xFF;
	Color col;
	_stub->getPaletteEntry(0xE4, &col);
	memcpy(_vid._tempLayer, _vid._frontLayer, Video::GAMESCREEN_W * Video::GAMESCREEN_H);
	while (timeout >= 0 && !_stub->_pi.quit) {
		const char *str;
		str = _res.getMenuString(LocaleData::LI_01_CONTINUE_OR_ABORT);
		_vid.drawString(str, (256 - strlen(str) * 8) / 2, 64, 0xE3);
		str = _res.getMenuString(LocaleData::LI_02_TIME);
		char buf[50];
		snprintf(buf, sizeof(buf), "%s : %d", str, timeout / 10);
		_vid.drawString(buf, 96, 88, 0xE3);
		str = _res.getMenuString(LocaleData::LI_03_CONTINUE);
		_vid.drawString(str, (256 - strlen(str) * 8) / 2, 104, colors[0]);
		str = _res.getMenuString(LocaleData::LI_04_ABORT);
		_vid.drawString(str, (256 - strlen(str) * 8) / 2, 112, colors[1]);
		snprintf(buf, sizeof(buf), "SCORE  %08u", _score);
		_vid.drawString(buf, 64, 154, 0xE3);
		if (_stub->_pi.dirMask & PlayerInput::DIR_UP) {
			_stub->_pi.dirMask &= ~PlayerInput::DIR_UP;
			if (current_color > 0) {
				SWAP(colors[current_color], colors[current_color - 1]);
				--current_color;
			}
		}
		if (_stub->_pi.dirMask & PlayerInput::DIR_DOWN) {
			_stub->_pi.dirMask &= ~PlayerInput::DIR_DOWN;
			if (current_color < 1) {
				SWAP(colors[current_color], colors[current_color + 1]);
				++current_color;
			}
		}
		if (_stub->_pi.enter) {
			_stub->_pi.enter = false;
			return (current_color == 0);
		}
		_stub->copyRect(0, 0, Video::GAMESCREEN_W, Video::GAMESCREEN_H, _vid._frontLayer, 256);
		_stub->updateScreen(0);
		if (col.b >= 0x3D) {
			color_inc = 0;
		}
		if (col.b < 2) {
			color_inc = 0xFF;
		}
		if (color_inc == 0xFF) {
			col.b += 2;
			col.g += 2;
		} else {
			col.b -= 2;
			col.g -= 2;
		}
		_stub->setPaletteEntry(0xE4, &col);
		_stub->processEvents();
		_stub->sleep(100);
		--timeout;
		memcpy(_vid._frontLayer, _vid._tempLayer, Video::GAMESCREEN_W * Video::GAMESCREEN_H);
	}
	return false;
}

bool Game::handleProtectionScreen() {
	bool valid = true;
	_cut.prepare();
	_cut.copyPalette(_protectionPal, 0);
	_cut.updatePalette();
	_cut._gfx.setClippingRect(64, 48, 128, 128);

	_menu._charVar1 = 0xE0;
	_menu._charVar2 = 0xEF;
	_menu._charVar4 = 0xE5;
	_menu._charVar5 = 0xE2;

	int shapeNum = getRandomNumber() % 30;
	for (int16 zoom = 2000; zoom != 0; zoom -= 100) {
		_cut.drawProtectionShape(shapeNum, zoom);
		_stub->copyRect(0, 0, Video::GAMESCREEN_W, Video::GAMESCREEN_H, _vid._tempLayer, 256);
		_stub->updateScreen(0);
		_stub->sleep(30);
	}
	int codeNum = getRandomNumber() % 5;
	_cut.drawProtectionShape(shapeNum, 1);
	_vid.setTextPalette();
	char codeText[7];
	int len = 0;
	do {
		codeText[len] = '\0';
		memcpy(_vid._frontLayer, _vid._tempLayer, Video::GAMESCREEN_W * Video::GAMESCREEN_H);
		_menu.drawString("PROTECTION", 2, 11, 5);
		char buf[20];
		snprintf(buf, sizeof(buf), "CODE %d :  %s", codeNum + 1, codeText);
		_menu.drawString(buf, 23, 8, 5);
		_vid.updateScreen();
		_stub->sleep(50);
		_stub->processEvents();
		char c = _stub->_pi.lastChar;
		if (c != 0) {
			_stub->_pi.lastChar = 0;
			if (len < 6) {
				if (c >= 'a' && c <= 'z') {
					c &= ~0x20;
				}
				if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
					codeText[len] = c;
					++len;
				}
			}
		}
		if (_stub->_pi.backspace) {
			_stub->_pi.backspace = false;
			if (len > 0) {
				--len;
			}
		}
		if (_stub->_pi.enter) {
			_stub->_pi.enter = false;
			if (len > 0) {
				const uint8 *p = _protectionCodeData + shapeNum * 0x1E + codeNum * 6;
				for (int i = 0; i < len; ++i) {
					uint8 r = 0;
					uint8 ch = codeText[i];
					for (int b = 0; b < 8; ++b) {
						if (ch & (1 << b)) {
							r |= (1 << (7 - b));
						}
					}
					r ^= 0x55;
					if (r != *p++) {
						valid = false;
						break;
					}
				}
				break;
			}
		}
	} while (!_stub->_pi.quit);
	_vid.fadeOut();
	return valid;
}

void Game::printLevelCode() {
	if (_printLevelCodeCounter != 0) {
		--_printLevelCodeCounter;
		if (_printLevelCodeCounter != 0) {
			char levelCode[50];
			snprintf(levelCode, sizeof(levelCode), "CODE: %s", _menu._passwords[_currentLevel][_skillLevel]);
			_vid.drawString(levelCode, (Video::GAMESCREEN_W - strlen(levelCode) * 8) / 2, 16, 0xE7);
		}
	}
}

void Game::printSaveStateCompleted() {
	if (_saveStateCompleted) {
		const char *str = _res.getMenuString(LocaleData::LI_05_COMPLETED);
		_vid.drawString(str, (176 - strlen(str) * 8) / 2, 34, 0xE6);
	}
}

void Game::drawLevelTexts() {
	LivePGE *pge = &_pgeLive[0];
	int8 obj = col_findCurrentCollidingObject(pge, 3, 0xFF, 0xFF, &pge);
	if (obj == 0) {
		obj = col_findCurrentCollidingObject(pge, 0xFF, 5, 9, &pge);
	}
	if (obj > 0) {
		_printLevelCodeCounter = 0;
		if (_textToDisplay == 0xFFFF) {
			uint8 icon_num = obj - 1;
			drawIcon(icon_num, 80, 8, 0xA);
			uint8 txt_num = pge->init_PGE->text_num;
			const char *str = (const char *)_res._tbn + READ_LE_UINT16(_res._tbn + txt_num * 2);
			_vid.drawString(str, (176 - strlen(str) * 8) / 2, 26, 0xE6);
			if (icon_num == 2) {
				printSaveStateCompleted();
				return;
			}
		} else {
			_currentInventoryIconNum = obj - 1;
		}
	}
	_saveStateCompleted = false;
}

void Game::drawStoryTexts() {
	if (_textToDisplay != 0xFFFF) {
		uint16 text_col_mask = 0xE8;
		const uint8 *str = _res.getGameString(_textToDisplay);
		memcpy(_vid._tempLayer, _vid._frontLayer, Video::GAMESCREEN_W * Video::GAMESCREEN_H);
		int textSpeechSegment = 0;
		while (!_stub->_pi.quit) {
			drawIcon(_currentInventoryIconNum, 80, 8, 0xA);
			if (*str == 0xFF) {
				text_col_mask = READ_LE_UINT16(str + 1);
				str += 3;
			}
			int16 text_y_pos = 26;
			while (1) {
				uint16 len = getLineLength(str);
				str = (const uint8 *)_vid.drawString((const char *)str, (176 - len * 8) / 2, text_y_pos, text_col_mask);
				text_y_pos += 8;
				if (*str == 0 || *str == 0xB) {
					break;
				}
				++str;
			}
			MixerChunk chunk;
			_res.load_VCE(_textToDisplay, textSpeechSegment++, &chunk.data, &chunk.len);
			if (chunk.data) {
				_mix.play(&chunk, 32000, Mixer::MAX_VOLUME);
			}
			_vid.updateScreen();
			while (!_stub->_pi.backspace && !_stub->_pi.quit) {
				inp_update();
				_stub->sleep(80);
			}
			if (chunk.data) {
				_mix.stopAll();
				free(chunk.data);
			}
			_stub->_pi.backspace = false;
			if (*str == 0) {
				break;
			}
			++str;
			memcpy(_vid._frontLayer, _vid._tempLayer, Video::GAMESCREEN_W * Video::GAMESCREEN_H);
		}
		_textToDisplay = 0xFFFF;
	}
}

void Game::prepareAnims() {
	if (!(_currentRoom & 0x80) && _currentRoom < 0x40) {
		int8 pge_room;
		LivePGE *pge = _pge_liveTable1[_currentRoom];
		while (pge) {
			prepareAnimsHelper(pge, 0, 0);
			pge = pge->next_PGE_in_room;
		}
		pge_room = _res._ctData[CT_UP_ROOM + _currentRoom];
		if (pge_room >= 0 && pge_room < 0x40) {
			pge = _pge_liveTable1[pge_room];
			while (pge) {
				if ((pge->init_PGE->object_type != 10 && pge->pos_y > 176) || (pge->init_PGE->object_type == 10 && pge->pos_y > 216)) {
					prepareAnimsHelper(pge, 0, -216);
				}
				pge = pge->next_PGE_in_room;
			}
		}
		pge_room = _res._ctData[CT_DOWN_ROOM + _currentRoom];
		if (pge_room >= 0 && pge_room < 0x40) {
			pge = _pge_liveTable1[pge_room];
			while (pge) {
				if (pge->pos_y < 48) {
					prepareAnimsHelper(pge, 0, 216);
				}
				pge = pge->next_PGE_in_room;
			}
		}
		pge_room = _res._ctData[CT_LEFT_ROOM + _currentRoom];
		if (pge_room >= 0 && pge_room < 0x40) {
			pge = _pge_liveTable1[pge_room];
			while (pge) {
				if (pge->pos_x > 224) {
					prepareAnimsHelper(pge, -256, 0);
				}
				pge = pge->next_PGE_in_room;
			}
		}
		pge_room = _res._ctData[CT_RIGHT_ROOM + _currentRoom];
		if (pge_room >= 0 && pge_room < 0x40) {
			pge = _pge_liveTable1[pge_room];
			while (pge) {
				if (pge->pos_x <= 32) {
					prepareAnimsHelper(pge, 256, 0);
				}
				pge = pge->next_PGE_in_room;
			}
		}
	}
}

void Game::prepareAnimsHelper(LivePGE *pge, int16 dx, int16 dy) {
	debug(DBG_GAME, "Game::prepareAnimsHelper() dx=0x%X dy=0x%X pge_num=%d pge->flags=0x%X pge->anim_number=0x%X", dx, dy, pge - &_pgeLive[0], pge->flags, pge->anim_number);
	if (!(pge->flags & 8)) {
		if (pge->index != 0 && loadMonsterSprites(pge) == 0) {
			return;
		}
		assert(pge->anim_number < 1287);
		const uint8 *dataPtr = _res._spr_off[pge->anim_number];
		if (dataPtr == 0) {
			return;
		}
		const int8 dw = (int8)dataPtr[0];
		const int8 dh = (int8)dataPtr[1];
		uint8 w = 0, h = 0;
		switch (_res._type) {
		case kResourceTypeAmiga:
			w = ((dataPtr[2] >> 7) + 1) * 16;
			h = dataPtr[2] & 0x7F;
			break;
		case kResourceTypePC:
			w = dataPtr[2];
			h = dataPtr[3];
			dataPtr += 4;
			break;
		}
		const int16 ypos = dy + pge->pos_y - dh + 2;
		int16 xpos = dx + pge->pos_x - dw;
		if (pge->flags & 2) {
			xpos = dw + dx + pge->pos_x;
			uint8 _cl = w;
			if (_cl & 0x40) {
				_cl = h;
			} else {
				_cl &= 0x3F;
			}
			xpos -= _cl;
		}
		if (xpos <= -32 || xpos >= 256 || ypos < -48 || ypos >= 224) {
			return;
		}
		xpos += 8;
		if (pge == &_pgeLive[0]) {
			_animBuffers.addState(1, xpos, ypos, dataPtr, pge, w, h);
		} else if (pge->flags & 0x10) {
			_animBuffers.addState(2, xpos, ypos, dataPtr, pge, w, h);
		} else {
			_animBuffers.addState(0, xpos, ypos, dataPtr, pge, w, h);
		}
	} else {
		assert(pge->anim_number < _res._numSpc);
		const uint8 *dataPtr = _res._spc + READ_BE_UINT16(_res._spc + pge->anim_number * 2);
		const int16 xpos = dx + pge->pos_x + 8;
		const int16 ypos = dy + pge->pos_y + 2;
		if (pge->init_PGE->object_type == 11) {
			_animBuffers.addState(3, xpos, ypos, dataPtr, pge);
		} else if (pge->flags & 0x10) {
			_animBuffers.addState(2, xpos, ypos, dataPtr, pge);
		} else {
			_animBuffers.addState(0, xpos, ypos, dataPtr, pge);
		}
	}
}

void Game::drawAnims() {
	debug(DBG_GAME, "Game::drawAnims()");
	_eraseBackground = false;
	drawAnimBuffer(2, _animBuffer2State);
	drawAnimBuffer(1, _animBuffer1State);
	drawAnimBuffer(0, _animBuffer0State);
	_eraseBackground = true;
	drawAnimBuffer(3, _animBuffer3State);
}

void Game::drawAnimBuffer(uint8 stateNum, AnimBufferState *state) {
	debug(DBG_GAME, "Game::drawAnimBuffer() state=%d", stateNum);
	assert(stateNum < 4);
	_animBuffers._states[stateNum] = state;
	uint8 lastPos = _animBuffers._curPos[stateNum];
	if (lastPos != 0xFF) {
		uint8 numAnims = lastPos + 1;
		state += lastPos;
		_animBuffers._curPos[stateNum] = 0xFF;
		do {
			LivePGE *pge = state->pge;
			if (!(pge->flags & 8)) {
				if (stateNum == 1 && (_blinkingConradCounter & 1)) {
					break;
				}
				switch (_res._type) {
				case kResourceTypeAmiga:
					_vid.AMIGA_decodeSpm(state->dataPtr, _res._memBuf);
					drawCharacter(_res._memBuf, state->x, state->y, state->h, state->w, pge->flags);
					break;
				case kResourceTypePC:
					if (!(state->dataPtr[-2] & 0x80)) {
						decodeCharacterFrame(state->dataPtr, _res._memBuf);
						drawCharacter(_res._memBuf, state->x, state->y, state->h, state->w, pge->flags);
					} else {
						drawCharacter(state->dataPtr, state->x, state->y, state->h, state->w, pge->flags);
					}
					break;
				}
			} else {
				drawObject(state->dataPtr, state->x, state->y, pge->flags);
			}
			--state;
		} while (--numAnims != 0);
	}
}

void Game::drawObject(const uint8 *dataPtr, int16 x, int16 y, uint8 flags) {
	debug(DBG_GAME, "Game::drawObject() dataPtr[]=0x%X dx=%d dy=%d",  dataPtr[0], (int8)dataPtr[1], (int8)dataPtr[2]);
	assert(dataPtr[0] < 0x4A);
	uint8 slot = _res._rp[dataPtr[0]];
	uint8 *data = _res.findBankData(slot);
	if (data == 0) {
		data = _res.loadBankData(slot);
	}
	int16 posy = y - (int8)dataPtr[2];
	int16 posx = x;
	if (flags & 2) {
		posx += (int8)dataPtr[1];
	} else {
		posx -= (int8)dataPtr[1];
	}
	int count = 0;
	switch (_res._type) {
	case kResourceTypeAmiga:
		count = dataPtr[8];
		dataPtr += 9;
		break;
	case kResourceTypePC:
		count = dataPtr[5];
		dataPtr += 6;
		break;
	}
	for (int i = 0; i < count; ++i) {
		drawObjectFrame(data, dataPtr, posx, posy, flags);
		dataPtr += 4;
	}
}

void Game::drawObjectFrame(const uint8 *bankDataPtr, const uint8 *dataPtr, int16 x, int16 y, uint8 flags) {
	debug(DBG_GAME, "Game::drawObjectFrame(0x%X, %d, %d, 0x%X)", dataPtr, x, y, flags);
	const uint8 *src = bankDataPtr + dataPtr[0] * 32;

	int16 sprite_y = y + dataPtr[2];
	int16 sprite_x;
	if (flags & 2) {
		sprite_x = x - dataPtr[1] - (((dataPtr[3] & 0xC) + 4) * 2);
	} else {
		sprite_x = x + dataPtr[1];
	}

	uint8 sprite_flags = dataPtr[3];
	if (flags & 2) {
		sprite_flags ^= 0x10;
	}

	uint8 sprite_h = (((sprite_flags >> 0) & 3) + 1) * 8;
	uint8 sprite_w = (((sprite_flags >> 2) & 3) + 1) * 8;

	switch (_res._type) {
	case kResourceTypeAmiga:
		if (sprite_w == 24) {
			// TODO: fix p24xN
			return;
		}
		_vid.AMIGA_decodeSpc(src, sprite_w, sprite_h, _res._memBuf);
		break;
	case kResourceTypePC:
		_vid.PC_decodeSpc(src, sprite_w, sprite_h, _res._memBuf);
		break;
	}

	src = _res._memBuf;
	bool sprite_mirror_x = false;
	int16 sprite_clipped_w;
	if (sprite_x >= 0) {
		sprite_clipped_w = sprite_x + sprite_w;
		if (sprite_clipped_w < 256) {
			sprite_clipped_w = sprite_w;
		} else {
			sprite_clipped_w = 256 - sprite_x;
			if (sprite_flags & 0x10) {
				sprite_mirror_x = true;
				src += sprite_w - 1;
			}
		}
	} else {
		sprite_clipped_w = sprite_x + sprite_w;
		if (!(sprite_flags & 0x10)) {
			src -= sprite_x;
			sprite_x = 0;
		} else {
			sprite_mirror_x = true;
			src += sprite_x + sprite_w - 1;
			sprite_x = 0;
		}
	}
	if (sprite_clipped_w <= 0) {
		return;
	}

	int16 sprite_clipped_h;
	if (sprite_y >= 0) {
		sprite_clipped_h = 224 - sprite_h;
		if (sprite_y < sprite_clipped_h) {
			sprite_clipped_h = sprite_h;
		} else {
			sprite_clipped_h = 224 - sprite_y;
		}
	} else {
		sprite_clipped_h = sprite_h + sprite_y;
		src -= sprite_w * sprite_y;
		sprite_y = 0;
	}
	if (sprite_clipped_h <= 0) {
		return;
	}

	if (!sprite_mirror_x && (sprite_flags & 0x10)) {
		src += sprite_w - 1;
	}

	uint32 dst_offset = 256 * sprite_y + sprite_x;
	uint8 sprite_col_mask = (flags & 0x60) >> 1;

	if (_eraseBackground) {
		if (!(sprite_flags & 0x10)) {
			_vid.drawSpriteSub1(src, _vid._frontLayer + dst_offset, sprite_w, sprite_clipped_h, sprite_clipped_w, sprite_col_mask);
		} else {
			_vid.drawSpriteSub2(src, _vid._frontLayer + dst_offset, sprite_w, sprite_clipped_h, sprite_clipped_w, sprite_col_mask);
		}
	} else {
		if (!(sprite_flags & 0x10)) {
			_vid.drawSpriteSub3(src, _vid._frontLayer + dst_offset, sprite_w, sprite_clipped_h, sprite_clipped_w, sprite_col_mask);
		} else {
			_vid.drawSpriteSub4(src, _vid._frontLayer + dst_offset, sprite_w, sprite_clipped_h, sprite_clipped_w, sprite_col_mask);
		}
	}
	_vid.markBlockAsDirty(sprite_x, sprite_y, sprite_clipped_w, sprite_clipped_h);
}

void Game::decodeCharacterFrame(const uint8 *dataPtr, uint8 *dstPtr) {
	int n = READ_BE_UINT16(dataPtr); dataPtr += 2;
	uint16 len = n * 2;
	uint8 *dst = dstPtr + 0x400;
	while (n--) {
		uint8 c = *dataPtr++;
		dst[0] = (c & 0xF0) >> 4;
		dst[1] = (c & 0x0F) >> 0;
		dst += 2;
	}
	dst = dstPtr;
	const uint8 *src = dstPtr + 0x400;
	do {
		uint8 c1 = *src++;
		if (c1 == 0xF) {
			uint8 c2 = *src++;
			uint16 c3 = *src++;
			if (c2 == 0xF) {
				c1 = *src++;
				c2 = *src++;
				c3 = (c3 << 4) | c1;
				len -= 2;
			}
			memset(dst, c2, c3 + 4);
			dst += c3 + 4;
			len -= 3;
		} else {
			*dst++ = c1;
			--len;
		}
	} while (len != 0);
}

void Game::drawCharacter(const uint8 *dataPtr, int16 pos_x, int16 pos_y, uint8 a, uint8 b, uint8 flags) {
	debug(DBG_GAME, "Game::drawCharacter(0x%X, %d, %d, 0x%X, 0x%X, 0x%X)", dataPtr, pos_x, pos_y, a, b, flags);

	bool var16 = false; // sprite_mirror_y
	if (b & 0x40) {
		b &= 0xBF;
		SWAP(a, b);
		var16 = true;
	}
	uint16 sprite_h = a;
	uint16 sprite_w = b;

	const uint8 *src = dataPtr;
	bool var14 = false;

	int16 sprite_clipped_w;
	if (pos_x >= 0) {
		if (pos_x + sprite_w < 256) {
			sprite_clipped_w = sprite_w;
		} else {
			sprite_clipped_w = 256 - pos_x;
			if (flags & 2) {
				var14 = true;
				if (var16) {
					src += (sprite_w - 1) * sprite_h;
				} else {
					src += sprite_w - 1;
				}
			}
		}
	} else {
		sprite_clipped_w = pos_x + sprite_w;
		if (!(flags & 2)) {
			if (var16) {
				src -= sprite_h * pos_x;
				pos_x = 0;
			} else {
				src -= pos_x;
				pos_x = 0;
			}
		} else {
			var14 = true;
			if (var16) {
				src += sprite_h * (pos_x + sprite_w - 1);
				pos_x = 0;
			} else {
				src += pos_x + sprite_w - 1;
				var14 = true;
				pos_x = 0;
			}
		}
	}
	if (sprite_clipped_w <= 0) {
		return;
	}

	int16 sprite_clipped_h;
	if (pos_y >= 0) {
		if (pos_y < 224 - sprite_h) {
			sprite_clipped_h = sprite_h;
		} else {
			sprite_clipped_h = 224 - pos_y;
		}
	} else {
		sprite_clipped_h = sprite_h + pos_y;
		if (var16) {
			src -= pos_y;
		} else {
			src -= sprite_w * pos_y;
		}
		pos_y = 0;
	}
	if (sprite_clipped_h <= 0) {
		return;
	}

	if (!var14 && (flags & 2)) {
		if (var16) {
			src += sprite_h * (sprite_w - 1);
		} else {
			src += sprite_w - 1;
		}
	}

	uint32 dst_offset = 256 * pos_y + pos_x;
	uint8 sprite_col_mask = ((flags & 0x60) == 0x60) ? 0x50 : 0x40;

	debug(DBG_GAME, "dst_offset=0x%X src_offset=0x%X", dst_offset, src - dataPtr);

	if (!(flags & 2)) {
		if (var16) {
			_vid.drawSpriteSub5(src, _vid._frontLayer + dst_offset, sprite_h, sprite_clipped_h, sprite_clipped_w, sprite_col_mask);
		} else {
			_vid.drawSpriteSub3(src, _vid._frontLayer + dst_offset, sprite_w, sprite_clipped_h, sprite_clipped_w, sprite_col_mask);
		}
	} else {
		if (var16) {
			_vid.drawSpriteSub6(src, _vid._frontLayer + dst_offset, sprite_h, sprite_clipped_h, sprite_clipped_w, sprite_col_mask);
		} else {
			_vid.drawSpriteSub4(src, _vid._frontLayer + dst_offset, sprite_w, sprite_clipped_h, sprite_clipped_w, sprite_col_mask);
		}
	}
	_vid.markBlockAsDirty(pos_x, pos_y, sprite_clipped_w, sprite_clipped_h);
}

int Game::loadMonsterSprites(LivePGE *pge) {
	debug(DBG_GAME, "Game::loadMonsterSprites()");
	InitPGE *init_pge = pge->init_PGE;
	if (init_pge->obj_node_number != 0x49 && init_pge->object_type != 10) {
		return 0xFFFF;
	}
	if (init_pge->obj_node_number == _curMonsterFrame) {
		return 0xFFFF;
	}
	if (pge->room_location != _currentRoom) {
		return 0;
	}

	const uint8 *mList = _monsterListLevels[_currentLevel];
	while (*mList != init_pge->obj_node_number) {
		if (*mList == 0xFF) { // end of list
			return 0;
		}
		mList += 2;
	}
	_curMonsterFrame = mList[0];
	if (_curMonsterNum != mList[1]) {
		_curMonsterNum = mList[1];
		if (_res._type == kResourceTypeAmiga) {
			_res.load(_monsterNames[1][_curMonsterNum], Resource::OT_SPM);
			static const uint8 tab[4] = { 0, 8, 0, 8 };
			const int offset = _vid._mapPalSlot2 * 16 + tab[_curMonsterNum];
			for (int i = 0; i < 8; ++i) {
				_vid.setPaletteColorBE(0x50 + i, offset + i);
			}
		} else {
			const char *name = _monsterNames[0][_curMonsterNum];
			_res.load(name, Resource::OT_SPRM);
			_res.load_SPR_OFF(name, _res._sprm);
			_vid.setPaletteSlotLE(5, _monsterPals[_curMonsterNum]);
		}
	}
	return 0xFFFF;
}

void Game::loadLevelMap() {
	debug(DBG_GAME, "Game::loadLevelMap() room=%d", _currentRoom);
	_currentIcon = 0xFF;
	switch (_res._type) {
	case kResourceTypeAmiga:
		if (_currentLevel == 1) {
			static const uint8 tab[64] = {
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 1, 0,
				0, 0, 0, 1, 0, 0, 0, 0, 2, 0, 0, 2, 0, 0, 0, 0,
				0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, 0, 1, 1, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0
			};
			const int num = tab[_currentRoom];
			if (num != 0 && _res._levNum != num) {
				char name[8];
				snprintf(name, sizeof(name), "level2_%d", num);
				_res.load(name, Resource::OT_LEV);
				_res._levNum = num;
			}
		}
		_vid.AMIGA_decodeLev(_currentLevel, _currentRoom);
		break;
	case kResourceTypePC:
		_vid.PC_decodeMap(_currentLevel, _currentRoom);
		_vid.PC_setLevelPalettes();
		break;
	}
}

void Game::loadLevelData() {
	_res.clearLevelRes();
	const Level *lvl = &_gameLevels[_currentLevel];
	switch (_res._type) {
	case kResourceTypeAmiga:
		{
			const char *name = lvl->nameAmiga;
			if (_currentLevel == 4) {
				name = _gameLevels[3].nameAmiga;
			}
			_res.load(name, Resource::OT_MBK);
			if (_currentLevel == 6) {
				_res.load(_gameLevels[5].nameAmiga, Resource::OT_CT);
			} else {
				_res.load(name, Resource::OT_CT);
			}
			_res.load(name, Resource::OT_PAL);
			_res.load(name, Resource::OT_RPC);
			_res.load(name, Resource::OT_SPC);
			if (_currentLevel == 1) {
				_res.load("level2_1", Resource::OT_LEV);
				_res._levNum = 1;
			} else {
				_res.load(name, Resource::OT_LEV);
			}
		}
		_res.load(lvl->nameAmiga, Resource::OT_PGE);
		_res.load(lvl->nameAmiga, Resource::OT_OBC);
		_res.load(lvl->nameAmiga, Resource::OT_ANI);
		_res.load(lvl->nameAmiga, Resource::OT_TBN);
		{
			char name[8];
			snprintf(name, sizeof(name), "level%d", lvl->spl);
			_res.load(name, Resource::OT_SPL);
		}
		if (_currentLevel == 0) {
			_res.load(lvl->nameAmiga, Resource::OT_SGD);
		}
		break;
	case kResourceTypePC:
		_res.load(lvl->name, Resource::OT_MBK);
		_res.load(lvl->name, Resource::OT_CT);
		_res.load(lvl->name, Resource::OT_PAL);
		_res.load(lvl->name, Resource::OT_RP);
		_res.load(lvl->name, Resource::OT_MAP);
		_res.load(lvl->name2, Resource::OT_PGE);
		_res.load(lvl->name2, Resource::OT_OBJ);
		_res.load(lvl->name2, Resource::OT_ANI);
		_res.load(lvl->name2, Resource::OT_TBN);
		break;
	}

	_cut._id = lvl->cutscene_id;

	_curMonsterNum = 0xFFFF;
	_curMonsterFrame = 0;

	_res.clearBankData();
	_printLevelCodeCounter = 150;

	_col_slots2Cur = _col_slots2;
	_col_slots2Next = 0;

	memset(_pge_liveTable2, 0, sizeof(_pge_liveTable2));
	memset(_pge_liveTable1, 0, sizeof(_pge_liveTable1));

	_currentRoom = _res._pgeInit[0].init_room;
	uint16 n = _res._pgeNum;
	while (n--) {
		pge_loadForCurrentLevel(n);
	}

	for (uint16 i = 0; i < _res._pgeNum; ++i) {
		if (_res._pgeInit[i].skill <= _skillLevel) {
			LivePGE *pge = &_pgeLive[i];
			pge->next_PGE_in_room = _pge_liveTable1[pge->room_location];
			_pge_liveTable1[pge->room_location] = pge;
		}
	}
	pge_resetGroups();
	_validSaveState = false;
}

void Game::drawIcon(uint8 iconNum, int16 x, int16 y, uint8 colMask) {
	uint8 buf[16 * 16];
	switch (_res._type) {
	case kResourceTypeAmiga:
		if (iconNum > 30) {
			// inventory icons
			switch (iconNum) {
			case 76: // cursor
				memset(buf, 0, 16 * 16);
				for (int i = 0; i < 3; ++i) {
					buf[i] = buf[15 * 16 + (15 - i)] = 1;
					buf[i * 16] = buf[(15 - i) * 16 + 15] = 1;
				}
				break;
			case 77: // up - icon.spr 4
				memset(buf, 0, 16 * 16);
				_vid.AMIGA_decodeIcn(_res._icn, 35, buf);
				break;
			case 78: // down - icon.spr 5
				memset(buf, 0, 16 * 16);
				_vid.AMIGA_decodeIcn(_res._icn, 36, buf);
				break;
			default:
				memset(buf, 5, 16 * 16);
				break;
			}
		} else {
			_vid.AMIGA_decodeIcn(_res._icn, iconNum, buf);
		}
		break;
	case kResourceTypePC:
		_vid.PC_decodeIcn(_res._icn, iconNum, buf);
		break;
	}
	_vid.drawSpriteSub1(buf, _vid._frontLayer + x + y * 256, 16, 16, 16, colMask << 4);
	_vid.markBlockAsDirty(x, y, 16, 16);
}

void Game::playSound(uint8 sfxId, uint8 softVol) {
	if (sfxId < _res._numSfx) {
		SoundFx *sfx = &_res._sfxList[sfxId];
		if (sfx->data) {
			MixerChunk mc;
			mc.data = sfx->data;
			mc.len = sfx->len;
			const int freq = _res._type == kResourceTypeAmiga ? 3546897 / 650 : 6000;
			_mix.play(&mc, freq, Mixer::MAX_VOLUME >> softVol);
		}
	} else {
		// in-game music
		_sfxPly.play(sfxId);
	}
}

uint16 Game::getRandomNumber() {
	uint32 n = _randSeed * 2;
	if (_randSeed > n) {
		n ^= 0x1D872B41;
	}
	_randSeed = n;
	return n & 0xFFFF;
}

void Game::changeLevel() {
	_vid.fadeOut();
	loadLevelData();
	loadLevelMap();
	_vid.setPalette0xF();
	_vid.setTextPalette();
	_vid.fullRefresh();
}

uint16 Game::getLineLength(const uint8 *str) const {
	uint16 len = 0;
	while (*str && *str != 0xB && *str != 0xA) {
		++str;
		++len;
	}
	return len;
}

void Game::handleInventory() {
	LivePGE *selected_pge = 0;
	LivePGE *pge = &_pgeLive[0];
	if (pge->life > 0 && pge->current_inventory_PGE != 0xFF) {
		playSound(66, 0);
		InventoryItem items[24];
		int num_items = 0;
		uint8 inv_pge = pge->current_inventory_PGE;
		while (inv_pge != 0xFF) {
			items[num_items].icon_num = _res._pgeInit[inv_pge].icon_num;
			items[num_items].init_pge = &_res._pgeInit[inv_pge];
			items[num_items].live_pge = &_pgeLive[inv_pge];
			inv_pge = _pgeLive[inv_pge].next_inventory_PGE;
			++num_items;
		}
		items[num_items].icon_num = 0xFF;
		int current_item = 0;
		int num_lines = (num_items - 1) / 4 + 1;
		int current_line = 0;
		bool display_score = false;
		while (!_stub->_pi.backspace && !_stub->_pi.quit) {
			// draw inventory background
			int icon_h = 5;
			int icon_y = 140;
			int icon_num = 31;
			do {
				int icon_x = 56;
				int icon_w = 9;
				do {
					drawIcon(icon_num, icon_x, icon_y, 0xF);
					++icon_num;
					icon_x += 16;
				} while (--icon_w);
				icon_y += 16;
			} while (--icon_h);

			if (!display_score) {
				int icon_x_pos = 72;
				for (int i = 0; i < 4; ++i) {
					int item_it = current_line * 4 + i;
					if (items[item_it].icon_num == 0xFF) {
						break;
					}
					drawIcon(items[item_it].icon_num, icon_x_pos, 157, 0xA);
					if (current_item == item_it) {
						drawIcon(76, icon_x_pos, 157, 0xA);
						selected_pge = items[item_it].live_pge;
						uint8 txt_num = items[item_it].init_pge->text_num;
						const char *str = (const char *)_res._tbn + READ_LE_UINT16(_res._tbn + txt_num * 2);
						_vid.drawString(str, (256 - strlen(str) * 8) / 2, 189, 0xED);
						if (items[item_it].init_pge->init_flags & 4) {
							char buf[10];
							snprintf(buf, sizeof(buf), "%d", selected_pge->life);
							_vid.drawString(buf, (256 - strlen(buf) * 8) / 2, 197, 0xED);
						}
					}
					icon_x_pos += 32;
				}
				if (current_line != 0) {
					drawIcon(78, 120, 176, 0xA); // down arrow
				}
				if (current_line != num_lines - 1) {
					drawIcon(77, 120, 143, 0xA); // up arrow
				}
			} else {
				char buf[50];
				snprintf(buf, sizeof(buf), "SCORE %08u", _score);
				_vid.drawString(buf, (114 - strlen(buf) * 8) / 2 + 72, 158, 0xE5);
				snprintf(buf, sizeof(buf), "%s:%s", _res.getMenuString(LocaleData::LI_06_LEVEL), _res.getMenuString(LocaleData::LI_13_EASY + _skillLevel));
				_vid.drawString(buf, (114 - strlen(buf) * 8) / 2 + 72, 166, 0xE5);
			}

			_vid.updateScreen();
			_stub->sleep(80);
			inp_update();

			if (_stub->_pi.dirMask & PlayerInput::DIR_UP) {
				_stub->_pi.dirMask &= ~PlayerInput::DIR_UP;
				if (current_line < num_lines - 1) {
					++current_line;
					current_item = current_line * 4;
				}
			}
			if (_stub->_pi.dirMask & PlayerInput::DIR_DOWN) {
				_stub->_pi.dirMask &= ~PlayerInput::DIR_DOWN;
				if (current_line > 0) {
					--current_line;
					current_item = current_line * 4;
				}
			}
			if (_stub->_pi.dirMask & PlayerInput::DIR_LEFT) {
				_stub->_pi.dirMask &= ~PlayerInput::DIR_LEFT;
				if (current_item > 0) {
					int item_num = current_item % 4;
					if (item_num > 0) {
						--current_item;
					}
				}
			}
			if (_stub->_pi.dirMask & PlayerInput::DIR_RIGHT) {
				_stub->_pi.dirMask &= ~PlayerInput::DIR_RIGHT;
				if (current_item < num_items - 1) {
					int item_num = current_item % 4;
					if (item_num < 3) {
						++current_item;
					}
				}
			}
			if (_stub->_pi.enter) {
				_stub->_pi.enter = false;
				display_score = !display_score;
			}
		}
		_vid.fullRefresh();
		_stub->_pi.backspace = false;
		if (selected_pge) {
			pge_setCurrentInventoryObject(selected_pge);
		}
		playSound(66, 0);
	}
}

void Game::inp_update() {
	if (_inp_replay && _inp_demo) {
		uint8 keymask = _inp_demo->readByte();
		if (_inp_demo->ioErr()) {
			_inp_replay = false;
		} else {
			_stub->_pi.dirMask = keymask & 0xF;
			_stub->_pi.enter = (keymask & 0x10) != 0;
			_stub->_pi.space = (keymask & 0x20) != 0;
			_stub->_pi.shift = (keymask & 0x40) != 0;
			_stub->_pi.quit = (keymask & 0x80) != 0;
		}
	}
	_stub->processEvents();
	if (_inp_record && _inp_demo) {
		uint8 keymask = _stub->_pi.dirMask;
		if (_stub->_pi.enter) {
			keymask |= 0x10;
		}
		if (_stub->_pi.space) {
			keymask |= 0x20;
		}
		if (_stub->_pi.shift) {
			keymask |= 0x40;
		}
		if (_stub->_pi.quit) {
			keymask |= 0x80;
		}
		_inp_demo->writeByte(keymask);
		if (_inp_demo->ioErr()) {
			_inp_record = false;
		}
	}
}

void Game::makeGameDemoName(char *buf) {
	sprintf(buf, "rs-level%d.demo", _currentLevel + 1);
}

void Game::makeGameStateName(uint8 slot, char *buf) {
	sprintf(buf, "rs-level%d-%02d.state", _currentLevel + 1, slot);
}

bool Game::saveGameState(uint8 slot) {
	bool success = false;
	char stateFile[20];
	makeGameStateName(slot, stateFile);
	File f;
	if (!f.open(stateFile, "zwb", _savePath)) {
		warning("Unable to save state file '%s'", stateFile);
	} else {
		// header
		f.writeUint32BE('FBSV');
		f.writeUint16BE(2);
		char hdrdesc[32];
		memset(hdrdesc, 0, sizeof(hdrdesc));
		sprintf(hdrdesc, "level=%d room=%d", _currentLevel + 1, _currentRoom);
		f.write(hdrdesc, sizeof(hdrdesc));
		// contents
		saveState(&f);
		if (f.ioErr()) {
			warning("I/O error when saving game state");
		} else {
			debug(DBG_INFO, "Saved state to slot %d", slot);
			success = true;
		}
	}
	return success;
}

bool Game::loadGameState(uint8 slot) {
	bool success = false;
	char stateFile[20];
	makeGameStateName(slot, stateFile);
	File f;
	if (!f.open(stateFile, "zrb", _savePath)) {
		warning("Unable to open state file '%s'", stateFile);
	} else {
		uint32 id = f.readUint32BE();
		if (id != 'FBSV') {
			warning("Bad save state format");
		} else {
			uint16 ver = f.readUint16BE();
			if (ver != 2) {
				warning("Invalid save state version");
			} else {
				char hdrdesc[32];
				f.read(hdrdesc, sizeof(hdrdesc));
				// contents
				loadState(&f);
				if (f.ioErr()) {
					warning("I/O error when loading game state");
				} else {
					debug(DBG_INFO, "Loaded state from slot %d", slot);
					success = true;
				}
			}
		}
	}
	return success;
}

void Game::saveState(File *f) {
	f->writeByte(_skillLevel);
	f->writeUint32BE(_score);
	if (_col_slots2Cur == 0) {
		f->writeUint32BE(0xFFFFFFFF);
	} else {
		f->writeUint32BE(_col_slots2Cur - &_col_slots2[0]);
	}
	if (_col_slots2Next == 0) {
		f->writeUint32BE(0xFFFFFFFF);
	} else {
		f->writeUint32BE(_col_slots2Next - &_col_slots2[0]);
	}
	for (int i = 0; i < _res._pgeNum; ++i) {
		LivePGE *pge = &_pgeLive[i];
		f->writeUint16BE(pge->obj_type);
		f->writeUint16BE(pge->pos_x);
		f->writeUint16BE(pge->pos_y);
		f->writeByte(pge->anim_seq);
		f->writeByte(pge->room_location);
		f->writeUint16BE(pge->life);
		f->writeUint16BE(pge->counter_value);
		f->writeByte(pge->collision_slot);
		f->writeByte(pge->next_inventory_PGE);
		f->writeByte(pge->current_inventory_PGE);
		f->writeByte(pge->unkF);
		f->writeUint16BE(pge->anim_number);
		f->writeByte(pge->flags);
		f->writeByte(pge->index);
		f->writeUint16BE(pge->first_obj_number);
		if (pge->next_PGE_in_room == 0) {
			f->writeUint32BE(0xFFFFFFFF);
		} else {
			f->writeUint32BE(pge->next_PGE_in_room - &_pgeLive[0]);
		}
		if (pge->init_PGE == 0) {
			f->writeUint32BE(0xFFFFFFFF);
		} else {
			f->writeUint32BE(pge->init_PGE - &_res._pgeInit[0]);
		}
	}
	f->write(&_res._ctData[0x100], 0x1C00);
	for (CollisionSlot2 *cs2 = &_col_slots2[0]; cs2 < _col_slots2Cur; ++cs2) {
		if (cs2->next_slot == 0) {
			f->writeUint32BE(0xFFFFFFFF);
		} else {
			f->writeUint32BE(cs2->next_slot - &_col_slots2[0]);
		}
		if (cs2->unk2 == 0) {
			f->writeUint32BE(0xFFFFFFFF);
		} else {
			f->writeUint32BE(cs2->unk2 - &_res._ctData[0x100]);
		}
		f->writeByte(cs2->data_size);
		f->write(cs2->data_buf, 0x10);
	}
}

void Game::loadState(File *f) {
	uint16 i;
	uint32 off;
	_skillLevel = f->readByte();
	_score = f->readUint32BE();
	memset(_pge_liveTable2, 0, sizeof(_pge_liveTable2));
	memset(_pge_liveTable1, 0, sizeof(_pge_liveTable1));
	off = f->readUint32BE();
	if (off == 0xFFFFFFFF) {
		_col_slots2Cur = 0;
	} else {
		_col_slots2Cur = &_col_slots2[0] + off;
	}
	off = f->readUint32BE();
	if (off == 0xFFFFFFFF) {
		_col_slots2Next = 0;
	} else {
		_col_slots2Next = &_col_slots2[0] + off;
	}
	for (i = 0; i < _res._pgeNum; ++i) {
		LivePGE *pge = &_pgeLive[i];
		pge->obj_type = f->readUint16BE();
		pge->pos_x = f->readUint16BE();
		pge->pos_y = f->readUint16BE();
		pge->anim_seq = f->readByte();
		pge->room_location = f->readByte();
		pge->life = f->readUint16BE();
		pge->counter_value = f->readUint16BE();
		pge->collision_slot = f->readByte();
		pge->next_inventory_PGE = f->readByte();
		pge->current_inventory_PGE = f->readByte();
		pge->unkF = f->readByte();
		pge->anim_number = f->readUint16BE();
		pge->flags = f->readByte();
		pge->index = f->readByte();
		pge->first_obj_number = f->readUint16BE();
		off = f->readUint32BE();
		if (off == 0xFFFFFFFF) {
			pge->next_PGE_in_room = 0;
		} else {
			pge->next_PGE_in_room = &_pgeLive[0] + off;
		}
		off = f->readUint32BE();
		if (off == 0xFFFFFFFF) {
			pge->init_PGE = 0;
		} else {
			pge->init_PGE = &_res._pgeInit[0] + off;
		}
	}
	f->read(&_res._ctData[0x100], 0x1C00);
	for (CollisionSlot2 *cs2 = &_col_slots2[0]; cs2 < _col_slots2Cur; ++cs2) {
		off = f->readUint32BE();
		if (off == 0xFFFFFFFF) {
			cs2->next_slot = 0;
		} else {
			cs2->next_slot = &_col_slots2[0] + off;
		}
		off = f->readUint32BE();
		if (off == 0xFFFFFFFF) {
			cs2->unk2 = 0;
		} else {
			cs2->unk2 = &_res._ctData[0x100] + off;
		}
		cs2->data_size = f->readByte();
		f->read(cs2->data_buf, 0x10);
	}
	for (i = 0; i < _res._pgeNum; ++i) {
		if (_res._pgeInit[i].skill <= _skillLevel) {
			LivePGE *pge = &_pgeLive[i];
			if (pge->flags & 4) {
				_pge_liveTable2[pge->index] = pge;
			}
			pge->next_PGE_in_room = _pge_liveTable1[pge->room_location];
			_pge_liveTable1[pge->room_location] = pge;
		}
	}
	resetGameState();
}

void AnimBuffers::addState(uint8 stateNum, int16 x, int16 y, const uint8 *dataPtr, LivePGE *pge, uint8 w, uint8 h) {
	debug(DBG_GAME, "AnimBuffers::addState() stateNum=%d x=%d y=%d dataPtr=0x%X pge=0x%X", stateNum, x, y, dataPtr, pge);
	assert(stateNum < 4);
	AnimBufferState *state = _states[stateNum];
	state->x = x;
	state->y = y;
	state->w = w;
	state->h = h;
	state->dataPtr = dataPtr;
	state->pge = pge;
	++_curPos[stateNum];
	++_states[stateNum];
}
