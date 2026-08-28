# Follower / Continue — sprites pretos (e depois “tudo RED”)

Documento para **outra branch** (ex. `follower-overworld` em `eaac0e75f`) e para **prompt de agente**.  
Não misturar com NPC Arthorios nem com Agenda/Conector.

Última atualização: 2026-08-27.

possivel causa analisada pelo arthorios: A causa era o save, não a paleta: graphicsId no FRLG é u8. O port do follower copiou o swap de 16 bits do emerald (id << 8), que no byte único vira 0 = gráfico do RED. O warp ignora isso e lê os templates do mapa, por isso a casa “corrigia”.
---

## Prompt para o agente

```
Contexto: pokefirered (pret) + feature de Pokémon seguidor (OW followers, OW_GFX_COMPRESS).
Branch alvo: a do commit do follower, SEM multiplayer e SEM NPC Arthorios.

Bug: ao Continue / ao arrancar a ROM no overworld, os sprites de pessoas (NPCs + por vezes o follower) saem ERRADOS. Entrar e sair de uma casa (warp LoadMapInStepsLocal) CORRIGE.

Sintomas observados nesta ordem:
1) Sprites de pessoas SOLIDAMENTE PRETOS no Continue.
2) Depois de um “fix” de paletteSlot em SpawnObjectEventOnReturnToField, os mesmos sprites passaram a parecer o jogador (RED): NPCs com graphic de RED; Squirtle follower com o chapéu/cara do RED no topo da cabeça (tiles a partilhar VRAM com o player).

Objetivo: Continue deve mostrar NPCs e follower com os gráficos certos, igual após um warp indoor↔outdoor. Não regressar ao preto nem ao “tudo RED”.

NÃO reaplicar às cegas as tentativas listadas em “O que já foi tentado”. Compara o caminho Continue vs Warp. O warp que funciona é InitObjectEventsLocal → TrySetupObjectEventSprite. O Continue que falha é SpawnObjectEventsOnReturnToField.

Ficheiros-chave:
- src/overworld.c (ReturnToFieldLocal, LoadMapInStepsLocal, InitObjectEventsLocal, ResumeMap, InitViewGraphics)
- src/event_object_movement.c (TrySetupObjectEventSprite vs SpawnObjectEventOnReturnToField)
- src/load_save.c (SaveObjectEvents / LoadObjectEvents, byte-swap graphicsId, follower)
- src/data/object_events/follower_core.inc (UpdateFollowingPokemon, RestoreFollowerObjectEventGraphics, LoadSheetGraphicsInfo)
- src/sprite.c (OW_GFX_COMPRESS, usingSheet, sheetSpan)

Teste: Continue num save em Pallet com party (follower visível). Sem entrar em casa. NPCs (Oak/girl/etc.) e o mon devem estar corretos. Depois entrar e sair de casa — não piorar.
```

---

## Sintomas (reproduzir)

| Quando | O que se vê |
|--------|-------------|
| **Continue** / boot da ROM no overworld | Pessoas erradas. Primeiro: **preto**. Depois do patch de paleta: **gráfico do RED**. Follower (ex. Squirtle) com **metade da cabeça do RED** (tiles misturados). |
| **Warp** (entrar e sair de casa) | Sprites **corretos**. |
| Start menu / fade | Pode “descongelar” ou recarregar OAM; não é a causa raiz. |

Isto **não** é o retângulo preto da Agenda (Yes/No). Esse é UI (`DestroyYesNoMenu` a furar o tilemap). Este doc é só **object events no overworld**.

---

## Causa raiz (hipótese principal)

A feature **follower** portou de pokeemerald-expansion:

- `OW_GFX_COMPRESS` + `LoadSheetGraphicsInfo` (sheets comprimidos, `tileTag`, `usingSheet`, `sheetSpan`)
- `OBJ_EVENT_GFX_MON_BASE` (0x200) nos `graphicsId`
- `SaveObjectEvents` / `LoadObjectEvents` com **byte-swap** de `graphicsId` e follower especial
- Continue usa `SpawnObjectEventOnReturnToField`; warp usa `TrySetupObjectEventSprite`

Vanilla FRLG no Continue **não** passava por sheets comprimidos. O caminho Continue ficou **incompleto** em relação ao warp:

