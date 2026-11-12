# Component catalogue

Every public component lives in `components/include/md/components/` (or
`layout/include/md/layout/` for the containers) and is painted with `QPainter`
through `md::View` — no Qt style sheets and no native style engine. The
`tests/` column names the Qt Test suite that exercises it; the gallery page that
shows it is in the last column (`apps/gallery/src/main.cpp`).

The data-heavy components read their rows from the headless `structure/` module
(`md::structure`): the container catalogue (nodes, trees, forests, a skip list and
object pools) plus the plain contracts `TreeNode`, `TableData`, `VirtualSource`,
`VirtualTreeSource` and `VirtualTableSource`. Nothing in `structure/` includes a
widget, a token or a painter.

## Basic widgets

| Component | Header | Notes | Tests | Gallery |
| --- | --- | --- | --- | --- |
| `Button` | `button.h` | Filled, filled tonal, elevated, outlined, text; icon + label | `tests/components/tst_basic_widgets.cpp` | Basic |
| `CheckBox` | `checkbox.h` | Two-state and tri-state | same | Basic |
| `Radio` | `radio.h` | Mutually exclusive by group id | same | Basic |
| `Rate` | `rate.h` | Stars with half steps | same | Basic |
| `Progress` | `progress.h` | Linear, buffer, indeterminate | same | Basic |
| `Ring` | `ring.h` | Determinate arc plus indeterminate trim | same | Basic |
| `SeekBar` | `seekbar.h` | Continuous or stepped, optional ticks | same | Basic |
| `Text` | `text.h` | Four-role type primitive with wrap | same | Basic |
| `Icon` | `icon.h` | Tinted pixmap, optional RTL mirroring | same | — |

## Models and inputs

| Component | Header | Notes | Tests | Gallery |
| --- | --- | --- | --- | --- |
| `Model` | `model.h` | Owned `QVariantList` model; any registered type | `tests/components/tst_input_controls.cpp` | Pickers |
| `Adapter` | `model.h` | Non-owning bridge to any item model; formatting only | same | Pickers |
| `TextInput` | `input.h` | Filled field: floating label, helper/error line, clear button, password echo | same | Inputs |
| `EmailInput` / `TelephoneInput` / `RegexInput` | `input.h` | Validating derivatives; the regex one takes a `QRegularExpression` | same | Inputs |
| `NumberInput` | `input.h` | Range, step, error state, `valueChanged` | same | Inputs |
| `DropDown` | `input.h` | Menu popup with keyboard, Esc and outside-click dismissal | same | Pickers |
| `Select` | `input.h` | Single-choice field over a `DropDown` | same | Pickers |
| `Complete` | `input.h` | Text field + filtered suggestion list + adapter | same | Inputs |
| `ColourPicker` | `input.h` | SV panel, hue strip, alpha strip, hex field | same | Pickers |
| `NumberPicker` | `input.h` | Ordered numeric set, cyclic wheel | same | Pickers |
| `RingPicker` | `input.h` | Cyclic wheel over any `QVariant` through an adapter | same | Pickers |

## Dates, cascades and mentions

| Component | Header | Notes | Tests | Gallery |
| --- | --- | --- | --- | --- |
| `DatePicker` | `date_picker.h` | Six-week grid, month/year pages, min/max, keyboard | `tests/components/tst_advanced_inputs.cpp` | Dates |
| `DateTimePicker` | `datetime_picker.h` | Calendar plus painted hour/minute/second fields | same | Dates |
| `Cascader` | `cascader.h` | Hierarchical columns, path and leaf signals | same | Dates |
| `Mention` | `mention.h` | `@name` as one atomic node with capsule painting | same | Dates |

## Forms

| Component | Header | Notes | Tests | Gallery |
| --- | --- | --- | --- | --- |
| `Form` | `form.h` | Vertical form that validates its items | `tests/components/tst_form_controls.cpp` | Inputs |
| `FormItem` | `form.h` | Label, editor and rules; inline error | same | Inputs |
| `FormRule` | `form.h` | Required, regex, custom validators | same | Inputs |

## Display and navigation

