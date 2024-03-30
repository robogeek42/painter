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

int gMode = 9; 
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
	int count;
} PathSegment;

PathSegment paths[] = {
	// A          B         horiz     
	/* 0 */ { {100,100}, {200,100}, true,  
		{ { BD, 4 }, {}, {} }, 
		{ { BD, 1 }, { BR, 5 }, {} },
		100
	},
	/* 1 */ { {200,100}, {200,150}, false, 
		{ { BL, 0 }, { BR, 5 }, {} },
		{ { BR, 7 }, { BD, 2 }, {} },
		50
	},
	/* 2 */ { {200,150}, {200,200}, false, 
		{ { BU, 1 }, { BR, 7 }, {} },
		{ { BL, 3 }, {}, {} },
		50
	},
	/* 3 */ { {200,200}, {100,200}, true,  
		{ { BU, 2 }, {}, {} },
		{ { BU, 4 }, {}, {} },
		100
	},
	/* 4 */ { {100,200}, {100,100}, false, 
		{ { BR, 3 }, {}, {} },
	    { { BR, 0 }, {}, {} },
		100
	},
	/* 5 */ { {200,100}, {250,100}, true, 
		{ { BD, 1 }, { BL, 0 }, {} },
		{ { BD, 6 }, {}, {} },
		50
	},
	/* 6 */ { {250,100}, {250,150}, false, 
		{ { BL, 5 }, {}, {} },
	    { { BL, 7 }, {}, {} },
		50
	},
	/* 7 */ { {250,150}, {200,150}, true, 
		{ { BU, 6 }, {}, {} },
		{ { BU, 1 }, { BD, 2 }, {} },
		50
	},
};
int num_segments = 8;
int curr_path_seg = 0;

typedef struct {
	int num_segments;
	int segments[10];
	bool seg_complete[10];
	bool complete;
	int value;
	int colour;
	Position TopLeft;
	Position BotRight;
} Shape;

Shape shapes[] = {
	{ 5, { 0, 1, 2, 3, 4 }, {}, false, 300, 9, {100,100},{200,200} },
	{ 4, { 5, 6, 7, 1 }, {}, false, 100, 13, {200,100},{250,150} }
};
int num_shapes = 2;

// counters
clock_t key_wait_ticks;
clock_t anim_ticks;

int player_frame = 0;

bool score_changed = true;
int score = 0;
int highscore = 0;
int frame = 1;
int skill = 1;
int bonus = 2600;

int main_colour = 9;

void wait();
void game_loop();
void load_images();
void create_sprites();
void draw_map();
void draw_map_debug();
void set_point( Position *ppos );
void draw_screen();
void update_scores();
void draw_path_segment( PathSegment *pps );
void move_along_path_segment( PathSegment *pps, uint8_t dir);
void check_shape_complete();
void fill_shape( int s, bool fast );
void flash_screen(int repeat, int speed);

void wait()
{
	char k=getchar();
	if (k=='q') exit(0);
}

static volatile SYSVAR *sys_vars = NULL;

