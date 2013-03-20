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

// convert our player value to controller value
#define JPPLAY(pl) ((pl) == PL2 ? 1 : 0)

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

/* globals */
unsigned char levelWidth, levelHeight;
unsigned char cursorX, cursorY; // absolute coords
unsigned char cameraX;
unsigned char vramX; // where cameraX points to in vram coords, wrapped on 0x1F

char blinkCounter = 0;
char blinkState = BLINK_UNITS;
char blinkMode = FALSE;

char cursorCounter = 0; //for cursor alternation
char cursorAlt = FALSE;

int curInput;
int prevInput;
unsigned char activePlayer;

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
char moveCamera(char); // direction
char moveCursor(char); // direction
char isInBufferArea(char, char); // x, y; isInBufferArea
const char* getTileMap(unsigned char, unsigned char); // x, y; tileMap
void waitGameInput();
void mapCursorSprite(char); // alternate
void redrawUnits();
void setBlinkMode(char); // on-off

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
	PL, FO, PL, MO, PL, MO, PL, PL, PL, PL, PL, PL, PL, PL, PL, PL,
	PL, PL, PL, PL, MO, PL, PL, PL, CT, MO, MO, PL, PL, FO, PL, PL,
	PL, FO, PL, PL, PL, PL, PL, FO, FO, PL, MO, FO, FO, FO, PL, PL,
	PL, PL, CT, PL, PL, FO, FO, PL, PL, PL, MO, FO, PL, PL, PL, PL,
	FO, FO, FO, PL, PL, PL, FO, FO, PL, MO, PL, PL, FO, CT, PL, PL,
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
	loadLevel(shortlevel);
	FadeOut(0, true);
	drawLevel(LOAD_ALL);
	mapCursorSprite(FALSE);
	MoveSprite(0,0,0,2,2);
	FadeIn(5, true);

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
}

void waitGameInput() {
	curInput = prevInput = ReadJoypad(JPPLAY(activePlayer));
	while(1) {
		curInput = ReadJoypad(JPPLAY(activePlayer));

		//drawOverlay();

		if(curInput&BTN_A && !(prevInput&BTN_A)) {
			if(levelBuffer[cursorX][cursorY].unit != 0xff && GETPLAY(unitList[levelBuffer[cursorX][cursorY].unit].info) == activePlayer) {
				// enter select unit mode if there's a unit here and it belongs to us
			}
		}
		if(curInput&BTN_X && !(prevInput&BTN_X)) {
			// toggle blink mode
			setBlinkMode(!blinkMode);
		}
		if(curInput&BTN_LEFT) {
			// move cur left
			moveCursor(DIR_LEFT);
			Print(0,OVR1, PSTR("LEFT"));
		}
		if(curInput&BTN_RIGHT) {
			// move cur right
			moveCursor(DIR_RIGHT);
			Print(0,OVR1, PSTR("Right"));
		}
		if(curInput&BTN_UP) {
			// move cur up
			moveCursor(DIR_UP);
			Print(0,OVR1, PSTR("Up"));
		}
		if(curInput&BTN_DOWN) {
			// move cur down
			moveCursor(DIR_DOWN);
			Print(0,OVR1, PSTR("DOWN"));
		}

		PrintByte(2,OVR2,cursorX,0);
		PrintByte(2,OVR3, cursorY, 0);

		prevInput = curInput;
		WaitVsync_(1);
	}
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
				PrintByte(10, OVR1, cameraX, 0);
				PrintByte(10, OVR2, vramX, 0);
			}
			v++;
		}
	}
	PrintByte(3, OVR4, v, 0);
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
				return ret;
			}
			i++;
		}
		unitFirstEmpty = 0xFF;
		return 0xFF;
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

void WaitVsync_(char count) {
	// this is used for periodicals like blink and cursor alternation
	// call this instead of WaitVsync to make sure that periodicals
	// get called even if we are doing something function-locked
	while(count > 0) {
		WaitVsync(1); // wait only once

		// insert periodicals here
		//TODO: make sure the correct periodicals only fire when they are supposed to
		if(cursorCounter >= 40) {
			cursorCounter = 0;
			mapCursorSprite(cursorAlt);
			cursorAlt = !cursorAlt;
		}
		cursorCounter++;

		//checkBlinkState();

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




		count--;
	}

}
