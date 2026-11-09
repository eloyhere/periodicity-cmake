# L3b — composited paint stages: background, elevation, foreground, ripple.
periodicity_add_module(layer
  DEPENDS periodicity::core periodicity::tokens periodicity::theme
          periodicity::drawable periodicity::ripple Qt6::Gui)
