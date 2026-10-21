# SMZ3-thor-3ds

Dual-screen SMZ3 (Super Metroid + A Link to the Past combo randomizer) for the Nintendo 3DS / New 3DS and the AYN Thor.
The top screen runs the game; the bottom screen shows the run: current map, items and checks, run data, and what is reachable by logic.

Status: planning. See [docs/PLAN.md](docs/PLAN.md).

## Android app (AYN Thor)

The Android app is **SMZ3** (`com.utsugi.smz3`; the debug build is `com.utsugi.smz3.debug`, "SMZ3 debug"). It is a fork of
[Lemuroid](https://github.com/Swordfish90/Lemuroid) (GPL-3.0) on top of LibretroDroid, with the run menu, the generator and
the bottom-screen tracker added in `vendor/` through `patches/*.patch`. Lemuroid's code keeps its own package names inside the app.

```bash
./build-android.sh            # build/smz3-debug.apk
./build-android.sh release    # build/smz3-release.apk, signed with a local key (vendor/Lemuroid/release.jks, not in git)
```

The app has its own data: runs made in the old `com.swordfish.lemuroid.debug` build are not carried over (saves under
`/sdcard/SMZ3/saves` are shared). After installing, allow notifications once (the game will not start without it).

### Run editor

| Options (QoL tab) | Run screen |
|---|---|
| ![Create run, QoL tab](docs/shots/create-qol-tab-thor.png) | ![Run tab](docs/shots/run-tab-redesign-thor.png) |

## Building it yourself

No binaries, ROMs or generated games are distributed here: only source, patches and build scripts. You build the app and the
3DS homebrew yourself and supply your own legally obtained ROMs.

- **Thor / Android:** `./build-android.sh` (see above). JDK 21, Android SDK and NDK required.
- **3DS:** `./build-3ds.sh` produces `snes9x_smz3.3dsx` (devkitPro + devkitARM required); run it on a 3DS with the Homebrew
  Launcher or in the Azahar emulator.
- **ROMs:** place your own A Link to the Past and Super Metroid dumps as described below; the app generates the SMZ3 game on
  the device.

## ROMs

`rom/` is gitignored. Local copies used for development:

| File | Source | SHA-1 |
|---|---|---|
| `alttp_usa.sfc` | A Link to the Past (USA) | `6d4f10a8b10e10dbe624cb23cf03b88bb8252973` |
| `supermetroid_ju.sfc` | Super Metroid (Japan, USA) (En,Ja) | `da957f0d63d14cb441d215462904c4fa8519c613` |
| `smz3_test_popoie_fang.sfc` | already generated SMZ3, for tracking tests | `c74bd08d523cc8ad995cda975039d840849cc85a` |
