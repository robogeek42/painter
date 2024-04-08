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

#define MAX_SHAPE_SEGS 30
#define MAX_PATHS 50
#define MAX_SHAPES 20

typedef struct {
	int num_segments;
	int segments[MAX_SHAPE_SEGS];
	bool seg_complete[MAX_SHAPE_SEGS];
	bool complete;
	int value;
	Position TopLeft;
	Position BotRight;
} Shape;

#define SAVE_LEVEL_VERSION 1
typedef struct {
	uint8_t version;
	int num_path_segments;
	int num_shapes;
	int colour_unpainted_line;
	int colour_painted_line;
	int colour_fill;
	int bonus;
	uint8_t num_enemies;
	PathSegment *paths;
	Shape *shapes;
} Level;

#define MAX_LEVELS 2
Level *levels[MAX_LEVELS] = {};
int cl = 0; // current level

int curr_path_seg = 0;

// counters
clock_t key_wait_ticks;
clock_t anim_ticks;
clock_t enemy_ticks;
clock_t bonus_countdown_ticks;

int bonus_countdown_time = 5*100;

int player_frame = 0;

bool score_changed = true;
int score = 0;
int highscore = 0;
int lives = 3;
int skill = 1;
int volume = 6; // 0-10

bool is_exit = false;
bool restart_level = false;
bool end_game = false;
bool level_complete = false;

int main_colour = 9;

Position last_play_beep_pos;

void wait();
bool game_loop();
void load_images();
void create_sprites();
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
bool flash_screen(int repeat, int speed);
void move_enemies();
void play_beep();
void play_wah_wah();
void play_crash();
Level* load_level(char *fname_pattern, int lnum);
bool intro_screen1();
bool intro_screen2();

void wait()
{
	char k=getchar();
	if (k=='q' || k=='x') exit(0);
}

static volatile SYSVAR *sys_vars = NULL;

void print_box_prompt(char *str, int fg, int bg)
{
	int len = strlen(str);
	int width = 8*(len+4);
	int height = 8*3;

	draw_filled_box_centre(160, 120, width+4, height+4,bg, fg);
	draw_filled_box_centre(160, 120, width, height,fg, bg);

	vdp_write_at_graphics_cursor();
	COL(fg);COL(128+bg); 
	vdp_move_to(160 - (8*len/2), 120-4);
	printf("%s",str);
	vdp_write_at_text_cursor();
}

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

	TAB(17,11);printf("LOADING");
	levels[cl] = load_level("levels/level%1d.data", cl+1);
	if (levels[cl]==NULL)
	{
		printf("Failed to load level\n");
		return 0;
	}

	load_images();
	create_sprites();

	vdp_audio_reset_channel( 0 );
	vdp_audio_reset_channel( 1 );

	if ( intro_screen1() )
	{
		bool play_again = false;
		do 
		{
			play_again = intro_screen2();
			lives = 3;
			if ( play_again )
			{
				play_again = game_loop();
			}
		} while(play_again );
	}

	TAB(0,26);COL(15);printf("Goodbye!\n");

	vdp_mode(0);
	vdp_logical_scr_dims(true);
	vdp_cursor_enable( true );
	return 0;
}

