/* SPDX-License-Identifier: GPL-3.0-only */
/* ChoralRoot FM-1: the layout tool's preview module (docs/LAYOUT-TOOL.md, step 2: a viewer).
 *
 * The WHOLE firmware as the browser emulator builds it (tools/emu/emu_fw.c: the firmware through emu_firmware.h, the
 * HAL of emu_hal_fw.h, the emu_fw_* hooks), compiled to a standalone WebAssembly module (no Emscripten JS runtime),
 * with -DCR_LAYOUT_PREVIEW. The preview is therefore the unit's own editor code (cr_edit.c ce_view / ce_screen) drawn
 * by the unit's own renderer (cr_draw.c on gfx.c), on the real engines and their factory presets. No sound is taken
 * out (the audio ISR still runs in simulated time: it is what applies an engine change and a sound load).
 *
 * The instance: power-on with no flash file (emu_fw_options no_flash: a fresh 0xFF flash in RAM, so default settings
 * and no user sounds), 1000 ms of the device clock, then a snapshot of the module's whole static data (.data + .bss:
 * every firmware variable, the RAM flash, the LCD). lay_reset() copies the snapshot back; lay_show() and lay_catalog()
 * start with it, so what one call edits (a knob, a send, a settings write to the RAM flash) never reaches a later
 * lay_show: the same calls give the same pixels. Time is simulated and moves only inside lay_show / lay_knob /
 * lay_select / lay_step, by fixed amounts.
 *
 * How the editor is driven:
 *   lay_show      by the editor's OWN functions (cr_ui.c cu_pool_load + cu_sends_to_fx: the sound; cu_edit_open: EDIT
 *                 tapped; cr_edit.c ce_group: a group button tapped, once to open the group, once more per screen;
 *                 ce_select: SELECT turned to the lane). Not by injected inputs: reaching an arbitrary engine and pool
 *                 position by gestures needs OPT + PRESETS, the picker's commit on release and a popup to wait out,
 *                 and would tie the API to today's gestures. The functions are the ones those gestures end in.
 *   lay_knob, lay_select   by INJECTED detents (emu_hal.enc, as the emulator's web_enc), taken by the 1 ms tick and
 *                 dispatched by cr_ui.c's own knob code, so the step sizes, the hot cell and the redraw are the unit's.
 *
 *   sh tools/layout/build_layout.sh  ->  build/layout-web/crlayout.wasm (+ crlayout.js) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the layout lint's hook (gfx.c GFX_HOOK_TEXT, as tests/cr_draw_test.c): the ink box of every text, canvas rows */
static void lay_hk_text(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char *s, uint32_t flags);
#define GFX_HOOK_TEXT(x0, y0, x1, y1, s, flags) lay_hk_text(x0, y0, x1, y1, s, flags)
#define GFX_HOOK_BLIT(x, y, r0) ((void)0)
#define GFX_HOOK_BEGIN() ((void)0)
#define GFX_HOOK_PIXELS(n) ((void)0)

#include "../emu/emu_fw.c"

#undef __attribute__                               /* (hostsim.c defines it away for the firmware) */
#define LAY_EXPORT __attribute__((used, visibility("default")))
#define LAY_API 2                                  /* the exported API's version (lay_version "api") */
#define LAY_FORMAT "choralroot-layout"             /* the layout file format's name, version 1 (lay_catalog) */

/* ------------------------------------------------------------------ JSON out --- */
static char jb[1u << 20];                          /* one answer at a time: the next call overwrites it */
static uint32_t jlen, jdepth;
static uint8_t jneed[16], jkeyed;
static void j_ch(char c) { if (jlen + 1u < sizeof jb) jb[jlen++] = c; }
static void j_raw(const char *s) { while (*s) j_ch(*s++); }
static void j_begin(void) { jlen = 0; jdepth = 0; jneed[0] = 0; jkeyed = 0; }
static const char *j_end(void) { jb[jlen] = 0; return jb; }
static void j_pre(void)
{
    if (jkeyed)
        jkeyed = 0;
    else if (jneed[jdepth])
        j_ch(',');
    jneed[jdepth] = 1;
}
static void j_open(char c) { j_pre(); j_ch(c); if (jdepth < 15u) jdepth++; jneed[jdepth] = 0; }
static void j_close(char c) { if (jdepth) jdepth--; j_ch(c); }
static void j_text(const char *s)                  /* the firmware's strings are Latin-1 ("\267": the middle dot) */
{
    static const char H[] = "0123456789abcdef";
    j_ch('"');
    for (; s && *s; s++) {
        uint8_t c = (uint8_t)*s;
        if (c == '"' || c == '\\') {
            j_ch('\\');
            j_ch((char)c);
        } else if (c < 0x20u || c >= 0x7Fu) {
            j_raw("\\u00");
            j_ch(H[c >> 4]);
            j_ch(H[c & 15u]);
        } else {
            j_ch((char)c);
        }
    }
    j_ch('"');
}
static void j_key(const char *k) { j_pre(); j_text(k); j_ch(':'); jkeyed = 1; }
static void j_str(const char *s) { j_pre(); j_text(s); }
static void j_int(int32_t v)
{
    char b[16];
    snprintf(b, sizeof b, "%d", (int)v);
    j_pre();
    j_raw(b);
}
static void j_null(void) { j_pre(); j_raw("null"); }
static void j_bool(int v) { j_pre(); j_raw(v ? "true" : "false"); }
static void j_ks(const char *k, const char *s) { j_key(k); if (s) j_str(s); else j_null(); }
static void j_ki(const char *k, int32_t v) { j_key(k); j_int(v); }
static void j_kb(const char *k, int v) { j_key(k); j_bool(v); }

/* ------------------------------------------------------------ the device clock --- */
static uint32_t lay_ms, lay_last_frame;
static uint64_t lay_frames;
static uint8_t lay_booted;
static void lay_run_ms(void)                       /* one millisecond of emu.c's headless loop (as emu_web.c run_ms) */
{
    uint32_t ms = lay_ms++;
    emu_fw_tick(ms);
    if (ms == 0 || ms - lay_last_frame >= 15u) {
        lay_last_frame = ms;
        emu_fw_frame();
    } else {
        emu_fw_idle();
    }
    while (lay_frames + EMU_BLOCK <= (uint64_t)ms * EMU_FS / 1000u) {
        int16_t blk[EMU_BLOCK * 2];
        emu_fw_audio(blk, EMU_BLOCK);              /* (not taken out: the ISR applies engine changes and loads) */
        lay_frames += EMU_BLOCK;
    }
}
static void lay_run(uint32_t n)
{
    while (n--)
        lay_run_ms();
}

/* ------------------------------------------------------------- the snapshot --- */
extern unsigned char __global_base, __data_end;    /* wasm-ld: the static data, .data and .bss (the stack and the heap
                                                    * are outside it) */
static uint8_t *lay_snap;
static uint32_t lay_snap_n;
static const eng_deep_t *lay_deep_hook(const track_t *t, const eng_deep_t *builtin);
static void lay_alloc(void);
static void lay_boot(void)
{
    if (lay_booted)
        return;
    emu_fw_options(0, 1, 0, 1);                    /* no flash file: a fresh flash in RAM, nothing read or written */
    emu_hal.ready = 2;                             /* simulated time */
    cp_deep_hook = lay_deep_hook;                  /* (cr_pages.c, CR_LAYOUT_PREVIEW: a design's pages, lay_load) */
    lay_alloc();                                   /* (the loaded tables: outside the snapshot's static data) */
    emu_fw_init(0);
    lay_booted = 1;
    lay_run(1000);                                 /* the power-on screen has gone, the two parts' sounds are loaded */
    lay_snap_n = (uint32_t)(&__data_end - &__global_base);
    lay_snap = (uint8_t *)malloc(lay_snap_n);
    if (lay_snap)
        memcpy(lay_snap, &__global_base, lay_snap_n);   /* (lay_snap and the allocator's state are in it) */
}
/* the instance as it was after power-on: every call below that says "from the reset instance" starts here */
LAY_EXPORT void lay_reset(void)
{
    lay_boot();
    if (lay_snap)
        memcpy(&__global_base, lay_snap, lay_snap_n);
}
LAY_EXPORT uint32_t lay_snapshot_bytes(void) { lay_boot(); return lay_snap ? lay_snap_n : 0u; }

