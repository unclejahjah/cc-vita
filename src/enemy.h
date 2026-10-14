// Basic foot soldiers (grey knights, barbarians, thieves) built from the player
// rig with different parts, as the game does. They walk up to the knight and
// swing at him; his swings stagger, knock back, launch or slam them.
#ifndef CC_ENEMY_H
#define CC_ENEMY_H

#include "player.h"

#define MAX_ENEMIES 8

typedef enum { E_IDLE, E_WALK, E_ATTACK, E_HIT, E_AIR, E_DOWN, E_DEAD } EnemyState;

typedef struct {
    int alive;
    Mover mv;
    float face;
    EnemyState state;
    int anim;
    float anim_t;
    float speed_toss_x;        // while tossed: pushed backwards (away from where it faces), px/frame
    float speed_toss_y;        // body speed, + is down
    float weight;              // current_weight: grows while airborne so juggles come down
    float toss_clock;
    int bounces;
    float vx;                  // knockback on the ground
    int hp;
    int who, weapon;           // rig parts: character and weapon frame
    float timer;               // think / recover / fade timing
    int last_hit;              // attack_id of the last player swing that landed
    int hit_player;            // this swing already hit the knight
    float fade;
} Enemy;

typedef struct {
    Enemy e[MAX_ENEMIES];
    int wave;
    float wave_timer;
    int kills;
} Enemies;

void enemies_reset(Enemies *es);
// AI, physics and hits both ways; spawns a new wave ahead of the knight when cleared.
void enemies_update(Enemies *es, const Level *lv, const Rig *rig, Player *p, float dt);
int enemies_actors(const Enemies *es, const Rig *rig, Actor *out, int max);

#endif
