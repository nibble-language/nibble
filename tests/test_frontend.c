//#define NDEBUG 1
#define PRINT_MEM_USAGE 0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "allocator.c"
#include "array.c"
#include "ast.c"
#include "cstring.c"
#include "hash_map.c"
#include "lexer.c"
#include "llist.h"
#include "nibble.c"
#include "parser.c"
#include "print.c"
#include "stream.c"

static void print_errors(ByteStream* errors)
{
    if (errors)
    {
        ByteStreamChunk* chunk = errors->first;

        while (chunk)
        {
            ftprint_out("[ERROR]: %s\n", chunk->buf);
            chunk = chunk->next;
        }
    }
}

#define TKN_TEST_POS(tk, tp, a, b)                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        assert((tk.kind == tp));                                                                                       \
        assert((tk.range.start == a));                                                                                 \
        assert((tk.range.end == b));                                                                                   \
    } while (0)
#define TKN_TEST_INT(tk, b, v)                                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        assert((tk.kind == TKN_INT));                                                                                  \
        assert((tk.as_int.rep == b));                                                                                  \
        assert((tk.as_int.value == v));                                                                                \
    } while (0)
#define TKN_TEST_FLOAT(tk, v)                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        assert((tk.kind == TKN_FLOAT));                                                                                \
        assert((tk.as_float.value == v));                                                                              \
    } while (0)
#define TKN_TEST_CHAR(tk, v)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        assert((tk.kind == TKN_INT));                                                                                  \
        assert((tk.as_int.rep == TKN_INT_CHAR));                                                                       \
        assert((tk.as_int.value == v));                                                                                \
    } while (0)
#define TKN_TEST_STR(tk, v)                                                                                            \
    do                                                                                                                 \
    {                                                                                                                  \
        assert((tk.kind == TKN_STR));                                                                                  \
        assert(strcmp(tk.as_str.value, v) == 0);                                                                       \
    } while (0)
#define TKN_TEST_IDEN(tk, v)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        assert((tk.kind == TKN_IDENT));                                                                                \
        assert(strcmp(tk.as_ident.value, v) == 0);                                                                     \
    } while (0)
#define TKN_TEST_KW(tk, k)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        assert((tk.kind == TKN_KW));                                                                                   \
        assert(tk.as_kw.kw == (k));                                                                                    \
    } while (0)

