# h3-mods

Plugins for Heroes of Might and Magic III with HD Mod, built for Horn of the Abyss (HotA) and Shadow of Death (SoD).

## Layout

| Folder | Contents |
|---|---|
| `src/KeyRemap` | Keyboard remaps: WASD map scrolling, town and battle hotkeys, F5/F9 quick save and load under HotA. |
| `src/MonsterHoverInfo` | Shows the exact size of a monster stack when you hover over it on the adventure map. |
| `src/HotaLoader` | A `version.dll` proxy. It loads HD Mod plugin packs and their LOD files under HotA, which HD Mod does not do itself, and patches the HD launcher so it lists plugins for the HotA profile. |
| `src/TextColor` | RoseKavalier's TextColor plugin with a fix for HD Mod 5.8 R313 and later. MIT licence, see `src/TextColor/LICENSE`. |
| `src/EnsrickPortraits` | A Python script that packs Ensrick's HD hero portraits into a LOD file. |
| `vendor/H3API` | RoseKavalier's H3API, the library the plugins are built on. MIT licence, see `vendor/H3API/LICENSE`. Only its build paths were changed. |

## Building

You need Visual Studio 2022 with the "Desktop development with C++" workload.

```
build.bat                   builds everything, and deploys if the repo is cloned into <game>\_repo
build.bat "D:\Games\HoMM3"  builds everything and deploys to that game folder
```

The DLLs land in `bin\Release`. When deploying, the script copies:

- `H3.KeyRemap.dll` to `_HD3_Data\Packs\KeyRemap\`
- `H3.MonsterHoverInfo.dll` to `_HD3_Data\Packs\MonsterHoverInfo\`
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
