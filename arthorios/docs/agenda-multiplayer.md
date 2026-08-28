# Agenda + Conector — plano de entregas e testes

Documento de planejamento do multiplayer local via dois key items.
Foco de teste: **emulador** (mGBA cabo/netplay e RetroArch + gpSP).
Hardware cabo GBA e Wireless Adapter entram como validação extra, não como pré-requisito.

Última atualização: 2026-08-27.

---

## 1. Objetivo

Dois key items:

| Item | Papel | Uso |
|------|--------|-----|
| **Conector** | Liga/desliga “estou online” | Mochila ou **Select** (registrado, como bicicleta) |
| **Agenda** | Lista contatos + pessoas online; batalha / troca / adicionar | Mochila (menu completo) |

Após o primeiro contato bem-sucedido (ou “Adicionar”), o outro treinador fica salvo no save.

Não é overworld compartilhado. É **descoberta + sessão de link** (batalha/troca) usando o engine vanilla, sem warp obrigatório à Union Room.

---

## 2. Princípio das entregas

Cada fase:

1. Compila (`make pokefirered.gba`).
2. Tem **comportamento visível** no jogo (texto, lista, ícone).
3. Tem **checklist de teste** neste arquivo.
4. Pode ser jogada **sem** a fase seguinte.

Não misturar UI, save, RFU e cabo na mesma entrega.

---

## 3. UX alvo (Agenda)

```
┌─ AGENDA ─────────────────────────────┐
│ CONECTOR: ON / OFF                   │
│                                      │
│ CONTATOS                             │
│  ● RED        ID: 38427   [online]   │
│  ○ BLUE       ID: 52891   [offline]  │
│                                      │
│ PESSOAS ONLINE                       │
│  ● ASH         ID: 10234  [novo]     │
│                                      │
│ A: ação   B: sair                    │
└──────────────────────────────────────┘

Contato online  → Batalhar / Trocar / Remover
Desconhecido    → Adicionar / Batalhar / Trocar
Contato offline → Remover (sem ligar)
```

Estados:

- **● Online** — Conector dos dois ON **e** sessão de link detectada (cabo/netplay) **ou** beacon RFU (wireless).
- **○ Offline** — salvo, não detectado agora.
- **Novo** — online, ainda não é contato.

---

## 4. Limites do emulador (importante para testes)

| Setup | O que simula | Agenda “online” |
|-------|----------------|-----------------|
| **mGBA** (2 janelas / netplay) | **Cabo link** | Só depois de handshake; sem scan passivo |
| **RetroArch + gpSP** `mul_poke` | **Cabo** via netpacket | Igual cabo |
| **RetroArch + gpSP** `rfu` | **Wireless Adapter** | Scan / Union Room possível |
| **mGBA core no RetroArch** | Sem RFU Pokémon | Não usar para Union Room |

Netplay RetroArch **não** é um único protocolo: no gpSP você escolhe cabo (`mul_poke`) ou wireless (`rfu`).

Fases 1–2 testam **1 ROM**. Fases 3+ precisam **2 instâncias**.

---

## 5. Dados no save

`SaveBlock2` tem `filler_B20[0x400]` (offset `0xB20`). Contatos cabem aí **sem** quebrar tamanho do save (`0xF24`).

Proposta (ajustar no código da Fase 1):

```c
#define PHONE_MAX_CONTACTS 20

struct PhoneContact {
    u8  name[PLAYER_NAME_LENGTH + 1]; // 8
    u32 trainerId;                    // 4
    u8  gender;                       // 1
    u8  flags;                        // 1  (reservado)
}; // 16 bytes × 20 = 320 bytes
```

Também no save ou em EWRAM + flag:

- `FLAG_SYS_CONNECTOR_ON` — Conector ligado (precisa de flag SYS livre).
- Lista **runtime** de peers online (não persistir; some ao desligar o Conector).

Compatibilidade: saves antigos têm o filler zerado → agenda vazia. OK.

---

## 6. Fases

### Fase 0 — Spec congelada (este doc)

**Entrega:** este arquivo + nomes dos itens.

Nomes provisórios (podem mudar):

- `ITEM_CONNECTOR` — “LINK CONNECTOR” / “CONECTOR”
- `ITEM_TRAINER_AGENDA` — “TRAINER AGENDA” / “AGENDA”

**Teste:** revisão humana. Sem build.

---

