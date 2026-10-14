#include "player.h"

#include <string.h>

#define WALK_SPEED_X 5.0f      // px per game frame
#define WALK_SPEED_Y 3.0f
#define DASH_SPEED_X 9.0f
#define JUMP_LAUNCH -20.0f     // speed_launch
#define GRAVITY 2.0f
#define DOUBLE_TAP 8.0f        // game frames between taps for a dash

static const char *ANIM_LABELS[A_COUNT] = {
    "stand", "walk", "dash", "jump", "land", "blocking",
    "punch1_1", "punch1_2", "punch1_3", "punch1_4",
    "punch2_1", "punch2_2", "punch2_3",
    "punch5_1", "punch5_2", "punch4_4",
    "punch11_1", "punch11_2",
    "uppercut", "slamdown", "dashpunch", "dashslam",
    "hit1", "hit2", "hit3", "juggle1", "hitground1", "blocked", "bounce1", "bounce2",
};

const char *anim_name(int anim) { return ANIM_LABELS[anim]; }

// ---- rig --------------------------------------------------------------------------

// An animation is a root frame whose "body" clip plays it; its stop() frame
// ends it and a "fin" label marks where the next combo hit may start. Moves
// without one can chain a couple of frames before they end.
static Anim find_anim(const Movie *m, const char *label) {
    Anim a = {-1, 1, 1};
    int f = sprite_find_label(&m->root, label);
    if (f < 0) return a;
    a.frame = f;
    const Place *body = frame_find_name(m, &m->root.frames[f], "body");
    if (body && m->chars[body->cid].type == CHAR_SPRITE) {
        const Sprite *s = m->chars[body->cid].ptr;
        a.length = s->first_stop > 0 ? s->first_stop : s->nframes;
        int fin = sprite_find_label(s, "fin");
        a.fin = fin > 0 ? fin : (a.length > 3 ? a.length - 2 : a.length);
    }
    return a;
}

void rig_init(Rig *r, Movie *m) {
    memset(r, 0, sizeof *r);
    r->m = m;
    for (int i = 0; i < A_COUNT; i++) r->anims[i] = find_anim(m, ANIM_LABELS[i]);
}

Actor rig_actor(const Rig *r, int anim, float t, const Mover *mv, float face, int who, int weapon) {
    static const int CHARACTER_PART_FRAMES[] = {31, 32, 36, 37, 59};
    const Anim *a = &r->anims[anim];
    if (a->frame < 0) a = &r->anims[A_STAND];
    int f = a->frame >= 0 ? a->frame : 0;
    Actor act = {r->m, &r->m->root.frames[f], mv->x, mv->y, face, (int)t, mv->body_y, 0, 0, 0, {0}, {0}};
    if (who >= 0)
        for (int k = 0; k < 5; k++) {
            act.part_count[act.nparts] = CHARACTER_PART_FRAMES[k];
            act.part_frame[act.nparts++] = who;
        }
    if (weapon >= 0) {
        act.part_count[act.nparts] = RIG_WEAPON_FRAMES;
        act.part_frame[act.nparts++] = weapon;
    }
    return act;
}

// ---- player -----------------------------------------------------------------------

void player_init(Player *p, const Rig *rig, float x, float y) {
    memset(p, 0, sizeof *p);
    p->rig = rig;
    p->level = 50;   // no experience yet: unlock every non-magic combo
    p->mv = (Mover){x, y, 0, 0};
    p->face = 1;
    p->state = P_IDLE;
    p->anim = A_STAND;
    p->hp = p->hp_max = 100;
}

const char *player_move_name(const Player *p) { return ANIM_LABELS[p->anim]; }

// combo_unlocks[i] is level >= 1, 2, 4, 8, 16, 32, 50
static int unlocked(const Player *p, int i) {
    static const int need[] = {1, 2, 4, 8, 16, 32, 50};
    return p->level >= need[i];
}

static int airborne(const Player *p) { return p->mv.body_y < 0 || p->speed_jump < 0; }

static void play(Player *p, PlayerState s, int anim) {
    if (p->anim != anim || p->state != s) p->anim_t = 0;
    p->state = s;
    p->anim = anim;
}