/* ------------------------------------------------------------ the built-in tables --- */
static const char *const LAY_FMT[] = {"int", "pct", "bipct", "time", "lfohz", "cutoff", "db", "semi", "enum", "bpm",
                                      "note", "onoff", "oct", "steps", "ofs", "fmnote", "fmfrq", "timei", "fix2", "inti"};
static const char *const LAY_STYPE[] = {"platform", "stack", "columns", "mixer", "edit", "page"};   /* CE_S_* */
static const char *const LAY_BAND[] = {"none", "env", "filter", "dx", "cz", "algo", "ade2"};         /* CR_W_* */
static const char *const LAY_STYLE[] = {"rule", "big", "ratio", "harm", "detune", "num", "val", "text", "knob", "bar",
                                        "waveph"};                                                    /* ENG_C_* */
static const char *const LAY_GBTN[CE_NSEC] = {"FX", "SEL", "ENV", "LFO", "SEQ", "PLAY", "REC"};   /* cr_edit.c CE_SEC_BTN:
                                                    * the buttons as the emulator names them (the panel prints the groups) */
static const char *const LAY_PBAND[] = {"none", "env", "filter", "window", "algo", "ade2"};          /* ENG_B_* */

/* a descriptor's value names: an F_ENUM's names[min .. max], another format's 0-terminated list (params.c) */
static uint32_t lay_nnames(const param_desc_t *d)
{
    uint32_t n = 0;
    if (!d->names)
        return 0;
    if (d->fmt == F_ENUM)
        return d->max >= d->min && d->min >= 0 ? (uint32_t)(d->max - d->min + 1) : 0u;
    while (d->names[n] && n < 512u)
        n++;
    return n;
}
static const char *lay_name(const param_desc_t *d, uint32_t i)
{
    return d->fmt == F_ENUM ? d->names[(uint32_t)d->min + i] : d->names[i];
}
static void j_desc(const param_desc_t *d)          /* the fields of a param_desc_t, into the open object */
{
    uint32_t i, n = lay_nnames(d);
    j_ks("short", d->label);
    j_ks("fmt", d->fmt < NELEM(LAY_FMT) ? LAY_FMT[d->fmt] : "?");
    j_ki("min", d->min);
    j_ki("max", d->max);
    j_ki("def", d->def);
    j_key("names");
    if (n) {
        j_open('[');
        for (i = 0; i < n; i++)
            j_str(lay_name(d, i) ? lay_name(d, i) : "");
        j_close(']');
    } else {
        j_null();
    }
    j_ks("unit", d->unit);
}

/* the params hash: FNV-1a 64 over everything a layout file binds to: the engine's name and index, its eight EDIT
 * descriptors and page titles, its deep pages (titles, the four descriptors with their names and units), the
 * sections, the screen plan. A layout made for one hash is for exactly these parameters */
static uint64_t lay_h;
static void h_bytes(const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    while (n--) {
        lay_h ^= *b++;
        lay_h *= 1099511628211ull;
    }
}
static void h_str(const char *s)
{
    uint8_t z = 0, one = 1;
    if (s)
        h_bytes(s, (uint32_t)strlen(s));
    h_bytes(s ? &z : &one, 1);
}
static void h_int(int32_t v) { h_bytes(&v, 4); }
static void h_desc(const param_desc_t *d)
{
    uint32_t i, n;
    h_str(d->label);
    if (!d->label)
        return;
    h_int(d->fmt);
    h_int(d->min);
    h_int(d->max);
    h_int(d->def);
    n = lay_nnames(d);
    h_int((int32_t)n);
    for (i = 0; i < n; i++)
        h_str(lay_name(d, i));
    h_str(d->unit);
}
static uint64_t lay_params_hash(uint32_t e)
{
    const engine_t *en = ENGINES[e];
    const eng_deep_t *d = en->deep;
    uint32_t i, c, r;
    lay_h = 14695981039346656037ull;
    h_int((int32_t)e);
    h_str(en->name);
    h_str(en->page_title[0]);
    h_str(en->page_title[1]);
    for (i = 0; i < 8u; i++)
        h_desc(&en->edit[i]);
    h_int(d ? (int32_t)d->npages : -1);
    if (d) {
        for (i = 0; i < d->npages; i++) {
            h_str(d->pages[i].title);
            for (c = 0; c < 4u; c++)
                h_desc(&d->pages[i].col[c]);
        }
        h_bytes(d->section, 8);
        h_int((int32_t)(d->screens ? d->nscreens : 0u));
        for (i = 0; d->screens && i < d->nscreens; i++) {
            const eng_screen_t *h = &d->screens[i];
            h_int(h->sec);
            h_int(h->pg[0]);
            h_int(h->pg[1]);
            h_int(h->band);
            h_int(h->letter);
            h_str(h->title);
            for (r = 0; r < 2u; r++)
                for (c = 0; c < 4u; c++) {
                    h_int(h->cell[r][c]);
                    h_str(h->label[r][c]);
                }
        }
    }
    return lay_h;
}
static void j_hash(uint32_t e)
{
    char b[20];
    uint64_t h = lay_params_hash(e);
    snprintf(b, sizeof b, "%08x%08x", (unsigned)(h >> 32), (unsigned)(h & 0xFFFFFFFFu));
    j_ks("params_hash", b);
}
static int lay_engine_ok(uint32_t e) { return e < NENGINES && eng_ok(e) && eng_rank(e) < NENG_SHOWN && eng_vis(eng_rank(e)) == e; }

/* {"firmware": "ChoralRoot 0.22", "api": 1, "format": .., "format_version": 1, "engines": [{index, name, params_hash}]}
 * the engines in the order the unit shows them (engines.c ENGINE_ORDER) */
LAY_EXPORT const char *lay_version(void)
{
    uint32_t r;
    j_begin();
    j_open('{');
    j_ks("firmware", FELUCCA_VERSION);
    j_ki("api", LAY_API);
    j_ks("format", LAY_FORMAT);
    j_ki("format_version", 2);
    j_key("engines");
    j_open('[');
    for (r = 0; r < (uint32_t)NENG_SHOWN; r++) {
        uint32_t e = eng_vis(r);
        j_open('{');
        j_ki("index", (int32_t)e);
        j_ks("name", ENGINES[e]->name);
        j_hash(e);
        j_ki("deep_pages", ENGINES[e]->deep ? (int32_t)ENGINES[e]->deep->npages : 0);
        j_close('}');
    }
    j_close(']');
    j_close('}');
    return j_end();
}

/* the sounds of the engine's pool as PRESETS shows them (cr_bank.c: INIT, the factory presets; a fresh flash has no
 * user sounds): {"engine": e, "presets": ["INIT", ..]}; lay_show's preset is an index into it */
LAY_EXPORT const char *lay_presets(uint32_t e)
{
    uint32_t i, n;
    lay_boot();
    j_begin();
    j_open('{');
    if (!lay_engine_ok(e)) {
        j_ks("error", "no such engine");
        j_close('}');
        return j_end();
    }
    j_ki("engine", (int32_t)e);
    j_key("presets");
    j_open('[');
    n = pool_count(e);
    for (i = 0; i < n; i++) {
        char b[16];
        pool_name(e, i, b);
        j_str(b);
    }
    j_close(']');
    j_close('}');
    return j_end();
}

