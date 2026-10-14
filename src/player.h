// The knight: moving, dashing, attacking, jumping and blocking with PLAYER.swf's
// animations. The combo rules are the game's own (f_PunchSet1/f_PunchSet2 in
// MAIN.swf): each press picks the next move from the current punch_group and
// punch_num. The game runs at 30 frames a second; jumps launch at -20 px/frame
// with gravity 2.
#ifndef CC_PLAYER_H
#define CC_PLAYER_H

#include "level.h"

typedef enum { P_IDLE, P_WALK, P_DASH, P_ATTACK, P_JUMP, P_LAND, P_BLOCK, P_HIT } PlayerState;

typedef struct {
    int frame;                 // root frame of the animation, -1 if missing
    int length;                // frames until it ends (its stop frame)
    int fin;                   // first frame the next combo hit may start
} Anim;

// The game's moves, by animation label.
enum {
    A_STAND, A_WALK, A_DASH, A_JUMP, A_LAND, A_BLOCK,
    A_P1_1, A_P1_2, A_P1_3, A_P1_4,          // X X X X
    A_P2_1, A_P2_2, A_P2_3,                  // Y Y Y
    A_P5_1, A_P5_2,                          // X X Y, X X Y Y
    A_P4_4,                                  // finisher after X X X Y (level 50)
    A_P11_1, A_P11_2,                        // air slashes
    A_UPPERCUT, A_SLAMDOWN, A_DASHPUNCH, A_DASHSLAM,
    A_HIT1, A_HIT2, A_HIT3, A_JUGGLE, A_HITGROUND, A_BLOCKED, A_BOUNCE1, A_BOUNCE2,
    A_COUNT
};

// The character rig's swappable parts are clips stopped on frame 0, one frame per
// character: torsos (31 frames, a copy per pose), arms (32) and three head views
// (36, 37, 59). Frame k is character k (0 green knight ... 5 grey knight,
// 6 barbarian, 7 thief). Weapons (112 frames) are numbered separately.
#define RIG_WEAPON_FRAMES 112

typedef struct {
    Movie *m;
    Anim anims[A_COUNT];
} Rig;

void rig_init(Rig *r, Movie *m);
const char *anim_name(int anim);
// An actor for a rig animation, dressed as character `who` with weapon `weapon` (-1: as drawn).
Actor rig_actor(const Rig *r, int anim, float t, const Mover *mv, float face, int who, int weapon);

// What a move does to whoever it hits.
enum { HIT_NONE, HIT_LIGHT, HIT_HEAVY, HIT_LAUNCH, HIT_SLAM, HIT_AIR };

typedef struct {
    float dx, dy;              // -1..1 stick/D-pad
    int light, heavy, jump;    // pressed this frame (X, Y, A in the game)
    int block;                 // held
} PlayerInput;

typedef struct {
    const Rig *rig;
    int level;                 // character level: combo_unlocks are levels 1, 2, 4, 8, 16, 32, 50
    Mover mv;
    float face;                // 1 right, -1 left
    PlayerState state;
    int anim;                  // A_*
    float anim_t;              // game frames into the current animation
    float speed_jump;          // body speed while airborne, px per game frame
    int punch_group, punch_num;
    int queued;                // 1 light / 2 heavy pressed before the current move's fin frame
    int dashing;
    float tap_timer;           // double-tap dash: time left to tap again
    int tap_dir;               // direction of the last released tap
    int held_dir;              // direction held last frame
    int attack_id;             // changes with every new swing, so each swing hits a target once
    int hp, hp_max;
    float vx;                  // knockback
} Player;

void player_init(Player *p, const Rig *rig, float x, float y);
// dt is in game frames (0.5 per 60 Hz display frame).
void player_update(Player *p, const Level *lv, const PlayerInput *in, float dt);
Actor player_actor(const Player *p);
const char *player_move_name(const Player *p);
// The current swing's effect while it can still connect, else HIT_NONE.
int player_attack(const Player *p);
// An enemy swing reaching the player; returns 1 if it hurt (0 if blocked).
int player_take_hit(Player *p, float from_x, int damage);

#endif