### Fase 1 — Itens + Agenda offline (UI)

**Objetivo:** abrir a Agenda no jogo e ver uma lista, **sem** link.

**Escopo:**

- IDs em `include/constants/items.h` (`ITEMS_COUNT` +2).
- Entradas em `src/data/items.json` (pocket Key Items, `fieldUseFunc`).
- Ícones: reusar Town Map / Vs. Seeker no começo (trocar arte depois).
- `FieldUseFunc_Connector` — mensagem “Conector ON/OFF” + flag (ainda **sem** `OpenLink`).
- `FieldUseFunc_Agenda` — fade + tela lista (modelo Town Map / list menu).
- Save: array de contatos; funções `Phone_AddContact` / `Phone_FindByTrainerId`.
- **Debug:** se a lista estiver vazia, inserir 2–3 contatos dummy (RED/BLUE offline) **só** em `#ifdef DEBUG` ou via special `DebugFillPhoneContacts`.
- Entregar os itens no inventário: Oak / Pallet / `giveitem` no NPC Arthorios **ou** special de debug.

**Arquivos prováveis:**

- `include/constants/items.h`, `src/data/items.json`, `src/data/item_icon_table.h`
- `include/item_use.h`, `src/item_use.c`
- `include/phone.h` (novo), `src/phone.c` (novo), `src/phone_agenda.c` (novo)
- `include/global.h` (struct no SaveBlock2)
- `include/constants/flags.h`
- `data/specials.inc` (debug give)

**Entrega visível:**

1. Key items na mochila.
2. Conector: mensagem ON, usar de novo: OFF.
3. Agenda: tela com dummy RED/BLUE, IDs, `[offline]`, B fecha.

**Não inclui:** cabo, RFU, batalha, troca.

---

### Fase 2 — Agenda útil sem segundo jogador

**Objetivo:** fluxo completo da UI com dados reais do **próprio** save.

**Escopo:**

- Header da Agenda mostra **seu** nome + ID de 5 dígitos (Trainer Card).
- Adicionar / remover contato dummy.
- Persistência: Save → reset emulador → contatos ainda lá.
- Conector registrado no Select (como bike).
- Ícone simples no overworld quando Conector ON (sprite ou `SE_PIN` + texto “LINK ON”).
- Bloquear Conector em Union Room / Safari / batalha (reusar `InUnionRoom`, `ArePlayerFieldControlsLocked`).

**Entrega visível:**

- Select liga/desliga Conector.
- Remover contato, salvar, reabrir ROM, contato sumiu.

---

### Fase 3 — Handshake cabo / netplay mGBA

**Objetivo:** dois emuladores com Conector ON trocam nome+ID e aparecem **online**.

**Escopo:**

- Com Conector ON: `OpenLinkTimed` + `InitLocalLinkPlayer` (fluxo próximo de `cable_club.c` `Task_LinkupStart`).
- Quando `GetLinkPlayerCount() >= 2`: copiar `gLinkPlayers[i]` para lista runtime de peers.
- Auto-salvar contato no **primeiro** handshake (nome + `trainerId` 32 bits).
- Agenda recarrega lista: contatos marcados online se `trainerId` bate.
- Se o outro desligar Conector / cair o link: voltam a offline.
- **Não** iniciar batalha ainda — só lista.

**Risco:** `OpenLink` no overworld compete com SIO. Manter Conector em **task** de prioridade baixa; cancelar em warp, menu, batalha selvagem.

**Entrega visível:**

- Duas instâncias mGBA linkadas.
- Ambos Conector ON → Agenda mostra o outro **● online**.
- Save → o contato fica na lista **○ offline** na próxima sessão sem link.

---

### Fase 4 — Solicitar batalha / troca (cabo)

**Objetivo:** da Agenda, mandar pedido; o outro aceita/recusa; entra Colosseum / Trade Center.

**Escopo:**

- Reusar `TryBattleLinkup` / `TryTradeLinkup` **depois** de fechar o handshake `LINKTYPE_PHONE` (não dá para manter os dois OpenLink).
- **MVP cabo:** os dois escolhem Batalhar ou Trocar na Agenda (contato **online**). Não há popup remoto ainda (serial do telefone não envia keys).
- Warp direto para Colosseum 2P / Trade Center, sem NPC do Center nem cap LV30 da Union Room.
- Recusar / B / falha de linkup → AbortLink vanilla e o Conector pode voltar a procurar.

