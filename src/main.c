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
int enemy_speed = 1;
//------------------------------------------------------------


typedef struct {
	int x;
	int y;
} Position;

// Position of player
Position pos = {0,0};

// Position of enemies
#define MAX_NUM_ENEMIES 3
Position enemy_pos[MAX_NUM_ENEMIES] = {};
int num_enemies = 1;
int enemy_curr_segment[MAX_NUM_ENEMIES];
int enemy_dir[MAX_NUM_ENEMIES];

typedef struct {
	int key; // Key that can move in that direction
	int seg; // A valid segment in that direction
} ValidNext;

typedef struct {
	Position A;
	Position B;
	bool horiz;
	ValidNext nextA[3]; // the other possible directions to move in from A
	int num_validA;
	ValidNext nextB[3]; // the other possible directions to move in from B
	int num_validB;
	int count;
} PathSegment;

PathSegment *paths;

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
clock_t enemy_ticks;

int player_frame = 0;

bool score_changed = true;
int score = 0;
int highscore = 0;
int frame = 1;
int skill = 1;
int bonus = 2600;
int volume = 3; // 0-10

int main_colour = 9;

Position last_play_beep_pos;

void wait();
void game_loop();
void load_images();
void create_sprites();
bool load_map();
void draw_map();
void draw_map_debug();
void set_point( Position *ppos );
void draw_screen();
void update_scores();
void draw_path_segment( PathSegment *pps );
bool is_at_position(Position *pposA, Position *pposB);
void copy_position(Position *pFrom, Position *pTo);
void move_along_path_segment( PathSegment *pps, Position *ppos, int *curr_seg, uint8_t dir, bool draw, int prox);
void check_shape_complete();
void fill_shape( int s, bool fast );
void flash_screen(int repeat, int speed);
void move_enemies();
void play_beep();
void play_wah_wah();

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
	int seed = 1234;
	srand(seed);

	// setup complete
	vdp_mode(gMode);
	vdp_logical_scr_dims(false);
	vdp_cursor_enable( false ); // hiding cursor causes read pixels to go wrong on emulator
	//vdu_set_graphics_viewport()

	load_images();
	create_sprites();

	load_map();
	vdp_audio_reset_channel( 0 );
	vdp_audio_reset_channel( 1 );

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
	enemy_ticks = clock();

	vdp_clear_screen();
	vdp_activate_sprites( 2 ); //  have to reactivate sprites after a clear
	draw_screen();
	draw_map();
	update_scores();

	pos.x = paths[curr_path_seg].A.x;
	pos.y = paths[curr_path_seg].A.y;
	set_point( &pos );
	copy_position(&pos, &last_play_beep_pos);

	enemy_curr_segment[0] = 3;
	enemy_pos[0].x = paths[enemy_curr_segment[0]].A.x;
	enemy_pos[0].y = paths[enemy_curr_segment[0]].A.y;
	enemy_dir[0] = BITS_LEFT;
	
	vdp_select_sprite(1);
	vdp_move_sprite_to(enemy_pos[0].x-4, enemy_pos[0].y-4);

	vdp_refresh_sprites();

	// wah-wah plays on channel 2
	play_wah_wah();
	flash_screen(10,30);
	vdp_audio_frequency_envelope_disable( 2 );
	vdp_audio_disable_channel( 2 );

	// beep on channel 0
	vdp_audio_reset_channel( 0 );
	vdp_audio_set_waveform( 0, VDP_AUDIO_WAVEFORM_SINEWAVE);
	play_beep();

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

			move_along_path_segment(&paths[curr_path_seg], &pos, &curr_path_seg, dir, true, 3);
			vdp_select_sprite(0);
			vdp_move_sprite_to(pos.x-4, pos.y-4);
			vdp_refresh_sprites();
			if ( is_at_position( &pos, &paths[ curr_path_seg ].A ) ||
			     is_at_position( &pos, &paths[ curr_path_seg ].B ) )
			{
				if (! is_at_position(&pos, &last_play_beep_pos)) 
				{
					play_beep();
					copy_position(&pos, &last_play_beep_pos);
				}
			}


			//TAB(0,0);printf("%d,%d seg:%d cnt:%d  ", pos.x, pos.y, curr_path_seg, paths[curr_path_seg].count);
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
			TAB(0,1);printf("Are you sure?");
			char k=getchar(); 
			if (k=='y' || k=='Y') exit=1;
			else 
			{
				TAB(0,1);printf("             ");
			}
		}

		if ( anim_ticks < clock() )
		{
			anim_ticks = clock() + anim_speed;
			// player
			player_frame=(player_frame+1)%4; 
			vdp_select_sprite(0);
			vdp_nth_sprite_frame( player_frame );
			vdp_refresh_sprites();
			// enemies
			for (int en=0; en<1+num_enemies ;en++)
			{
				vdp_select_sprite(en+1);
				vdp_nth_sprite_frame( player_frame );
				vdp_refresh_sprites();
			}
		}

		if ( enemy_ticks < clock() )
		{
			enemy_ticks = clock() + enemy_speed;
			move_enemies();
		}

		vdp_update_key_state();
	} while (exit==0);

}

