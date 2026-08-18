# v1.1.0

- Fixed completed levels being mistaken for deaths and clearing the finished recording.
- Fixed pause transitions incorrectly discarding an active recording; deletion now occurs only when a real player death triggers a level reset.
- Fixed Speedhack resetting to 1.0x after the player dies or restarts the level.
- Speedhack now resets only when leaving the level.
- Added macOS support and macOS CI packaging.
- Reworked macro timing to match xdBot's command-tick order.
- Added xdBot-style Input Fixes that capture and restore P1/P2 position and rotation at each input.
- Added duplicate command-tick protection to prevent replay inputs from being consumed twice.
- Upgraded replay JSON to format version 2 while retaining version 1 loading compatibility.

# v1.0.0

- Fixed pause transitions incorrectly deleting an active normal-mode recording.
- A recording is now discarded only after the next game update confirms an actual player death.
- Replay inputs are now injected before the matching 240 TPS physics tick.
- Frame conversion now rounds to the nearest tick to reduce floating-point drift.

# v0.3.1

- Replaced fixed Speedhack presets with a custom multiplier input.
- Speedhack now accepts values from 0.1x to 10.0x.

# v0.3.0

- Added a gameplay tools menu.
- Added Speedhack presets: 0.5x, 1.0x, 1.5x, and 2.0x.
- Added a Noclip toggle.
- Safe Mode now blocks completions assisted by replay, Speedhack, or Noclip.
- Speedhack and Noclip automatically reset when leaving a level.

# v0.2.2

- Fixed Safe Mode becoming inactive after the last replay input was processed.
- Added a dim5lBOT button to the level-complete screen.

# v0.2.1

- Fixed a critical bug that discarded normal-mode recordings when the game was paused.
- Normal-mode recordings are now discarded only on an unpaused player death.

# v0.2.0-beta.11

- Added the official dim5lBOT mod icon.

# v0.2.0-beta.10

- Added Safe Mode, enabled by default, to block replay-assisted level completions.
- Added a Safe Mode toggle and warning state to the replay menu.
- CI now tests one Windows build and one Android64 build before universal packaging.

# v0.2.0-beta.9

- Packaged Windows, Android32, and Android64 into one universal Geode mod.
- Prepared a single artifact suitable for Geode Index submission.

# v0.2.0-beta.8

- Added Android 32-bit and 64-bit support.
- Normal-mode recordings are now discarded completely when the player dies.
- Practice-mode recordings still rewind to the active checkpoint.

# v0.2.0-beta.7

- Replaced the small two-step X control with a clear one-click Delete button.

# v0.2.0-beta.6

- Replays can now be saved under a chosen name.
- Added a paginated replay browser for loading multiple saved macros.
- Added overwrite confirmation and two-step deletion for saved replays.

# v0.2.0-beta.5

- Reworked playback to use xdBot's `GJBaseGameLayer::processCommands` method.
- Recording and playback now share Geometry Dash's 240 TPS level-time frame clock.

# v0.2.0-beta.4

- Play now closes the pause layer so replay frames can advance.

# v0.2.0-beta.3

- Fixed gameplay inputs not being recorded on Geometry Dash 2.2081.
- Practice respawns now discard inputs recorded after the active checkpoint.

# v0.2.0-beta.2

- Fixed replay control buttons being clipped outside the popup.

# v0.2.0-beta.1

- Added frame-based P1/P2 input recording.
- Added macro replay with direct-input blocking.
- Added JSON saving and loading.
- Added a custom dim5lBOT control panel.
