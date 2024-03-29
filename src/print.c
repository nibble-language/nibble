#include <assert.h>
#include <float.h>

#include "cstring.h"
#include "print_floats.h"


enum format_flags {
    FORMAT_FLAG_LEFT_JUSTIFIED = 1U << 0,
    FORMAT_FLAG_FORCE_SIGN = 1U << 1,
    FORMAT_FLAG_SPACE_SIGN = 1U << 2,
    FORMAT_FLAG_HASH = 1U << 3,
    FORMAT_FLAG_ZERO_PAD = 1U << 4,
    FORMAT_FLAG_WIDTH = 1U << 5,

    FORMAT_FLAG_PRECISION = 1U << 6,

    FORMAT_FLAG_CHAR = 1U << 7,
    FORMAT_FLAG_SHORT = 1U << 8,
    FORMAT_FLAG_LONG = 1U << 9,
    FORMAT_FLAG_LONG_LONG = 1U << 10,

    FORMAT_FLAG_UPPERCASE = 1U << 11,

    FORMAT_FLAG_NULL_TERM = 1U << 12,
};

typedef struct PrintState {
    size_t count;
    void* arg;
    PutCharFunc* put_char;
} PrintState;

static bool put_char_wrapper(PrintState* state, char character)
{
    // NOTE: Allow dumping to /dev/null
    bool success = state->put_char ? state->put_char(state->arg, character) : true;

    if (success) {
        state->count += 1;
    }

    return success;
}

static size_t ascii_to_i64(const char* str, int64_t* out_value)
{
    size_t i = 0;

    if (str) {
        int64_t result = 0;
        bool is_negative = false;

        if (str[0] == '-') {
            is_negative = true;
            ++i;
        }

        while (str[i] && is_dec_digit(str[i])) {
            result = (10 * result) + (str[i] - '0');
            ++i;
        }

        *out_value = is_negative ? -result : result;
    }

    // Return the number of characters read to parse the integer.
    return i;
}

static void ftprint_int_(PrintState* dest, unsigned long long value, unsigned long long base, bool negative, uint64_t precision,
                         uint32_t width, uint64_t flags)
{
    char temp_buf[PRINT_MAX_NUM_DIGITS];
    size_t len = 0;

    bool prec_flag = (flags & FORMAT_FLAG_PRECISION);
    bool width_flag = (flags & FORMAT_FLAG_WIDTH);
    bool print_sign = negative || (flags & FORMAT_FLAG_FORCE_SIGN);
    bool print_base = (flags & FORMAT_FLAG_HASH) && ((base == 2) || (base == 8) || (base == 16));
    bool left_just_flag = (flags & FORMAT_FLAG_LEFT_JUSTIFIED);
    bool zero_pad_flag = (flags & FORMAT_FLAG_ZERO_PAD);
    bool uppercase_flag = (flags & FORMAT_FLAG_UPPERCASE);

    if (prec_flag && (precision == 0) && (value == 0))
        return;

    // Write the value into the temporary buffer in reverse order.
    char base_10_digit = uppercase_flag ? 'A' : 'a';

    do {
        char digit = (char)(value % base);
        char base_digit = '0';

        if (digit >= 10) {
            base_digit = base_10_digit - 10;
        }

        // TODO: Will overflow for large bases
        // Consider using an enum for bases
        temp_buf[len++] = base_digit + digit;

        value = value / base;
    } while (value > 0 && (len < PRINT_MAX_NUM_DIGITS));

    // Pre-calculate padding sizes
    uint64_t tot_len = len;
    uint64_t zero_pad = 0;
    uint32_t space_pad = 0;

    if (prec_flag && (precision > len))
        zero_pad = precision - len;

    tot_len += zero_pad;

    if (print_base) {
        tot_len += 1;

        if (base != 8)
            tot_len += 1;
    }
    else if (print_sign) {
        tot_len += 1;
    }

    if (width_flag && (width > tot_len)) {
        uint64_t pad = width - tot_len;

        if (left_just_flag) {
            space_pad += pad;
        }
        else {
            if (zero_pad_flag)
                zero_pad += pad;
            else
                space_pad += pad;
        }
    }

    // Print width space padding for right justified numbers
    if (!left_just_flag) {
        while (space_pad--)
            put_char_wrapper(dest, ' ');
    }

    // Print base or sign.
    if (print_base) {
        put_char_wrapper(dest, '0');

        if (base == 2) {
            put_char_wrapper(dest, 'b');
        }
        else if (base == 16) {
            put_char_wrapper(dest, (uppercase_flag) ? 'X' : 'x');
        }
    }
    else if (print_sign) {
        put_char_wrapper(dest, (negative) ? '-' : '+');
    }

    // Print zero padding.
    while (zero_pad--)
        put_char_wrapper(dest, '0');

    // Print digits (reversed in buffer).
    while (len--)
        put_char_wrapper(dest, temp_buf[len]);

    // Print space padding for left justified numbers
    if (left_just_flag) {
        while (space_pad--)
            put_char_wrapper(dest, ' ');
    }

    return;
}