void start_level()
{
	COL(128);COL(15);
	vdp_clear_screen();
	vdp_activate_sprites( 2 ); //  have to reactivate sprites after a clear
	draw_screen();
	draw_map();
	update_scores();

	curr_path_seg = 0;
	pos.x = levels[cl]->paths[curr_path_seg].A.x;
	pos.y = levels[cl]->paths[curr_path_seg].A.y;
	set_point( &pos );
	copy_position(&pos, &last_play_beep_pos);

	enemy_curr_segment[0] = 3;
	enemy_pos[0].x = levels[cl]->paths[enemy_curr_segment[0]].A.x;
	enemy_pos[0].y = levels[cl]->paths[enemy_curr_segment[0]].A.y;
	enemy_dir[0] = BITS_LEFT;
	
	for (int s=0;s<=levels[cl]->num_enemies;s++)
	{
		vdp_select_sprite(s);
		vdp_show_sprite();
	}
	vdp_select_sprite(1);
	vdp_move_sprite_to(enemy_pos[0].x-4, enemy_pos[0].y-4);

	vdp_refresh_sprites();

	// wah-wah plays on channel 2
	play_wah_wah();
	if ( !flash_screen(10,30) ) is_exit = true;
		
	vdp_audio_frequency_envelope_disable( 2 );
	vdp_audio_disable_channel( 2 );

	// beep on channel 0
	vdp_audio_reset_channel( 0 );
	vdp_audio_set_waveform( 0, VDP_AUDIO_WAVEFORM_SINEWAVE);
	play_beep();

}

