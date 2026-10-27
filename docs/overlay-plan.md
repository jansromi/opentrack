# Status overlay — implementation plan

An on-screen overlay, like an FPS counter, showing the current axis values and
the modifiers acting on them (precision mode, detents, hard stops, analog
joystick control, Tail/Up View, freeze/zero). The target is Windows; most of the
work is platform-neutral and can be built on macOS.

The main use is a glanceable panel on a **secondary monitor** that isn't
running the sim. Putting it on the sim's monitor is supported but secondary.

## Goals

- Small read-only panel to glance at, placed on any monitor (typically a
  secondary one next to the sim). It stays on top of whatever else is on that
  monitor.
- Shows per-axis output and the active modifiers, with low overhead
  (~10–20 Hz refresh).
- Works on any monitor in a multi-monitor setup and is placed by monitor plus
  anchor and offset.
- Never takes focus or mouse input while the game is running.

## Non-goals

- Drawing inside exclusive-fullscreen DirectX/Vulkan games. That needs
  present-hook injection like RTSS or the Steam overlay. When the overlay is on
  the sim's monitor, the sim must run borderless windowed.
- VR headset overlays (to be revisited if needed).

## Visual design

Chosen layout: active modifiers on top, axis table below (mockup:
https://claude.ai/artifact/4p1GJKXixxzAL4M3GQCKYR).

- Dark translucent panel, monospace font, tabular digits.
- **Modifier list**: one per line in larger type, colour-coded with a left
  border. Shows only names and changing values, never explanatory text:
  `FROZEN`, `PRECISION ×0.35`, `TAIL VIEW ◄` / `UP VIEW`, `JOYSTICK YAW`,
  `Z DETENT 30 · 0.4s` (time left only when a pass delay applies),
  `Z HARD STOP 45`. A dimmed `TRACKING` when nothing is active.
- **Axis table**: axis name, value, unit, source (`TRK` / `KEY` / `JOY` /
  `TRK+KEY` / `TRK+JOY`), and a short marker (`◆ DET 30`, `■ STOP 45`,
  `×0.35`, `TAIL ◄`). An affected value takes its modifier's colour.
- Colours: precision cyan, detent amber, hard stop red, joystick violet,
  Tail/Up View green, frozen light grey.
- Text size is a setting (S / M / L / XL).

## Architecture

```
pipeline thread (logic())            GUI thread
  └─ fills pipeline_status  ──mtx──►  QTimer (50–100 ms)
                                       └─ status_overlay::update(status)
                                            └─ QPainter on a translucent
                                               top-level window
```

### 1. `pipeline_status` snapshot (logic/, platform-neutral)

A plain struct filled at the end of each `pipeline::logic()` tick and copied
out under `mtx`, the same pattern as `raw_and_mapped_pose()`.

```cpp
struct axis_status {
    double raw, mapped;                 // m_raw_6dof / m_output_pose
    translation_control_mode mode;      // tracked / keys / analog / tracked+…
    bool manual_keys_held;              // a + or − key is held right now
    bool analog_active;                 // joystick axis is driving it
    bool on_detent;                     // detent_state.active
    double detent_value;
    double detent_remaining;            // delay - held_time, <0 if n/a
    bool press_to_pass;
    bool hard_stopped;                  // clamped by apply_hard_stops()
    double hard_stop_value;
};

struct pipeline_status {
    std::array<axis_status, 6> axes;
    bool enabled, zeroed, frozen;
    bool precision_held, precision_toggled;
    int  tailview;                      // -1 left, 0 none, 1 right
    bool upview;
    bool analog_device_present;
};
```

Work items:
- `manual_translation`: expose per-axis detent and hard-stop state.
  `apply_hard_stops()` needs to report when it clamped (return or out-param).
- `pipeline`: add `pipeline_status status() const`, filled in `logic()`.
- Keep this free of Qt widgets so it can be unit-tested.

### 2. `status_overlay` window (gui/ or a new `overlay/` module)

- A `QWidget` with `Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
  Qt::Tool | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus`,
  plus `WA_TranslucentBackground`, `WA_ShowWithoutActivating` and
  `WA_TransparentForMouseEvents`.
- Custom `paintEvent` using `QPainter`: semi-transparent rounded background,
  monospace font, colour-coded modifier tags.
- Owned by the main window (`opentrack/main-window.cpp`), started and stopped
  with tracking (optionally also shown while not tracking).
- A refresh timer reads `pipeline::status()` and calls `update()` only when the
  status changed or a value moved by more than a threshold.

### 3. Multi-monitor placement

Settings:
- **Monitor**: dropdown from `QGuiApplication::screens()`, labelled
  `N: <manufacturer> <model> (WxH)`, plus an *Identify* button that briefly
  shows a big number on each screen.
- **Anchor**: one of 9 positions (corners, edge middles, center).
- **Offset X/Y**: in pixels, relative to the anchor.

Identifying a monitor: save `QScreen::name()`, `manufacturer()`, `model()` and
`serialNumber()`, not the index, because Windows reorders monitors after a
reboot or replug. Match in this order:
1. same serial number (if the monitor reports one)
2. same manufacturer and model
3. same name (e.g. `\\.\DISPLAY2`)
4. fall back to the primary monitor, keeping the saved choice so the overlay
   moves back when that monitor reappears.

Positioning:
- Compute from `QScreen::geometry()`, not `availableGeometry()`, because a game
  covers the taskbar.
- Call `setScreen()` before `show()`, then `move()`, to avoid Windows
  misplacing windows that cross monitors with different scaling.
- Re-match and reposition on `screenAdded`, `screenRemoved`,
  `primaryScreenChanged` and `QScreen::geometryChanged`.
- Different scaling per monitor: Qt 6 handles this on Windows by default.
  Offsets are in the monitor's own pixels and font size is a setting.

Optional **place mode**: temporarily drop `WindowTransparentForInput` so the
overlay can be dragged; on release, snap to the nearest anchor on the current
monitor and update the settings.

### 4. Settings

New `overlay_settings` bundle (in `logic/main-settings.hpp` or a new bundle):
- enabled, show only while tracking
- monitor identity (name, manufacturer, model, serial), anchor, offset X/Y
- font size, opacity, background on/off
- which items to show (axis rows, modifier line, raw values, per-axis
  visibility)
- refresh interval
- toggle hotkey (bound through the existing keybinding worker, keyboard or
  joystick)

New *Overlay* tab in `gui/options-dialog.*`.

### 5. Windows-specific pass

- Check it stays visible on the secondary monitor while the sim has focus on
  the primary one (the main use case).
- Secondary: check it on top of a borderless sim on the same monitor. If the
  game knocks the overlay off the top when it takes focus, re-assert
  with `SetWindowPos(HWND_TOPMOST, …, SWP_NOACTIVATE)` on a timer or on
  `WM_WINDOWPOSCHANGING`.
- Check the analog joystick row with real `win32_joy_ctx` data.
- Check a mixed-scaling setup (e.g. 100% and 150%).
- Make sure the overlay never shows up in the taskbar or Alt-Tab.

## What can be tested where

| Area | macOS | Windows |
|---|---|---|
| Status snapshot, unit tests | ✅ | ✅ |
| Overlay drawing and layout | ✅ | ✅ |
| Monitor list, matching, fallback, hotplug | ✅ (with an external display) | ✅ |
| Analog joystick indicator | debug fake only (`#ifdef _WIN32`) | ✅ |
| Different scaling per monitor | partly | ✅ |
| Visible on the secondary monitor while the sim has focus | ✅ (any app) | ✅ |
| On top of a borderless game on the same monitor | — | ✅ |

A debug setting or env var (`OPENTRACK_OVERLAY_FAKE_STATUS=1`) feeds a
cycling fake `pipeline_status`, so every state can be seen on macOS.

## Milestones

1. ~~**Visual design**: settle the layout, density, colours and tags (mockups).~~
   Done, see *Visual design*.
2. **Status snapshot**: `pipeline_status` plus the manual_translation and
   hard-stop reporting.
3. **Overlay window**: drawing, refresh timer, show/hide with tracking, fake
   status mode.
4. **Placement**: monitor matching, anchor and offset, hotplug, optional place
   mode.
5. **Settings UI**: Overlay tab, toggle hotkey.
6. **Windows pass**: on-top behaviour over games, joystick row, scaling, README
   entry.

## Open questions

- Source column: always shown, or only for axes that aren't plain `TRK`?
- Row markers: keep them, or rely on the coloured value since the modifier
  list already names the state?
- Hide rows for disabled axes?
- Show raw (pre-mapping) values alongside the output values?
- Hide the overlay automatically when nothing is active?
- VR: needed later? (It would need a different approach, e.g. an OpenVR
  overlay.)
