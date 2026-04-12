#pragma once
/**
 * giromin_plugdata.h
 *
 * Wrappers para a API de descrições do PlugData.
 * Em vanilla PD essas funções não existem — o __attribute__((weak)) faz
 * com que o símbolo seja NULL se não encontrado, e o wrapper verifica isso
 * antes de chamar. O objeto compila e carrega normalmente nos dois casos.
 */

extern "C" {
#include "m_pd.h"
}

#if defined(__GNUC__) || defined(__clang__)
extern "C" void class_set_description(t_class *c, t_symbol *s)
    __attribute__((weak));
extern "C" void class_set_inlet_description(t_class *c, int n, t_symbol *s)
    __attribute__((weak));
extern "C" void class_set_outlet_description(t_class *c, int n, t_symbol *s)
    __attribute__((weak));
#endif

static inline void gm_class_desc(t_class *c, const char *s) {
#if defined(__GNUC__) || defined(__clang__)
    if (class_set_description) class_set_description(c, gensym(s));
#endif
}

static inline void gm_inlet_desc(t_class *c, int n, const char *s) {
#if defined(__GNUC__) || defined(__clang__)
    if (class_set_inlet_description) class_set_inlet_description(c, n, gensym(s));
#endif
}

static inline void gm_outlet_desc(t_class *c, int n, const char *s) {
#if defined(__GNUC__) || defined(__clang__)
    if (class_set_outlet_description) class_set_outlet_description(c, n, gensym(s));
#endif
}