static void ftprint_int(PrintState* dest, long long value, long long base, uint64_t precision, uint32_t width, int64_t flags)
{
    bool negative = value < 0;
    unsigned long long uvalue = (unsigned long long)(negative ? 0 - value : value);

    ftprint_int_(dest, uvalue, base, negative, precision, width, flags);
}

static void print_uint(PrintState* dest, unsigned long long value, long long base, uint64_t precision, uint32_t width, int64_t flags)
{
    ftprint_int_(dest, value, base, false, precision, width, flags);
}

static void ftprint_float(PrintState* dest, double value, uint64_t precision, uint32_t width, uint64_t flags)
{
    F64String fstr = {0};

    f64_to_str(&fstr, value);

    bool negative = fstr.flags & F64_STRING_IS_NEG;

    /////////////////////////////////////
    // Handle NaN, Inf, -Inf
    ////////////////////////////////////
    if (fstr.flags & F64_STRING_IS_NAN) {
        const char* nan = "nan";

        for (size_t i = 0; i < cstr_len(nan); ++i)
            put_char_wrapper(dest, nan[i]);

        return;
    }
    else if ((fstr.flags & F64_STRING_IS_INF) && !negative) {
        bool plus = (flags & FORMAT_FLAG_FORCE_SIGN);
        const char* inf = (plus) ? "+inf" : "inf";

        for (size_t i = 0; i < cstr_len(inf); ++i)
            put_char_wrapper(dest, inf[i]);

        return;
    }
    else if (fstr.flags & F64_STRING_IS_INF) {
        assert(negative);
        const char* inf = "-inf";

        for (size_t i = 0; i < cstr_len(inf); ++i)
            put_char_wrapper(dest, inf[i]);

        return;
    }

    bool prec_flag = (flags & FORMAT_FLAG_PRECISION);
    bool width_flag = (flags & FORMAT_FLAG_WIDTH);
    bool print_sign = negative || (flags & FORMAT_FLAG_FORCE_SIGN);
    bool left_just_flag = (flags & FORMAT_FLAG_LEFT_JUSTIFIED);
    bool zero_pad_flag = (flags & FORMAT_FLAG_ZERO_PAD);

    if (!prec_flag)
        precision = PRINT_DEFAULT_FLOAT_PRECISION;

    f64str_round(&fstr, (u32)precision);

    ///////////////////////////
    // Compute paddings
    ///////////////////////////

    u32 tot_frac_digits = f64str_num_frac_digits(&fstr);
    u32 tot_int_digits = f64str_num_int_digits(&fstr);

    u64 tot_len = MAX(tot_int_digits, 1) + 1 + MAX(tot_frac_digits, 1); // The decimal point is the "+ 1".
    u64 width_pad = 0;

    if (print_sign)
        tot_len += 1;

    if (width_flag && (width > tot_len))
        width_pad = width - tot_len;

    // Print width padding for right-justified numbers.
    if (!left_just_flag) {
        char c = zero_pad_flag ? '0' : ' ';

        while (width_pad--)
            put_char_wrapper(dest, c);
    }

    // Print sign.
    if (print_sign)
        put_char_wrapper(dest, (negative) ? '-' : '+');

    // Print integral digits.
    if (tot_int_digits > 0) {
        for (u32 i = 0; i < tot_int_digits; i++) {
            put_char_wrapper(dest, i < fstr.num_digits ? fstr.digits[i] : '0');
        }
    }
    else {
        put_char_wrapper(dest, '0');
    }

    put_char_wrapper(dest, '.'); // Decimal point

    // Print fractional digits.
    if (tot_frac_digits > 0) {
        int end_frac = fstr.decimal_point + tot_frac_digits;

        for (int i = fstr.decimal_point; i < end_frac; i++) {
            put_char_wrapper(dest, i >= 0 ? fstr.digits[i] : '0');
        }
    }
    else {
        for (u32 i = 0; i < precision; i++) {
            put_char_wrapper(dest, '0');
        }
    }

    // Print width padding for left-justified numbers.
    if (left_just_flag) {
        while (width_pad--)
            put_char_wrapper(dest, ' ');
    }

    return;
}