| Component | Header | Notes | Tests | Gallery |
| --- | --- | --- | --- | --- |
| `Typography` | `typography.h` | All fifteen design-baseline type-scale roles | `tests/components/tst_display_controls.cpp` | Display |
| `List` | `list.h` | Model-backed rows, selection, keyboard, wheel | same | Display |
| `Recycler` | `recycler.h` | Virtualised rows with view-type pooling | same | Display |
| `PopUp` | `popup.h` | Anchored surface, placement flipping, outside/Esc dismissal | same | Display |
| `TabBar` / `Tab` | `tabbar.h`, `tab.h` | Fixed or scrollable tabs, sliding indicator | same | Display |
| `Banner` | `banner.h` | Title, description, two actions, close | same | Display |
| `GuideView` | `guide.h` | Scrim with a punched-out target and a step bubble | same | Display |
| `Marquee` | `marquee.h` | Scrolls only when the text overflows | same | Display |
| `ToolBar` | `toolbar.h` | Small top app bar with nav view and actions | `tests/layout/tst_layouts.cpp` | Feedback |
| `Badge` | `badge.h` | Count capped at `99+`, dot variant, follows a target widget | same | Feedback |
| `StateLayout` | `state.h` | Content/loading/empty/error/offline roles | same | Feedback |
| `Link` | `link.h` | Inline hyperlink with href, visited colour, elide and RTL; Enter/Space | `tests/components/tst_navigation_components.cpp` | Display |
| `Scrollbar` | `scrollbar.h` | Standalone horizontal/vertical scrollbar: thumb drag, track paging, wheel, keyboard | same | Display |

## Theme

| Component | Header | Notes | Tests | Gallery |
| --- | --- | --- | --- | --- |
| `Theme` attributes | `theme/theme.h` | `colourPrimary()`, `colourSecondary()`, `colourAccent()` and `colour("colorPrimary")`; `Theme::fromColours()` builds a whole theme from three seeds | `tests/components/tst_theme_android.cpp` | Theme |
| `ThemeSwitcher` | `theme_switcher.h` | Preset rows with three swatches; applies a `Theme` to the application theme and its `QPalette` | same | Theme |

## Media and content

| Component | Header | Notes | Tests | Gallery |
| --- | --- | --- | --- | --- |
| `Avatar` | `avatar.h` | Icon, image, pixmap, base64 or URL source; initials fallback; circle or square; ring | `tests/components/tst_media_basics.cpp` | Media |
| `Image` | `image.h` | Contain, cover or fill; dp corner radius; placeholder and error placeholder | same | Media |
| `Card` | `card.h` | Elevated, filled or outlined; media child and a trailing action row | same | Media |
| `Tag` | `tag.h` | Filled, tonal or outlined chip; selectable; close affordance; colour dot | same | Media |
| `Skeleton` | `skeleton.h` | Rect, circle or text line; shimmer only while visible and animating | same | Media |
| `Transfer` | `transfer.h` | Two panes, four move buttons, Ctrl/Shift multi-selection, independent scrolling | same | Media |

## Time and navigation

| Component | Header | Notes | Tests | Gallery |
| --- | --- | --- | --- | --- |
| `TimePicker` | `time_picker.h` | Cyclic wheel columns, drag and snap, 12/24-hour, optional seconds | `tests/components/tst_calendar_nav.cpp` | Time |
| `Calendar` / `CalendarCell` | `calendar.h` | Six weeks, Monday first; marked dates; days/months/years pages; keyboard | same | Time |
| `Carousel` | `carousel.h` | Sliding transition, loop, autoplay and clickable indicators | same | Time |
| `Pagination` | `pagination.h` | First/previous/window/next/last with a compressed page window | same | Time |
| `Timeline` | `timeline.h` | Rail from the first marker to the last; scrolls when it overflows | same | Time |
| `Affix` | `affix.h` | Sticks past a threshold, raises elevation and reports `stuckChanged` | same | Time |
| `Anchor` | `anchor.h` | Highlights the anchor at the scroll offset; activation is a separate signal | same | Time |

## Feedback and overlays

| Component | Header | Notes | Tests | Gallery |
| --- | --- | --- | --- | --- |
| `Menu` | `menu.h` | Anchored panel, nested submenus, checkable items, keyboard and outside-click dismissal | `tests/components/tst_feedback_dialog.cpp` | Feedback |
| `Alert` | `alert.h` | Inline banner in four severities, optional action and close | same | Feedback |
| `Notification` | `notification.h` | Child-widget snackbar with a queue and timed dismissal | same | Feedback |
| `Dialog` family | `dialog.h` | Confirm, Message, SingleChoice, MultipleChoice and Custom share one scrim | same | Feedback |
| `Language` | `language.h` | Ten common languages in a menu; the host installs the translator | same | Feedback |

