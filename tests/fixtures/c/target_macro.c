// The target macro is defined, and it names the target being compiled for.
//
// The descriptor carries it (`kKr32` uses "__kr32__", `kKr16` uses "__kr16__"), and the
// preprocessor defines it, so portable code can select by target. A probe written with
// the uppercase spelling `__KR32__` reports it as absent, which is how it was once
// recorded as missing here: the macro exists, the spelling differs.
//
// `#error` when neither is defined, so a descriptor that loses its macro fails to
// compile rather than silently taking a third branch. Returns 42 when the macro agrees
// with the profile's pointer width.

#if defined(__kr32__)
int profile_pointer_bits = 32;
#elif defined(__kr16__)
int profile_pointer_bits = 16;
#else
#error no target macro is defined
#endif

int main() {
    if (profile_pointer_bits != (int)(sizeof(void*) * 8)) {
        return 1;
    }
    return 42;
}
