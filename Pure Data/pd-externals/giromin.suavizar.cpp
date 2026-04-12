/**
 * giromin.suavizar — Pure Data external
 *
 * Filtro EMA (Exponential Moving Average) assimétrico.
 * Parametrizado como [slide] do Max: número de steps para atingir o alvo.
 *
 * Uso: [giromin.suavizar RISE FALL]
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
 *   [giromin.suavizar 1 1]     → pass-through (sem filtragem)
 *   [giromin.suavizar 1 20]    → sobe imediato, desce em ~20 steps
 *   [giromin.suavizar 10 10]   → suave e simétrico
 */

#include <cmath>

#include "giromin_plugdata.h"

/* ── classe PD ── */
static t_class *giromin_suavizar_class;

typedef struct _giromin_suavizar {
    t_object x_obj;
    t_float  rise;          /* coeficiente de subida [0-1] */
    t_float  fall;          /* coeficiente de descida [0-1] */
    t_float  filtered;      /* valor filtrado atual */
    t_float  last_filtered; /* valor filtrado anterior (para detectar direção) */
    t_outlet *out;
} t_giromin_suavizar;

/* ── Handler: float de entrada ── */
static void giromin_suavizar_float(t_giromin_suavizar *x, t_float input)
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
static void giromin_suavizar_reset(t_giromin_suavizar *x)
{
    x->filtered      = 0.f;
    x->last_filtered = 0.f;
    post("giromin.suavizar: estado zerado");
}

/* ── Construtor ── */
static void *giromin_suavizar_new(t_floatarg rise, t_floatarg fall)
{
    t_giromin_suavizar *x = (t_giromin_suavizar *)pd_new(giromin_suavizar_class);

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
extern "C" void giromin_suavizar_setup(void)
{
    giromin_suavizar_class = class_new(
        gensym("giromin.suavizar"),
        (t_newmethod)giromin_suavizar_new,
        0,                       /* sem destrutor */
        sizeof(t_giromin_suavizar),
        CLASS_DEFAULT,
        A_DEFFLOAT,              /* rise (default 1 = pass-through) */
        A_DEFFLOAT,              /* fall (default 1 = pass-through) */
        0
    );

    class_addfloat(giromin_suavizar_class,
                   (t_method)giromin_suavizar_float);

    class_addmethod(giromin_suavizar_class,
                    (t_method)giromin_suavizar_reset,
                    gensym("reset"), A_NULL);

    gm_class_desc(giromin_suavizar_class, "Filtro de suavizacao assimetrico — separa velocidade de subida (rise) e descida (fall)");
    gm_inlet_desc(giromin_suavizar_class, 0, "valor de entrada (float)");
    gm_inlet_desc(giromin_suavizar_class, 1, "rise — steps para subir (1 = sem filtro, maior = mais lento)");
    gm_inlet_desc(giromin_suavizar_class, 2, "fall — steps para descer (1 = sem filtro, maior = mais lento)");
    gm_outlet_desc(giromin_suavizar_class, 0, "valor filtrado");
    post("giromin.suavizar: filtro EMA assimetrico carregado");
}

/* Alias para PD 0.55+ que busca setup_classname com hex para '.' (0x2e) */
extern "C" void setup_giromin0x2esuavizar(void) { giromin_suavizar_setup(); }
