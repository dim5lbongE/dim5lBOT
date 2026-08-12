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