**Entrega visível:**

- Batalha 1v1 a partir da Agenda.
- Troca a partir da Agenda.
- Após sucesso, contato já salvo (Fase 3).

---

### Fase 5 — Wireless / RetroArch gpSP `rfu` (opcional)

**Objetivo:** scan de “pessoas online” sem cabo (beacon RFU).

**Escopo:**

- `SetHostRfuGameData(ACTIVITY_*)` + `InitializeRfuLinkManager_EnterUnionRoom` **sem** warp.
- Filtrar activity própria (não misturar com Union Room vanilla se possível).
- Lista “PESSOAS ONLINE” = beacons incompatíveis com cabo.

**Teste principal:** RetroArch 1.17+ + **gpSP** + serial `rfu`. Hardware Wireless Adapter se disponível.

**Não bloquear** Fases 1–4 se RFU atrasar.

---

### Fase 6 — Polish

- Ícones originais dos itens.
- Textos PT/EN consistentes.
- Som ao detectar jogador.
- Follower: forçar hide com Conector ON.
- Remover dummies de debug.

---

## 7. Ordem de implementação (checklist de código)

Fase 1:

- [x] Constantes de item + `ITEMS_COUNT`
- [x] JSON + field funcs
- [x] Struct save + init no new game (`NewGameInitData` / clear save)
- [x] Tela Agenda (list menu)
- [x] Giveitem no Arthorios (Pallet) + special `GivePhoneKeyItems`
- [x] Build + teste 1 ROM

Fase 2:

- [x] Fundo da Agenda limpo (VRAM/paleta)
- [x] Header com nome + ID do jogador + ON/OFF
- [x] Flag Conector bloqueada em Union Room / Safari
- [x] A: remover contato (salva no `.sav`)
- [x] CONNECTOR registrável no Select

Fase 3:

- [x] Task link overworld (Conector ON)
- [x] Sync peers → Agenda ON/OFF
- [x] Auto-add contato no handshake

Fase 4:

- [x] Menu ação Batalhar/Trocar
- [ ] Yes/No remoto (ambos escolhem a mesma ação no cabo)
- [x] Warp Colosseum / Trade

Fase 5–6: conforme acima.

---

## 8. Guias de teste

### 8.1 Build

WSL, na raiz do repo:

```bash
make pokefirered.gba -j16
```

ROM: `pokefirered.gba`. Usar **duas cópias** da ROM e **dois saves** (Jogador A / Jogador B) — nomes e IDs diferentes.

---

### 8.2 Fase 1 — uma instância (mGBA)

1. Novo jogo **ou** save com special de give.
2. Mochila → Key Items: CONECTOR e AGENDA.
3. Usar CONECTOR → texto ON. Usar de novo → OFF.
4. Usar AGENDA → lista dummy, IDs, `[offline]`.
5. B fecha; overworld volta.
6. Abrir Agenda de novo: mesma lista (se dummy for save) **ou** dummy de novo (se só RAM — documentar no PR da fase).

**Falhas comuns:** crash ao abrir (window/tilemap), item sem `fieldUseFunc`, pocket errado.

---

### 8.3 Fase 2 — persistência

1. Remover um dummy / adicionar via debug.
2. **Save** no jogo.
3. Fechar mGBA, reabrir o **mesmo** `.sav`.
4. Agenda: lista igual.
5. Registrar CONECTOR no Select; no campo, Select liga/desliga.
6. Entrar em batalha selvagem com Conector ON: não deve corromper; ao voltar, estado ON ou OFF definido (documentar o comportamento escolhido).

---

### 8.4 Fase 3 — duas instâncias mGBA (cabo local)

No mGBA (nomes de menu variam por versão):

1. Abrir ROM A. **File → New multiplayer window** (ou Tools → Start multiplayer) → ROM B.
2. Confirmar que o indicador de link está ativo **antes** de entrar no jogo (ou conforme a versão: link na tela de título).
3. Dois saves diferentes, ambos no overworld (Pallet).
4. **A e B:** Conector ON.
5. Esperar 2–10 s (handshake).
6. **A:** abrir Agenda → B aparece **● online** (nome + ID).
7. **B:** o mesmo para A.
8. **A:** Conector OFF → na Agenda de B, A vira **○ offline** (ou some de “pessoas online”).
9. Completar um handshake, Save em A, reset A **sem** link → contato de B permanece offline.

