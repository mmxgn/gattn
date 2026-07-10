# Accessibility testing

Quick manual pass for what a screen-reader / keyboard-only user experiences.

## 1. Set up Orca

```sh
nix-shell -p orca --run 'orca &'
```

Toggle speech with `Super+Alt+S`.

## 2. Screen-reader smoke test

Launch `./build/gattn` from another shell. With Orca on:

- **Row announcement**: focus the sidebar list, arrow-down through rows.
  Expected: `<name>, <state>, <cwd-basename>` per row.
- **State-change**: kick a claude session; when it stops working, Orca
  should say `<name> needs input` even without focus.
- **Icon buttons**: Tab into the row's inline buttons. Tooltips
  (`Open folder`, `Open shell here`, `Show diff`, `Close session`)
  should be spoken.
- **Search bar**: `Ctrl+F`, type — placeholder + echoes should be spoken.

## 3. Keyboard-only

Disable the mouse (`xinput disable <id>`) and drive the app entirely
from the keyboard:

- `Ctrl+N` new, `Ctrl+Shift+W` close, `Ctrl+Tab` / `Ctrl+Down` next
- `Ctrl+Shift+A` next-unattended, `Ctrl+Shift+D` diff, `F2` rename
- `Ctrl+F` search, `Ctrl+G` grid, `Esc` closes dialogs / exits grid

Anything reachable only by clicking is an a11y bug.

## 4. Focus visibility

Tab through every focusable widget. The focus ring must be visible on
each. Adwaita provides this by default; note regressions.

## 5. Contrast

- Switch light / dark via GNOME Settings → Appearance. State-color
  icons (gray/green/amber/blue) must remain distinguishable in both.
- Turn on Settings → Accessibility → High Contrast. Sidebar + terminal
  should stay usable.

## 6. AT-SPI inspection

```sh
nix-shell -p accerciser --run accerciser
```

Click into gattn's window. Each sidebar row should have a `LABEL`
property with the composed text; the state icon should have its state
label; roles should be `list item` / `button` / `list`.

## 7. Log findings

Any gap becomes a `TODO a11y:` in `BUGS.org`. That's the ledger for
the next pass.
