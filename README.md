# FlatOut 2 split-screen for Nucleus Co-op

!!! I'm still ironing out kinks so take this guide with a grain of salt until end of October!!!

Play FlatOut 2 with up to 4 players on one PC and one screen: [Nucleus Co-op](https://github.com/SplitScreen-Me/splitscreenme-nucleus) runs one game window per player, and the windows race each other over the game's own LAN mode.

What this handler adds on top of plain Nucleus:

- **Every controller drives its own window, without clicking on it.** Windows only gives DirectInput controller data from Xbox One/Series pads to the window that is in front, and FlatOut 2 only uses DirectInput. A small replacement `dinput8.dll` ("fo2pad") gives each window one virtual pad read through XInput, which works in the background.
- **Menus work with the controller.** FlatOut 2's menus are keyboard-only on PC; fo2pad turns the pad into key presses for each window.
- **LAN between the windows works.** FlatOut uses fixed network ports, ignores loopback addresses and its own MAC address; fo2pad gives each window its own virtual LAN address and MAC.
- Throttle on RT, brake on LT, nitro on A, music muted (all adjustable in the handler options).

## Requirements

- **FlatOut 2 v1.2 from GOG** (the original 2006 game; `FlatOut2.exe` is 2,990,080 bytes). The current Steam version is not supported.
- [Nucleus Co-op](https://github.com/SplitScreen-Me/splitscreenme-nucleus/releases) 2.4.1 or newer.
- One Xbox (XInput) controller per window. The keyboard can be used for one window.
- Start FlatOut 2 normally once before using the handler, so it creates its settings with a resolution your screen supports.

## Install

1. Download `FlatOut 2 (Nucleus handler).nc` from the [Releases](../../releases) page.
2. Drag the `.nc` file onto the Nucleus Co-op window (or use its "extract handler" button) and add the game by selecting `FlatOut2.exe`.

Manual install: copy `handlers/FlatOut 2.js` and the `handlers/FlatOut 2/` folder into the `handlers` folder of Nucleus Co-op.

Nothing is written to the game's own folder. fo2pad is copied only into the per-player instance folders Nucleus creates.

## Playing

1. Select FlatOut 2 in Nucleus, drop one controller on each screen, press Play.
2. Player 1: **Multiplayer → LAN → Create Game**. Everyone else: **Multiplayer → LAN → Join Game**.

| | Menus | Racing |
|---|---|---|
| D-pad / left stick | move | steer (left stick) |
| A | select | nitro |
| B | back | gear down (manual gears) |
| Start | back | pause |
| RT / LT | | throttle / brake |
| X / Y / RB | | camera / handbrake / reset car |

A, B and the left stick only act as menu keys when no trigger was pressed during the last 1.5 seconds, so they don't interfere while driving.

Two-player layouts stretch the picture (the game can't render at those window sizes); four players in quadrants look right on a 16:9 screen.

## Handler options

| Option | Default | |
|---|---|---|
| Music | Off | Off / Player 1 only / All players. Sound effects always play. |
| Throttle / brake | Triggers (RT / LT) | or left stick up / down, like FlatOut's default |
| LAN between screens | On | virtual LAN per window; switch off to leave networking untouched |
| Debug log | Off | writes `fo2pad.log` into each instance folder |

## How fo2pad works

See [`handlers/FlatOut 2/README.txt`](handlers/FlatOut%202/README.txt) for details and the `fo2pad.ini` settings. The source is [`handlers/FlatOut 2/src/fo2pad.c`](handlers/FlatOut%202/src/fo2pad.c).

## Building

- The DLL: [Tiny C Compiler](https://download.savannah.gnu.org/releases/tinycc/) 0.9.27 (32-bit), then `python "handlers/FlatOut 2/src/build.py" path\to\tcc.exe`.
- The `.nc` package: `python tools/make_nc.py`.

## How this was made

This project was written with AI assistance. The handler, the fo2pad DLL, the reverse engineering of the game (keyboard hook, controller files, network behaviour) and the test tools were written by Claude, Anthropic's AI model, in a Claude Code session.

## Credits

- The LAN fixes follow what [MorMund's FO2-Splitscreen](https://github.com/MorMund/FO2-Splitscreen) mod found about FlatOut 2's networking.
- [Nucleus Co-op](https://github.com/SplitScreen-Me/splitscreenme-nucleus) and its DirectX 9 wrapper, which runs the game in a window.

## License

[MIT](LICENSE)