void load_images() 
{
	char fname[40];
	for (int fn=1; fn<=4; fn++)
	{
		sprintf(fname, "img/b5%02d.rgb2", fn);
		load_bitmap_file(fname, 8, 8, 0 + fn-1);
	}
	for (int fn=1; fn<=4; fn++)
	{
		sprintf(fname, "img/ball%02d.rgb2", fn);
		load_bitmap_file(fname, 8, 8, 4 + fn-1);
	}
}

void create_sprites() 
{
	vdp_adv_create_sprite( 0, 0, 4 );
	vdp_adv_create_sprite( 1, 4, 4 );

	vdp_activate_sprites( 2 );

	vdp_select_sprite( 0 );
	vdp_show_sprite();

	for (int sp=0;sp<MAX_NUM_ENEMIES;sp++)
	{
		vdp_select_sprite( 1+sp );
		if (sp < num_enemies)
		{
			vdp_show_sprite();
		}
		else
		{
			vdp_hide_sprite();
		}
		vdp_refresh_sprites();
	}

}

void draw_screen()
{
	vdp_set_text_colour(main_colour);
	TAB(7,0);printf("SCORE");TAB(27,0);printf("HIGH");
	vdp_set_text_colour(15);
	TAB(17,1);printf("PAINTER");

	vdp_set_text_colour(main_colour);
	TAB(7,27);printf("FRAME");TAB(18,27);printf("SKILL");TAB(27,27);printf("BONUS");
}
void update_scores()
{
	vdp_set_text_colour(15);
	TAB(9,2);printf("%d  ",score);TAB(29,2);printf("%d  ",highscore);
	TAB(9,29);printf("%d",frame);TAB(20,29);printf("%d",skill);TAB(29,29);printf("%d  ",bonus);
}

void draw_path_segment( PathSegment *pps ) {
	vdp_move_to( pps->A.x, pps->A.y );
	vdp_line_to( pps->B.x, pps->B.y );
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
		shapes[s].complete = false;
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
				draw_path_segment( &paths[paths[p].nextA[n].seg] );
			}
			if (paths[p].nextB[n].key>0)
			{
				vdp_gcol(0,3);
				draw_path_segment( &paths[paths[p].nextB[n].seg] );
			}
		}
		wait();
		vdp_clear_screen();
		vdp_activate_sprites( 1 ); //  have to reactivate sprites after a clear
		draw_map();
	}
}

bool is_near( int a, int b, int plusminus )
{
	if (!plusminus) return a==b;
	if ( a >= (b - plusminus) && a <= (b + plusminus) ) return true;
	return false;
}

bool is_at_position(Position *pposA, Position *pposB)
{
	if ( pposA->x == pposB->x && pposA->y == pposB->y ) return true;
	return false;
}

void copy_position(Position *pFrom, Position *pTo)
{
	pTo->x = pFrom->x;
	pTo->y = pFrom->y;
}

