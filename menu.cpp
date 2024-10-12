#include "menu.h"
#include <ti/getcsc.h>
#include <sys/util.h>
#include <graphx.h>
#include <keypadc.h>
#include <debug.h>
#include <time.h>

int run_menu() {
	gfx_FillScreen(222);
	gfx_SetColor(0);
	gfx_PrintStringXY("TI84Tris", 100, 100);
	gfx_PrintStringXY("1 -> Practice", 100, 120);
	gfx_PrintStringXY("2 -> 40L", 100, 140);
	gfx_PrintStringXY("3 -> Cheese Practice", 100, 160);

	kb_Scan();

	if (kb_Data[3] & kb_1) {
		return 1;
	}
	if (kb_Data[4] & kb_2) {
		return 2;
	}
	if (kb_Data[5] & kb_3) {
		return 3;
	}

	return 0;
}