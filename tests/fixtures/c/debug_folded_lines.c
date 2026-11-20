/* Two statements whose entire body is one jump: the `continue;` in the first loop
 * and the `break;` in the nested loops below.
 *
 * E5 (`empty_block_elimination`) lowers each to a block holding a single `br` and
 * then eliminates that block, retargeting its predecessors. The eliminated `br` is
 * the only instruction that carried the statement's location, so at `-O1` neither
 * line has an instruction, a `.Ldbg` label or a KDBG `Line` record. That is the
 * documented contract (docs/symbols.md): a line is placeable only while an
 * krc_debug_o1_jump_only_lines and krc_debug_o0_jump_only_lines name this file's
 * line numbers, so a change to its line numbering has to update those two tests.
 */

int main(void) {
    int hits = 0;
    for (int index = 0; index < 8; index = index + 1) {
        if (index % 2 != 0) {
            continue;
        }
        hits = hits + 1;
    }
    int outer = 0;
    for (outer = 0; outer < 3; outer = outer + 1) {
        for (int index = 0; index < 8; index = index + 1) {
            if (index == 6) {
                break;
            }
            hits = hits + 1;
        }
    }
    return hits;
}
