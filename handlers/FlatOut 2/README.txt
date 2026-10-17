FlatOut 2 split-screen for Nucleus Co-op
========================================

Requirements
- FlatOut 2 v1.2, GOG version (= the original 2006 exe). The current Steam version is not supported.
- Start the game normally once before using Nucleus, so it creates Savegame\options.cfg and device.cfg
  with a resolution your screen supports.
- One Xbox / XInput controller per screen (the keyboard can be used for one screen).

What is in this folder
- dinput8.dll   "fo2pad", copied by the handler into every Nucleus instance folder (never into the real
                game folder). It only affects the FlatOut 2 process it is loaded into.
- src\          its source code (fo2pad.c) and build script (build.py).

What fo2pad does
- Controllers: FlatOut 2 only reads controllers through DirectInput, and Windows only delivers DirectInput
  data from Xbox One / Series pads to the window that is really in front. fo2pad gives each instance one
  virtual pad read through XInput (which works in the background), laid out like an Xbox pad in
  DirectInput (triggers share the Z axis). Real controllers are hidden from the game.
- Menus: FlatOut's menus only read the keyboard. fo2pad feeds the pad into the game's keyboard handler:
  D-pad / left stick = arrows, A = Enter, B and Start = Escape. A, B and the stick only act as keys while
  no trigger was pressed in the last 1.5 s, so they don't interfere while racing.
- LAN on one PC: FlatOut uses fixed ports, ignores 127.x peers and its own MAC address. Each instance binds
  to its own loopback address 127.0.0.N, while the game is shown a virtual LAN 192.168.80.N with a
  per-instance MAC; packet addresses are translated in between.

Settings (fo2pad.ini, written by the handler into each instance folder)
  [Pad]  Slot=1..4 (XInput slot, 0 = no pad)   Log=0/1 (writes fo2pad.log)
  [Menu] Keys=1   DriveHoldMs=1500   StartIsEscape=1
  [Net]  Address=127.0.0.N (empty = no LAN changes)   LogPackets=0/1
  Debug only: [Menu] SelfTest=1 (presses Enter every few seconds), Remote=1 (presses the keys listed in
  fo2pad_remote.txt, e.g. "DOWN DOWN ENTER").

Building
  Tiny C Compiler 0.9.27 (32-bit, https://download.savannah.gnu.org/releases/tinycc/):
    python src\build.py <path to tcc.exe>
  tcc decorates the stdcall export name; build.py renames it to DirectInput8Create and writes
  ..\dinput8.dll.

Credits
  The LAN fixes (virtual addresses, per-instance MAC) follow what MorMund's FO2-Splitscreen mod found:
  https://github.com/MorMund/FO2-Splitscreen
