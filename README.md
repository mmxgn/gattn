<p align="center"><img src="data/icons/hicolor/scalable/apps/org.mmxgn.gattn.svg" width="96" alt="gattn logo"></p>

# gattn

Attention-grabbing session manager for agentic programming on GNOME.

Inspired by [attn](https://github.com/victorarias/attn).

Monitors multiple Claude Code sessions in a sidebar, highlights when one needs your attention, and lets you switch between them without leaving the keyboard.

![screenshot](img/screenshot.png)

## Features

### Attention grabbing

Session dots in the sidebar change colour when a session changes state — idle, working, needs input, or done. When gattn is not in focus, a desktop notification fires so you know exactly which session (or shell process) needs you, without watching a terminal.

### Keyboard-only navigation

Everything is reachable without a mouse:

| Shortcut | Action |
|---|---|
| `Ctrl+N` | New session |
| `Ctrl+Shift+W` | Close session |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | Next / previous session |
| `Ctrl+↑` / `Ctrl+↓` | Next / previous session |
| `Ctrl+←` / `Ctrl+→` | Focus sidebar / terminal |
| `Ctrl+PgUp` / `Ctrl+PgDn` | Jump to first / last session |
| `Ctrl+Shift+A` | Jump to next unattended session |
| `F2` | Rename session |
| `Ctrl+F` | Search / filter sessions |
| `Ctrl+G` | Toggle grid view |
| `Ctrl+Shift+D` | Show diff |
| `Ctrl++` / `Ctrl+-` / `Ctrl+0` | Zoom in / out / reset terminal font |
| `F11` | Fullscreen |

### Grid view

![grid view](img/grid.png)

`Ctrl+G` switches to a tiled overview of all open sessions at once, so you can spot activity across many agents at a glance.

### Diff

`Ctrl+Shift+D` (or the git icon in the sidebar) runs `git diff HEAD` in the session's live working directory and shows the result with syntax highlighting — red for deletions, green for additions.

![diff view](img/diff.png)

<!-- TODO: explain / reuse buttons — open the diff in Claude to get an explanation or paste it into a new session -->

## Install

Grab the latest asset from the [releases page](https://github.com/mmxgn/gattn/releases/latest).

**AppImage**
```sh
chmod +x gattn-*.AppImage && ./gattn-*.AppImage
```

**Ubuntu / Debian**
```sh
sudo apt install ./gattn_*.deb
```

**Fedora**
```sh
sudo dnf install ./gattn-*.rpm
```

**Nix**
```sh
nix run github:mmxgn/gattn
```

## Develop

```sh
nix develop
meson setup build && ninja -C build && ./build/gattn
```

## Requirements

GTK 4 · libadwaita · VTE (gtk4 variant) · GtkSourceView 5
