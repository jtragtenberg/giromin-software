/**
 * giromin.map — Pure Data external
 *
 * Mapeia valores em [-1, 1] para [0, 1] com range ajustável, inversão e auto-range.
 * Para deslocar o centro de dados cíclicos antes deste objeto, use [giromin.phase].
 *
 * Uso: [giromin.map IN_MIN IN_MAX]   (args opcionais, default: -1 1)
 *
 * Inlet 0 (hot):
 *   float           → recebe valor, mapeia e emite
 *   "invert"        → inverte in_min e in_max entre si; emite pelos outlets 1 e 2
 *   "autorange 1"   → liga aprendizado de range (reseta min/max internos)
 *   "autorange 0"   → para aprendizado e fixa in_min/in_max com o que aprendeu
 *
 * Inlet 1 (cold float): in_min — limite inferior do range de trabalho
 * Inlet 2 (cold float): in_max — limite superior do range de trabalho
 *
 * Outlets:
 *   0: valor mapeado [0, 1]
 *   1: ar_min — emite quando o mínimo diminui (autorange) ou ao usar invert
 *   2: ar_max — emite quando o máximo aumenta (autorange) ou ao usar invert
 */

#include <cmath>
#include <cfloat>

extern "C" {
#include "m_pd.h"
}

static t_class *giromin_map_class;

typedef struct {
    t_object x_obj;
    t_float  val;        /* último valor recebido */
    t_float  in_min;     /* mínimo do range de trabalho */
    t_float  in_max;     /* máximo do range de trabalho */
    int      autorange;  /* 0 = desligado, 1 = aprendendo */
    t_float  ar_min;     /* mínimo rastreado durante autorange */
    t_float  ar_max;     /* máximo rastreado durante autorange */
    t_outlet *out_val;
    t_outlet *out_armin;
    t_outlet *out_armax;
} t_giromin_map;

/* ── Emite in_min/in_max pelos outlets 1 e 2 (ordem PD: direita primeiro) ── */
static void giromin_map_emit_range(t_giromin_map *x) {
    t_float lo = x->autorange ? x->ar_min : x->in_min;
    t_float hi = x->autorange ? x->ar_max : x->in_max;
    if (lo == FLT_MAX || hi == -FLT_MAX) return;
    outlet_float(x->out_armax, hi);
    outlet_float(x->out_armin, lo);
}

/* ── Processamento central ─────────────────────────────────────────────────── */
static void giromin_map_compute(t_giromin_map *x) {
    t_float v = x->val;

    t_float lo, hi;
    if (x->autorange) {
        if (v > x->ar_max) { x->ar_max = v; outlet_float(x->out_armax, x->ar_max); }
        if (v < x->ar_min) { x->ar_min = v; outlet_float(x->out_armin, x->ar_min); }
        lo = x->ar_min;
        hi = x->ar_max;
    } else {
        lo = x->in_min;
        hi = x->in_max;
    }

    t_float span = hi - lo;
    t_float out  = (fabsf(span) < 1e-9f) ? 0.5f : (v - lo) / span;
    if (out < 0.0f) out = 0.0f;
    if (out > 1.0f) out = 1.0f;

    outlet_float(x->out_val, out);
}

static void giromin_map_float(t_giromin_map *x, t_floatarg f) {
    x->val = f;
    giromin_map_compute(x);
}

/* "invert" — troca in_min e in_max entre si */
static void giromin_map_invert(t_giromin_map *x) {
    t_float tmp = x->in_min;
    x->in_min   = x->in_max;
    x->in_max   = tmp;
    giromin_map_emit_range(x);
    giromin_map_compute(x);
}

/* "autorange 1|0" */
static void giromin_map_autorange(t_giromin_map *x, t_floatarg f) {
    if (f != 0.0f) {
        x->autorange = 1;
        x->ar_min    =  FLT_MAX;
        x->ar_max    = -FLT_MAX;
        post("giromin.map: auto-range LIGADO");
    } else {
        if (x->autorange && x->ar_min < x->ar_max) {
            x->in_min = x->ar_min;
            x->in_max = x->ar_max;
            post("giromin.map: auto-range FIXADO — in_min=%g  in_max=%g",
                 (double)x->in_min, (double)x->in_max);
        } else {
            post("giromin.map: auto-range DESLIGADO (dados insuficientes)");
        }
        x->autorange = 0;
    }
}

/* ── Construtor ──────────────────────────────────────────────────────────── */
static void *giromin_map_new(t_symbol * /*sym*/, int argc, t_atom *argv) {
    t_giromin_map *x = (t_giromin_map *)pd_new(giromin_map_class);
    x->val       = 0.0f;
    x->in_min    = (argc > 0) ? atom_getfloat(argv)     : -1.0f;
    x->in_max    = (argc > 1) ? atom_getfloat(argv + 1) :  1.0f;
    x->autorange = 0;
    x->ar_min    =  FLT_MAX;
    x->ar_max    = -FLT_MAX;

    floatinlet_new(&x->x_obj, &x->in_min);
    floatinlet_new(&x->x_obj, &x->in_max);

    x->out_val   = outlet_new(&x->x_obj, &s_float);
    x->out_armin = outlet_new(&x->x_obj, &s_float);
    x->out_armax = outlet_new(&x->x_obj, &s_float);
    return x;
}

/* ── Setup ──────────────────────────────────────────────────────────────── */
extern "C" void giromin_map_setup(void) {
    giromin_map_class = class_new(
        gensym("giromin.map"),
        (t_newmethod)giromin_map_new,
        0, sizeof(t_giromin_map),
        CLASS_DEFAULT,
        A_GIMME, A_NULL
    );
    class_addfloat(giromin_map_class, giromin_map_float);
    class_addmethod(giromin_map_class,
                    (t_method)giromin_map_invert,
                    gensym("invert"), A_NULL);
    class_addmethod(giromin_map_class,
                    (t_method)giromin_map_autorange,
                    gensym("autorange"), A_FLOAT, A_NULL);
    post("giromin.map: mapeamento [-1,1] -> [0,1] carregado");
}

extern "C" void setup_giromin0x2emap(void) { giromin_map_setup(); }
