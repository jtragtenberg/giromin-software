/**
 * giromin.centro — Pure Data external
 *
 * Desloca o centro (fase) de dados cíclicos, produzindo um delta no mesmo espaço.
 *
 * Exemplo: dados de sensor em [-1, 1] com wrap cíclico (período 2).
 *   center=0.7, val=-0.9 → delta=0.4 (passou pela borda, caminho mais curto)
 *
 * NOTA SOBRE A DESCONTINUIDADE:
 *   O wrapper escolhe sempre o caminho mais curto no círculo.
 *   Isso move o ponto de salto para o oposto do centro.
 *   Exemplo: center=0.7 → descontinuidade em -0.3.
 *   Calibre o centro na posição de repouso — o salto vai para o extremo oposto.
 *
 * Uso: [giromin.centro AMPLITUDE]
 *   AMPLITUDE: metade do período (default 1 → range [-1, 1], período 2)
 *   Para dados em [0, 360]: [giromin.centro 180]
 *   Para dados em [-π, π]: [giromin.centro 3.14159]
 *
 * Inlet 0 (hot):
 *   float        → calcula delta cíclico em relação ao centro, emite resultado
 *   "center"     → captura valor atual como novo centro
 *   "center <f>" → define centro explicitamente
 *
 * Inlet 1 (cold float): amplitude (default = arg de criação ou 1)
 *
 * Outlet 0: delta em [-AMPLITUDE, AMPLITUDE]
 */

#include <cmath>

#include "giromin_plugdata.h"

static t_class *giromin_centro_class;

typedef struct {
    t_object x_obj;
    t_float  val;        /* último valor recebido */
    t_float  center;     /* ponto zero de referência */
    t_float  amplitude;  /* metade do período (default 1) */
    t_outlet *out;
} t_giromin_centro;

/* ── Wrapper cíclico ─────────────────────────────────────────────────────────
 * Retorna a distância mais curta de val até center no espaço cíclico
 * com período 2*amplitude. Resultado em [-amplitude, amplitude].
 */
static t_float cyclic_delta(t_float val, t_float center, t_float amp) {
    t_float period = 2.0f * amp;
    t_float d = val - center;
    while (d >  amp) d -= period;
    while (d < -amp) d += period;
    return d;
}

static void giromin_centro_float(t_giromin_centro *x, t_floatarg f) {
    x->val = f;
    outlet_float(x->out, cyclic_delta(x->val, x->center, x->amplitude));
}

static void giromin_centro_center(t_giromin_centro *x, t_symbol * /*s*/,
                                  int argc, t_atom *argv) {
    x->center = (argc > 0) ? atom_getfloat(argv) : x->val;
    post("giromin.centro: centro = %g  (descontinuidade em %g)",
         (double)x->center,
         (double)(x->center + (x->center >= 0.0f ? -x->amplitude : x->amplitude)));
    outlet_float(x->out, cyclic_delta(x->val, x->center, x->amplitude));
}

static void *giromin_centro_new(t_symbol * /*s*/, int argc, t_atom *argv) {
    t_giromin_centro *x = (t_giromin_centro *)pd_new(giromin_centro_class);
    x->val       = 0.0f;
    x->center    = 0.0f;
    x->amplitude = (argc > 0) ? atom_getfloat(argv) : 1.0f;
    if (x->amplitude <= 0.0f) x->amplitude = 1.0f;

    floatinlet_new(&x->x_obj, &x->amplitude);
    x->out = outlet_new(&x->x_obj, &s_float);
    return x;
}

extern "C" void giromin_centro_setup(void) {
    giromin_centro_class = class_new(
        gensym("giromin.centro"),
        (t_newmethod)giromin_centro_new,
        0, sizeof(t_giromin_centro),
        CLASS_DEFAULT,
        A_GIMME, A_NULL
    );
    class_addfloat(giromin_centro_class, giromin_centro_float);
    class_addmethod(giromin_centro_class,
                    (t_method)giromin_centro_center,
                    gensym("center"), A_GIMME, A_NULL);
    gm_class_desc(giromin_centro_class, "Desloca o ponto de referencia de dados ciclicos — util para calibrar a posicao de repouso de um sensor");
    gm_inlet_desc(giromin_centro_class, 0, "valor de entrada (float) | 'center' para capturar centro atual | 'center F' para definir explicitamente");
    gm_inlet_desc(giromin_centro_class, 1, "amplitude — metade do periodo do dado ciclico (padrao 1 para dados em [-1, 1])");
    gm_outlet_desc(giromin_centro_class, 0, "delta em [-amplitude, +amplitude] relativo ao centro definido");
    post("giromin.centro: deslocamento de fase ciclica carregado");
}

extern "C" void setup_giromin0x2ecentro(void) { giromin_centro_setup(); }
