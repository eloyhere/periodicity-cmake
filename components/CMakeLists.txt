# L6 — public Material components.
#
# Qt Network is linked for Avatar's url loader; every other component stays on
# Widgets. Nothing else in the layer performs I/O.
periodicity_add_module(components
  DEPENDS periodicity::layout periodicity::structure periodicity::store Qt6::Widgets Qt6::Network)
