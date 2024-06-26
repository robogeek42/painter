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

const char* KEY_STRING[16] = {
	"-",	// 0 0000
	"U",	// 1 0001
	"R",	// 2 0010
	"UR",	// 3 0011
	"D",	// 4 0100
	"UD",	// 5 0101
	"RD",	// 6 0110
	"URD",  // 7 0111
	"L",	// 8 1000
	"UL",	// 9 0001
	"RL",	// A 0010
	"URL",	// B 0011
	"DL",	// C 0100
	"UDL",	// D 0101
	"RDL",	// E 0110
	"URDL", // F 0111
};

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

Level *level;

#define MAX_PATHS 100
#define MAX_SHAPES 25

// counters
clock_t key_wait_ticks;

void wait();
void game_loop();
void draw_level(bool fill);
void draw_level_debug();
void set_point( Position *ppos );
void draw_rulers();
void draw_screen();
void draw_path_segment( PathSegment *pps );
bool is_at_position(Position *pposA, Position *pposB);
void copy_position(Position *pFrom, Position *pTo);
void fill_shape( int s );
void print_level_data();

Level* load_level(char *fname);
bool save_level(char *name, Level *level);
Level* create_blank_level();

void wait()
{
	char k=getchar();
	if (k=='q') exit(0);
}

static volatile SYSVAR *sys_vars = NULL;

int changed=true;
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

	level = create_blank_level();

	game_loop();

	vdp_mode(0);
	vdp_logical_scr_dims(true);
	vdp_cursor_enable( true );
	return 0;
}

bool input_path_segment_horiz(PathSegment *pps)
{
	pps->A.y = input_int(0,1,"Enter Y");
	pps->B.y = pps->A.y;
	
	pps->A.x = input_int(0,1,"Enter A.x");
	pps->B.x = input_int(0,1,"Enter B.x");
	pps->horiz = true;

	pps->count = abs(pps->B.x - pps->A.x) + 1;

	if ( pps->A.x == pps->B.x ) return false;
	if ( pps->A.x >= 320 || pps->B.x >= 320) return false;

	return true;
}
bool input_path_segment_vert(PathSegment *pps)
{
	pps->A.x = input_int(0,1,"Enter X");
	pps->B.x = pps->A.x;
	
	pps->A.y = input_int(0,1,"Enter A.y");
	pps->B.y = input_int(0,1,"Enter B.y");
	pps->horiz = false;

	pps->count = abs(pps->B.y - pps->A.y) + 1;

	if ( pps->A.y == pps->B.y ) return false;
	if ( pps->A.y >= 240 || pps->B.y >= 240) return false;

	return true;
}

void set_path_counts(Level *lvl)
{
	for (int p=0; p < lvl->num_path_segments; p++)
	{
		PathSegment *pps = &lvl->paths[p];
		if (pps->horiz)
		{
			pps->count = abs(pps->B.x - pps->A.x) + 1;
		} else {
			pps->count = abs(pps->B.y - pps->A.y) + 1;
		}
	}
}

bool enter_path_segment( int seg )
{
	bool changed = false;
	PathSegment ps;
	clear_line(1);
	char horv = input_char(0,1,"Horiz or Vert");
	bool ok = false;
	if ( horv == 'h' || horv == 'H' )
	{
		ok = input_path_segment_horiz( &ps );
	}
	else if ( horv == 'v' || horv == 'V' )
	{
		ok = input_path_segment_vert( &ps );
	} 
	if ( ok && level )
	{
		level->paths[seg].A.x = ps.A.x;
		level->paths[seg].A.y = ps.A.y;
		level->paths[seg].B.x = ps.B.x;
		level->paths[seg].B.y = ps.B.y;
		level->paths[seg].horiz = ps.horiz;
		level->paths[seg].count = ps.count;
		COL(7);TAB(0,1);printf("Added %d (%d,%d)->(%d,%d) c:%d",seg,
				ps.A.x,ps.A.y,ps.B.x,ps.B.y, ps.count);
		changed=true;
	} else {
		COL(7);TAB(0,1);printf("Failed (%d,%d)->(%d,%d)",
				ps.A.x,ps.A.y,ps.B.x,ps.B.y);
	}
	return changed;
}

