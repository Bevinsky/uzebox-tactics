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

/* structs */
struct GridBufferSquare {
    char terrain;
    unsigned char unit; // 0xff for no unit
    unsigned char property; // 0xff for no property
};
struct Unit {
    char inUse;
    char type;
    char player;
    char hp;
    unsigned char xPos;
    unsigned char yPos;
};
struct Property {
	char owner;
	unsigned char xPos;
	unsigned char yPos;
};

/* defines */
#define LEVEL_HEIGHT 11
#define MAX_UNITS 40
#define MAX_PROPERTIES 20
#define TRUE 1
#define FALSE 0

#define BLINK_UNITS 0
#define BLINK_TERRAIN 1

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define ERROR(msg) \
	Print(0,0,PSTR(msg));\
	while(1)\
		WaitVsync(1);



// terrain types
#define PL	0x01 // plain
#define MO	0x02 // mountain
#define FO	0x03 // forest
#define CT	0x04 // city
#define BS	0x05 // base

// unit types
#define UN1 0x08
#define UN2 0x10
#define UN3 0x18
#define UN4 0x20
#define UN5 0x28

// players
#define PL1	0x80
#define PL2	0x40
#define NEU	0x00

// level data masks
#define TERRAIN_MASK 0b00000111
#define UNIT_MASK	 0b00111000
#define OWNER_MASK	 0b11000000

// stripe load directions
#define LOAD_LEFT   0xAA
#define LOAD_RIGHT  0xBB

/* globals */
char levelWidth, levelHeight;
char cursorX, cursorY;
char cameraX; // no need for y

char blinkCounter = 0;
char blinkState = BLINK_TERRAIN;
char blinkMode = FALSE;

const char* currentLevel;

// what is visible on the screen; 14 wide, 11 high, 2 loading columns on each side
struct GridBufferSquare screenBuffer[16][LEVEL_HEIGHT];

unsigned char lastEmptyUnit = 0; //this can change during a game...
unsigned char propertyCount = 0; //...and this cannot

struct Unit unitList[MAX_UNITS]; //is this enough?
struct Property propertyList[MAX_PROPERTIES]; // at most 20 properties...?

/* declarations */
// param1, param2, param3; return
void initialize();
void loadLevel(const char*); // level
void drawHPBar(unsigned char, unsigned char, char); // x, y, value
char addUnit(unsigned char, unsigned char, char, char); // x, y, player, type; unitIndex
void addProperty(unsigned char, unsigned char, char); // x, y, player
void preloadBuffer();
void loadStripe(unsigned char, unsigned char); // xSource, xDest
void moveCamera(char); // direction
char isInBufferArea(char, char); // x, y; isInBufferArea
const char* getTileMap(unsigned char, unsigned char); // x, y; tileMap

const char testlevel[] PROGMEM =
{
    14,
    PL, MO, FO, PL, MO, FO, PL, MO, FO, PL, MO, FO, PL, MO,
    CT|NEU, BS|NEU, CT|PL1, BS|PL1, CT|PL2, BS|PL2, CT|NEU, BS|NEU, CT|PL1, BS|PL1, CT|PL2, BS|PL2, CT|NEU, BS|NEU,
    PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL,
    PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL,
    PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL,
    PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL,
    PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL,
    PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL,
    PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL,
    PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL,
    PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL,
    PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL,
    PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL,
    PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL
};

/* main function */
void main() {
	initialize();
	//addUnit(3, 6, PL2, UN3);

	// i don't know what's going on but it seems like the RAM variables are
	// overlapping with VRAM, which is a fucking pain in the ass (as in,
	// makes it impossible to code)

	while(1) {
		unitList[0].hp++;
		unitList[1].hp++;
		unitList[2].hp++;
		unitList[3].hp++;
		unitList[4].hp++; // this cycles tiles on screen, wtf?
		WaitVsync(5);
	}
	//loadLevel(testlevel);
	//preloadBuffer();

	while(1)
		WaitVsync(1);
	/*while(1) {
		char dir = 1;
		WaitVsync(3);
		Screen.scrollX += dir; //this doesn't work the way i thought it would... test it
		PrintByte(2, VRAM_TILES_V-1, Screen.scrollX, 0);
		if(Screen.scrollX > 20)
			dir = -1;
		else if(Screen.scrollX < 0)
			dir = 1;
		drawHPBar(0, VRAM_TILES_V-3, Screen.scrollX%56);
	}*/
	return;
}


