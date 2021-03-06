/* lib includes */
#include <avr/io.h>
#include <stdlib.h>
#include <math.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
//#include <uzebox.h>
#include "kernel/uzebox.h"


/* data includes */

#include "res/tiles.inc"
#include "res/fontmap.inc"
#include "res/sprites.inc"

/* structs */
struct GridBufferSquare {
    unsigned char unit; // index to Unit array; 0xff for no unit
    unsigned char info; // terrain and player bitfield, also hasproduced
    // pp.xx.a.ttt pp=player, a=has produced, ttt=terrain
};
struct Unit {
    char isUnit;
    char info; // unit type and player bitfield
    char hp;
    char other; // xxxxxx.ba, a=moved on turn, b=attacked on turn
    unsigned char xPos;
    unsigned char yPos;
};
struct Movement {
	char direction;
	char movePoints;
};

/* defines */
#ifndef OFF_SCREEN 
#define OFF_SCREEN 28*8 
#endif

#define LEVEL_HEIGHT 11
#define MAX_UNITS 40
#define MAX_PROPERTIES 20
#define MAX_LEVEL_WIDTH 30
#define MAX_VIS_WIDTH 14
#define TRUE 1
#define FALSE 0

#define EEPROM_INDEX 833

#define BLINK_UNITS 0
#define BLINK_TERRAIN 1

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define ABS(a) ((a) < 0 ? -(a) : (a))
#define MANH(x1, y1, x2, y2) (ABS((x1)-(x2)) + ABS((y1)-(y2)))

#define ERROR(msg) \
	{Print(4,4,PSTR(msg));\
	while(1)\
		WaitVsync(1);}

// convert our player value to controller value
#define JPPLAY(pl) ((pl) == PL2 ? 1 : 0)

// level data masks
#define TERRAIN_MASK 0b00000111
#define UNIT_MASK	 0b00111000
#define OWNER_MASK	 0b11000000

// produced
#define HASPROD_MASK 0b00001000

#define HASPROD(x) ((x)&HASPROD_MASK)
#define SETHASPROD(x, y, v) levelBuffer[x][y].info = (levelBuffer[x][y].info&0xF7)|((v)<<3)

//unit stats masks
#define HASMOVED_MASK 0b00000001
#define HASATTACKED_MASK 0b00000010

#define HASMOVED(x) ((x)&HASMOVED_MASK)
#define HASATTACKED(x) ((x)&HASATTACKED_MASK)
// x is an index in the unit list
#define SETHASMOVED(x, y)	unitList[x].other = (unitList[x].other&0xFE)|((y))
#define SETHASATTACKED(x, y)	unitList[x].other = (unitList[x].other&0xFD)|((y)<<1)

#define MAX_UNIT_MP 10

// terrain types
#define PL	0x01 // plain
#define MO	0x02 // mountain
#define FO	0x03 // forest
#define CT	0x04 // city
#define BS	0x05 // base
#define NO_TERRAIN 0xFF //no terrain (when would there ever be no terrain?)

#define GETTERR(x) ((x)&TERRAIN_MASK)
#define INDEXTERR(x) (x)

// unit types
#define UN1 0x08
#define UN2 0x10
#define UN3 0x18
#define UN4 0x20
#define UN5 0x28
#define NO_UNIT 0xFF

#define GETUNIT(x) ((x)&UNIT_MASK)
#define INDEXUNIT(x) ((x) >> 3)

// players
#define PL1	0x80
#define PL2	0x40
#define NEU	0x00

#define GETPLAY(x) ((x)&OWNER_MASK)
#define INDEXPLAY(x) (((x) >> 6))

// map load directions
#define LOAD_ALL	0x01
#define LOAD_LEFT   0x04
#define LOAD_RIGHT  0x06

// cursor/movement directions
#define DIR_LEFT    0x04
#define DIR_RIGHT   0x06
#define DIR_UP      0x08
#define DIR_DOWN    0x0A

//overlay lines
#define OVR1 (VRAM_TILES_V-4)
#define OVR2 (VRAM_TILES_V-3)
#define OVR3 (VRAM_TILES_V-2)
#define OVR4 (VRAM_TILES_V-1)

//interface tiles (this might change if the tile map changes drastically, fuck gconvert
#define INTERFACE_TL 23
#define INTERFACE_TOP 24
#define INTERFACE_TR 25
#define INTERFACE_LEFT 26
#define INTERFACE_MID 27
#define INTERFACE_RIGHT 28
#define INTERFACE_BL 35
#define INTERFACE_BOT 36
#define INTERFACE_BR 37
#define INTERFACE_ARROW 42
#define INTERFACE_ARROW_TOP 53
#define INTERFACE_ARROW_BOT 52
#define INTERFACE_GLIGHT 55
#define INTERFACE_RLIGHT 54
#define INTERFACE_DOLLAR 56

//sprite indices
#define SPRITE_CURSOR 0
#define SPRITE_ARROW 4
#define SPRITE_EXPLOSION 5

#define SPRITE_POS_EXPL1 4
#define SPRITE_POS_EXPL2 5
//#define SPRITE_MOVINGUNIT 5


/* globals */
unsigned char levelWidth, levelHeight;
unsigned char cursorX, cursorY; // absolute coords
unsigned char cameraX;
unsigned char vramX; // where cameraX points to in vram coords, wrapped on 0x1F

unsigned char selectionVar = 0; // generic selection variable

struct EepromBlockStruct eepromData;

char blinkCounter = 0;
char blinkState = BLINK_UNITS;
char blinkMode = FALSE;

char cursorCounter = 0; //for cursor alternation
char cursorAlt = FALSE;

int curInput;
int prevInput;
unsigned char activePlayer;

unsigned char credits[] = {0, 0};

const char* currentLevel;

enum
{
	scrolling, unit_menu, unit_movement, unit_moving, unit_attack, end_turn, pause, menu
}	controlState;

// what is visible on the screen; 14 wide, 11 high, 2 loading columns on each side
struct GridBufferSquare levelBuffer[MAX_LEVEL_WIDTH][LEVEL_HEIGHT];

unsigned char unitFirstEmpty = 0;
unsigned char unitListEnd = 0;
signed char lastJumpedUnit = -1;

struct Unit unitList[MAX_UNITS]; //is this enough?

struct Movement movementBuffer[10]; // ought to be enough
uint8_t movementCount = 0;
uint8_t  movementPoints = 0;
unsigned char movingUnit = 0;
unsigned char arrowX = 0, arrowY = 0;

unsigned char attackingUnit = 0;
unsigned char attackedUnit = 0;

/* declarations */
// param1, param2, param3; return
void initialize();
void loadLevel(const char*); // level
void drawLevel(char); // direction
void drawHPBar(unsigned char, unsigned char, char); // x, y, value
void drawDefenseBar(unsigned char, unsigned char, char); //same as hp bar
void drawOverlay();
void drawArrow();
unsigned char addUnit(unsigned char, unsigned char, char, char); // x, y, player, type; unitIndex
void removeUnitByIndex(unsigned char); // index
void removeUnit(unsigned char, unsigned char); // x, y
void moveUnit();
char moveCamera(char); // direction
char moveCameraInstant(char); // x
char moveCursor(char); // direction
char moveCursorInstant(unsigned char, unsigned char); // x, y
char validArrowTile(unsigned char, unsigned char); // x, y, hasArrow
const char* getTileMap(unsigned char, unsigned char); // x, y; tileMap
void waitGameInput();
void mapCursorSprite(char); // alternate
void mapMovingUnitSprite();
void tweenUnitSprite(char, char, char, char);
void redrawUnits();
void setBlinkMode(char); // on-off
const char* getUnitName(unsigned char); // unit; unitName
char getNeededMovePoints(const char unit, const char terrain);
char getAttackRange(const char unit);
char getDamage(struct Unit* srcUnit, struct Unit* dstUnit);
char getRandomNumber(); // ; rand
char getRandomNumberLimit(char); // max; rand
unsigned char getNextAttackableUnitIndex(signed char last, char dir);
void saveEeprom();

void WaitVsync_(char);