static void attack(Player *p, int anim, int group, int num) {
    p->punch_group = group;
    p->punch_num = num;
    p->queued = 0;
    p->state = P_ATTACK;
    p->anim = anim;
    p->anim_t = 0;
    p->attack_id++;
}

// f_PunchSet1: the light attack (X)
static void light(Player *p) {
    int g = p->punch_group, n = p->punch_num;
    if (p->dashing && !airborne(p) && unlocked(p, 2)) {
        p->dashing = 0;
        attack(p, A_DASHPUNCH, 4, 1);
    } else if (airborne(p)) {
        if (g == 11) attack(p, n == 1 ? A_P11_2 : A_P11_1, 11, n == 1 ? 2 : 1);
        else attack(p, A_P11_1, 11, 1);
    } else if (g == 1 && n == 1) {
        attack(p, A_P1_2, 1, 2);
    } else if (g == 1 && n == 2 && unlocked(p, 0)) {
        attack(p, A_P1_3, 1, 3);
    } else if (g == 1 && n == 3) {
        attack(p, A_P1_4, 1, 4);
    } else {
        attack(p, A_P1_1, 1, 1);
    }
}

// f_PunchSet2: the heavy attack (Y). (X X X Y needs magic, which isn't in yet.)
static void heavy(Player *p) {
    int g = p->punch_group, n = p->punch_num;
    if (airborne(p)) {
        if (g == 11 && n == 1 && unlocked(p, 3)) attack(p, A_UPPERCUT, 3, 1);
        else if (g == 12 && n == 1) attack(p, A_P2_2, 12, 2);
        else if (g == 12 && n == 2) attack(p, A_P11_1, 13, 1);
        else if (g == 13) attack(p, n == 1 ? A_P11_2 : A_P11_1, 13, n == 1 ? 2 : 1);
        else { p->speed_jump = 0; attack(p, A_SLAMDOWN, 12, 1); }
        return;
    }
    if (p->dashing && unlocked(p, 2)) {
        p->dashing = 0;
        attack(p, A_DASHSLAM, 4, 2);
    } else if (g == 1 && n == 1 && unlocked(p, 3)) {
        attack(p, A_UPPERCUT, 3, 1);
    } else if (g == 1 && n == 2 && unlocked(p, 4)) {
        attack(p, A_P5_1, 5, 1);
    } else if (g == 5 && n == 1 && unlocked(p, 5)) {
        attack(p, A_P5_2, 5, 2);
    } else if (g == 4 && n == 2) {
        attack(p, A_P2_2, 2, 2);
    } else if (g == 2 && n == 1) {
        attack(p, A_P2_2, 2, 2);
    } else if (g == 2 && n == 2) {
        attack(p, A_P2_3, 2, 3);
    } else {
        attack(p, A_P2_1, 2, 1);
    }
}

int player_attack(const Player *p) {
    if (p->state != P_ATTACK) return HIT_NONE;
    const Anim *a = &p->rig->anims[p->anim];
    // the blow lands from just after the wind-up until the move can chain
    if (p->anim_t < 2 || p->anim_t > a->fin + 1) return HIT_NONE;
    switch (p->anim) {
    case A_UPPERCUT: return HIT_LAUNCH;
    case A_SLAMDOWN: case A_DASHSLAM: case A_P2_3: case A_P5_2: return HIT_SLAM;
    case A_P11_1: case A_P11_2: return HIT_AIR;
    case A_P2_1: case A_P2_2: case A_P5_1: case A_P1_4: case A_DASHPUNCH: return HIT_HEAVY;
    default: return HIT_LIGHT;
    }
}

int player_take_hit(Player *p, float from_x, int damage) {
    float dir = from_x < p->mv.x ? -1 : 1;   // where the hit came from
    if (p->state == P_BLOCK && p->face == dir) {
        play(p, P_BLOCK, A_BLOCK);
        p->vx = -dir * 3;
        return 0;
    }
    p->hp -= damage;
    if (p->hp < 0) p->hp = 0;
    p->vx = -dir * 6;
    p->dashing = 0;
    p->punch_group = p->punch_num = 0;
    p->queued = 0;
    p->state = P_HIT;
    p->anim = A_HIT1;
    p->anim_t = 0;
    return 1;
}