/* one cell of the catalog: the knob, what it is bound to, the descriptor from the engine's own table */
static void j_cell(const track_t *t, const eng_deep_t *d, const ce_view_t *vw, const ce_scr_t *sc, uint32_t ln,
                   uint32_t c)
{
    ce_ref_t r = vw->ref[ln < CR_ED_ROWS ? ln : 0u][c];
    const eng_screen_t *h = ce_plan(d, vw->hs);
    const param_desc_t *pd = 0;
    char lab[24];
    /* a column the view hides for this sound (an unused MOD slot shows its source only): the page's own column */
    if ((r.k == CE_R_NONE || r.k == CE_R_DASH) && d && (sc->type == CE_S_STACK || sc->type == CE_S_EDIT) &&
        ln < sc->n && sc->pg[ln] < d->npages && d->pages[sc->pg[ln]].col[c].label)
        r = ce_r(CE_R_DEEP, sc->pg[ln], c);
    j_open('{');
    j_ki("knob", (int32_t)c + 1);
    if (r.k == CE_R_DEEP && d && r.a < d->npages) {
        pd = &d->pages[r.a].col[r.b & 3u];
        j_ks("bind", "deep");
        j_ki("page", r.a);
        j_ki("col", r.b & 3u);
        j_ks("page_title", d->pages[r.a].title);
    } else if (r.k == CE_R_TRK && r.a < P_COUNT) {
        pd = cp_desc(t, r.a);
        j_ks("bind", "track");
        j_ki("param", r.a);
    } else if (r.k == CE_R_TXT) {
        j_ks("bind", "text");
        j_ks("text", CE_TXT[r.a % NELEM(CE_TXT)]);
    } else {
        j_ks("bind", r.k == CE_R_DASH ? "dash" : "none");
    }
    if (pd && pd->label) {
        if (h && ln < 2u && h->label[ln][c])
            snprintf(lab, sizeof lab, "%s", h->label[ln][c]);
        else
            ce_label(t, r, pd, lab, sizeof lab);
        j_ks("label", lab);
        j_desc(pd);
        if (h && ln < 2u)
            j_ks("style", h->cell[ln][c] < NELEM(LAY_STYLE) ? LAY_STYLE[h->cell[ln][c]] : "?");
        if (r.k == CE_R_DEEP && d->desc)
            j_kb("by_mode", 1);        /* the engine may show another label / names by the sound's mode */
    }
    j_close('}');
}

/* today's built-in layout of engine e, read from the firmware's tables through the editor's own derivation (cr_edit.c
 * ce_scr / ce_lanes / ce_view on the engine's INIT sound), as a layout file of format 1. From the reset instance */
LAY_EXPORT const char *lay_catalog(uint32_t e)
{
    const eng_deep_t *d;
    track_t *t;
    uint32_t g, k, ln, c, i, ncell = 0;
    lay_reset();
    j_begin();
    j_open('{');
    if (!lay_engine_ok(e)) {
        j_ks("error", "no such engine");
        j_close('}');
        return j_end();
    }
    cu_pool_load(0, e, 0);                         /* the pool's INIT: the tables are the engine's, whatever the sound */
    cu_sends_to_fx();
    lay_run(60);
    cu_edit_open(0);
    t = ce_trk();
    d = ce_deep(t);
    j_ks("format", LAY_FORMAT);
    j_ki("version", 1);
    j_ks("generated", "by lay_catalog from the firmware's built-in tables: do not edit (tools/layout/dump_catalogs.mjs writes it; read-only in step 2, docs/LAYOUT-TOOL.md). The firmware version is not in it: params_hash binds it to the parameters");
    j_key("engine");
    j_open('{');
    j_ki("index", (int32_t)e);
    j_ks("name", ENGINES[e]->name);
    j_close('}');
    j_hash(e);
    /* the parameter pages the cells bind to (core.h eng_page_t), as they are */
    j_key("pages");
    j_open('[');
    for (i = 0; cp_deep(t) && i < cp_deep(t)->npages; i++) {
        const eng_page_t *pg = &cp_deep(t)->pages[i];
        j_open('{');
        j_ki("page", (int32_t)i);
        j_ks("title", pg->title);
        j_key("cols");
        j_open('[');
        for (c = 0; c < 4u; c++) {
            if (pg->col[c].label) {
                j_open('{');
                j_desc(&pg->col[c]);
                j_close('}');
                ncell++;
            } else {
                j_null();
            }
        }
        j_close(']');
        j_close('}');
    }
    j_close(']');
    j_ki("page_cells", (int32_t)ncell);
    j_key("header");                               /* the owner's rule 1: one shared header on every page. Today the
                                                    * editor has none (cr_edit.c ce_screen: s->header = 0) */
    j_null();
    j_key("groups");
    j_open('[');
    for (g = 0; g < CE_NSEC; g++) {
        ce_scr_t sc;
        uint32_t ns;
        if (!ce_has(t, g))
            continue;
        ns = ce_scr(t, g, 0, &sc);
        j_open('{');
        j_ki("group", (int32_t)g);
        j_ks("name", CE_SEC_NAME[g]);
        j_ks("button", LAY_GBTN[g]);
        j_key("screens");
        j_open('[');
        for (k = 0; k < ns; k++) {
            ce_view_t vw;
            uint32_t nl;
            ce_scr(t, g, k, &sc);
            nl = ce_lanes(t, g, &sc);
            cx.grp[0] = (uint8_t)g;
            cx.scr[0][g] = (uint8_t)k;
            cx.lane[0][g] = 0;
            ce_view(t, 0, &vw);
            j_open('{');
            j_ki("screen", (int32_t)k);
            j_ks("kind", vw.kind == CR_K_STACK ? "stack" : "edit8");
            j_ks("type", sc.type < NELEM(LAY_STYPE) ? LAY_STYPE[sc.type] : "?");
            j_ks("band", vw.wide < NELEM(LAY_BAND) ? LAY_BAND[vw.wide] : "?");
            j_kb("tall", vw.tall);
            j_kb("header", 0);
            if (ce_plan(d, vw.hs)) {
                const eng_screen_t *h = ce_plan(d, vw.hs);
                j_ki("plan", vw.hs - 1);
                j_ks("plan_band", h->band < NELEM(LAY_PBAND) ? LAY_PBAND[h->band] : "?");
            }
            if (vw.kind == CR_K_STACK) {
                j_key("headings");
                j_open('[');
                for (c = 0; c < 4u; c++)
                    j_str(vw.head[c]);
                j_close(']');
            }
            j_key("pages");                        /* the deep pages behind the screen, in lane order */
            j_open('[');
            for (i = 0; sc.type != CE_S_PLAT && i < sc.n; i++)
                j_int(sc.pg[i]);
            j_close(']');
            j_key("lanes");
            j_open('[');
            for (ln = 0; ln < nl; ln++) {
                cx.lane[0][g] = (uint8_t)ln;
                ce_view(t, 0, &vw);
                j_open('{');
                j_ki("lane", (int32_t)ln);
                j_ks("title", vw.right);
                j_key("cells");
                j_open('[');
                for (c = 0; c < 4u; c++)
                    j_cell(t, d, &vw, &sc, vw.tall ? 0u : ln, c);
                j_close(']');
                j_close('}');
            }
            j_close(']');
            j_close('}');
        }
        j_close(']');
        j_close('}');
    }
    j_close(']');
    j_close('}');
    return j_end();                                /* (the walk's state stays until the next lay_show / lay_reset; the
                                                    * answer is in the static data, so no reset here) */
}