/*const char testlevel[] PROGMEM =
{
    16,
    PL, MO, FO, PL, MO, FO, PL, MO, FO, PL, MO, FO, PL, MO, FO, PL,
    CT|NEU, BS|NEU, CT|PL1, BS|PL1, CT|PL2, BS|PL2, CT|NEU, BS|NEU, CT|PL1, BS|PL1, CT|PL2, BS|PL2, CT|NEU, BS|NEU, PL, FO,
    PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, FO, MO,
    PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, FO, MO,
    PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, FO, MO,
    PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, FO, MO,
    PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, FO, MO,
    PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, FO, MO,
    PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, FO, MO,
    PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, FO, MO,
    PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, FO, MO
};*/

//const char testlevel[] PROGMEM = {};

const char testlevel[] PROGMEM =
{
	16, 11,
	PL, FO, PL, PL, PL, PL, PL, PL, PL, MO, MO, PL, MO, PL, PL, MO,
	PL, PL, MO, MO, MO, MO, BS, PL, PL, CT, MO, FO, PL, PL|UN1|PL2, BS|PL2, PL,
	PL, BS|PL1, PL, MO, PL, PL, FO, FO, PL, FO, FO, PL, PL, MO, MO, PL,
	PL|UN3|PL2, FO|UN1|PL1, PL|UN5|PL2, MO, PL, PL, PL, FO, PL, PL, FO, PL, FO, PL, PL, PL,
	PL, PL|UN2|PL2, MO, MO, PL, MO, MO, FO, PL, PL, PL, FO, PL, FO, PL, PL,
	PL, FO, PL|UN4|PL1, MO, PL, MO, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL,
	PL, PL, PL, PL, MO, PL, PL, PL, CT, MO, MO, PL, PL, FO, PL, PL,
	PL, FO, PL, PL, PL|UN2|PL2, PL|UN4|PL1, PL, FO, FO, PL, MO, FO, FO, FO, PL|UN4|PL2, PL|UN2|PL2,
	PL, PL, CT, PL, PL, FO, FO, PL, PL, PL, MO, FO, PL, PL, PL, PL,
	FO, FO|UN2|PL1, FO, PL, PL, PL, FO, FO, PL, MO, PL, PL, FO, CT|UN1|PL2, PL, PL|UN3|PL1,
	PL, MO, MO, FO|UN1|PL2, MO, PL, PL, PL, FO, BS, PL, MO, MO, MO, MO, PL
};

const char shortlevel[] PROGMEM =
{
	5, 4,
	PL, PL, PL, PL, CT,
	BS|PL1, MO, MO, FO, MO|UN1|PL1,
	FO, MO, MO, BS|PL2, CT|UN3|PL2,
	PL, PL, PL, PL, CT
};

/* main function */
int main() {
	initialize();
	loadLevel(testlevel);
	FadeOut(0, true);
	drawLevel(LOAD_ALL);
	drawOverlay();
	mapCursorSprite(FALSE);
	MoveSprite(0,0,0,2,2);
	FadeIn(5, true);

	activePlayer = PL1;
	credits[0] = 10;
	credits[1] = 10;

	waitGameInput();

	return 0;
}


void initialize() {
	Screen.scrollHeight = 28;
	Screen.overlayHeight = 4;
	Screen.overlayTileTable = terrainTiles; // seems like it has to share the tiles, otherwise we can't use the fonts
	ClearVram();
	SetFontTilesIndex(TERRAINTILES_SIZE);
	SetTileTable(terrainTiles);
	SetSpritesTileTable(spriteTiles);

	eepromData.id = EEPROM_INDEX;
	if(!isEepromFormatted() || EepromReadBlock(EEPROM_INDEX, &eepromData)) {
		// no idea what to do here...
	}
}

void jumpToNextUnit() {
	for(unsigned char i = lastJumpedUnit+1; i != lastJumpedUnit; i = (i+1)%MAX_UNITS) {
		//TODO: make this only jump to not moved or attacked units
		//should this be !(hasmoved || hasattacked)?
		if(unitList[i].isUnit && GETPLAY(unitList[i].info) == activePlayer && !(HASMOVED(unitList[i].other) && HASATTACKED(unitList[i].other))) {
			if((unitList[i].xPos < cameraX) || (unitList[i].xPos > (cameraX + MAX_VIS_WIDTH))) {
				signed char tempX = unitList[i].xPos - MAX_VIS_WIDTH/2;
				if(tempX < 0) {
					tempX = 0;
				}
				else if(tempX > (levelWidth - MAX_VIS_WIDTH)) {
					tempX = levelWidth - MAX_VIS_WIDTH;
				}
				moveCameraInstant(unitList[i].xPos - MAX_VIS_WIDTH/2);
			}
			moveCursorInstant(unitList[i].xPos, unitList[i].yPos);
			lastJumpedUnit = i;
			break;
		}
	}
}
void drawTwoSelMenu(const char* prompt, const char* sel1, const char* sel2) {

	Fill((vramX+5)%0x1F,	 5, 12, 5, INTERFACE_MID);
	SetTile((vramX+5)%0x1F,	 5, INTERFACE_TL);
	Fill((vramX+5)%0x1F, 6, 1, 3, INTERFACE_LEFT);
	SetTile((vramX+5)%0x1F, 9, INTERFACE_BL);
	Fill((vramX+5+1)%0x1F, 9, 10, 1, INTERFACE_BOT);
	SetTile((vramX+5+11)%0x1F, 9, INTERFACE_BR);
	Fill((vramX+5+11)%0x1F, 6, 1, 3, INTERFACE_RIGHT);
	SetTile((vramX+5+11)%0x1F, 5, INTERFACE_TR);
	Fill((vramX+5+1)%0x1F, 5, 10, 1, INTERFACE_TOP);

	Print((vramX+5+1)%0x1F, 6, prompt);

	Print((vramX+5+4)%0x1F, 7, sel1);
	Print((vramX+5+4)%0x1F, 8, sel2);

	SetTile((vramX+5+3)%0x1F, 7+selectionVar, INTERFACE_ARROW);

}

void attackUnit() {
	MoveSprite(0, OFF_SCREEN, 0, 2, 2);
//	PrintHexByte(11, OVR2, sprites[SPRITE_POS_EXPL1].y);
//	while(1);
	sprites[SPRITE_POS_EXPL1].tileIndex = SPRITE_EXPLOSION; // this doesn't need to be set every call
	sprites[SPRITE_POS_EXPL1].x = OFF_SCREEN;
	sprites[SPRITE_POS_EXPL1].y = 0;

	sprites[SPRITE_POS_EXPL2].tileIndex = SPRITE_EXPLOSION; // this doesn't need to be set every call
	sprites[SPRITE_POS_EXPL2].flags = SPRITE_FLIP_X;
	sprites[SPRITE_POS_EXPL2].x = OFF_SCREEN;
	sprites[SPRITE_POS_EXPL2].y = 0;

	char cycles = 0;
	char ex1_start = (getRandomNumberLimit(4) + 1) * 3; // random half-cycle between 1 and 5
	char ex2_start = ex1_start + (getRandomNumberLimit(7) + 3) * 3;
	char max_cycles = ex2_start + 20 * 3;

	int8_t damage = getDamage(&unitList[attackingUnit], &unitList[attackedUnit]);

	while(cycles < max_cycles) {
		if(cycles == ex1_start) {
			sprites[SPRITE_POS_EXPL1].x = (cursorX-cameraX)*16 + getRandomNumberLimit(10);
			sprites[SPRITE_POS_EXPL1].y = cursorY*16 + getRandomNumberLimit(2) + 1;
		}
		if(cycles == ex2_start) {
			sprites[SPRITE_POS_EXPL2].x = (cursorX-cameraX)*16 + getRandomNumberLimit(10) + 1;
			sprites[SPRITE_POS_EXPL2].y = cursorY*16 + getRandomNumberLimit(3) + 8;
		}
		if(cycles > ex1_start && cycles < ex1_start+60) {
			sprites[SPRITE_POS_EXPL1].tileIndex = SPRITE_EXPLOSION+(cycles-ex1_start)/6;
		}
		if(cycles > ex2_start && cycles < ex2_start+60) {
			sprites[SPRITE_POS_EXPL2].tileIndex = SPRITE_EXPLOSION+(cycles-ex2_start)/6;
		}

		if(damage != 0 && unitList[attackedUnit].hp != 0){
		    unitList[attackedUnit].hp -= 1;
		    damage--;

		    drawOverlay();
		}

		WaitVsync_(3);
		cycles += 3;
	}

	// hide sprites
	sprites[SPRITE_POS_EXPL1].x = OFF_SCREEN;
	sprites[SPRITE_POS_EXPL1].y = 0;
	sprites[SPRITE_POS_EXPL2].x = OFF_SCREEN;
	sprites[SPRITE_POS_EXPL2].y = 0;

	// animate hp bar

	cycles = 0;

	while(damage != 0 && unitList[attackedUnit].hp != 0) {
		unitList[attackedUnit].hp -= 1;
		damage--;

		drawOverlay();

		WaitVsync_(3);
		//cycles += 3;
	}

	WaitVsync_(10);

	if(unitList[attackedUnit].hp <= 0) {
		// less than just to be sure
		removeUnitByIndex(attackedUnit);
		drawLevel(LOAD_ALL);
		drawOverlay();
	}
}