void initialize() {
	Screen.scrollHeight = 28;
	Screen.overlayHeight = 4;
	Screen.overlayTileTable = terrainTiles; // seems like it has to share the tiles, otherwise we can't use the fonts
	ClearVram();
	SetFontTilesIndex(TERRAINTILES_SIZE);
	SetTileTable(terrainTiles);
}

void loadLevel(const char* level) {
	char val, terr, owner, unit;
	unsigned int x, y; // i know i said this wasn't needed but there will be overflow on the array access otherwise

	levelWidth = pgm_read_byte(&level[0]);
	levelHeight = LEVEL_HEIGHT;
	currentLevel = level;
	cameraX = 0;
	lastEmptyUnit = 0;
	propertyCount = 0;
	// reset the unit list
	for(x = 0;x < MAX_UNITS;x++)
		unitList[x].inUse = FALSE;

	// loop y first because then we work in order. locality probably isn't an issue but eh.
	for(y = 0; y < levelHeight; y++) {
		for(x = 0; x < levelWidth; x++) {
			val = pgm_read_byte(&level[y*levelWidth+x+1]);
			terr = val & TERRAIN_MASK;
			owner = val & OWNER_MASK;
			unit = val & UNIT_MASK;
			if(unit != 0 && owner != NEU) {
				//this can be a unit
				addUnit(x, y, owner, unit);
			}
			if(terr == CT || terr == BS) {
				//it's a property, add it
				addProperty(x, y, owner);
			}
		}
	}
}

void preloadBuffer() {
	cameraX = 0; // reset the camera
	unsigned char x, lim;
	lim = MIN(levelWidth, 14);
	for(x = 0; x < lim; x++) {
		//load each column
		loadStripe(x, x); // source is the same as destination, since we are at the start of the buffer
	}
}

// loads a column into both buffer and vram
// source is the game index column index
// dest is the index of the buffer
void loadStripe(unsigned char xSource, unsigned char xDest) {
	unsigned char units[LEVEL_HEIGHT];
	unsigned char properties[LEVEL_HEIGHT];
	unsigned char y, i, terrain;
	const char* tilemap;
	// reset the lists to "no unit/property"
	for(y = 0; y < LEVEL_HEIGHT; y++) {
		units[y] = 0xff;
		properties[y] = 0xff;
	}

	if(xSource < 0 || xSource >= levelWidth) {
		ERROR("err 03")
	}
	if(xDest < 0 || xDest >= 17) {
		ERROR("err 04")
	}

	for(i = 0;i < lastEmptyUnit; i++) {
		if(unitList[i].inUse && unitList[i].xPos == xSource) {
			//unit is in the right column
			units[unitList[i].yPos] = i; // assign the unit for this row to the list
		}
	}
	for(i = 0; i < propertyCount; i++) {
		if(propertyList[i].xPos == xSource) {
			//property is in the right column
			properties[propertyList[i].yPos] = i; // assign the property for this row to the list
		}
	}

	for(y = 0; y < LEVEL_HEIGHT; y++) {
		terrain = pgm_read_byte(&currentLevel[y*levelWidth+xSource+1]) & TERRAIN_MASK;
		screenBuffer[xDest][y].terrain = terrain;
		screenBuffer[xDest][y].unit = units[y];
		screenBuffer[xDest][y].property = properties[y];
		tilemap = getTileMap(xDest, y);
		DrawMap2(xDest, y, tilemap);
	}

}

void addProperty(unsigned char x, unsigned char y, char player) {
	//this is only called at the start of a game, so we can just populate with the counter
	if(propertyCount == MAX_PROPERTIES) {
		//bad...
		ERROR("err 02")
	}
	propertyList[propertyCount].owner = player;
	propertyList[propertyCount].xPos = x;
	propertyList[propertyCount].yPos = y;
	propertyCount++;
}

