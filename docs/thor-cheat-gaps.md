# Thor Cheat Gap Inventory

Generated on 2026-05-09 from Thor device `c3ca0370`.

This is a historical cheat-gap snapshot, not a live compatibility database. Re-scan the Thor library before using it as a current missing-cheat list.

ROM scan source: `/storage/2664-21DE/Roms/n3ds/zcci`
Cheat scan source: `/storage/emulated/0/Azaharuser/cheats`

The scan found 113 games with parsed title IDs. Before this update, 33 entries had no matching cheat text file. This pass adds 3 exact-title cheat files, leaving 29 valid parsed games without Azahar-ready text cheats. One entry parsed as `0004000000000000`, which is not a real title ID and should be treated as a bad/unknown ROM header for cheat matching.

Performance work for Thor Base/Pro/Max belongs in `docs/thor-optimization-notes.md`.

## Added In This Pass

| Title ID | Game | Source | Notes |
| --- | --- | --- | --- |
| `0004000000053700` | E.X. Troopers (Japan / English patch) | https://rutube.ru/video/083ddaea2c07a2ec7d2edee11c3f3cde/ | Disabled by default; includes a Thor low-end 30 FPS preset, 60 FPS option, 30 FPS reset, AA disable, and speed helper for the English patch. |
| `00040000000BA800` | Pokemon Mystery Dungeon: Gates to Infinity | https://projectpokemon.org/home/forums/topic/57553-pok%C3%A9mon-mystery-dungeon-gates-to-infinity-us-text-speed-modifier/ | Disabled by default. |
| `00040000001D7100` | Persona Q2: New Cinema Labyrinth | https://kriegisrei.github.io/pq2stuff/ | Disabled by default. |

## Public Sources Checked

| Source | Result |
| --- | --- |
| JourneyOver/CTRPF-AR-CHEAT-CODES `ca51e1d` | No exact title-ID matches for the unresolved Thor gaps. |
| FlagBrew/Sharkive `2f8a18c` | No exact title-ID matches for the unresolved Thor gaps. |
| citra-games-wiki `9efbe70` cheats snapshot | No useful exact-title text cheats. |
| gamegenie.53lu.com 3DS list crawl | No exact title-ID matches for the unresolved Thor gaps. |
| GameBrew NTR Plugins Collections, https://www.gamebrew.org/wiki/NTR_Plugins_Collections_3DS | Some exact IDs exist only as `.plg` NTR plugins; those are not Azahar text cheat files and need conversion or live research before bundling. |
| EtherealGames CTRPF index, https://etherealgames.com/ctrpf-ar-code-index/ | Blocked by HTTP 403 during automated fetch. |

## Still Missing Exact Text Cheats

| Title ID | Game | Current status |
| --- | --- | --- |
| `0004000000095800` | Art Academy - Lessons for Everyone | No exact text cheat found. |
| `00040000000E7600` | Attack of the Friday Monsters! A Tokyo Tale | No exact text cheat found. |
| `0004000000096600` | Castlevania - Lords of Shadow - Mirror of Fate | No exact text cheat found. |
| `000400000004D200` | Cave Story 3D | NTR plugin list has this exact EUR ID; no Azahar-ready text cheat found. |
| `0004000000132500` | Code Name: S.T.E.A.M. | NTR plugin list has this exact ID; no Azahar-ready text cheat found. |
| `00040000000BBF00` | Crimson Shroud | No exact text cheat found. |
| `00040000001C1E00` | Detective Pikachu | No exact text cheat found. |
| `0004000000056200` | Doctor Lautrec and the Forgotten Knights | No exact text cheat found. |
| `00040000000C2D00` | HarmoKnight | No exact text cheat found. |
| `0004000000074000` | Heroes of Ruin | No exact text cheat found. |
| `00040000000F9900` | Hometown Story | NTR plugin list has this exact ID; no Azahar-ready text cheat found. |
| `00040000001CBC00` | Jake Hunter Detective Story - Ghost of the Dusk | No exact text cheat found. |
| `00040000001D1900` | Luigi's Mansion | Public request thread found, but no posted text cheat. |
| `00040000001C4E00` | Mario Party - The Top 100 | No exact text cheat found. |
| `0004000000188C00` | Mario Sports Superstars | No exact text cheat found. |
| `0004000000174F00` | Medabots9-KWG-1007 English patch | No exact text cheat found. |
| `0004000000182800` | Petit Novel series - Harvest December | No exact text cheat found. |
| `00040000000D0900` | Pokemon Art Academy | No exact text cheat found. |
| `00040000000F3000` | Professor Layton and the Azran Legacy | Exact ID is EUR; NTR plugin list has USA `00040000000F2F00`, so do not import it. |
| `00040000000A8500` | Professor Layton and the Miracle Mask | No exact text cheat found. |
| `0004000000160C00` | Project X Zone 2 | NTR plugin list has this exact USA ID; no Azahar-ready text cheat found. |
| `0004000000036400` | Rayman 3D | No exact text cheat found. |
| `000400000018CC00` | Return to PopoloCrois - A Story of Seasons Fairytale | NTR plugin list has this exact ID; no Azahar-ready text cheat found. |
| `0004000000067600` | Rhythm Thief & the Emperor's Treasure | No exact text cheat found. |
| `00040000000B3500` | Sonic & All-Stars Racing Transformed | No exact text cheat found. |
| `00040000000D9900` | The Starship Damrey | No exact text cheat found. |
| `0004000000115100` | Weapon Shop de Omasse | No exact text cheat found. |
| `0004000000188500` | Yu-Gi-Oh English Patched | NTR plugin list has this exact Japanese ID; no Azahar-ready text cheat found. |
| `0004000000096700` | Zero Escape - Virtue's Last Reward | No exact text cheat found. |

## Invalid / Needs Re-scan

| Title ID | Game | Current status |
| --- | --- | --- |
| `0004000000000000` | Tales of the World: Reve Unitia English patch | Parsed as all-zero title ID; treat as invalid for cheat matching until the ROM header is rechecked. |
