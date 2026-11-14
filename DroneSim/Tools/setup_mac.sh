#!/bin/bash
# One-command setup for DroneSim on a Mac.
#
#   cd DroneSim
#   bash Tools/setup_mac.sh
#
# What it does, in order:
#   1. Finds Unreal Engine (installed through the Epic Games Launcher) and checks Xcode.
#   2. Points the project at the engine version it found.
#   3. Downloads the free photoscanned textures and tree models (about 130 MB).
#   4. Compiles the project. The full compiler output is saved to Saved/setup_build.log.
#   5. Imports the assets and builds the ground / water / tree materials.
#   6. Opens the project in the Unreal editor.
#
# If step 4 fails, send the lines containing "error" from Saved/setup_build.log
# (the script prints them) and they can be fixed.
#
# Options:
#   UE_ROOT=/path/to/UE_5.x bash Tools/setup_mac.sh     use a specific engine
#   SKIP_ASSETS=1 bash Tools/setup_mac.sh               skip the download and import

set -u

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PROJECT="$PROJECT_DIR/DroneSim.uproject"
LOG_DIR="$PROJECT_DIR/Saved"
BUILD_LOG="$LOG_DIR/setup_build.log"
IMPORT_LOG="$LOG_DIR/setup_import.log"
mkdir -p "$LOG_DIR"

step() { printf '\n==> %s\n' "$1"; }
fail() { printf '\nSTOPPED: %s\n' "$1" >&2; exit 1; }

[ -f "$PROJECT" ] || fail "DroneSim.uproject not found next to the Tools folder."

# ---------------------------------------------------------------- 1. engine and Xcode
step "Looking for Unreal Engine"
if [ -z "${UE_ROOT:-}" ]; then
	# Newest UE_5.x in the launcher's default location.
	UE_ROOT="$(ls -d "/Users/Shared/Epic Games"/UE_5.* 2>/dev/null | sort -t_ -k2 -V | tail -n 1)"
fi
if [ -z "${UE_ROOT:-}" ] || [ ! -d "$UE_ROOT/Engine" ]; then
	fail "Unreal Engine 5 was not found in '/Users/Shared/Epic Games'.
Install it with the Epic Games Launcher (Unreal Engine > Library > +), version 5.3 or newer,
or run:  UE_ROOT=/path/to/UE_5.x bash Tools/setup_mac.sh"
fi
echo "Using $UE_ROOT"

BUILD_SH="$UE_ROOT/Engine/Build/BatchFiles/Mac/Build.sh"
EDITOR_APP="$UE_ROOT/Engine/Binaries/Mac/UnrealEditor.app"
EDITOR_CMD="$UE_ROOT/Engine/Binaries/Mac/UnrealEditor-Cmd"
[ -f "$BUILD_SH" ] || fail "Engine build script missing: $BUILD_SH"

if ! xcode-select -p >/dev/null 2>&1 || ! xcodebuild -version >/dev/null 2>&1; then
	fail "Xcode is needed to compile the project.
Install Xcode from the App Store, open it once to accept the licence, then run this script again."
fi
echo "Xcode: $(xcodebuild -version | head -n 1)"

# Unreal compiles its shaders with Apple's Metal compiler, which newer Xcode versions download separately.
if ! xcrun -sdk macosx metal --version >/dev/null 2>&1; then
	echo "The Metal shader compiler is missing. Installing it (needs your Mac password)..."
	sudo xcodebuild -runFirstLaunch && xcodebuild -downloadComponent MetalToolchain
	if ! xcrun -sdk macosx metal --version >/dev/null 2>&1; then
		fail "The Metal toolchain could not be installed. In Terminal run:
  sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
  sudo xcodebuild -runFirstLaunch
  xcodebuild -downloadComponent MetalToolchain
then run this script again."
	fi
fi

# ---------------------------------------------------------------- 2. engine version in the project
ENGINE_VERSION="$(basename "$UE_ROOT" | sed 's/^UE_//')"
case "$ENGINE_VERSION" in
	5.*)
		step "Setting the project's engine version to $ENGINE_VERSION"
		/usr/bin/sed -i '' -E "s/(\"EngineAssociation\": *\")[^\"]*\"/\1$ENGINE_VERSION\"/" "$PROJECT"
		;;
	*)
		echo "Engine folder is not named UE_5.x; leaving EngineAssociation in the project unchanged."
		;;
esac

# ---------------------------------------------------------------- 3. assets
if [ -z "${SKIP_ASSETS:-}" ]; then
	step "Downloading photoscanned textures and tree models (about 130 MB, skipped if already there)"
	if ! python3 "$PROJECT_DIR/Tools/fetch_assets.py"; then
		echo "Download failed. Continuing: the sim still runs, with flat colours and simple trees."
		echo "Run 'python3 Tools/fetch_assets.py' again later, then this script."
	fi
fi

# ---------------------------------------------------------------- 4. compile
step "Compiling DroneSim (first time takes 5-20 minutes)"
if ! "$BUILD_SH" DroneSimEditor Mac Development -project="$PROJECT" -waitmutex 2>&1 | tee "$BUILD_LOG"; then
	:
fi
if ! grep -q -E "Result: Succeeded|BUILD SUCCESSFUL|Target is up to date" "$BUILD_LOG"; then
	printf '\n---------------- compiler errors ----------------\n'
	grep -n -E "error[ :]|Error:|fatal error" "$BUILD_LOG" | head -n 60
	printf -- '-------------------------------------------------\n'
	fail "The project did not compile. Copy the errors above (full log: $BUILD_LOG) and send them."
fi
echo "Compiled."

# ---------------------------------------------------------------- 5. import assets, build materials
if [ -z "${SKIP_ASSETS:-}" ] && [ -d "$PROJECT_DIR/Downloads" ] && [ -x "$EDITOR_CMD" ]; then
	step "Importing assets and building materials (a few minutes; shaders compile in the background)"
	"$EDITOR_CMD" "$PROJECT" -run=pythonscript -script="$PROJECT_DIR/Tools/setup_realistic_assets.py" -unattended -nosplash >"$IMPORT_LOG" 2>&1
	grep -E "\[DroneSim setup\]" "$IMPORT_LOG" | sed 's/^.*\[DroneSim setup\]/   /'
	if ! grep -q "\[DroneSim setup\] finished" "$IMPORT_LOG"; then
		echo "The import did not report finishing (log: $IMPORT_LOG)."
		echo "You can run it by hand in the editor: Tools > Execute Python Script > Tools/setup_realistic_assets.py"
	fi
fi

# ---------------------------------------------------------------- 6. open
step "Opening the project"
if [ -d "$EDITOR_APP" ]; then
	open -a "$EDITOR_APP" --args "$PROJECT"
else
	open "$PROJECT"
fi

cat <<'EOF'

Done. When the editor has loaded:
  - Press the green Play button. The Jondal landscape is built when play starts.
  - Scan the QR code in the top-right corner with a phone on the same Wi-Fi.
    macOS asks whether to allow incoming connections for UnrealEditor: choose Allow.
  - F1 shows the keys, F2 changes landscape, F3 starts a race, G changes the weather.
EOF
