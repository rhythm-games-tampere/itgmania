# Moves the profiles an instrumented MSVC build wrote next to itself
# (NAME!1.pgc, ...) to PGO_DIR, next to the .pgd, where the linker reads them.
# Run with cmake -DEXE_DIR=... -DNAME=... -DPGO_DIR=... -P CollectPgc.cmake.
file(GLOB pgcs "${EXE_DIR}/${NAME}!*.pgc")
foreach(pgc IN LISTS pgcs)
  get_filename_component(pgc_name "${pgc}" NAME)
  file(RENAME "${pgc}" "${PGO_DIR}/${pgc_name}")
endforeach()
