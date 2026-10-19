Hub.Handler.Version = 1;
Hub.Handler.Id = "";
Hub.Maintainer.Name = "Local";
Hub.Maintainer.Id = "";

// FlatOut 2 (GOG, v1.2) - up to 4 split-screen instances on one screen, joined over LAN.
//
// How it works:
//  - Window:      FlatOut 2 is fullscreen-only. Nucleus' DirectX 9 wrapper (d3d9.dll) forces it windowed.
//  - Everything else is done by fo2pad, a dinput8.dll shipped in "handlers\FlatOut 2" (source in src\),
//    copied into each instance and configured through fo2pad.ini:
//      * the instance sees exactly one gamepad, read from its player's XInput slot. (FlatOut only uses
//        DirectInput, and Windows only gives DirectInput data from Xbox pads to the focused window.)
//      * pad -> keyboard keys for FlatOut's keyboard-only menus, so no window ever needs focus.
//      * LAN on one PC: own loopback IP, host address and MAC per instance (fixed ports otherwise clash).
//  - Saves:       Savegame is copied (not linked) per instance, so instances never overwrite each other
//                 or the original saves.

Game.ExecutableName = "FlatOut2.exe";
Game.GameName = "FlatOut 2";
Game.GUID = "FlatOut 2";
Game.MaxPlayers = 8;
Game.MaxPlayersOneMonitor = 4;
Game.HandlerInterval = 100;
Game.PauseBetweenStarts = 5;

Game.Description =
  "*<Made for the GOG version (original 2006 game v1.2). The current Steam version is not supported.|255,200,0>* " +
  "Start FlatOut 2 normally once first, so it creates its settings. " +
  "Windows 11 Smart App Control must be off: it blocks the handler's unsigned dinput8.dll and the game closes right away. " +
  "One Xbox (XInput) controller per screen; the keyboard can be used for one screen too. " +
  "Menus: D-pad or left stick to move, A to select, B or Start to go back. " +
  "To race together: player 1 creates a game under Multiplayer > LAN, the others join it there. " +
  "If every window shows the FlatOut 2 setup dialog, run the game once normally and choose a resolution your screen supports.";

Game.AddOption("Music", "Sound effects always play; this only affects the soundtrack.", "Music", ["Off", "Player 1 only", "All players"]);
Game.AddOption("Throttle / brake", "Pad layout for accelerating and braking.", "Pedals", ["Triggers (RT / LT)", "Left stick up / down"]);
Game.AddOption("Debug log", "Writes fo2pad.log in each instance folder (for troubleshooting).", "DebugLog", ["Off", "On"]);
Game.AddOption(
  "LAN between screens",
  "Gives every instance its own loopback IP so they can join each other's LAN games.",
  "Network",
  ["On", "Off"]
);

// ---------------- Files ----------------
Game.SymlinkGame = true;
Game.SymlinkExe = false;
Game.SymlinkFolders = false;
// options.cfg / device.cfg / pad maps / profiles must be real per-instance copies
Game.DirSymlinkCopyInstead = ["Savegame"];
Game.DirSymlinkCopyInsteadIncludeSubFolders = true;

// ---------------- Window ----------------
Game.UseDirectX9Wrapper = true;
Game.HideTaskbar = true;

// ---------------- Controllers ----------------
// List XInput pads in the Nucleus setup screen (Context.GamepadId = XInput slot). No Nucleus input dll is
// installed (CustomDllEnabled = false); fo2pad reads the slot itself.
Game.Hook.CustomDllEnabled = false;
Game.Hook.XInputEnabled = true;
Game.Hook.DInputEnabled = false;
Game.Hook.XInputReroute = false;
Game.SupportsKeyboard = true;
Game.SupportsMultipleKeyboardsAndMice = false;

// fo2pad's virtual pad reports itself as an Xbox 360 controller (VID 045E, PID 028E). FlatOut stores the
// selected pad as its product GUID's bytes reversed, and keeps the bindings in Savegame\am<that>.dat
var X360_PAD = "444956444950000000000000028e045e";
var NO_PAD = "00000000000000000000000000000000";

