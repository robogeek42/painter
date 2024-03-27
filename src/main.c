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

#define SIGN(X) ( (X > 0) ? 1 : ((X < 0) ? -1 : 0) )

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
	int key; // Key that can move in that direction
	int dir; // A valid direction
} ValidNext;

typedef struct {
	Position A;
	Position B;
	bool horiz;
	ValidNext nextA[3]; // thre other possible directions to move in from A
	ValidNext nextB[3]; // thre other possible directions to move in from B
} PathSegment;

PathSegment paths[] = {
	// A          B         horiz     
	/* 0 */ { {100,100}, {200,100}, true,  
		{ { BD, 4 }, {}, {} }, 
		{ { BD, 1 }, { BR, 5 }, {} } 
	},
	/* 1 */ { {200,100}, {200,150}, false, 
		{ { BL, 0 }, { BR, 5 }, {} },
		{ { BR, 7 }, { BD, 2 }, {} }
	},
	/* 2 */ { {200,150}, {200,200}, false, 
		{ { BU, 1 }, { BR, 7 }, {} },
		{ { BL, 3 }, {}, {} }
	},
	/* 3 */ { {200,200}, {100,200}, true,  
		{ { BU, 2 }, {}, {} },
		{ { BU, 4 }, {}, {} }
	},
	/* 4 */ { {100,200}, {100,100}, false, 
		{ { BR, 3 }, {}, {} },
	    { { BR, 0 }, {}, {} }
	},
	/* 5 */ { {200,100}, {250,100}, true, 
		{ { BD, 1 }, { BL, 0 }, {} },
		{ { BD, 6 }, {}, {} }
	},
	/* 6 */ { {250,100}, {250,150}, false, 
		{ { BL, 5 }, {}, {} },
	    { { BL, 7 }, {}, {} }
	},
	/* 7 */ { {250,150}, {200,150}, true, 
		{ { BU, 6 }, {}, {} },
		{ { BU, 1 }, { BD, 2 }, {} }
	},
};
int num_segments = 8;
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
void draw_map();
void draw_map_debug();
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

	vdp_clear_screen();
	vdp_activate_sprites( 1 ); //  have to reactivate sprites after a clear
	draw_map();

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
		if ( dir>0 && ( key_wait_ticks < clock() ) )
		{
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
				draw_map_debug();
			}
		}

		if ( vdp_check_key_press( KEY_x ) ) // x - exit
		{
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

void draw_map()
{
	vdp_gcol(0, 8);
	for (int p = 0; p < num_segments; p++)
	{
		draw_path_segment( &paths[p] );
	}
}

void draw_map_debug()
{
	draw_map();
	for (int p=0; p<num_segments; p++)
	{
		vdp_gcol(0,1);
		draw_path_segment( &paths[p] );
		//TAB(0,1+p);printf("%d: A %d,%d B %d,%d", p, 
		//		paths[p].A.x, paths[p].A.y, paths[p].B.x, paths[p].B.y);
		for (int n=0;n<3;n++)
		{
			if (paths[p].nextA[n].key>0)
			{
				vdp_gcol(0,2);
				draw_path_segment( &paths[paths[p].nextA[n].dir] );
			}
			if (paths[p].nextB[n].key>0)
			{
				vdp_gcol(0,3);
				draw_path_segment( &paths[paths[p].nextB[n].dir] );
			}
		}
		wait();
		vdp_clear_screen();
		vdp_activate_sprites( 1 ); //  have to reactivate sprites after a clear
		draw_map();
	}
}
void draw_player()
{
	vdp_move_sprite_to(pos.x-4, pos.y-4);
	vdp_gcol(0,15);
	vdp_point( pos.x, pos.y );
}

void draw_path_segment( PathSegment *pps )
{
	vdp_move_to( pps->A.x, pps->A.y );
	vdp_line_to( pps->B.x, pps->B.y );
}

bool is_near( int a, int b, int plusminus )
{
	if ( a >= (b - plusminus) && a <= (b + plusminus) ) return true;
	return false;
}
void move_along_path_segment( PathSegment *pps, uint8_t dir)
{
	int prox = 3;
	vdp_point( pos.x, pos.y );

	if ( pps->horiz ) // Currently on a Horizontal path
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

		vdp_point( pos.x, pos.y );

		// See if we can move to a new segment
		for (int i=0; i<3; i++)
		{
			// front of line (point A)
			if ( pps->nextA[i].key > 0 && (dir & pps->nextA[i].key ) )
			{
				// only move to next segment along the same direction if it is exactly at the end
				if ( paths[pps->nextA[i].dir].horiz )
				{
					if ( pos.x == pps->A.x )
					{
						current_ps = pps->nextA[i].dir;
					} 
				}
				else  // the next section is vertical
				{
					if ( is_near(pos.x, pps->A.x, prox) )
					{
						while (pos.x != pps->A.x)
						{
							pos.x += SIGN( pps->A.x - pos.x );
							vdp_point( pos.x, pos.y );
						}
						// Move to the next segment
						current_ps = pps->nextA[i].dir;
						// we may have jumped down so make sure we are on the line
						pos.x = paths[current_ps].A.x; // A or B doesn't matter
					}
				}
			}
			// end of line (point B)
			if ( pps->nextB[i].key > 0 && (dir & pps->nextB[i].key ))
			{
				// only move to next segment along the same direction if it is exactly at the end
				if ( paths[pps->nextB[i].dir].horiz )
				{
					if ( pos.x == pps->B.x )
					{
						current_ps = pps->nextB[i].dir;
					} 
				}
				else // the next section is vertical
				{
					// jump to next segment if the end of the line is in range
					if ( is_near(pos.x, pps->B.x, prox) )
					{
						while (pos.x != pps->B.x)
						{
							pos.x += SIGN( pps->B.x - pos.x );
							vdp_point( pos.x, pos.y );
						}
						// Move to the next segment
						current_ps = pps->nextB[i].dir;
						// we may have jumped down so make sure we are on the line
						pos.x = paths[current_ps].B.x; // A or B doesn't matter
					}
				}
			}
		}

	} else { // Currently on a Vertical path

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
			// front of line (point A)
			if ( pps->nextA[i].key > 0 && (dir & pps->nextA[i].key ) )
			{
				// only move to next segment along the same direction if it is exactly at the end
				if ( !paths[pps->nextA[i].dir].horiz )
				{
					if ( pos.y == pps->A.y )
					{
						current_ps = pps->nextA[i].dir;
					} 
				}
				else  // the next section is horizontal
				{
					if ( is_near(pos.y, pps->A.y, prox) )
					{
						while (pos.y != pps->A.y)
						{
							pos.y += SIGN( pps->A.y - pos.y );
							vdp_point( pos.x, pos.y );
						}
						// Move to the next segment
						current_ps = pps->nextA[i].dir;
						// we may have jumped down so make sure we are on the line
						pos.y = paths[current_ps].A.y; // A or B doesn't matter
					}
				}
			}
			// end of line (point B)
			if ( pps->nextB[i].key > 0 && (dir & pps->nextB[i].key ))
			{
				// only move to next segment along the same direction if it is exactly at the end
				if ( !paths[pps->nextB[i].dir].horiz )
				{
					if ( pos.y == pps->B.y )
					{
						current_ps = pps->nextB[i].dir;
					} 
				}
				else // the next section is horizontal
				{
					// jump to next segment if the end of the line is in range
					if ( is_near(pos.y, pps->B.y, prox) )
					{
						while (pos.y != pps->B.y)
						{
							pos.y += SIGN( pps->B.y - pos.y );
							vdp_point( pos.x, pos.y );
						}
						// Move to the next segment
						current_ps = pps->nextB[i].dir;
						// we may have jumped down so make sure we are on the line
						pos.y = paths[current_ps].B.y; // A or B doesn't matter
					}
				}
			}
		}
	}
}

