#ifndef NIBBLE_PRINT_IR_H
#define NIBBLE_PRINT_IR_H
#include "bytecode.h"

char* IR_print_instr(Allocator* arena, IR_Instr* instr);
void IR_print_out_proc(Allocator* arena, Symbol* sym);
#endif