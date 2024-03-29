#ifndef NIBBLE_RESOLVER_H
#define NIBBLE_RESOLVER_H
#include "compiler.h"
#include "ast/module.h"

#define MAX_LOCAL_SYMS 512

typedef struct ModuleState {
    Module* mod;
    Symbol* proc;
    Scope* scope;
} ModuleState;

typedef struct Resolver {
    NibbleCtx* ctx;
    ModuleState state;
} Resolver;

bool resolve_module(Resolver* resolver, Module* mod);
bool resolve_reachable_sym_defs(Resolver* resolver);
#endif
