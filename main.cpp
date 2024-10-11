#include <ti/getcsc.h>
#include <sys/util.h>
#include <graphx.h>
#include <keypadc.h>
#include <debug.h>
#include <time.h>

#define max(a,b)            (((a) > (b)) ? (a) : (b))
#define min(a,b)            (((a) < (b)) ? (a) : (b))

const uint8_t MINO = 8;
const uint24_t DEFAULT_POS = 192;
const uint8_t DEFAULT_BAG[8] = {0,1,2,3,4,5,6,0};
const uint8_t DAS = 2; // ARR = 0, if you dont like this then you're bad at game


// settings related to gravity

const uint24_t GRAVITY = 15;
const uint24_t LOCKDOWN_DELAY = 60;
const uint16_t LOCKDOWN_TIMES = 10;

const uint16_t SRS_ROT[28] = {
	1632, 1632, 1632, 1632, //O
	240, 17476, 3840, 8738, //I
	3648, 19520, 19968, 17984, //T
	3712, 50240, 11776, 17504, //L
	3616, 17600, 36352, 25664, //J
	1728, 35904, 27648, 17952, //S
	3168, 19584, 50688, 9792 //Z
};

const int8_t SRS_KICK[4][10] = {
	{0, 0, -1, 0, -1, -1, 0, 2, -1, 2},
	{0, 0, 1, 0, 1, 1, 0, -2, 1, -2},
	{0, 0, 1, 0, 1, -1, 0, 2, 1, 2},
	{0, 0, -1, 0, -1, 1, 0, -2, -1, -2},
};

const int8_t I_SRS_KICK[4][10] = {
	{0, 0, 1, 0, -2, 0, 1, -2, -2, 1},
	{0, 0, 2, 0, -1, 0, 2, 1, -1, -2},
	{0, 0, -1, 0, 2, 0, -1, 2, 2, -1},
	{0, 0, -2, 0, 1, 0, -2, -1, 1, 2},
};

const uint8_t mino_colors[8] = {
	222, 230, 63, 144, 226, 17, 7, 224
};

const int8_t offsets[14] = {6, -4, 6, 0, 2, -4, 2, -4, 2, -4, 2, -4, 2, -4};

const uint16_t attack_table[29] = 
{	0, 100, 200, 400, 
	200, 400, 600, 
	1000,
	100,
	0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4
};

// 0 single, double, triple, quad
// 4 tss, tsd, tst
// 7 pc
// 8 b2b
// 9 c1 ... c20
// 29

bool airborne = true;

uint24_t gravity_time = 0;
uint24_t lockdown_time = 0;

uint16_t lockdown_times_remaining = LOCKDOWN_TIMES;

uint8_t board[40][10];

int16_t active[3] = {500, 0, 0};

int16_t prev_shadow[3] = {500, 0, 0};

uint8_t current_bag[8] = {0, 0, 0, 0, 0, 0, 0, 7};
uint8_t next_bag[7] = {8, 0, 0, 0, 0, 0, 0};

// 8th element of current_bag is the bag position.
// next_bag is only used for preview purposes.

uint8_t rotation = 0;

uint16_t held = 0;

uint24_t held_time[2] = {};

// these are held inputs, not held pieces.

uint8_t held_mino = 8;

bool can_fit_mino(int16_t x, int16_t y) {
	if ((x >= 10) | (x < 0)) return false;
	if ((y >= 40) | (y < 0)) return false;

	return (board[y][x] <= 10);
}

bool can_fit_tetr(int16_t x, int16_t y, int8_t state, int8_t rot) {
	bool flag = false;

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			if ((SRS_ROT[state * 4 + rot] & (1 << (i*4+j))) != 0) {
				//dbg_printf("x:%d y:%d\n", x+j, y+i);
				if (!can_fit_mino(x+j, y+i)) {
					flag = true;
					break;
				}
			}
		}
		if (flag) {
			break;
		}
	}
	return !flag;
}

void initialize_graphics() {
	gfx_Begin();
	gfx_SetDrawBuffer();

	// draw the background and board
	gfx_SetColor(0);
	gfx_FillRectangle(118, 38, 84, 164);
	gfx_SetColor(255);
	gfx_FillRectangle(120, 40, 80, 160);

	//draw the NEXT queue boxes

	for (int i = 0; i < 5; i++) {
		gfx_SetColor(0);
		gfx_FillRectangle(200, 38 + 24*i, 44, 28);
		gfx_SetColor(255);
		gfx_FillRectangle(202, 40 + 24*i, 40, 24);
	}

	//draw the HOLD queue box

	gfx_SetColor(0);
	gfx_FillRectangle(78, 38, 42, 28);
	gfx_SetColor(255);
	gfx_FillRectangle(80, 40, 38, 24);

}

