# L4 — the component kernel: measure, arrange, paint template, state machine.
periodicity_add_module(view
  DEPENDS periodicity::core periodicity::tokens periodicity::theme
          periodicity::drawable periodicity::layer periodicity::animation
          Qt6::Widgets)
