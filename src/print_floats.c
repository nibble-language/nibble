#include <string.h>
#include <assert.h>
#include "cstring.h"
#include "print_floats.h"

// x = f * (2^e)
typedef struct CustomFP {
    u64 f; // 64-bit significand
    int e; // exponent (unbiased).
} CustomFP;

typedef union F64Bits {
    double f;
    u64 i;
} F64Bits;

// Modified from musl libc implementation (MIT license)
// http://git.etalabs.net/cgit/musl/tree/src/math/ceil.c
double ceil(double x)
{
    const F64Bits xbits = {.f = x};
    const int exp = (xbits.i & F64_EXP_MASK) >> F64_EXP_POS;
    const int exp_52 = 0x3FF + 52;

    // All floating-point numbers larger than 2^52 are exact integers, so return x.
    // This also handles 0, "NaN", and "inf".
    if (exp >= exp_52 || x == 0.0) {
        return x;
    }

    const int exp_neg_1 = 0x3FF - 1;
    const bool is_neg = xbits.i >> 63;

    // If |x| < 1, then negative numbers round to -0, and positive numbers round to 1.
    if (exp <= exp_neg_1) {
        return is_neg ? -0.0 : 1.0;
    }

    // The exponent is guaranteed to be in the range [0, 51] from this point forward.
    // Use addition with 2^52 to get the nearest integer neighbor.
    //
    // Examples:
    //     10.7 + 2^52 - 2^52 => 11.0
    //     10.2 + 2^52 - 2^52 => 10.0
    //     -10.1 - 2^52 + 2^52 => -10.0

    const double thresh_2p52 = 0x1.0p52;
    const double int_neighbor = is_neg ? (x - thresh_2p52 + thresh_2p52) : (x + thresh_2p52 - thresh_2p52);
    const double neighbor_diff = int_neighbor - x;

    if (neighbor_diff < 0.0) {
        return x + neighbor_diff + 1.0;
    }

    return x + neighbor_diff;
}

static CustomFP custom_fp_norm(CustomFP x)
{
    const u64 last_bit_mask = 0x8000000000000000ULL;
    const u64 last_byte_mask = 0xFF00000000000000ULL;

    // Shift left until the most-significant byte has a 1.
    while (!(x.f & last_byte_mask)) {
        x.f = x.f << 8;
        x.e -= 8;
    }

    // Shift left until the most-significant bit is a 1 (i.e., normalized).
    while (!(x.f & last_bit_mask)) {
        x.f = x.f << 1;
        x.e -= 1;
    }

    return x;
}

static CustomFP custom_fp_from_f64(double x)
{
    CustomFP fp;
    F64Bits bits = {.f = x};

    // Note that the exponent bias is traditionally 1023, but we want to treat the "fraction" as a non-fraction.
    // So, we add 52 (length of fraction bits).
    const int exp_bias = 1075;
    const u64 implicit_one = 0x0010000000000000ULL;

    // Handle 0 (exp == 0, f == 0) and subnormals (exp == 0, f != 0)
    //
    // For subnormals, the double is (-1)^sign * 2^(1 - 1023) * 0.fraction
    // OR, (-1)^sign * 2^(1 - 1075) * fraction
    //
    // For zero, the same computation just works.
    if (!(bits.i & F64_EXP_MASK)) {
        fp.f = bits.i & F64_FRAC_MASK;
        fp.e = 1 - exp_bias;
    }
    // Normal doubles.
    // (-1)^sign * 2^(exp - 1023) * 1.fraction
    // OR,
    // (-1)^sign * 2^(exp - 1075) * (2^52 + fraction)
    else {
        int unbiased_exp = ((bits.i & F64_EXP_MASK) >> F64_EXP_POS);
        fp.f = implicit_one + (bits.i & F64_FRAC_MASK);
        fp.e = unbiased_exp - exp_bias;
    }

    return fp;
}


u32 f64str_num_frac_digits(F64String* fstr)
{
    return MAX(fstr->num_digits - fstr->decimal_point, 0);
}

u32 f64str_num_int_digits(F64String* fstr)
{
    return MAX(fstr->decimal_point, 0);
}

bool f64str_has_nonzero_digit(F64String* fstr, u32 round_digit)
{
    u32 start = round_digit + 1;

    for (u32 i = start; i < fstr->num_digits; i++) {
        if (fstr->digits[i] > '0' && fstr->digits[i] <= '9') {
            return true;
        }
    }

    return false;
}

