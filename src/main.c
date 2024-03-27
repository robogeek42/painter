/*
  vim:ts=4
  vim:sw=4
*/
#include "colmap.h"

#include "agon/vdp_vdu.h"
#include "agon/vdp_key.h"
#include <mos_api.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>
#include "util.h"

int gMode = 8; 
int gScreenWidth = 320;
int gScreenHeight = 240;

#define BITS_UP 1
#define BITS_RIGHT 2
#define BITS_DOWN 4
#define BITS_LEFT 8
#define BU 1
#define BR 2
#define BD 4
#define BL 8

bool debug = false;

//------------------------------------------------------------
// Configuration vars
int key_wait = 1;
int anim_speed = 10;
//------------------------------------------------------------


typedef struct {
	int x;
	int y;
} Position;

// Position of player
Position pos = {0,0};

typedef struct {
	Position A;
	Position B;
	bool horiz;
	int valid_next_keyA[3];
	int valid_next_dirA[3];
	int valid_next_keyB[3];
	int valid_next_dirB[3];
} PathSegment;

PathSegment paths[] = {
	// A          B         horiz     
	/* 0 */ { {100,100}, {200,100}, true,  { BD,  0, 0 }, { 3, -1, -1 }, { BD, BR,  0 }, {  1,  4, -1 } },
	/* 1 */ { {200,100}, {200,150}, false, { BL, BR, 0 }, { 0,  4, -1 }, { BL,  0,  0 }, {  2, -1, -1 } },
	/* 2 */ { {200,150}, {100,150}, true,  { BU,  0, 0 }, { 1, -1, -1 }, { BU,  0,  0 }, {  3, -1, -1 } },
	/* 3 */ { {100,150}, {100,100}, false, { BR,  0, 0 }, { 2, -1, -1 }, { BR,  0,  0 }, {  0, -1, -1 } },
	/* 4 */ { {200,100}, {250,100}, true,  { BD, BL, 0 }, { 1,  0, -1 }, {  0,  0,  0 }, { -1, -1, -1 } },
};
int num_segments = 5;
int current_ps = 0;

// counters
clock_t key_wait_ticks;
clock_t anim_ticks;

int player_frame = 0;

void wait();
void game_loop();
void load_images();
void create_sprites();
void draw_player();
void draw_path_segment( PathSegment *pps );
void move_along_path_segment( PathSegment *pps, uint8_t dir);

void wait()
{
	char k=getchar();
	if (k=='q') exit(0);
}

int main(/*int argc, char *argv[]*/)
{
	vdp_vdu_init();
	if ( vdp_key_init() == -1 ) return 1;
	vdp_set_key_event_handler( key_event_handler );

	// setup complete
	vdp_mode(gMode);
	vdp_logical_scr_dims(false);
	//vdu_set_graphics_viewport()

	load_images();
	create_sprites();

	pos.x = (gScreenWidth/2) & 0xFFFFF8;
	pos.y = (gScreenHeight/2) & 0xFFFFF8;
	vdp_move_sprite_to(pos.x, pos.y);

	game_loop();

	vdp_mode(0);
	vdp_logical_scr_dims(true);
	vdp_cursor_enable( true );
	return 0;
}