void set_point( Position *ppos )
{
	uint24_t col = 0;

	// Sprite has black transparent and a hole in the middle to 
	// read pixels under

	vdp_select_sprite(0);
	vdp_move_sprite_to(ppos->x-4, ppos->y-4);
	vdp_refresh_sprites();

	col = readPixelColour( sys_vars, ppos->x, ppos->y );
	//TAB(0,0);printf("%06x",col);
	//TAB(0,1);
	// If we read dark grey (colour 8) then it is a bit of the line 
	// we haven't visted yet
	if ( col == 0x555555 )
	{
		// reduce count in this line segment
		if (paths[curr_path_seg].count>0)
		{
			paths[curr_path_seg].count--;
			// also reduce count in connected segments
			if ( is_at_position( ppos, &paths[curr_path_seg].A ) )
			{
				//printf("at end A of %d  \n", curr_path_seg);
				for (int n=0; n<3; n++)
				{
					if ( paths[curr_path_seg].nextA[n].key > 0 )
					{
						if ( paths[ paths[curr_path_seg].nextA[n].seg ].count > 0)
						{
							paths[ paths[curr_path_seg].nextA[n].seg ].count--;
							//printf("reduce %d -> %d to %d  \n",curr_path_seg,  paths[curr_path_seg].nextA[n].seg, paths[ paths[curr_path_seg].nextA[n].seg ].count);
						}
					}
				}
			}
			if ( is_at_position( ppos, &paths[curr_path_seg].B ) )
			{
				//printf("at end B of %d  \n", curr_path_seg);
				for (int n=0; n<3; n++)
				{
					if ( paths[curr_path_seg].nextB[n].key > 0 )
					{
						if ( paths[ paths[curr_path_seg].nextB[n].seg ].count > 0)
						{
							paths[ paths[curr_path_seg].nextB[n].seg ].count--;
							//printf("reduce %d -> %d to %d  \n",curr_path_seg,  paths[curr_path_seg].nextB[n].seg, paths[ paths[curr_path_seg].nextB[n].seg ].count);
						}
					}
				}
			}
			// paint the point with the new colour
			vdp_gcol(0,15);
			vdp_point( ppos->x, ppos->y );

			if ( paths[curr_path_seg].count == 0 )
			{
				check_shape_complete();
			}
		}
	}
}

