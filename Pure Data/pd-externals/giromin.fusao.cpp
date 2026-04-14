/**
 * giromin.fusao — Pure Data external
 *
 * Fusão de sensores IMU usando o algoritmo de Madgwick.
 * Portado de Madgwick.cpp (João Tragtenberg / Aster / SOH Madgwick).
 *
 * Uso: [giromin.fusao BETA]
 *   BETA: ganho do filtro (default: 0.3). Maior = reage mais rápido,
 *         porém mais suscetível a ruído. Menor = mais suave, mais lento.
 *
 * Entrada (inlet 0): lista com 6 floats — gx gy gz ax ay az  (graus/s e g)
 *                    ou lista com 9 floats — gx gy gz ax ay az mx my mz
 *
 * Saídas:
 *   outlet 0: lista  w x y z  (quaternion)
 *   outlet 1: float  w
 *   outlet 2: float  x
 *   outlet 3: float  y
 *   outlet 4: float  z
 *
 * Mensagens:
 *   reset          — reinicia quaternion para identidade (1 0 0 0)
 *   beta FLOAT     — altera o ganho em runtime
 *
 * Compilação: make no diretório pd-externals (veja o Makefile)
 */

#include <cmath>
#include <cstring>
#ifdef _WIN32
# include <windows.h>
#else
# include <time.h>
#endif

#include "giromin_plugdata.h"

/* ── Inverse square root ──────────────────────────────────────────────────── */
static float inv_sqrt(float x)
{
    return 1.0f / sqrtf(x);
}

/* ── Classe PD ────────────────────────────────────────────────────────────── */
static t_class *giromin_fusao_class;

typedef struct _giromin_fusao {
    t_object  x_obj;
    float     beta;          /* ganho Madgwick */
    float     q0, q1, q2, q3; /* quaternion */
    double    last_time;     /* microssegundos, para calcular dt */
    t_outlet *out_list;      /* outlet 0: lista w x y z */
    t_outlet *out_w;         /* outlet 1 */
    t_outlet *out_x;         /* outlet 2 */
    t_outlet *out_y;         /* outlet 3 */
    t_outlet *out_z;         /* outlet 4 */
} t_giromin_fusao;

/* ── Algoritmo 6-DOF (sem magnetômetro) ─────────────────────────────────────
 * Portado de Madgwick::MadgwickUpdate(gx,gy,gz,ax,ay,az,deltat)
 * gx gy gz em graus/s → convertidos para rad/s aqui
 */
