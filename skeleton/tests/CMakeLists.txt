# The guest-test directory, so a tree made from this skeleton configures and runs a
# row out of the box. Replace the placeholder with the VM's own tests; keep the row
# names prefixed with the target so a family run can tell whose failure it is.

add_executable(krNN_unit test_machine.cpp)
target_link_libraries(krNN_unit PRIVATE krNN_core)
add_test(NAME krNN.unit COMMAND krNN_unit)