bool game_loop()
{
	key_wait_ticks = clock();
	anim_ticks = clock();
	enemy_ticks = clock();
	bonus_countdown_ticks = clock() + bonus_countdown_time;

	cl = 0;
	level_complete = false;
	start_level();

	do 
	{
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

			move_along_path_segment(&levels[cl]->paths[curr_path_seg], &pos, &curr_path_seg, dir, true, 3);
			vdp_select_sprite(0);
			vdp_move_sprite_to(pos.x-4, pos.y-4);
			vdp_refresh_sprites();
			if ( is_at_position( &pos, &levels[cl]->paths[ curr_path_seg ].A ) ||
			     is_at_position( &pos, &levels[cl]->paths[ curr_path_seg ].B ) )
			{
				if (! is_at_position(&pos, &last_play_beep_pos)) 
				{
					play_beep();
					copy_position(&pos, &last_play_beep_pos);
				}
			}


			//TAB(0,0);printf("%d,%d seg:%d cnt:%d  ", pos.x, pos.y, curr_path_seg, levels[cl]->paths[curr_path_seg].count);
			if (score_changed) {
				update_scores();
			}
			if (level_complete)
			{
				free( levels[cl]->paths );
				free( levels[cl]->shapes );
				free( levels[cl] );
				cl = MIN(MAX_LEVELS, cl + 1);

				levels[cl] = load_level("ped/level%1d.data", cl+1);
				if (levels[cl]==NULL)
				{
					printf("Failed to load level\n");
					is_exit = true;
				}
				level_complete = false;
				for (int s=0;s<=MAX_NUM_ENEMIES;s++)
				{
					vdp_select_sprite(s);
					vdp_hide_sprite();
				}

				vdp_clear_screen();
				draw_screen();
				draw_map();

				print_box_prompt("READY?",12,11);
				wait();
				start_level();
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

		if ( vdp_check_key_press( KEY_space ) )  // fire a gap
		{
			if (key_wait_ticks < clock()) 
			{
				key_wait_ticks = clock() + key_wait;
			}
		}

		if ( vdp_check_key_press( KEY_x ) ) // x - exit
		{
			TAB(0,1);printf("Are you sure?");
			char k=getchar(); 
			if (k=='y' || k=='Y') is_exit = true;
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
			for (int en=0; en< 1 + levels[cl]->num_enemies ;en++)
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

			if ( restart_level )
			{
				free( levels[cl]->paths );
				free( levels[cl]->shapes );
				free( levels[cl] );
				levels[cl] = load_level("ped/level%1d.data", cl+1);
				if (levels[cl]==NULL)
				{
					printf("Failed to load level\n");
					is_exit = true;
				}
				restart_level = false;
				for (int s=0;s<=MAX_NUM_ENEMIES;s++)
				{
					vdp_select_sprite(s);
					vdp_hide_sprite();
				}
				print_box_prompt("Oh Dear! READY?",12,11);
				wait_for_any_key();
				start_level();
			}
		}

		if ( bonus_countdown_ticks < clock() )
		{
			bonus_countdown_ticks = clock() + bonus_countdown_time;
			if ( levels[cl]->bonus > 0) levels[cl]->bonus -= 100;
		}


		vdp_update_key_state();
	} while (!is_exit && !end_game);

	for (int chan=0;chan<3;chan++)
	{
		vdp_audio_frequency_envelope_disable( chan );
		vdp_audio_volume_envelope_disable( chan );
		vdp_audio_disable_channel( chan );
	}

	if ( is_exit && !end_game ) return false;

	return true;

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
		vdp_hide_sprite();
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
	vdp_set_text_colour(15);
	//TAB(20,2);printf("%d",lives);
	TAB(17,3);printf("        ");
	for (int i=0; i< lives; i++)
	{
		vdp_adv_select_bitmap(0);
		vdp_draw_bitmap(160-20+16*i,24);
	}
}
void update_scores()
{
	vdp_set_text_colour(15);
	TAB(9,2);printf("%d  ",score);TAB(29,2);printf("%d  ",highscore);
	TAB(9,29);printf("%d",cl+1);TAB(20,29);printf("%d",skill);TAB(29,29);printf("%d  ",levels[cl]->bonus);
}

void draw_path_segment( PathSegment *pps ) {
	vdp_move_to( pps->A.x, pps->A.y );
	vdp_line_to( pps->B.x, pps->B.y );
}

void draw_map()
{
	vdp_gcol(0, 8);
	for (int p = 0; p < levels[cl]->num_path_segments; p++)
	{
		draw_path_segment( &levels[cl]->paths[p] );
	}
	vdp_write_at_graphics_cursor();
	vdp_gcol(0,15);
	for ( int s=0; s < levels[cl]->num_shapes; s++ )
	{
		Shape *pshape = &levels[cl]->shapes[s];
		int w = pshape->BotRight.x - pshape->TopLeft.x;
		int h = pshape->BotRight.y - pshape->TopLeft.y;
		vdp_move_to( pshape->TopLeft.x + (w/2) - 12, pshape->TopLeft.y + (h/2) - 4);
		printf("%d",pshape->value);
		pshape->complete = false;
	}
	vdp_write_at_text_cursor();
}

void draw_map_debug()
{
	draw_map();
	for (int p=0; p<levels[cl]->num_path_segments; p++)
	{
		PathSegment *pps = &levels[cl]->paths[p];
		vdp_gcol(0,1);
		draw_path_segment( pps );
		//TAB(0,1+p);printf("%d: A %d,%d B %d,%d", p, 
		//		pps->A.x, pps->A.y, pps->B.x, pps->B.y);
		for (int n=0;n<3;n++)
		{
			if (pps->nextA[n].key>0)
			{
				vdp_gcol(0,2);
				draw_path_segment( &levels[cl]->paths[pps->nextA[n].seg] );
			}
			if (pps->nextB[n].key>0)
			{
				vdp_gcol(0,3);
				draw_path_segment( &levels[cl]->paths[pps->nextB[n].seg] );
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
		PathSegment *pps = &levels[cl]->paths[curr_path_seg];

		// reduce count in this line segment
		if (pps->count>0)
		{
			pps->count--;
			// also reduce count in connected segments
			if ( is_at_position( ppos, &pps->A ) )
			{
				//printf("at end A of %d  \n", curr_path_seg);
				for (int n=0; n<3; n++)
				{
					if ( pps->nextA[n].key > 0 )
					{
						if ( levels[cl]->paths[ pps->nextA[n].seg ].count > 0)
						{
							levels[cl]->paths[ pps->nextA[n].seg ].count--;
							//printf("reduce %d -> %d to %d  \n",curr_path_seg,  pps->nextA[n].seg, levels[cl]->paths[ pps->nextA[n].seg ].count);
						}
					}
				}
			}
			if ( is_at_position( ppos, &pps->B ) )
			{
				//printf("at end B of %d  \n", curr_path_seg);
				for (int n=0; n<3; n++)
				{
					if ( pps->nextB[n].key > 0 )
					{
						if ( levels[cl]->paths[ pps->nextB[n].seg ].count > 0)
						{
							levels[cl]->paths[ pps->nextB[n].seg ].count--;
							//printf("reduce %d -> %d to %d  \n",curr_path_seg,  pps->nextB[n].seg, levels[cl]->paths[ pps->nextB[n].seg ].count);
						}
					}
				}
			}
			// paint the point with the new colour
			vdp_gcol(0,15);
			vdp_point( ppos->x, ppos->y );

			if ( pps->count == 0 )
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
				if ( levels[cl]->paths[pps->nextA[i].seg].horiz )
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
				if ( levels[cl]->paths[pps->nextB[i].seg].horiz )
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
				if ( !levels[cl]->paths[pps->nextA[i].seg].horiz )
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
				if ( !levels[cl]->paths[pps->nextB[i].seg].horiz )
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
	for (int s=0; s < levels[cl]->num_shapes; s++)
	{
		Shape *pshape = &levels[cl]->shapes[s];
		bool shape_complete = pshape->complete;
		if (!shape_complete)
		{
			int incomplete = pshape->num_segments;
			for (int seg=0; seg < pshape->num_segments; seg++)
			{
				if ( levels[cl]->paths[ pshape->segments[seg] ].count <= 0 )
				{
					pshape->seg_complete[seg] = true;
					incomplete--;
				}
			}
			if (incomplete==0) {
				 pshape->complete = true;
				 fill_shape( s, false );
				 score += pshape->value;
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
		score += levels[cl]->bonus;
		level_complete = true;
	}
}

void fill_shape( int s, bool fast )
{
	Shape *pshape = &levels[cl]->shapes[s];
	vdp_gcol(0, levels[cl]->colour_fill);
	int x1 = pshape->TopLeft.x;
	int y1 = pshape->TopLeft.y;
	int x2 = pshape->BotRight.x;
	int y2 = pshape->BotRight.y;
	// fast fill:
	if (fast)
	{
		vdp_move_to( pshape->TopLeft.x+1, pshape->TopLeft.y+1 );
		vdp_filled_rect( pshape->BotRight.x-1, pshape->BotRight.y-1 );
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

bool flash_screen(int repeat, int speed)
{
	clock_t ticks;

	bool flash_on = true;
	bool ret = true;
	for (int rep=0; rep<repeat && ret; rep++)
	{
		//vdp_define_colour( 0, flash_on?15:0, 255, 0, 255 );
		int col = flash_on? 0b00101010 :0;
		vdp_define_colour( 0, col, 0, 0, 0 );
		ticks = clock()+speed;
		while (ticks > clock())
		{
			if ( vdp_check_key_press( KEY_x ) )
			{
				ret = false;
			}
			vdp_update_key_state();
		}
		flash_on = !flash_on;
	}
	vdp_define_colour( 0, 0, 0, 0, 0 );

	return ret;
}

#define DIR_OPP(d) ((d+2)%4)

int direction_choice( int seg, bool at_A )
{
	int dir = 0;
	PathSegment *pps = &levels[cl]->paths[seg];
	if ( pps->horiz )
	{
		if ( pps->A.x > pps->B.x )
		{
			if (at_A) dir = BITS_LEFT; else dir = BITS_RIGHT;
		} else {
			if (at_A) dir = BITS_RIGHT; else dir = BITS_LEFT;
		}
	}
	else
	{
		if ( pps->A.y > pps->B.y )
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
	for (int en=0; en<levels[cl]->num_enemies; en++)
	{
		PathSegment *pps = &levels[cl]->paths[enemy_curr_segment[en]];
		//for debug printf below
		//int old_seg = enemy_curr_segment[en];
		//int old_dir = enemy_dir[en];

		move_along_path_segment(pps, &enemy_pos[en], &enemy_curr_segment[en], enemy_dir[en], false, 0);

		// choose a new segment
		if ( is_at_position( &enemy_pos[en], &pps->A ) )
		{
			int rand_seg_num = ( rand() % pps->num_validA );
			enemy_curr_segment[en] = pps->nextA[rand_seg_num].seg;
			enemy_dir[en] = pps->nextA[rand_seg_num].key;
			//TAB(0,3);printf("%d: A s:%d(%d) -> s:%d(%d)    \n",en,old_seg,old_dir,enemy_curr_segment[en],enemy_dir[en]);
		}
		if ( is_at_position( &enemy_pos[en], &pps->B ) )
		{
			int rand_seg_num = ( rand() % pps->num_validB );
			enemy_dir[en] = pps->nextB[rand_seg_num].key;
			enemy_curr_segment[en] = pps->nextB[rand_seg_num].seg;
			//TAB(0,3);printf("%d: B s:%d(%d) -> s:%d(%d)    \n",en,old_seg,old_dir,enemy_curr_segment[en],enemy_dir[en]);
		}
		//TAB(0,2);printf("%d: (%d,%d) d:%d s:%d    \n",en,enemy_pos[en].x,enemy_pos[en].y,enemy_dir[en],enemy_curr_segment[en]);
		vdp_select_sprite(en+1);
		vdp_move_sprite_to(enemy_pos[en].x-4, enemy_pos[en].y-4);
		vdp_refresh_sprites();

		// collision
		if ( is_near( enemy_pos[en].x, pos.x, 7 ) &&
			 is_near( enemy_pos[en].y, pos.y, 7 ) )
		{ 
			play_crash();
			flash_screen(2,30);
			vdp_audio_frequency_envelope_disable( 2 );
			vdp_audio_volume_envelope_disable( 2 );
			vdp_audio_disable_channel( 2 );
			lives--;
			if ( lives == 0 )
			{
				end_game = true;
			}
			restart_level = true;
		}
	}
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
void play_crash()
{
	vdp_audio_reset_channel( 2 );
	vdp_audio_set_waveform( 2,VDP_AUDIO_WAVEFORM_VICNOISE );
	vdp_audio_volume_envelope_ADSR( 2,1,10,12*volume,40 );
	vdp_audio_play_note( 2, // channel
		   				20*volume, // volume
						73, // freq
						1000 // duration
						);
}


Level* load_level(char *fname_pattern, int lnum)
{
	FILE *fp;

	char fname[60] = {};
	sprintf(fname, fname_pattern, lnum);

	if ( !(fp = fopen( fname, "rb" ) ) ) {
		printf("Err open %s\n",fname);
		return NULL;
	}

	Level *newlevel = (Level*) calloc(1, sizeof(Level) );
	if (newlevel==NULL)
	{
		return NULL;
	}
	
	wait_clock(10);

	int objs_read = fread( newlevel, sizeof(Level), 1, fp );
	if ( objs_read != 1 || newlevel->version != SAVE_LEVEL_VERSION )
	{
		TAB(25,0);printf("Fail L %d!=1 v%d\n", objs_read, newlevel->version );
		return NULL;
	}
	newlevel->paths = (PathSegment*) calloc(MAX_PATHS, sizeof(PathSegment));
	newlevel->shapes = (Shape*) calloc(MAX_SHAPES, sizeof(Shape));
	if (newlevel->num_path_segments > 0)
	{
		objs_read = fread( newlevel->paths, sizeof(PathSegment), newlevel->num_path_segments, fp );
		if ( objs_read != newlevel->num_path_segments )
		{
			TAB(25,0);printf("Fail P %d!=%d\n", objs_read,  newlevel->num_path_segments);
			return NULL;
		}
	}
	if (newlevel->num_shapes > 0)
	{
		objs_read = fread( newlevel->shapes, sizeof(Shape), newlevel->num_shapes, fp );
		if ( objs_read != newlevel->num_shapes )
		{
			TAB(25,0);printf("Fail S %d!=%d\n", objs_read,  newlevel->num_shapes);
			return NULL;
		}
	}
	TAB(25,0);printf("OK             ");

	switch (lnum)
	{
		case 0:
			break;
		case 1: 
			newlevel->bonus = 2000; 
			break;
		case 2: 
			newlevel->bonus = 2600; 
			break;
		case 3: 
			newlevel->bonus = 3000; 
			break;
		case 4: 
		case 5: 
			newlevel->bonus = 4000; 
			newlevel->num_enemies = 2;
			break;
		default: 
			newlevel->bonus = 5000; 
			newlevel->num_enemies = 3;
			break;
	}

	return newlevel;
}

#if 0
int curr_print_fcol = 15;
int curr_print_bcol = 0;
void PRINT(int X, int Y, int fcol, int bcol, char *str)
{
	TAB(X,Y);
	if (bcol != curr_print_bcol)
	{
		curr_print_bcol = bcol;
	}
}
#endif


bool intro_screen1()
{
 	vdp_clear_screen();
	vdp_gcol(0,11);
	vdp_move_to(0,0);
	vdp_filled_rect(319,39);

	vdp_gcol(0,12);
	vdp_move_to(104,8);
	vdp_filled_rect(223,31);
	COL(11);COL(140);TAB(14,2);printf("P A I N T E R");
	COL(13);COL(139);TAB(4,2);printf("AGON");TAB(32,2);printf("AGON");

	COL(128);
	COL(14);TAB(4,6);printf("Use your");COL(10);printf(" PAINTER ");COL(14);printf("to fill in the");
	COL(14);TAB(2,7);printf("boxes by completing the lines around");
	COL(14);TAB(2,8);printf("them.  You must fill in all the boxes");
	COL(14);TAB(2,9);printf("before the");COL(13);printf(" BONUS ");COL(14);printf("value reaches zero.");

	COL(13);TAB(4,12);printf("To avoid the");COL(11);printf(" CHASERS ");COL(13);printf("you may fire");
	COL(13);TAB(2,13);printf("up to 3 temporary ");COL(15);printf("GAPS ");COL(13);printf("in the lines");
	COL(13);TAB(2,14);printf("by pressing the ");COL(14);printf("SPACE BAR");

	COL(14);TAB(4,17);printf("The skill factor affects both the");
	COL(14);TAB(2,18);printf("speed and intelligence of the chasers.");

	COL(13);TAB(4,21);printf("You have 3 lives, with a bonus life");
	COL(13);TAB(2,22);printf("if you reach a score of ");COL(15);printf("50000.");

	COL(15);TAB(11,25);printf("Press SPACE to continue");

	COL(15);COL(128);

	uint8_t key_pressed = wait_for_key_with_exit(KEY_space, KEY_x);
	if (key_pressed == 0) return false;
	return true;
}

bool intro_screen2()
{
 	vdp_clear_screen();
	vdp_gcol(0,11);
	vdp_move_to(0,0);
	vdp_filled_rect(319,39);

	vdp_gcol(0,12);
	vdp_move_to(104,8);
	vdp_filled_rect(223,31);
	COL(11);COL(140);TAB(14,2);printf("P A I N T E R");
	COL(13);COL(139);TAB(4,2);printf("AGON");TAB(32,2);printf("AGON");

	COL(13);COL(139);TAB(13,12);printf("  Skill Level  ");
	COL(128);
	COL(15);TAB(20,14);printf("%d", skill);
	COL(14);TAB(6,25);printf("Press");COL(15);printf(" SPACE BAR ");COL(14);printf("to start game");

	COL(15);COL(128);
	
	uint8_t key_pressed = wait_for_key_with_exit(KEY_space, KEY_x);
	if (key_pressed == 0) return false;
	return true;
}
