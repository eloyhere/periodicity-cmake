# L1b — the configuration store.
#
# A Store is a named bag of values (QVariantMap) with a process-wide registry:
# `Store::open("md.theme")` always returns the same object, which makes a store
# a natural place for configuration that several containers read. The module is
# widget-free (Qt Core only); the Font, ThemeScope and Language containers in
# components publish their data here.
periodicity_add_module(store
  DEPENDS periodicity::core Qt6::Core)

add_subdirectory(tests)
