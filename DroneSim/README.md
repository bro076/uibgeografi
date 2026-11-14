# DroneSim – manual flight trainer for DJI-style drones

An Unreal Engine 5 C++ project for teaching students to fly a camera drone by hand,
with the option to use a phone as the remote controller.

> **Status: the Unreal C++ has never been compiled.** It was written without access to
> Unreal Engine. Expect to fix some build errors on first compile.
> What *has* been tested outside Unreal: the phone controller page (touch sticks, buttons,
> pilot/spectator hand-over, against a mock server that speaks the same protocol) and the QR
> code generator (output decoded successfully).

## What is in it

| File | What it does |
| --- | --- |
| `Source/DroneSim/DronePawn.*` | The drone: flight model, flight modes, auto take-off / landing / return-to-home, battery, wind, crashes, cameras, keyboard + gamepad input |
| `Source/DroneSim/PhoneControllerSubsystem.*` | Small HTTP + WebSocket server so phones on the same Wi-Fi can act as the remote |
| `Source/DroneSim/DroneHUD.*` | On-screen telemetry (H, D, H.S, V.S, battery), warnings, stick position display, pairing QR code |
| `Source/DroneSim/QrCode.*` | QR code generator for the pairing URL |
| `Content/Web/controller.html` | The page the phone loads: two virtual sticks, flight buttons, live telemetry |

There are no art assets. The drone is a placeholder built from engine cubes (red lights
at the front, green at the back) and the default map is the engine's Open World template.

## Getting it running

1. Install Unreal Engine 5.3 or newer and Visual Studio with the "Game development with C++" workload
   (Xcode on Mac).
2. Right-click `DroneSim.uproject` → **Generate Visual Studio project files**. If asked, pick
   your engine version (`EngineAssociation` in the `.uproject` is set to 5.6; change it or use
   **Switch Unreal Engine version**).
3. Open `DroneSim.uproject` and let it compile, or build the `DroneSimEditor` target from the IDE
   to see compiler errors properly.
4. Press **Play**. The drone is placed on the ground under the player start.

If the Open World template map is missing in your engine version, open any map and set
**World Settings → GameMode Override** to `DroneSimGameMode`, or change `GameDefaultMap` in
`Config/DefaultEngine.ini`.

## Using a phone as the controller

1. The PC and the phones must be on the **same Wi-Fi network**.
2. Start the sim. The top-right corner shows a QR code and an address like `http://192.168.1.20:8080`.
3. Scan the QR code (or type the address) on the phone and turn it sideways.
   Menu → *Full screen* hides the browser bars.

Things to know:

- The first time, Windows Firewall asks whether to allow Unreal on the network – allow it on
  private networks. Many school/university Wi-Fi networks block device-to-device traffic
  ("client isolation"); if phones cannot load the page, use a phone hotspot or a small
  travel router.
- The first phone to connect is the pilot. Others see telemetry only and get a
  **Take control** button, so the phone can be "handed over" between students.
- If the pilot phone stops sending for half a second, the sticks read as centred and the drone hovers.
- If the shown address is wrong (PCs with several network adapters), use the PC's Wi-Fi IPv4 address
  with the same port. The port is `PhonePort` in `Config/DefaultGame.ini`.
- A phone has no physical sticks, so it trains stick logic and orientation, not finger feel.
  A USB gamepad is closer to a real remote and works at the same time.

## Controls

| Action | Keyboard | Gamepad | Phone |
| --- | --- | --- | --- |
| Left stick | W A S D | left stick | left pad |
| Right stick | arrow keys | right stick | right pad |
| Take off / land | T | A | TAKE OFF / LAND |
| Return to home | H | B | RTH |
| Cine / Normal / Sport | 1 / 2 / 3 | X cycles | C N S |
| ATTI mode (no GPS hold) | 4 | – | Menu |
| Stick mode 1 / 2 / 3 | M cycles | – | Menu |
| Camera view (FPV / chase / pilot) | C | Y | VIEW |
| Gimbal tilt | PgUp / PgDn | triggers | GIMBAL slider |
| Wind: calm / 3 / 7 / 11 m/s | G | – | Menu |
| Reset | R | – | Menu |
| Help / QR code on-off | F1 / Q | – | – |

Motors can also be started the DJI way: hold both sticks down and inward for a moment, then
raise the throttle. On the ground, holding the throttle down stops the motors.

## What is simulated

- **GPS modes (Cine / Normal / Sport):** sticks command speed; releasing them brakes to a hover.
  Speed, climb, tilt and yaw-rate limits per mode approximate a Mavic 3 class aircraft and are
  editable on the pawn.
- **ATTI mode:** sticks command tilt, nothing holds position, the drone drifts with wind and
  momentum. This is the mode that teaches real manual control.
- **Aircraft response:** tilt lag, thrust limit, drag, gravity, wind with gusts and stronger
  wind at height. The drone leans into the wind to hold position and loses ground if the wind
  exceeds what the mode's tilt limit can fight.
- **Flight controller behaviours:** auto take-off to 1.2 m, auto landing, return-to-home (climb
  to 30 m, fly back, land), slowed descent near the ground, 120 m height ceiling, low-battery
  RTH and forced landing, home point recorded at take-off.
- **Mistakes have consequences:** hitting the ground faster than 3.5 m/s or an obstacle faster
  than 1.5 m/s is a crash.
- **Pilot view:** the camera stands at the take-off point and watches the drone, for practising
  orientation when the drone is facing you (the red/green lights tell front from back).

Not simulated: obstacle sensing, GPS/compass errors, signal range, propeller sound, ground effect.

## Making it look realistic

The realism of the picture comes from the environment and assets, which have to be added in the
Unreal editor:

- **Real places:** the free *Cesium for Unreal* plugin can stream photogrammetry of real
  locations, so students can fly over the actual area they will survey.
- **Natural scenery:** Quixel Megascans assets from Fab, or one of Epic's free sample environments.
- **Drone model:** create a Blueprint subclass of `DronePawn`, add a static mesh, untick
  *Show Placeholder Mesh*, and set that Blueprint as the game mode's default pawn.
- The project config already enables Lumen and virtual shadow maps.