void game_loop()
{
	int exit=0;
	
	key_wait_ticks = clock();
	anim_ticks = clock();

	for (int p = 0; p < num_segments; p++)
	{
		draw_path_segment( &paths[p] );
	}
	pos.x = paths[current_ps].A.x;
	pos.y = paths[current_ps].A.y;
	draw_player();

	do {
		uint8_t dir=0;

		// player movement key presses
		if ( vdp_check_key_press( KEY_LEFT ) ) { dir |= BITS_LEFT; }
		if ( vdp_check_key_press( KEY_RIGHT ) ) { dir |= BITS_RIGHT; }
		if ( vdp_check_key_press( KEY_UP ) ) { dir |= BITS_UP; }
		if ( vdp_check_key_press( KEY_DOWN ) ) { dir |= BITS_DOWN; }

		// move the player
		if ( dir>0 && ( key_wait_ticks < clock() ) ) {
			key_wait_ticks = clock() + key_wait;

			move_along_path_segment(&paths[current_ps], dir);

			draw_player();
			TAB(0,0);printf("%d,%d %d %d", pos.x, pos.y, current_ps, dir);
		}

		if ( vdp_check_key_press( KEY_backtick ) )  // ' - toggle debug
		{
			if (key_wait_ticks < clock()) 
			{
				key_wait_ticks = clock() + key_wait;
				debug = !debug;
			}
		}

		if ( vdp_check_key_press( KEY_x ) ) { // x - exit
			TAB(6,8);printf("Are you sure?");
			char k=getchar(); 
			if (k=='y' || k=='Y') exit=1;
		}

		if ( anim_ticks < clock() )
		{
			anim_ticks = clock() + anim_speed;
			player_frame=(player_frame+1)%4; 
			vdp_nth_sprite_frame( player_frame );
		}

		vdp_update_key_state();
	} while (exit==0);

}

void load_images() 
{
	char fname[40];
	for (int fn=1; fn<=4; fn++)
	{
		sprintf(fname, "img/brush%02d.rgb2", fn);
		load_bitmap_file(fname, 8, 8, 0 + fn-1);
	}
}

void create_sprites() 
{
	vdp_adv_create_sprite( 0, 0, 4 );

	vdp_activate_sprites( 1 );

	vdp_select_sprite( 0 );
	vdp_show_sprite();
	vdp_refresh_sprites();

}

void draw_player()
{
	vdp_move_sprite_to(pos.x-4, pos.y-4);
	vdp_gcol(0,15);
	vdp_point( pos.x, pos.y );
}

void draw_path_segment( PathSegment *pps )
{
	vdp_gcol(0, 8);
	vdp_move_to( pps->A.x, pps->A.y );
	vdp_line_to( pps->B.x, pps->B.y );
}

void move_along_path_segment( PathSegment *pps, uint8_t dir)
{
	if ( pps->horiz )
	{
		Position *less, *more;
		if ( pps->A.x < pps->B.x ) 
		{
			less = &pps->A;
			more = &pps->B;
		} else {
			less = &pps->B;
			more = &pps->A;
		}

		if ( (dir & BITS_LEFT) && (pos.x > less->x) ) pos.x -= 1;
		if ( (dir & BITS_RIGHT) && (pos.x < more->x) ) pos.x += 1;

		for (int i=0; i<3; i++)
		{
			if ( pps->valid_next_keyA[i]>0 && pos.x == pps->A.x )
			{
				if ( (dir & pps->valid_next_keyA[i]) ) 
				{
					current_ps = pps->valid_next_dirA[i];
				}
			}
			if ( pps->valid_next_keyB[i]>0 && pos.x == pps->B.x )
			{
				if ( (dir & pps->valid_next_keyB[i]) ) 
				{
					current_ps = pps->valid_next_dirB[i];
				}
			}
		}

	} else {
		Position *less, *more;
		if ( pps->A.y < pps->B.y ) 
		{
			less = &pps->A;
			more = &pps->B;
		} else {
			less = &pps->B;
			more = &pps->A;
		}

		if ( (dir & BITS_UP) && (pos.y > less->y) ) pos.y -= 1;
		if ( (dir & BITS_DOWN) && (pos.y < more->y) ) pos.y += 1;

		for (int i=0; i<3; i++)
		{
			if ( pps->valid_next_keyA[i]>0 && pos.y == pps->A.y )
			{
				if ( (dir & pps->valid_next_keyA[i]) ) 
				{
					current_ps = pps->valid_next_dirA[i];
				}
			}
			if ( pps->valid_next_keyB[i]>0 && pos.y == pps->B.y )
			{
				if ( (dir & pps->valid_next_keyB[i]) ) 
				{
					current_ps = pps->valid_next_dirB[i];
				}
			}
		}
	}
}

