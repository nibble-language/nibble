#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.c"

#define TKN_TEST_POS(tk, tp, a, b)                                                                                     \
    do {                                                                                                               \
        assert((tk.type == tp));                                                                                       \
        assert((tk.pos.start == a));                                                                                   \
        assert((tk.pos.end == b));                                                                                     \
    } while (0)
#define TKN_TEST_INT(tk, b, v)                                                                                         \
    do {                                                                                                               \
        assert((tk.type == TKN_INT));                                                                                  \
        assert((tk.int_.base = b));                                                                                    \
        assert((tk.int_.value == v));                                                                                  \
    } while (0)
#define TKN_TEST_FLOAT(tk, v)                                                                                          \
    do {                                                                                                               \
        assert((tk.type == TKN_FLOAT));                                                                                \
        assert((tk.float_.value == v));                                                                                \
    } while (0)
#define TKN_TEST_CHAR(tk, v)                                                                                           \
    do {                                                                                                               \
        assert((tk.type == TKN_CHAR));                                                                                 \
        assert((tk.char_.value == v));                                                                                 \
    } while (0)

void test_lexer()
{
    Lexer lexer = {0};

    // Test basic 1 character tokens, newlines, and c++ comments.
    unsigned int i = 10;
    init_lexer(&lexer, "(+[]-)  \n  //++--\n{;:,./}", i);

    next_token(&lexer);
    TKN_TEST_POS(lexer.token, TKN_LPAREN, i, ++i);

    next_token(&lexer);
    TKN_TEST_POS(lexer.token, TKN_PLUS, i, ++i);

    next_token(&lexer);
    TKN_TEST_POS(lexer.token, TKN_LBRACE, i, ++i);

    next_token(&lexer);
    TKN_TEST_POS(lexer.token, TKN_RBRACE, i, ++i);

    next_token(&lexer);
    TKN_TEST_POS(lexer.token, TKN_MINUS, i, ++i);

    next_token(&lexer);
    TKN_TEST_POS(lexer.token, TKN_RPAREN, i, ++i);

    i += 12;
    next_token(&lexer);
    TKN_TEST_POS(lexer.token, TKN_LBRACKET, i, ++i);

    next_token(&lexer);
    TKN_TEST_POS(lexer.token, TKN_SEMICOLON, i, ++i);

    next_token(&lexer);
    TKN_TEST_POS(lexer.token, TKN_COLON, i, ++i);

    next_token(&lexer);
    TKN_TEST_POS(lexer.token, TKN_COMMA, i, ++i);

    next_token(&lexer);
    TKN_TEST_POS(lexer.token, TKN_DOT, i, ++i);

    next_token(&lexer);
    TKN_TEST_POS(lexer.token, TKN_DIV, i, ++i);

    next_token(&lexer);
    TKN_TEST_POS(lexer.token, TKN_RBRACKET, i, ++i);

    next_token(&lexer);
    TKN_TEST_POS(lexer.token, TKN_EOF, i, ++i);

    // Test nested c-style comments
    init_lexer(&lexer, "/**** 1 /* 2 */ \n***/+-", 0);

    next_token(&lexer);
    TKN_TEST_POS(lexer.token, TKN_PLUS, 21, 22);

    next_token(&lexer);
    TKN_TEST_POS(lexer.token, TKN_MINUS, 22, 23);

    next_token(&lexer);
    TKN_TEST_POS(lexer.token, TKN_EOF, 23, 24);

    // Test error when have unclosed c-style comments
    init_lexer(&lexer, "/* An unclosed comment", 0);

    next_token(&lexer);
    TKN_TEST_POS(lexer.token, TKN_EOF, 22, 23);
    assert(lexer.num_errors == 1);

    init_lexer(&lexer, "/* An unclosed comment\n", 0);

    next_token(&lexer);
    TKN_TEST_POS(lexer.token, TKN_EOF, strlen(lexer.str), strlen(lexer.str) + 1);
    assert(lexer.num_errors == 1);

    // Test integer literals
    init_lexer(&lexer, "123 333\n0xFF 0b0111 011", 0);

    next_token(&lexer);
    TKN_TEST_INT(lexer.token, 10, 123);

    next_token(&lexer);
    TKN_TEST_INT(lexer.token, 10, 333);

    next_token(&lexer);
    TKN_TEST_INT(lexer.token, 16, 0xFF);

    next_token(&lexer);
    TKN_TEST_INT(lexer.token, 2, 7);

    next_token(&lexer);
    TKN_TEST_INT(lexer.token, 8, 9);

    next_token(&lexer);
    assert(lexer.token.type == TKN_EOF);

    init_lexer(&lexer, "0Z 0b3 09 1A\n999999999999999999999999", 0);

    next_token(&lexer);
    assert(lexer.num_errors == 1);

    next_token(&lexer);
    assert(lexer.num_errors == 2);

    next_token(&lexer);
    assert(lexer.num_errors == 3);

    next_token(&lexer);
    assert(lexer.num_errors == 4);

    next_token(&lexer);
    assert(lexer.num_errors == 5);

    next_token(&lexer);
    assert(lexer.token.type == TKN_EOF);

    // Test floating point literals
    init_lexer(&lexer, "1.23 .23 1.33E2", 0);

    next_token(&lexer);
    TKN_TEST_FLOAT(lexer.token, 1.23);

    next_token(&lexer);
    TKN_TEST_FLOAT(lexer.token, .23);

    next_token(&lexer);
    TKN_TEST_FLOAT(lexer.token, 1.33E2);

    init_lexer(&lexer, "1.33ea 1.33e100000000000", 0);

    next_token(&lexer);
    assert(lexer.num_errors == 1);

    next_token(&lexer);
    assert(lexer.num_errors == 2);

    // Test character literals
    init_lexer(&lexer,
               "'a' '1' ' ' '\\0' '\\a' '\\b' '\\f' '\\n' '\\r' '\\t' '\\v' "
               "'\\\\' '\\'' '\\\"' '\\?'",
               0);

    next_token(&lexer);
    TKN_TEST_CHAR(lexer.token, 'a');

    next_token(&lexer);
    TKN_TEST_CHAR(lexer.token, '1');

    next_token(&lexer);
    TKN_TEST_CHAR(lexer.token, ' ');

    next_token(&lexer);
    TKN_TEST_CHAR(lexer.token, '\0');

    next_token(&lexer);
    TKN_TEST_CHAR(lexer.token, '\a');

    next_token(&lexer);
    TKN_TEST_CHAR(lexer.token, '\b');

    next_token(&lexer);
    TKN_TEST_CHAR(lexer.token, '\f');

    next_token(&lexer);
    TKN_TEST_CHAR(lexer.token, '\n');

    next_token(&lexer);
    TKN_TEST_CHAR(lexer.token, '\r');

    next_token(&lexer);
    TKN_TEST_CHAR(lexer.token, '\t');

    next_token(&lexer);
    TKN_TEST_CHAR(lexer.token, '\v');

    next_token(&lexer);
    TKN_TEST_CHAR(lexer.token, '\\');

    next_token(&lexer);
    TKN_TEST_CHAR(lexer.token, '\'');

    next_token(&lexer);
    TKN_TEST_CHAR(lexer.token, '"');

    next_token(&lexer);
    TKN_TEST_CHAR(lexer.token, '?');

    next_token(&lexer);
    assert(lexer.token.type == TKN_EOF);
    assert(lexer.num_errors == 0);

    init_lexer(&lexer, "'\\x12'  '\\x3'", 0);

    next_token(&lexer);
    TKN_TEST_CHAR(lexer.token, '\x12');

    next_token(&lexer);
    TKN_TEST_CHAR(lexer.token, '\x3');

    init_lexer(&lexer, "'' 'a '\n' '\\z' '\\0'", 0);

    next_token(&lexer);
    assert(lexer.num_errors == 1);

    next_token(&lexer);
    assert(lexer.num_errors == 2);

    next_token(&lexer);
    assert(lexer.num_errors == 3);

    next_token(&lexer);
    assert(lexer.num_errors == 4);

    next_token(&lexer);
    TKN_TEST_CHAR(lexer.token, '\0');

    next_token(&lexer);
    assert(lexer.token.type == TKN_EOF);
}

int main(void)
{
    printf("Nibble!\n");
    test_lexer();
}