int main(int argc, char *argv[])
{
	sys_vars = vdp_vdu_init();
	if ( vdp_key_init() == -1 ) return 1;
	vdp_set_key_event_handler( key_event_handler );

	if (argc > 1)
	{
		key_wait=atoi(argv[1]);
	}
	// setup complete
	vdp_mode(gMode);
	vdp_logical_scr_dims(false);
	vdp_cursor_enable( false ); // hiding cursor causes read pixels to go wrong on emulator
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
	draw_screen();
	draw_map();
	update_scores();

	//flash_screen(3,25);

	pos.x = paths[curr_path_seg].A.x;
	pos.y = paths[curr_path_seg].A.y;
	vdp_move_sprite_to(pos.x-4, pos.y-4);
	set_point( &pos );

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

			move_along_path_segment(&paths[curr_path_seg], dir);

			//TAB(0,0);printf("%d,%d seg:%d cnt:%d", pos.x, pos.y, curr_path_seg, paths[curr_path_seg].count);
			if (score_changed) {
				update_scores();
			}
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
			vdp_refresh_sprites();
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

void draw_screen()
{
	vdp_set_text_colour(main_colour);
	TAB(7,0);printf("SCORE");TAB(27,0);printf("HIGH");
	vdp_set_text_colour(15);
	TAB(17,1);printf("PAINTER");

	vdp_set_text_colour(main_colour);
	TAB(7,27);printf("FRAME");TAB(18,27);printf("SKILL");TAB(27,27);printf("BOBUS");
}
void update_scores()
{
	vdp_set_text_colour(15);
	TAB(9,2);printf("%d  ",score);TAB(29,2);printf("%d  ",highscore);
	TAB(9,29);printf("%d",frame);TAB(20,29);printf("%d",skill);TAB(29,29);printf("%d  ",bonus);
}
void draw_map()
{
	vdp_gcol(0, 8);
	for (int p = 0; p < num_segments; p++)
	{
		draw_path_segment( &paths[p] );
	}
	vdp_write_at_graphics_cursor();
	vdp_gcol(0,15);
	for ( int s=0; s < num_shapes; s++ )
	{
		int w = shapes[s].BotRight.x - shapes[s].TopLeft.x;
		int h = shapes[s].BotRight.y - shapes[s].TopLeft.y;
		vdp_move_to( shapes[s].TopLeft.x + (w/2) - 12, shapes[s].TopLeft.y + (h/2) - 4);
		printf("%d",shapes[s].value);
	}
	vdp_write_at_text_cursor();
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

void set_point( Position *ppos )
{
	uint24_t col = 0;

	// Hide sprite while we read the pixel colour under it
	vdp_hide_sprite();
	vdp_refresh_sprites();

	col = readPixelColour( sys_vars, ppos->x, ppos->y );
	//TAB(0,0);printf("%06x",col);

	// re-display the sprite
	vdp_show_sprite();
	vdp_refresh_sprites();

	// If we read dark grey (colour 8) then it is a bit of the line 
	// we haven't visted yet
	if ( col == 0x555555 && paths[curr_path_seg].count>0 )
	{
		// reduce count in this line segment
		paths[curr_path_seg].count--;
		if ( paths[curr_path_seg].count == 0 )
		{
			check_shape_complete();
		}
		// also reduce count in connected segments
		if ( pos.x == paths[curr_path_seg].A.x && pos.y == paths[curr_path_seg].A.y )
		{
			for (int n=0; n<3; n++)
			{
				if ( paths[curr_path_seg].nextA[n].key > 0 )
				{
					if ( paths[ paths[curr_path_seg].nextA[n].dir ].count > 0)
					{
						paths[ paths[curr_path_seg].nextA[n].dir ].count--;
					}
				}
			}
		}
		if ( pos.x == paths[curr_path_seg].B.x && pos.y == paths[curr_path_seg].B.y )
		{
			for (int n=0; n<3; n++)
			{
				if ( paths[curr_path_seg].nextB[n].key > 0 )
				{
					if ( paths[ paths[curr_path_seg].nextB[n].dir ].count > 0)
					{
						paths[ paths[curr_path_seg].nextB[n].dir ].count--;
					}
				}
			}
		}
	}
	// paint the point with the new colour
	vdp_gcol(0,15);
	vdp_point( ppos->x, ppos->y );

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

		set_point( &pos );

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
						curr_path_seg = pps->nextA[i].dir;
					} 
				}
				else  // the next section is vertical
				{
					if ( is_near(pos.x, pps->A.x, prox) )
					{
						while (pos.x != pps->A.x)
						{
							pos.x += SIGN( pps->A.x - pos.x );
							set_point( &pos );
						}
						// Move to the next segment
						curr_path_seg = pps->nextA[i].dir;
						// we may have jumped down so make sure we are on the line
						pos.x = paths[curr_path_seg].A.x; // A or B doesn't matter
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
						curr_path_seg = pps->nextB[i].dir;
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
							set_point( &pos );
						}
						// Move to the next segment
						curr_path_seg = pps->nextB[i].dir;
						// we may have jumped down so make sure we are on the line
						pos.x = paths[curr_path_seg].B.x; // A or B doesn't matter
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

		set_point( &pos );

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
						curr_path_seg = pps->nextA[i].dir;
					} 
				}
				else  // the next section is horizontal
				{
					if ( is_near(pos.y, pps->A.y, prox) )
					{
						while (pos.y != pps->A.y)
						{
							pos.y += SIGN( pps->A.y - pos.y );
							set_point( &pos );
						}
						// Move to the next segment
						curr_path_seg = pps->nextA[i].dir;
						// we may have jumped down so make sure we are on the line
						pos.y = paths[curr_path_seg].A.y; // A or B doesn't matter
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
						curr_path_seg = pps->nextB[i].dir;
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
							set_point( &pos );
						}
						// Move to the next segment
						curr_path_seg = pps->nextB[i].dir;
						// we may have jumped down so make sure we are on the line
						pos.y = paths[curr_path_seg].B.y; // A or B doesn't matter
					}
				}
			}
		}
	}
	vdp_move_sprite_to(pos.x-4, pos.y-4);
}

void check_shape_complete()
{
	bool all_complete = true;
	for (int s=0; s < num_shapes; s++)
	{
		bool shape_complete = shapes[s].complete;
		if (!shape_complete)
		{
			int incomplete = shapes[s].num_segments;
			for (int seg=0; seg < shapes[s].num_segments; seg++)
			{
				if ( paths[ shapes[s].segments[seg] ].count <= 0 )
				{
					shapes[s].seg_complete[seg] = true;
					incomplete--;
				}
			}
			if (incomplete==0) {
				 shapes[s].complete = true;
				 fill_shape( s, false );
				 score += shapes[s].value;
			}
			else
			{
				all_complete = false;
			}
		}
	}
	if (all_complete)
	{
		flash_screen(10, 25);
	}
}

void fill_shape( int s, bool fast )
{
	vdp_gcol(0, shapes[s].colour);
	int x1 = shapes[s].TopLeft.x;
	int y1 = shapes[s].TopLeft.y;
	int x2 = shapes[s].BotRight.x;
	int y2 = shapes[s].BotRight.y;
	// fast fill:
	if (fast)
	{
		vdp_move_to( shapes[s].TopLeft.x+1, shapes[s].TopLeft.y+1 );
		vdp_filled_rect( shapes[s].BotRight.x-1, shapes[s].BotRight.y-1 );
	} else {
		// slow fill
		int slice_width = MAX(1, (x2-x1)/50);
		for (int x=x1+1; x<x2; x+=slice_width)
		{
			vdp_move_to( x, y1+1 );
			vdp_filled_rect( MIN(x+slice_width, x2-2), y2-1 );
			clock_t ticks=clock()+1;
			while (ticks > clock()) {
				vdp_update_key_state();
			};
		}
	}
}

void flash_screen(int repeat, int speed)
{
	clock_t ticks;

	bool flash_on = true;
	for (int rep=0; rep<repeat; rep++)
	{
		//vdp_define_colour( 0, flash_on?15:0, 255, 0, 255 );
		int col = flash_on?63:0;
		putch(19);putch(0);putch(col);putch(0);putch(0);putch(0);
		ticks = clock()+speed;
		while (ticks > clock())
		{
			vdp_update_key_state();
		}
		flash_on = !flash_on;
	}
	putch(19);putch(0);putch(0);putch(0);putch(0);putch(0);
}