**Se não conectar:**

- Link do emulador não estava ativo (é o caso mais comum).
- Só um dos dois ligou o Conector.
- `OpenLink` cancelado por outro sistema (Quest Log, wireless check).

**Netplay mGBA (duas PCs):** mesmo roteiro depois do TCP Host/Join do mGBA. Latência alta pode falhar o handshake — testar LAN primeiro.

**Glitches no mapa quando o 2.º jogador liga (casas pretas, cerca partida, clones do RED):**

São **duas** camadas:

1. **Continue / follower** — NPCs e follower já saem com tiles do jogador (preto ou RED) **antes** do cabo. Documentado em `arthorios/docs/follower-ow-sprites.md`. Warp indoor corrige. Não é o handshake a “clonar” avatares de Union Room (`gFieldLinkPlayerCount` só no cable club).
2. **Janela no overworld + cabo** — um popup em BG0 (`baseBlock 0x380`) + `CopyWindowToVram` no mesmo frame que `OpenLink` / `LinkVSync` **pica tiles do mapa** (retângulos pretos na casa e na cerca). Esse popup foi **removido**. Presença = Agenda `ON`. Não voltar a desenhar janela de campo em BG0 enquanto o serial está aberto.

O Conector **não** deve criar `SpawnLinkPlayers` / sprites de parceiro no mapa.

---


### 8.5 Fase 4 — batalha e troca (mGBA cabo)

Pré-requisito: Fase 3 OK (online na Agenda).

**Batalha:**

1. Party com pelo menos 1 Pokémon válido em cada lado.
2. Agenda → contato **ON** → A → BATTLE (os **dois** ao mesmo tempo).
3. “Please wait” no campo; sucesso → Colosseum (sem NPC).
4. Terminar batalha → volta ao mapa; Conector ainda ON e a descoberta pode retomar.

**Troca:**

1. Mesmo, opção Trocar.
2. Trade Center → troca → confirma.
3. Party atualizada nos dois saves.

**Recusar:** pedido some; ninguém trava em “Please wait”.

---

### 8.6 RetroArch + gpSP (cabo netpacket)

Requisitos: RetroArch **1.17+**, core **gpSP** (não mGBA).

1. Core Options → **Link Cable Connectivity** → `mul_poke` (cabo Pokémon).
2. Mesma (ou compatível) ROM do hack nas duas instâncias.
3. Netplay → Host (A) / Join (B). Lobby ou LAN.
4. Seguir testes 8.4–8.5 (Conector + Agenda).

Isso **não** testa Union Room. É o equivalente ao cabo.

---

### 8.7 RetroArch + gpSP (wireless) — Fase 5

1. Core Options → `rfu` / GBA Wireless Adapter.
2. Host/Join netplay.
3. Conector ON nos dois.
4. Agenda: o outro em **PESSOAS ONLINE** **sem** passar pelo Direct Corner.
5. Comparar com Union Room vanilla (Center 2F) no mesmo core: se vanilla falhar, o problema é o emulador, não o hack.

---

### 8.8 Hardware (opcional)

| Hardware | Teste |
|----------|--------|
| 2 GBA + cabo | Fases 3–4 (Direct Corner como controle; depois Conector) |
| Wireless Adapter | Fase 5 |

---

## 9. Critérios de “fase pronta”

| Fase | Pronto quando |
|------|----------------|
| 1 | Agenda abre, itens existem, 1 tester sozinho |
| 2 | Save persiste; Select funciona |
| 3 | 2 mGBA: nomes corretos e estado online/offline |
| 4 | 1 batalha + 1 troca completas pela Agenda |
| 5 | Scan RFU em gpSP `rfu` **ou** hardware |
| 6 | Sem dummies; ícones ok |

---

## 10. Fora de escopo (por enquanto)

- Andar juntos no mesmo mapa (hub tipo Colosseum).
- Servidor / matchmaking na internet além do netplay do emulador.
- Chat completo da Union Room.
- Mais de 2 jogadores no Conector cabo (vanilla cabo vai até 4; deixar 2P primeiro).

---

## 11. Próximo passo concreto

Começar **Fase 1**: itens + struct save + tela da Agenda com contatos dummy + giveitem.

Teste de aceite da Fase 1: uma ROM, sem multiplayer, screenshot/gravação da lista dummy.
