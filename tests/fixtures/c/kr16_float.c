// The KR16 IP16 profile has no floating-point type, so `double`, `float`, and
// `long double` must be rejected rather than lowered to the IR. The compiler
// must report the profile by name.
double real;
float single;
long double wide;

int main(void) {
    real = 1.0;
    single = 2.0f;
    wide = 3.0L;
    return 0;
}
