/**
 * giromin.peak — Pure Data external
 *
 * Detecta picos locais e emite exatamente dois eventos por disparo:
 *   1. peak_val  — no momento da detecção do pico (note on)
 *   2. 0         — após debounce_ms (note off + libera novo pico)
 *   Entre os dois eventos: nenhum output, nenhum novo pico detectado.
 *
 * LATÊNCIA: 1 sample de controle.
 *
 * Uso: [giromin.peak THRESHOLD DEBOUNCE_MS]
 *   THRESHOLD:   valor mínimo para considerar pico (default 0.5)
 *   DEBOUNCE_MS: duração do note on e bloqueio de novos picos (default 200)
 *
 * Inlets:
 *   0 (hot):  float — valor de entrada
 *   1 (cold): threshold
 *   2 (cold): debounce_ms
 *
 * Outlet 0:
 *   peak_val — no momento do pico
 *   0        — após debounce_ms
 */

#include <cfloat>

extern "C" {
#include "m_pd.h"
}

static t_class *giromin_peak_class;

typedef struct {
    t_object  x_obj;
    t_float   val;
    t_float   prev;
    t_float   threshold;
    t_float   debounce_ms;
    int       in_debounce;
    t_clock  *debounce_clock;
    t_outlet *out;
} t_giromin_peak;

/* Emite note off e libera detecção — chamado após debounce_ms */
static void giromin_peak_debounce_end(t_giromin_peak *x) {
    x->in_debounce = 0;
    outlet_float(x->out, 0.0f);
}

static void giromin_peak_float(t_giromin_peak *x, t_floatarg f) {
    if (!x->in_debounce) {
        if (x->prev <= x->val && x->val > f && x->val > x->threshold) {
            x->in_debounce = 1;

            t_float deb = (x->debounce_ms > 0.f) ? x->debounce_ms : 1.f;
            outlet_float(x->out, x->val);
            clock_delay(x->debounce_clock, deb);
        }
    }
    x->prev = x->val;
    x->val  = f;
}

static void giromin_peak_reset(t_giromin_peak *x) {
    clock_unset(x->debounce_clock);
    x->in_debounce = 0;
    x->val  = 0.f;
    x->prev = 0.f;
    outlet_float(x->out, 0.0f);
}

static void *giromin_peak_new(t_symbol * /*s*/, int argc, t_atom *argv) {
    t_giromin_peak *x = (t_giromin_peak *)pd_new(giromin_peak_class);
    x->val         = 0.f;
    x->prev        = 0.f;
    x->threshold   = (argc > 0) ? atom_getfloat(argv)     : 0.5f;
    x->debounce_ms = (argc > 1) ? atom_getfloat(argv + 1) : 200.f;
    x->in_debounce = 0;

    floatinlet_new(&x->x_obj, &x->threshold);
    floatinlet_new(&x->x_obj, &x->debounce_ms);

    x->debounce_clock = clock_new(x, (t_method)giromin_peak_debounce_end);
    x->out = outlet_new(&x->x_obj, &s_float);
    return x;
}

static void giromin_peak_free(t_giromin_peak *x) {
    clock_free(x->debounce_clock);
}

extern "C" void giromin_peak_setup(void) {
    giromin_peak_class = class_new(
        gensym("giromin.peak"),
        (t_newmethod)giromin_peak_new,
        (t_method)giromin_peak_free,
        sizeof(t_giromin_peak),
        CLASS_DEFAULT,
        A_GIMME, A_NULL
    );
    class_addfloat(giromin_peak_class, giromin_peak_float);
    class_addmethod(giromin_peak_class,
                    (t_method)giromin_peak_reset,
                    gensym("reset"), A_NULL);
    post("giromin.peak: detector de picos carregado");
}

extern "C" void setup_giromin0x2epeak(void) { giromin_peak_setup(); }