void fill_bag(uint8_t to_fill[]) {
	for (int i = 0; i < 7; i++) to_fill[i] = DEFAULT_BAG[i];

	// Fisher-Yates random permutation generator

	for (int i = 0; i < 6; i++) {
		uint8_t j = i + random() % (7-i);
		// sad no xor swapping because swapping with self results in 0 :(
		uint8_t temp = to_fill[i];
		to_fill[i] = to_fill[j];
		to_fill[j] = temp;
	}
}

void update_seven_bag() {

	if (next_bag[0] == 8) {
		fill_bag(next_bag);
	}

	if (active[0] == 500) {

		//bag is out, create it again
		
		if (current_bag[7] == 7) {
			
			for (int i = 0; i < 7; i++) {
				current_bag[i] = next_bag[i];
			}

			fill_bag(next_bag);
			current_bag[7] = 0;
		}


		active[0] = DEFAULT_POS%10;
		active[1] = DEFAULT_POS/10; 
		active[2] = current_bag[current_bag[7]];
		
		if (active[2] <= 1) {
			active[0]++;
		}

		rotation = 0;

		current_bag[7]++;
	}
}

 void clear_lines() {

 	uint8_t passed = 39;

 	uint8_t cleared = 0;

 	for (int i = 39; i > 0; i--) {
 		bool full = true;
 		bool unfull = true;
 		for (int j = 0; j < 10; j++) {
 			if (board[i][j] <= 10) {
 				full = false;
 			} else {
 				unfull = false;
 			}
 		}
 		if (full) {
 			for (int j = 0; j < 10; j++) {
 				board[i][j] = 0;
 			}
 			cleared++;
 		} else {
 			if (unfull) continue;
 			for (int j = 0; j < 10; j++) {
 				uint8_t temp = board[i][j];
 				board[i][j] = board[passed][j];
 				board[passed][j] = temp;
 			}
 			passed--;
 		}
  	}

	gfx_SetColor(255);
	gfx_FillRectangle(120, 40, 80, 160);  	

	gfx_SetColor(0);

  	for (int i = 20; i < 40; i++) {
		for (int j = 0; j < 10; j++) {
			if (((board[i][j] > 0) & (board[i][j] <= 7)) | (board[i][j] > 10) | (board[i][j] == 9)) {

				if (board[i][j] <= 7) {
					gfx_SetColor(mino_colors[board[i][j]]);
				} else if (board[i][j] == 9) {
					gfx_SetColor(mino_colors[0]);
				} else {
					gfx_SetColor(mino_colors[board[i][j] - 10]);
				}

				gfx_FillRectangle(120+(MINO*j), 40+(MINO*(i-20)), MINO, MINO);
			}
		}
	}

 }

 void draw_preview(bool erase) {

	for (int i = 0; i < 5; i++) {

		uint8_t mino = 0;

		if ((i + current_bag[7]) < 7) {
			mino = current_bag[i + current_bag[7]];
		} else {
			mino = next_bag[i + current_bag[7] - 7];
		}

		if (erase) {
			gfx_SetColor(255);
		} else {
			gfx_SetColor(mino_colors[mino + 1]);
		}

		uint16_t mino_state = SRS_ROT[4 * mino];

		for (int k = 0; k < 4; k++) {
			for (int j = 0; j < 4; j++) {
				if ((mino_state & (1 << (k*4+j))) != 0) {
					gfx_FillRectangle(200 + (MINO * j) + offsets[2*mino], 40 + (MINO * k) + 24 * i + offsets[2*mino +1], MINO, MINO);
				}
			}
		}
	}
}

void cycle_mino() {

	draw_preview(true);

	active[0] = 500;

	update_seven_bag();

	draw_preview(false);

	clear_lines();

}

void apply_gravity() {

	if (can_fit_tetr(active[0], active[1] + 1, active[2], rotation)) {
		gravity_time++;
		if (gravity_time >= GRAVITY) {
			gravity_time -= GRAVITY;
			active[1]++;
		}
		airborne = true;
	} else {
		lockdown_time++;
		if ((lockdown_times_remaining <= 0) | (gravity_time >= LOCKDOWN_DELAY)) {
			for (int i = 0; i < 4; i++) {
				for (int j = 0; j < 4; j++) {
					if ((SRS_ROT[active[2] * 4 + rotation] & (1 << (i*4+j))) != 0) {
						board[active[1] + i][active[0] + j] = active[2] + 11;
					}
				}
			}
			cycle_mino();
			active[0] = 500;
			lockdown_times_remaining = LOCKDOWN_TIMES;
			gravity_time = 0;
			lockdown_time = 0;
		}
		airborne = false;
	}
}