void clear_drawing()
{
	vdp_move_to(4,20);
	vdp_gcol(0,0);
	vdp_filled_rect(319,223);
}

int find_ends(Position pos, int *endslist)
{
	int count=0;

	for (int i=0; i<level->num_path_segments; i++)
	{
		if ( pos.x == level->paths[i].A.x && pos.y == level->paths[i].A.y )
		{
			endslist[count++] = i;
		}
		if ( pos.x == level->paths[i].B.x && pos.y == level->paths[i].B.y )
		{
			endslist[count++] = i;
		}
	}

	return count;
}

bool is_before(PathSegment *a, PathSegment *b, bool check_x)
{
	if (check_x)
	{
		int max_a = MAX(a->A.x, a->B.x);
		int min_b = MIN(b->A.x, b->B.x);
		return (max_a <= min_b);
	} else {
		int max_a = MAX(a->A.y, a->B.y);
		int min_b = MIN(b->A.y, b->B.y);
		return (max_a <= min_b);
	}
}
int getNextDir(PathSegment *from, PathSegment *to)
{
	// work out direction key from seg to next
	// case 1 - both horizontal
	if ( from->horiz == true && to->horiz == true)
	{
		if (is_before(from, to, true)) 
		{
			return BITS_RIGHT;
		} else {
			return BITS_LEFT;
		}
	} 
	// case 2 - both vertical
	if ( from->horiz == false && to->horiz == false)
	{
		if (is_before(from, to, false)) 
		{
			return BITS_DOWN;
		} else {
			return BITS_UP;
		}
	} 
	// case 3 - from horizontal to vertical
	if ( from->horiz == true && to->horiz == false )
	{
		if (is_before(from, to, false)) 
		{
			return BITS_DOWN;
		} else {
			return BITS_UP;
		}
	}
	// case 4 - from vertical to horizontal
	if ( from->horiz == false && to->horiz == true )
	{
		if (is_before(from, to, true)) 
		{
			return BITS_RIGHT;
		} else {
			return BITS_LEFT;
		}
	}
	return 0;
}

void recalc_shape(int snum)
{
	// get TopLeft and BottomRight
	Position pos_tl, pos_br;
	pos_tl.x = 1000; pos_tl.y = 1000;
	pos_br.x = 0; pos_br.y = 0;

	for (int s=0;s<level->shapes[snum].num_segments;s++)
	{
		if ( level->paths[level->shapes[snum].segments[s]].A.x < pos_tl.x ) pos_tl.x = level->paths[level->shapes[snum].segments[s]].A.x;
		if ( level->paths[level->shapes[snum].segments[s]].B.x < pos_tl.x ) pos_tl.x = level->paths[level->shapes[snum].segments[s]].B.x;
		if ( level->paths[level->shapes[snum].segments[s]].A.y < pos_tl.y ) pos_tl.y = level->paths[level->shapes[snum].segments[s]].A.y;
		if ( level->paths[level->shapes[snum].segments[s]].B.y < pos_tl.y ) pos_tl.y = level->paths[level->shapes[snum].segments[s]].B.y;

		if ( level->paths[level->shapes[snum].segments[s]].A.x > pos_br.x ) pos_br.x = level->paths[level->shapes[snum].segments[s]].A.x;
		if ( level->paths[level->shapes[snum].segments[s]].B.x > pos_br.x ) pos_br.x = level->paths[level->shapes[snum].segments[s]].B.x;
		if ( level->paths[level->shapes[snum].segments[s]].A.y > pos_br.y ) pos_br.y = level->paths[level->shapes[snum].segments[s]].A.y;
		if ( level->paths[level->shapes[snum].segments[s]].B.y > pos_br.y ) pos_br.y = level->paths[level->shapes[snum].segments[s]].B.y;
	}
	level->shapes[snum].TopLeft.x = pos_tl.x;
	level->shapes[snum].TopLeft.y = pos_tl.y;
	level->shapes[snum].BotRight.x = pos_br.x;
	level->shapes[snum].BotRight.y = pos_br.y;
}