static void madgwick_update_6(t_giromin_fusao *x,
                               float gx, float gy, float gz,
                               float ax, float ay, float az,
                               float dt)
{
    float recipNorm;
    float s0, s1, s2, s3;
    float qDot1, qDot2, qDot3, qDot4;
    float _2q0, _2q1, _2q2, _2q3, _4q0, _4q1, _4q2, _8q1, _8q2;
    float q0q0, q1q1, q2q2, q3q3;

    /* graus/s → rad/s */
    gx *= 0.0174533f;
    gy *= 0.0174533f;
    gz *= 0.0174533f;

    float q0 = x->q0, q1 = x->q1, q2 = x->q2, q3 = x->q3;

    /* Taxa de variação do quaternion a partir do giroscópio */
    qDot1 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
    qDot2 = 0.5f * ( q0 * gx + q2 * gz - q3 * gy);
    qDot3 = 0.5f * ( q0 * gy - q1 * gz + q3 * gx);
    qDot4 = 0.5f * ( q0 * gz + q1 * gy - q2 * gx);

    /* Feedback do acelerômetro apenas se válido */
    if (!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {
        recipNorm = inv_sqrt(ax * ax + ay * ay + az * az);
        ax *= recipNorm; ay *= recipNorm; az *= recipNorm;

        _2q0 = 2.0f * q0; _2q1 = 2.0f * q1;
        _2q2 = 2.0f * q2; _2q3 = 2.0f * q3;
        _4q0 = 4.0f * q0; _4q1 = 4.0f * q1; _4q2 = 4.0f * q2;
        _8q1 = 8.0f * q1; _8q2 = 8.0f * q2;
        q0q0 = q0 * q0; q1q1 = q1 * q1; q2q2 = q2 * q2; q3q3 = q3 * q3;

        s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay;
        s1 = _4q1 * q3q3 - _2q3 * ax + 4.0f * q0q0 * q1 - _2q0 * ay
             - _4q1 + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az;
        s2 = 4.0f * q0q0 * q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay
             - _4q2 + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az;
        s3 = 4.0f * q1q1 * q3 - _2q1 * ax + 4.0f * q2q2 * q3 - _2q2 * ay;

        recipNorm = inv_sqrt(s0*s0 + s1*s1 + s2*s2 + s3*s3);
        s0 *= recipNorm; s1 *= recipNorm; s2 *= recipNorm; s3 *= recipNorm;

        qDot1 -= x->beta * s0;
        qDot2 -= x->beta * s1;
        qDot3 -= x->beta * s2;
        qDot4 -= x->beta * s3;
    }

    q0 += qDot1 * dt; q1 += qDot2 * dt;
    q2 += qDot3 * dt; q3 += qDot4 * dt;

    recipNorm = inv_sqrt(q0*q0 + q1*q1 + q2*q2 + q3*q3);
    x->q0 = q0 * recipNorm; x->q1 = q1 * recipNorm;
    x->q2 = q2 * recipNorm; x->q3 = q3 * recipNorm;
}

/* ── Algoritmo 9-DOF (com magnetômetro) ─────────────────────────────────────
 * Portado de Madgwick::MadgwickUpdate(gx,gy,gz,ax,ay,az,mx,my,mz,deltat)
 */
static void madgwick_update_9(t_giromin_fusao *x,
                               float gx, float gy, float gz,
                               float ax, float ay, float az,
                               float mx, float my, float mz,
                               float dt)
{
    float recipNorm;
    float s0, s1, s2, s3;
    float qDot1, qDot2, qDot3, qDot4;
    float hx, hy;
    float _2q0mx, _2q0my, _2q0mz, _2q1mx, _2bx, _2bz, _4bx, _4bz;
    float _2q0, _2q1, _2q2, _2q3, _2q0q2, _2q2q3;
    float q0q0, q0q1, q0q2, q0q3, q1q1, q1q2, q1q3, q2q2, q2q3, q3q3;

    /* Se sem magnetômetro, usa 6-DOF */
    if ((mx == 0.0f) && (my == 0.0f) && (mz == 0.0f)) {
        madgwick_update_6(x, gx, gy, gz, ax, ay, az, dt);
        return;
    }

    gx *= 0.0174533f; gy *= 0.0174533f; gz *= 0.0174533f;

    float q0 = x->q0, q1 = x->q1, q2 = x->q2, q3 = x->q3;

    qDot1 = 0.5f * (-q1*gx - q2*gy - q3*gz);
    qDot2 = 0.5f * ( q0*gx + q2*gz - q3*gy);
    qDot3 = 0.5f * ( q0*gy - q1*gz + q3*gx);
    qDot4 = 0.5f * ( q0*gz + q1*gy - q2*gx);

    if (!((ax == 0.0f) && (ay == 0.0f) && (az == 0.0f))) {
        recipNorm = inv_sqrt(ax*ax + ay*ay + az*az);
        ax *= recipNorm; ay *= recipNorm; az *= recipNorm;

        recipNorm = inv_sqrt(mx*mx + my*my + mz*mz);
        mx *= recipNorm; my *= recipNorm; mz *= recipNorm;

        _2q0mx = 2.0f*q0*mx; _2q0my = 2.0f*q0*my; _2q0mz = 2.0f*q0*mz;
        _2q1mx = 2.0f*q1*mx;
        _2q0 = 2.0f*q0; _2q1 = 2.0f*q1; _2q2 = 2.0f*q2; _2q3 = 2.0f*q3;
        _2q0q2 = 2.0f*q0*q2; _2q2q3 = 2.0f*q2*q3;
        q0q0 = q0*q0; q0q1 = q0*q1; q0q2 = q0*q2; q0q3 = q0*q3;
        q1q1 = q1*q1; q1q2 = q1*q2; q1q3 = q1*q3;
        q2q2 = q2*q2; q2q3 = q2*q3; q3q3 = q3*q3;

        hx = mx*q0q0 - _2q0my*q3 + _2q0mz*q2 + mx*q1q1
             + _2q1*my*q2 + _2q1*mz*q3 - mx*q2q2 - mx*q3q3;
        hy = _2q0mx*q3 + my*q0q0 - _2q0mz*q1 + _2q1mx*q2
             - my*q1q1 + my*q2q2 + _2q2*mz*q3 - my*q3q3;
        _2bx = sqrtf(hx*hx + hy*hy);
        _2bz = -_2q0mx*q2 + _2q0my*q1 + mz*q0q0 + _2q1mx*q3
               - mz*q1q1 + _2q2*my*q3 - mz*q2q2 + mz*q3q3;
        _4bx = 2.0f*_2bx; _4bz = 2.0f*_2bz;

        s0 = -_2q2*(2.0f*q1q3 - _2q0q2 - ax) + _2q1*(2.0f*q0q1 + _2q2q3 - ay)
             - _2bz*q2*(_2bx*(0.5f - q2q2 - q3q3) + _2bz*(q1q3 - q0q2) - mx)
             + (-_2bx*q3 + _2bz*q1)*(_2bx*(q1q2 - q0q3) + _2bz*(q0q1 + q2q3) - my)
             + _2bx*q2*(_2bx*(q0q2 + q1q3) + _2bz*(0.5f - q1q1 - q2q2) - mz);
        s1 =  _2q3*(2.0f*q1q3 - _2q0q2 - ax) + _2q0*(2.0f*q0q1 + _2q2q3 - ay)
             - 4.0f*q1*(1.0f - 2.0f*q1q1 - 2.0f*q2q2 - az)
             + _2bz*q3*(_2bx*(0.5f - q2q2 - q3q3) + _2bz*(q1q3 - q0q2) - mx)
             + (_2bx*q2 + _2bz*q0)*(_2bx*(q1q2 - q0q3) + _2bz*(q0q1 + q2q3) - my)
             + (_2bx*q3 - _4bz*q1)*(_2bx*(q0q2 + q1q3) + _2bz*(0.5f - q1q1 - q2q2) - mz);
        s2 = -_2q0*(2.0f*q1q3 - _2q0q2 - ax) + _2q3*(2.0f*q0q1 + _2q2q3 - ay)
             - 4.0f*q2*(1.0f - 2.0f*q1q1 - 2.0f*q2q2 - az)
             + (-_4bx*q2 - _2bz*q0)*(_2bx*(0.5f - q2q2 - q3q3) + _2bz*(q1q3 - q0q2) - mx)
             + (_2bx*q1 + _2bz*q3)*(_2bx*(q1q2 - q0q3) + _2bz*(q0q1 + q2q3) - my)
             + (_2bx*q0 - _4bz*q2)*(_2bx*(q0q2 + q1q3) + _2bz*(0.5f - q1q1 - q2q2) - mz);
        s3 =  _2q1*(2.0f*q1q3 - _2q0q2 - ax) + _2q2*(2.0f*q0q1 + _2q2q3 - ay)
             + (-_4bx*q3 + _2bz*q1)*(_2bx*(0.5f - q2q2 - q3q3) + _2bz*(q1q3 - q0q2) - mx)
             + (-_2bx*q0 + _2bz*q2)*(_2bx*(q1q2 - q0q3) + _2bz*(q0q1 + q2q3) - my)
             + _2bx*q1*(_2bx*(q0q2 + q1q3) + _2bz*(0.5f - q1q1 - q2q2) - mz);

        recipNorm = inv_sqrt(s0*s0 + s1*s1 + s2*s2 + s3*s3);
        s0 *= recipNorm; s1 *= recipNorm; s2 *= recipNorm; s3 *= recipNorm;

        qDot1 -= x->beta * s0;
        qDot2 -= x->beta * s1;
        qDot3 -= x->beta * s2;
        qDot4 -= x->beta * s3;
    }

    q0 += qDot1 * dt; q1 += qDot2 * dt;
    q2 += qDot3 * dt; q3 += qDot4 * dt;

    recipNorm = inv_sqrt(q0*q0 + q1*q1 + q2*q2 + q3*q3);
    x->q0 = q0 * recipNorm; x->q1 = q1 * recipNorm;
    x->q2 = q2 * recipNorm; x->q3 = q3 * recipNorm;
}

/* ── Emite quaternion em todos os outlets ── */
static void giromin_fusao_output(t_giromin_fusao *x)
{
    /* outlets 1-4 da direita para esquerda (convenção PD) */
    outlet_float(x->out_z, (t_float)x->q3);
    outlet_float(x->out_y, (t_float)x->q2);
    outlet_float(x->out_x, (t_float)x->q1);
    outlet_float(x->out_w, (t_float)x->q0);

    /* outlet 0: lista w x y z */
    t_atom atoms[4];
    SETFLOAT(&atoms[0], (t_float)x->q0);
    SETFLOAT(&atoms[1], (t_float)x->q1);
    SETFLOAT(&atoms[2], (t_float)x->q2);
    SETFLOAT(&atoms[3], (t_float)x->q3);
    outlet_list(x->out_list, &s_list, 4, atoms);
}

/* ── Handler: lista de 6 ou 9 floats ── */
static void giromin_fusao_list(t_giromin_fusao *x, t_symbol * /*s*/,
                                   int argc, t_atom *argv)
{
    if (argc < 6) {
        pd_error(x, "giromin.fusao: precisa de 6 floats (gx gy gz ax ay az)"
                    " ou 9 floats (gx gy gz ax ay az mx my mz)");
        return;
    }

    float gx = atom_getfloat(argv + 0);
    float gy = atom_getfloat(argv + 1);
    float gz = atom_getfloat(argv + 2);
    float ax = atom_getfloat(argv + 3);
    float ay = atom_getfloat(argv + 4);
    float az = atom_getfloat(argv + 5);

    /* dt em segundos usando relógio real do sistema */
#ifdef _WIN32
    static LARGE_INTEGER freq = {0};
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER cnt;
    QueryPerformanceCounter(&cnt);
    double now = (double)cnt.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double now = ts.tv_sec + ts.tv_nsec * 1e-9;
#endif
    float dt;
    if (x->last_time == 0.0) {
        dt = 0.01f;
    } else {
        dt = (float)(now - x->last_time);
        if (dt <= 0.0f || dt > 1.0f) dt = 0.01f;
    }
    x->last_time = now;

    if (argc >= 9) {
        float mx = atom_getfloat(argv + 6);
        float my = atom_getfloat(argv + 7);
        float mz = atom_getfloat(argv + 8);
        madgwick_update_9(x, gx, gy, gz, ax, ay, az, mx, my, mz, dt);
    } else {
        madgwick_update_6(x, gx, gy, gz, ax, ay, az, dt);
    }

    giromin_fusao_output(x);
}

/* ── Mensagem: reset ── */
static void giromin_fusao_reset(t_giromin_fusao *x)
{
    x->q0 = 1.0f; x->q1 = 0.0f; x->q2 = 0.0f; x->q3 = 0.0f;
    x->last_time = 0.0;
    post("giromin.fusao: reset");
}

/* ── Mensagem: beta FLOAT ── */
static void giromin_fusao_beta(t_giromin_fusao *x, t_floatarg f)
{
    x->beta = f;
}

/* ── Construtor ── */
static void *giromin_fusao_new(t_floatarg beta_arg)
{
    t_giromin_fusao *x = (t_giromin_fusao *)pd_new(giromin_fusao_class);

    x->beta      = (beta_arg != 0.0f) ? beta_arg : 0.3f;
    x->q0        = 1.0f;
    x->q1 = x->q2 = x->q3 = 0.0f;
    x->last_time = 0.0;

    x->out_list = outlet_new(&x->x_obj, &s_list);
    x->out_w    = outlet_new(&x->x_obj, &s_float);
    x->out_x    = outlet_new(&x->x_obj, &s_float);
    x->out_y    = outlet_new(&x->x_obj, &s_float);
    x->out_z    = outlet_new(&x->x_obj, &s_float);

    return (void *)x;
}

/* ── Setup ── */
extern "C" void giromin_fusao_setup(void)
{
    giromin_fusao_class = class_new(
        gensym("giromin.fusao"),
        (t_newmethod)giromin_fusao_new,
        0,
        sizeof(t_giromin_fusao),
        CLASS_DEFAULT,
        A_DEFFLOAT,   /* argumento opcional: beta */
        (t_atomtype)0
    );

    class_addlist(giromin_fusao_class,
                  (t_method)giromin_fusao_list);

    class_addmethod(giromin_fusao_class,
                    (t_method)giromin_fusao_reset,
                    gensym("reset"), A_NULL);

    class_addmethod(giromin_fusao_class,
                    (t_method)giromin_fusao_beta,
                    gensym("beta"), (t_atomtype)A_FLOAT, (t_atomtype)0);

    gm_class_desc(giromin_fusao_class, "Fusao de sensores IMU pelo algoritmo Madgwick — combina giroscopio e acelerometro (e magnetometro opcional) em quaternion");
    gm_inlet_desc(giromin_fusao_class, 0, "lista de 6 floats: gx gy gz ax ay az (graus/s e g) | ou 9 floats incluindo magnetometro mx my mz");
    gm_outlet_desc(giromin_fusao_class, 0, "lista w x y z — quaternion de orientacao");
    gm_outlet_desc(giromin_fusao_class, 1, "w — componente escalar do quaternion");
    gm_outlet_desc(giromin_fusao_class, 2, "x — componente x do quaternion");
    gm_outlet_desc(giromin_fusao_class, 3, "y — componente y do quaternion");
    gm_outlet_desc(giromin_fusao_class, 4, "z — componente z do quaternion");
    post("giromin.fusao: fusao de sensores Madgwick carregado");
}

extern "C" void setup_giromin0x2efusao(void) { giromin_fusao_setup(); }