static void walk_or_stand(Player *p, const Level *lv, float dx, float dy) {
    if (dx || dy) {
        if (dx) p->face = dx < 0 ? -1 : 1;
        if (p->dashing) dx *= DASH_SPEED_X / WALK_SPEED_X;
        if (lv) level_move(lv, &p->mv, dx, dy);
        play(p, p->dashing ? P_DASH : P_WALK, p->dashing ? A_DASH : A_WALK);
    } else {
        p->dashing = 0;
        play(p, P_IDLE, A_STAND);
    }
}

void player_update(Player *p, const Level *lv, const PlayerInput *in, float dt) {
    const Anim *anims = p->rig->anims;
    p->anim_t += dt;
    float dx = in->dx * WALK_SPEED_X * dt, dy = in->dy * WALK_SPEED_Y * dt;

    // double-tap left or right to dash: a second press in the same direction
    // soon after releasing the first
    int dir = in->dx > 0.5f ? 1 : in->dx < -0.5f ? -1 : 0;
    if (p->tap_timer > 0) p->tap_timer -= dt;
    if (dir && dir != p->held_dir) {   // pressed from neutral (or reversed)
        if (p->tap_timer > 0 && dir == p->tap_dir) p->dashing = 1;
        else p->dashing = 0;
    }
    if (!dir && p->held_dir) {         // released: open the window for a second tap
        p->tap_dir = p->held_dir;
        p->tap_timer = DOUBLE_TAP;
    }
    p->held_dir = dir;

    // knockback slides to a stop
    if (p->vx) {
        if (lv) level_move(lv, &p->mv, p->vx * dt, 0);
        p->vx *= 0.8f;
        if (p->vx > -0.2f && p->vx < 0.2f) p->vx = 0;
    }

    // the body rises and falls while the feet stay on the ground plane
    if (airborne(p)) {
        if ((dx || dy) && p->state != P_HIT) {
            if (dx && p->state != P_ATTACK) p->face = dx < 0 ? -1 : 1;
            if (lv) level_move(lv, &p->mv, dx, dy);
        }
        p->mv.body_y += p->speed_jump * dt;
        p->speed_jump += GRAVITY * dt;
        if (p->mv.body_y >= 0) {
            p->mv.body_y = 0;
            p->speed_jump = 0;
            p->punch_group = p->punch_num = 0;
            play(p, P_LAND, A_LAND);
            return;
        }
    }

    switch (p->state) {
    case P_IDLE:
    case P_WALK:
    case P_DASH:
    case P_LAND:
        if (in->jump && anims[A_JUMP].frame >= 0) {
            p->speed_jump = JUMP_LAUNCH;
            p->punch_group = p->punch_num = 0;
            play(p, P_JUMP, A_JUMP);
            break;
        }
        if (in->light) { light(p); break; }
        if (in->heavy) { heavy(p); break; }
        if (in->block && anims[A_BLOCK].frame >= 0) { p->dashing = 0; play(p, P_BLOCK, A_BLOCK); break; }
        if (p->state == P_LAND && p->anim_t < anims[A_LAND].length && !dx && !dy) break;   // finish landing unless moving
        walk_or_stand(p, lv, dx, dy);
        break;

    case P_JUMP:
        if (in->light) light(p);
        else if (in->heavy) heavy(p);
        break;

    case P_ATTACK: {
        const Anim *a = &anims[p->anim];
        if (in->light) p->queued = 1;
        if (in->heavy) p->queued = 2;
        if (p->queued && p->anim_t >= a->fin) {
            if (p->queued == 1) light(p);
            else heavy(p);
            break;
        }
        if (p->anim_t >= a->length) {
            p->queued = 0;
            if (airborne(p)) { play(p, P_JUMP, A_JUMP); p->anim_t = anims[A_JUMP].length; }
            else { p->punch_group = p->punch_num = 0; play(p, P_IDLE, A_STAND); }
        }
        break;
    }

    case P_BLOCK:
        if (!in->block) play(p, P_IDLE, A_STAND);
        break;

    case P_HIT:
        if (p->anim_t >= anims[A_HIT1].length + 2) play(p, P_IDLE, A_STAND);
        break;
    }
}

Actor player_actor(const Player *p) {
    return rig_actor(p->rig, p->anim, p->anim_t, &p->mv, p->face, -1, -1);
}