/* ------------------------------------------------------------------- the editor --- */
static uint32_t lay_eng;
/* the real editor on pool position `preset` of engine e, at group g (0 OSC 1 FILT 2 ENV 3 LFO 4 MOD 5 FX 6 MIX), its
 * screen and lane. From the reset instance. Returns the screen's number of lanes, or < 0: -1 no such engine, -2 no
 * such preset, -3 the engine has no such group, -4 no such screen, -5 no such lane */
LAY_EXPORT int32_t lay_show(uint32_t e, uint32_t preset, uint32_t g, uint32_t screen, uint32_t lane)
{
    track_t *t;
    ce_scr_t sc;
    uint32_t k, nl;
    lay_reset();
    if (!lay_engine_ok(e))
        return -1;
    if (preset >= pool_count(e))
        return -2;
    cu_pool_load(0, e, preset);                    /* as PRESETS / the engine picker: the sound into the chord part */
    cu_sends_to_fx();
    lay_run(60);                                   /* the audio side takes the engine and the patch */
    cu_edit_open(0);                               /* EDIT tapped */
    t = ce_trk();
    if (g >= CE_NSEC || !ce_has(t, g))
        return -3;
    if (screen >= ce_scr(t, g, 0, &sc))
        return -4;
    ce_scr(t, g, screen, &sc);
    nl = ce_lanes(t, g, &sc);
    if (lane >= nl)
        return -5;
    if (g != cx.grp[0])
        ce_group(g);                               /* the group's button tapped: the group */
    for (k = 0; k < screen; k++)
        ce_group(g);                               /* .. tapped again: its next screen */
    if (lane)
        ce_select((int32_t)lane);                  /* SELECT: the lane */
    lay_eng = e;
    lay_run(45);                                   /* three frames: drawn */
    return (int32_t)nl;
}

/* KNOB k (1..4) turned n detents (+ clockwise), as the unit's encoder: the tick takes the steps, cr_ui.c's knob code
 * edits the active lane's cell, the next frames redraw (the hot cell marked for 800 ms of device time) */
LAY_EXPORT void lay_knob(uint32_t k, int32_t n)
{
    lay_boot();
    if (k < 1u || k > 4u)
        return;
    emu_hal.enc[emu_hal.enc_id[EMU_E_K1 + k - 1u]] += n * emu_hal.enc_dir[EMU_E_K1 + k - 1u];
    lay_run(30);
}
/* SELECT turned n detents: the lane (running on across the group's screens, as on the unit) */
LAY_EXPORT void lay_select(int32_t n)
{
    lay_boot();
    emu_hal.enc[emu_hal.enc_id[EMU_E_SELECT]] += n * emu_hal.enc_dir[EMU_E_SELECT];
    lay_run(30);
}
/* the device clock n ms on, with no input (n <= 5000): a hot cell's mark ends after 800 ms */
LAY_EXPORT void lay_step(uint32_t n)
{
    lay_boot();
    lay_run(n > 5000u ? 5000u : n);
}

/* ------------------------------------------------------------ the picture, the lint --- */
typedef struct { int16_t x0, y0, x1, y1; uint8_t flags; char s[40]; } lay_box_t;
static lay_box_t lay_boxes[256];
static uint32_t lay_nbox;
static uint8_t lay_lint_on;
static void lay_hk_text(int32_t x0, int32_t y0, int32_t x1, int32_t y1, const char *s, uint32_t flags)
{
    uint32_t i;
    lay_box_t b;
    if (!lay_lint_on)
        return;
    memset(&b, 0, sizeof b);
    b.x0 = (int16_t)x0;
    b.x1 = (int16_t)x1;
    b.y0 = (int16_t)(y0 - cv_oy);                  /* canvas rows -> screen rows */
    b.y1 = (int16_t)(y1 - cv_oy);
    b.flags = (uint8_t)(flags & ~2u);              /* (cut by a strip: every strip draws its part) */
    snprintf(b.s, sizeof b.s, "%s", s);
    for (i = 0; i < lay_nbox; i++)
        if (!memcmp(&lay_boxes[i], &b, 8) && !strcmp(lay_boxes[i].s, b.s))
            return;                                /* the same text from another strip */
    if (lay_nbox < 256u)
        lay_boxes[lay_nbox++] = b;
}

static uint8_t lay_rgba[240 * 240 * 4];
/* the screen as it is now, drawn again from scratch (every strip: the lint sees every text): 240 x 240 RGBA, the
 * firmware's RGB565 (big-endian in emu_hal.lcd) widened as tests/cr_draw_test.c's PPMs. The clock does not move: two
 * calls give the same bytes */
static void lay_to_rgba(void)
{
    uint32_t i;
    for (i = 0; i < 240u * 240u; i++) {
        uint32_t c = emu_hal.lcd[i];
        c = ((c >> 8) | (c << 8)) & 0xFFFFu;
        lay_rgba[4u * i] = (uint8_t)((c >> 11) * 255u / 31u);
        lay_rgba[4u * i + 1u] = (uint8_t)(((c >> 5) & 63u) * 255u / 63u);
        lay_rgba[4u * i + 2u] = (uint8_t)((c & 31u) * 255u / 31u);
        lay_rgba[4u * i + 3u] = 255;
    }
}
LAY_EXPORT uint8_t *lay_render(void)
{
    lay_boot();
    lay_nbox = 0;
    lay_lint_on = 1;
    cr_draw_invalidate();
    cr_ui_draw();
    lay_lint_on = 0;
    lay_to_rgba();
    return lay_rgba;
}
/* the LCD as the firmware wrote it: 240 x 240 RGB565, big-endian (the emulator's web_fb) */
LAY_EXPORT uint16_t *lay_fb(void) { lay_boot(); return emu_hal.lcd; }

static void j_box(const lay_box_t *b)
{
    j_ks("text", b->s);
    j_key("box");
    j_open('[');
    j_int(b->x0);
    j_int(b->y0);
    j_int(b->x1);
    j_int(b->y1);
    j_close(']');
}
/* the text-box lint of the last lay_render, tests/cr_draw_test.c's rules for a screen without ring or header:
 *   "off-screen"  a box outside 0..240 (not a text in a scrolled view: clipped on purpose)
 *   "overlap"     two boxes intersect ("other": the second)
 *   "ellipsis"    a text that did not fit and was cut with "..." (a note, not an error)
 * {"findings": [{kind, text, box: [x0, y0, x1, y1], other?}], "texts": [{text, box, flags}]} */
LAY_EXPORT const char *lay_lint(void)
{
    uint32_t i, j;
    j_begin();
    j_open('{');
    j_key("findings");
    j_open('[');
    for (i = 0; i < lay_nbox; i++) {
        const lay_box_t *b = &lay_boxes[i];
        if ((b->x0 < 0 || b->y0 < 0 || b->x1 > 240 || b->y1 > 240) && !(b->flags & 32u)) {
            j_open('{');
            j_ks("kind", "off-screen");
            j_box(b);
            j_close('}');
        }
        if (b->flags & 1u) {
            j_open('{');
            j_ks("kind", "ellipsis");
            j_box(b);
            j_close('}');
        }
        if (cu_scr.message[0] && b->x0 < 120 + 114 && b->x1 > 120 - 114 && b->y0 < 140 && b->y1 > 100)
            continue;                              /* (a message's card lies over what is under it) */
        for (j = i + 1u; j < lay_nbox; j++) {
            const lay_box_t *o = &lay_boxes[j];
            if ((b->flags | o->flags) & 32u)
                continue;
            if (b->x0 < o->x1 && o->x0 < b->x1 && b->y0 < o->y1 && o->y0 < b->y1) {
                j_open('{');
                j_ks("kind", "overlap");
                j_box(b);
                j_key("other");
                j_open('{');
                j_box(o);
                j_close('}');
                j_close('}');
            }
        }
    }
    j_close(']');
    j_key("texts");
    j_open('[');
    for (i = 0; i < lay_nbox; i++) {
        j_open('{');
        j_box(&lay_boxes[i]);
        j_ki("flags", lay_boxes[i].flags);
        j_close('}');
    }
    j_close(']');
    j_close('}');
    return j_end();
}