char addUnit(unsigned char x, unsigned char y, char player, char type) {
	unsigned char i;
	for(i = 0; i<lastEmptyUnit;i++) {
		if(!unitList[i].inUse) {
			unitList[i].inUse = TRUE;
			unitList[i].hp = 100;
			unitList[i].player = player;
			unitList[i].type = type;
			unitList[i].xPos = x;
			unitList[i].yPos = y;
			return i;
		}
	}
	// if we got here, we passed the last used slot
	if(i == MAX_UNITS) {
		//this is bad...
		ERROR("err 01")
	}

	lastEmptyUnit = i+1;
	unitList[i].inUse = TRUE;
	unitList[i].hp = 100;
	unitList[i].player = player;
	unitList[i].type = type;
	unitList[i].xPos = x;
	unitList[i].yPos = y;
	return i;
}

// gets the tile map for a certain buffer coordinate
// NOT game coordinates!
const char* getTileMap(unsigned char x, unsigned char y) {
	unsigned char terrain, unitOwner, propertyOwner, unit, displayUnit;
	terrain = screenBuffer[x][y].terrain;
	if(screenBuffer[x][y].unit != 0xff) {
		unit = unitList[screenBuffer[x][y].unit].type;
		unitOwner = unitList[screenBuffer[x][y].unit].player;
	}
	else {
		unit = 0;
		unitOwner = 0;
	}
	if(screenBuffer[x][y].property != 0xff) {
		propertyOwner = propertyList[screenBuffer[x][y].property].owner;
	}
	else {
		propertyOwner = 0;
	}

	if(unit) { // if we have a unit to display, display it!
		// TODO: add exceptions for when we are moving a unit with sprites
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




	if(displayUnit) {
		// TODO: add unit tiles
		return map_mountain;
		/*switch(unit) {
		case UN1:
			//return;
			break;
		case UN2:
			//return;
			break;
		case UN3:
			//return;
			break;
		case UN4:
			//return;
			break;
		case UN5:
			//return;
			break;
		}*/
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
				return map_plain; //placeholder needed
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
				return map_plain; //placeholder needed
			}
		default:
			return map_plain; // placeholder needed
		}
	}
}

void drawHPBar(unsigned char x, unsigned char y, char val) {
	if(val > 56)
		val = 56;
	DrawMap2(x, y, hp_bar_base);
	x += 2;
	Fill(x, y, 7, 1, 0);
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
		SetTile(x,y,0);
	}

}

void testDraw() {
	for(char y = 0; y < LEVEL_HEIGHT; y++)
	{
		for(char x = 0; x < 14; x++)
		{
			switch(pgm_read_byte(&testlevel[y*pgm_read_byte(&testlevel[0])+x+1])&TERRAIN_MASK)
			{
				case PL:
					DrawMap2(2*x, 2*y, map_plain);
					break;
				case MO:
					DrawMap2(2*x, 2*y, map_mountain);
					break;
				case FO:
					DrawMap2(2*x, 2*y, map_forest);
					break;
				case CT:
					switch(pgm_read_byte(&testlevel[y*pgm_read_byte(&testlevel[0])+x])&OWNER_MASK)
					{
						case PL1:
							DrawMap2(2*x, 2*y, map_city_red);
							break;
						case PL2:
							DrawMap2(2*x, 2*y, map_city_blu);
							break;
						case NEU:
						default:
							DrawMap2(2*x, 2*y, map_city_neu);
						break;
					}
					break;
				case BS:
					switch(pgm_read_byte(&testlevel[y*pgm_read_byte(&testlevel[0])+x])&OWNER_MASK)
					{
						case PL1:
							DrawMap2(2*x, 2*y, map_base_red);
							break;
						case PL2:
							DrawMap2(2*x, 2*y, map_base_blu);
							break;
						case NEU:
						default:
							DrawMap2(2*x, 2*y, map_base_neu);
						break;
					}
					break;
				default:
					DrawMap2(2*x, 2*y, map_base_blu);
					break;
			}
		}
	}

}