void endTurn() {
	unsigned char i, x, y, terr;
	setBlinkMode(FALSE);
	drawLevel(LOAD_ALL);
	activePlayer = (activePlayer == PL1) ? PL2 : PL1;

	for(i = 0; i < MAX_UNITS; i++) {
		if(unitList[i].isUnit && GETPLAY(unitList[i].info) == activePlayer) {
			// reset markers on units
			SETHASMOVED(i, FALSE);
			SETHASATTACKED(i, FALSE);

			x = unitList[i].xPos;
			y = unitList[i].yPos;
			terr = GETTERR(levelBuffer[x][y].info);

			// heal units on bases&cities
			if(GETPLAY(levelBuffer[x][y].info) == activePlayer && (terr == CT || terr == BS)) {
				unitList[i].hp += 20;
				if(unitList[i].hp > 100)
					unitList[i].hp = 100;
			}
			// convert bases/cities
			else if(terr == CT || terr == BS) {
				levelBuffer[x][y].info = terr|activePlayer;
			}
		}
	}
	// money 'n shit
	// 4 per owned, 4 by default
	credits[(activePlayer == PL1) ? 0 : 1] += 4;
	for(x=0; x < levelWidth; x++) {
		for(y=0; y < levelHeight; y++) {
			terr = GETTERR(levelBuffer[x][y].info);
			if(GETPLAY(levelBuffer[x][y].info) == activePlayer && (terr == CT || terr == BS)) {
				SETHASPROD(x, y, FALSE);
				credits[activePlayer == PL1 ? 0 : 1] += 4;
			}
		}
	}

	if(credits[activePlayer == PL1 ? 0 : 1] > 200)
		credits[(activePlayer == PL1) ? 0 : 1] = 200;

}


void waitGameInput() {
	curInput = prevInput = ReadJoypad(JPPLAY(activePlayer));
	//char asd = 0; //unused 
	//char tmpUnit = 0; //unused
	while(1) {
		curInput = ReadJoypad(JPPLAY(activePlayer));

		drawOverlay();

		switch(controlState) //scrolling, unit_menu, unit_movement, pause, menu
		{
			case scrolling:
				if(curInput&BTN_A && !(prevInput&BTN_A)) {
					if(levelBuffer[cursorX][cursorY].unit != 0xff && GETPLAY(unitList[levelBuffer[cursorX][cursorY].unit].info) == activePlayer) {
						// enter select unit mode if there's a unit here and it belongs to us
						//displayUnitMenu();
						selectionVar = 0;
						//setBlinkMode(FALSE);
						controlState = unit_menu;

					}
				}
				if(curInput&BTN_X && !(prevInput&BTN_X)) {
					// toggle blink mode
					setBlinkMode(!blinkMode);
				}
				if(curInput&BTN_LEFT) {
					// move cur left
					moveCursor(DIR_LEFT);
				}
				if(curInput&BTN_RIGHT) {
					// move cur right
					moveCursor(DIR_RIGHT);
				}
				if(curInput&BTN_UP) {
					// move cur up
					moveCursor(DIR_UP);
				}
				if(curInput&BTN_DOWN) {
					// move cur down
					moveCursor(DIR_DOWN);
				}
				if(curInput&BTN_Y && !(prevInput&BTN_Y)) {
					jumpToNextUnit();
				}
				if(curInput&BTN_SELECT && !(prevInput&BTN_SELECT)) {
					// open end turn menu
					controlState = end_turn;
					selectionVar = 0;

					MoveSprite(0, 224, 0, 2, 2);
					drawTwoSelMenu(PSTR("End turn?"), PSTR("Yes"), PSTR("No"));
//
//
//					DrawMap2((vramX+5)%0x1F, 5, INTERFACE_TR)

				}
				break;
			case unit_menu:
				if(curInput&BTN_X && !(prevInput&BTN_X)) {
					// toggle blink mode
					setBlinkMode(!blinkMode);
				}
				if(curInput&BTN_A && !(prevInput&BTN_A)) {
					// do selection
					if(selectionVar == 1) { // move
						if(!HASMOVED(unitList[levelBuffer[cursorX][cursorY].unit].other)) {
							controlState = unit_movement;
							movementPoints = 10;
							moveCursorInstant(cursorX, cursorY); // just to normalize
							movingUnit = levelBuffer[cursorX][cursorY].unit;
							arrowX = unitList[movingUnit].xPos;
							arrowY = unitList[movingUnit].yPos;
						}
					}
					else if(selectionVar == 0){ // attack
						if(!HASATTACKED(unitList[levelBuffer[cursorX][cursorY].unit].other)) {
							attackingUnit = levelBuffer[cursorX][cursorY].unit;
							attackedUnit = getNextAttackableUnitIndex(-1, 1);
							if(attackedUnit != 0xFF) {
								controlState = unit_attack;
								cursorX = unitList[attackedUnit].xPos;
								cursorY = unitList[attackedUnit].yPos;
								moveCursorInstant(cursorX, cursorY);
							}
						}
					}
					else {
						ERROR("inv. sel um.");
					}
				}
				if(curInput&BTN_B && !(prevInput&BTN_B)) {
					// leave unit action menu
					controlState = scrolling;
				}
				if(curInput&BTN_UP && !(prevInput&BTN_UP)) {
					// move selection up
					selectionVar = 0; //only works because 2 choices, uncomment below if more
					/*
					if(selectionVar != 0) {
						selectionVar--;
					}
					*/
				}
				if(curInput&BTN_DOWN && !(prevInput&BTN_DOWN)) {
					// move selection down
					selectionVar = 1; //only works because 2 choices, uncomment below if more
					/*
					if(selectionVar != 1) {
						selectionVar++;
					}
					*/
				}
				
				break;
				
			case unit_movement:
				if(curInput&BTN_LEFT && !(prevInput&BTN_LEFT)) {
					if(movementCount > 0 && movementBuffer[movementCount-1].direction == DIR_RIGHT) {
						movementCount--;
						arrowX--;
						movementPoints += movementBuffer[movementCount].movePoints;
					}
					else if(movementCount < 10 && validArrowTile(arrowX-1, arrowY)) {
						movementBuffer[movementCount].direction = DIR_LEFT;
						movementBuffer[movementCount].movePoints = getNeededMovePoints(GETUNIT(unitList[movingUnit].info), GETTERR(levelBuffer[arrowX-1][arrowY].info));
						movementPoints -= movementBuffer[movementCount].movePoints;
						movementCount++;
						arrowX--;
					}
				}
				if(curInput&BTN_RIGHT && !(prevInput&BTN_RIGHT)) {
					if(movementCount > 0 && movementBuffer[movementCount-1].direction == DIR_LEFT) {
						movementCount--;
						arrowX++;
						movementPoints += movementBuffer[movementCount].movePoints;
					}
					else if(movementCount < 10 && validArrowTile(arrowX+1, arrowY)) {
						movementBuffer[movementCount].direction = DIR_RIGHT;
						movementBuffer[movementCount].movePoints = getNeededMovePoints(GETUNIT(unitList[movingUnit].info), GETTERR(levelBuffer[arrowX+1][arrowY].info));
						movementPoints -= movementBuffer[movementCount].movePoints;
						movementCount++;
						arrowX++;
					}
				}
				if(curInput&BTN_UP && !(prevInput&BTN_UP)) {
					if(movementCount > 0 && movementBuffer[movementCount-1].direction == DIR_DOWN) {
						movementCount--;
						arrowY--;
						movementPoints += movementBuffer[movementCount].movePoints;
					}
					else if(movementCount < 10 && validArrowTile(arrowX, arrowY-1)) {
						movementBuffer[movementCount].direction = DIR_UP;
						movementBuffer[movementCount].movePoints = getNeededMovePoints(GETUNIT(unitList[movingUnit].info), GETTERR(levelBuffer[arrowX][arrowY-1].info));
						movementPoints -= movementBuffer[movementCount].movePoints;
						movementCount++;
						arrowY--;
					}
				}
				if(curInput&BTN_DOWN && !(prevInput&BTN_DOWN)) {
					if(movementCount > 0 && movementBuffer[movementCount-1].direction == DIR_UP) {
						movementCount--;
						arrowY++;
						movementPoints += movementBuffer[movementCount].movePoints;
					}
					else if(movementCount < 10 && validArrowTile(arrowX, arrowY+1)) {
						movementBuffer[movementCount].direction = DIR_DOWN;
						movementBuffer[movementCount].movePoints = getNeededMovePoints(GETUNIT(unitList[movingUnit].info), GETTERR(levelBuffer[arrowX][arrowY+1].info));
						movementPoints -= movementBuffer[movementCount].movePoints;
						movementCount++;
						arrowY++;
					}
				}
				if(curInput&BTN_B && !(prevInput&BTN_B)) {
					// leave movement mode
					controlState = unit_menu;
					movementCount = 0;
					drawLevel(LOAD_ALL);
				}
				if(curInput&BTN_X && !(prevInput&BTN_X)) {
					// toggle blink mode
					setBlinkMode(!blinkMode);
				}
				if(curInput&BTN_A && !(prevInput&BTN_A)) {
					// move unit!
					if(movementCount > 0) {
						controlState = unit_moving;
						drawLevel(LOAD_ALL);
						moveUnit();
						moveCursorInstant(unitList[movingUnit].xPos, unitList[movingUnit].yPos);
						controlState = scrolling;
						movementCount = 0;
						drawLevel(LOAD_ALL);
						SETHASMOVED(movingUnit, TRUE);
					}
					else {
						// some error bleep
					}
				}

				
				break;
			case unit_attack:
				if(curInput&BTN_UP && !(prevInput&BTN_UP)) {
					attackedUnit = getNextAttackableUnitIndex(attackedUnit, -1);
					cursorX = unitList[attackedUnit].xPos;
					cursorY = unitList[attackedUnit].yPos;
					moveCursorInstant(cursorX, cursorY);
				}
				if(curInput&BTN_DOWN && !(prevInput&BTN_DOWN)) {
					attackedUnit = getNextAttackableUnitIndex(attackedUnit, 1);
					cursorX = unitList[attackedUnit].xPos;
					cursorY = unitList[attackedUnit].yPos;
					moveCursorInstant(cursorX, cursorY);
				}
				if(curInput&BTN_X && !(prevInput&BTN_X)) {
					// toggle blink mode
					setBlinkMode(!blinkMode);
				}
				if(curInput&BTN_B && !(prevInput&BTN_B)) {
					// leave attack mode
					controlState = unit_menu;
					movementCount = 0;
					cursorX = unitList[attackingUnit].xPos;
					cursorY = unitList[attackingUnit].yPos;
					moveCursorInstant(cursorX, cursorY);
					drawLevel(LOAD_ALL);
				}

				if(curInput&BTN_A && !(prevInput&BTN_A)) {
					attackUnit();
					SETHASATTACKED(attackingUnit, TRUE);
					moveCursorInstant(unitList[attackingUnit].xPos, unitList[attackingUnit].yPos);
					controlState = scrolling;
				}



				break;
			case end_turn:
				if((curInput&BTN_SELECT && !(prevInput&BTN_SELECT)) || (curInput&BTN_B && !(prevInput&BTN_B))) {
					// open end turn menu
					controlState = scrolling;
					moveCursorInstant(cursorX, cursorY);
					drawLevel(LOAD_ALL);
				}
				if((curInput&BTN_UP && !(prevInput&BTN_UP)) || (curInput&BTN_DOWN && !(prevInput&BTN_DOWN))) {
					selectionVar = !selectionVar;
					drawTwoSelMenu(PSTR("End turn?"), PSTR("Yes"), PSTR("No"));
				}
				if(curInput&BTN_A && !(prevInput&BTN_A)) {
					if(selectionVar == 0) { // end turn
						endTurn();
						jumpToNextUnit();
						controlState = scrolling;
						//?
					}
					else{
						controlState = scrolling;
						moveCursorInstant(cursorX, cursorY);
						drawLevel(LOAD_ALL);
					}
				}
				break;
			case pause:
			
				break;
				
			case menu:
				
				break;
				
			case unit_moving:
			
				break;
		}

		prevInput = curInput;
		WaitVsync_(1);
	}
}

