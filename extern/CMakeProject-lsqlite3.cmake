set(LSQLITE_SRC "lsqlite3_v096/lsqlite3.c" "lsqlite3_v096/sqlite3.c")

source_group("" FILES ${LSQLITE_SRC})

add_library("lsqlite3" STATIC ${LSQLITE_SRC})

target_include_directories("lsqlite3" PUBLIC "lua-5.1/src" "lsqlite3_v096")
