// Drop-in for cc-vita/src/cc_buttons.c: replaces make_trigger() so Ltrigger/Rtrigger
// become Vita-style L/R shoulder buttons (a wide slab, big rounded outer-top corner),
// drawn in the game's button look: black outline, grey body, light top, dark shade,
// dark letter. Uses Box, Poly, poly_begin_loop, poly_pt, letter, fill_from,
// append_fill and fill_box from cc_buttons.c. Call: make_trigger(l, 'L') as now.

// one closed loop: a box with its own radius per corner (tl, tr, br, bl)
static void round_box(Poly *p, float x0, float y0, float x1, float y1, const float r[4]) {
    const float cx[4] = {x0 + r[0], x1 - r[1], x1 - r[2], x0 + r[3]};
    const float cy[4] = {y0 + r[0], y0 + r[1], y1 - r[2], y1 - r[3]};
    const float a0[4] = {3.1415926f, 4.7123890f, 0.0f, 1.5707963f};
    poly_begin_loop(p);
    for (int k = 0; k < 4; k++)
        for (int i = 0; i <= 8; i++) {
            float a = a0[k] + 1.5707963f * i / 8;
            poly_pt(p, cx[k] + r[k] * cosf(a), cy[k] + r[k] * sinf(a));
        }
}

static void make_trigger(Shape *s, char c) {
    // keep the original's footprint so prompts line up as before
    Box b = {1e30f, 1e30f, -1e30f, -1e30f};
    for (int i = 0; i < s->nfills; i++) {
        Box fb = fill_box(&s->fills[i]);
        if (fb.x0 < b.x0) b.x0 = fb.x0;
        if (fb.y0 < b.y0) b.y0 = fb.y0;
        if (fb.x1 > b.x1) b.x1 = fb.x1;
        if (fb.y1 > b.y1) b.y1 = fb.y1;
    }
    if (b.x0 > b.x1) return;
    float h = b.y1 - b.y0, w = fminf(b.x1 - b.x0, h * 1.9f);
    float h2 = h * 0.72f;                       // a shoulder button is flatter than it is wide
    float x0 = (b.x0 + b.x1) / 2 - w / 2, x1 = x0 + w;
    float y0 = (b.y0 + b.y1) / 2 - h2 / 2, y1 = y0 + h2;
    int left = c == 'L';
    // big curve on the outer top corner (the Vita's rounded shoulder), small elsewhere
    float big = h2 * 0.85f, small = h2 * 0.22f;
    float r[4] = {left ? big : small, left ? small : big, small, small};
    float o = h * 0.07f;                        // outline width

    s->nfills = 0;                              // drop the trigger art
    s->nlines = 0;
    static const uint8_t BLACK[4] = {0, 0, 0, 255}, SHADE[4] = {0x5a, 0x5a, 0x62, 255},
                         BASE[4] = {0x9a, 0x9a, 0xa4, 255}, LIGHT[4] = {0xd8, 0xd8, 0xe0, 255},
                         INK[4] = {0x26, 0x26, 0x2c, 255};
    Poly p = {0};
    round_box(&p, x0, y0, x1, y1, r);
    append_fill(s, fill_from(&p, BLACK));
    float ri[4];
    for (int k = 0; k < 4; k++) ri[k] = fmaxf(r[k] - o, 0);
    Poly q = {0};
    round_box(&q, x0 + o, y0 + o, x1 - o, y1 - o, ri);
    append_fill(s, fill_from(&q, SHADE));
    Poly bp = {0};
    round_box(&bp, x0 + o, y0 + o, x1 - o, y1 - o * 2.6f, ri);
    append_fill(s, fill_from(&bp, BASE));
    float hr[4];
    for (int k = 0; k < 4; k++) hr[k] = fminf(ri[k] * 0.8f, (h2 * 0.42f - o * 1.6f) * 0.5f);
    Poly lp = {0};                              // glossy top, like the ball buttons
    round_box(&lp, x0 + o * 2.2f, y0 + o * 1.6f, x1 - o * 2.2f, y0 + h2 * 0.42f, hr);
    append_fill(s, fill_from(&lp, LIGHT));
    float lh = h2 * 0.5f, cxm = (x0 + x1) / 2 + (left ? w * 0.06f : -w * 0.06f);
    Poly tp = {0};
    letter(&tp, c, (Box){cxm - lh * 0.31f, y0 + (h2 - lh) / 2 - o * 0.3f, cxm + lh * 0.31f, y0 + (h2 + lh) / 2 - o * 0.3f});
    append_fill(s, fill_from(&tp, INK));
}