static void test_lexer(void)
{
    Allocator allocator = allocator_create(4096);

    // Test basic tokens, newlines, and c++ comments.
    {
        unsigned int i = 10;
        Token token = {0};
        Lexer lexer = lexer_create(
            "(+[]-*%&|^<>#) && || => :> >> << == >= <= = += -= *= /= &= |= ^= %= \n  //++--\n{;:,./}", i, NULL);

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_LPAREN, i, ++i);

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_PLUS, i, ++i);

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_LBRACKET, i, ++i);

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_RBRACKET, i, ++i);

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_MINUS, i, ++i);

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_ASTERISK, i, ++i);

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_MOD, i, ++i);

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_AND, i, ++i);

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_OR, i, ++i);

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_CARET, i, ++i);

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_LT, i, ++i);

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_GT, i, ++i);

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_POUND, i, ++i);

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_RPAREN, i, ++i);

        i++;
        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_LOGIC_AND, i, i + 2);
        i += 3;

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_LOGIC_OR, i, i + 2);
        i += 3;

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_ARROW, i, i + 2);
        i += 3;

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_CAST, i, i + 2);
        i += 3;

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_RSHIFT, i, i + 2);
        i += 3;

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_LSHIFT, i, i + 2);
        i += 3;

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_EQ, i, i + 2);
        i += 3;

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_GTEQ, i, i + 2);
        i += 3;

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_LTEQ, i, i + 2);
        i += 3;

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_ASSIGN, i, i + 1);
        i += 2;

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_ADD_ASSIGN, i, i + 2);
        i += 3;

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_SUB_ASSIGN, i, i + 2);
        i += 3;

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_MUL_ASSIGN, i, i + 2);
        i += 3;

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_DIV_ASSIGN, i, i + 2);
        i += 3;

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_AND_ASSIGN, i, i + 2);
        i += 3;

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_OR_ASSIGN, i, i + 2);
        i += 3;

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_XOR_ASSIGN, i, i + 2);
        i += 3;

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_MOD_ASSIGN, i, i + 2);
        i += 3;

        i += 10;
        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_LBRACE, i, ++i);

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_SEMICOLON, i, ++i);

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_COLON, i, ++i);

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_COMMA, i, ++i);

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_DOT, i, ++i);

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_DIV, i, ++i);

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_RBRACE, i, ++i);

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_EOF, i, i);

        lexer_destroy(&lexer);
    }

    // Test nested c-style comments
    {
        Lexer lexer = lexer_create("/**** 1 /* 2 */ \n***/+-", 0, NULL);
        Token token = {0};

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_PLUS, 21, 22);

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_MINUS, 22, 23);

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_EOF, 23, 23);

        lexer_destroy(&lexer);
    }

    // Test error when have unclosed c-style comments
    {
        ByteStream errors = byte_stream_create(&allocator);
        Lexer lexer = lexer_create("/* An unclosed comment", 0, &errors);
        Token token = {0};

        token = scan_token(&lexer);
        TKN_TEST_POS(token, TKN_EOF, 22, 22);
        assert(errors.num_chunks == 1);

        print_errors(&errors);

        lexer_destroy(&lexer);
    }

    {
        ByteStream errors = byte_stream_create(&allocator);
        Lexer lexer = lexer_create("/* An unclosed comment\n", 0, &errors);
        Token token = {0};

        token = scan_token(&lexer);
        size_t len = cstr_len(lexer.str);
        TKN_TEST_POS(token, TKN_EOF, len, len);
        assert(errors.num_chunks == 1);

        print_errors(&errors);
        lexer_destroy(&lexer);
    }

    // Test integer literals
    {
        Lexer lexer = lexer_create("123 333\n0xFF 0b0111 011 0", 0, NULL);
        Token token = {0};

        token = scan_token(&lexer);
        TKN_TEST_INT(token, TKN_INT_DEC, 123);

        token = scan_token(&lexer);
        TKN_TEST_INT(token, TKN_INT_DEC, 333);

        token = scan_token(&lexer);
        TKN_TEST_INT(token, TKN_INT_HEX, 0xFF);

        token = scan_token(&lexer);
        TKN_TEST_INT(token, TKN_INT_BIN, 7);

        token = scan_token(&lexer);
        TKN_TEST_INT(token, TKN_INT_OCT, 9);

        token = scan_token(&lexer);
        TKN_TEST_INT(token, TKN_INT_DEC, 0);

        token = scan_token(&lexer);
        assert(token.kind == TKN_EOF);

        lexer_destroy(&lexer);
    }

    // Test integer literal lexing errors
    {
        ByteStream errors = byte_stream_create(&allocator);
        Lexer lexer = lexer_create("0Z 0b3 09 1A\n999999999999999999999999", 0, &errors);
        Token token = {0};

        token = scan_token(&lexer);
        assert(errors.num_chunks == 1);

        token = scan_token(&lexer);
        assert(errors.num_chunks == 2);

        token = scan_token(&lexer);
        assert(errors.num_chunks == 3);

        token = scan_token(&lexer);
        assert(errors.num_chunks == 4);

        token = scan_token(&lexer);
        assert(errors.num_chunks == 5);

        token = scan_token(&lexer);
        assert(token.kind == TKN_EOF);

        print_errors(&errors);
        lexer_destroy(&lexer);
    }

    // Test floating point literals
    {
        Lexer lexer = lexer_create("1.23 .23 1.33E2", 0, NULL);
        Token token = {0};

        token = scan_token(&lexer);
        TKN_TEST_FLOAT(token, 1.23);

        token = scan_token(&lexer);
        TKN_TEST_FLOAT(token, .23);

        token = scan_token(&lexer);
        TKN_TEST_FLOAT(token, 1.33E2);

        lexer_destroy(&lexer);
    }

    {
        ByteStream errors = byte_stream_create(&allocator);
        Lexer lexer = lexer_create("1.33ea 1.33e100000000000", 0, &errors);

        scan_token(&lexer);
        assert(errors.num_chunks == 1);

        scan_token(&lexer);
        assert(errors.num_chunks == 2);

        print_errors(&errors);
        lexer_destroy(&lexer);
    }

    // Test character literals
    {
        ByteStream errors = byte_stream_create(&allocator);
        Lexer lexer = lexer_create("'a' '1' ' ' '\\0' '\\a' '\\b' '\\f' '\\n' '\\r' '\\t' '\\v' "
                                   "'\\\\' '\\'' '\\\"' '\\?'",
                                   0, &errors);
        Token token = {0};

        token = scan_token(&lexer);
        TKN_TEST_CHAR(token, 'a');

        token = scan_token(&lexer);
        TKN_TEST_CHAR(token, '1');

        token = scan_token(&lexer);
        TKN_TEST_CHAR(token, ' ');

        token = scan_token(&lexer);
        TKN_TEST_CHAR(token, '\0');

        token = scan_token(&lexer);
        TKN_TEST_CHAR(token, '\a');

        token = scan_token(&lexer);
        TKN_TEST_CHAR(token, '\b');

        token = scan_token(&lexer);
        TKN_TEST_CHAR(token, '\f');

        token = scan_token(&lexer);
        TKN_TEST_CHAR(token, '\n');

        token = scan_token(&lexer);
        TKN_TEST_CHAR(token, '\r');

        token = scan_token(&lexer);
        TKN_TEST_CHAR(token, '\t');

        token = scan_token(&lexer);
        TKN_TEST_CHAR(token, '\v');

        token = scan_token(&lexer);
        TKN_TEST_CHAR(token, '\\');

        token = scan_token(&lexer);
        TKN_TEST_CHAR(token, '\'');

        token = scan_token(&lexer);
        TKN_TEST_CHAR(token, '"');

        token = scan_token(&lexer);
        TKN_TEST_CHAR(token, '?');

        token = scan_token(&lexer);
        assert(token.kind == TKN_EOF);
        assert(errors.num_chunks == 0);

        lexer_destroy(&lexer);
    }

    // Test escaped hex chars
    {
        ByteStream errors = byte_stream_create(&allocator);
        Lexer lexer = lexer_create("'\\x12'  '\\x3'", 0, &errors);
        Token token = {0};

        token = scan_token(&lexer);
        TKN_TEST_CHAR(token, '\x12');

        token = scan_token(&lexer);
        TKN_TEST_CHAR(token, '\x3');

        token = scan_token(&lexer);
        assert(token.kind == TKN_EOF);
        assert(errors.num_chunks == 0);

        lexer_destroy(&lexer);
    }

    // Test errors when lexing escaped hex chars
    {
        ByteStream errors = byte_stream_create(&allocator);
        Lexer lexer = lexer_create("'' 'a '\n' '\\z' '\\0'", 0, &errors);
        Token token = {0};

        token = scan_token(&lexer);
        assert(errors.num_chunks == 1);

        token = scan_token(&lexer);
        assert(errors.num_chunks == 2);

        token = scan_token(&lexer);
        assert(errors.num_chunks == 3);

        token = scan_token(&lexer);
        assert(errors.num_chunks == 4);

        token = scan_token(&lexer);
        TKN_TEST_CHAR(token, '\0');

        token = scan_token(&lexer);
        assert(token.kind == TKN_EOF);

        print_errors(&errors);
        lexer_destroy(&lexer);
    }

    // Test basic string literals
    {
        ByteStream errors = byte_stream_create(&allocator);
        const char* str = "\"hello world\" \"a\\nb\" \n \"\\x50 a \\x51\" \"\" \"\\\"nested\\\"\"";
        Lexer lexer = lexer_create(str, 0, &errors);
        Token token = {0};

        token = scan_token(&lexer);
        TKN_TEST_STR(token, "hello world");

        token = scan_token(&lexer);
        TKN_TEST_STR(token, "a\nb");

        token = scan_token(&lexer);
        TKN_TEST_STR(token, "\x50 a \x51");

        token = scan_token(&lexer);
        TKN_TEST_STR(token, "");

        token = scan_token(&lexer);
        TKN_TEST_STR(token, "\"nested\"");

        token = scan_token(&lexer);
        assert(token.kind == TKN_EOF);
        assert(errors.num_chunks == 0);

        lexer_destroy(&lexer);
    }

    // Test errors when scanning string literals
    {
        ByteStream errors = byte_stream_create(&allocator);
        const char* str = "\"\n\" \"\\xTF\" \"\\W\" \"unclosed";
        Lexer lexer = lexer_create(str, 0, &errors);

        scan_token(&lexer);
        assert(errors.num_chunks == 1);

        scan_token(&lexer);
        assert(errors.num_chunks == 2);

        scan_token(&lexer);
        assert(errors.num_chunks == 3);

        scan_token(&lexer);
        assert(errors.num_chunks == 4);

        print_errors(&errors);
        lexer_destroy(&lexer);
    }

    // Test basic identifiers
    {
        ByteStream errors = byte_stream_create(&allocator);
        const char* str = "vars x1a x11 _abc abc_ _ab_c_ i";
        Lexer lexer = lexer_create(str, 0, &errors);
        Token token = {0};

        token = scan_token(&lexer);
        TKN_TEST_IDEN(token, "vars");

        token = scan_token(&lexer);
        TKN_TEST_IDEN(token, "x1a");

        token = scan_token(&lexer);
        TKN_TEST_IDEN(token, "x11");

        token = scan_token(&lexer);
        TKN_TEST_IDEN(token, "_abc");

        token = scan_token(&lexer);
        TKN_TEST_IDEN(token, "abc_");

        token = scan_token(&lexer);
        TKN_TEST_IDEN(token, "_ab_c_");

        token = scan_token(&lexer);
        TKN_TEST_IDEN(token, "i");

        token = scan_token(&lexer);
        assert(token.kind == TKN_EOF);
        assert(errors.num_chunks == 0);

        lexer_destroy(&lexer);
    }

    // Test invalid identifier combinations.
    {
        ByteStream errors = byte_stream_create(&allocator);
        const char* str = "1var";
        Lexer lexer = lexer_create(str, 0, &errors);
        Token token = {0};

        token = scan_token(&lexer);
        assert(errors.num_chunks == 1);

        token = scan_token(&lexer);
        assert(token.kind == TKN_EOF);

        print_errors(&errors);
        lexer_destroy(&lexer);
    }

    // Test keywords
    {
        ByteStream errors = byte_stream_create(&allocator);
        char* str = array_create(&allocator, char, 256);

        for (int i = 0; i < KW_COUNT; i += 1)
        {
            ftprint_char_array(&str, false, "%s ", keyword_names[i].str);
        }

        array_push(str, '\0');

        Lexer lexer = lexer_create(str, 0, &errors);
        Token token = {0};

        for (int i = 0; i < KW_COUNT; i += 1)
        {
            token = scan_token(&lexer);
            TKN_TEST_KW(token, (Keyword)i);
        }

        token = scan_token(&lexer);
        assert(token.kind == TKN_EOF);
        assert(errors.num_chunks == 0);

        lexer_destroy(&lexer);
    }

    allocator_destroy(&allocator);
}

