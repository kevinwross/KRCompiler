static int counter = 2;

int first(void);
int second(void) {
    return counter;
}

int main(void) {
    return first() + second();
}
