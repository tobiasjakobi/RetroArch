#ifndef RGLGEN_H__
#define RGLGEN_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rglgen_headers.h"

struct rglgen_sym_map;

typedef void (*rglgen_func_t)();
typedef rglgen_func_t (*rglgen_proc_address_t)(const char*);
void rglgen_resolve_symbols(rglgen_proc_address_t proc);
void rglgen_resolve_symbols_custom(rglgen_proc_address_t proc,
      const struct rglgen_sym_map *map);

#endif