/* where the editor is and what its active lane shows now (the screen model the last frame drew, cr_ui.c cu_scr):
 * {"editor", "engine", "sound", "group", "screen", "lane", "lanes", "title", "page", "kind", "band", "header",
 *  "hot": knob or 0, "cells": [{label, value, pct, on} x 4]} */
LAY_EXPORT const char *lay_state(void)
{
    const cr_screen_t *s = &cu_scr;
    uint32_t g, c, r;
    lay_boot();
    g = cx.grp[0] % CE_NSEC;
    r = s->active < CR_ED_ROWS ? s->active : 0u;
    j_begin();
    j_open('{');
    j_kb("editor", cu.page == PG_EDIT);
    j_ki("engine", (int32_t)(trk[CR_PART_CHORD].eng_req % NENGINES));
    j_ks("sound", psnd[0].name);
    j_kb("edited", psnd[0].edited);
    j_ki("group", (int32_t)g);
    j_ki("screen", cx.scr[0][g]);
    j_ki("lane", cx.lane[0][g]);
    j_ki("lanes", s->n_rows);
    j_ks("title", s->title);
    j_ks("page", s->page);
    j_ks("kind", s->kind == CR_K_STACK ? "stack" : s->kind == CR_K_EDIT8 ? "edit8" : "other");
    j_ks("band", s->wide < NELEM(LAY_BAND) ? LAY_BAND[s->wide] : "?");
    j_kb("header", s->header);
    j_ki("hot", s->hot_r ? s->hot_c + 1 : 0);
    j_key("cells");
    j_open('[');
    for (c = 0; c < 4u; c++) {
        const cr_cell_t *cl = &s->cell[r][c];
        j_open('{');
        j_ks("label", s->kind == CR_K_STACK ? s->head[c] : cl->label);
        j_ks("value", cl->value);
        j_ki("pct", cl->pct);
        j_kb("on", cl->flags & CR_CF_ON);
        j_close('}');
    }
    j_close(']');
    j_close('}');
    return j_end();
}

/* ---------------------------------------- lay_load: a design's classic pages (step 3) --- */
/* cr_pages.c cp_deep() asks lay_deep_hook (CR_LAYOUT_PREVIEW only) for the engine's pages: the loaded table when there
 * is one, else the built-in. A loaded table is the design's pages: each column a copy of a built-in (page, col)'s
 * descriptor, get / set / desc / mod_dst going through to that built-in (page, col) (la_t.src), so the engine sees its
 * own patch addresses while the editor (ce_scr, ce_view, ce_screen, cr_draw) draws the design as the unit would. A
 * design's own labels are answered through cr_edit.c's ce_label_hook (CR_LAYOUT_PREVIEW only).
 *
 * The packed form (web/fm1layout.js packDesign), bytes:
 *   'L' 2 engine npages; per page: tlen title[tlen], 4 x (srcpage srccol llen label[llen]) (srcpage 0xFF: empty);
 *   section[8]; nscreens; per screen: sec pg0 pg1 band(ENG_B_*) tlen title[tlen]      (nscreens 0: no screen plan)
 *   engine 0xFF: the loaded table off
 * The tables live in one block allocated before the snapshot (lay_boot), outside the static data, so lay_reset (and
 * every lay_show with it) keeps them */
#define LA_PG 64u
#define LA_SC 32u
typedef struct {
    uint32_t eng;                                  /* the engine loaded, 0xFF none */
    eng_deep_t deep;
    eng_page_t page[LA_PG];
    uint8_t src[LA_PG][4][2];
    char title[LA_PG][16], label[LA_PG][4][16];
    eng_screen_t scr[LA_SC];
    char stitle[LA_SC][16];
} la_t;
static la_t *la;
static const eng_deep_t *la_base(void) { return la && la->eng < NENGINES ? ENGINES[la->eng]->deep : 0; }
static int la_map(uint32_t p, uint32_t c, uint32_t *bp, uint32_t *bc)
{
    if (!la || la->eng >= NENGINES || p >= la->deep.npages || c > 3u || la->src[p][c][0] == 0xFFu)
        return 0;
    *bp = la->src[p][c][0];
    *bc = la->src[p][c][1];
    return 1;
}
static int32_t la_get(const track_t *t, uint32_t p, uint32_t c)
{
    uint32_t bp, bc;
    return la_map(p, c, &bp, &bc) ? la_base()->get(t, bp, bc) : 0;
}
static void la_set(track_t *t, uint32_t p, uint32_t c, int32_t v)
{
    uint32_t bp, bc;
    if (la_map(p, c, &bp, &bc))
        la_base()->set(t, bp, bc, v);
}
static const param_desc_t *la_desc(const track_t *t, uint32_t p, uint32_t c)
{
    uint32_t bp, bc;
    if (la_map(p, c, &bp, &bc) && la_base()->desc)
        return la_base()->desc(t, bp, bc);
    return &la->page[p < LA_PG ? p : 0].col[c & 3u];
}
static int32_t la_mod_dst(const track_t *t, uint32_t p, uint32_t c)
{
    uint32_t bp, bc;
    if (p == ENG_MOD_TRK)
        return la_base()->mod_dst(t, p, c);
    return la_map(p, c, &bp, &bc) ? la_base()->mod_dst(t, bp, bc) : -1;
}
/* cr_edit.c ce_label_hook: a design's own label for a cell */
static int la_label(const track_t *t, uint32_t p, uint32_t c, char *out, uint32_t n)
{
    if (!la || la->eng != eng_idx(t->eng_req) || p >= la->deep.npages || !la->label[p][c & 3u][0])
        return 0;
    snprintf(out, n, "%s", la->label[p][c & 3u]);
    return 1;
}
static const eng_deep_t *lay_deep_hook(const track_t *t, const eng_deep_t *builtin)
{
    return la && la->eng < NENGINES && la->eng == eng_idx(t->eng_req) ? &la->deep : builtin;
}
static void lay_alloc(void)                        /* (lay_boot, before the snapshot) */
{
    la = (la_t *)malloc(sizeof *la);
    if (la) {
        memset(la, 0, sizeof *la);
        la->eng = 0xFFu;
    }
    ce_label_hook = la_label;
}

static uint8_t lay_in[1u << 16];
/* where the page writes a packed layout before lay_load (up to lay_buf_size bytes), and the strings of lay_draw_* */
LAY_EXPORT uint8_t *lay_buf(void) { return lay_in; }
LAY_EXPORT uint32_t lay_buf_size(void) { return sizeof lay_in; }

static const char *la_err(const char *e)
{
    if (la)
        la->eng = 0xFFu;
    j_begin();
    j_open('{');
    j_kb("ok", 0);
    j_ks("error", e);
    j_close('}');
    return j_end();
}
/* a packed design's classic pages for its engine (above). Takes effect at the next lay_show (which starts from the
 * reset instance; the tables stay). {"ok": true, "engine", "pages", "screens"} or {"ok": false, "error"} (and nothing
 * loaded) */
