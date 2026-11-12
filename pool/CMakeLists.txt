# L1p — the pool layer: object storage, workers, buffers and process channels.
#
# Headless: Qt Core and the standard library, no widgets, no tokens, no
# painting. The object pools that used to live in `structure` live here now,
# and the thread pool recycles its task nodes through them.
find_package(Threads REQUIRED)

periodicity_add_module(pool
  DEPENDS periodicity::core Qt6::Core Threads::Threads)

add_subdirectory(tests)
