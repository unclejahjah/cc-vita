// Level scenes: the parallax layers of a LEVELn.swf, positioned by the same
// camera rules as the game's f_UpdateCamera (MAIN.swf).
#ifndef CC_LEVEL_H
#define CC_LEVEL_H

#include "render.h"

#define GAME_W 848.0f   // the game's stage, in pixels
#define GAME_H 480.0f
#define MAX_LAYERS 8

typedef struct {
    const Place *container;    // root place, e.g. "bg1_c"
    const Place *inner;        // its scrolling child, e.g. "bg1"
    float factor;              // parallax: inner position = camera * factor
    float scale_mul, scale_add;// container scale % = zoom % * mul + add
    int is_game;               // the play layer (characters are drawn into it)
} Layer;

// A collision edge, in play-layer pixels. type is the segment clip's n_type
// (1-4 walls, 100/101 stairs, 300 ladder, 400 ... as the game's scripts use them).
typedef struct {
    float x0, y0, x1, y1;
    int type;
} Segment;

// A play-layer object the game depth-sorts with the characters (by y).
typedef struct {
    const Place *place;
    float y;
} SortedPlace;

typedef struct {
    Movie *m;
    int nsegs;
    Segment *segs;
    Frame game_static;         // play-layer art drawn behind everything that moves, in timeline order
    SortedPlace *sorted;       // play-layer objects sorted by y with the characters
    int nsorted;
    int nlayers;
    Layer layer[MAX_LAYERS];
    float spawn_x, spawn_y;    // pixels, in the play layer's own space
    float edge_left, edge_right;   // camera limits from the edge_left/edge_right markers (0,0 = none)
    int hidden;                // editor markers removed from the play layer
} Level;

// Clamps a camera x so the screen stays between the level's edges.
float level_clamp_camera_x(const Level *lv, float cam_x);

// Like the game's f_BSPHitTest: how far along (x0,y0)->(x1,y1) the first
// segment is hit, 0..1, or 0 if nothing is hit. *hit gets that segment's index.
float level_hit_test(const Level *lv, float x0, float y0, float x1, float y1, int *hit);

// A character's position as the game's movement code keeps it: x, y are the
// feet on the ground plane (used for collision and depth); body_y lifts the
// drawn body, e.g. while climbing stairs. ground is n_groundtype (0 plain, 100 stairs).
typedef struct {
    float x, y, body_y;
    int ground;
} Mover;

// Moves by dx then dy like f_MoveCharH/V: walls stop the move just short of
// the hit (so characters slide along them). Stair edges (100/101) switch to
// stairs mode, where walking sideways climbs along the stair line below.
void level_move(const Level *lv, Mover *mv, float dx, float dy);

// The play layer's matrix (its twips -> screen) for a camera, as level_render uses.
Mat level_game_matrix(const Level *lv, Mat screen, float cam_x, float cam_y, float zoom);

Level *level_load(const char *path, char *err, int errlen);
void level_free(Level *lv);

// A character to draw inside the play layer, at play-layer pixel coordinates.
typedef struct {
    Movie *m;
    const Frame *frame;
    float x, y, scale;         // feet; scale < 0 faces left
    int tick;
    float body_y;              // the "body" child is drawn this far below the feet (negative = lifted),
                               // the shadow stays on the ground; depth still uses y
    int body_frame;            // > 0: show this frame of the body clip (like gotoAndStop), else play by tick
    float fade;                // 0 opaque .. 1 invisible
    int nparts;                // part clips pinned to a frame (see DrawList.part_count)
    uint16_t part_count[8], part_frame[8];
} Actor;

// screen: maps game stage pixels to the display. cam_x/cam_y: the point (play
// layer pixels) the camera follows. zoom: 100 = normal.
void level_render(const Level *lv, Mat screen, float cam_x, float cam_y, float zoom, int tick,
                  const Actor *actors, int nactors, DrawList *out);

#endif