void displayUnitMenu()
{
	Print(8,4,PSTR("one"));
	Print(8,5,PSTR("two"));
	Print(9,4,PSTR("three"));
	Print(9,5,PSTR("four"));
}

void drawScreenData() {
	PrintByte(13, OVR3, cursorX, 0);
	PrintByte(16, OVR3, cursorY, 0);
	PrintByte(13, OVR4, cameraX, 0);
	PrintByte(16, OVR4, vramX, 0);


}

void loadLevel(const char* level) {
	char val, terr, owner, unit;
	unsigned int x, y; // i know i said this wasn't needed but there will be overflow on the array access otherwise

	levelWidth = pgm_read_byte(&level[0]);
	levelHeight = pgm_read_byte(&level[1]);
	if(levelHeight > 11) {
		ERROR("inv. level height");
	}

	currentLevel = level;
	cameraX = 0;
	Screen.scrollX = 0;
	vramX = 0;
	// reset the unit list
	for(x = 0;x < MAX_UNITS;x++)
		unitList[x].isUnit = FALSE;

	// loop y first because then we work in order. locality probably isn't an issue but eh.
	for(y = 0; y < levelHeight; y++) {
		for(x = 0; x < levelWidth; x++) {
			val = pgm_read_byte(&level[y*levelWidth+x+2]);
			terr = val & TERRAIN_MASK;
			owner = val & OWNER_MASK;
			unit = val & UNIT_MASK;
			levelBuffer[x][y].info = terr | owner;
			levelBuffer[x][y].unit = 0xFF;
			if(unit != 0 && owner != NEU) {
				//this can be a unit
				addUnit(x, y, owner, unit);
			}
		}
	}
}

void drawLevel(char dir) {
	char x, y, bound;
	switch(dir){
		case LOAD_ALL:
			if(cameraX == 0) {
				vramX = 0;
				Screen.scrollX = 0;
			}
			else if(cameraX == levelWidth-MAX_VIS_WIDTH) {
				vramX = 4;
				Screen.scrollX = 32;
			}
			else {
				vramX = 2;
				Screen.scrollX = 16;
			}
			if(cameraX+MAX_VIS_WIDTH == levelWidth)
				bound = cameraX+MAX_VIS_WIDTH;
			else
				bound = cameraX+MAX_VIS_WIDTH+1;
			for(y = 0; y < LEVEL_HEIGHT; y++) {
				for(x = 0; x < MAX_VIS_WIDTH+1; x++) {
					if(y < levelHeight && x < levelWidth && x+cameraX < bound)
						DrawMap2(vramX+x*2, y*2, getTileMap(x+cameraX, y));
					else
						DrawMap2(vramX+x*2, y*2, map_placeholder);
				}
			}
			break;
		case LOAD_LEFT:
			// assume that we want to load the column that's cameraX-1
			// load it at vramX-2
			if(cameraX - 1 < 0) {
				ERROR("inv. left map load");
			}
			for(y = 0; y < levelHeight; y++) {
				DrawMap2(vramX-2, y*2, getTileMap(cameraX-1, y));
			}
			break;
		case LOAD_RIGHT:
			// assume that we want to load the column that's cameraX+MAX+1
		// load it at vramX+MAX*2+2
		if(cameraX+MAX_VIS_WIDTH+1 > MAX_LEVEL_WIDTH) {
			ERROR("inv. right map load");
		}
		for(y = 0; y < levelHeight; y++) {
			DrawMap2(vramX+(MAX_VIS_WIDTH+1)*2, y*2, getTileMap(cameraX+MAX_VIS_WIDTH+1, y));
		}
		break;
		default:
			ERROR("inv. load var");
	}
}