## Data, virtual data and playback

| Component | Header | Notes | Tests | Gallery |
| --- | --- | --- | --- | --- |
| `Table` | `table.h` | `QAbstractItemModel` table: headers, row separators, hover and selection, wheel and keyboard, only visible rows painted | `tests/components/tst_table_video.cpp` | Data |
| `VirtualTable` | `virtual_table.h` | `VirtualTableSource` table: viewport + prefetch queries, frozen columns, 10k rows without materialising them | same | Data |
| `Video` | `video.h` | Self-painted player surface: frames pushed or pulled, transport, seek, volume, loop; decoding stays with the host | same | Data |
| `TreeSelect` | `tree_select.h` | Tree field over an item model and an Adapter; a branch expands and is never selectable, only leaves select | `tests/components/tst_select_tree.cpp` | Selects |
| `VirtualTreeSelect` | `virtual_tree_select.h` | The same rules over `VirtualTreeSource`: children are queried only for expanded branches | same | Selects |
| `VirtualSelect` | `virtual_select.h` | `VirtualSource` list with a windowed popup, draggable scrollbar and optional label filtering | same | Selects |

## Navigation and pages

| Component | Header | Notes | Tests | Gallery |
| --- | --- | --- | --- | --- |
| `Drawer` | `drawer.h` | Modal (scrim, slide-in, Esc/scrim dismiss) or standard panel; header, sections, items with badge and icon; `itemActivated` | `tests/components/tst_drawer_pager.cpp` | Navigation |
| `Pager` | `pager.h` | Draggable page container with snapping, loop, clickable indicator and a motion-token transition; one visible page at rest | same | Navigation |
| `MenuItem` | `menu_item.h` | Navigation row with icon, badge, shortcut, indent, selection pill; RTL | `tests/components/tst_navigation_components.cpp` | Overlays |
| `SubMenu` | `sub_menu.h` | Expandable menu row with animated height, indented children and `childActivated` | same | Overlays |
| `MenuGroup` | `menu_group.h` | Titled group of menu rows, single selection (optionally model-backed), keyboard navigation | same | Overlays |
| `NavigationView` | `navigation_view.h` | Header, sections, dividers and icon/badge rows; owns the selection, reports activation, scrolls and mirrors in RTL | `tests/components/tst_navigation_view.cpp` | Navigation |
| `Page` | `page.h` | Header (title, subtitle, back, actions), scrollable body and optional footer; children are reparented but not owned | same | Navigation |

## Configuration and stores

| Component | Header | Notes | Tests | Gallery |
| --- | --- | --- | --- | --- |
| `Store` | `md/store/store.h` | Process-wide registry of named stores (`Store::open("md.theme")` always returns the same object), value change signals | `store/tests/tst_store.cpp` | - |
| `Font` | `font.h` | Font configuration container/scope: family and scale for a subtree; `installFromStore()` follows the `md.font` store | `tests/components/tst_config_containers.cpp` | Display |
| `Language` | `language.h` | Language selector that is also a container: `installOn(host)`, `translate()`, `layoutDirectionFor()`, `installFromStore()` | same | Overlays |
| `Theme` | `theme_scope.h` | Theme scope container; adapters derive from the inherited theme, `installFromStore()`/`publishTheme()` use the `md.theme` store | same | Theme |
| `Tooltip` | `tooltip.h` | Non-interactive anchored label in the Tooltip layer: delay, Above/Below flip, screen clamping, `showText()` | `tests/components/tst_tooltip.cpp` | Overlays |

Every `md::View` can bind one or more stores: `bindStore("md.theme")`,
`bindStore(&store)`, `unbindStore()`. The framework applies the `theme` key by
installing a ThemeScope on the view and lets subclasses extend `onStoreChanged()`
(`Text`/`Typography` apply `font.family` and `font.scale`).

## Popup layers

| Component | Header | Notes | Tests | Gallery |
| --- | --- | --- | --- | --- |
| `Dropdown` | `dropdown.h` | Anchored popup layer: below/above/left/right placement with a 4 dp gap, screen clamping, anchor tracking, outside-click/Escape dismissal, `popupGlobalRect()`; positions itself, no Affix dependency | `tests/components/tst_dropdown_modal.cpp` | Overlays |
| `Modal` | `modal.h` | Full-window scrim with configurable dim plus a per-window layer registry (`Content`, `Dropdown`, `Drawer`, `Dialog`, `Notification`, `Tooltip`); `restack()` keeps higher levels above lower ones | same | Overlays |

