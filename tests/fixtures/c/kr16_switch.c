int classify(int value) {
    switch (value) {
    case 1:
        return 3;
    case 2:
        return 5;
    default:
        return 7;
    }
}

int main(void) {
    return classify(0) + classify(1) + classify(2);
}
