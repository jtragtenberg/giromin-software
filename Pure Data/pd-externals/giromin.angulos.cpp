/**
 * giromin.angulos — Pure Data external
 *
 * Converte um quaternion unitário (w x y z) para ângulos de Euler Tait-Bryan.
 * Portado diretamente de IMUGestureToolkit::convertQuaternionToEuler().
 *
 * Uso: [giromin.angulos ORDER]
 *   ORDER: xyz xzy yxz yzx zxy zyx  (default: xyz)
 *   Pode ser mudado em runtime: send "order xyz" (ou qualquer outra ordem)
 *
 * Entrada (inlet 0): lista de 4 floats — w x y z
 *
 * Saídas (da esquerda para direita):
 *   outlet 0 (esq):  euler_first  [-π, π]  — primeiro eixo da ordem (ex: roll para XYZ)
 *   outlet 1 (meio): euler_last   [-π, π]  — último eixo da ordem (ex: yaw para XYZ)
 *   outlet 2 (dir):  euler_mid    [-π/2, π/2] — eixo do meio (gimbal lock, ex: pitch para XYZ)
 *
 * Exemplos:
 *   quaternion identidade (w=1 x=0 y=0 z=0) → (0, 0, 0)
 *   Para testar no PD: [list 1 0 0 0( → [giromin.angulos xyz]
 *
 * Compilação: veja o Makefile nesta pasta
 */

#include <cmath>
#include <cstring>

#include "giromin_plugdata.h"

/* ── classe PD ── */
static t_class *giromin_angulos_class;

typedef struct _giromin_angulos {
    t_object x_obj;
    int      order;         /* 0=XYZ 1=XZY 2=YXZ 3=YZX 4=ZXY 5=ZYX */
    t_outlet *out_first;    /* outlet 0 (esq): a_first */
    t_outlet *out_last;     /* outlet 1 (meio): a_last */
    t_outlet *out_mid;      /* outlet 2 (dir): a_mid — eixo gimbal */
} t_giromin_angulos;

/* ── Conversão quaternion → Euler ──────────────────────────────────────────
 * Portado de IMUGestureToolkit::convertQuaternionToEuler() (sem dependência JUCE)
 *
 * Retorna {a_first, a_last, a_mid}:
 *   a_first, a_last ∈ [-π, π]    (via atan2)
 *   a_mid           ∈ [-π/2, π/2] (via asin — eixo com restrição de gimbal)
 */
static void quat_to_euler(float w, float x, float y, float z, int order,
                           float *a_first, float *a_last, float *a_mid)
{
    /* Elementos da matriz de rotação 3×3 */
    float xx = x*x, yy = y*y, zz = z*z;
    float r00 = 1.f - 2.f*(yy+zz),  r01 = 2.f*(x*y - w*z),  r02 = 2.f*(x*z + w*y);
    float r10 = 2.f*(x*y + w*z),     r11 = 1.f - 2.f*(xx+zz), r12 = 2.f*(y*z - w*x);
    float r20 = 2.f*(x*z - w*y),     r21 = 2.f*(y*z + w*x),   r22 = 1.f - 2.f*(xx+yy);

    /* Clamp para evitar NaN no asin (domínio [-1, 1]) */
    #define CLAMP1(v) ((v) < -1.f ? -1.f : ((v) > 1.f ? 1.f : (v)))

    switch (order) {
        case 0: /* XYZ */
            *a_mid   = asinf(CLAMP1( r02));
            *a_first = atan2f(-r12,  r22);
            *a_last  = atan2f(-r01,  r00);
            break;
        case 1: /* XZY */
            *a_mid   = asinf(CLAMP1(-r01));
            *a_first = atan2f( r21,  r11);
            *a_last  = atan2f( r02,  r00);
            break;
        case 2: /* YXZ */
            *a_mid   = asinf(CLAMP1(-r12));
            *a_first = atan2f( r02,  r22);
            *a_last  = atan2f( r10,  r11);
            break;
        case 3: /* YZX */
            *a_mid   = asinf(CLAMP1( r10));
            *a_first = atan2f(-r20,  r00);
            *a_last  = atan2f(-r12,  r11);
            break;
        case 4: /* ZXY */
            *a_mid   = asinf(CLAMP1( r21));
            *a_first = atan2f(-r01,  r11);
            *a_last  = atan2f(-r20,  r22);
            break;
        case 5: /* ZYX */
        default:
            *a_mid   = asinf(CLAMP1(-r20));
            *a_first = atan2f( r10,  r00);
            *a_last  = atan2f( r21,  r22);
            break;
    }
    #undef CLAMP1
}