static bool test_parse_typespec(Allocator* gen_arena, Allocator* ast_arena, const char* code, const char* sexpr)
{
    ByteStream err_stream = byte_stream_create(gen_arena);
    Parser parser = parser_create(ast_arena, code, 0, &err_stream);

    next_token(&parser);

    TypeSpec* type = parse_typespec(&parser);
    char* s = ftprint_typespec(gen_arena, type);
    AllocatorStats stats = allocator_stats(ast_arena);

    ftprint_out("%s\n\t%s\n\tAST mem size: %lu bytes\n", code, s, stats.used);

    parser_destroy(&parser);
    allocator_reset(ast_arena);
    allocator_reset(gen_arena);

    return cstr_cmp(s, sexpr) == 0;
}

static bool test_parse_expr(Allocator* gen_arena, Allocator* ast_arena, const char* code, const char* sexpr)
{
    ByteStream err_stream = byte_stream_create(gen_arena);
    Parser parser = parser_create(ast_arena, code, 0, &err_stream);

    next_token(&parser);

    Expr* expr = parse_expr(&parser);
    char* s = ftprint_expr(gen_arena, expr);
    AllocatorStats stats = allocator_stats(ast_arena);

    ftprint_out("%s\n\t%s\n\tAST mem size: %lu bytes\n", code, s, stats.used);

    parser_destroy(&parser);
    allocator_reset(ast_arena);
    allocator_reset(gen_arena);

    return cstr_cmp(s, sexpr) == 0;
}

