int main(void) {
    if ('\v' != 11) {
        return 1;
    }
    if ('\f' != 12) {
        return 2;
    }
    if ('\a' != 7) {
        return 3;
    }
    if ('\b' != 8) {
        return 4;
    }
    if ('\?' != '?') {
        return 5;
    }
    const char *text = "a\vb\f";
    if (text[1] != 11 || text[3] != 12) {
        return 6;
    }
    return 0;
}