void redrawUnits() {
	// redraws all unit tiles on the visible map
	unsigned char i, v;

	v = 0;
	// not sure how the unit list thing works.
	for(i = 0; i < MAX_UNITS; i++) {
		if(unitList[i].isUnit) {
			if(unitList[i].xPos >= cameraX-1 && unitList[i].xPos <= cameraX+MAX_VIS_WIDTH) {
				// the unit is in our current camera buffer, redraw it
				DrawMap2(((unitList[i].xPos-cameraX)*2 + vramX)&0x1F, unitList[i].yPos*2, getTileMap(unitList[i].xPos, unitList[i].yPos));
				/*PrintByte(10, OVR1, cameraX, 0);
				PrintByte(10, OVR2, vramX, 0);*/
			}
			v++;
		}
	}
	//PrintByte(3, OVR4, v, 0);
}


void drawOverlay() {

	// draw the basic panel
	Fill(1, OVR1, 26, 3, INTERFACE_MID);
	Fill(0, OVR1, 1, 3, INTERFACE_LEFT);
	SetTile(0, OVR4, INTERFACE_BL);
	Fill(1, OVR4, 26, 1, INTERFACE_BOT);
	SetTile(27, OVR4, INTERFACE_BR);
	Fill(27, OVR1, 1, 3, INTERFACE_RIGHT);

	// is there a unit here? draw info
	// TODO: might need conditions for other overlay types, this is preliminary
	if(levelBuffer[cursorX][cursorY].unit != 0xFF) {
		struct Unit* unit = &unitList[levelBuffer[cursorX][cursorY].unit];
		Print(12, OVR1, getUnitName(unit->info));
		drawHPBar(12, OVR2, unit->hp);

		if(GETPLAY(unit->info) == activePlayer) {
			Print(12, OVR3, PSTR("MOV"));
			Print(18, OVR3, PSTR("ATK"));

			if(HASMOVED(unit->other))
				SetTile(15, OVR3, INTERFACE_RLIGHT);
			else
				SetTile(15, OVR3, INTERFACE_GLIGHT);
			if(HASATTACKED(unit->other))
				SetTile(21, OVR3, INTERFACE_RLIGHT);
			else
				SetTile(21, OVR3, INTERFACE_GLIGHT);
		}
	}



	if(activePlayer == PL1) {
		Print(26, OVR1, PSTR("P1"));
		PrintByte(27, OVR2, credits[0], TRUE);
	}
	else {
		Print(26, OVR1, PSTR("P2"));
		PrintByte(27, OVR2, credits[1], TRUE);
	}

	SetTile(23, OVR2, INTERFACE_DOLLAR);

	const char* map;
	switch(levelBuffer[cursorX][cursorY].info & TERRAIN_MASK) {
		case PL:
			map = map_plain;
			Print(3, OVR1, PSTR("Plains"));
			break;
		case MO:
			map = map_mountain;
			Print(3, OVR1, PSTR("Mountains"));
			break;
		case FO:
			map = map_forest;
			Print(3, OVR1, PSTR("Forest"));
			break;
		case CT:
			Print(3, OVR1, PSTR("City"));
			switch(levelBuffer[cursorX][cursorY].info & OWNER_MASK) {
				case PL1:
					map = map_city_red;
					break;
				case PL2:
					map = map_city_blu;
					break;
				case NEU:
					map = map_city_neu;
					break;
				default:
					map = map_placeholder;
			}
			break;
		case BS:
			Print(3, OVR1, PSTR("Base"));
			switch(levelBuffer[cursorX][cursorY].info & OWNER_MASK) {
				case PL1:
					map = map_base_red;
					break;
				case PL2:
					map = map_base_blu;
					break;
				case NEU:
					map = map_base_neu;
					break;
				default:
					map = map_placeholder;
			}
			break;
		default:
			map = map_placeholder;
		}
	DrawMap2(1, OVR2, map);

	drawDefenseBar(3, OVR2, 100);

	//drawScreenData();

	// are we in unit action mode? draw the action menu (with selection arrow)
	if(controlState == unit_menu) {
		SetTile(27-1, OVR2, INTERFACE_TR);
		SetTile(27-7, OVR2, INTERFACE_TL);
		SetTile(27-7, OVR3, INTERFACE_BL);
		SetTile(27-1, OVR3, INTERFACE_BR);
		Fill(27-6, OVR2, 5, 1, INTERFACE_TOP);
		Fill(27-6, OVR3, 5, 1, INTERFACE_BOT);
		DrawMap2(27-5, OVR2, map_attack_text);
		DrawMap2(27-5, OVR3, map_move_text);
		SetTile(27-6, OVR2+selectionVar, selectionVar == 0 ? INTERFACE_ARROW_TOP : INTERFACE_ARROW_BOT); // this looks ugly but sprites don't work in overlay...
	}
	if(controlState == unit_movement) {
		PrintByte(17, OVR3, movementPoints, 0);

	}
	/*
	if(controlState == unit_attack) {
		PrintByte(27, OVR2, attackedUnit, 0);

	}
	*/
	//PrintByte(12, OVR3, getRandomNumber(),FALSE);
}

void drawArrow() {
	unsigned char traverseX, traverseY, traverseI;
	traverseX = unitList[movingUnit].xPos;
	traverseY = unitList[movingUnit].yPos;
	for(traverseI = 0;traverseI < movementCount+1;traverseI++) {
		switch(movementBuffer[traverseI].direction) {
		case DIR_UP:
			traverseY--;
			break;
		case DIR_DOWN:
			traverseY++;
			break;
		case DIR_LEFT:
			traverseX--;
			break;
		case DIR_RIGHT:
			traverseX++;
			break;
		}

		if(traverseX >= levelWidth || traverseY >= levelHeight)
			continue;

		DrawMap2(((traverseX-cameraX)*2 + vramX)&0x1F, traverseY*2, getTileMap(traverseX, traverseY));
		//PrintByte(12, OVR1, movementCount, 0);
	}



}

char moveCamera(char dir) {
	switch(dir) {
		case LOAD_LEFT:
			if(cameraX == 0) {
				//nothing happens
				return FALSE;
			}
			drawLevel(dir);
			//animate the screen movement
			while(1) {
				Screen.scrollX--;
				WaitVsync_(1);
				if(Screen.scrollX % 16 == 0)
					break;
			}
			cameraX--;
			vramX = (vramX-2)&0x1F;
			// TODO: sprite movement (cursors, moving units, etc)
			// cursors seem to stay in place when screen moves... intredasting
			break;

		case LOAD_RIGHT:
			if(cameraX == levelWidth-MAX_VIS_WIDTH) {
				return FALSE;
			}
			drawLevel(dir);
			while(1) {
				Screen.scrollX++;
				WaitVsync_(1);
				if(Screen.scrollX % 16 == 0)
					break;
			}
			cameraX++;
			vramX = (vramX+2)&0x1F;
			break;

		case LOAD_ALL:

		default:
			ERROR("inv. move");
	}

	return TRUE;
}

char moveCameraInstant(char x) {
	if(x == cameraX)
		return TRUE;
	//vramX = 0;
	cameraX = x;
	drawLevel(LOAD_ALL);
	return TRUE;
}

char moveCursor(char direction) {
	char temp;
	switch(direction) {
	case DIR_UP:
		if(cursorY == 0)
			return FALSE;
		temp = cursorY*16;
		while(1) {
			temp--;
			MoveSprite(0, (cursorX-cameraX)*16, temp, 2, 2);
			if(temp % 16 == 0)
				break;
			WaitVsync_(1);
		}
		cursorY--;
		break;
	case DIR_DOWN:
		if(cursorY == levelHeight-1)
			return FALSE;
		temp = cursorY*16;
		while(1) {
			temp++;
			MoveSprite(0, (cursorX-cameraX)*16, temp, 2, 2);
			if(temp % 16 == 0)
				break;
			WaitVsync_(1);
		}
		cursorY++;
		break;
	case DIR_LEFT:
		if(cursorX == 0)
			return FALSE;
		if(cameraX > 0) {
			if(cursorX-cameraX == 1) { // converts to screen coords
				// we want to shift the screen, not the cursor itself
				moveCamera(LOAD_LEFT);
				cursorX--;
				break; // don't continue
			}
		}
		// if we don't want to move the screen, we move the cursor!
		temp = (cursorX-cameraX)*16; // must be relative to screen
		while(1) {
			temp--;
			MoveSprite(0, temp, cursorY*16, 2, 2);
			if(temp % 16 == 0)
				break;
			WaitVsync_(1);
		}
		cursorX--;
		break;
	case DIR_RIGHT:
		if(cursorX == levelWidth-1)
			return FALSE;
		if(cameraX < levelWidth-MAX_VIS_WIDTH) {
			if(cursorX-cameraX == MAX_VIS_WIDTH-2) { // right edge, screen coords
				moveCamera(LOAD_RIGHT);
				cursorX++;
				break;
			}
		}
		// else, move the cursor
		temp = (cursorX-cameraX)*16;
		while(1) {
			temp++;
			MoveSprite(0, temp, cursorY*16, 2, 2);
			if(temp % 16 == 0)
				break;
			WaitVsync_(1);
		}
		cursorX++;
		break;
	}
	return TRUE;
}