void draw_hold(bool erase) {

	if (held_mino == 8) return;

	if (erase) {
		gfx_SetColor(255);
	} else {
		gfx_SetColor(mino_colors[held_mino + 1]);
	}

	uint16_t mino_state = SRS_ROT[4 * held_mino];

	for (int k = 0; k < 4; k++) {
		for (int j = 0; j < 4; j++) {
			if ((mino_state & (1 << (k*4+j))) != 0) {
				gfx_FillRectangle(80+(MINO*j)+offsets[2*held_mino], 40+(MINO*k)+offsets[2*held_mino+1], MINO, MINO);
			}
		}
	}
}



void rotate_piece(uint8_t dir) {

	uint8_t r_new = (rotation + 2*dir + 1)%4;

	for (int i = 0; i < 5; i++) {
		uint8_t access = ((dir == 0) ? rotation : r_new);
		int8_t kickx = SRS_KICK[access][i*2];
		int8_t kicky = SRS_KICK[access][i*2+1];
		if (active[2] == 1) {
			kickx = I_SRS_KICK[access][i*2];
			kicky = I_SRS_KICK[access][i*2+1];
		}
		if (dir == 1) {
			kickx*=-1; kicky*=-1;
		}
		if (can_fit_tetr(active[0] + kickx, active[1] + kicky, active[2], r_new)) {
			rotation = r_new;
			active[0] += kickx;
			active[1] += kicky;
			break;
		}
	}

	return;

}

void draw_shadow(bool erase) {

	int8_t f_dist = 0;

	while (can_fit_tetr(active[0], active[1]+f_dist, active[2], rotation)) {
		f_dist++;
	}
	f_dist--;

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			if ((SRS_ROT[active[2] * 4 + rotation] & (1 << (i*4+j))) != 0) {
				board[active[1] + i + f_dist][active[0] + j] = ((erase) ? 0 : 9);
			}
		}
	}

}

void cycle_shadow() {

	gfx_SetColor(255);

	if (prev_shadow[0] != 500) {
		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				if ((SRS_ROT[prev_shadow[2]] & (1 << (i*4+j))) != 0) {
					board[prev_shadow[1] + i][prev_shadow[0] + j] = 0;
					gfx_FillRectangle(120+(MINO*(prev_shadow[0] + j)), 40+(MINO*(prev_shadow[1] + i-20)), MINO, MINO);
				}
			}
		}
	}

	int8_t f_dist = 0;

	while (can_fit_tetr(active[0], active[1]+f_dist, active[2], rotation)) {
		f_dist++;
	}
	f_dist--;

	gfx_SetColor(mino_colors[0]);

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			if ((SRS_ROT[active[2] * 4 + rotation] & (1 << (i*4+j))) != 0) {
				board[active[1] + i + f_dist][active[0] + j] = 0;
				gfx_FillRectangle(120+(MINO*(active[0] + j)), 40+(MINO*(active[1] + i + f_dist - 20)), MINO, MINO);
			}
		}
	}

	prev_shadow[0] = active[0];
	prev_shadow[1] = active[1] + f_dist;
	prev_shadow[2] = (active[2] * 4) + rotation;


}

