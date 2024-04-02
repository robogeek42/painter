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
} PathSegment;

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

typedef struct {
	int level_num;
	int num_path_segments;
	PathSegment *paths;
	int num_shapes;
	Shape *shapes;
	int colour_unpainted_line;
	int colour_painted_line;
	int colour_fill;
	int bonus;
	int num_enemies;
} Level;

Level *level;

#define MAX_PATHS 50
#define MAX_SHAPES 20

// counters
clock_t key_wait_ticks;

void wait();
void game_loop();
void draw_level();
void draw_level_debug();
void set_point( Position *ppos );
void draw_rulers();
void draw_screen();
void draw_path_segment( PathSegment *pps );
bool is_at_position(Position *pposA, Position *pposB);
void copy_position(Position *pFrom, Position *pTo);
void fill_shape( int s );

Level* load_level(char *fname, int level_num);
void save_level(char *name, Level *level);
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

	vdp_audio_reset_channel( 0 );
	vdp_audio_reset_channel( 1 );

	level = create_blank_level();

	game_loop();

	vdp_mode(0);
	vdp_logical_scr_dims(true);
	vdp_cursor_enable( true );
	return 0;
}

void clear_line(int y)
{
	TAB(0,y);
	for ( int i=0; i<40; i++ )
	{
		printf(" ");
	}
}

int input_int(int x, int y, char *msg)
{
	char buf[30];
	TAB(x,y);
	printf("%s:",msg);
	scanf("%[^\n]s",buf);
	clear_line(y);
	return atoi(buf);
}
char input_char(int x, int y, char *msg)
{
	char buf[30];
	TAB(x,y);
	printf("%s:",msg);
	scanf("%[^\n]s",buf);
	clear_line(y);
	return buf[0];
}
void game_loop()
{
	int exit=0;
	
	key_wait_ticks = clock();

	vdp_clear_screen();

	do {
		if (changed)
		{
			draw_screen();
			draw_rulers();
			draw_level();
			changed=false;
		}

		if ( vdp_check_key_press( KEY_p ) )
		{
			if (key_wait_ticks < clock()) 
			{
				key_wait_ticks = clock() + key_wait;
			}
			
			Position A, B;
			bool horiz;
			char horv = input_char(0,1,"Horiz or Vert");
			bool ok = false;
			if ( horv == 'h' || horv == 'H' )
			{
				A.y = input_int(0,1,"Enter Y");
				B.y = A.y;
				
				A.x = input_int(0,1,"Enter A.x");
				B.x = input_int(0,1,"Enter B.x");
				horiz = true;
				if ( A.x != B.x && A.x < 320 && B.x < 320 )
				{
					ok = true;
				}
			}
			else if ( horv == 'v' || horv == 'V' )
			{
				A.x = input_int(0,1,"Enter X");
				B.x = A.x;
				
				A.y = input_int(0,1,"Enter A.y");
				B.y = input_int(0,1,"Enter B.y");
				horiz = false;
				if ( A.y != B.y && A.y < 240 && B.y < 240 )
				{
					ok = true;
				}
			} 
			if ( ok && level )
			{
				level->paths[level->num_path_segments].A.x = A.x;
				level->paths[level->num_path_segments].A.y = A.y;
				level->paths[level->num_path_segments].B.x = B.x;
				level->paths[level->num_path_segments].B.y = B.y;
				level->paths[level->num_path_segments].horiz = horiz;
				COL(7);TAB(28,1);printf("Added seg:%d",level->num_path_segments);
				level->num_path_segments++;
				changed=true;
			}
			
		}

		if ( vdp_check_key_press( KEY_v ) )  // show each path seg in turn
		{
			if (key_wait_ticks < clock()) 
			{
				key_wait_ticks = clock() + key_wait;
				draw_level_debug();
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
	TAB(8,0); printf("Edges:%d",level->num_path_segments);
	TAB(17,0);printf("Shapes:%d",level->num_shapes);

	print_key(0,28,"","D","ebug");
	print_key(7,28,"","P", "ath");
	print_key(12,28,"","S","hape");
	print_key(18,28,"","C","onnect");
}

void draw_path_segment( PathSegment *pps ) {
	vdp_move_to( pps->A.x, pps->A.y );
	vdp_line_to( pps->B.x, pps->B.y );
}

void draw_level()
{
	vdp_gcol(0, 8);
	for (int p = 0; p < level->num_path_segments; p++)
	{
		draw_path_segment( &level->paths[p] );
	}
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
	draw_level();
	for (int p=0; p<level->num_path_segments; p++)
	{
		vdp_gcol(0,1);
		draw_path_segment( &level->paths[p] );
		//TAB(0,1+p);printf("%d: A %d,%d B %d,%d", p, 
		//		paths[p].A.x, paths[p].A.y, paths[p].B.x, paths[p].B.y);
		for (int n=0;n<3;n++)
		{
			if (level->paths[p].nextA[n].key>0)
			{
				vdp_gcol(0,2);
				draw_path_segment( &level->paths[level->paths[p].nextA[n].seg] );
			}
			if (level->paths[p].nextB[n].key>0)
			{
				vdp_gcol(0,3);
				draw_path_segment( &level->paths[level->paths[p].nextB[n].seg] );
			}
		}
		wait();
		vdp_clear_screen();
		vdp_activate_sprites( 1 ); //  have to reactivate sprites after a clear
		draw_level();
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

void fill_shape( int s )
{
	vdp_gcol(0, level->shapes[s].colour);
	int x1 = level->shapes[s].TopLeft.x;
	int y1 = level->shapes[s].TopLeft.y;
	int x2 = level->shapes[s].BotRight.x;
	int y2 = level->shapes[s].BotRight.y;
	vdp_move_to( x1 + 1, y1 + 1 );
	vdp_filled_rect( x2 - 1, y2 - 1 );
}

#define DIR_OPP(d) ((d+2)%4)

Level* load_level(char *fname, int level_num)
{
	// Open the file
	FILE *fp;
	int bytes_read = 0;
	char buffer[100];

	if ( !(fp = fopen( fname, "rb" ) ) ) return NULL;
	bytes_read = fread( buffer, 1, 100, fp );

	Level *newlevel = (Level*) calloc(1, sizeof(Level) );
	newlevel->level_num = level_num;

	// read the info about the level
	//  - number of paths
	//  - number of shapes

	newlevel->paths = (PathSegment*) calloc(newlevel->num_path_segments, sizeof(PathSegment));
	newlevel->shapes = (Shape*) calloc(newlevel->num_shapes, sizeof(Shape));

	return newlevel;
}

void save_level(char *name, Level *level)
{
}

Level *create_blank_level()
{
	Level *level = (Level*) calloc(1, sizeof(Level) );
	level->num_path_segments = 0;
	level->paths = (PathSegment*) calloc(MAX_PATHS, sizeof(PathSegment));
	level->num_shapes = 0;
	level->shapes = (Shape*) calloc(MAX_SHAPES, sizeof(Shape));

	return level;
}