char moveCursorInstant(unsigned char x, unsigned char y) {
	char normalizedCameraX;

	normalizedCameraX = (char)x - MAX_VIS_WIDTH/2;
	if(normalizedCameraX < 0)
		normalizedCameraX = 0;
	if(normalizedCameraX > levelWidth-MAX_VIS_WIDTH)
		normalizedCameraX = levelWidth-MAX_VIS_WIDTH;
	if(levelWidth < MAX_VIS_WIDTH)
		normalizedCameraX = 0;

	//PrintByte(19, OVR4, normalizedCameraX, 0);
	//WaitVsync_(60);

	moveCameraInstant(normalizedCameraX);

	cursorX = x;
	cursorY = y;

	MoveSprite(0, (cursorX-cameraX)*16, cursorY*16, 2, 2);
	
	return TRUE;
}


void moveUnit() {
	uint8_t traverseX, traverseY, traverseI;
	uint8_t newX, newY;
	newX = 0;
	newY = 0;

	mapMovingUnitSprite();

	traverseX = unitList[movingUnit].xPos;
	traverseY = unitList[movingUnit].yPos;
	for(traverseI = 0;traverseI < movementCount; traverseI++) {
		switch(movementBuffer[traverseI].direction) {
			case DIR_UP:
				newX = traverseX;
				newY = traverseY-1;
				break;
			case DIR_DOWN:
				newX = traverseX;
				newY = traverseY+1;
				break;
			case DIR_LEFT:
				newX = traverseX-1;
				newY = traverseY;
				break;
			case DIR_RIGHT:
				newX = traverseX+1;
				newY = traverseY;
				break;
			default:
				newX = traverseX;
				newY = traverseY;
		}

		tweenUnitSprite(traverseX, traverseY, newX, newY);
		traverseX = newX;
		traverseY = newY;
	}

	//newX and newY will be the final coords
	levelBuffer[unitList[movingUnit].xPos][unitList[movingUnit].yPos].unit = 0xFF;
	unitList[movingUnit].xPos = newX;
	unitList[movingUnit].yPos = newY;
	levelBuffer[unitList[movingUnit].xPos][unitList[movingUnit].yPos].unit = movingUnit;
	MoveSprite(4, -16, 0, 2, 2);
}

void tweenUnitSprite(char sx, char sy, char dx, char dy) {
	char xdir, ydir, tween;

	xdir = dx - sx;
	ydir = dy - sy;

	sx = (sx - cameraX) * 16;
	sy = sy * 16;
	dx = (dx - cameraX) * 16;
	dy = dy * 16;

	if(xdir != 0) {
		tween = sx;
		while(tween != dx) {
			tween += xdir;
			MoveSprite(4, tween, dy, 2, 2);
			WaitVsync_(2);
		}
	}
	else if(ydir != 0) {
		tween = sy;
		while(tween != dy) {
			tween += ydir;
			MoveSprite(4, dx, tween, 2, 2);
			WaitVsync_(2);
		}
	}


}

char validArrowTile(unsigned char x, unsigned char y) {
	unsigned char traverseX, traverseY, traverseI;

	// can't be outside the level
	if(x >= levelWidth || y >= levelHeight) {
		return FALSE;
	}

	// can't have a unit
	if(levelBuffer[x][y].unit != 0xFF) {
		return FALSE;
	}

	// not enough points
	if(movementPoints - getNeededMovePoints(GETUNIT(unitList[movingUnit].info), GETTERR(levelBuffer[x][y].info)) < 0) {
		return FALSE;
	}

	traverseX = unitList[movingUnit].xPos;
	traverseY = unitList[movingUnit].yPos;
	for(traverseI = 0;traverseI < movementCount;traverseI++) {
		switch(movementBuffer[traverseI].direction) {
		case DIR_UP:
			traverseY--;
			break;
		case DIR_DOWN:
			traverseY++;
			break;
		case DIR_LEFT:
			traverseX--;
			break;
		case DIR_RIGHT:
			traverseX++;
			break;
		}

		if(x == traverseX && y == traverseY)
			return FALSE;
	}
	return TRUE;
}

void setBlinkMode(char active) {
	blinkMode = active;
	blinkCounter = 0;
	blinkState = BLINK_UNITS;
	redrawUnits();
}


//TODO: MAX units *per* team, not total units
//TODO: unit adding (and probably removing) is FLAWED, needs to be redone
unsigned char addUnit(unsigned char x, unsigned char y, char player, char type) {
	char ret;

	if(levelBuffer[x][y].unit != 0xFF)
	{
		//ERROR("Unit already in space!");
		return 0xFF;
	}
	else if (unitFirstEmpty == 0xFF)
	{
		//ERROR("Unit list fulL!");
		return 0xFF;
	}
	else
	{
		unitList[unitFirstEmpty].isUnit = TRUE;
		unitList[unitFirstEmpty].hp = 100;
		unitList[unitFirstEmpty].info = player | type;
		unitList[unitFirstEmpty].xPos = x;
		unitList[unitFirstEmpty].yPos = y;
		levelBuffer[x][y].unit = unitFirstEmpty;
		ret = unitFirstEmpty;
		

		/*
		for(int i = unitFirstEmpty+1; i < MAX_UNITS-1; i++) //Starting from 1+ the known earliest empty space, search for the next empty space.
		{
			if(!unitList[i].isUnit) 
			{
				unitFirstEmpty = i;
				break;
			} 
			else if(i == MAX_UNITS-1) //If we reach the end of the units list, the list is full. 
				unitFirstEmpty = 0xFF;
		}
		*/

		for(unsigned char i = unitFirstEmpty; ;) {
			if(!unitList[i].isUnit) {
				unitFirstEmpty = i;
				break;
			}

			i = (i+1)%MAX_UNITS;
			if(i == unitFirstEmpty) {
				unitFirstEmpty = 0xFF;
				break;
			}
		}
		
		return ret;
	}
}

void removeUnit(unsigned char x, unsigned char y) {
	if(levelBuffer[x][y].unit == 0xFF)
		ERROR("ru");

	unitList[levelBuffer[x][y].unit].isUnit = FALSE;
	//memset(&unitList[levelBuffer[x][y].unit], sizeof(unitList[levelBuffer[x][y].unit]), 0); //Zero out the unit struct.

	//if(unitFirstEmpty > (levelBuffer[x][y].unit + (levelBuffer[x][y].unit < unitFirstEmpty) * MAX_UNITS) ) //If this newly opened index is earlier than the last known index, change it accordingly.
	unitFirstEmpty = levelBuffer[x][y].unit;
	levelBuffer[x][y].unit = 0xFF; //Mark this grid buffer square as no unit.

}

void removeUnitByIndex(unsigned char unit) {
	if(unit > MAX_UNITS)
		ERROR("rubi");

	unitList[unit].isUnit = FALSE;
	unitFirstEmpty = unit;
	levelBuffer[unitList[unit].xPos][unitList[unit].yPos].unit = 0xFF;
}

unsigned char getNextAttackableUnitIndex(signed char last, char dir) {
	int8_t i = last+dir;
	int8_t count = 0;
	int8_t range = getAttackRange(unitList[attackingUnit].info);
	unsigned char player = activePlayer == PL1 ? PL2 : PL1; // reverse the player
	if(last == -1) {
		i = 0;
		last = MAX_UNITS-1;
		dir = 1;
	}

	for(; count < MAX_UNITS; i += dir, count++) {
		if(i >= MAX_UNITS)
			i = 0;
		if(i < 0)
			i = MAX_UNITS-1;

		if(unitList[i].isUnit && GETPLAY(unitList[i].info) == player) {
			if(range == 1 && ABS(unitList[i].xPos - unitList[attackingUnit].xPos) == 1 && ABS(unitList[i].yPos - unitList[attackingUnit].yPos) == 1)
				return i;
			else if(MANH(unitList[i].xPos, unitList[i].yPos, unitList[attackingUnit].xPos, unitList[attackingUnit].yPos) <= range)
				return i;
		}
	}
	if(unitList[last].isUnit && GETPLAY(unitList[last].info) == player) {
		if(range == 1 && ABS(unitList[last].xPos - unitList[attackingUnit].xPos) == 1 && ABS(unitList[last].yPos - unitList[attackingUnit].yPos) == 1)
			return last;
		else if(MANH(unitList[last].xPos, unitList[last].yPos, unitList[attackingUnit].xPos, unitList[attackingUnit].yPos) <= range)
			return last;
	}

	return 0xFF;
}


