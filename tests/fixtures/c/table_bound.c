/* A loop bound that must hold: the index may never pass the array's size.
 *
 * The kernel's process table search has this shape -- a static array of records,
 * a bound, and a field test -- and at -O1 the rebuilt kernel walked the index far
 * past the bound. This fixture is the smallest program with the same shape: fill
 * every record so the field test never ends the loop, and the bound is the only
 * thing that can. The return value is the index, so the row's expected exit code
 * is the array size.
 */

#define ENTRIES 256

struct Record {
    int used;
    int pid;
    int state;
    int status;
    char name[32];
};

static struct Record table[ENTRIES];

int main(void) {
    int index = 0;
    while (index < ENTRIES) {
        table[index].used = 1;
        table[index].pid = index;
        ++index;
    }

    /* Every record is in use, so only the bound can stop this. */
    index = 0;
    while (index < ENTRIES && table[index].used != 0) {
        ++index;
    }
    if (index != ENTRIES) {
        return 1;
    }

    /* And once one is free, the search must stop on it. */
    table[7].used = 0;
    index = 0;
    while (index < ENTRIES && table[index].used != 0) {
        ++index;
    }
    if (index != 7) {
        return 2;
    }
    return 0;
}
