<div align="center">

# Agent Prompt Panel

**Your next agent session starts in the Xfce panel.**

`xfce4-agent-prompt-panel-plugin` · Native GTK3 · Keyboard-first · Terminal-powered

[Install](#installation) · [Shortcuts](#keyboard-shortcuts) · [Settings](#settings) · [Troubleshooting](#troubleshooting) · [Development](#development)

</div>

---

Type a prompt, press **Enter**, and continue in your preferred terminal.
Press **Tab** to switch between Copilot and Junie, or **Up**/**Down** to browse
recent sessions for the selected assistant.
No search button, embedded chat window, or extra prompt history.

```text
Xfce panel
+--------------------------------------+
| Ask Copilot...                        |
+--------------------------------------+
       |
       +-- Tab ---------------> Switch between Copilot and Junie
       +-- Enter with text --> New session with your prompt
       +-- Empty Enter ------> Fresh interactive session
       +-- Up / Down --------> Selected assistant's sessions --> Enter to resume
```

## Highlights

| Feature | What it does |
| --- | --- |
| Instant prompting | Launches interactive Copilot or Junie from a single panel text field. |
| Recent sessions | Shows up to 20 local sessions for the selected assistant, with titles, project folders, and last-used times. |
| Preferred terminal | Uses your Xfce terminal preference, including Terminator. |
| Independent windows | Launches Terminator with `--no-dbus`, even while another session is open. |
| Project-aware resume | Opens a saved session in its own project directory. |
| Minimal footprint | Stores only the project folder, both CLI paths, and field width. |

## Installation

The project is currently installed **from source**:

```sh
git clone https://github.com/LoveEnforcementAgency/xfce4-agent-prompt-panel-plugin.git
```

The command above checks out the project into a directory named
`xfce4-agent-prompt-panel-plugin`.

### 1. Install dependencies

Requires **Xfce panel 4.18+**, **GTK 3.24+**, libyaml, JSON-GLib, a C compiler,
CMake, and a configured terminal. The default build includes tests.

<details>
<summary><strong>Arch Linux / Manjaro</strong></summary>

```sh
sudo pacman -S --needed base-devel cmake ninja pkgconf \
  xfce4-panel gtk3 libyaml json-glib exo terminator
```

</details>

<details>
<summary><strong>Debian / Ubuntu</strong></summary>

```sh
sudo apt update
sudo apt install build-essential cmake ninja-build pkg-config \
  libxfce4panel-2.0-dev libgtk-3-dev libyaml-dev \
  libjson-glib-dev exo-utils terminator
```

</details>

Terminator is optional if you already have another preferred terminal.
Install the CLI for each assistant you want to use:
[GitHub Copilot CLI](https://github.com/github/copilot-cli), Junie CLI, or both.
Make its executable available on `PATH` or configure its path in Properties.
You do not need both CLIs installed. The Copilot integration was tested with
CLI **1.0.114**.

### 2. Build and install

Run these commands from the downloaded source folder:

```sh
cd xfce4-agent-prompt-panel-plugin

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local" \
  -DCMAKE_INSTALL_LIBDIR=lib

cmake --build build
cmake --install build
```

The plugin installs for **your user only**, without `sudo`:

```text
~/.local/
  lib/xfce4/panel/plugins/libcopilot.so
  share/xfce4/panel/plugins/copilot.desktop
```

> [!IMPORTANT]
> User-local plugin discovery works on Xfce 4.20. Older panels or distributions
> without this support may require installation to their system prefix and
> library directory instead.

### 3. Authenticate and add the panel item

Authenticate each CLI using its normal login flow; for Copilot, run:

```sh
copilot login
xfce4-panel --restart
xfce4-panel --add=copilot
```

Alternatively, open **Panel Preferences → Items → Add → Agent Prompt Panel**.
The internal plugin ID remains `copilot`.

To use Terminator, select it in
**Xfce Preferred Applications → Utilities → Terminal Emulator**.

## Keyboard shortcuts

| Where | Key | Action |
| --- | --- | --- |
| Prompt field, with text | **Enter** | Opens a session with the selected assistant and submits the prompt. |
| Empty prompt field | **Enter** | Opens a fresh session with the selected assistant. |
| Prompt field | **Tab / Shift+Tab** | Switches between Copilot and Junie, preserving any draft. |
| Prompt field | **Up / Down** | Opens recent sessions for the selected assistant, preserving any draft. |
| Session dropdown | **Up / Down** | Moves between sessions. |
| Session dropdown | **Enter** | Resumes the selected session in a terminal. |
| Session dropdown | **Escape** | Closes the dropdown and returns focus to the field. |

The session list loads asynchronously and refreshes whenever it opens.
Copilot mode shows only Copilot sessions; Junie mode shows only Junie sessions.
Switching assistants cancels any pending session load.
Sessions whose project folders no longer exist are visible but disabled.
The dropdown opens at its populated size and can flip above a bottom panel.

Each terminal session is independent of the plugin. Closing or restarting the
panel does **not** close your terminals.

## Settings

Right-click the field and choose **Properties**.

| Setting | Purpose |
| --- | --- |
| Project folder | Working directory for new sessions; defaults to your home folder. |
| Copilot executable | Program name or executable path, such as `copilot`; not a shell command with arguments. |
| Junie executable | Program name or executable path, such as `junie`; not a shell command with arguments. |
| Panel field width | Horizontal width in pixels, from 120 to 600. |

On vertical panels, the text field stays available and uses the panel width.
Resuming a session uses that session's directory, not the configured folder for
new sessions.
Changing a CLI path checks that it is executable. An unchanged, uninstalled CLI
does not prevent saving other settings; attempting to launch it shows an error.

## Permissions and session history

Copilot and Junie run with their **normal interactive permissions and authentication**.
The plugin adds no blanket approvals. Saved CLI permissions still apply,
and prompts are passed as literal arguments rather than evaluated as shell code.

The plugin does not save prompts or read conversation contents. The session
picker reads Copilot workspace metadata and history-file modification times, and
Junie session IDs, task names, project folders, and timestamps from these local indexes:

```text
$COPILOT_HOME/session-state
# Default when COPILOT_HOME is unset: ~/.copilot/session-state
```

```text
$JUNIE_HOME/sessions/index.jsonl
# Default when JUNIE_HOME is unset: ~/.junie/sessions/index.jsonl
```

> [!NOTE]
> Session metadata is an internal CLI format, not a documented listing API.
> Future CLI changes may require a plugin update. Remote-only sessions are not
> listed, and saved sessions are never modified by the picker.

Each CLI retains its own session history. Prompts and project context read by
the selected assistant are sent to its configured model provider; normal usage
charges and data policies apply.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| Plugin is missing from Add New Items | Restart the panel; check the Xfce version and user-local discovery support. |
| A warning icon appears after Enter | Hover over it for the error. The prompt is preserved so you can retry. |
| Copilot cannot be found | Check `command -v copilot` and the executable in **Properties**. |
| A terminal opens but Copilot needs authentication | Run `copilot login` in a terminal. |
| The wrong terminal opens | Check Xfce's **Preferred Applications → Terminal Emulator**. |
| No recent sessions appear | Check `COPILOT_HOME` or `JUNIE_HOME` and local history. Empty Copilot history files and invalid index records are skipped. |
| A session cannot be selected | Its saved project folder may no longer exist. |

The field clears when the terminal launcher starts successfully. Errors occurring
later inside the terminal, such as authentication failures, are handled by the CLI.

## Development

### Preview without changing your panel

```sh
./build/agent-panel-preview --directory /absolute/path/to/project
```

Use `--vertical` for a vertical layout, `--executable /path/to/copilot` to choose
the CLI, or `--config /path/to/settings.rc` for persistent preview settings.
The preview executable is not installed.

### Run tests

```sh
ctest --test-dir build --output-on-failure
```

Tests cover native panel focus, field-only UI, Properties, literal terminal
arguments, independent windows, launch-error recovery, settings persistence,
YAML and JSONL metadata, provider isolation, mode-switch cancellation, dropdown
sizing, keyboard navigation, and session resume.

Terminal integration tests launch a **fake CLI in your configured terminal**;
they may briefly open windows but make no model calls. GTK tests are skipped
without a display. If Xvfb and the terminal dependencies are installed:

```sh
xvfb-run -a ctest --test-dir build --output-on-failure
```

For a build without tests, configure with `-DBUILD_TESTING=OFF`.
JSON-GLib remains required for reading Junie session metadata.

### Source layout

```text
src/
  assistant.[ch]           Assistant names, history locations, and CLI arguments
  sessions.[ch]            Shared session model, sorting, and provider dispatch
  providers/
    copilot-sessions.c    Copilot workspace YAML reader
    junie-sessions.c      Junie session-index JSONL reader
  terminal.[ch]            Preferred-terminal launching and argument quoting
  agent-ui.[ch]            GTK field, session dropdown, and Properties
  agent-plugin.c           Xfce panel integration
  preview.c                Standalone development preview
tests/
  fake-assistant.c         Fake CLI for terminal integration tests
  test-assistant.c         CLI argument contracts
  test-copilot-sessions.c  Copilot metadata tests
  test-junie-sessions.c    Junie metadata tests
  test-ui.c                GTK and terminal integration tests
  test-panel.c             Native panel integration tests
data/
  agent.desktop.in         Panel plugin metadata
```

The internal `copilot` plugin ID, installed `libcopilot.so` and `copilot.desktop`
filenames, and `Copilot` settings group are retained for compatibility with
existing panel items. Source files and build targets use assistant-neutral names;
Copilot-specific session readers retain their provider name.

## Uninstall

First remove **Agent Prompt Panel** from the panel, then delete its installed files:

```sh
rm "$HOME/.local/lib/xfce4/panel/plugins/libcopilot.so"
rm "$HOME/.local/share/xfce4/panel/plugins/copilot.desktop"
```

This does not delete either assistant's session history.

## Licence

Licensed under the **European Union Public Licence v. 1.2 (EUPL-1.2)**.
See [LICENSE.md](LICENSE.md) for the full licence text.

---

<div align="center">

**Love Enforcement Agency**

*In Love We Trust.*

</div>
