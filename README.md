# h3-mods

Plugins for Heroes of Might and Magic III with HD Mod, built for Horn of the Abyss (HotA) and Shadow of Death (SoD).

## Layout

| Folder | Contents |
|---|---|
| `src/KeyRemap` | Keyboard remaps: WASD map scrolling, town and battle hotkeys, F5/F9 quick save and load under HotA. |
| `src/MonsterHoverInfo` | Shows the exact size of a monster stack when you hover over it on the adventure map. |
| `src/AutoCombatAI` | Replaces auto-combat and HD quick combat for your own creature stacks. Each turn it plays every move, attack, shot, wait and defend forward to the end of the battle against a port of the stock enemy AI, scores it by the creatures killed against the creatures you lose, with losses weighted three times, and logs each choice to `AutoCombatAI.log`. |
| `src/HotaLoader` | A `version.dll` proxy. It loads HD Mod plugin packs and their LOD files under HotA, which HD Mod does not do itself, and patches the HD launcher so it lists plugins for the HotA profile. |
| `src/TextColor` | RoseKavalier's TextColor plugin with a fix for HD Mod 5.8 R313 and later. MIT licence, see `src/TextColor/LICENSE`. |
| `src/EnsrickPortraits` | A Python script that packs Ensrick's HD hero portraits into a LOD file. |
| `vendor/H3API` | RoseKavalier's H3API, the library the plugins are built on. MIT licence, see `vendor/H3API/LICENSE`. Only its build paths were changed. |

## Installing the prebuilt DLLs

The `dist` folder has the same layout as the game folder. Copy everything inside `dist` into your HoMM3 folder (the one with `h3hota.exe`) and let it merge:

| File | Purpose |
|---|---|
| `version.dll`, `verreal.dll` | The HotA plugin loader. `verreal.dll` is a copy of the 32-bit Windows `version.dll` that the loader forwards to. |
| `_HD3_Data\Packs\KeyRemap\H3.KeyRemap.dll` | KeyRemap |
| `_HD3_Data\Packs\MonsterHoverInfo\H3.MonsterHoverInfo.dll` | MonsterHoverInfo |
| `_HD3_Data\Packs\AutoCombatAI\H3.AutoCombatAI.dll` | AutoCombatAI |
| `_HD3_Data\Packs\SoD_SP\H3.TextColor.dll` | The fixed TextColor. It overwrites the copy that SoD_SP ships, so skip it if you do not use SoD_SP. |

Then enable the packs as described in [Enabling the plugins](#enabling-the-plugins).

## Building

You need Visual Studio 2022 with the "Desktop development with C++" workload.

```
build.bat                   builds everything, and deploys if the repo is cloned into <game>\_repo
build.bat "D:\Games\HoMM3"  builds everything and deploys to that game folder
```

The DLLs land in `bin\Release`. When deploying, the script copies:

- `H3.KeyRemap.dll` to `_HD3_Data\Packs\KeyRemap\`
- `H3.MonsterHoverInfo.dll` to `_HD3_Data\Packs\MonsterHoverInfo\`
- `H3.AutoCombatAI.dll` to `_HD3_Data\Packs\AutoCombatAI\`
- `version.dll` to the game folder, and a copy of the system `version.dll` as `verreal.dll` if it is missing

TextColor is built but not deployed, because SoD_SP ships its own copy.

You can also open `h3-mods.sln` in Visual Studio. It will offer to retarget the older projects to the current toolset; accept it.

## Enabling the plugins

Add the packs to the `<Packs>` line in `_HD3_Data\Settings\hota.ini` (or `sod.ini`), for example:

```
<Packs> = "EnsrickPortraits", "KeyRemap", "MonsterHoverInfo"
```

## Ensrick portraits

The script needs Python 3 and Pillow (`pip install pillow`). Point it at the extracted portrait pack and write the LOD into its pack folder:

```
python src\EnsrickPortraits\build_ext_lod.py <extracted portraits folder> <game>\_HD3_Data\Packs\EnsrickPortraits\EnsrickPortraits.lod
```
