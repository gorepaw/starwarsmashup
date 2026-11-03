# Building

Windows, Visual Studio 2022 (C++ workload), CMake 3.28+, Python 3, Git.

## 1. Godot editor (double precision)

The project needs a custom Godot build with `precision=double`. A stock Godot editor can't load the GDExtension.

Pinned version: **Godot 4.7.2-stable**. The engine source lives outside this repo. It needs several GB, so put it on a drive with room.

```powershell
git clone --depth 1 --branch 4.7.2-stable https://github.com/godotengine/godot <godot-src>
python -m venv <venv>
<venv>\Scripts\python -m pip install scons
cd <godot-src>
<venv>\Scripts\scons platform=windows target=editor precision=double d3d12=no accesskit=no -j <cores>
```

The editor is written to `<godot-src>\bin\godot.windows.editor.double.x86_64.exe`.

- `d3d12=no` and `accesskit=no` skip two drivers that need extra SDKs. Vulkan is the default renderer, so development doesn't need Direct3D 12.
- **AccessKit is screen-reader support. Release builds should include it** (`python misc\scripts\install_accesskit.py`, then drop `accesskit=no`).

## 2. C++ (core, tools, GDExtension)

```powershell
cmake --preset msvc
cmake --build --preset msvc-release
ctest --preset msvc-release
```

- zlib and godot-cpp (10.0.0-stable, double precision) are fetched at configure time.
- godot-cpp generates its bindings from [`third_party/godot-api/extension_api-4.7.2-double.json`](../third_party/godot-api/), dumped from the double-precision editor. godot-cpp's bundled API files come from single-precision engines and can't be used. After an engine upgrade, regenerate it:

  ```powershell
  cd third_party\godot-api
  <godot> --headless --dump-extension-api
  move extension_api.json extension_api-<version>-double.json
  ```
- The GDExtension is written to `game/bin/swm.windows.template_debug.double.x86_64.dll`.
- To build only `core/` and `tools/`, configure with `-DSWM_BUILD_GODOT=OFF`.

## 3. Godot project

```powershell
# Fresh checkout only: see "Known issue" below.
New-Item -ItemType Directory -Force game\.godot
Set-Content game\.godot\extension_list.cfg "res://swm.gdextension"

<godot> --headless --path game --import
<godot> --headless --path game --script res://tests/smoke_test.gd
```

**Known issue (Godot 4.7.2):** on a fresh project with no `.godot/` yet, the editor discovers the extension during its first filesystem scan instead of at startup. A headless session then crashes at exit (`0xC0000005`) if the extension registers any class. The import itself has already finished, so a second run is clean. Writing `.godot/extension_list.cfg` first makes the extension load at startup and avoids the crash. Upstream: [godotengine/godot#123511](https://github.com/godotengine/godot/issues/123511), fix pending in [#123658](https://github.com/godotengine/godot/pull/123658). We confirmed it here: fresh imports crash with `SwmInfo` registered, exit cleanly with no classes registered, and exit cleanly with `extension_list.cfg` pre-written.

The smoke test checks that the engine and the extension are both double precision and that the extension calls into `core/`. Open the project in the editor with `<godot> --path game -e`.