// gets the tile map for a certain game coordinate
const char* getTileMap(unsigned char x, unsigned char y) {
	unsigned char terrain, unitOwner, propertyOwner, unit, displayUnit;

	if(x >= levelWidth || y >= levelHeight) {
		return map_placeholder;
	}

	terrain = GETTERR(levelBuffer[x][y].info);
	if(levelBuffer[x][y].unit != 0xff) {
		unit = GETUNIT(unitList[levelBuffer[x][y].unit].info);
		unitOwner = GETPLAY(unitList[levelBuffer[x][y].unit].info);
	}
	else {
		unit = 0;
		unitOwner = 0;
	}
	propertyOwner = GETPLAY(levelBuffer[x][y].info); // this should be 0 if there is no owner

	if(unit) { // if we have a unit to display, display it!
		displayUnit = TRUE;
	}
	else {
		displayUnit = FALSE;
	}
	// blink mode overrides?
	if(blinkMode) {
		if(unit && blinkState == BLINK_UNITS) {
			// there is a unit on the tile, we are blinking and the state is units
			// show the unit on the tile
			displayUnit = TRUE;
		}
		else {
			// is this correct?
			displayUnit = FALSE;
		}
	}

	if(controlState == unit_moving && levelBuffer[x][y].unit == movingUnit) {
		// if this tile has the moving unit on it, don't draw it as a tile
		// (draw it as a sprite instead)
		displayUnit = FALSE;
	}

	if(!(blinkMode && blinkState == BLINK_TERRAIN)) {
		if(controlState == unit_movement) {
			unsigned char traverseX, traverseY, traverseI;
			traverseX = unitList[movingUnit].xPos;
			traverseY = unitList[movingUnit].yPos;
			for(traverseI = 0;traverseI < movementCount;traverseI++) {
				switch(movementBuffer[traverseI].direction) {
				case DIR_UP:
					traverseY--;
					break;
				case DIR_DOWN:
					traverseY++;
					break;
				case DIR_LEFT:
					traverseX--;
					break;
				case DIR_RIGHT:
					traverseX++;
					break;
				}

				if(x == traverseX && y == traverseY) {
					char curDirection, nextDirection;
					if(traverseI+1 == movementCount) { // we've reached the end, draw a stub
						switch(movementBuffer[traverseI].direction) {
						case DIR_UP:
							return map_arrow_top;
						case DIR_DOWN:
							return map_arrow_down;
						case DIR_LEFT:
							return map_arrow_left;
						case DIR_RIGHT:
							return map_arrow_right;
						}
					}

					curDirection = movementBuffer[traverseI].direction;
					nextDirection = movementBuffer[traverseI+1].direction;

					if(curDirection == nextDirection) { // they're the same, it's a vert or horz
						switch(curDirection) {
						case DIR_UP:
						case DIR_DOWN:
							return map_arrow_vert;
						case DIR_LEFT:
						case DIR_RIGHT:
							return map_arrow_horz;
						}

					}
					else { // it's a corner
						if((curDirection == DIR_UP && nextDirection == DIR_LEFT) ||
						   (curDirection == DIR_RIGHT && nextDirection == DIR_DOWN))
							return map_arrow_tr;
						if((curDirection == DIR_UP && nextDirection == DIR_RIGHT) ||
						   (curDirection == DIR_LEFT && nextDirection == DIR_DOWN))
							return map_arrow_tl;
						if((curDirection == DIR_DOWN && nextDirection == DIR_LEFT) ||
						   (curDirection == DIR_RIGHT && nextDirection == DIR_UP))
							return map_arrow_br;
						if((curDirection == DIR_DOWN && nextDirection == DIR_RIGHT) ||
						   (curDirection == DIR_LEFT && nextDirection == DIR_UP))
							return map_arrow_bl;
					}

					return map_placeholder;
				}

			}



		}
	}


	if(displayUnit) {
		switch(unit) { // all of these need placeholders
		case UN1:
			switch(unitOwner) {
			case PL1:
				return map_unit1_red;
			case PL2:
				return map_unit1_blu;
			}
			break;
		case UN2:
			switch(unitOwner) {
			case PL1:
				return map_unit2_red;
			case PL2:
				return map_unit2_blu;
			}
			break;
		case UN3:
			switch(unitOwner) {
			case PL1:
				return map_unit3_red;
			case PL2:
				return map_unit3_blu;
			}
			break;
		case UN4:
			switch(unitOwner) {
			case PL1:
				return map_unit4_red;
			case PL2:
				return map_unit4_blu;
			}
			break;
		case UN5:
			switch(unitOwner) {
			case PL1:
				return map_unit5_red;
			case PL2:
				return map_unit5_blu;
			}
			break;
		}
		return map_placeholder; // in case all the above passes
	}
	else {
		switch(terrain) {
		case PL:
			return map_plain;
		case MO:
			return map_mountain;
		case FO:
			return map_forest;
		case CT:
			switch(propertyOwner) {
			case PL1:
				return map_city_red;
			case PL2:
				return map_city_blu;
			case NEU:
				return map_city_neu;
			default:
				return map_placeholder;
			}
		case BS:
			switch(propertyOwner) {
			case PL1:
				return map_base_red;
			case PL2:
				return map_base_blu;
			case NEU:
				return map_base_neu;
			default:
				return map_placeholder;
			}
		default:
			return map_placeholder;
		}
	}
}

void mapCursorSprite(char alt) {
	if(!alt) {
		sprites[0].tileIndex = 0;
		sprites[1].tileIndex = 0;
		sprites[2].tileIndex = 2;
		sprites[3].tileIndex = 2;
	}
	else {
		sprites[0].tileIndex = 1;
		sprites[1].tileIndex = 1;
		sprites[2].tileIndex = 3;
		sprites[3].tileIndex = 3;
	}
	sprites[0].flags = 0;
	sprites[1].flags = SPRITE_FLIP_X;
	sprites[2].flags = 0;
	sprites[3].flags = SPRITE_FLIP_X;
}

/*const char* _movingSpriteMap[] PROGMEM = {
	0,0,0,0,0,
	sprite_unit1_red, sprite_unit2_red, sprite_unit3_red, sprite_unit4_red, sprite_unit5_red,
	sprite_unit1_blu, sprite_unit2_blu, sprite_unit3_blu, sprite_unit4_blu, sprite_unit5_blu,
};*/

void mapMovingUnitSprite() {
	const char* map;

	// TODO: this doesn't work and I don't know why.
	/*uint8_t u = INDEXUNIT(GETUNIT(unitList[movingUnit].info));
	uint8_t pl = INDEXPLAY(GETPLAY(unitList[movingUnit].info));

	map = (const char*)(
			pgm_read_word(&_movingSpriteMap[pl*2+u])
	);*/

	switch(GETUNIT(unitList[movingUnit].info)|GETPLAY(unitList[movingUnit].info))  {
	case PL1|UN1:
		map = sprite_unit1_red;
		break;
	case PL1|UN2:
		map = sprite_unit2_red;
		break;
	case PL1|UN3:
		map = sprite_unit3_red;
		break;
	case PL1|UN4:
		map = sprite_unit4_red;
		break;
	case PL1|UN5:
		map = sprite_unit5_red;
		break;
	case PL2|UN1:
		map = sprite_unit1_blu;
		break;
	case PL2|UN2:
		map = sprite_unit2_blu;
		break;
	case PL2|UN3:
		map = sprite_unit3_blu;
		break;
	case PL2|UN4:
		map = sprite_unit4_blu;
		break;
	case PL2|UN5:
		map = sprite_unit5_blu;
		break;
	default:
		ERROR("inv. mmsp")
	}

	MapSprite(4, map);
	sprites[4].flags = 0;
	sprites[5].flags = 0;
	sprites[6].flags = 0;
	sprites[7].flags = 0;
	MoveSprite(4,(cursorX-cameraX)*16, cursorY*16, 2, 2);


}

