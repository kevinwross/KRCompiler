struct S {
    operator bool() const { return true; }
};

int main() {
    S s;
    return static_cast<bool>(s) ? 0 : 1;
}
