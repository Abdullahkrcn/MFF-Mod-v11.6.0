# Building MFF v11.6.0 hook module

## Requirements

- **Android NDK** (r25+ recommended)
- **Dobby** or **And64InlineHook** (ARM64 prebuilt or source)

## Steps

### 1. Get Dobby (ARM64)

- Clone [Dobby](https://github.com/jmpews/Dobby) and build for Android arm64-v8a, or use a prebuilt `libdobby.a`.
- Place `libdobby.a` in `lib/arm64-v8a/` (create the folder). If you use a different path, edit `CMakeLists.txt` and set `target_link_libraries` to that path.

### 2. Configure with NDK

From the project root (same folder as this README):

```bash
mkdir build && cd build
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_NDK=$ANDROID_NDK
```

### 3. Build

```bash
cmake --build .
```

The output will be `zygisk/arm64-v8a.so`. Replace the existing `zygisk/arm64-v8a.so` in your Magisk ZIP with this file, then flash the module.

## Using And64InlineHook

1. In `src/mff_11_6_0_hooks.cpp`, set `#define USE_DOBBY 0`.
2. In `CMakeLists.txt`, comment out the Dobby `target_link_libraries` line and uncomment the And64InlineHook one; set the path to your `libAnd64InlineHook.a`.
3. Rebuild.

## Method names for MFF v11.6.0

The hooks resolve methods by **IL2CPP Internal Call name** or by **class/method name**. The current names (e.g. `Battle.Character::ApplyDamage`) are placeholders. To get the correct names for v11.6.0:

1. Dump the game with **Zygisk-Il2CppDumper** or **Il2CppDumper** (from the game’s `libil2cpp.so` + `global-metadata.dat` for v11.6.0).
2. In the dump (e.g. `script.json` or generated headers), find:
   - Damage dealt: e.g. `ApplyDamage`, `DealDamage`, or similar.
   - Damage received: e.g. `TakeDamage`, `OnDamage`.
   - Skill cooldown: e.g. `GetRemainCooldown`, `GetCooldown`, `IsReady`.
   - Death check: e.g. `get_isDead`, `IsDead`, `CheckDeath`.
3. Update in `mff_11_6_0_hooks.cpp`:
   - `resolve_icall("Namespace.Class::MethodName")` for Internal Calls.
   - `resolve_method("Assembly-CSharp", "Namespace", "ClassName", "MethodName", param_count)` for normal methods.

## Safety guard

- Hooks run only in the MFF process (cmdline contains `mherosgb`).
- Hooks are installed after a 4-second delay and only after `libil2cpp.so` is loaded.
- Resolution is retried for up to 20 seconds. If no method is found, no hook is installed to avoid crashes.
