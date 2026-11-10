#include <stdio.h>

class D {
public:
    ~D() { printf("done\n"); }
};

int main() {
    {
        D inner;
    }
    printf("after\n");
    return 0;
}