bool enter_shape(int snum)
{
	bool ok = true;
	int segs[MAX_SHAPE_SEGS];
	int num_segs = 0;
	for (int s=0;s<MAX_SHAPE_SEGS;s++)
	{
		clear_line(1);
		TAB(0,1);printf("Shape: seg %d (-1 = stop) ",s);
		segs[s] = input_int(27,1,"?");
		if (segs[s] < 0 ) break;
		if (segs[s] >= level->num_path_segments )
		{ 
			ok = false;
			break;
		}
		vdp_gcol(0,15);
		draw_path_segment( &level->paths[segs[s]] );
		num_segs++;
	}
	if (num_segs < 4) {
		clear_line(1);
		TAB(0,1);printf("Too few segments %d\n",num_segs);
		ok = false;
	}
	if ( ok )
	{
		// check they are all different
		for (int s=0; s < num_segs - 1; s++)
		{
			for (int c=s+1; c < num_segs ;c++)
			{
				if (segs[s]==segs[c]) 
				{
					clear_line(1);
					TAB(0,1);printf("segs same %d(%d) %d(%d)  \n",s,segs[s],c,segs[c]);
					ok = false;
					break;
				}
			}
		}
	}
	if ( ok )
	{
		// copy the segements into the shape in the level
		for (int s=0;s<num_segs;s++)
		{
			level->shapes[snum].segments[s] = segs[s];
		}
		clear_line(1);
		level->shapes[snum].value = input_int(0,1,"Score:");
		level->shapes[snum].num_segments = num_segs;
		level->shapes[snum].complete = false;

		recalc_shape( snum );

	}
	return ok;
}


