int main(void) {
    char text[4] = "abc";
    void *generic = text;
    const char *specific = text;
    return generic != specific;
}
