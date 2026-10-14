#include "enemy.h"

#include <stdlib.h>
#include <string.h>

#define ENEMY_SPEED_X 3.0f     // px per game frame
#define ENEMY_SPEED_Y 2.0f
#define ENEMY_HP 40
// f_KidToss in MAIN.swf: global gravity 3, weight 0 growing by weightplus 1 every
// 10 frames aloft, bounce at int(speed * -0.35) while landing faster than 10,
// 7 damage for a hard first landing, 5 damage and half speed off a wall.
#define GRAVITY 3.0f
#define WEIGHT_PLUS 1.0f
#define REACH_X 75.0f          // a swing connects this far in front (px)
#define REACH_Y 22.0f          // and this far in depth
#define WAVE_SIZE 3

// barbarian and thief, with a weapon each (frames of the rig's part clips). Grey
// knights (character 5) fight on the player's side, so they aren't enemies.
static const int TYPES[][2] = {{6, 55}, {7, 33}};
#define NTYPES (int)(sizeof TYPES / sizeof TYPES[0])

void enemies_reset(Enemies *es) { memset(es, 0, sizeof *es); }

static float frand(void) { return rand() / (float)RAND_MAX; }

static void play(Enemy *e, EnemyState s, int anim) {
    if (e->anim != anim || e->state != s) e->anim_t = 0;
    e->state = s;
    e->anim = anim;
}

static void spawn_wave(Enemies *es, const Level *lv, const Player *p) {
    es->wave++;
    for (int i = 0, made = 0; i < MAX_ENEMIES && made < WAVE_SIZE; i++) {
        Enemy *e = &es->e[i];
        if (e->alive) continue;
        memset(e, 0, sizeof *e);
        // walk from the knight to the spawn point so walls keep it on the floor
        float side = (made == 2) ? -1 : 1;
        e->mv = p->mv;
        e->mv.body_y = 0;
        if (lv) level_move(lv, &e->mv, side * (380 + 120 * made), (made - 1) * 35.0f);
        else e->mv.x += side * (380 + 120 * made);
        e->face = e->mv.x > p->mv.x ? -1 : 1;
        e->alive = 1;
        e->hp = ENEMY_HP;
        const int *t = TYPES[(es->wave + made) % NTYPES];
        e->who = t[0];
        e->weapon = t[1];
        e->timer = 10 + 20 * frand();
        e->last_hit = -1;
        play(e, E_IDLE, A_STAND);
        made++;
    }
}

// The knight's current swing against one enemy.
static void take_player_hit(Enemy *e, const Player *p, int kind) {
    if (e->state == E_DEAD || e->state == E_DOWN || e->last_hit == p->attack_id) return;
    float dx = e->mv.x - p->mv.x, dy = e->mv.y - p->mv.y;
    int in_front = dx * p->face > -15 && dx * p->face < REACH_X;
    // airborne targets can be hit by swings at their height
    float height = (e->mv.body_y - p->mv.body_y);
    if (!in_front || dy > REACH_Y || dy < -REACH_Y || height < -120 || height > 120) return;
    e->last_hit = p->attack_id;
    int damage = kind == HIT_LIGHT ? 4 : kind == HIT_AIR ? 4 : 7;
    e->hp -= damage;
    e->face = -p->face;   // faces the knight, so the toss carries it away from him
    e->hit_player = 1;    // interrupts its own swing
    int airborne = e->state == E_AIR;
    float tx = 0, ty = 0;
    if (kind == HIT_LAUNCH) { tx = 2; ty = -32; }                 // uppercut: straight up
    else if (kind == HIT_SLAM) { tx = 9; ty = airborne ? 25 : -14; }   // spiked down in the air
    else if (airborne) { tx = kind == HIT_HEAVY ? 7 : 3; ty = kind == HIT_HEAVY ? -18 : -13; }   // juggle
    else if (kind == HIT_HEAVY || e->hp <= 0) { tx = 11; ty = -15; }   // knocked flying
    if (ty) {
        e->speed_toss_x = tx;
        e->speed_toss_y = ty;
        if (!airborne) { e->weight = 0; e->toss_clock = 0; e->bounces = 0; }
        if (e->mv.body_y >= 0) e->mv.body_y = -1;
        play(e, E_AIR, A_JUGGLE);
    } else {
        e->vx = p->face * (kind == HIT_HEAVY ? 7 : 3);
        static const int stagger[3] = {A_HIT1, A_HIT2, A_HIT3};
        play(e, E_HIT, stagger[p->attack_id % 3]);
    }
}