1. **Tiles / OBJ VRAM** — se `LoadSheetGraphicsInfo` falha, reutiliza tag, ou não define `usingSheet`/`sheetSpan`/`oam.tileNum`, todos os sprites apontam para os tiles do **player (RED)**. Preto = tiles a 0 / paleta 0; RED = `tileNum` do jogador.
2. **Paleta** — o warp faz `sprite->oam.paletteNum = graphicsInfo->paletteSlot` **depois** de `CreateSprite`. O Continue **não fazia** isso. Forçar `paletteSlot` no Continue (tentativa nesta branch) passou de preto → **todos com cara de RED** (paleta/slot do player a vazar, ou a mascarar o bug de tiles).
3. **Ordem Continue** — vanilla: spawn de objetos **antes** de `InitViewGraphics` (`DISPCNT_OBJ_1D_MAP` + tilesets). Com `OW_GFX_COMPRESS`, spawn antes do 1D pode dar lixo. Reordenar spawn **depois** de `InitViewGraphics` foi tentado; **não** fechou o bug (e pode ter piorado tiles).

O follower agrava: `RestoreFollowerObjectEventGraphics` no load; `RemoveFollowingPokemon` + `UpdateFollowingPokemon` a seguir ao spawn; dois spawns do mesmo mon; sheet dinâmico (`OBJ_EVENT_PAL_TAG_DYNAMIC`) vs NPCs `compressed = FALSE`.

NPCs de pessoas no `object_event_graphics_info.h` têm `.compressed = FALSE`. Mesmo assim o Continue chama `LoadSheetGraphicsInfo` em `SpawnObjectEventOnReturnToField` (`#if OW_GFX_COMPRESS`). Se o `tileTag` for `TAG_NONE` e `compressed` for FALSE, `LoadSheetGraphicsInfo` só entra se `tag != TAG_NONE || info->compressed` — com ambos falsos **não carrega sheet** e o sprite pode ficar com tiles default (player / 0).

---

## Dois caminhos de spawn (comparar lado a lado)

### Warp (funciona) — `LoadMapInStepsLocal`

1. `ResumeMap` → `ResetSpriteData`, `InitObjectEventPalettes(0)`
2. `InitObjectEventsLocal` → `ResetObjectEvents`, `InitPlayerAvatar`, `TrySpawnObjectEvents`, `UpdateFollowingPokemon`
3. `TrySetupObjectEventSprite`:
   - `LoadSheetGraphicsInfo` se `OW_GFX_COMPRESS`
   - `paletteTag = TAG_NONE` **antes** de `CreateSprite`
   - depois `paletteNum = graphicsInfo->paletteSlot`
   - `sheetSpan` se `compressed && usingSheet`
4. Depois: `InitOverworldGraphicsRegisters` (OBJ 1D), tilesets, `DrawWholeMapView`

Nota: no warp os objetos nascem **antes** dos registos 1D, mas o mapa **anterior** já estava em 1D. No **primeiro** Continue a seguir ao title, o DISPCNT ainda pode ser o do menu (2D).

### Continue (falha) — `CB2_ContinueSavedGame` → `CB2_ReturnToField` → `ReturnToFieldLocal`

1. `LoadObjectEvents` (swap `graphicsId`, follower)
2. `ResumeMap` (reset sprites + paletas patched 0–9)
3. `ReloadObjectsAndRunReturnToFieldMapScript` → `SpawnObjectEventsOnReturnToField` para **cada** `gObjectEvents[i].active`
4. `SpawnObjectEventOnReturnToField` (código **diferente** do warp):
   - `RestoreFollowerObjectEventGraphics` se follower
   - `LoadSheetGraphicsInfo` + `LoadObjectEventPalette` + `CreateSprite` **com paletteTag ainda no template**
   - **não** punha `paletteNum = paletteSlot` (vanilla+follower port)
   - follower dinâmico: `LoadDynamicFollowerPalette`
5. `RemoveFollowingPokemon` + `UpdateFollowingPokemon` (segundo spawn do follower)
6. `InitViewGraphics` (vanilla: **depois** dos sprites)

---

## O que já foi tentado (nesta branch `arthorios` / working tree)

Tratar como **experiências**, não como solução final. Na branch do follower, **rever o diff** antes de copiar.

### Tentativa A — `ReturnToFieldLocal` (`src/overworld.c`)

Vanilla: spawn no case 0; `InitViewGraphics` no case 2.

Alterado para: case 0 só `ResumeMap`; case 2 `InitViewGraphics` **depois** `ReloadObjects` + follower + `SetCameraToTrackPlayer`.

Intenção: OBJ 1D + tilesets antes de `OW_GFX_COMPRESS`.  
Resultado: **não** resolveu o Continue. Manter ou reverter conforme comparação com pret vanilla + expansion.

### Tentativa B — `paletteNum = paletteSlot` em `SpawnObjectEventOnReturnToField`

Espelhar o warp (`TrySetupObjectEventSprite` ~linha 1676). Também `LoadPlayerObjectReflectionPalette` / `LoadSpecialObjectReflectionPalette`.

