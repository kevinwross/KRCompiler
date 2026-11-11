unsigned int strlen(const char *text);
int strcmp(const char *left, const char *right);
char *strcpy(char *destination, const char *source);

int main(void) {
    char buffer[8];
    strcpy(buffer, "abc");
    return (int)strlen(buffer) + (strcmp(buffer, "abc") == 0 ? 1 : 0);
}
