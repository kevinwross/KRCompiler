struct Inner { int x; int y; };
struct Outer { struct Inner inner; int z; };

int main() {
    struct Outer outer = {.inner.y = 4, .inner.x = 3, .z = 5};
    return outer.inner.x + outer.inner.y + outer.z;
}