static void think(Enemy *e, const Level *lv, const Rig *rig, Player *p, float dt) {
    float dx = p->mv.x - e->mv.x, dy = p->mv.y - e->mv.y;
    // stand off to the side the enemy is on, level with the knight
    float want_x = p->mv.x + (dx > 0 ? -60 : 60);
    float gx = want_x - e->mv.x;
    switch (e->state) {
    case E_IDLE:
    case E_WALK:
        e->timer -= dt;
        if ((gx > 8 || gx < -8 || dy > 6 || dy < -6) && p->hp > 0) {
            float mx = gx > 0 ? ENEMY_SPEED_X : -ENEMY_SPEED_X;
            if (gx < ENEMY_SPEED_X && gx > -ENEMY_SPEED_X) mx = gx;
            float my = dy > 0 ? ENEMY_SPEED_Y : -ENEMY_SPEED_Y;
            if (dy < ENEMY_SPEED_Y && dy > -ENEMY_SPEED_Y) my = dy;
            if (lv) level_move(lv, &e->mv, mx * dt, my * dt);
            else { e->mv.x += mx * dt; e->mv.y += my * dt; }
            e->face = dx < 0 ? -1 : 1;
            play(e, E_WALK, A_WALK);
        } else {
            e->face = dx < 0 ? -1 : 1;
            play(e, E_IDLE, A_STAND);
            if (e->timer <= 0 && p->hp > 0) {
                e->hit_player = 0;
                play(e, E_ATTACK, A_P1_1);
            }
        }
        break;
    case E_ATTACK: {
        const Anim *a = &rig->anims[A_P1_1];
        // the blow lands at the swing's fin frame
        if (!e->hit_player && e->anim_t >= a->fin - 1) {
            e->hit_player = 1;
            float ex = p->mv.x - e->mv.x, ey = p->mv.y - e->mv.y;
            if (ex * e->face > -10 && ex * e->face < REACH_X && ey < REACH_Y && ey > -REACH_Y && p->mv.body_y > -60)
                player_take_hit(p, e->mv.x, 8);
        }
        if (e->anim_t >= a->length) {
            e->timer = 25 + 30 * frand();
            play(e, E_IDLE, A_STAND);
        }
        break;
    }
    case E_HIT:
        if (e->anim_t >= rig->anims[e->anim].length + 3) {
            e->timer = 8 + 10 * frand();
            play(e, E_IDLE, A_STAND);
        }
        break;
    case E_AIR: {
        // carried backwards; a wall flips it, hurts, and halves the speed
        float want = -e->face * e->speed_toss_x * dt, x0 = e->mv.x;
        if (lv) level_move(lv, &e->mv, want, 0);
        else e->mv.x += want;
        if (want && (e->mv.x - x0) * want < want * want * 0.5f) {
            e->face = -e->face;
            e->hp -= 5;
            e->speed_toss_x *= 0.5f;
        }
        e->mv.body_y += (e->speed_toss_y + e->weight) * dt;
        e->toss_clock += dt;
        if (e->toss_clock >= 10) { e->toss_clock -= 10; e->weight += WEIGHT_PLUS; }
        e->speed_toss_y += GRAVITY * dt;
        if (e->mv.body_y > 0) {
            e->mv.body_y = 0;
            e->bounces++;
            if (e->bounces == 1 && (e->speed_toss_y > 40 || e->speed_toss_x > 15)) e->hp -= 7;
            if (e->speed_toss_y > 10) {
                // bounce: still in the air, and still hittable (that's the juggle)
                e->speed_toss_y = (int)(e->speed_toss_y * -0.35f);
                e->mv.body_y = -1;
                e->anim = e->bounces % 2 ? A_BOUNCE1 : A_BOUNCE2;
                e->anim_t = 0;
            } else {
                e->speed_toss_x = e->speed_toss_y = 0;
                e->weight = e->toss_clock = 0;
                e->bounces = 0;
                e->timer = e->hp > 0 ? 25 : 40;
                play(e, E_DOWN, A_HITGROUND);
            }
        }
        break;
    }
    case E_DOWN:
        e->timer -= dt;
        if (e->timer <= 0) {
            if (e->hp > 0) { e->timer = 15; play(e, E_IDLE, A_STAND); }
            else { e->timer = 30; play(e, E_DEAD, A_HITGROUND); }
        }
        break;
    case E_DEAD:
        e->timer -= dt;
        e->fade = 1 - e->timer / 30;
        if (e->timer <= 0) e->alive = 0;
        break;
    }
}

void enemies_update(Enemies *es, const Level *lv, const Rig *rig, Player *p, float dt) {
    int kind = player_attack(p), alive = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &es->e[i];
        if (!e->alive) continue;
        int was_dead = e->hp <= 0;
        e->anim_t += dt;
        if (kind != HIT_NONE) take_player_hit(e, p, kind);
        if (e->vx) {
            if (lv) level_move(lv, &e->mv, e->vx * dt, 0);
            else e->mv.x += e->vx * dt;
            e->vx *= e->state == E_AIR ? 0.97f : 0.8f;
            if (e->vx > -0.2f && e->vx < 0.2f) e->vx = 0;
        }
        think(e, lv, rig, p, dt);
        if (!was_dead && e->hp <= 0) es->kills++;
        if (e->alive) alive++;
    }
    if (!alive) {
        es->wave_timer -= dt;
        if (es->wave_timer <= 0) {
            spawn_wave(es, lv, p);
            es->wave_timer = 45;   // pause before the next wave once this one is cleared
        }
    }
}

int enemies_actors(const Enemies *es, const Rig *rig, Actor *out, int max) {
    int n = 0;
    for (int i = 0; i < MAX_ENEMIES && n < max; i++) {
        const Enemy *e = &es->e[i];
        if (!e->alive) continue;
        out[n] = rig_actor(rig, e->anim, e->anim_t, &e->mv, e->face, e->who, e->weapon);
        out[n].fade = e->fade;
        n++;
    }
    return n;
}