/* ── Handler: lista de 4 floats (w x y z) ── */
static void giromin_angulos_list(t_giromin_angulos *x, t_symbol * /*s*/,
                                int argc, t_atom *argv)
{
    if (argc < 4) {
        pd_error(x, "giromin.angulos: precisa de 4 floats (w x y z)");
        return;
    }
    float w  = atom_getfloat(argv);
    float qx = atom_getfloat(argv + 1);
    float qy = atom_getfloat(argv + 2);
    float qz = atom_getfloat(argv + 3);

    float a_first, a_last, a_mid;
    quat_to_euler(w, qx, qy, qz, x->order, &a_first, &a_last, &a_mid);

    /* Saída da direita para a esquerda (convenção PD) */
    outlet_float(x->out_mid,   (t_float)a_mid);
    outlet_float(x->out_last,  (t_float)a_last);
    outlet_float(x->out_first, (t_float)a_first);
}

/* ── Handler: mudança de ordem em runtime ── */
static void giromin_angulos_order(t_giromin_angulos *x, t_symbol *s)
{
    const char *name = s->s_name;
    if      (strcmp(name, "xyz") == 0) x->order = 0;
    else if (strcmp(name, "xzy") == 0) x->order = 1;
    else if (strcmp(name, "yxz") == 0) x->order = 2;
    else if (strcmp(name, "yzx") == 0) x->order = 3;
    else if (strcmp(name, "zxy") == 0) x->order = 4;
    else if (strcmp(name, "zyx") == 0) x->order = 5;
    else pd_error(x, "giromin.angulos: ordem desconhecida '%s' (xyz xzy yxz yzx zxy zyx)", name);
}

/* ── Construtor ── */
static void *giromin_angulos_new(t_symbol *s)
{
    t_giromin_angulos *x = (t_giromin_angulos *)pd_new(giromin_angulos_class);
    x->order = 0; /* default: XYZ */

    /* Aplica argumento de ordem se fornecido */
    if (s && s->s_name[0] != '\0')
        giromin_angulos_order(x, s);

    /* Cria outlets: esquerda → direita = first, last, mid */
    x->out_first = outlet_new(&x->x_obj, &s_float);
    x->out_last  = outlet_new(&x->x_obj, &s_float);
    x->out_mid   = outlet_new(&x->x_obj, &s_float);

    return (void *)x;
}

/* ── Setup — chamado quando PD carrega o external ── */
extern "C" void giromin_angulos_setup(void)
{
    giromin_angulos_class = class_new(
        gensym("giromin.angulos"),
        (t_newmethod)giromin_angulos_new,
        0,                          /* sem destrutor */
        sizeof(t_giromin_angulos),
        CLASS_DEFAULT,
        A_DEFSYM,                   /* argumento de criação: ordem (opcional) */
        0
    );

    class_addlist(giromin_angulos_class,
                  (t_method)giromin_angulos_list);

    class_addmethod(giromin_angulos_class,
                    (t_method)giromin_angulos_order,
                    gensym("order"), A_SYMBOL, 0);

    gm_class_desc(giromin_angulos_class, "Converte quaternion (w x y z) para angulos de Euler Tait-Bryan em 6 ordens possiveis");
    gm_inlet_desc(giromin_angulos_class, 0, "lista w x y z — quaternion unitario de entrada");
    gm_outlet_desc(giromin_angulos_class, 0, "primeiro angulo da ordem escolhida [-pi, pi]");
    gm_outlet_desc(giromin_angulos_class, 1, "ultimo angulo da ordem escolhida [-pi, pi]");
    gm_outlet_desc(giromin_angulos_class, 2, "angulo do meio — eixo gimbal [-pi/2, pi/2]");
    post("giromin.angulos: quaternion -> angulos de Euler carregado");
}

/* Alias para PD 0.55+ que busca setup_classname com hex para '.' (0x2e) */
extern "C" void setup_giromin0x2eangulos(void) { giromin_angulos_setup(); }
