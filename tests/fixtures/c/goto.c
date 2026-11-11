int main(void) {
    int total = 0;
    int index = 0;
begin:
    index++;
    total += index;
    if (index < 3) {
        goto begin;
    }
    goto done;
    total = 100;
done:
    return total;
}
