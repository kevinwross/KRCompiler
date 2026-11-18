/* A character constant has the type int in C, unlike C++, so its size is the size of int
   rather than one. int is two bytes on KR16 and four on KR32, so the assertion is written
   against sizeof(int) instead of a literal. */
_Static_assert(sizeof('a') == sizeof(int), "a character constant is an int in C");

int main(void) {
    if (sizeof('a') != sizeof(int)) { return 1; }
    return 42;
}