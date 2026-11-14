# DroneSim – manual flight trainer for DJI-style drones

An Unreal Engine 5 C++ project for teaching students to fly a camera drone by hand,
with the option to use a phone as the remote controller.

> **Status: the Unreal C++ has never been compiled.** It was written without access to
> Unreal Engine. Expect to fix some build errors on first compile.
> What *has* been tested outside Unreal: the phone controller page (touch sticks, buttons,
> pilot/spectator hand-over, against a mock server that speaks the same protocol), the QR
> code generator (output decoded successfully), and the elevation files and ground-type rules
> for the three landscapes (previewed as top-down maps), and the asset downloader (every file
> fetched successfully). The landscapes have not been seen rendered in Unreal, and the editor
> script that builds the realistic materials has never been run. Split-screen multi-drone play
> and the weather system are likewise untested in the engine.

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
| `Tools/fetch_assets.py` | Downloads the photoscanned textures and tree models |
| `Tools/setup_realistic_assets.py` | Run in the Unreal editor: imports them and builds the ground, water and tree materials |
| `Content/Web/controller.html` | The page the phone loads: two virtual sticks, flight buttons, live telemetry |

The repository contains no art assets. Out of the box the landscapes are flat-coloured with
cone and sphere trees. The realistic look comes from free photoscanned assets that two scripts
download and install on your PC — see **Realistic graphics** below. Do that step; it is the
difference between a schematic and a landscape.

The drone itself is a placeholder built from engine cubes (red lights at the front, green at
the back).

## Realistic graphics

Two steps, once per PC (about 130 MB download, all CC0 from polyhaven.com):

1. In a terminal: `python Tools/fetch_assets.py`
2. In the Unreal editor: **Tools → Execute Python Script…** and pick
   `Tools/setup_realistic_assets.py`. Watch the Output Log for lines starting
   `[DroneSim setup]`.

Press Play again and you get:

- **Ground:** photoscanned grass, forest floor, mountain heath, lichen-covered rock, snow,
  ploughed soil and hay meadow, blended per ground type, with surface relief (normal maps).
  Cliffs are textured from the side so they do not smear. Each texture switches to a larger
  scale with distance so no tiling pattern shows from altitude.
- **Glacier:** snow surface with glossy blue ice where the glacier steepens, and dark
  mirror-like meltwater lakes.
- **Fjord:** dark reflective water with drifting ripples.
- **Trees:** scanned fir and broadleaf tree models (the broadleaf is also used small, in
  rows, for the orchards) in place of cones and spheres. They are over a million triangles
  each and rely on Nanite, so they need a recent graphics card (roughly RTX 2060 or better).
- **Light and air:** physically based sky, volumetric clouds and valley haze, Lumen global
  illumination and reflections, light bloom and auto exposure. These are on even without
  the asset step.

What is still not realistic after this: buildings are plain white and red boxes, there is no
grass or undergrowth geometry, the terrain mesh has one point every 8–12 m so close-up
ground shapes are smooth, and the drone model is a box. For those, see
*Making it look even better* at the end.

If the script reports errors, paste the `[DroneSim setup]` lines from the Output Log and
they can be fixed; each stage (ground, water, trees) runs independently, so a failure in one
does not block the others.

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
- **Ground is grey/untextured:** run the *Realistic graphics* step. (Without it the ground uses
  the engine's vertex-colour material, which some engine versions do not load in game.)

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
- Each phone gets its own drone — see *Flying together* below.
- If a phone stops sending for half a second, its sticks read as centred and its drone hovers.
- **Take off / Land** and **RTH** must be held for 0.6 s, like the press-and-hold actions on a
  DJI remote. **❚❚** is the flight pause button: it brakes to a hover and cancels an automatic
  take-off, landing or return-to-home.
- If the shown address is wrong (PCs with several network adapters), use the PC's Wi-Fi IPv4 address
  with the same port. The port is `PhonePort` in `Config/DefaultGame.ini`.
- A phone has no physical sticks, so it trains stick logic and orientation, not finger feel.
  A USB gamepad is closer to a real remote and works at the same time.

## Flying together

Up to **four students fly in the same world at once**, on one PC and one screen:

- The first four phones to connect each get their own drone (grey, orange, blue, yellow),
  lined up 2.5 m apart at the take-off point. The phone shows which drone it flies.
- The screen splits so every pilot has their own camera view and telemetry. Use a projector
  or large display.
- Drones can collide with each other, and everyone shares the same weather.
- Drone 1 is the instructor's: it also responds to the keyboard and first gamepad, and only
  it can change the weather or the landscape. Extra gamepads control drones 2–4.
- A fifth phone watches drone 1's telemetry and is given a drone as soon as one frees up.
  A drone is removed 15 s after its phone disconnects.

Rendering four views costs roughly four times the graphics work; lower the resolution or
quality settings if it stutters. Flying together across several PCs over the network is
not implemented.

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
| Weather (next type) | G | – | Menu (drone 1 only) |
| Flight pause / brake | Space | Start | ❚❚ |
| Reset | R | – | Menu |
| Next landscape | F2 | – | Menu (drone 1 only) |
| Help / QR code on-off | F1 / Q | – | – |

Motors can also be started the DJI way: hold both sticks down and inward for a moment, then
raise the throttle. On the ground, holding the throttle down stops the motors.

## What is simulated

- **GPS modes (Cine / Normal / Sport):** sticks command speed; releasing them brakes to a hover.
  Speed, climb, tilt and yaw-rate limits per mode approximate a Mavic 3 class aircraft and are
  editable on the pawn.
- **ATTI mode:** sticks command tilt, nothing holds position, the drone drifts with wind and
  momentum. This is the mode that teaches real manual control.
- **Aircraft response:** tilt lag, thrust limit, drag, gravity, gusty wind that is stronger
  at height. The drone leans into the wind to hold position and loses ground if the wind
  exceeds what the mode's tilt limit can fight.
- **Flight controller behaviours:** auto take-off to 1.2 m, auto landing, return-to-home (climb
  to 30 m, fly back, land), slowed descent near the ground, 120 m height ceiling, low-battery
  RTH and forced landing, home point recorded at take-off.
- **Weather:** Calm, Breezy, Overcast and windy, Fog, Rain squall, and *Changing weather*, which
  drifts between them every minute or two with a gradual transition, so conditions worsen
  mid-flight. Weather sets wind strength and gustiness, visibility (fog can hide the drone
  from the pilot view within about 100 m), daylight and temperature. The wind direction
  wanders. Rain shows as streaks and a "land as soon as possible" warning; it does not damage
  the drone.
- **Cold:** temperature falls with altitude, so the glacier is below freezing in bad weather,
  and the battery drains 2 % faster per degree below 15 °C.
- **Mistakes have consequences:** hitting the ground faster than 3.5 m/s or an obstacle faster
  than 1.5 m/s is a crash.
- **Pilot view:** the camera stands at the take-off point and watches the drone, for practising
  orientation when the drone is facing you (the red/green lights tell front from back).

Not simulated: obstacle sensing, GPS/compass errors, signal range, propeller sound, ground effect.

## Making it look even better

Beyond the *Realistic graphics* step, further realism has to be added by hand in the Unreal editor:

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