void take_inputs() {

	int16_t mino_state = SRS_ROT[active[2] * 4 + rotation];

	bool redraw_shadow = false;

	kb_Scan();

	if ((kb_Data[7] & kb_Up) != 0) {
		if (!(held & 1)) {
			if (!airborne) lockdown_times_remaining--;
			rotate_piece(0);
			held |= 1;
			redraw_shadow = true;
		}
	} else {
		held &= ~1;
	}
	if ((kb_Data[7] & kb_Down) != 0) {
		if (!(held & 2)) {
			if (!airborne) lockdown_times_remaining--;
			rotate_piece(1);
			held |= 2;
			redraw_shadow = true;
		}
	} else {
		held &= ~2;
	}
	if ((kb_Data[7] & kb_Left) != 0) {

		uint8_t repeat_times = 0;
		if (held_time[0] == 0) {
			repeat_times = 1;
		} else if (held_time[0] > DAS) {
			repeat_times = 100;
		}
		while ((repeat_times > 0) & (can_fit_tetr(active[0]-1, active[1], active[2], rotation))) {
			if (!airborne) lockdown_times_remaining--;
			active[0]--;
			repeat_times--;
			redraw_shadow = true;
		}
		held_time[0]++;
	} else {
		held_time[0] = 0;
	}
	if ((kb_Data[7] & kb_Right) != 0) {
		uint8_t repeat_times = 0;
		if (held_time[1] == 0) {
			repeat_times = 1;
		} else if (held_time[1] > DAS) {
			repeat_times = 100;
		}
		while ((repeat_times > 0) & (can_fit_tetr(active[0]+1, active[1], active[2], rotation))) {
			if (!airborne) lockdown_times_remaining--;
			active[0]++;
			repeat_times--;
			redraw_shadow = true;
		}
		held_time[1]++;
	} else {
		held_time[1] = 0;
	}
	if (((kb_Data[3] & kb_GraphVar) != 0)) {
		if (!(held&4)) {

			while (can_fit_tetr(active[0], active[1]+1, active[2], rotation)) {
				active[1]++;
			}

			for (int i = 0; i < 4; i++) {
				for (int j = 0; j < 4; j++) {
					if ((mino_state & (1 << (i*4+j))) != 0) {
						board[active[1] + i][active[0] + j] = active[2] + 11;
					}
				}
			}

			held |= 4;

			cycle_mino();

			prev_shadow[0] = 500;
			redraw_shadow = true;

		}
	} else {
		held &= ~4;
	}
	if ((kb_Data[1] & kb_Del) != 0) {
		if (!(held&8)) {
			draw_hold(true);
			if (held_mino == 8) {
				held_mino = active[2];
				cycle_mino();
			} else {
				uint8_t temp = held_mino;
				held_mino = active[2];
				active[2] = temp;

				// spawn in mino

				active[0] = DEFAULT_POS%10;
				active[1] = DEFAULT_POS/10;

				if (active[2] <= 1) {
					active[0]++;
				}

			}
			draw_hold(false);
			redraw_shadow = true;
			held |= 8;
			rotation = 0;
		}
	} else {
		held &= ~8;
	}
	if ((kb_Data[1] & kb_2nd) != 0) {
		while (can_fit_tetr(active[0], active[1]+1, active[2], rotation)) {
			active[1]++;
		}
	}
	if ((kb_Data[1] & kb_Mode) != 0) {
		if (!(held & 16)) {
			if (!airborne) lockdown_times_remaining--;
			if (can_fit_tetr(active[0], active[1], active[2], (rotation+2)%4 )) {
				rotation+=2; rotation %=4;
			}
			held |= 16;
			redraw_shadow = true;
		}
	} else {
		held &= ~16;
	}

	if (redraw_shadow) {
		cycle_shadow();
	}
}


void erase_active() {
	gfx_SetColor(255);

	for (int i = 20; i < 40; i++) {
		for (int j = 0; j < 10; j++) {
			if ((board[i][j] > 0) & (board[i][j] <= 7)) {
				gfx_FillRectangle(120+(MINO*(j)), 40+(MINO*(i-20)), MINO, MINO);
				board[i][j] = 0;
			}
		}
	}
}

void draw_active() {

	if (active[0] != 500) {
		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				if ((SRS_ROT[active[2] * 4 + rotation] & (1 << (i*4+j))) != 0) {
					board[active[1] + i][active[0] + j] = active[2] + 1;
					//dbg_printf("x:%d y:%d\n", active[1]+i, active[0]+j);
				}
			}
		}
	}

	gfx_SetColor(0);

	for (int i = 20; i < 40; i++) {
		for (int j = 0; j < 10; j++) {
			if (((board[i][j] > 0) & (board[i][j] <= 7)) | (board[i][j] > 10) | (board[i][j] == 9)) {

				if (board[i][j] <= 7) {
					gfx_SetColor(mino_colors[board[i][j]]);
				} else if (board[i][j] == 9) {
					gfx_SetColor(mino_colors[0]);
				} else {
					gfx_SetColor(mino_colors[board[i][j] - 10]);
				}

				gfx_FillRectangle(120+(MINO*j), 40+(MINO*(i-20)), MINO, MINO);
			}
		}
	}

	gfx_BlitBuffer();
}



int main(void) {

	srandom(time(NULL));

	initialize_graphics();

	update_seven_bag();

	draw_preview(false);

	gfx_PrintStringXY("v1.1", 10, 10);

	while (kb_Data[6] != kb_Enter) {

		// start clock

		clock_t frame_start = clock();
		
		// erase the active piece

		erase_active();

		// do movements

		//dbg_printf("x %d : y %d : r %d\n", active[0], active[1], active[2]);

		take_inputs();

		// draw the active piece

		//`apply_gravity();

		draw_active();


		// regulate framerate

		clock_t frame_time = clock() - frame_start;

		while (frame_time < CLOCKS_PER_SEC/30) {
			frame_time = clock() - frame_start;
        }

	}

	gfx_End();

	return 0;
}

