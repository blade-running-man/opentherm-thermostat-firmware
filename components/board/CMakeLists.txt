# One descriptor per chip, and it is selected by IDF_TARGET, not by a build flag.
#
# Why by target and not by -DBOARD_*: flags from platformio.ini reach the compiler but
# not the CMake logic, and branching on them would mean #if inside the files
# themselves -- that is, keeping sources in the tree that build into nothing. CMake,
# on the other hand, knows the target by itself.
#
# The limitation is stated out loud: this way exactly ONE board per chip is possible.
# Today that is true (C3 -- LOLIN C3 mini, C6 -- SuperMini). When a second board on
# the same chip appears, an explicit variable will be added here, and not before:
# machinery for a case that does not exist costs more than its absence.
#
# The else() is mandatory and must be FATAL_ERROR. Without it a build for an unknown
# chip would produce an empty component and a link failure on board_get() -- a message
# from which the cause cannot be found.
idf_build_get_property(board_target IDF_TARGET)
if(board_target STREQUAL "esp32c3")
    set(board_src "board_lolin_c3_mini.c")
elseif(board_target STREQUAL "esp32c6")
    set(board_src "board_supermini_c6.c")
else()
    message(FATAL_ERROR
        "components/board: no descriptor for target '${board_target}'. "
        "Add board_<board>.c and a branch above.")
endif()

idf_component_register(SRCS ${board_src}
                       INCLUDE_DIRS "include")