LAY_EXPORT const char *lay_load(const uint8_t *ptr, uint32_t len)
{
    const uint8_t *q = ptr, *end = ptr + len;
    const eng_deep_t *b;
    uint8_t sec[8];
    uint32_t e, n, i, c, k, ns;
#define LA_U8() (q < end ? *q++ : (uint8_t)0xFFu)
#define LA_STR(dst, cap) do { uint32_t l_ = LA_U8(), m_; if (q + l_ > end) return la_err("short"); \
        for (m_ = 0; m_ < l_ && m_ + 1u < (cap); m_++) (dst)[m_] = (char)q[m_]; \
        (dst)[m_] = 0; q += l_; } while (0)
    lay_boot();
    if (!la)
        return la_err("no memory");
    if (len < 3u || q[0] != 'L' || q[1] != 2u)
        return la_err("not a packed design (L 2)");
    q += 2;
    e = LA_U8();
    la->eng = 0xFFu;
    if (e == 0xFFu) {
        j_begin(); j_open('{'); j_kb("ok", 1); j_ki("engine", -1); j_close('}');
        return j_end();
    }
    if (!lay_engine_ok(e) || !(b = ENGINES[e]->deep) || !b->pages)
        return la_err("the engine has no pages of its own");
    n = LA_U8();
    if (!n || n > LA_PG)
        return la_err("pages: 1 .. 64");
    memset(la->page, 0, sizeof la->page);
    memset(la->label, 0, sizeof la->label);
    for (i = 0; i < n; i++) {
        LA_STR(la->title[i], 16u);
        la->page[i].title = la->title[i];
        for (c = 0; c < 4u; c++) {
            uint32_t sp = LA_U8(), sc = LA_U8();
            la->src[i][c][0] = (uint8_t)sp;
            la->src[i][c][1] = (uint8_t)sc;
            LA_STR(la->label[i][c], 16u);
            if (sp != 0xFFu) {
                if (sp >= b->npages || sc > 3u || !b->pages[sp].col[sc].label)
                    return la_err("a column names no built-in parameter");
                la->page[i].col[c] = b->pages[sp].col[sc];
            }
        }
    }
    for (i = 0; i < 8u; i++)
        sec[i] = LA_U8();
    ns = LA_U8();
    if (ns > LA_SC)
        return la_err("screens: up to 32");
    memset(la->scr, 0, sizeof la->scr);
    for (k = 0; k < ns; k++) {
        eng_screen_t *h = &la->scr[k];
        h->sec = LA_U8();
        h->pg[0] = LA_U8();
        h->pg[1] = LA_U8();
        h->band = LA_U8();
        LA_STR(la->stitle[k], 16u);
        h->title = la->stitle[k][0] ? la->stitle[k] : 0;
        if (h->pg[0] >= n || (h->pg[1] != 0xFFu && h->pg[1] >= n) || h->sec > 4u || h->band > ENG_B_ADE2)
            return la_err("a screen names no page");
    }
    if (q > end)
        return la_err("short");
    la->deep = *b;                                 /* blob, .. as the engine's; then the design's own */
    memcpy(la->deep.section, sec, sizeof sec);
    la->deep.npages = n;
    la->deep.pages = la->page;
    la->deep.get = la_get;
    la->deep.set = la_set;
    la->deep.desc = la_desc;
    la->deep.mod_dst = b->mod_dst ? la_mod_dst : 0;
    la->deep.screens = ns ? la->scr : 0;
    la->deep.nscreens = ns;
    la->eng = e;
    j_begin();
    j_open('{');
    j_kb("ok", 1);
    j_ki("engine", (int32_t)e);
    j_ki("pages", (int32_t)n);
    j_ki("screens", (int32_t)ns);
    j_close('}');
    return j_end();
#undef LA_U8
#undef LA_STR
}

/* ------------------------------------- the parameters by their built-in address (free pages) --- */
/* a parameter of the editor's sound by the built-in table's (page, col), or page 0xFF: a part parameter (col = its P_
 * id). The design's free pages read and turn them here: the engine's own get / set, the editor's step (cp_dstep) */
static const param_desc_t *lp_desc(const track_t *t, uint32_t p, uint32_t c)
{
    const eng_deep_t *b = ENGINES[eng_idx(t->eng_req)]->deep;
    if (p == 0xFFu)
        return c < P_COUNT ? cp_desc(t, c) : 0;
    if (!b || !b->pages || p >= b->npages || c > 3u || !b->pages[p].col[c].label)
        return 0;
    return b->desc ? b->desc(t, p, c) : &b->pages[p].col[c];
}
static int32_t lp_get(const track_t *t, uint32_t p, uint32_t c)
{
    return p == 0xFFu ? t->p[c] : ENGINES[eng_idx(t->eng_req)]->deep->get(t, p, c);
}
/* the value now (the editor's sound) */
LAY_EXPORT int32_t lay_pget(uint32_t p, uint32_t c)
{
    track_t *t;
    lay_boot();
    t = ce_trk();
    return lp_desc(t, p, c) ? lp_get(t, p, c) : 0;
}
/* the value's text as the editor makes it (cp_value: "2.1k", "SAW", "+50%"): a C string */
LAY_EXPORT const char *lay_ptext(uint32_t p, uint32_t c)
{
    static char b[24];
    track_t *t;
    const param_desc_t *d;
    lay_boot();
    t = ce_trk();
    d = lp_desc(t, p, c);
    b[0] = 0;
    if (d)
        cp_value(d, lp_get(t, p, c), b, sizeof b);
    return b;
}
/* n detents on it, as the editor's knob steps (cp_dstep), 30 ms pass; returns the new value */
LAY_EXPORT int32_t lay_pstep(uint32_t p, uint32_t c, int32_t n)
{
    track_t *t;
    const param_desc_t *d;
    int32_t v;
    lay_boot();
    t = ce_trk();
    d = lp_desc(t, p, c);
    if (!d)
        return 0;
    v = cp_dstep(d, lp_get(t, p, c), n, 0);
    if (p == 0xFFu)
        t->p[c] = (int16_t)v;
    else
        ENGINES[eng_idx(t->eng_req)]->deep->set(t, p, c, v);
    lay_run(30);
    return lp_get(t, p, c);
}

/* ------------------------------------------------ the free pages' drawing (step 4, preview only) --- */
/* A design's free page is drawn by the firmware's own primitives and faces (cr_draw.c / cr_gfx.c / gfx.c) in the
 * renderer's six strips, under the editor's own title line (cr_ed_title: the sound's name left, the page's right):
 * lay_draw_begin(title, page), the calls below (recorded), lay_draw_end() draws them and answers the 240 x 240 RGBA
 * (as lay_render); lay_lint() then answers for it. Coordinates are screen pixels; colours cr_screen.h CR_COL_* (the
 * named ones and the palette's tokens) */
enum { LD_TEXT, LD_RECT, LD_GLYPH, LD_HBAR, LD_BAND, LD_P5KNOB, LD_DISC, LD_WAVE };
typedef struct {
    uint8_t op, col, a, b;                         /* text: bold, align; rect: radius, outline width (0: filled) */
    int16_t x, y, w, h;                            /* text: w = maxw */
    uint8_t px, g, pct, pct2;
    uint16_t flags;
    uint8_t wv[20];
    char s[32];
} ld_t;
static ld_t ld[200];
static uint32_t nld;
static char ld_title[24], ld_page[16];
static ld_t *ld_new(uint32_t op)
{
    ld_t *o;
    if (nld >= NELEM(ld))
        return 0;
    o = &ld[nld++];
    memset(o, 0, sizeof *o);
    o->op = (uint8_t)op;
    return o;
}
/* a new page: the title line's left text (the sound's name) and right text (the page's) */
LAY_EXPORT void lay_draw_begin(const char *title, const char *page)
{
    lay_boot();
    nld = 0;
    snprintf(ld_title, sizeof ld_title, "%s", title ? title : "");
    snprintf(ld_page, sizeof ld_page, "%s", page ? page : "");
}
/* text s (a C string, Latin-1) on baseline y at x: px size, bold 0/1, align 0 left 1 centre 2 right; maxw > 0: cut to
 * that width with an ellipsis, as the editor's cells */