// FlatOut's default pad map with accelerate/brake moved from the left stick's Y axis (axis 1) to the
// shared trigger axis Z (axis 2): accelerate = Z negative = RT, brake = Z positive = LT. 336 bytes.
var padMapTriggers = [2,0,1,0,80,0,0,0,50,0,0,0,90,0,0,0,0,0,0,0,3,0,0,0,2,0,0,0,0,0,0,0,1,0,0,0,7,0,0,0,2,0,0,0,0,0,0,0,2,0,0,0,46,0,0,0,0,0,0,0,0,0,0,0,3,0,0,0,2,0,0,0,2,0,0,0,0,0,0,0,4,0,0,0,4,0,0,0,2,0,0,0,0,0,0,0,5,0,0,0,5,0,0,0,2,0,0,0,0,0,0,0,6,0,0,0,35,0,0,0,0,0,0,0,0,0,0,0,7,0,0,0,6,0,0,0,2,0,0,0,0,0,0,0,8,0,0,0,13,0,0,0,0,0,0,0,0,0,0,0,9,0,0,0,13,0,0,0,2,0,0,0,0,0,0,0,10,0,0,0,2,0,0,0,1,0,0,0,0,0,240,216,11,0,0,0,2,0,0,0,1,0,0,0,0,0,16,39,12,0,0,0,0,0,0,0,1,0,0,0,0,0,240,216,13,0,0,0,0,0,0,0,1,0,0,0,0,0,16,39,14,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,15,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,16,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,17,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,18,0,0,0,1,0,0,0,2,0,0,0,0,0,0,0,19,0,0,0,0,0,0,0,2,0,0,0,0,0,0,0];

Game.Play = function () {
  var root = Context.GetFolder(Nucleus.Folder.InstancedGameFolder);
  var optionsCfg = root + "\\Savegame\\options.cfg";
  var hasPad = !Context.IsKeyboardPlayer && Context.GamepadId > 0;

  function setLine(path, startsWith, newLine) {
    var lineNum = Context.FindLineNumberInTextFile(path, startsWith, Nucleus.SearchType.StartsWith);
    if (lineNum > 0) {
      Context.ReplaceLinesInTextFile(path, [lineNum + "|" + newLine]);
    }
  }

  // --- fo2pad: dinput8.dll from the handler folder + this instance's settings ---
  Context.CopyScriptFolder(root);
  Context.WriteTextFile(root + "\\fo2pad.ini", [
    "[Pad]",
    "Slot=" + (hasPad ? Context.GamepadId : 0),
    "Log=" + (Context.Options["DebugLog"] == "On" ? 1 : 0),
    "[Menu]",
    "Keys=1",
    "[Net]",
    "Address=" + (Context.Options["Network"] == "On" ? "127.0.0." + (Context.PlayerID + 1) : "")
  ]);

  if (Context.FindFiles(root + "\\Savegame", "options.cfg").length == 0) {
    return;
  }

  // --- controller: pad players use the virtual pad, the keyboard player the keyboard ---
  setLine(optionsCfg, "Settings.Control.ControllerGuid ", 'Settings.Control.ControllerGuid = "' + (hasPad ? X360_PAD : NO_PAD) + '"');
  setLine(optionsCfg, "Settings.Control.Controller ", "Settings.Control.Controller = " + (hasPad ? 1 : 0));
  setLine(optionsCfg, "Settings.Control.ForceFeedback ", "Settings.Control.ForceFeedback = FALSE");
  if (hasPad) {
    // binding record k starts at 16 + 16 * k: action, code (button index / axis), type, axis direction
    var bytes = padMapTriggers.slice(0);
    bytes[16 + 16 * 1 + 4] = 0; // nitro (FlatOut default: Start) -> A
    bytes[16 + 16 * 19 + 4] = 7; // gear up (default: A, only used with manual gears) -> Start
    if (Context.Options["Pedals"] != "Triggers (RT / LT)") {
      bytes[16 + 16 * 10 + 4] = 1; // accelerate -> left stick Y axis, as in FlatOut's default
      bytes[16 + 16 * 11 + 4] = 1; // brake
    }
    // HexEdit needs an existing file of the right size, so write a placeholder first
    var padMap = root + "\\Savegame\\am" + X360_PAD + ".dat";
    var placeholder = "";
    for (var i = 0; i < bytes.length; i++) {
      placeholder += "x";
    }
    Context.WriteTextFile(padMap, [placeholder]);
    Context.HexEdit(padMap, "0", bytes);
  }

  // --- name, music ---
  var name = String(Context.Nickname).replace(/["\\]/g, "").substring(0, 15).replace(/\s+$/, "");
  if (name.length > 0) {
    setLine(optionsCfg, "Settings.Game.DefaultPlayerName ", 'Settings.Game.DefaultPlayerName = "' + name + '"');
  }
  var music = Context.Options["Music"];
  if (music == "Off" || (music == "Player 1 only" && Context.PlayerID > 0)) {
    setLine(optionsCfg, "Settings.Audio.InterfaceMusicVolume ", "Settings.Audio.InterfaceMusicVolume = 0");
    setLine(optionsCfg, "Settings.Audio.IngameMusicVolume ", "Settings.Audio.IngameMusicVolume = 0");
  }

  Context.StartArguments = "";
};
