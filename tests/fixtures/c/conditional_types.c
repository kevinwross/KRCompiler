int strcmp(const char *left, const char *right);

double choose_double(int condition) {
    return condition ? 0 : 3.5;
}

const char *choose_text(int condition) {
    return condition ? 0 : "text";
}

int main(void) {
    if (choose_double(0) != 3.5) {
        return 1;
    }
    if (strcmp(choose_text(0), "text") != 0) {
        return 2;
    }
    return 0;
}