LAY_EXPORT void lay_draw_text(uint32_t bold, uint32_t px, int32_t x, int32_t y, const char *s, uint32_t col,
                              uint32_t align, int32_t maxw)
{
    ld_t *o = ld_new(LD_TEXT);
    if (!o)
        return;
    o->a = (uint8_t)(bold ? 1 : 0);
    o->b = (uint8_t)(align > 2u ? 0u : align);
    o->px = (uint8_t)px;
    o->x = (int16_t)x;
    o->y = (int16_t)y;
    o->w = (int16_t)maxw;
    o->col = (uint8_t)col;
    snprintf(o->s, sizeof o->s, "%s", s ? s : "");
}
/* a rectangle: line 0 = filled (r > 0: rounded), else an outline that many px wide (r > 0: its corners quarter arcs) */
LAY_EXPORT void lay_draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t col, uint32_t r, uint32_t line)
{
    ld_t *o = ld_new(LD_RECT);
    if (!o)
        return;
    o->x = (int16_t)x; o->y = (int16_t)y; o->w = (int16_t)w; o->h = (int16_t)h;
    o->col = (uint8_t)col; o->a = (uint8_t)r; o->b = (uint8_t)line;
}
/* a cell glyph in the box (cr_cglyph: CR_G_KNOB the dial and its value arc, CR_G_BAR, CR_G_SAW, CR_G_SQUARE,
 * CR_G_WAVE, CR_G_NOISE ..; the pictograms through cr_picto), pct / pct2 Q8 of 255, flags CR_CF_* (CR_CF_BIP) */
LAY_EXPORT void lay_draw_glyph(uint32_t g, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t col, uint32_t pct,
                               uint32_t pct2, uint32_t flags)
{
    ld_t *o = ld_new(LD_GLYPH);
    if (!o)
        return;
    o->g = (uint8_t)g;
    o->x = (int16_t)x; o->y = (int16_t)y; o->w = (int16_t)w; o->h = (int16_t)h;
    o->col = (uint8_t)col;
    o->pct = (uint8_t)(pct > 255u ? 255u : pct);
    o->pct2 = (uint8_t)(pct2 > 255u ? 255u : pct2);
    o->flags = (uint16_t)flags;
}
/* the knob dial (CR_G_KNOB) centred at (cx, cy), size px high */
LAY_EXPORT void lay_draw_knob(int32_t cx, int32_t cy, int32_t size, uint32_t col, uint32_t pct, uint32_t bip)
{
    lay_draw_glyph(CR_G_KNOB, cx - size / 2, cy - size / 2, size, size, col, pct, 0, bip ? CR_CF_BIP : 0u);
}
/* a Prophet-5 knob (the "p5" style, docs/LAYOUT-TOOL.md): centred at (cx, cy) in a size px box: the printed scale
 * (11 ticks, 0..10, over the dial's 270 degrees at the box's edge, the end ticks 2 px), the face (a disc of radius
 * size / 2 - 6 in `face` with a 1 px rim in `rim`), the pointer (2 px, from `from` % of the face's radius, 0: 30 %,
 * to 95 %) at the value's angle, as CR_G_KNOB's (pct Q8 of 255); bip: tick 5 (12 o'clock) in the pointer's colour,
 * 2 px, the centre mark */
LAY_EXPORT void lay_draw_p5knob(int32_t cx, int32_t cy, int32_t size, uint32_t face, uint32_t rim, uint32_t tick,
                                uint32_t ptr, uint32_t pct, uint32_t bip, uint32_t from)
{
    ld_t *o = ld_new(LD_P5KNOB);
    if (!o)
        return;
    o->x = (int16_t)cx; o->y = (int16_t)cy; o->w = (int16_t)size;
    o->col = (uint8_t)face; o->a = (uint8_t)rim; o->b = (uint8_t)tick; o->g = (uint8_t)ptr;
    o->pct = (uint8_t)(pct > 255u ? 255u : pct);
    o->flags = (uint16_t)(bip ? 1u : 0u);
    o->px = (uint8_t)(from && from < 95u ? from : 30u);
}
/* a filled disc (cr_disc), centre and radius Q4 (1/16 px: a centre between two pixels, a radius of 2.5 px) */
LAY_EXPORT void lay_draw_disc(int32_t cx16, int32_t cy16, int32_t r16, uint32_t col)
{
    ld_t *o = ld_new(LD_DISC);
    if (!o)
        return;
    o->x = (int16_t)cx16; o->y = (int16_t)cy16; o->w = (int16_t)r16;
    o->col = (uint8_t)col;
}
/* ONE cycle of a waveform as a thin line in the box, no baseline (the p5 style's led icons, as the panel prints them
 * over its SHAPE buttons): kind 0 saw (a ramp to 70 %, then straight down), 1 triangle, 2 pulse, 3 sine (one hump),
 * 4 noise */
LAY_EXPORT void lay_draw_wave(uint32_t kind, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t col)
{
    ld_t *o = ld_new(LD_WAVE);
    if (!o)
        return;
    o->g = (uint8_t)kind;
    o->x = (int16_t)x; o->y = (int16_t)y; o->w = (int16_t)w; o->h = (int16_t)h;
    o->col = (uint8_t)col;
}
/* the editor's small level bar (cr_hbar) */
LAY_EXPORT void lay_draw_hbar(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t col, uint32_t pct, uint32_t bip)
{
    ld_t *o = ld_new(LD_HBAR);
    if (!o)
        return;
    o->x = (int16_t)x; o->y = (int16_t)y; o->w = (int16_t)w; o->h = (int16_t)h;
    o->col = (uint8_t)col;
    o->pct = (uint8_t)(pct > 255u ? 255u : pct);
    o->flags = (uint16_t)(CR_CF_PCT | (bip ? CR_CF_BIP : 0u));
}
/* one of the editor's wide bands (cr_wide: CR_W_ENV, CR_W_FILTER ..) at its own place (rows 24 .. 120), its values
 * the 20 bytes at wv (cr_screen_t.wv) */
LAY_EXPORT void lay_draw_band(uint32_t kind, const uint8_t *wv)
{
    ld_t *o = ld_new(LD_BAND);
    if (!o)
        return;
    o->g = (uint8_t)kind;
    if (wv)
        memcpy(o->wv, wv, sizeof o->wv);
}
/* a rounded outline lw px wide: the sides between the corners, the corners quarter arcs (cr_arc, anti-aliased) */
static void ld_rline(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, int32_t lw, uint16_t c)
{
    int32_t rc;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    rc = r * 16 - lw * 8;                          /* the line's middle, Q4 from the corner's centre */
    cr_fill(x + r, y, w - 2 * r, lw, c);
    cr_fill(x + r, y + h - lw, w - 2 * r, lw, c);
    cr_fill(x, y + r, lw, h - 2 * r, c);
    cr_fill(x + w - lw, y + r, lw, h - 2 * r, c);
    if (rc <= 0)
        return;
    cr_arc((x + r) * 16, (y + r) * 16, rc, lw * 16, 32768u, 16384u, 0, 0, 0, c);
    cr_arc((x + w - r) * 16, (y + r) * 16, rc, lw * 16, 49152u, 16384u, 0, 0, 0, c);
    cr_arc((x + w - r) * 16, (y + h - r) * 16, rc, lw * 16, 0u, 16384u, 0, 0, 0, c);
    cr_arc((x + r) * 16, (y + h - r) * 16, rc, lw * 16, 16384u, 16384u, 0, 0, 0, c);
}
/* the Prophet-5 knob (lay_draw_p5knob), Q4 inside: the angles are CR_G_KNOB's (a0 7:30, 270 degrees clockwise) */
static void ld_p5knob(const ld_t *o)
{
    uint32_t a0 = 24576u, sweep = 49152u, a = a0 + sweep * (uint32_t)o->pct / 256u, k;
    int32_t cx = o->x * 16, cy = o->y * 16, ro = o->w * 8 - 8, ri = ro - 40, rf = (o->w / 2 - 6) * 16, r0, r1;
    uint16_t ptr = cr_rgb(o->g, T_TEXT);
    int16_t p[4];
    for (k = 0; k <= 10u; k++) {                   /* the scale: 0 .. 10 */
        uint32_t ak = a0 + sweep * k / 10u;
        int bipk = (o->flags & 1u) && k == 5u;
        p[0] = (int16_t)(cx + ((ri * cr_cos(ak)) >> 14)); p[1] = (int16_t)(cy + ((ri * cr_sin(ak)) >> 14));
        p[2] = (int16_t)(cx + ((ro * cr_cos(ak)) >> 14)); p[3] = (int16_t)(cy + ((ro * cr_sin(ak)) >> 14));
        cr_poly(p, 2u, k == 0u || k == 10u || bipk ? 32 : 16, 0, 0, bipk ? ptr : cr_rgb(o->b, T_LINE));
    }
    if (rf < 32)
        return;
    cr_disc(cx, cy, rf, cr_rgb(o->a, T_MID));      /* the face and its rim */
    cr_disc(cx, cy, rf - 16, cr_rgb(o->col, T_SURF));
    r0 = rf * o->px / 100;                         /* the pointer */
    r1 = rf * 95 / 100;
    p[0] = (int16_t)(cx + ((r0 * cr_cos(a)) >> 14)); p[1] = (int16_t)(cy + ((r0 * cr_sin(a)) >> 14));
    p[2] = (int16_t)(cx + ((r1 * cr_cos(a)) >> 14)); p[3] = (int16_t)(cy + ((r1 * cr_sin(a)) >> 14));
    cr_poly(p, 2u, 32, 0, 0, ptr);
}
/* lay_draw_wave: the cycle's points (Q4) inside the box less half the line (1.5 px: 24 Q4, as CR_G_KNOB's pointer is
 * 38 for 2.4 px), drawn by cr_poly */
