bool truth() { return true; }

int main() {
    bool flag = false;
    if (flag) {
        return 1;
    }
    return truth() ? 0 : 1;
}
