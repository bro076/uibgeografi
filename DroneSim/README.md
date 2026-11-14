# DroneSim – manual flight trainer for DJI-style drones

An Unreal Engine 5 C++ project for teaching students to fly a camera drone by hand,
with the option to use a phone as the remote controller.

> **Status: the Unreal C++ has never been compiled.** It was written without access to
> Unreal Engine. Expect to fix some build errors on first compile.
> What *has* been tested outside Unreal: the phone controller page (touch sticks, buttons,
> pilot/spectator hand-over, against a mock server that speaks the same protocol), the QR
> code generator (output decoded successfully), and the elevation files and ground-type rules
> for the three landscapes (previewed as top-down maps). The landscapes have not been seen
> rendered in Unreal.

## What is in it

| File | What it does |
| --- | --- |
| `Source/DroneSim/DronePawn.*` | The drone: flight model, flight modes, auto take-off / landing / return-to-home, battery, wind, crashes, cameras, keyboard + gamepad input |
| `Source/DroneSim/PhoneControllerSubsystem.*` | Small HTTP + WebSocket server so phones on the same Wi-Fi can act as the remote |
| `Source/DroneSim/DroneHUD.*` | On-screen telemetry (H, D, H.S, V.S, battery), warnings, stick position display, pairing QR code |
| `Source/DroneSim/QrCode.*` | QR code generator for the pairing URL |
| `Source/DroneSim/DroneTerrain.*` | Builds a landscape at runtime from an elevation file: ground mesh, sea, trees, orchards, buildings |
| `Source/DroneSim/DroneSimGameMode.*` | Spawns the sky and lighting, loads and switches landscapes |
| `Content/Terrain/*.r16` | Real elevation data for the three landscapes |
| `Tools/fetch_terrain.py` | Downloads elevation data for any coordinate, to add your own sites |
| `Content/Web/controller.html` | The page the phone loads: two virtual sticks, flight buttons, live telemetry |

There are no art assets. The drone is a placeholder built from engine cubes (red lights
at the front, green at the back). The landscapes are generated in code from real elevation
data, coloured per vertex, with trees and buildings made from engine cones, spheres and boxes.
The terrain shape is accurate; the surface detail is schematic, not photorealistic.

## Landscapes

Press **F2** (or Menu → *Next landscape* on the phone) to switch. `StartScene` in
`Config/DefaultGame.ini` picks the one shown first.

| # | Landscape | Centre (take-off point) | Size | What is there |
| --- | --- | --- | --- | --- |
| 0 | Jondal – fjord and village | 60.2767 N, 6.2545 E, by the bay | 6.1 km, 12 m grid | Hardangerfjord, the village on the flat ground by the shore, forested valley sides rising to 1070 m |
| 1 | Folgefonna – glacier | 60.2206 N, 6.4372 E, at 1347 m on the ice margin | 6.1 km, 12 m grid | The ice cap with blue-ice bands where it steepens, bare rock, snow patches, meltwater lakes |
| 2 | Hardanger – farms and orchards | 60.3262 N, 6.6680 E, above Lofthus at 94 m | 4.1 km, 8 m grid | Field patchwork on the fjord-side slopes, fruit trees in rows, red barns and white houses, forest above |

Elevation comes from Kartverket's national elevation model via Terrain Tiles on AWS Open
Data (© Kartverket, CC BY 4.0). Ground cover is inferred from height, slope and
roughness, not from land-cover maps, so individual fields, buildings and the exact ice
margin are plausible rather than real. Trees and buildings have collision; the fjord
surface counts as a crash. The drone is held inside the terrain by a flight-area limit.

To add another site:

```
pip install numpy pillow
python3 Tools/fetch_terrain.py mysite 60.39 5.32 10
```

then add a line to the `Scenes` table in `DroneSimGameMode.cpp`.

## Getting it running

1. Install Unreal Engine 5.3 or newer and Visual Studio with the "Game development with C++" workload
   (Xcode on Mac).
2. Right-click `DroneSim.uproject` → **Generate Visual Studio project files**. If asked, pick
   your engine version (`EngineAssociation` in the `.uproject` is set to 5.6; change it or use
   **Switch Unreal Engine version**).
3. Open `DroneSim.uproject` and let it compile, or build the `DroneSimEditor` target from the IDE
   to see compiler errors properly.
4. Press **Play**. The default map is the engine's empty map; the sky and the Jondal landscape
   are built into it when play starts (expect a pause of a few seconds).

Two things most likely to need a tweak on first run:

- **Ground invisible from above, or the drone falls through it:** tick *Flip Winding* on
  `DroneTerrain` (set the default in `DroneTerrain.h`).
- **Ground is grey/untextured:** the engine's vertex-colour material was not found. Make a
  material with a *Vertex Color* node wired to Base Color and assign it as `TerrainMaterial`.

To fly in a map of your own instead, open it and set **World Settings → GameMode Override**
to `DroneSimGameMode`. If the map contains a directional light, the built-in sky and
landscapes are skipped and the drone starts at the player start.

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
| Next landscape | F2 | – | Menu |
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

- **The same three places, properly dressed:** the `.r16` files import directly as Unreal
  Landscapes (Landscape mode → Import from File; 513 × 513, X/Y scale = grid spacing × 100,
  Z scale 1280, then lower the landscape by 100 m so sea level is at 0). Paint and populate
  them with Megascans materials and foliage.
- **Real places with imagery:** the free *Cesium for Unreal* plugin can stream photogrammetry
  of real locations, so students can fly over the actual area they will survey.
- **Natural scenery:** Quixel Megascans assets from Fab, or one of Epic's free sample environments.
- **Drone model:** create a Blueprint subclass of `DronePawn`, add a static mesh, untick
  *Show Placeholder Mesh*, and set that Blueprint as the game mode's default pawn.
- The project config already enables Lumen and virtual shadow maps.
