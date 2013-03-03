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


/* globals */



/* declarations */
void initialize();




/* main function */
int main() {
	initialize();

	Print(0,0,PSTR("ABCDEFGHIJKLMNOPQRSTUV"));

	while(1) {
		WaitVsync(1);
	}
	return 0;
}


void initialize() {
	ClearVram();
	SetTileTable(font_map);
	SetFontTilesIndex(0);


}













