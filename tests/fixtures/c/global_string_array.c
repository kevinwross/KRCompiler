int strlen(const char *text);
char text[] = "hello";
int main(void) {
    return strlen(text) == 5 ? 0 : 1;
}
