/**
 * giromin.map — Pure Data external
 *
 * Mapeia valores de [in_min, in_max] para [out_min, out_max] com inversão e auto-range.
 *
 * Uso: [giromin.map IN_MIN IN_MAX]                    → saída em [0, 1], linear
 *      [giromin.map IN_MIN IN_MAX OUT_MIN OUT_MAX]      → saída em [out_min, out_max], linear
 *      [giromin.map IN_MIN IN_MAX OUT_MIN OUT_MAX EXP]  → com curva exponencial
 *
 * Inlet 0 (hot):
 *   float           → recebe valor, mapeia e emite
 *   "invert"        → inverte in_min e in_max entre si; emite pelos outlets 1 e 2
 *   "autorange 1"   → liga aprendizado de range (reseta min/max internos)
 *   "autorange 0"   → para aprendizado e fixa in_min/in_max com o que aprendeu
 *   "exp F"         → define o expoente de curva (0 ou 1 = linear)
 *
 * Inlet 1 (cold float): in_min  — limite inferior do range de entrada
 * Inlet 2 (cold float): in_max  — limite superior do range de entrada
 * Inlet 3 (cold float): out_min — limite inferior do range de saída (default 0)
 * Inlet 4 (cold float): out_max — limite superior do range de saída (default 1)
 *
 * Exponencial (exp):
 *   0 ou 1   → linear (padrão)
 *   > 1      → curva côncava (comprime baixos, expande altos)
 *   0 < e<1  → curva convexa (expande baixos, comprime altos)
 *   < 0      → inverte a direção da curva
 *
 * Outlets:
 *   0: valor mapeado [out_min, out_max]
 *   1: ar_min — emite quando o mínimo diminui (autorange) ou ao usar invert
 *   2: ar_max — emite quando o máximo aumenta (autorange) ou ao usar invert
 */

#include <cmath>
#include <cfloat>

#include "giromin_plugdata.h"

static t_class *giromin_map_class;

typedef struct {
    t_object x_obj;
    t_float  val;        /* último valor recebido */
    t_float  in_min;     /* mínimo do range de entrada */
    t_float  in_max;     /* máximo do range de entrada */
    t_float  out_min;    /* mínimo do range de saída */
    t_float  out_max;    /* máximo do range de saída */
    t_float  exp;        /* expoente de curva (0 ou 1 = linear) */
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
    t_float norm = (fabsf(span) < 1e-9f) ? 0.5f : (v - lo) / span;
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;

    /* curva exponencial — mesma lógica do else/rescale */
    t_float e = x->exp;
    t_float curved;
    if (fabsf(e) == 1.0f || e == 0.0f)
        curved = norm;
    else if (e > 0.0f)
        curved = powf(norm, e);
    else
        curved = 1.0f - powf(1.0f - norm, -e);

    t_float out = x->out_min + curved * (x->out_max - x->out_min);

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

/* "exp F" — define o expoente de curva */
static void giromin_map_exp(t_giromin_map *x, t_floatarg f) {
    x->exp = f;
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
    x->out_min   = (argc > 2) ? atom_getfloat(argv + 2) :  0.0f;
    x->out_max   = (argc > 3) ? atom_getfloat(argv + 3) :  1.0f;
    x->exp       = (argc > 4) ? atom_getfloat(argv + 4) :  1.0f;
    x->autorange = 0;
    x->ar_min    =  FLT_MAX;
    x->ar_max    = -FLT_MAX;

    floatinlet_new(&x->x_obj, &x->in_min);
    floatinlet_new(&x->x_obj, &x->in_max);
    floatinlet_new(&x->x_obj, &x->out_min);
    floatinlet_new(&x->x_obj, &x->out_max);

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
                    (t_method)giromin_map_exp,
                    gensym("exp"), A_FLOAT, A_NULL);
    class_addmethod(giromin_map_class,
                    (t_method)giromin_map_autorange,
                    gensym("autorange"), A_FLOAT, A_NULL);
    gm_class_desc(giromin_map_class, "Mapeia um valor de um intervalo de entrada para um intervalo de saida, com curva exponencial e autorange");
    gm_inlet_desc(giromin_map_class, 0, "valor de entrada (float)");
    gm_inlet_desc(giromin_map_class, 1, "in_min — limite inferior do intervalo de entrada");
    gm_inlet_desc(giromin_map_class, 2, "in_max — limite superior do intervalo de entrada");
    gm_inlet_desc(giromin_map_class, 3, "out_min — limite inferior do intervalo de saida (padrao 0)");
    gm_inlet_desc(giromin_map_class, 4, "out_max — limite superior do intervalo de saida (padrao 1)");
    gm_outlet_desc(giromin_map_class, 0, "valor mapeado em [out_min, out_max]");
    gm_outlet_desc(giromin_map_class, 1, "ar_min — minimo aprendido pelo autorange (ou in_min apos invert)");
    gm_outlet_desc(giromin_map_class, 2, "ar_max — maximo aprendido pelo autorange (ou in_max apos invert)");
    post("giromin.map: carregado");
}

extern "C" void setup_giromin0x2emap(void) { giromin_map_setup(); }
