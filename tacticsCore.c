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
    unsigned char unit; // index to Unit array; 0xff for no unit
    unsigned char info; // terrain and player bitfield
};
struct Unit {
    char isUnit;
    char info; // unit type and player bitfield
    char hp;
    unsigned char xPos;
    unsigned char yPos;
};

/* defines */
#define LEVEL_HEIGHT 11
#define MAX_UNITS 40
#define MAX_PROPERTIES 20
#define MAX_LEVEL_WIDTH 30
#define MAX_VIS_WIDTH 14
#define TRUE 1
#define FALSE 0

#define BLINK_UNITS 0
#define BLINK_TERRAIN 1

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define ERROR(msg) \
	Print(4,0,PSTR(msg));\
	while(1)\
		WaitVsync(1);

// level data masks
#define TERRAIN_MASK 0b00000111
#define UNIT_MASK	 0b00111000
#define OWNER_MASK	 0b11000000

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
#define LOAD_ALL	0x1
#define LOAD_LEFT   0x2
#define LOAD_RIGHT  0x3

//overlay lines
#define OVR1 (VRAM_TILES_V-4)
#define OVR2 (VRAM_TILES_V-3)
#define OVR3 (VRAM_TILES_V-2)
#define OVR4 (VRAM_TILES_V-1)

/* globals */
unsigned char levelWidth, levelHeight;
unsigned char cursorX, cursorY;
unsigned char cameraX; // no need for y
unsigned char vramX; // where cameraX is in vram coords

char blinkCounter = 0;
char blinkState = BLINK_TERRAIN;
char blinkMode = FALSE;

const char* currentLevel;

// what is visible on the screen; 14 wide, 11 high, 2 loading columns on each side
struct GridBufferSquare levelBuffer[MAX_LEVEL_WIDTH][LEVEL_HEIGHT];

unsigned char unitFirstEmpty = 0;
unsigned char unitListStart = 0;
unsigned char unitListEnd = 0;

struct Unit unitList[MAX_UNITS]; //is this enough?

/* declarations */
// param1, param2, param3; return
void initialize();
void loadLevel(const char*); // level
void drawLevel(char); // direction
void drawHPBar(unsigned char, unsigned char, char); // x, y, value
char addUnit(unsigned char, unsigned char, char, char); // x, y, player, type; unitIndex
void removeUnit(unsigned char, unsigned char); // x, y
void moveCamera(char); // direction
char isInBufferArea(char, char); // x, y; isInBufferArea
const char* getTileMap(unsigned char, unsigned char); // x, y; tileMap

const char testlevel[] PROGMEM =
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
    PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, FO, MO,
    PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, FO, MO,
    PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, FO, MO,
    PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL, FO, MO
};

/* main function */
void main() {
	initialize();
	loadLevel(testlevel);
	FadeOut(0, true);
	drawLevel(LOAD_ALL);
	FadeIn(9, true);

	int prev, cur;

	cur = ReadJoypad(0);
	prev = cur;

	while(1) {
		cur = ReadJoypad(0);

		PrintByte(1, OVR1, cameraX, 0);
		PrintByte(1, OVR2, cur, 0);
		PrintByte(1, OVR3, prev, 0);

		if(cur&BTN_LEFT){
			moveCamera(LOAD_LEFT);
		}
		else if(cur&BTN_RIGHT){
			moveCamera(LOAD_RIGHT);
		}



		prev = cur;
		WaitVsync(1);
	}

	/*char right = 1;
	while(1) {
		PrintByte(3, OVR1,Screen.scrollX, 0);
		PrintByte(0, OVR2, right, 0);
				PrintByte(2, OVR3, cameraX, 0);
				PrintByte(2, OVR4, vramX, 0);

		if(right) {
			if(Screen.scrollX % 16 == 0) {
				drawLevel(LOAD_RIGHT);
				cameraX++;
				vramX = (vramX+2)&0x1F;
				if(cameraX == levelWidth-MAX_VIS_WIDTH)
					right = 0;
			}
			Screen.scrollX++;
		}
		else {

			if(Screen.scrollX % 16 == 0) {
				drawLevel(LOAD_LEFT);
				cameraX--;
				vramX = (vramX-2)&0x1F;
				if(cameraX == 0) {
					right = 1;
				}
			}
			Screen.scrollX--;
		}


		WaitVsync(3);
	}*/


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
}

void loadLevel(const char* level) {
	char val, terr, owner, unit;
	unsigned int x, y; // i know i said this wasn't needed but there will be overflow on the array access otherwise

	levelWidth = pgm_read_byte(&level[0]);
	levelHeight = LEVEL_HEIGHT;
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
			val = pgm_read_byte(&level[y*levelWidth+x+1]);
			terr = val & TERRAIN_MASK;
			owner = val & OWNER_MASK;
			unit = val & UNIT_MASK;
			if(unit != 0 && owner != NEU) {
				//this can be a unit
				addUnit(x, y, owner, unit);
			}
			levelBuffer[x][y].info = terr | owner;
		}
	}
}

void drawLevel(char dir) {
	char x, y;
	switch(dir){
	case LOAD_ALL:
		for(y = 0; y < levelHeight; y++) {
			for(x = 0; x < levelWidth; x++) {
				DrawMap2(x*2, y*2, getTileMap(x, y));
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

void moveCamera(char dir) {
	switch(dir) {
	case LOAD_LEFT:
		if(cameraX == 0) {
			//nothing happens
			return;
		}
		drawLevel(dir);
		//animate the screen movement
		while(1) {
			Screen.scrollX--;
			WaitVsync(1);
			if(Screen.scrollX % 16 == 0)
				break;
		}
		cameraX--;
		vramX = (vramX-2)&0x1F;
		// TODO: sprite movement (cursors, moving units, etc)
		break;
	case LOAD_RIGHT:
		if(cameraX == levelWidth-MAX_VIS_WIDTH) {
			return;
		}
		drawLevel(dir);
		while(1) {
			Screen.scrollX++;
			WaitVsync(1);
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

}

//TODO: MAX units *per* team, not total units
char addUnit(unsigned char x, unsigned char y, char player, char type) {
	char ret;
	if(levelBuffer[x][y].unit != 0xFF)
	{
		ERROR("Unit already in space!");
		return 0xff;
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
	}
	return ret;
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
		
		if(unitFirstEmpty > levelBuffer[x][y].unit) //If this newly opened index is earlier than the last known index, change it accordingly.
			unitFirstEmpty = levelBuffer[x][y].unit;
		levelBuffer[x][y].unit = 0xFF; //Mark this grid buffer square as no unit.
	}
}
		

// gets the tile map for a certain game coordinate
const char* getTileMap(unsigned char x, unsigned char y) {
	unsigned char terrain, unitOwner, propertyOwner, unit, displayUnit;
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
