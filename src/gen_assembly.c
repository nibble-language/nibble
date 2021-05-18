#include "gen_assembly.h"

typedef struct Generator Generator;

struct Generator {
    FILE* out_fd;
    char* out_buf;
};

static Generator generator;

#define emit_data(f, ...) ftprint_char_array(&generator.out_buf, false, (f), ## __VA_ARGS__)

bool generate_program(Program* prog, const char* output_file)
{
    generator.out_buf = array_create(&prog->gen_mem, char, 512);
    generator.out_fd = fopen(output_file, "w");    

    if (!generator.out_fd)
    {
        ftprint_err("Failed to write output file `%s`\n", output_file);
        return false;
    }

    emit_data("# Generated by Nibble compiler\n");

    array_push(generator.out_buf, '\0');
    ftprint_file(generator.out_fd, false, "%s", generator.out_buf);

    return true;
}
