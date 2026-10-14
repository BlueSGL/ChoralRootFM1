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
#define LAY_API 1                                  /* the exported API's version (lay_version "api") */
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
static void lay_boot(void)
{
    if (lay_booted)
        return;
    emu_fw_options(0, 1, 0, 1);                    /* no flash file: a fresh flash in RAM, nothing read or written */
    emu_hal.ready = 2;                             /* simulated time */
    cp_deep_hook = lay_deep_hook;                  /* (cr_pages.c, CR_LAYOUT_PREVIEW: step 3's door; the built-in pages) */
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
                                      "note", "onoff", "oct", "steps", "ofs", "fmnote", "fmfrq", "timei"};
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
    j_ki("format_version", 1);
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
LAY_EXPORT uint8_t *lay_render(void)
{
    uint32_t i;
    lay_boot();
    lay_nbox = 0;
    lay_lint_on = 1;
    cr_draw_invalidate();
    cr_ui_draw();
    lay_lint_on = 0;
    for (i = 0; i < 240u * 240u; i++) {
        uint32_t c = emu_hal.lcd[i];
        c = ((c >> 8) | (c << 8)) & 0xFFFFu;
        lay_rgba[4u * i] = (uint8_t)((c >> 11) * 255u / 31u);
        lay_rgba[4u * i + 1u] = (uint8_t)(((c >> 5) & 63u) * 255u / 63u);
        lay_rgba[4u * i + 2u] = (uint8_t)((c & 31u) * 255u / 31u);
        lay_rgba[4u * i + 3u] = 255;
    }
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

/* -------------------------------------------------------- step 3's door: lay_load --- */
/* cr_pages.c cp_deep() asks this hook (CR_LAYOUT_PREVIEW only) for the engine's pages: an alternate eng_deep_t per
 * engine, the built-in one when there is none. Step 3 fills lay_alt[] from a packed layout; step 2 never does */
static const eng_deep_t *lay_alt[NENGINES];
static const eng_deep_t *lay_deep_hook(const track_t *t, const eng_deep_t *builtin)
{
    const eng_deep_t *a = lay_alt[eng_idx(t->eng_req)];
    return a ? a : builtin;
}
static uint8_t lay_in[1u << 16];
/* where the page writes a packed layout before lay_load (up to lay_buf_size bytes) */
LAY_EXPORT uint8_t *lay_buf(void) { return lay_in; }
LAY_EXPORT uint32_t lay_buf_size(void) { return sizeof lay_in; }
/* RESERVED (step 3): packed tables replacing an engine's built-in layout through the hook above. Step 2: the hook is
 * installed (lay_boot) and returns the built-in tables; nothing is read. {"ok": false, "error": "not implemented"} */
LAY_EXPORT const char *lay_load(const uint8_t *ptr, uint32_t len)
{
    (void)ptr;
    (void)len;
    lay_boot();
    j_begin();
    j_open('{');
    j_kb("ok", 0);
    j_ks("error", "not implemented");
    j_close('}');
    return j_end();
}
