int strcmp(const char *left, const char *right);

class Namer {
public:
    const char *method_name() { return __func__; }
};

int main(void) {
    Namer namer;
    return strcmp(namer.method_name(), "method_name") == 0 ? 0 : 1;
}
