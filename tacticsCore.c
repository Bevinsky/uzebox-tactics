/* lib includes */
#include <avr/io.h>
#include <stdlib.h>
#include <math.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <uzebox.h>


/* data includes */

#include "res/tiles.inc"
#include "res/fontmap.inc"
#include "res/sprites.inc"

/* structs */
struct GridBufferSquare {
    unsigned char unit; // index to Unit array; 0xff for no unit
    unsigned char info; // terrain and player bitfield
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
	Print(4,0,PSTR(msg));\
	while(1)\
		WaitVsync(1);

// convert our player value to controller value
#define JPPLAY(pl) ((pl) == PL2 ? 1 : 0)

// level data masks
#define TERRAIN_MASK 0b00000111
#define UNIT_MASK	 0b00111000
#define OWNER_MASK	 0b11000000

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

// unit types
#define UN1 0x08
#define UN2 0x10
#define UN3 0x18
#define UN4 0x20
#define UN5 0x28
#define NO_UNIT 0xFF

#define GETUNIT(x) ((x)&UNIT_MASK)

// players
#define PL1	0x80
#define PL2	0x40
#define NEU	0x00

#define GETPLAY(x) ((x)&OWNER_MASK)

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

//sprite indices
#define SPRITE_CURSOR 0
#define SPRITE_ARROW 4
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

const char* currentLevel;

enum
{
	scrolling, unit_menu, unit_movement, unit_moving, unit_attack, pause, menu
}	controlState;

// what is visible on the screen; 14 wide, 11 high, 2 loading columns on each side
struct GridBufferSquare levelBuffer[MAX_LEVEL_WIDTH][LEVEL_HEIGHT];

unsigned char unitFirstEmpty = 0;
unsigned char unitListStart = 0;
unsigned char unitListEnd = 0;
char lastJumpedUnit = -1;

struct Unit unitList[MAX_UNITS]; //is this enough?

struct Movement movementBuffer[10]; // ought to be enough
char movementCount = 0;
char movementPoints = 0;
unsigned char movingUnit = 0;
unsigned char arrowX = 0, arrowY = 0;

char attackingUnit = 0;

/* declarations */
// param1, param2, param3; return
void initialize();
void loadLevel(const char*); // level
void drawLevel(char); // direction
void drawHPBar(unsigned char, unsigned char, char); // x, y, value
void drawOverlay();
void drawArrow();
char addUnit(unsigned char, unsigned char, char, char); // x, y, player, type; unitIndex
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
char getRandomNumber(); // ; rand
char getRandomNumberLimit(char); // max; rand
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

const char testlevel[] PROGMEM =
{
	16, 11,
	PL, FO, PL, PL, PL, PL, PL, PL, PL, MO, MO, PL, MO, PL, PL, MO,
	PL, PL, MO, MO, MO, MO, BS, PL, PL, CT, MO, FO, PL, PL|UN1|PL2, BS|PL2, PL,
	PL, BS|PL1, PL, MO, PL, PL, FO, FO, PL, FO, FO, PL, PL, MO, MO, PL,
	PL, FO|UN1|PL1, PL, MO, PL, PL, PL, FO, PL, PL, FO, PL, FO, PL, PL, PL,
	PL, PL, MO, MO, PL, MO, MO, FO, PL, PL, PL, FO, PL, FO, PL, PL,
	PL, FO, PL|UN1|PL1, MO, PL, MO, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL,
	PL, PL, PL, PL, MO, PL, PL, PL, CT, MO, MO, PL, PL, FO, PL, PL,
	PL, FO, PL, PL, PL, PL, PL, FO, FO, PL, MO, FO, FO, FO, PL, PL,
	PL, PL, CT, PL, PL, FO, FO, PL, PL, PL, MO, FO, PL, PL, PL, PL,
	FO, FO|UN2|PL1, FO, PL, PL, PL, FO, FO, PL, MO, PL, PL, FO, CT, PL, PL|UN3|PL1,
	PL, MO, MO, FO, MO, PL, PL, PL, FO, BS, PL, MO, MO, MO, MO, PL
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
void main() {
	initialize();
	loadLevel(testlevel);
	FadeOut(0, true);
	drawLevel(LOAD_ALL);
	drawOverlay();
	mapCursorSprite(FALSE);
	MoveSprite(0,0,0,2,2);
	FadeIn(5, true);

	activePlayer = PL1;

	waitGameInput();

	while(1) {
		int cur = ReadJoypad(0);

		if(cur&BTN_LEFT)
			moveCamera(LOAD_LEFT);
		if(cur&BTN_RIGHT)
			moveCamera(LOAD_RIGHT);


		WaitVsync(1);
	}


	char ttt = FALSE;
	while(1) {
		WaitVsync(20);
		mapCursorSprite(ttt);
		ttt = !ttt;

	}






	while(1)
		WaitVsync(1);
	return;
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

void waitGameInput() {
	curInput = prevInput = ReadJoypad(JPPLAY(activePlayer));
	char asd = 0;
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
					for(char i = lastJumpedUnit+1; i != lastJumpedUnit; i = (i+1)%MAX_UNITS) {
						//TODO: make this only jump to not moved or attacked units
						if(unitList[i].isUnit && GETPLAY(unitList[i].info) == activePlayer) {
							moveCursorInstant(unitList[i].xPos, unitList[i].yPos);
							lastJumpedUnit = i;
							break;
						}
					}
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
							controlState = unit_attack;
							moveCursorInstant(cursorX, cursorY);
							attackingUnit = levelBuffer[cursorX][cursorY].unit;
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
					if(selectionVar != 0) {
						selectionVar--;
					}
				}
				if(curInput&BTN_DOWN && !(prevInput&BTN_DOWN)) {
					// move selection down
					if(selectionVar != 1) {
						selectionVar++;
					}
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
				
			case pause:
			
				break;
				
			case menu:
				
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
		for(y = 0; y < levelHeight; y++) {
			if(cameraX+MAX_VIS_WIDTH == levelWidth)
				bound = cameraX+MAX_VIS_WIDTH;
			else
				bound = cameraX+MAX_VIS_WIDTH+1;

			for(x = 0; x < levelWidth && x+cameraX < bound; x++) {
				DrawMap2(vramX+x*2, y*2, getTileMap(x+cameraX, y));
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
		Print(1, OVR1, getUnitName(unit->info));
		drawHPBar(1, OVR2, unit->hp);
		Print(1, OVR3, PSTR("MOV"));
		Print(6, OVR3, PSTR("ATK"));
		if(HASMOVED(unit->other))
			SetTile(4, OVR3, INTERFACE_RLIGHT);
		else
			SetTile(4, OVR3, INTERFACE_GLIGHT);
		if(HASATTACKED(unit->other))
			SetTile(9, OVR3, INTERFACE_RLIGHT);
		else
			SetTile(9, OVR3, INTERFACE_GLIGHT);
	}

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
		PrintByte(17, OVR2, movementPoints, 0);

	}
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
		PrintByte(12, OVR1, movementCount, 0);
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

	moveCameraInstant(normalizedCameraX);

	cursorX = x;
	cursorY = y;

	MoveSprite(0, (cursorX-cameraX)*16, cursorY*16, 2, 2);



}

void moveUnit() {
	char traverseX, traverseY, traverseI;
	char newX, newY;

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
			WaitVsync_(3);
		}
	}
	else if(ydir != 0) {
		tween = sy;
		while(tween != dy) {
			tween += ydir;
			MoveSprite(4, dx, tween, 2, 2);
			WaitVsync_(3);
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
char addUnit(unsigned char x, unsigned char y, char player, char type) {
	char ret;

	if(levelBuffer[x][y].unit != 0xFF)
	{
		ERROR("Unit already in space!");
		return 0xFF;
	}
	else if (unitFirstEmpty == 0xFF)
	{
		ERROR("Unit list fulL!");
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
		char i = unitFirstEmpty;
		while(i != unitFirstEmpty - 1)
		{
			if(!unitList[i].isUnit)
			{
				unitFirstEmpty = i;
				break;
			}
			if(i == MAX_UNITS-1)
				i = 0;
			else
				i++;
		}
		
		if(unitFirstEmpty == ret)
			unitFirstEmpty = 0xFF;
		
		return ret;
	}
}

// TODO: make a function that removes based on index too
void removeUnit(unsigned char x, unsigned char y) {
	if(levelBuffer[x][y].unit == 0xFF)
	{
		ERROR("No unit in that space");
	}
	else
	{
		unitList[levelBuffer[x][y].unit].isUnit = FALSE;
		//memset(&unitList[levelBuffer[x][y].unit], sizeof(unitList[levelBuffer[x][y].unit]), 0); //Zero out the unit struct.
		
		//if(unitFirstEmpty > (levelBuffer[x][y].unit + (levelBuffer[x][y].unit < unitFirstEmpty) * MAX_UNITS) ) //If this newly opened index is earlier than the last known index, change it accordingly.
		unitFirstEmpty = levelBuffer[x][y].unit;
		levelBuffer[x][y].unit = 0xFF; //Mark this grid buffer square as no unit.
	}
}

char getNextAttackableUnitIndex(char last) {
	int i = last+1;
	char range = getAttackRange(unitList[attackingUnit].info);
	char player = activePlayer == PL1 ? PL2 : PL1; // reverse the player
	if(last == -1) {
		i = 0;
		last = MAX_UNITS-1;
	}

	for(; i != last; i++) {
		if(i >= MAX_UNITS)
			i = 0;

		if(unitList[i].isUnit && GETPLAY(unitList[i].info) == player && MANH(unitList[i].xPos, unitList[i].yPos, unitList[attackingUnit].xPos, unitList[attackingUnit].yPos) <= range) {
			// if the unit belongs to the other player and is inside our attack range
			return i;
		}
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

void mapMovingUnitSprite() {
	const char* map;
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
	MoveSprite(4,(cursorX-cameraX)*16, cursorY*16, 2, 2);


}

void drawHPBar(unsigned char x, unsigned char y, char val) {
	if(val > 56)
		val = 56;
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

char getDamage(struct Unit* srcUnit, struct Unit* dstUnit) {
	char baseDamage;


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
	}

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

char getAttackRange(const char unit) {
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
	return 1;
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



		if(blinkMode) {
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
