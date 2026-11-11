int strlen(const char *text);

int main(void) {
    char text[] = "abc";
    return strlen(text) == 3 ? 0 : 1;
}
