int strcmp(const char *left, const char *right);

const char *name = "hello";
const char *names[] = { "one", "two" };

int main(void) {
    if (strcmp(name, "hello") != 0) {
        return 1;
    }
    if (strcmp(names[0], "one") != 0) {
        return 2;
    }
    if (strcmp(names[1], "two") != 0) {
        return 3;
    }
    return 0;
}
