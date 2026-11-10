int main(void) {
    const char *name = __func__;
    return name[0] == 'm' && name[1] == 'a' && name[2] == 'i' &&
                   name[3] == 'n'
               ? 0
               : 1;
}