#define TEST_TYPESPEC(c, s) assert(test_parse_typespec(&gen_arena, &ast_arena, (c), (s)))
#define TEST_EXPR(c, s) assert(test_parse_expr(&gen_arena, &ast_arena, (c), (s)))
void test_parser(void)
{
    Allocator ast_arena = allocator_create(4096);
    Allocator gen_arena = allocator_create(4096);

    // Test base typespecs
    TEST_TYPESPEC("int32", "(:ident int32)");
    TEST_TYPESPEC("(int32)", "(:ident int32)");
    TEST_TYPESPEC("proc(int32, b:float32) => float32",
                  "(:proc =>(:ident float32) (:ident int32) (b (:ident float32)))");
    TEST_TYPESPEC("proc(int32, b:float32)", "(:proc => (:ident int32) (b (:ident float32)))");
    TEST_TYPESPEC("struct {a:int32; b:float32;}", "(:struct (a (:ident int32)) (b (:ident float32)))");
    TEST_TYPESPEC("union {a:int32; b:float32;}", "(:union (a (:ident int32)) (b (:ident float32)))");

    // Test pointer to base types
    TEST_TYPESPEC("^int32", "(:ptr (:ident int32))");
    TEST_TYPESPEC("^(int32)", "(:ptr (:ident int32))");
    TEST_TYPESPEC("^proc(int32, b:float32) => float32",
                  "(:ptr (:proc =>(:ident float32) (:ident int32) (b (:ident float32))))");
    TEST_TYPESPEC("^struct {a:int32; b:float32;}", "(:ptr (:struct (a (:ident int32)) (b (:ident float32))))");
    TEST_TYPESPEC("^union {a:int32; b:float32;}", "(:ptr (:union (a (:ident int32)) (b (:ident float32))))");

    // Test array of base types
    TEST_TYPESPEC("[3]int32", "(:arr 3 (:ident int32))");
    TEST_TYPESPEC("[](int32)", "(:arr (:ident int32))");
    TEST_TYPESPEC("[3]proc(int32, b:float32) => float32",
                  "(:arr 3 (:proc =>(:ident float32) (:ident int32) (b (:ident float32))))");
    TEST_TYPESPEC("[3]struct {a:int32; b:float32;}", "(:arr 3 (:struct (a (:ident int32)) (b (:ident float32))))");
    TEST_TYPESPEC("[3]union {a:int32; b:float32;}", "(:arr 3 (:union (a (:ident int32)) (b (:ident float32))))");

    // Test const base types
    TEST_TYPESPEC("const int32", "(:const (:ident int32))");
    TEST_TYPESPEC("const (int32)", "(:const (:ident int32))");
    TEST_TYPESPEC("const proc(int32, b:float32) => float32",
                  "(:const (:proc =>(:ident float32) (:ident int32) (b (:ident float32))))");
    TEST_TYPESPEC("const struct {a:int32; b:float32;}", "(:const (:struct (a (:ident int32)) (b (:ident float32))))");
    TEST_TYPESPEC("const union {a:int32; b:float32;}", "(:const (:union (a (:ident int32)) (b (:ident float32))))");

    // Test mix of nested type modifiers
    TEST_TYPESPEC("[3]^ const char", "(:arr 3 (:ptr (:const (:ident char))))");

    // Test base expressions
    TEST_EXPR("1", "1");
    TEST_EXPR("3.14", "3.140000");
    TEST_EXPR("\"hi\"", "\"hi\"");
    TEST_EXPR("x", "x");
    TEST_EXPR("(x)", "x");
    TEST_EXPR("{0, 1}", "(compound {0 1})");
    TEST_EXPR("{0, 1 :Vec2i}", "(compound (:ident Vec2i) {0 1})");
    TEST_EXPR("{[0] = 0, [1] = 1}", "(compound {[0] = 0 [1] = 1})");
    TEST_EXPR("{x = 0, y = 1}", "(compound {x = 0 y = 1})");
    TEST_EXPR("sizeof(int32)", "(sizeof (:ident int32))");
    TEST_EXPR("typeof(x)", "(typeof x)");
    TEST_EXPR("(typeof(x))", "(typeof x)");

    // Test expression modifiers
    TEST_EXPR("x.y", "(field x y)");
    TEST_EXPR("x.y.z", "(field (field x y) z)");
    TEST_EXPR("x[0]", "(index x 0)");
    TEST_EXPR("x[0][1]", "(index (index x 0) 1)");
    TEST_EXPR("f(1)", "(call f 1)");
    TEST_EXPR("f(1, y=2)", "(call f 1 y=2)");
    TEST_EXPR("-x:>int", "(- (cast (:ident int) x))");
    TEST_EXPR("(-x):>int", "(cast (:ident int) (- x))");

    // Test unary + base expressions
    TEST_EXPR("-+~!p.a", "(- (+ (~ (! (field p a)))))");
    TEST_EXPR("*^p.a", "(* (^ (field p a)))");

    // Test expressions with multiplicative and unary precedence
    TEST_EXPR("a * *ptr / -b", "(/ (* a (* ptr)) (- b))");
    TEST_EXPR("a % b & !c", "(& (% a b) (! c))");
    TEST_EXPR("a << *p >> c", "(>> (<< a (* p)) c)");

    // Test expressions with additive and multiplicative precedence
    TEST_EXPR("a * b + c / d - e % f | g & h ^ x & y",
              "(^ (| (- (+ (* a b) (/ c d)) (% e f)) (& g h)) (& x y))");

    // Test expressions with comparative and additive precedence
    TEST_EXPR("a + b == -c - d", "(== (+ a b) (- (- c) d))");
    TEST_EXPR("a + b != -c - d", "(!= (+ a b) (- (- c) d))");
    TEST_EXPR("a + b > -c - d", "(> (+ a b) (- (- c) d))");
    TEST_EXPR("a + b >= -c - d", "(>= (+ a b) (- (- c) d))");
    TEST_EXPR("a + b < -c - d", "(< (+ a b) (- (- c) d))");
    TEST_EXPR("a + b <= -c - d", "(<= (+ a b) (- (- c) d))");

    // Test expressions with logical "or", logical "and", and comparative precedence
    TEST_EXPR("a == b && c > d", "(&& (== a b) (> c d))");
    TEST_EXPR("a && b || c == d", "(|| (&& a b) (== c d))");

    // Large ternary expression
    TEST_EXPR("x > 3 ? -2*x : f(1,b=2) - (3.14 + y.val) / z[2]",
              "(? (> x 3) (* (- 2) x) (- (call f 1 b=2) (/ (+ 3.140000 (field y val)) (index z 2))))");

    // Other expressions to test for the hell of it
    TEST_EXPR("a ^ ^b", "(^ a (^ b))");
    TEST_EXPR("\"abc\"[0]", "(index \"abc\" 0)");
    TEST_EXPR("(a :> (int)) + 2", "(+ (cast (:ident int) a) 2)");
    TEST_EXPR("(a:>^int)", "(cast (:ptr (:ident int)) a)");
    TEST_EXPR("a:>^int", "(cast (:ptr (:ident int)) a)");

    allocator_destroy(&gen_arena);
    allocator_destroy(&ast_arena);
}

int main(void)
{
    ftprint_out("Nibble tests!\n");

    if (!nibble_init())
    {
        ftprint_err("Failed to initialize Nibble\n");
        exit(1);
    }

    test_lexer();
    test_parser();

    nibble_cleanup();
}