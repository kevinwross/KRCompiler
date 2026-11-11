int main(void) {
    if ('\x41' != 'A') {
        return 1;
    }
    if ('\x7f' != 127) {
        return 2;
    }
    const char *text = "\x42\x43";
    if (text[0] != 'B' || text[1] != 'C') {
        return 3;
    }
    return 0;
}
