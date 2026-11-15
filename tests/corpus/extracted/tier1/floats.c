/* Extracted from ../kr32/examples at commit ba69940 for the Tier 1-2 corpus.
 * Copied verbatim; the original header, if any, follows. */
// Floating-point scalar coverage for the KR32 C compiler.
//
// Demonstrates float and double literals, locals, globals, arithmetic,
// comparisons, casts, FP arguments, FP return values, and floating-point
// values passed through the variadic printf.

// printf is variadic, so it needs a prototype: an undeclared call is compiled
// with the fixed-parameter convention and would pass a double in F0 instead of
// on the stack, where va_arg reads it.
int printf(const char *format, ...);

double global_double = 4.5;

float DoubleFloat(float value) {
    return value * value;
}

double DoubleDouble(double value) {
    return value * value;
}

double fpCrazyTown(double Smack, float Crackle, double Pop, float Boom) {
    double Fizzle = 22.0 / 7.0;
    double Bizzle = Smack + Pop + Fizzle;
    double Cizzle = Crackle + Boom + Fizzle;
    return Fizzle * 2.0 + Bizzle * Crackle / 2.0 + Pop - Boom * Cizzle;
}

double MixValues(int integer_part, double double_part, float float_part) {
    return integer_part + double_part + float_part;
}

int main(void) {
    float f = 2.5f;
    double d = 3.25;
    float sum = f + (float)d;
    double mixed = MixValues(3, 2.5, 1.25f);

    // The integer conversions exercise the cast path; the last two lines
    // print floating-point values directly through printf.
    printf("float 2.5 squared = %d\n", (int)DoubleFloat(f));
    printf("double 3.25 squared = %d\n", (int)DoubleDouble(d));
    printf("2.5 + 3.25 = %d\n", (int)sum);
    printf("3 + 2.5 + 1.25 = %d\n", (int)mixed);
    printf("global 4.5 squared = %d\n", (int)DoubleDouble(global_double));
    printf("fpCrazyTown result = %f\n", fpCrazyTown(1.0, 2.0f, 3.0, 4.0f));

    if (d > f && mixed >= 6.0) {
        printf("comparisons: ok\n");
    } else {
        printf("comparisons: failed\n");
    }

    return 0;
}