void game_loop()
{
	int exit=0;
	
	key_wait_ticks = clock();

	level->colour_fill = 1; // default, can set for a map

	vdp_clear_screen();

	do {
		if (changed)
		{
			clear_drawing();

			draw_screen();
			draw_rulers();
			draw_level(true);
			changed=false;
		}

		if ( vdp_check_key_press( KEY_p ) )
		{
			if (key_wait_ticks < clock()) 
			{
				key_wait_ticks = clock() + key_wait;
				if ( enter_path_segment( level->num_path_segments ) )
				{
					level->num_path_segments++;
					changed=true;
				}
			}
		}
		if ( vdp_check_key_press( KEY_h ) )
		{
			if (key_wait_ticks < clock()) 
			{
				key_wait_ticks = clock() + key_wait;
			
				PathSegment ps;
				clear_line(1);
				bool ok = false;
				ok = input_path_segment_horiz( &ps );
				if ( ok && level )
				{
					level->paths[level->num_path_segments].A.x = ps.A.x;
					level->paths[level->num_path_segments].A.y = ps.A.y;
					level->paths[level->num_path_segments].B.x = ps.B.x;
					level->paths[level->num_path_segments].B.y = ps.B.y;
					level->paths[level->num_path_segments].horiz = ps.horiz;
					COL(7);TAB(0,1);printf("Added seg:%d (%d,%d)->(%d,%d)",level->num_path_segments,
							ps.A.x,ps.A.y,ps.B.x,ps.B.y);
					level->num_path_segments++;
					changed=true;
				} else {
					COL(7);TAB(0,1);printf("Failed seg: (%d,%d)->(%d,%d)",
							ps.A.x,ps.A.y,ps.B.x,ps.B.y);
				}

			} 
		}
		if ( vdp_check_key_press( KEY_v ) )
		{
			if (key_wait_ticks < clock()) 
			{
				key_wait_ticks = clock() + key_wait;
			
				PathSegment ps;
				clear_line(1);
				bool ok = false;
				ok = input_path_segment_vert( &ps );
				if ( ok && level )
				{
					level->paths[level->num_path_segments].A.x = ps.A.x;
					level->paths[level->num_path_segments].A.y = ps.A.y;
					level->paths[level->num_path_segments].B.x = ps.B.x;
					level->paths[level->num_path_segments].B.y = ps.B.y;
					level->paths[level->num_path_segments].horiz = ps.horiz;
					COL(7);TAB(0,1);printf("Added seg:%d (%d,%d)->(%d,%d)",level->num_path_segments,
							ps.A.x,ps.A.y,ps.B.x,ps.B.y);
					level->num_path_segments++;
					changed=true;
				}
			}
		}

		if ( vdp_check_key_press( KEY_d ) )  // show each path seg in turn
		{
			if (key_wait_ticks < clock()) 
			{
				key_wait_ticks = clock() + key_wait;
				clear_line(1);
				TAB(0,1); printf("Debug:");
				//vdp_mode(0);
				//print_level_data();
				//vdp_mode(9);
				draw_level_debug();
				clear_line(1);
				changed=true;
			}
		}

		if ( vdp_check_key_press( KEY_o ) )  // o change fill colour for this level
		{
			if (key_wait_ticks < clock()) 
			{
				key_wait_ticks = clock() + key_wait;
				clear_line(1);
				level->colour_fill = input_int(0,1,"Fill colour?");
				clear_line(1);
				changed=true;
			}
		}

		if ( vdp_check_key_press( KEY_m ) )  // modify all values
		{
			if (key_wait_ticks < clock()) 
			{
				key_wait_ticks = clock() + key_wait;
				clear_line(1);
				char horv = input_char(0,1,"Horiz or Vert");
				clear_line(1);
				int val  = input_int(0,1,"Enter value to change");
				clear_line(1);
				int newval  = input_int(0,1,"Enter new value");
				
				for (int seg=0; seg < level->num_path_segments; seg++)
				{
					if ( horv == 'v' || horv == 'V' )
					{
						if ( level->paths[seg].A.x == val ) level->paths[seg].A.x = newval; 
						if ( level->paths[seg].B.x == val ) level->paths[seg].B.x = newval; 
					}

					if ( horv == 'h' || horv == 'H' )
					{
						if ( level->paths[seg].A.y == val ) level->paths[seg].A.y = newval; 
						if ( level->paths[seg].B.y == val ) level->paths[seg].B.y = newval; 
					}
				}
				for (int sh=0; sh < level->num_shapes; sh++)
				{
					recalc_shape(sh);
				}
				changed=true;
			}
		}
		if ( vdp_check_key_press( KEY_delete ) )  // delete most recent segment only
		{
			if (key_wait_ticks < clock()) 
			{
				key_wait_ticks = clock() + key_wait;
				clear_line(1);
				TAB(0,1);printf("Del seg %d: Are you sure?",level->num_path_segments-1);
				char k=getchar(); 
				if (k=='y' || k=='Y') {
					level->num_path_segments--;
				}
				changed=true;
			}
		}
		if ( vdp_check_key_press( KEY_minus ) )  // delete most recent shape
		{
			if (key_wait_ticks < clock()) 
			{
				key_wait_ticks = clock() + key_wait;
				clear_line(1);
				TAB(0,1);printf("Del shape %d: Are you sure?",level->num_shapes-1);
				char k=getchar(); 
				if (k=='y' || k=='Y') {
					level->num_shapes--;
				}
				changed=true;
			}
		}
		if ( vdp_check_key_press( KEY_e ) )  // Edit a path segment or shape
		{
			if (key_wait_ticks < clock()) 
			{
				key_wait_ticks = clock() + key_wait;

				clear_line(1);
				char pos = input_char(0,1,"Path or Shape?");
				clear_line(1);
				if (pos == 'p' || pos == 'P' )
				{
					int seg = input_int(0,1,"Enter seg to edit:");

					PathSegment *ps = &level->paths[seg];
					TAB(0,28);COL(15);printf("s:%d (%d,%d)->(%d,%d) c:%d",seg, ps->A.x,ps->A.y,ps->B.x,ps->B.y, ps->count);
					
					if ( enter_path_segment( seg ) )
					{
						changed=true;
					}
				}
				if (pos == 's' || pos == 'S' )
				{
					int sh = input_int(0,1,"Enter shape to edit:");
					if (sh < level->num_shapes )
					{
						if ( !enter_shape( sh ) )
						{
							TAB(30,0);printf("Error");
						}
						changed=true;
					}
				}
			}
		}
		if ( vdp_check_key_press( KEY_s ) )  // Enter a shape
		{
			if (key_wait_ticks < clock()) 
			{
				key_wait_ticks = clock() + key_wait;
				if ( enter_shape( level->num_shapes ) )
				{
					level->num_shapes++;
				} else {
					TAB(30,0);printf("Error");
				}
				changed=true;
			}
		}

		if ( vdp_check_key_press( KEY_f ) ) // file command
		{
			if (key_wait_ticks < clock()) 
			{
				key_wait_ticks = clock() + key_wait;
				char filename[42];

				clear_line(1);
				char lors = input_char(0,1,"Load or Save?");
				if ( lors == 's' || lors == 'S' )
				{
					clear_line(1);
					input_string(0,1,"Filename:", filename, 42);
					bool ret = save_level(filename, level);
					if (!ret) {
						TAB(25,0);printf("Fail to save");
					}
				} 
				if ( lors == 'l' || lors == 'L' )
				{
					clear_line(1);
					input_string(0,1,"Filename:", filename, 42);
					Level *newlevel = load_level(filename);
					if (newlevel!=NULL)
					{
						if (level->paths) free(level->paths);
						if (level->shapes) free(level->shapes);
						free(level);
						level = newlevel;
						changed = true;
					} 
				}
				clear_line(1);
			}
		}
		if ( vdp_check_key_press( KEY_c ) ) // connections
		{
			if (key_wait_ticks < clock()) 
			{
				key_wait_ticks = clock() + key_wait;
				clear_line(1);
				TAB(0,1);printf("Update conns");
				// clear current connections
				for (int seg=0; seg < level->num_path_segments; seg++)
				{
					level->paths[seg].num_validA = 0;
					level->paths[seg].num_validB = 0;
					for (int i=0; i < 3; i++)
					{
						level->paths[seg].nextA[i].seg = 0;
						level->paths[seg].nextA[i].key = 0;
						level->paths[seg].nextB[i].seg = 0;
						level->paths[seg].nextB[i].key = 0;
					}
				}
				// find ends of each segment and get direction to move to get there
				for (int seg=0; seg < level->num_path_segments; seg++)
				{
					PathSegment *pps = &level->paths[seg];
					int endslistA[10] = {}; 
					int endslistB[10] = {}; 
					int endslist_count=0;
					endslist_count = find_ends(pps->A, endslistA);
					int ind=0;
					for (int i=0; i < endslist_count; i++)
					{
						if (endslistA[i] == seg) continue;

						pps->nextA[ind].seg = endslistA[i];
						pps->nextA[ind].key = getNextDir(pps, &level->paths[endslistA[i]]);
						ind++;
					}
					pps->num_validA = ind;
					endslist_count = find_ends(pps->B, endslistB);
					ind=0;
					for (int i=0; i < endslist_count; i++)
					{
						if (endslistB[i] == seg) continue;

						pps->nextB[ind].seg = endslistB[i];
						pps->nextB[ind].key = getNextDir(pps, &level->paths[endslistB[i]]);
						ind++;
					}
					pps->num_validB = ind;
				}
			}
			TAB(13,1);printf("Done. Shapes ");
			for (int sh=0; sh < level->num_shapes; sh++)
			{
				recalc_shape(sh);
			}
			TAB(25,1);printf("Done.");
			clear_drawing();
			draw_level(true);
		}

		if ( vdp_check_key_press( KEY_x ) ) // x - exit
		{
			clear_line(1);
			TAB(0,1);printf("Are you sure?");
			char k=getchar(); 
			if (k=='y' || k=='Y') exit=1;
			else 
			{
				clear_line(1);
			}
		}

		vdp_update_key_state();
	} while (exit==0);

}

