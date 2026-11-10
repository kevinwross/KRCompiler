_Static_assert(sizeof(long) == 2, "KR16 long is 16 bits");
_Static_assert(sizeof(void*) == 2, "KR16 pointers are 16 bits");
_Static_assert(sizeof(int) == 2, "KR16 int is 16 bits");
_Static_assert((char)-1 > 0, "KR16 char is unsigned");

int main(void) {
    return 0;
}