void move_along_path_segment( PathSegment *pps, Position *ppos, int *curr_seg, uint8_t dir, bool draw, int prox)
{
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

		if ( (dir & BITS_LEFT) && (ppos->x > less->x) ) ppos->x -= 1;
		if ( (dir & BITS_RIGHT) && (ppos->x < more->x) ) ppos->x += 1;

		// always set the new point we move to
		if (draw) set_point( ppos ); // only player ever sets point

		// See if we can move to a new segment
		for (int i=0; i<3; i++)
		{
			// front of line (point A)
			if ( pps->nextA[i].key > 0 && (dir & pps->nextA[i].key ) )
			{
				// only move to next segment along the same direction if it is exactly at the end
				if ( paths[pps->nextA[i].seg].horiz )
				{
					if ( ppos->x == pps->A.x )
					{
						(*curr_seg) = pps->nextA[i].seg;
						if (draw) set_point( ppos );
					} 
				}
				else  // the next section is vertical - allow fuzziness
				{
					if ( is_near(ppos->x, pps->A.x, prox) )
					{
						while (ppos->x != pps->A.x)
						{
							ppos->x += SIGN( pps->A.x - ppos->x );
							if (draw) set_point( ppos );
						}
						// Move to the next segment
						(*curr_seg) = pps->nextA[i].seg;
					}
				}
			}
			// end of line (point B)
			if ( pps->nextB[i].key > 0 && (dir & pps->nextB[i].key ))
			{
				// only move to next segment along the same direction if it is exactly at the end
				if ( paths[pps->nextB[i].seg].horiz )
				{
					if ( ppos->x == pps->B.x )
					{
						(*curr_seg) = pps->nextB[i].seg;
						if (draw) set_point( ppos );
					} 
				}
				else // the next section is vertical
				{
					// jump to next segment if the end of the line is in range
					if ( is_near(ppos->x, pps->B.x, prox) )
					{
						while (ppos->x != pps->B.x)
						{
							ppos->x += SIGN( pps->B.x - ppos->x );
							if (draw) set_point( ppos );
						}
						// Move to the next segment
						(*curr_seg) = pps->nextB[i].seg;
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

		if ( (dir & BITS_UP) && (ppos->y > less->y) ) ppos->y -= 1;
		if ( (dir & BITS_DOWN) && (ppos->y < more->y) ) ppos->y += 1;

		if (draw) set_point( ppos );

		for (int i=0; i<3; i++)
		{
			// front of line (point A)
			if ( pps->nextA[i].key > 0 && (dir & pps->nextA[i].key ) )
			{
				// only move to next segment along the same direction if it is exactly at the end
				if ( !paths[pps->nextA[i].seg].horiz )
				{
					if ( ppos->y == pps->A.y )
					{
						(*curr_seg) = pps->nextA[i].seg;
						if (draw) set_point( ppos );
					} 
				}
				else  // the next section is horizontal
				{
					if ( is_near(ppos->y, pps->A.y, prox) )
					{
						while (ppos->y != pps->A.y)
						{
							ppos->y += SIGN( pps->A.y - ppos->y );
							if (draw) set_point( ppos );
						}
						// Move to the next segment
						(*curr_seg) = pps->nextA[i].seg;
					}
				}
			}
			// end of line (point B)
			if ( pps->nextB[i].key > 0 && (dir & pps->nextB[i].key ))
			{
				// only move to next segment along the same direction if it is exactly at the end
				if ( !paths[pps->nextB[i].seg].horiz )
				{
					if ( ppos->y == pps->B.y )
					{
						(*curr_seg) = pps->nextB[i].seg;
						if (draw) set_point( ppos );
					} 
				}
				else // the next section is horizontal
				{
					// jump to next segment if the end of the line is in range
					if ( is_near(ppos->y, pps->B.y, prox) )
					{
						while (ppos->y != pps->B.y)
						{
							ppos->y += SIGN( pps->B.y - ppos->y );
							if (draw) set_point( ppos );
						}
						// Move to the next segment
						(*curr_seg) = pps->nextB[i].seg;
					}
				}
			}
		}
	}
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
		flash_screen(4, 25);
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
		vdp_audio_reset_channel( 1 );
		vdp_audio_set_waveform( 1, VDP_AUDIO_WAVEFORM_VICNOISE);
		int slice_width = MAX(1, (x2-x1)/50);
		for (int x=x1+1; x<x2; x+=slice_width)
		{
			vdp_move_to( x, y1+1 );
			vdp_filled_rect( MIN(x+slice_width, x2-2), y2-1 );

			// fill noise on channel 1
			vdp_audio_play_note( 1, 10*volume, 10+(x-x1)/3, 20);
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
		int col = flash_on? 0b00101010 :0;
		vdp_define_colour( 0, col, 0, 0, 0 );
		ticks = clock()+speed;
		while (ticks > clock())
		{
			vdp_update_key_state();
		}
		flash_on = !flash_on;
	}
	vdp_define_colour( 0, 0, 0, 0, 0 );
}

#define DIR_OPP(d) ((d+2)%4)

int direction_choice( int seg, bool at_A )
{
	int dir = 0;
	if ( paths[seg].horiz )
	{
		if ( paths[seg].A.x > paths[seg].B.x )
		{
			if (at_A) dir = BITS_LEFT; else dir = BITS_RIGHT;
		} else {
			if (at_A) dir = BITS_RIGHT; else dir = BITS_LEFT;
		}
	}
	else
	{
		if ( paths[seg].A.y > paths[seg].B.y )
		{
			if (at_A) dir = BITS_UP; else dir = BITS_DOWN;
		} else {
			if (at_A) dir = BITS_DOWN; else dir = BITS_UP;
		}
	}
	return dir;
}

void move_enemies()
{
	for (int en=0; en<num_enemies; en++)
	{
		//int old_seg = enemy_curr_segment[en];
		//int old_dir = enemy_dir[en];
		move_along_path_segment(&paths[enemy_curr_segment[en]], &enemy_pos[en], &enemy_curr_segment[en], enemy_dir[en], false, 0);
		// choose a new segment
		if ( is_at_position( &enemy_pos[en], &paths[enemy_curr_segment[en]].A ) )
		{
			int rand_seg_num = ( rand() % paths[enemy_curr_segment[en]].num_validA );
			enemy_curr_segment[en] = paths[enemy_curr_segment[en]].nextA[rand_seg_num].seg;
			enemy_dir[en] = paths[enemy_curr_segment[en]].nextA[rand_seg_num].key;
			//TAB(0,3);printf("%d: A s:%d(%d) -> s:%d(%d)    \n",en,old_seg,old_dir,enemy_curr_segment[en],enemy_dir[en]);
		}
		if ( is_at_position( &enemy_pos[en], &paths[enemy_curr_segment[en]].B ) )
		{
			int rand_seg_num = ( rand() % paths[enemy_curr_segment[en]].num_validB );
			enemy_dir[en] = paths[enemy_curr_segment[en]].nextB[rand_seg_num].key;
			enemy_curr_segment[en] = paths[enemy_curr_segment[en]].nextB[rand_seg_num].seg;
			//TAB(0,3);printf("%d: B s:%d(%d) -> s:%d(%d)    \n",en,old_seg,old_dir,enemy_curr_segment[en],enemy_dir[en]);
		}
		//TAB(0,2);printf("%d: (%d,%d) d:%d s:%d    \n",en,enemy_pos[en].x,enemy_pos[en].y,enemy_dir[en],enemy_curr_segment[en]);
		vdp_select_sprite(en+1);
		vdp_move_sprite_to(enemy_pos[en].x-4, enemy_pos[en].y-4);
		vdp_refresh_sprites();
	}
}

bool load_map()
{
	paths = (PathSegment*) calloc(8, sizeof(PathSegment));

	paths[0] = (PathSegment)
		// A          B         horiz     
		/* 0 */ { {100,100}, {200,100}, true,  
			{ { BD, 4 }, {}, {} }, 1,
			{ { BD, 1 }, { BR, 5 }, {} }, 2,
			101
		};
	paths[1] = (PathSegment)
		/* 1 */ { {200,100}, {200,150}, false, 
			{ { BL, 0 }, { BR, 5 }, {} }, 2,
			{ { BR, 7 }, { BD, 2 }, {} }, 2,
			51
		};
	paths[2] = (PathSegment)
		/* 2 */ { {200,150}, {200,200}, false, 
			{ { BU, 1 }, { BR, 7 }, {} }, 2,
			{ { BL, 3 }, {}, {} }, 1,
			51
		};
	paths[3] = (PathSegment)
		/* 3 */ { {200,200}, {100,200}, true,  
			{ { BU, 2 }, {}, {} }, 1,
			{ { BU, 4 }, {}, {} }, 1,
			101
		};
	paths[4] = (PathSegment)
		/* 4 */ { {100,200}, {100,100}, false, 
			{ { BR, 3 }, {}, {} }, 1,
			{ { BR, 0 }, {}, {} }, 1,
			101
		};
	paths[5] = (PathSegment)
		/* 5 */ { {200,100}, {250,100}, true, 
			{ { BD, 1 }, { BL, 0 }, {} }, 2,
			{ { BD, 6 }, {}, {} }, 1,
			51
		};
	paths[6] = (PathSegment)
		/* 6 */ { {250,100}, {250,150}, false, 
			{ { BL, 5 }, {}, {} }, 1,
			{ { BL, 7 }, {}, {} }, 1,
			51
		};
	paths[7] = (PathSegment)
		/* 7 */ { {250,150}, {200,150}, true, 
			{ { BU, 6 }, {}, {} }, 1,
			{ { BU, 1 }, { BD, 2 }, {} }, 2,
			51
		};

	return true;
}

void play_beep()
{
	vdp_audio_play_note( 0, 20*volume, 2217, 40);
}

void play_wah_wah()
{
	vdp_audio_reset_channel( 2 );
	vdp_audio_set_waveform( 2,VDP_AUDIO_WAVEFORM_SQUARE );

	vdp_audio_frequency_envelope_stepped( 
			2,  // channel
			2,  // number of steps
			VDP_AUDIO_FREQ_ENVELOPE_CONTROL_REPEATS,
			10  // step length
	);
	uint16_t words[4] = {
		2,   // phase 1: freq adjustment
		18,  // phase 1: in x steps
		-2,  // phase 2: freq adjustment
		18   // phase 2: in x steps
	};
	mos_puts( (char *)words, sizeof(words), 0 );

	vdp_audio_play_note( 2, // channel
		   				10*volume, // volume
						73, // freq
						2600 // duration
						);
}
void stop_wah_wah()
{
	vdp_audio_reset_channel( 2 );
}
