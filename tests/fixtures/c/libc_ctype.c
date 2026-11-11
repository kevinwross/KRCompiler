#include <ctype.h>

int main(void) {
    if (!isalpha('A') || isalpha('1')) {
        return 1;
    }
    if (!isdigit('7') || isdigit('a')) {
        return 2;
    }
    if (!isalnum('9') || !isalnum('z')) {
        return 3;
    }
    if (!isblank(' ') || !isblank('\t')) {
        return 4;
    }
    if (!iscntrl(1) || iscntrl('A')) {
        return 5;
    }
    if (!isgraph('!') || isgraph(' ')) {
        return 6;
    }
    if (!islower('a') || !isupper('Z')) {
        return 7;
    }
    if (!isprint(' ') || !isprint('~')) {
        return 8;
    }
    if (!ispunct('!') || ispunct('A')) {
        return 9;
    }
    if (!isspace(' ') || !isspace('\n') || isspace('A')) {
        return 10;
    }
    if (!isxdigit('f') || !isxdigit('A') || isxdigit('g')) {
        return 11;
    }
    if (tolower('A') != 'a' || toupper('z') != 'Z') {
        return 12;
    }
    return 0;
}
