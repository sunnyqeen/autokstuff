cmake_minimum_required(VERSION 3.5)

project(autokstuff C)

add_executable(autokstuff main.c kstuff_toggle.c)
target_link_libraries(autokstuff PRIVATE SceSystemService SceUserService)
set_target_properties(autokstuff PROPERTIES OUTPUT_NAME autokstuff.elf)