// Adapted from https://research.swtch.com/ftoa
void f64_to_str(F64String* dst, double f)
{
    dst->num_digits = 0;
    dst->decimal_point = 0;
    dst->flags = 0;

    F64Bits fbits = {.f = f};

    // Handle negative values.
    if (fbits.i >> 63) {
        fbits.i ^= F64_SIGN_MASK; // Make positive.
        dst->flags |= F64_STRING_IS_NEG;
    }

    // Handle zero
    if (!fbits.i) {
        dst->num_digits = 1;
        dst->digits[0] = '0';
        dst->decimal_point = 1;

        return;
    }

    int biased_exp = (fbits.i & F64_EXP_MASK) >> F64_EXP_POS;
    u64 fraction = (fbits.i & F64_FRAC_MASK);

    // Handle infinity: exponent = 0x7FF, fraction = 0.
    // Handle NaNs: exponent = 0x7FF, fraction != 0.
    if (biased_exp == 0x7FF) {
        dst->flags |= (fraction != 0 ? F64_STRING_IS_NAN : F64_STRING_IS_INF);
        return;
    }

    CustomFP fp = custom_fp_norm(custom_fp_from_f64(fbits.f));

    // Convert significand to a string (itoa)
    {
        char tmp_buf[PRINT_MAX_NUM_DIGITS];
        int len = 0;
        u64 value = fp.f;

        // Write digits into tmp_buf in reverse order.
        do {
            assert(len < PRINT_MAX_NUM_DIGITS);
            tmp_buf[len++] = '0' + (char)(value % 10);
            value = value / 10;
        } while (value > 0);

        // Copy into dst buffer in the correct order.
        for (int i = len - 1; i >= 0; i--) {
            dst->digits[dst->num_digits++] = tmp_buf[i];
        }
    }

    // Multiply significand by 2 "e" times.
    int e = fp.e;

    for (; e > 0; e--) {
        bool add_digit = dst->digits[0] >= '5';
        char carry = 0;

        for (u32 i = dst->num_digits; i-- > 0;) {
            int x = carry + 2 * (dst->digits[i] - '0');

            carry = x / 10; // TODO: x >= 10
            dst->digits[i + add_digit] = (x % 10) + '0';
        }

        if (add_digit) {
            dst->digits[0] = '1';
            dst->num_digits += 1;
        }
    }

    dst->decimal_point = dst->num_digits;

    // Divide significand by 2 "e" times.
    for (; e < 0; e++) {
        // If the last digit is odd, add a new digit for the .5
        if (dst->digits[dst->num_digits - 1] % 2 != 0) {
            dst->digits[dst->num_digits] = '0';
            dst->num_digits += 1;
        }

        int read_delta = 0;
        char prev_rem = 0;

        // Just like when dividing by hand, if the first (left-most) digit is less than 2,
        // then we have to consider the first two digits together.
        if (dst->digits[0] < '2') {
            read_delta = 1;
            prev_rem = dst->digits[0] - '0';
            dst->num_digits -= 1;
            dst->decimal_point -= 1;
        }

        // Divide by 2 (left to right).
        // 'prev_rem' is the remainder of the previous step.
        for (u32 i = 0; i < dst->num_digits; i++) {
            int x = (prev_rem * 10) + (dst->digits[i + read_delta] - '0');

            dst->digits[i] = (x / 2) + '0';
            prev_rem = x % 2;
        }
    }

    dst->digits[dst->num_digits] = '\0';
}

void f64str_round(F64String* fstr, u32 precision)
{
    const u32 digits_cap = ARRAY_LEN(fstr->digits);
    u32 tot_frac_digits = f64str_num_frac_digits(fstr);

    // Location of the first fractional digit to be discarded and used for rounding.
    int round_digit = fstr->decimal_point + precision;

    // Precision is too low, and thus, this procedure discards all non-zero digits after the decimal point.
    // Sets digits array to 0.00...0 (precision determines the number of digits after decimal point)
    if (round_digit < 0) {
        fstr->num_digits = MIN(precision + 1, digits_cap - 1);
        fstr->decimal_point = 1;

        memset(fstr->digits, '0', fstr->num_digits);
    }
    else if (tot_frac_digits > 0 && (u32)round_digit < fstr->num_digits) {
        if (tot_frac_digits > precision) {
            // Round if the "round_digit" is greater than '5', OR
            // The "round_digit" is '5' and either the previous digit is odd or have any non-zero digit after
            // the round_digit.
            if ((fstr->digits[round_digit] > '5') ||
                ((fstr->digits[round_digit] == '5') &&
                 ((fstr->digits[round_digit - 1] % 2 == 1) || f64str_has_nonzero_digit(fstr, (u32)round_digit)))) {
                int i = round_digit - 1;

                // Convert nines to zero
                while (i >= 0 && fstr->digits[i] == '9') {
                    fstr->digits[i] = '0';
                    i -= 1;
                }

                if (i >= 0) {
                    fstr->digits[i] += 1; // Round up
                }
                else {
                    // Ex: 999.996 (prec of 2) => 1000.00
                    fstr->digits[0] = '1';
                    fstr->digits[round_digit] = '0';
                    fstr->decimal_point += 1;
                    fstr->num_digits += 1;
                }
            }

            // Decrease the number of digits by the number of discarded digits due to rounding.
            fstr->num_digits -= (tot_frac_digits - precision);
        }
        else if (tot_frac_digits < precision) {
            // Pad with '0' until the number of fractional digits equals the precision.
            while (tot_frac_digits < precision && (fstr->num_digits < digits_cap)) {
                fstr->digits[fstr->num_digits] = '0';
                fstr->num_digits += 1;
                tot_frac_digits += 1;
            }
        }
    }

    fstr->digits[fstr->num_digits] = '\0'; // Null terminate
}