## Containers

| Component | Header | Notes | Tests | Gallery |
| --- | --- | --- | --- | --- |
| `Linear` | `layout/linear.h` | Horizontal or vertical, weights, gravity, spacing, RTL | `tests/layout/tst_layouts.cpp` | Layouts |
| `Frame` | `layout/frame.h` | Stacked children with nine-way gravity | same | Layouts |
| `Constraint` | `layout/constraint.h` | Parent anchors, stretching, centring, RTL | same | Layouts |
| `AppBar` | `layout/appbar.h` | Collapsing bar, elevation follows scroll | same | Layouts |
| `Collapse` | `layout/collapse.h` | One expandable section with an animated height | same | Layouts |
| `Coordinator` | `layout/coordinator.h` | Nested-scroll behaviours attached to children | same | Layouts |

## Visualisation and time

| Component | Header | Notes | Tests | Gallery |
| --- | --- | --- | --- | --- |
| `Clock` | `clock.h` | Analog or digital dial, explicit zones, frozen instants | `tests/components/tst_clock.cpp` | Charts |
| `Schedule` | `schedule.h` | Day/week agenda, overlap lanes, drag and keyboard moves | `tests/components/tst_schedule.cpp` | Charts |
| `Scheduler` | `scheduler.h` | Task-queue lanes, pending counts and batch-budget controls | `tests/components/tst_scheduler.cpp` | Charts |
| `Canvas` | `canvas.h` | Hosts 1D, 2D and 3D drawing contexts | `tests/components/tst_canvas.cpp` | Charts |
| `MathematicsCoordination` | `math_coordination.h` | Data-to-pixel transform, nice ticks, auto-fit and navigation | `tests/components/tst_math_coordination.cpp` | Charts |
| `LineChart` | `line_chart.h` | Multi-series lines, smooth curves, area fills, markers | `tests/components/tst_line_chart.cpp` | Charts |
| `ScatterChart` | `scatter_chart.h` | Multi-series circle, square or diamond markers with hover | `tests/components/tst_scatter_chart.cpp` | Charts |
| `PieChart` | `pie_chart.h` | Slices, donut mode, percentages and reveal animation | `tests/components/tst_pie_chart.cpp` | Charts |
| `BarChart` | `bar_chart.h` | Grouped or stacked bars, horizontal mode, value labels | `tests/components/tst_bar_chart.cpp` | Charts |
| `HistogramChart` | `histogram_chart.h` | Automatic bins, density and cumulative modes | `tests/components/tst_histogram_chart.cpp` | Charts |
| `BoxPlotChart` | `box_plot_chart.h` | Five-number boxes, whiskers, outliers and hover | `tests/components/tst_box_plot_chart.cpp` | Charts |
| `HeatmapChart` | `heatmap_chart.h` | Themed matrix cells, empty hatching, hover highlight | `tests/components/tst_heatmap_chart.cpp` | Charts |
| `RadarChart` | `radar_chart.h` | Polar axes with polygon series and range clamping | `tests/components/tst_radar_chart.cpp` | Charts |
| `FunnelChart` | `funnel_chart.h` | Stages as trapezoids with labels and percentages | `tests/components/tst_funnel_chart.cpp` | Charts |

## Conventions worth knowing

- A component implements `measureOverride()` and `paintContent()`; `paintEvent()`
  is `final`, so the state layer, ripple and focus ring cannot be bypassed.
- A container implements `measureOverride()` and `arrangeChildren()`, and calls
  `child->arrange()` rather than `setGeometry()`. `View::resizeEvent()` also
  re-arranges, so a container placed in an ordinary Qt layout still places its
  children.
- The pipeline no longer paints a contour around every view. The design baseline outlines
  only the surfaces that ask for one; `layer::OutlineLayer` remains available to
  a component that draws it itself (an outlined button, a card).
- The gallery is the visual counterpart of these tests: if a page renders and
  reacts, the public API is wired end to end.
- Localized literals go through `View::localized(key, fallback)`, which reads the
  `strings` map of the `md.language` store. Components use it for empty states
  and meridian labels; a `Language` container publishes the map from its
  `Adapter::strings()`.
