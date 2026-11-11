int classify(int value) {
    switch (value) {
    case 1:
        return 10;
    case 2:
        return 20;
    default:
        return 30;
    }
}

int fall_through(int value) {
    int result = 0;
    switch (value) {
    case 1:
        result += 1;
    case 2:
        result += 2;
        break;
    default:
        result = 9;
        break;
    }
    return result;
}

int main(void) {
    return fall_through(1) == 3 && fall_through(2) == 2 &&
                   classify(1) == 10 && classify(9) == 30
               ? 0
               : 1;
}
