/* Tier 1 — arrays. One- and two-dimensional layouts, initialisers, and the
 * element stride.
 *
 * The 2-D checks read `matrix[row][column]` and compare against the value written
 * at that coordinate, so a wrong stride shows up as a wrong value. */

int main(void) {
    int one[4] = {1, 2, 3, 4};
    if (one[0] != 1) { return 1; }
    if (one[3] != 4) { return 2; }
    if (one[1] + one[2] != 5) { return 3; }

    /* A partially initialised array zero-fills the rest. */
    int partial[4] = {7};
    if (partial[0] != 7) { return 4; }
    if (partial[1] != 0) { return 5; }
    if (partial[3] != 0) { return 6; }

    /* A character array indexed directly. */
    char letters[3];
    letters[0] = 'a';
    letters[1] = 'b';
    letters[2] = 'c';
    if (letters[0] != 'a') { return 7; }
    if (letters[2] != 'c') { return 8; }

    /* Two dimensions: write by coordinate, read by coordinate. */
    int matrix[3][4];
    int row = 0;
    while (row < 3) {
        int column = 0;
        while (column < 4) {
            matrix[row][column] = row * 10 + column;
            column = column + 1;
        }
        row = row + 1;
    }
    if (matrix[0][0] != 0) { return 9; }
    if (matrix[0][3] != 3) { return 10; }
    if (matrix[1][0] != 10) { return 11; }
    if (matrix[2][3] != 23) { return 12; }

    /* The row's own address and the row as an array name the same elements. */
    int* first_row = matrix[1];
    if (first_row[2] != 12) { return 13; }
    if (*(first_row + 2) != 12) { return 14; }

    /* Summing a 2-D array by nested loops. */
    int total = 0;
    for (row = 0; row < 3; row = row + 1) {
        for (int column = 0; column < 4; column = column + 1) {
            total = total + matrix[row][column];
        }
    }
    if (total != 138) { return 15; }

    /* An array of pointers to strings. */
    char* words[3];
    words[0] = "one";
    words[1] = "two";
    words[2] = "three";
    if (words[0][0] != 'o') { return 16; }
    if (words[2][1] != 'h') { return 17; }

    return 0;
}
