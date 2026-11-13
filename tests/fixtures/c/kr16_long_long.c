// The KR16 IP16 profile has no 64-bit integer type, so `long long` must be
// rejected rather than lowered to a zero-width type. The compiler must report
// the profile by name.
long long wide;

int main(void) {
    wide = 1;
    return 0;
}