size_t ftprintv(PutCharFunc* put_char, void* arg, const char* format, va_list args)
{
    PrintState state = {0};
    state.put_char = put_char;
    state.arg = arg;

    uint64_t flags = 0;
    uint64_t precision = 0;
    uint32_t width = 0;

    while (*format) {
        // Handle a format specifier.
        // %[.precision][length]specifier
        if (*format == '%') {
            ++format;

            // Reset flags
            flags = 0;

            // Parse flags sub-specifier
            bool parsing = true;
            while (parsing) {
                switch (*format) {
                case '-': {
                    flags |= FORMAT_FLAG_LEFT_JUSTIFIED;
                    ++format;
                } break;
                case '+': {
                    flags |= FORMAT_FLAG_FORCE_SIGN;
                    ++format;
                } break;
                case ' ': {
                    flags |= FORMAT_FLAG_SPACE_SIGN;
                    ++format;
                } break;
                case '#': {
                    flags |= FORMAT_FLAG_HASH;
                    ++format;
                } break;
                case '0': {
                    flags |= FORMAT_FLAG_ZERO_PAD;
                    ++format;
                } break;
                default: {
                    parsing = false;
                } break;
                }
            }

            // Process width sub-specifier
            width = 0;
            if (is_dec_digit(*format)) {
                int64_t w = 0;
                format += ascii_to_i64(format, &w);
                flags |= FORMAT_FLAG_WIDTH;

                width = w < 0 ? 0 : (uint32_t)w;
            }
            else if (*format == '*') {
                int w = va_arg(args, int);

                width = w < 0 ? 0 : (uint32_t)w;
                flags |= FORMAT_FLAG_WIDTH;
            }

            // Process precision sub-specifier
            precision = 0;
            if (*format == '.') {
                ++format;
                flags |= FORMAT_FLAG_PRECISION;

                if (is_dec_digit(*format)) {
                    int64_t p = 0;

                    format += ascii_to_i64(format, &p);
                    precision = p < 0 ? 0 - p : p;
                }
                else if (*format == '*') {
                    const int p = va_arg(args, int);

                    precision = p < 0 ? 0 - p : p;
                    ++format;
                }
            }

            // Process length sub-specifier
            switch (*format) {
            case 'l': {
                flags |= FORMAT_FLAG_LONG;
                ++format;

                if (*format == 'l') {
                    flags &= ~FORMAT_FLAG_LONG;
                    flags |= FORMAT_FLAG_LONG_LONG;
                    ++format;
                }
            } break;
            case 'h': {
                flags |= FORMAT_FLAG_SHORT;
                ++format;

                if (*format == 'h') {
                    flags &= ~FORMAT_FLAG_SHORT;
                    flags |= FORMAT_FLAG_CHAR;
                    ++format;
                }
            } break;
            default:
                break;
            }

            // Evaluate arg
            switch (*format) {
            case 'd':
            case 'i':
            case 'u':
            case 'x':
            case 'X':
            case 'o':
            case 'b': {
                uint64_t base = 10;

                // Figure out the base
                if (*format == 'x') {
                    base = 16;
                }
                else if (*format == 'X') {
                    base = 16;
                    flags |= FORMAT_FLAG_UPPERCASE;
                }
                else if (*format == 'o') {
                    base = 8;
                }
                else if (*format == 'b') {
                    base = 2;
                }

                // Print signed numbers
                if (*format == 'd' || *format == 'i') {
                    if (flags & FORMAT_FLAG_LONG_LONG) {
                        long long value = va_arg(args, long long);

                        ftprint_int(&state, value, base, precision, width, flags);
                    }
                    else if (flags & FORMAT_FLAG_LONG) {
                        long value = va_arg(args, long);

                        ftprint_int(&state, value, base, precision, width, flags);
                    }
                    else if (flags & FORMAT_FLAG_SHORT) {
                        short value = va_arg(args, int);

                        ftprint_int(&state, value, base, precision, width, flags);
                    }
                    else if (flags & FORMAT_FLAG_CHAR) {
                        char value = va_arg(args, int);

                        ftprint_int(&state, value, base, precision, width, flags);
                    }
                    else {
                        int value = va_arg(args, int);

                        ftprint_int(&state, value, base, precision, width, flags);
                    }
                }

                // Unsigned numbers
                else {
                    if (flags & FORMAT_FLAG_LONG_LONG) {
                        print_uint(&state, va_arg(args, unsigned long long), base, precision, width, flags);
                    }
                    else if (flags & FORMAT_FLAG_LONG) {
                        print_uint(&state, va_arg(args, unsigned long), base, precision, width, flags);
                    }
                    else if (flags & FORMAT_FLAG_SHORT) {
                        unsigned short value = va_arg(args, unsigned int);

                        print_uint(&state, value, base, precision, width, flags);
                    }
                    else if (flags & FORMAT_FLAG_CHAR) {
                        unsigned char value = va_arg(args, int);

                        print_uint(&state, value, base, precision, width, flags);
                    }
                    else {
                        print_uint(&state, va_arg(args, unsigned int), base, precision, width, flags);
                    }
                }
                ++format;
                break;
            }

            // Print a floating-point number
            case 'f':
            case 'F': {
                if (*format == 'F') {
                    flags |= FORMAT_FLAG_UPPERCASE;
                }

                ftprint_float(&state, va_arg(args, double), precision, width, flags);
                ++format;
            } break;

            // Print a character.
            case 'c': {
                uint32_t str_len = 1;

                // Pad with spaces if width specified, is right-aligned, and width > 1.
                if ((flags & FORMAT_FLAG_WIDTH) && !(flags & FORMAT_FLAG_LEFT_JUSTIFIED)) {
                    while (str_len < width) {
                        put_char_wrapper(&state, ' ');
                        ++str_len;
                    }
                }

                put_char_wrapper(&state, (char)va_arg(args, int));

                // Pad with spaces if a width was specified, is left-aligned, and the
                // string is smaller than the width.
                if ((flags & FORMAT_FLAG_WIDTH) && (flags & FORMAT_FLAG_LEFT_JUSTIFIED)) {
                    while (str_len < width) {
                        put_char_wrapper(&state, ' ');
                        ++str_len;
                    }
                }

                ++format;
            } break;

            // Print a string
            case 's': {
                const char* value = va_arg(args, const char*);
                bool is_null = !value;
                const char null_str[] = "(null)";
                unsigned str_len = 0;

                if (is_null) {
                    str_len = 0;
                }
                else if (flags & FORMAT_FLAG_PRECISION) {
                    str_len = cstr_nlen(value, precision);
                }
                else {
                    str_len = cstr_len(value);
                }

                // Pad with spaces if a width was specified, is right-aligned, and the
                // string is smaller than the width.
                if (!is_null && (flags & FORMAT_FLAG_WIDTH) && !(flags & FORMAT_FLAG_LEFT_JUSTIFIED)) {
                    while (str_len < width) {
                        put_char_wrapper(&state, ' ');
                        ++str_len;
                    }
                }

                if (is_null) {
                    value = (cstr_len(null_str) > precision) && (flags & FORMAT_FLAG_PRECISION) ? "" : null_str;
                }

                while ((!(flags & FORMAT_FLAG_PRECISION) || (precision > 0)) && *value) {
                    put_char_wrapper(&state, *value++);
                    --precision;
                }

                // Pad with spaces if a width was specified, is left-aligned, and the
                // string is smaller than the width.
                if (!is_null && (flags & FORMAT_FLAG_WIDTH) && (flags & FORMAT_FLAG_LEFT_JUSTIFIED)) {
                    while (str_len < width) {
                        put_char_wrapper(&state, ' ');
                        ++str_len;
                    }
                }

                ++format;
            } break;

            // Nothing printed. A pointer is extracted and the current number of chars
            // written is stored in the pointed location.
            case 'n': {
                int* p = va_arg(args, int*);

                *p = state.count;
                ++format;
            } break;
            default:

                // Unknown specifier NOT SUPPORTED!!!!
                put_char_wrapper(&state, *format++);
                break;
            }
        }
        else {
            // Just print the next character in the format string.
            put_char_wrapper(&state, *format++);
        }
    }

    put_char_wrapper(&state, '\0');

    return state.count;
}