void print_key(int x, int y, char *pre, char *key, char* post)
{
	vdp_set_text_colour(2);
	TAB(x,y); printf("%s",pre);
	vdp_set_text_colour(5);
	printf("%s",key);
	vdp_set_text_colour(2);
	printf("%s",post);
}

void draw_rulers()
{
	vdp_gcol(0,6);
	vdp_move_to(0,17);
	vdp_line_to(319,17);
	for (int l=0; l<320; l+=10)
	{
		vdp_move_to(l,17);
		if (l%100==0)
		{
			vdp_line_to(l,20);
		} else {
			vdp_line_to(l,18);
		}
	}
	vdp_move_to(0,20);
	vdp_line_to(0,219);
	for (int l=20; l<220; l+=10)
	{
		vdp_move_to(0,l);
		if (l%100==0)
		{
			vdp_line_to(3,l);
		} else {
			vdp_line_to(1,l);
		}
	}
}
void draw_screen()
{
	vdp_set_text_colour(11);
	TAB(0,0); printf("EDITOR"); 
	TAB(8,0); printf("E:%d",level->num_path_segments);
	TAB(13,0);printf("S:%d",level->num_shapes);
	TAB(18,0);printf("%d",sizeof(Level));

	print_key(0,29,"","D","ebug");
	print_key(6,29,"","hvP", "ath");
	print_key(13,29,"","S","hape");
	print_key(19,29,"","C","onnect");
	print_key(27,29,"","F","ile");
	print_key(32,29,"c","O","l");
	print_key(36,29,"","M","od");
}

