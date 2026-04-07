/**
 * giromin.ema — Pure Data external
 *
 * Filtro EMA (Exponential Moving Average) assimétrico.
 * Parametrizado como [slide] do Max: número de steps para atingir o alvo.
 *
 * Uso: [giromin.ema RISE FALL]
 *   RISE: steps de subida  (1 = sem filtro, 10 = ~10 steps, 100 = muito lento)
 *   FALL: steps de descida
 *   Conversão interna: alpha = 1 - 1/slide  (slide >= 1)
 *
 * Inlets:
 *   inlet 0 (hot):  valor de entrada (float) — dispara o cálculo
 *   inlet 1 (cold): rise (steps)
 *   inlet 2 (cold): fall (steps)
 *
 * Outlet: valor filtrado
 *
 * Mensagem "reset": zera o estado interno
 *
 * Como funciona:
 *   Se entrada > última saída filtrada: usa rise
 *   Se entrada ≤ última saída filtrada: usa fall
 *   alpha  = 1 - 1/slide
 *   output = (1 - alpha) * input + alpha * filtered
 *          = input/slide + filtered * (1 - 1/slide)
 *
 * Exemplos:
 *   [giromin.ema 1 1]     → pass-through (sem filtragem)
 *   [giromin.ema 1 20]    → sobe imediato, desce em ~20 steps
 *   [giromin.ema 10 10]   → suave e simétrico
 */

#include <cmath>

extern "C" {
#include "m_pd.h"
}

/* ── classe PD ── */
static t_class *giromin_ema_class;

typedef struct _giromin_ema {
    t_object x_obj;
    t_float  rise;          /* coeficiente de subida [0-1] */
    t_float  fall;          /* coeficiente de descida [0-1] */
    t_float  filtered;      /* valor filtrado atual */
    t_float  last_filtered; /* valor filtrado anterior (para detectar direção) */
    t_outlet *out;
} t_giromin_ema;

/* ── Handler: float de entrada ── */
static void giromin_ema_float(t_giromin_ema *x, t_float input)
{
    /* Escolhe slide baseado na direção do sinal */
    t_float slide = (input > x->last_filtered) ? x->rise : x->fall;

    /* slide >= 1 (1 = pass-through) */
    if (slide < 1.f) slide = 1.f;

    /* Converte slide → alpha: alpha = 1 - 1/slide */
    t_float alpha = 1.f - 1.f / slide;

    /* EMA: output = (1-alpha)*input + alpha*filtered */
    t_float result = (1.f - alpha) * input + alpha * x->filtered;

    x->last_filtered = result;
    x->filtered      = result;

    outlet_float(x->out, result);
}

/* ── Handler: reset do estado interno ── */
static void giromin_ema_reset(t_giromin_ema *x)
{
    x->filtered      = 0.f;
    x->last_filtered = 0.f;
    post("giromin.ema: estado zerado");
}

/* ── Construtor ── */
static void *giromin_ema_new(t_floatarg rise, t_floatarg fall)
{
    t_giromin_ema *x = (t_giromin_ema *)pd_new(giromin_ema_class);

    x->rise          = (rise  < 1.f) ? 1.f : rise;
    x->fall          = (fall  < 1.f) ? 1.f : fall;
    x->filtered      = 0.f;
    x->last_filtered = 0.f;

    /* Inlets adicionais (cold): escrevem diretamente nas variáveis */
    floatinlet_new(&x->x_obj, &x->rise);
    floatinlet_new(&x->x_obj, &x->fall);

    x->out = outlet_new(&x->x_obj, &s_float);

    return (void *)x;
}

/* ── Setup ── */
extern "C" void giromin_ema_setup(void)
{
    giromin_ema_class = class_new(
        gensym("giromin.ema"),
        (t_newmethod)giromin_ema_new,
        0,                       /* sem destrutor */
        sizeof(t_giromin_ema),
        CLASS_DEFAULT,
        A_DEFFLOAT,              /* rise (default 1 = pass-through) */
        A_DEFFLOAT,              /* fall (default 1 = pass-through) */
        0
    );

    class_addfloat(giromin_ema_class,
                   (t_method)giromin_ema_float);

    class_addmethod(giromin_ema_class,
                    (t_method)giromin_ema_reset,
                    gensym("reset"), A_NULL);

    post("giromin.ema: filtro EMA assimetrico carregado");
}

/* Alias para PD 0.55+ que busca setup_classname com hex para '.' (0x2e) */
extern "C" void setup_giromin0x2eema(void) { giromin_ema_setup(); }