size_t ftprint(PutCharFunc* put_char, void* arg, const char* format, ...)
{
    size_t n = 0;
    va_list va;

    va_start(va, format);
    n = ftprintv(put_char, arg, format, va);
    va_end(va);

    return n;
}

typedef struct PrintBuffer {
    char* buf;
    size_t size;
    size_t index;

    // Used only when print to a file.
    FILE* fd;
    bool nullterm; // True if the null-terminator should be written out.
} PrintBuffer;

static bool flush_file_buffer(PrintBuffer* fb)
{
    size_t n = fwrite(fb->buf, sizeof(char), fb->index, fb->fd);

    if (n == fb->index) {
        fb->index -= n;

        return true;
    }

    return false;
}

static bool output_to_file(void* data, char character)
{
    bool ret = true;
    PrintBuffer* dest = (PrintBuffer*)data;
    bool write_char = character || dest->nullterm;

    if (dest->index < dest->size) {
        if (write_char)
            dest->buf[dest->index++] = character;
    }
    else {
        if (flush_file_buffer(dest)) {
            if (write_char)
                dest->buf[dest->index++] = character;
        }
        else {
            return false;
        }
    }

    if (!character)
        ret = flush_file_buffer(dest);

    return ret;
}

size_t ftprintv_file(FILE* fd, bool nullterm, const char* format, va_list vargs)
{
    char buffer[PRINT_FILE_BUF_SIZE]; // Buffer is automatically flushed to screen when full.
    PrintBuffer dest = {0};

    dest.buf = buffer;
    dest.size = sizeof(buffer);
    dest.fd = fd;
    dest.nullterm = nullterm;

    return ftprintv(output_to_file, &dest, format, vargs);
}

size_t ftprint_file(FILE* fd, bool nullterm, const char* format, ...)
{
    size_t n = 0;
    va_list vargs;
    char buffer[PRINT_FILE_BUF_SIZE]; // Buffer is automatically flushed to screen when full.
    PrintBuffer dest = {0};

    dest.buf = buffer;
    dest.size = sizeof(buffer);
    dest.fd = fd;
    dest.nullterm = nullterm;

    va_start(vargs, format);
    n = ftprintv(output_to_file, &dest, format, vargs);
    va_end(vargs);

    return n;
}