void drawHPBar(unsigned char x, unsigned char y, char val) {
	val = val >> 1;
	DrawMap2(x, y, hp_bar_base);
	x += 2;
	Fill(x, y, 7, 1, INTERFACE_MID);
	while(val >= 8) {
		SetTile(x, y, 9);
		val -= 8;
		x++;
	}
	//i want a fancy indexing thing here but fucking tiles man
	switch(val) {
	case 7:
		SetTile(x,y,10);
		break;
	case 6:
		SetTile(x,y,11);
		break;
	case 5:
		SetTile(x,y,12);
		break;
	case 4:
		SetTile(x,y,13);
		break;
	case 3:
		SetTile(x,y,14);
		break;
	case 2:
		SetTile(x,y,21);
		break;
	case 1:
		SetTile(x,y,22);
		break;
	case 0:
	default:
		SetTile(x,y,INTERFACE_MID);
	}

}

void drawDefenseBar(unsigned char x, unsigned char y, char val) {
	val = val >> 1;
	DrawMap2(x, y, hp_bar_base);
	x += 2;
	Fill(x, y, 7, 1, INTERFACE_MID);
	while(val >= 8) {
		SetTile(x, y, 9);
		val -= 8;
		x++;
	}
	//i want a fancy indexing thing here but fucking tiles man
	switch(val) {
	case 7:
		SetTile(x,y,10);
		break;
	case 6:
		SetTile(x,y,11);
		break;
	case 5:
		SetTile(x,y,12);
		break;
	case 4:
		SetTile(x,y,13);
		break;
	case 3:
		SetTile(x,y,14);
		break;
	case 2:
		SetTile(x,y,21);
		break;
	case 1:
		SetTile(x,y,22);
		break;
	case 0:
	default:
		SetTile(x,y,INTERFACE_MID);
	}

}

const char* getUnitName(unsigned char unit) {
	switch(GETUNIT(unit)) {
	case UN1:
		return PSTR("Infantry");
	case UN2:
		return PSTR("Tank");
	case UN3:
		return PSTR("Mortar");
	case UN4:
		return PSTR("Mercenary");
	case UN5:
		return PSTR("Rocket");
	default:
		ERROR("inv. unit");
	}

}

char getNeededMovePoints(const char unit, const char terrain) {
	//oh god save me please
	switch(unit|terrain) {
		case UN1|BS:
		case UN2|BS:
		case UN4|BS:
		case UN5|BS:
			return 1;
		case UN1|PL:
		case UN2|PL:
		case UN4|PL:
		case UN5|PL:
		case UN4|FO:
		case UN1|CT:
		case UN4|CT:
		case UN5|CT:
		case UN3|BS:
			return 2;
		case UN1|FO:
		case UN3|PL:
		case UN5|FO:
		case UN2|CT:
		case UN3|CT:
			return 3;
		case UN2|FO:
			return 4;
		case UN3|FO:
			return 5;
		case UN4|MO:
		case UN5|MO:
			return 7;
		case UN1|MO:
			return 8;
		case UN2|MO:
		case UN3|MO:
			return 11;
		default:
			ERROR("gnmp");
	}
}

const char _damage[] PROGMEM = {
	25, 15, 15, 25, 35,
	25, 35, 25, 15, 15,
	15, 35, 25, 25, 15,
	35, 25, 25, 15, 35,
	15, 35, 35, 15, 15
};

char getDamage(struct Unit* srcUnit, struct Unit* dstUnit) {
	char baseDamage = 0;

	// src index and dst index
	// shifts the unit number so it can be used as an index
	uint8_t src = INDEXUNIT(GETUNIT(srcUnit->info))-1;
	uint8_t dst = INDEXUNIT(GETUNIT(dstUnit->info))-1;

	baseDamage = pgm_read_byte(&_damage[src*5+dst]);

/* old lookup routine; is 200 bytes more!
	// get base damage (unit -> unit)
	switch(GETUNIT(srcUnit->info)) {
	case UN1:
		switch(GETUNIT(dstUnit->info)) {
		case UN2:
		case UN3:
			baseDamage = 15;
			break;
		case UN1:
		case UN4:
			baseDamage = 25;
			break;
		case UN5:
			baseDamage = 35;
			break;
		}
		break;
	case UN2:
		switch(GETUNIT(dstUnit->info)) {
		case UN4:
		case UN5:
			baseDamage = 15;
			break;
		case UN1:
		case UN3:
			baseDamage = 25;
			break;
		case UN2:
			baseDamage = 35;
			break;
		}
		break;
	case UN3:
		switch(GETUNIT(dstUnit->info)) {
		case UN1:
		case UN5:
			baseDamage = 15;
			break;
		case UN3:
		case UN4:
			baseDamage = 25;
			break;
		case UN2:
			baseDamage = 35;
			break;
		}
		break;
	case UN4:
		switch(GETUNIT(dstUnit->info)) {
		case UN4:
			baseDamage = 15;
			break;
		case UN2:
		case UN3:
			baseDamage = 25;
			break;
		case UN1:
		case UN5:
			baseDamage = 35;
			break;
		}
		break;
	case UN5:
		switch(GETUNIT(dstUnit->info)) {
		case UN1:
		case UN4:
		case UN5:
			baseDamage = 15;
			break;
		case UN2:
		case UN3:
			baseDamage = 35;
			break;
		}
		break;
	}*/

	// get random boost (0-10 extra)
	baseDamage += getRandomNumberLimit(10);

	//terrain resistance
	switch(GETTERR(levelBuffer[dstUnit->xPos][dstUnit->yPos].info)) {
	case BS:
		if(GETUNIT(srcUnit->info) == UN3)
			baseDamage -= 5; // mortar vs base
		else
			baseDamage -= 10;
		break;
	case MO:
		baseDamage -= 8;
		break;
	case FO:
		baseDamage -= 5;
		break;
	case CT:
		if(GETUNIT(srcUnit->info) != UN3)
			baseDamage -= 5;
	}
	if(baseDamage <= 0)
		baseDamage = 1;

	return baseDamage;
}

const char _range[] PROGMEM = {
	1, 1, 3, 1, 2
};

char getAttackRange(const char unit) {
	uint8_t u = INDEXUNIT(GETUNIT(unit))-1;
	if(u >= 5)
		ERROR("inv. u ar");
	return pgm_read_byte(&_range[u]);
	/*
	switch(GETUNIT(unit)) {
	case UN1:
	case UN2:
	case UN4:
		return 1;
	case UN5:
		return 2;
	case UN3:
		return 3;
	default:
		ERROR("inv. u ar");
	}
	return 1;*/
}

char getRandomNumberLimit(char max) {
	char a = getRandomNumber();
	if(a < 0)
		a = -a;
	return a % (max+1);
}

char getRandomNumber() {
	unsigned char lo, hi, xor, save;

	lo = (unsigned char)eepromData.data[0];
	hi = (unsigned char)eepromData.data[1];

	if(!hi && !lo)
		lo = 0x31; // some good normal value

	xor =  lo&1;
	xor ^= (lo>>2)&1;
	xor ^= (lo>>3)&1;
	xor ^= (lo>>5)&1;

	save = hi&1;
	hi = ((hi>>1)&0x7F)|(xor<<7);
	lo = ((lo>>1)&0x7F)|(save<<7);

	eepromData.data[0] = (char)lo;
	eepromData.data[1] = (char)hi;

	return (char)lo;
}

void WaitVsync_(char count) {
	// this is used for periodicals like blink and cursor alternation
	// call this instead of WaitVsync to make sure that periodicals
	// get called even if we are doing something function-locked
	while(count > 0) {


		// insert periodicals here
		//TODO: make sure the correct periodicals only fire when they are supposed to
		if(cursorCounter >= 40) {
			cursorCounter = 0;
			mapCursorSprite(cursorAlt);
			cursorAlt = !cursorAlt;
		}
		cursorCounter++;

		if(controlState == unit_movement) {
			drawArrow();
		}



		if(controlState != end_turn && blinkMode) {
			if(blinkCounter >= 30) {
				// toggle blink
				blinkState = !blinkState;
				redrawUnits();
				blinkCounter = 0;
			}
			blinkCounter++;
		}
		else {
			blinkCounter = 0;
		}



		WaitVsync(1); // wait only once
		count--;
	}

}