void draw_path_segment( PathSegment *pps ) {
	vdp_move_to( pps->A.x, pps->A.y );
	vdp_line_to( pps->B.x, pps->B.y );

	vdp_move_to( pps->A.x-1, pps->A.y-1 );
	vdp_filled_rect( pps->A.x+1, pps->A.y+1 );

	vdp_move_to( pps->B.x-1, pps->B.y-1 );
	vdp_filled_rect( pps->B.x+1, pps->B.y+1 );
}
// side 1 or -1
void draw_path_segment_label( PathSegment *pps, char *str, int col, int offset ) {
	vdp_write_at_graphics_cursor();
	if (pps->horiz)
	{
		int texty;
		if (pps->A.y<120) //draw below line
		{
			texty = pps->A.y+2;
		} else {
			texty = pps->A.y-9;
		}
		int width = abs(pps->A.x - pps->B.x); 
		if (pps->A.x>pps->B.x)
		{
			vdp_move_to( pps->B.x+(width/2)+offset*8, texty );
		} else {
			vdp_move_to( pps->A.x+(width/2)+offset*8, texty );
		}
	} else {
		int textx;
		if (pps->A.x<160) // text to right of line
		{
			textx = pps->A.x+2;
		} else {
			textx = pps->A.x-9;
		}
		int height = abs(pps->A.y - pps->B.y); 
		if (pps->A.y>pps->B.y)
		{
			vdp_move_to( textx, pps->B.y+(height/2)+offset*8 );
		} else {
			vdp_move_to( textx, pps->A.y+(height/2)+offset*8 );
		}
	}
	vdp_gcol(0, col);
	printf("%s",str);
	vdp_write_at_text_cursor();
}

void draw_level(bool fill)
{
	// fill first
	if (fill)
	{
		for ( int s=0; s < level->num_shapes; s++ )
		{
			fill_shape(s);
		}
	}
	// outline with a segment label
	for (int p = 0; p < level->num_path_segments; p++)
	{
		vdp_gcol(0, 8);
		draw_path_segment( &level->paths[p] );
		char str[6]; sprintf(str,"%d",p);
		draw_path_segment_label( &level->paths[p], str, 7, 0 );
	}
	// value of box
	vdp_write_at_graphics_cursor();
	vdp_gcol(0,15);
	for ( int s=0; s < level->num_shapes; s++ )
	{
		int w = level->shapes[s].BotRight.x - level->shapes[s].TopLeft.x;
		int h = level->shapes[s].BotRight.y - level->shapes[s].TopLeft.y;
		vdp_move_to( level->shapes[s].TopLeft.x + (w/2) - 12, level->shapes[s].TopLeft.y + (h/2) - 4);
		printf("%d",level->shapes[s].value);
		level->shapes[s].complete = false;
	}
	vdp_write_at_text_cursor();
}