static void ld_wave(const ld_t *o, uint16_t c)
{
    static const int8_t NOISE[6] = {60, 10, 85, 30, 100, 45};   /* (x %, y %) is i * 20 %, NOISE[i] % down */
    int32_t x0 = o->x * 16 + 12, y0 = o->y * 16 + 12, w = o->w * 16 - 24, h = o->h * 16 - 24;
    int16_t p[2 * 9];
    uint32_t np = 0, k;
#define LD_PT(fx, fy) (p[2 * np] = (int16_t)(x0 + (fx)), p[2 * np + 1] = (int16_t)(y0 + (fy)), np++)
    switch (o->g) {
    case 0: LD_PT(0, h); LD_PT(w * 7 / 10, 0); LD_PT(w * 7 / 10, h); break;
    case 1: LD_PT(0, h); LD_PT(w / 2, 0); LD_PT(w, h); break;
    case 2: LD_PT(w * 15 / 100, h); LD_PT(w * 15 / 100, 0); LD_PT(w * 85 / 100, 0); LD_PT(w * 85 / 100, h); break;
    case 3:
        for (k = 0; k <= 8u; k++)                  /* half a turn of sine in 8 segments */
            LD_PT(w * (int32_t)k / 8, h - ((h * cr_sin(k * 32768u / 8u)) >> 14));
        break;
    default:
        for (k = 0; k < 6u; k++)
            LD_PT(w * (int32_t)k / 5, h * NOISE[k] / 100);
        break;
    }
#undef LD_PT
    cr_poly(p, np, 24, 0, 0, c);
}
static void ld_play(void)
{
    static cr_screen_t s;
    uint32_t i;
    cr_screen_clear(&s);
    s.kind = CR_K_EDIT8;
    snprintf(s.title, sizeof s.title, "%s", ld_title);
    snprintf(s.page, sizeof s.page, "%s", ld_page);
    if (cr_in_strip(0, 26))
        cr_ed_title(&s);                           /* the shared header: the editor's own title line */
    for (i = 0; i < nld; i++) {
        const ld_t *o = &ld[i];
        uint16_t c = cr_rgb(o->col, T_TEXT);
        switch (o->op) {
        case LD_TEXT:
            if (o->w > 0)
                cr_text_fit(P8(o->x), P8(o->y), o->s, o->px, o->a, o->b, c, T_BG, P8(o->w));
            else
                cr_text(P8(o->x), P8(o->y), o->s, o->px, o->a, o->b, 4096, c, T_BG, 0);
            break;
        case LD_RECT:
            if (!o->b) {
                if (o->a)
                    cv_rrect(o->x, o->y, o->w, o->h, o->a, c, T_BG);
                else
                    cr_fill(o->x, o->y, o->w, o->h, c);
            } else if (o->a) {
                ld_rline(o->x, o->y, o->w, o->h, o->a, o->b, c);
            } else {
                cr_fill(o->x, o->y, o->w, o->b, c);
                cr_fill(o->x, o->y + o->h - o->b, o->w, o->b, c);
                cr_fill(o->x, o->y, o->b, o->h, c);
                cr_fill(o->x + o->w - o->b, o->y, o->b, o->h, c);
            }
            break;
        case LD_GLYPH: {
            cr_cell_t cl;
            memset(&cl, 0, sizeof cl);
            cl.flags = (uint16_t)(CR_CF_ON | o->flags);
            cl.glyph = o->g;
            cl.pct = o->pct;
            cl.pct2 = o->pct2;
            if (o->g == CR_G_LFOWAVE) {            /* (its wave: CR_LW_*, in pct2) */
                cl.wave = o->pct2;
                cl.pct2 = 0;
            }
            cr_cglyph(&cl, o->x * 16, o->y * 16, o->w * 16, o->h * 16, c);
            break;
        }
        case LD_HBAR: {
            cr_cell_t cl;
            memset(&cl, 0, sizeof cl);
            cl.flags = (uint16_t)(CR_CF_ON | o->flags);
            cl.pct = o->pct;
            cr_hbar(&cl, o->x * 16, o->w * 16, o->y * 16, o->h * 16, c);
            break;
        }
        case LD_P5KNOB:
            ld_p5knob(o);
            break;
        case LD_DISC:
            cr_disc(o->x, o->y, o->w, c);
            break;
        case LD_WAVE:
            ld_wave(o, c);
            break;
        case LD_BAND:
            s.wide = o->g;
            memcpy(s.wv, o->wv, sizeof s.wv);
            cr_wide(&s, CR_NAMED[CR_KC[0]]);
            break;
        }
    }
}
/* the recorded page drawn in the renderer's strips (as cr_draw: cr_cv_begin, cv_oy, the clip) and blitted to the LCD;
 * the picture as lay_render's, and lay_lint answers for it. The firmware's draw cache is told the LCD changed */
LAY_EXPORT uint8_t *lay_draw_end(void)
{
    uint32_t k;
    lay_boot();
    lay_nbox = 0;
    lay_lint_on = 1;
    for (k = 0; k < CR_NSTRIP; k++) {
        cr_strip_y = k * CR_STRIP_H;
        cr_cv_begin();
        cv_oy = -(int32_t)cr_strip_y;
        cr_clip_all();
        ld_play();
        cv_oy = 0;
        lcd_blit(0, cr_strip_y, 240u, CR_STRIP_H, cv_px);
    }
    lay_lint_on = 0;
    cr_draw_invalidate();
    lay_to_rgba();
    return lay_rgba;
}
/* the advance of s at px / bold, whole px (the page's own checks: a title that fits the header) */
LAY_EXPORT int32_t lay_text_w(const char *s, uint32_t px, uint32_t bold)
{
    lay_boot();
    return (cr_tw(s, px, bold ? 1 : 0) + 255) >> 8;
}
/* the band values the editor's screen model holds now (cu_scr.wv, 20 bytes): a free page's band takes them from the
 * classic screen that has that band */
LAY_EXPORT const uint8_t *lay_wv(void) { lay_boot(); return cu_scr.wv; }