Resultado: preto → **todos RED** / follower com cabeça do RED.  
Conclusão: paleta fixa **sem** tiles/sheet corretos puxa o gráfico do player. **Não deixar este patch sozinho.** O warp também faz `paletteTag = TAG_NONE` **antes** do `CreateSprite`; o Continue **não**. Copiar só o `paletteSlot` é incompleto.

### O que o warp faz e o Continue ainda não (checklist)

- [ ] `*(u16 *)&spriteTemplate->paletteTag = TAG_NONE` antes de `CreateSprite`
- [ ] `paletteNum = graphicsInfo->paletteSlot` **depois**, com as mesmas exceções DYNAMIC / SUBSTITUTE
- [ ] `sheetSpan` só se `compressed && usingSheet` (não em todo `usingSheet`)
- [ ] `LoadPlayerObjectReflectionPalette` / special **antes** do create, como no warp
- [ ] Follower: um único spawn (`UpdateFollowingPokemon`) vs spawn-do-save + remove + create
- [ ] `LoadSheetGraphicsInfo` para NPCs **não comprimidos** (`compressed == FALSE`, `tileTag == TAG_NONE`): o Continue não deve forçar sheet do player
- [ ] `LoadObjectEvents` byte-swap vs `graphicsId >= 0x200` (follower)
- [ ] Ordem DISPCNT_OBJ_1D vs `CreateSprite` no **primeiro** load após title

---

## Ficheiros e funções (mapa)

| Ficheiro | Função | Papel |
|----------|--------|--------|
| `overworld.c` | `ReturnToFieldLocal` | Continue / return to field |
| `overworld.c` | `LoadMapInStepsLocal` | Warp / new map (OK após 1ª casa) |
| `overworld.c` | `InitObjectEventsLocal` | Spawn “limpo” a partir dos templates |
| `overworld.c` | `ResumeMap` | `ResetSpriteData` + `InitObjectEventPalettes` |
| `overworld.c` | `InitViewGraphics` | 1D + tilesets + `DrawWholeMapView` |
| `event_object_movement.c` | `TrySetupObjectEventSprite` | **Referência correta** (warp) |
| `event_object_movement.c` | `SpawnObjectEventOnReturnToField` | **Continue — alinha com o de cima** |
| `load_save.c` | `SaveObjectEvents` / `LoadObjectEvents` | Follower `active=FALSE` no save; restore no load; swap 16-bit |
| `follower_core.inc` | `LoadSheetGraphicsInfo` | Tag `0xCE00 + uuid`; compress |
| `follower_core.inc` | `UpdateFollowingPokemon` | Cria `OBJ_EVENT_ID_FOLLOWER` |
| `follower_core.inc` | `RestoreFollowerObjectEventGraphics` | Reconstroi `graphicsId` da party |

Commit de referência do follower (sem Arthorios): `eaac0e75f` (`feat(follower): adiciona Pokémon seguidor no overworld`). Branch local: `follower-overworld`.

---

## Estratégia recomendada (para o agente)

1. Diff `TrySetupObjectEventSprite` vs `SpawnObjectEventOnReturnToField`. O Continue deve seguir a **mesma** sequência de paleta/tag/sheet que o warp, não um subset.
2. Confirmar no debugger/mGBA: `sprite->oam.tileNum`, `usingSheet`, `sheetTileStart`, `paletteNum` do player vs NPC vs follower no Continue vs após warp.
3. Se NPCs `compressed == FALSE`: no Continue, **não** chamar `LoadSheetGraphicsInfo` de forma que partilhe o sheet do player; usar o mesmo `CreateSprite` + `images` + `paletteSlot` que o warp.
4. Follower: depois dos NPCs estáveis, `RemoveFollowingPokemon` + `UpdateFollowingPokemon` **uma vez**, com `FollowerSetGraphics` / paleta dinâmica.
5. Não “consertar” com `DmaClear` VRAM no overworld (destrói mapas). Não usar `ShowFieldMessage` no handshake (outro bug).
6. Testar Continue **sem** warp. Depois indoor. Depois save/load outra vez.

---

## Não confundir com o cabo (2.º jogador)

Quando o outro mGBA liga, os clones RED + follower partido **já vinham do Continue**. O handshake **não** chama `SpawnLinkPlayers`.

O que o cabo *pode* acrescentar: **buracos pretos no mapa** (casa, cerca) se alguém desenhar janela em BG0 (`CopyWindowToVram`) ao mesmo tempo que `OpenLink`/`LinkVSync`. Popup de “X is online” no campo foi removido por isso. Ver `arthorios/docs/agenda-multiplayer.md` §8.4.

---


## Fora de âmbito

- Agenda Yes/No (buraco preto na lista) — `phone.c` `Phone_HandleRemoveConfirm`
- Handshake cabo / `OpenLink` no overworld
- NPC Arthorios em Pallet
