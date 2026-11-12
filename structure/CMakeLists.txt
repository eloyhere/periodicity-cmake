# L1a — the data-structure layer.
#
# Two kinds of headers live here:
#   * the container catalogue (node/, tree/, forest/, skip/, pool/) that the
#     framework uses for its own hierarchies and queues;
#   * the plain data contracts the data-heavy components share (tree.h,
#     table.h, virtual_source.h): tree nodes, table data and the virtual
#     data-source interfaces TreeSelect, VirtualTreeSelect, VirtualSelect,
#     Table and VirtualTable read from.
#
# No widgets, no painting, no tokens: Qt Core only.
periodicity_add_module(structure
  DEPENDS periodicity::core Qt6::Core)

add_subdirectory(tests)
