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
    char owner;
    char unit; // 0xff for no unit
};
struct Unit {
    char inUse;
    char type;
    char player;
    char hp;
    char xPos;
    char yPos;
};


/* globals */
char levelWidth, levelHeight;

// the complete level definition
struct GridBufferSquare levelBuffer[40][14]; // the max level width has to be hard coded

// what is visible on the screen; 15 wide, 14 high, 2 loading columns on each side
struct GridBufferSquare screenBuffer[17][14]; // should this be pointers to levelBuffer items, perhaps?

struct Unit unitList[40]; //is this enough?

/* defines */
#define PL1	0x80
#define PL2	0x40
#define NEU	0x00

#define PL	0x01 // plain
#define MO	0x02 // mountain
#define FO	0x03 // forest
#define CT	0x04 // city
#define BS	0x05 // base

#define TERRAIN_MASK 0b00000111
#define UNIT_MASK	 0b00111000
#define OWNER_MASK	 0b11000000

/* declarations */
// param1, param2, param3; return
void initialize();
void loadLevel(const char*); // level
char addUnit(char, char, char, char); // x, y, player, type; unitIndex

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

	for(char y = 0; y < 12; y++)
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

	Print(0,VRAM_TILES_V-4,PSTR("Infantry"));
	Print(0,VRAM_TILES_V-3,PSTR("HP a shit load"));

	while(1) {
		char dir = 1;
		WaitVsync(10);
		Screen.scrollX += dir; //this doesn't work the way i thought it would... test it
		PrintByte(2, VRAM_TILES_V-1, Screen.scrollX, 0);
		if(Screen.scrollX > 20)
			dir = -1;
		else if(Screen.scrollX < 0)
			dir = 1;
	}
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
	int x, y; // i know i said this wasn't needed but there will be overflow on the array access otherwise

	levelWidth = pgm_read_byte(&level[0]);
	levelHeight = 14;

	/*
	if(levelWidth >= 40) {
		//this shouldn't be allowed to happen
	}
	*/

	// loop y first because then we work in order. locality probably isn't an issue but eh.
	for(y = 0; y < levelHeight; y++) {
		for(x = 0; x < levelWidth; x++) {
			val = pgm_read_byte(&level[y*levelWidth+x+1]);
			terr = val & TERRAIN_MASK;
			owner = val & OWNER_MASK;
			unit = val & UNIT_MASK;
			if(unit != 0 && owner != NEU) {
				//this can be a unit
				switch(unit) {
				// TODO: insert code to add a unit for that player on that square
				// TODO: be sure to set the unit variable to the unit list index!
				default:
					break;
				}
			}
			else {
				unit = 0xff; // no unit
			}
			// TODO: would it be better to store terrain and owner in the same variable?
			levelBuffer[x][y].terrain = terr;
			levelBuffer[x][y].owner = owner;
			levelBuffer[x][y].unit = unit;
		}
	}
}
