// A character literal has the type char in C++, so it occupies one byte. Before that was
// fixed this front end typed it int, and `make_tuple(41, 'a')` deduced `tuple<int, int>`
// which could not be assigned to the `tuple<int, char>` it was written into. This pins the
// typing so a regression fails here rather than several layers away.
static_assert(sizeof('a') == 1, "a character literal is one byte in C++");

int main() {
    if (sizeof('a') != 1) { return 1; }
    char letter = 'a';
    if (letter != 97) { return 2; }
    return 42;
}