void draw_level_debug()
{
	for (int p=0; p<level->num_path_segments; p++)
	{
		clear_drawing();
		draw_level(false);
		TAB(25,0);COL(2);printf("A");COL(1);printf("-%d-",p);COL(3);printf("B");COL(15);
		vdp_gcol(0,1);
		draw_path_segment( &level->paths[p] );
		TAB(8,1);printf("%d: A %d,%d B %d,%d", p, 
				level->paths[p].A.x, level->paths[p].A.y, level->paths[p].B.x, level->paths[p].B.y);
		//TAB(8,1);printf("%d %d",level->paths[p].num_validA, level->paths[p].num_validB);
		for (int n=0;n<3;n++)
		{
			PathSegment *pps;
			if (level->paths[p].nextA[n].key>0)
			{
				vdp_gcol(0,2);
				pps =  &level->paths[level->paths[p].nextA[n].seg];
				char str[6];
				sprintf(str,"%s",KEY_STRING[level->paths[p].nextA[n].key]);
				draw_path_segment( pps );
				draw_path_segment_label( pps, str, 2, -1 );
			}
			if (level->paths[p].nextB[n].key>0)
			{
				vdp_gcol(0,3);
				pps =  &level->paths[level->paths[p].nextB[n].seg];
				char str[6];
				sprintf(str,"%s",KEY_STRING[level->paths[p].nextB[n].key]);
				draw_path_segment( pps );
				COL(3);
				draw_path_segment_label( pps, str, 3, -1 );
			}
		}
		//if( !wait_for_any_key_with_exit(KEY_x) ) break;
		wait();
		clear_line(1);
		draw_level(false);
	}
	clear_drawing();
	draw_level(true);
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

void fill_shape( int s )
{
	vdp_gcol(0, level->colour_fill);
	int x1 = level->shapes[s].TopLeft.x;
	int y1 = level->shapes[s].TopLeft.y;
	int x2 = level->shapes[s].BotRight.x;
	int y2 = level->shapes[s].BotRight.y;
	vdp_move_to( x1 + 1, y1 + 1 );
	vdp_filled_rect( x2 - 1, y2 - 1 );
}

void print_level_data()
{
	int byte = 0;
	printf("%3d: version %02x\n",byte,level->version); byte += 1;
	printf("%3d: num seg %02x %02x %02x\n",byte,
			level->num_path_segments & 0xFF,
			(level->num_path_segments >> 8) & 0xFF,
			(level->num_path_segments >> 16) & 0xFF
			); byte += 3;
	printf("%3d: num sha %02x %02x %02x\n",byte,
			level->num_shapes & 0xFF,
			(level->num_shapes >> 8) & 0xFF,
			(level->num_shapes >> 16) & 0xFF
			); byte += 3;
	printf("%3d: col unp %02x %02x %02x\n",byte,
			level->colour_unpainted_line & 0xFF,
			(level->colour_unpainted_line >> 8) & 0xFF,
			(level->colour_unpainted_line >> 16) & 0xFF
			); byte += 3;
	printf("%3d: col pai %02x %02x %02x\n",byte,
			level->colour_painted_line & 0xFF,
			(level->colour_painted_line >> 8) & 0xFF,
			(level->colour_painted_line >> 16) & 0xFF
			); byte += 3;
	printf("%3d: col fil %02x %02x %02x\n",byte,
			level->colour_fill & 0xFF,
			(level->colour_fill >> 8) & 0xFF,
			(level->colour_fill >> 16) & 0xFF
			); byte += 3;
	printf("%3d: bonus   %02x %02x %02x\n",byte,
			level->bonus & 0xFF,
			(level->bonus >> 8) & 0xFF,
			(level->bonus >> 16) & 0xFF
			); byte += 3;
	printf("%3d: num ene %02x\n",byte, level->num_enemies); byte += 1;
	printf("%3d: ppath   %02x %02x %02x\n",byte,
			(uint24_t)level->paths & 0xFF,
			((uint24_t)level->paths >> 8) & 0xFF,
			((uint24_t)level->paths >> 16) & 0xFF
			); byte += 3;
	printf("%3d: pshapes %02x %02x %02x\n",byte,
			(uint24_t)level->shapes & 0xFF,
			((uint24_t)level->shapes >> 8) & 0xFF,
			((uint24_t)level->shapes >> 16) & 0xFF
			); byte += 3;
	for (int i=0;i<level->num_path_segments;i++)
	{
		PathSegment *pps = &level->paths[i];
		printf("s:%d A(%d,%d) B(%d,%d) %s va:%d na[%d %d %d] vb:%d nb[%d %d %d]\n",
				i, pps->A.x, pps->A.y, pps->B.x, pps->B.y, 
				pps->horiz?"H":"V",
				pps->num_validA, pps->nextA[0].seg, pps->nextA[1].seg, pps->nextA[2].seg, 
				pps->num_validB, pps->nextB[0].seg, pps->nextB[1].seg, pps->nextB[2].seg
				);
	/*
	6 Position A;
	6 Position B;
	1 bool horiz;
	18 ValidNext nextA[3]; // the other possible directions to move in from A
	3 int num_validA;
	18 ValidNext nextB[3]; // the other possible directions to move in from B
	3 int num_validB;
	
	*/

	}
	printf("%d bytes",byte);

	wait();
}

#define DIR_OPP(d) ((d+2)%4)

bool save_level(char *fname, Level *level)
{
	// Open the file for writing
	FILE *fp;
	int objs_written = 0;

	if ( !(fp = fopen( fname, "wb" ) ) ) return false;

	objs_written = fwrite( (const void*) level, sizeof(Level), 1, fp);
	if (objs_written!=1) {
		TAB(25,0);printf("Fail %d\n",objs_written);
		fclose(fp);
		return false;
	}
	
	if (level->num_path_segments > 0)
	{
		objs_written = fwrite( (const void*) level->paths, sizeof(PathSegment), level->num_path_segments, fp);
		if ( objs_written != level->num_path_segments )
		{
			TAB(25,0);printf("Fail P %d!=%d", objs_written,  level->num_path_segments);
			fclose(fp);
			return false;
		}
	}
	if (level->num_shapes > 0)
	{
		objs_written = fwrite( (const void*) level->shapes, sizeof(Shape), level->num_shapes, fp);
		if ( objs_written != level->num_shapes )
		{
			TAB(25,0);printf("Fail S %d!=%d", objs_written,  level->num_shapes);
			fclose(fp);
			return false;
		}
	}

	TAB(25,0);printf("OK             ");
	
	fclose(fp);

	return true;
}

Level* load_level(char *fname)
{
	FILE *fp;
	
	if ( !(fp = fopen( fname, "rb" ) ) ) return NULL;

	Level *newlevel = (Level*) calloc(1, sizeof(Level) );
	
	int objs_read = fread( newlevel, sizeof(Level), 1, fp );
	if ( objs_read != 1 || newlevel->version != SAVE_LEVEL_VERSION )
	{
		TAB(25,0);printf("Fail L %d!=1 v%d\n", objs_read, newlevel->version );
		fclose(fp);
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
			fclose(fp);
			return NULL;
		}
		set_path_counts(newlevel);
	}
	if (newlevel->num_shapes > 0)
	{
		objs_read = fread( newlevel->shapes, sizeof(Shape), newlevel->num_shapes, fp );
		if ( objs_read != newlevel->num_shapes )
		{
			TAB(25,0);printf("Fail S %d!=%d\n", objs_read,  newlevel->num_shapes);
			fclose(fp);
			return NULL;
		}
	}
	TAB(25,0);printf("OK             ");

	return newlevel;
}

Level *create_blank_level()
{
	Level *level = (Level*) calloc(1, sizeof(Level) );
	level->version = SAVE_LEVEL_VERSION;
	level->num_path_segments = 0;
	level->num_shapes = 0;
	level->colour_unpainted_line = 8;
	level->colour_painted_line = 15;
	level->colour_fill = 1;
	level->bonus = 1000;
	level->num_enemies = 1;
	level->paths = (PathSegment*) calloc(MAX_PATHS, sizeof(PathSegment));
	level->shapes = (Shape*) calloc(MAX_SHAPES, sizeof(Shape));

	return level;
}
