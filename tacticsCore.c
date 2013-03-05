/* lib includes */
#include <avr/io.h>
#include <stdlib.h>
#include <math.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <uzebox.h>
#include "defines.h"


/* data includes */

#include "res/fontmap.inc"
#include "res/tiles.inc" 

/* globals */

/* defines */
#define PL1	0b10000000
#define PL2	0b01000000
#define NEU	0b00000000

#define PL	0x1 // plain
#define MO	0x2 // mountain
#define FO	0x3 // forest
#define CT	0x4 // city
#define BS	0x5 // base

#define TERRAIN_MASK	0b00000111
#define UNIT_MASK	0b00111000
#define OWNER_MASK	0b11000000

/* declarations */
void initialize();

const char testlevel[] PROGMEM = 
{
    14,
    PL, MO, FO, PL, MO, FO, PL, MO, FO, PL, MO, FO, PL, MO, FO, 
    CT|NEU, BS|NEU, CT|PL1, BS|PL1, CT|PL2, BS|PL2, CT|NEU, BS|NEU, CT|PL1, BS|PL1, CT|PL2, BS|PL2, CT|NEU, BS|NEU, CT|PL1,
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
};


/* main function */
void main() {
	initialize();

	for(char y = 0; y < 12; y++)
	{
		for(char x = 0; x < 14; x++)
		{
			switch(testlevel[y*testlevel[0]+x+1]&TERRAIN_MASK)
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
					switch(testlevel[y*testlevel[0]+x+1]&OWNER_MASK)
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
					switch(testlevel[y*testlevel[0]+x]&OWNER_MASK)
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
					DrawMap2(2*x, 2*y, map_plain);
					break;
			}
		}
	}



	while(1) {
		WaitVsync(1);
	}
	return 0;
}


void initialize() {
	ClearVram();
	SetFontTilesIndex(TERRAINTILES_SIZE);
	SetTileTable(terrainTiles);
}













