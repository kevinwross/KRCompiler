int strcmp(const char *left, const char *right);

int main(void) {
    const char *text = "hello" " world";
    return strcmp(text, "hello world") == 0 ? 0 : 1;
}
