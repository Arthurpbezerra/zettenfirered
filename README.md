# Pokémon FireRed — Arthorios

A fan ROM of **Pokémon FireRed** built on the [pret/pokefirered](https://github.com/pret/pokefirered) decompilation. The Kanto story, maps, and battle engine stay vanilla; this fork adds a **following Pokémon** on the overworld (ported from Emerald / pokeemerald-expansion), a **link-cable multiplayer** stack that lives in the field instead of the Pokémon Center, and a Pallet Town NPC who hands out the new key items.

The project is a work in progress. Following Pokémon and the Agenda/Connector loop are playable today. Co-op presence is limited to Pallet Town and Route 1. Wireless Adapter (RFU) scan is not implemented yet.

**This repository does not contain a ROM.** You must build from source (see [Building](#building)). Pokémon is © Nintendo / Game Freak / The Pokémon Company.

---

## Table of contents

- [What this fork changes](#what-this-fork-changes)
- [Following Pokémon (Emerald-style)](#following-pokémon-emerald-style)
- [Connector and Trainer Agenda](#connector-and-trainer-agenda)
- [Overworld co-op (v1)](#overworld-co-op-v1)
- [Arthorios in Pallet Town](#arthorios-in-pallet-town)
- [Other changes](#other-changes)
- [Current status](#current-status)
- [Building](#building)
- [Playing with two people](#playing-with-two-people)
- [Documentation](#documentation)
- [Credits and collaborators](#credits-and-collaborators)
- [Upstream decompilation](#upstream-decompilation)
- [Legal](#legal)

---

## What this fork changes

Vanilla FireRed only lets two players meet inside a Pokémon Center Cable Club. Emerald-style hacks (and HeartGold/SoulSilver) let a party Pokémon walk behind you on routes. This project brings both ideas into FireRed without replacing the original story:

| Feature | What it does |
|---------|----------------|
| **Following Pokémon** | The first living Pokémon in the party walks behind the player on towns and routes, using Emerald overworld sprites for all 151 Kanto species. |
| **LINK CONNECTOR** | Key item that keeps a serial/link session open in the overworld. Register it to **Select**, like the Bicycle. |
| **TRAINER AGENDA** | Key item that lists saved contacts, who is online, and starts a battle or trade without walking to a Pokémon Center. |
| **Co-op presence** | On Pallet Town and Route 1, the other trainer’s avatar is drawn on your map while both Connectors are on. |
| **Arthorios NPC** | Standing in Pallet Town. Gives the Connector and Agenda, then lets the player pick any Kanto species at level 1. |
| **Title logo** | Custom FireRed title graphic packed with the vanilla tile layout (`arthorios/tools/make_title_logo.py`). |

Everything else — gyms, story flags, Safari Zone, Union Room, Mystery Gift — is still the pret FireRed engine.

---

## Following Pokémon (Emerald-style)

### The idea

Pokémon Emerald (and later HGSS) can show a party member on the field. The player is not alone on routes: the lead Pokémon copies facing and step timing, hides while surfing or biking, and swaps graphics when the party order changes.

FireRed never shipped that system. **pokeemerald-expansion** (the rh-hideout Emerald fork) later rebuilt it as a first-class overworld feature: compressed sprite sheets, a dedicated follower object-event slot, and a graphics table for every species. This ROM **ports that model into FireRed**, not a from-scratch clone of HGSS.

The Emerald design that was kept:

- The follower is the **first living Pokémon** in the party (`GetFirstLiveMon`). Faint the lead and the next healthy mon takes the slot.
- Graphics come from **Emerald-style overworld sheets** (`graphics/object_events/pics/pokemon/overworld/`), generated from a pokeemerald tree by `tools/gen_follower_headers.py`.
- Sprites are **LZ-compressed** (`OW_GFX_COMPRESS`) so 151 sheets fit in ROM without exploding the object-event budget.
- The follower uses a reserved local ID (`OBJ_EVENT_ID_FOLLOWER`) and movement type `MOVEMENT_TYPE_FOLLOW_PLAYER`, same idea as expansion.
- **Indoors are off.** `OW_MON_OUTDOORS_ONLY` is `TRUE`: the mon appears on `MAP_TYPE_TOWN` and `MAP_TYPE_ROUTE` only. Houses, gyms, and caves stay empty of the follower so indoor collision and cutscenes stay vanilla.
- The mon is **hidden** while surfing, underwater, on a bike, or during forced movement (`FOLLOWER_INVISIBLE_FLAGS`).
- Scripts that must not desync the follower set `FLAG_SAFE_FOLLOWER_MOVEMENT` (Oak’s lab starter cutscene, trainer-battle intro). Other scripts can hide it with `FLAG_TEMP_HIDE_FOLLOWER`.

Species without a dedicated sheet fall back to a substitute graphic so the object event never points at garbage VRAM.

### Why a port is not a copy-paste

FireRed’s object-event `graphicsId` is an **8-bit** field. Emerald expansion stores species followers with IDs at `OBJ_EVENT_GFX_MON_BASE` (0x200) and a 16-bit save swap. Naively copying that swap into FireRed turns the byte into `0` — which is Red’s overworld graphic. Warps look fine because they respawn from map templates; **Continue** reloads saved object events and used to show black NPCs, then every NPC as Red.

That bug is tracked separately from the link cable. See [arthorios/docs/follower-ow-sprites.md](arthorios/docs/follower-ow-sprites.md). If sprites look wrong after Continue, enter and leave a building (warp path) as a workaround until the Continue spawn path matches the warp path.

### Main files

| Path | Role |
|------|------|
| `src/data/object_events/follower_core.inc` | Spawn, graphics, hide/show, first-live-mon |
| `src/data/object_events/object_event_graphics_*_followers.h` | Generated INCBINs, pic tables, graphics info |
| `tools/gen_follower_headers.py` | Rebuilds those headers from a sibling `pokeemerald` tree |
| `src/event_object_movement.c` / `src/overworld.c` / `src/load_save.c` | Integration with warp, Continue, and save |
| `include/constants/event_objects.h` | `OW_MON_OUTDOORS_ONLY`, follower local ID |

---

## Connector and Trainer Agenda

Vanilla Cable Club is a room you warp into. This fork treats “I am online” as a **key item state** that can stay up while you walk.

### LINK CONNECTOR (`ITEM_CONNECTOR`)

- Key item. Toggle **online / offline**.
- Registrable on **Select** (shares the register slot with the Bicycle).
- While ON, `LinkSession` owns the serial hardware (`OpenLink` / `CloseLink` / `ResetSerial`). Nothing else is allowed to open or close SIO during a session.
- Blocked in Union Room and Safari Zone so those vanilla modes are not fighting the custom session.

### TRAINER AGENDA (`ITEM_TRAINER_AGENDA`)

- Key item. Opens a list UI (not a field popup — popups on BG0 while the cable is live corrupt the map).
- Shows **Connector ON/OFF**, saved **contacts**, and **people currently on the link**.
- After the first successful handshake, the other trainer is **written into the save** (up to 20 contacts in `SaveBlock2.phone`, magic `PHN1`).
- On an online contact: **Battle**, **Trade**, or **Remove**.
- Incoming battle/trade prompts open **inside the Agenda** (Yes/No on the app channel). The other player does not need to be standing in a Pokémon Center.
- **Select** on the Agenda screen opens **link diagnostics** (`LinkDiag`: session/packet/error counters).

Battle and trade **do not drop the cable**. The session enters a barrier, hands `gLinkType` to the vanilla Colosseum or Trade Center engine, then resumes `LINKTYPE_PHONE` (0x7701) when you return to the field.

### Stack (who owns what)

```
UI / game
  phone.c          Agenda, Connector, battle/trade requests
  overworld.c      remote avatar, return from club
  field + wild     suppress trainers / scripts / encounters in co-op
        │
        ▼
LinkCoop     presence packets (pose, map, gender)
LinkProto    versioned datagrams (control / presence / coop / app)
LinkDiag     counters; vanilla link-error UI suppressed during a session
        │
        ▼
LinkSession  only module that touches SIO
        │
        ▼
Vanilla link.c, Cable Club, battle, trade
```

Protocol version is checked on connect. A mismatch shows *“Partner ROM is a different version.”* Use the same build on both machines.

Timeouts have two profiles: **LOCAL** (two mGBA windows) and **REMOTE** (RetroArch netplay, ~3× longer budgets). Short timeouts look like a dropped cable on the internet.

Save layout: `PhoneSaveData` occupies `0x400` bytes in SaveBlock2 (replaces `filler_B20`). Total save size is unchanged (`0xF24`). Old saves without the magic are initialized the first time the Agenda opens.

Implementation notes, file map, and test checklists live in [arthorios/docs/agenda-multiplayer.md](arthorios/docs/agenda-multiplayer.md).

---

## Overworld co-op (v1)

This is **mirrored presence**, not a shared world.

While the session is established and **both players are on the same whitelisted map**, each side sends a presence packet about every 24 frames (~0.4 s): map, facing, gender, step destination, a one-shot RNG seed. The other game draws one `LinkPlayerObjectEvent` at that pose.

**Whitelist (hardcoded):** Pallet Town, Route 1.

While co-op is active on that map:

- Wild encounters are off.
- Overworld trainer eyes and on-frame / step scripts are off.

That is intentional for v1: two players walking through Pallet should not start Oak’s cutscene twice or fight the same bug catcher on desynced RNG.

What v1 does **not** do (on purpose):

- Synchronized warps or a shared save.
- Smooth walk interpolation (the remote avatar hops to the last packet).
- Presence timeout (if packets stop, the avatar freezes in place).
- Four-player Connector sessions.
- Vanilla `SpawnLinkPlayers()` on the field (that path spawned extra Red clones).

---

## Arthorios in Pallet Town

An NPC named **Arthorios** stands in Pallet Town (`OBJ_EVENT_GFX_ARTHORIOS`).

1. First talk: he gives **CONNECTOR** and **AGENDA**.
2. Every talk after that: a species list of Kanto Pokémon. Confirm and you receive that species at **level 1** (party or PC).

This is a debug/gift NPC for development and local play, not a story character. Dialogue is currently mixed Portuguese/English.

---

## Other changes

- **Title screen logo** rebuilt so the version label sits under the Pokémon mark the way vanilla FireRed packed the 256×64 sheet (`arthorios/tools/make_title_logo.py`).
- **Sprite resource pack** under `arthorios/` (FRLG Accurate NPC Megapack and related sheets) for custom overworld graphics. See credits below.
- Dummy Agenda contacts (RED / BLUE / GREEN) are still seeded on new game; they will be removed in polish.

---

## Current status

| Area | Status |
|------|--------|
| Following Pokémon on towns/routes | Done (Continue sprite bug still open) |
| Connector + Agenda offline | Done |
| Cable handshake, auto-add contact | Done |
| Battle / trade from Agenda (handoff) | Implemented; needs full netplay soak |
| Co-op avatar Pallet + Route 1 | Partial (no interpolation / presence timeout) |
| Hide follower while Connector is ON | Not done |
| Wireless Adapter / RFU scan | Not started |
| Portuguese UI polish, unique item icons | Not done |

**Known issues (short list):**

- Continue/boot on the overworld can show **black NPCs** or **everyone as Red**, including a broken follower head. Warping indoor/outdoor fixes it. Not caused by the link cable. Details: [follower-ow-sprites.md](arthorios/docs/follower-ow-sprites.md).
- Remote co-op avatar **teleports** every ~0.4 s.
- Connector and Bicycle share one Select register slot.
- Do **not** draw field message windows at the same time as `OpenLink` (black rectangles on houses/fences). Incoming online notices stay in the Agenda.

---

## Building

Toolchain and OS setup: **[INSTALL.md](INSTALL.md)** (WSL is the usual path on Windows).

From the repo root, after the pret environment is installed:

```bash
make pokefirered.gba -j16
```

Output: `pokefirered.gba` at the repository root.

This fork does **not** match the retail FireRed SHA-1. The hashes in the [upstream pret README](https://github.com/pret/pokefirered) apply only to an unmodified decompilation.

For two-player tests, copy the ROM twice and use **two different saves** (different names and trainer IDs).

---

## Playing with two people

Give both players the Connector and Agenda (talk to Arthorios, or use a save that already has the items).

### Local (mGBA)

1. Open ROM A. **File → New multiplayer window** → ROM B.
2. Confirm the emulator link indicator is active.
3. Both trainers: Connector **ON**, standing in the overworld (Pallet is enough).
4. Wait a few seconds. Open the Agenda: the other name should show as online.
5. Battle or Trade from the contact menu; the other side accepts in their Agenda.

### Remote (RetroArch)

1. Core: **gpSP**.
2. Core Options → **Link Cable Connectivity** → `mul_poke` (this is the cable path; `rfu` is Wireless Adapter and is not used by the Agenda yet).
3. Same ROM on host and guest. Netplay host/join.
4. Same in-game steps as local. If the link drops immediately, the session may still be on the LOCAL timeout profile.

Hardware (two GBAs + link cable) is expected to work for handshake/battle/trade; it is not the primary test setup.

---

## Documentation

| Document | Contents |
|----------|----------|
| [INSTALL.md](INSTALL.md) | Compilers, WSL, msys2, Cygwin |
| [arthorios/docs/agenda-multiplayer.md](arthorios/docs/agenda-multiplayer.md) | Architecture, phases, test checklists, backlog |
| [arthorios/docs/follower-ow-sprites.md](arthorios/docs/follower-ow-sprites.md) | Continue vs warp sprite bug (follower port) |

---

## Credits and collaborators

This ROM exists because several independent communities already solved the hard parts. Names below are the people and groups this fork actually depends on.

### This fork

| | |
|--|--|
| **Arthur Pinto Bezerra** ([Arthorios](https://github.com/Arthurpbezerra)) | Project lead. Port of following Pokémon into FireRed, Connector/Agenda multiplayer (`LinkSession`, `LinkProto`, `LinkCoop`, `LinkDiag`, `phone.c`), Pallet Town NPC, title logo tooling, and the design docs under `arthorios/docs/`. |

### pret — FireRed / LeafGreen decompilation

The entire engine, maps, and matching build system come from **[pret/pokefirered](https://github.com/pret/pokefirered)**.

Contacts and other pret projects: [pret.github.io](https://pret.github.io/).

A small sample of people with a large footprint on this tree (commit counts on the imported history, not a ranking of worth): **PikalaxALT**, **GriffinR** (Martin Griffin), **jiangzhengwenjz**, **Evan**, **garak**, **Kurausukun**, **ultima-soul**, **scnorton**, **cbt6**, **Eduardo Quezada**, **ProjectRevoTPP**, **luckytyphlosion**, **SatoMew**, **Deokishisu**, **LOuroboros**, **SphericalIce**, **Squeetz**, **hedara90**, **Marcus Huderle** (porymap), and many others listed in `git log`. If you contributed to pret and your name is missing here, that is a documentation gap, not a claim that the work is unused.

### Following Pokémon — Emerald lineage

The follower is an **Emerald idea**, implemented for modern decomp in **[rh-hideout/pokeemerald-expansion](https://github.com/rh-hideout/pokeemerald-expansion)**.

| | |
|--|--|
| **ghoulslash** | Original following-Pokémon work in the Emerald community; graphics and engine pieces this port still resembles. |
| **pokeemerald-expansion maintainers and contributors** | `follower_core`, `OW_GFX_COMPRESS` sheet loading, species graphics tables, and the overall “party mon as object event” contract. Notable expansion contributors include **DizzyEgg**, **AsparagusEduardo** (Eduardo Quezada), **ghoulslash**, **Pawkkie**, **Bassoonian**, **AgustinGDLV**, **Jaizu**, and the [full expansion credits](https://github.com/rh-hideout/pokeemerald-expansion/blob/master/CREDITS.md). |
| **pret/pokeemerald** | The Emerald decompilation expansion itself sits on. |

Overworld Pokémon sprites in this ROM were taken from that Emerald/expansion pipeline (Kanto subset), not drawn from scratch for FireRed.

### Overworld NPC and trainer sprites

Custom and adapted NPC sheets under `arthorios/` come from the **FRLG Accurate NPC Megapack** compiled by **SoulfulLex**, who asked that original spriters be credited rather than the pack itself.

Sprite work used or bundled in that pack includes (non-exhaustive; see `arthorios/MegaPack Readme.txt`):

**Avatar**, **Aveontrainer**, **Daman**, **Delta231**, **GreenWithAwesome / 15avaughn**, **Hemoglobin_A1C**, **hyo-oppa**, **Kalarie**, **Kimoras**, **KisiroKitsune**, **M.vit**, **Marnic**, **Mimi**, **MrDollSteak**, **Mr. Gela / theo**, **Othienka**, **Ozander**, **Poffin_Case**, **Pokésho**, **Reign**, **Solo993**, **Spherical Ice**, **Tsuka**, and others named in that readme.

If a sprite in `arthorios/` is used in-game, credit those artists. Do not credit this fork as the original author of those sheets.

### Tools and adjacent projects

- **[porymap](https://github.com/huderlem/porymap)** (Marcus Huderle and contributors) — map editing against pret JSON.
- pret toolchains in `tools/` (gbagfx, preproc, scaninc, ramscrgen, and the rest) — same as upstream pokefirered.
- **mGBA** and **RetroArch / gpSP** — the emulators this multiplayer is developed and tested against (`mul_poke` cable netplay).

---

## Upstream decompilation

Unmodified pret FireRed/LeafGreen builds these retail-matching images (for reference only; **this fork’s `pokefirered.gba` will not hash-match**):

* [**pokefirered.gba**](https://datomatic.no-intro.org/?page=show_record&s=23&n=1616) `sha1: 41cb23d8dccc8ebd7c649cd8fbb58eeace6e2fdc`
* [**pokeleafgreen.gba**](https://datomatic.no-intro.org/?page=show_record&s=23&n=1617) `sha1: 574fa542ffebb14be69902d1d36f1ec0a4afd71e`
* [**pokefirered_rev1.gba**](https://datomatic.no-intro.org/?page=show_record&s=23&n=1672) `sha1: dd5945db9b930750cb39d00c84da8571feebf417`
* [**pokeleafgreen_rev1.gba**](https://datomatic.no-intro.org/index.php?page=show_record&s=23&n=1668) `sha1: 7862c67bdecbe21d1d69ce082ce34327e1c6ed5e`
* [**pokefirered_switch.gba**](https://datomatic.no-intro.org/index.php?page=show_record&s=23&n=x550) `sha1: baa452d0b24629dd7782cfc07a8984085dde1311`
* [**pokeleafgreen_switch.gba**](https://datomatic.no-intro.org/index.php?page=show_record&s=23&n=x551) `sha1: 62b9fc77549dbc67032eb6cbd0ea6ad3b825690f`

Setup questions that are about the compiler, not about Agenda or followers, are still answered by pret’s [INSTALL.md](INSTALL.md) and [pret.github.io](https://pret.github.io/).

---

## Legal

Pokémon FireRed, LeafGreen, Emerald, and all related characters, names, and assets are copyright of **Nintendo**, **Game Freak**, and **The Pokémon Company**. This is a non-commercial fan project. It is not affiliated with or endorsed by those companies.

Do not distribute a built `.gba` that contains Nintendo’s copyrighted data if that would violate your local law. This repository is source and documentation only.
