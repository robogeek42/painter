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

bool debug = false;

//------------------------------------------------------------
// Configuration vars
int key_wait = 1;
int anim_speed = 10;
int move_speed = 2;
//------------------------------------------------------------


typedef struct {
	int x;
	int y;
} Position;

// Position of player
Position pos = {0,0};

// counters
clock_t key_wait_ticks;
clock_t anim_ticks;

int player_frame = 0;

void wait();
void game_loop();
void load_images();
void create_sprites();
void draw_player();

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

	pos.x = (gScreenWidth/2) & 0xFFFFF0;
	pos.y = (gScreenHeight/2) & 0xFFFFF0;
	vdp_move_sprite_to(pos.x, pos.y);

	game_loop();

my_exit:
	vdp_mode(0);
	vdp_logical_scr_dims(true);
	vdp_cursor_enable( true );
	return 0;
}

void game_loop()
{
	int exit=0;
	int dir=0;
	
	key_wait_ticks = clock();
	anim_ticks = clock();

	do {
		int dir=0;

		// player movement
		if ( vdp_check_key_press( KEY_LEFT ) ) { dir |= BITS_LEFT; }
		if ( vdp_check_key_press( KEY_RIGHT ) ) { dir |= BITS_RIGHT; }
		if ( vdp_check_key_press( KEY_UP ) ) { dir |= BITS_UP; }
		if ( vdp_check_key_press( KEY_DOWN ) ) { dir |= BITS_DOWN; }

		// move the player
		if ( dir>0 && ( key_wait_ticks < clock() ) ) {
			key_wait_ticks = clock() + key_wait;

			if ( dir & BITS_UP ) pos.y -= move_speed;
			if ( dir & BITS_RIGHT ) pos.x += move_speed;
			if ( dir & BITS_DOWN ) pos.y += move_speed;
			if ( dir & BITS_LEFT ) pos.x -= move_speed;

			draw_player();
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
		sprintf(fname, "img/ball%02d.rgb2", fn);
		load_bitmap_file(fname, 16, 16, 0 + fn-1);
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
	vdp_move_sprite_to(pos.x, pos.y);
}
