# L4 — the component kernel: measure, arrange, paint template, state machine.
periodicity_add_module(view
  DEPENDS periodicity::core periodicity::schedule periodicity::tokens
          periodicity::theme periodicity::event periodicity::ripple
          periodicity::drawable periodicity::layer periodicity::animation
          Qt6::Widgets)
