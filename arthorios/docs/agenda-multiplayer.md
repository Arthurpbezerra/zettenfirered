# Agenda + Conector — contexto, arquitetura e plano de entregas

Documento vivo do multiplayer via dois key items (**Conector** + **Agenda**).
Serve como referência de **ideia**, **implementação atual**, **testes**, **bugs/glitches** e **backlog de melhorias**.

Caminho alvo de teste remoto: **RetroArch 1.17+**, core **gpSP**, `Link Cable Connectivity = mul_poke`, netplay host/join.
mGBA (duas janelas) serve para desenvolvimento local; RFU fica para a Fase G.

Última atualização: **2026-08-29**.

---

## Estado atual (resumo)

| Área | Status | Notas |
|------|--------|-------|
| Fases 1–2 (itens, Agenda offline, save, Select) | ✅ Feito | Contatos dummy ainda presentes |
| Fase 3 (handshake cabo, peers online) | ✅ Feito | `LinkSession` + `Phone_TryResumeLink` |
| Fase 4 (batalha/troca pela Agenda) | ✅ Quase | Yes/No remoto via canal `app`; falta polish e testes netplay |
| Co-op v1 (Pallet + Route 1) | 🟡 Parcial | Avatar remoto + supressão de eventos; movimento bruto |
| Fase 5 (RFU / scan wireless) | ⬜ Não iniciado | Fora do caminho crítico |
| Fase 6 (polish) | ⬜ Pendente | Ícones, PT, som, follower hide |
| Fase F (warp/encontros com host) | ⬜ Pendente | Escopo reduzido recomendado |
| Fase G (presente, chat, RFU Agenda) | ⬜ Pendente | Opcional |

**Trabalho recente (branch local, ainda não commitado em 2026-08-29):** módulos `link_session`, `link_proto`, `link_coop`, `link_diag`; refatoração grande de `phone.c`; integração em `overworld.c`, `field_control_avatar.c`, `wild_encounter.c`; entradas em `ld_script.ld` / `ld_script_rev10.ld`.

---

## 1. Objetivo

Dois key items:

| Item | Papel | Uso |
|------|--------|-----|
| **Conector** | Liga/desliga “estou online” | Mochila ou **Select** (registrado, como bicicleta) |
| **Agenda** | Lista contatos + pessoas online; batalha / troca / remover | Mochila (menu completo) |

Comportamento alvo:

- Após o primeiro handshake bem-sucedido, o outro treinador é salvo automaticamente no save.
- Com o Conector ON, a sessão persiste no overworld; batalha/troca **não** derrubam o cabo (handoff).
- Em **Pallet Town** e **Route 1**, se os dois estiverem no mesmo mapa, o avatar remoto aparece (co-op v1).
- SELECT na Agenda abre **diagnóstico de link** (`LinkDiag`).

---

## 2. Arquitetura geral

O stack customizado foi dividido em camadas com responsabilidades fixas. A regra de ouro: **só `LinkSession` mexe no hardware serial** (`OpenLink` / `CloseLink` / `ResetSerial`).

```
┌─────────────────────────────────────────────────────────────┐
│  UI / jogo                                                  │
│  phone.c (Agenda, pedidos batalha/troca, diagnóstico)       │
│  overworld.c (avatar co-op, retorno pós-clube)              │
│  field_control_avatar.c / wild_encounter.c (supressão co-op)│
└───────────────────────────┬─────────────────────────────────┘
                            │ LinkSession_* / LinkCoop_* / LinkProto_*
┌───────────────────────────▼─────────────────────────────────┐
│  LinkCoop (co-op v1)     — canal PRESENCE, pose remota      │
│  LinkProto (datagramas)  — canais control/presence/coop/app │
│  LinkDiag (contadores)   — métricas quando UI vanilla muda  │
└───────────────────────────┬─────────────────────────────────┘
                            │ SendBlock / gBlockRecvBuffer
┌───────────────────────────▼─────────────────────────────────┐
│  LinkSession (máquina de estados, dono do SIO)              │
│  gLinkType = LINKTYPE_PHONE (0x7701) enquanto idle online   │
└───────────────────────────┬─────────────────────────────────┘
                            │ OpenLinkTimed, CloseLink, …
┌───────────────────────────▼─────────────────────────────────┐
│  Motor vanilla (link.c, cable_club, batalha, troca)         │
└─────────────────────────────────────────────────────────────┘
```

### 2.1 LinkSession — dono do cabo

**Arquivos:** `src/link_session.c`, `include/link_session.h`

Estado em **estático de módulo** (não em `gTasks`), para sobreviver a `ResetTasks()` quando a Agenda abre/fecha.

**Estados:**

| Estado | Significado |
|--------|-------------|
| `IDLE` | Serial fechado; aguarda Conector ON + condições de campo |
| `DISCOVERING` | `OpenLinkTimed`; sub-fases boot → wait partner → wait exchange |
| `ESTABLISHED` | Peer presente; tráfego de aplicativo permitido |
| `BARRIER` | Os dois lados concordam em trocar atividade (batalha/troca) |
| `HANDOFF` | Motor vanilla do Cable Club possui o link |
| `DRAINING` | Desligamento limpo antes de voltar a IDLE |

**Perfis de timeout** (`LINK_PROFILE_LOCAL` vs `LINK_PROFILE_REMOTE`):

- Local (duas janelas mGBA): budgets curtos (ex.: partner 360 frames).
- Remoto (netplay): budgets ~3× maiores — netplay adiciona delay fixo de input; timeouts curtos geram falsos “link caiu”.

**Contratos importantes:**

- `LinkSession_Update()` — **uma vez por frame** em `Phone_TryResumeLink()` (overworld) e no loop da Agenda (`CB2_PhoneAgenda`).
- `CanOpenLink()` — mais restrito que `ShouldKeepRunning()`: não abre serial com fade ativo, controles bloqueados ou CB2 fora do campo.
- Dropout breve: grace period antes de `RestartDiscovery()` (evita reconectar a cada hiccup de netplay).
- Handoff: serial **permanece aberto**; `gLinkType` muda para `LINKTYPE_SINGLE_BATTLE` ou `LINKTYPE_TRADE_SETUP`; após atividade, `LinkSession_EndHandoff()` tenta retomar `LINKTYPE_PHONE`.

**Comandos no canal `control`:**

| Cmd | Uso |
|-----|-----|
| `CTRL_HANDOFF_REQ` | Pedido de handoff + `linkType` |
| `CTRL_HANDOFF_CANCEL` | Cancela barreira |
| `CTRL_ARRIVED` | Rendezvous no mapa do clube (ambos carregaram a sala) |

**Nota:** `LinkSession_Init()` existe mas **não é chamado** em nenhum ponto do boot atual — o estado zero-inicial funciona por acidente. Considerar chamar em `Phone_EnsureReady()` ou no new game.

### 2.2 LinkProto — datagramas sobre SendBlock

**Arquivos:** `src/link_proto.c`, `include/link_proto.h`

Cabeçalho: `{ magic=0xA7E2, protoVersion=1, channel, len, seq }`, payload até **24 bytes**.

**Canais:**

| Canal | Dono | Conteúdo |
|-------|------|----------|
| `control` | LinkSession | Handoff, arrived, cancel |
| `presence` | LinkCoop | Posição, mapa, direção, gender, RNG |
| `coop` | (reservado) | Futuro: warp, encontros host |
| `app` | Phone | Pedidos batalha/troca/accept/decline |

**Política de envio:**

- `presence` e `coop` **cedem** se houver pacote `control`/`app` pendente ou se `SendBlock` estiver ocupado — evita que posição “atropele” accept/handoff.
- Pacotes inválidos são descartados e contados em `LinkDiag`.
- ROM com `protoVersion` diferente → flag `LinkProto_HasVersionMismatch()` → texto *"Partner ROM is a different version."* na Agenda.

### 2.3 LinkCoop — arquitetura do co-op v1

**Arquivos:** `src/link_coop.c`, `include/link_coop.h`, `src/overworld.c` (`Overworld_UpdateCoopPartner`)

O co-op v1 **não** sincroniza mundo compartilhado. É um modelo **“presença espelhada”**:

1. Cada frame (quando estabelecido), `LinkCoop_Update()` registra handler do canal `presence`.
2. A cada **24 frames** (~0,4 s), se o serial estiver livre, envia `struct LinkPresence`:
   - `mapGroup`, `mapNum`, `direction`, `gender`, `x`, `y` (destino do passo), `rng`.
3. O receptor valida com `PresenceLooksSane()` (direção, gender, coords dentro do layout do mapa local).
4. `LinkCoop_IsActive()` exige:
   - sessão estabelecida + peer conhecido;
   - **mapa local** na whitelist (`MAP_PALLET_TOWN`, `MAP_ROUTE1`);
   - **mapa do peer** igual ao local.
5. `Overworld_UpdateCoopPartner()` (chamado de `DoCB1_Overworld`):
   - lê pose via `LinkCoop_GetPeerPose()`;
   - spawna/atualiza **um** `LinkPlayerObjectEvent` + sprite `SpriteCB_LinkPlayer`;
   - valida coords com `CoopCoordsInMap()`;
   - descarta avatar stale com `CoopDiscardStaleAvatar()` quando co-op desativa.

**Supressão de eventos de campo** (`LinkCoop_ShouldSuppressFieldEvents()` → `LinkCoop_IsActive()`):

| Sistema | Arquivo | Comportamento |
|---------|---------|---------------|
| Trainers overworld | `field_control_avatar.c` | `CheckForTrainersWantingBattle` desligado |
| Scripts on-frame | `field_control_avatar.c` | `TryRunOnFrameMapScript` desligado |
| Scripts de passo | `field_control_avatar.c` | `TryStartStepBasedScript` desligado |
| Encontros selvagens | `wild_encounter.c` | `TryStandardWildEncounter` retorna FALSE |

**RNG:** no primeiro pacote de presença, o **slave** copia `gRngValue` do master (`rngSynced`). Não há re-sync contínuo — se os lados consumirem RNG em ritmos diferentes, divergem.

**O que o co-op v1 NÃO faz (de propósito):**

- Não sincroniza warps (cada jogador fica no seu save/mapa).
- Não interpola movimento — posição absoluta é aplicada a cada pacote (avatar “teleporta” em saltos).
- Não tem timeout de presença — se pacotes param, avatar congela no último ponto.
- Não usa `SpawnLinkPlayers()` vanilla (evita clones RED do Cable Club).

**Whitelist hardcoded** em `MapIsWhitelisted()` — mover para dados (map header flag ou tabela) quando expandir mapas.

### 2.4 LinkDiag — observabilidade

**Arquivos:** `src/link_diag.c`, `include/link_diag.h`

Contadores saturantes (u16) para sessões, pacotes, rejeições, timeouts, erros de hardware.
UI vanilla de erro de link é suprimida durante sessão (`SetSuppressLinkErrorMessage`).

**Acesso:** SELECT na Agenda → tela `AGENDA_STATE_DIAG` (`Phone_ShowDiagnostics`).

### 2.5 Phone / Agenda — camada de aplicativo

**Arquivo principal:** `src/phone.c` (~1600 linhas)

**Mensagens no canal `app` (`struct PhoneAppMsg`):**

| kind | Direção | Efeito |
|------|---------|--------|
| `PHONE_MSG_BATTLE` | A → B | Pedido de batalha |
| `PHONE_MSG_TRADE` | A → B | Pedido de troca |
| `PHONE_MSG_ACCEPT` | B → A | Aceite |
| `PHONE_MSG_DECLINE` | B → A ou cancelamento | Recusa |

**Fluxo batalha/troca:**

1. Solicitante escolhe ação → `Phone_TrySendMsg` → estado `WAIT_REPLY`.
2. Receptor: se Agenda fechada, `Phone_TryOpenAgendaForIncoming()` abre Agenda com fade; se aberta, `Phone_BeginIncomingPrompt()` (Yes/No **dentro da Agenda**, nunca popup de campo).
3. Aceite → `PHONE_MSG_ACCEPT` → barreira/handoff → warp Colosseum ou Trade Center.
4. `Task_PhoneClubLinkup`: rendezvous (`CTRL_ARRIVED`) → troca de trainer cards → sucesso.
5. Retorno: `CB2_ReturnToFieldFromMultiplayer` + `Phone_OnClubLinkupEnd` → `LinkSession_EndHandoff()` retoma descoberta.

**Ponto crítico documentado no código:** ao fechar Agenda, **não** chamar `CloseLink` — deixa `CB1_Overworld` ativo ou o handshake do clube nunca dispara (`Task_ClosePhoneAgenda`).

---

## 3. Mapa de arquivos

| Arquivo | Papel |
|---------|-------|
| `include/link_session.h`, `src/link_session.c` | Máquina de estados, handoff, perfis timeout |
| `include/link_proto.h`, `src/link_proto.c` | Datagramas, validação, prioridade de envio |
| `include/link_coop.h`, `src/link_coop.c` | Presença co-op, whitelist, supressão |
| `include/link_diag.h`, `src/link_diag.c` | Contadores de diagnóstico |
| `include/phone.h`, `src/phone.c` | Agenda, Conector, app channel, clube |
| `include/global.h` | `PhoneSaveData`, `PhoneContact` em `SaveBlock2` |
| `include/link.h` | `LINKTYPE_PHONE` (0x7701) |
| `src/overworld.c` | `Overworld_UpdateCoopPartner`, retorno pós-multiplayer |
| `src/field_control_avatar.c` | Supressão trainers/scripts em co-op |
| `src/wild_encounter.c` | Supressão encontros em co-op |
| `src/item_use.c` | `FieldUseFunc_Connector`, `FieldUseFunc_Agenda` |
| `ld_script.ld`, `ld_script_rev10.ld` | Link order dos novos `.o` |
| `arthorios/docs/follower-ow-sprites.md` | Bug de sprites no Continue (feature follower, **não** é bug do cabo) |

---

## 4. Princípio das entregas

Cada fase:

1. Compila (`make pokefirered.gba`).
2. Tem **comportamento visível** no jogo.
3. Tem **checklist de teste** neste arquivo.
4. Pode ser jogada **sem** a fase seguinte.

Não misturar UI, save, RFU e cabo na mesma entrega.

---

## 5. UX alvo (Agenda)

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
│ A: ação   B: sair   SELECT: diag     │
└──────────────────────────────────────┘

Contato online  → Batalhar / Trocar / Remover
Desconhecido    → Adicionar / Batalhar / Trocar
Contato offline → Remover (sem ligar)
```

Estados:

- **● Online** — Conector dos dois ON **e** sessão de link detectada (cabo/netplay) **ou** beacon RFU (wireless, Fase G).
- **○ Offline** — salvo, não detectado agora.
- **Novo** — online, ainda não é contato.

---

## 6. Limites do emulador (importante para testes)

| Setup | O que simula | Agenda “online” |
|-------|----------------|-----------------|
| **mGBA** (2 janelas / netplay) | **Cabo link** | Só depois de handshake; sem scan passivo |
| **RetroArch + gpSP** `mul_poke` | **Cabo** via netpacket | Igual cabo |
| **RetroArch + gpSP** `rfu` | **Wireless Adapter** | Scan / Union Room possível |
| **mGBA core no RetroArch** | Sem RFU Pokémon | Não usar para Union Room |

Netplay RetroArch **não** é um único protocolo: no gpSP você escolhe cabo (`mul_poke`) ou wireless (`rfu`).

Fases 1–2 testam **1 ROM**. Fases 3+ precisam **2 instâncias**.

---

## 7. Dados no save

`SaveBlock2.phone` ocupa `0x400` bytes (substitui `filler_B20`). Tamanho total do save inalterado (`0xF24`).

```c
#define PHONE_SAVE_MAGIC   0x50484E31 // "PHN1"
#define PHONE_MAX_CONTACTS 20

struct PhoneContact {
    u8  name[PLAYER_NAME_LENGTH + 1]; // 8 + EOS
    u32 trainerId;
    u8  gender;
    u8  flags;        // PHONE_CONTACT_DUMMY para dummies
    u8  padding[2];
}; // 16 bytes × 20 = 320 bytes

struct PhoneSaveData {
    u32 magic;
    struct PhoneContact contacts[PHONE_MAX_CONTACTS];
    u8 dummySeeded;
    u8 unused[0x2BB];
}; // 0x400
```

Também:

- `FLAG_SYS_CONNECTOR_ON` — Conector ligado.
- Lista **runtime** de peers online (`sPhoneOnlineIds` em EWRAM) — não persiste.

Compatibilidade: saves antigos sem magic → `Phone_InitSave()` na primeira abertura da Agenda.

**Melhoria pendente:** byte de versão do bloco phone + checksum para migrações futuras.

---

## 8. Fases

### Fase 0 — Spec congelada ✅

Nomes atuais:

- `ITEM_CONNECTOR` — “LINK CONNECTOR” / “CONECTOR”
- `ITEM_TRAINER_AGENDA` — “TRAINER AGENDA” / “AGENDA”

### Fase 1 — Itens + Agenda offline ✅

Key items, struct save, tela lista, giveitem no NPC Arthorios (Pallet).

### Fase 2 — Agenda útil sem segundo jogador ✅

Header nome+ID, persistência, Conector no Select, bloqueio Union Room/Safari.

### Fase 3 — Handshake cabo / netplay ✅

`LinkSession` no overworld, sync peers, auto-add contato, online/offline na lista.

### Fase 4 — Batalha / troca 🟡

- Menu ação Batalhar/Trocar ✅
- Yes/No remoto via canal `app` ✅ (implementado; validar em 2 instâncias)
- Warp Colosseum / Trade ✅
- Handoff sem fechar cabo ✅
- Rendezvous pós-warp ✅
- Abrir Agenda automaticamente para pedido incoming ✅

### Fase E — Co-op v1 (presença) 🟡

- Canal `presence` + avatar remoto em Pallet/Route 1 ✅
- Supressão encontros/scripts/trainers ✅
- Interpolação de movimento ⬜
- Timeout de presença ⬜
- Whitelist em dados ⬜

### Fase F — Co-op avançado ⬜

Escopo **recomendado reduzido** (não perseguir mundo compartilhado completo):

- Encontros selvagens com autoridade do host (host sorteia, guest recebe).
- Mensagem explícita ao sair do mapa whitelist.
- Reabrir interação segura com NPCs (itens de script) — cutscenes permanecem off.

Fora do escopo realista: warps sincronizados, quests compartilhadas, 4P co-op.

### Fase G — RFU / wireless ⬜

Scan passivo, “PESSOAS ONLINE” sem cabo. Depende gpSP `rfu` ou hardware.

### Fase 6 — Polish ⬜

Ícones originais, textos PT, som ao detectar jogador, esconder follower com Conector ON, remover dummies.

---

## 9. Checklist de código

Fase 1:

- [x] Constantes de item + `ITEMS_COUNT`
- [x] JSON + field funcs
- [x] Struct save + init (`Phone_InitSave`)
- [x] Tela Agenda (list menu)
- [x] Giveitem no Arthorios + special `GivePhoneKeyItems`

Fase 2:

- [x] Fundo da Agenda limpo (VRAM/paleta)
- [x] Header com nome + ID + ON/OFF
- [x] Flag Conector bloqueada em Union Room / Safari
- [x] A: remover contato (persiste no `.sav`)
- [x] CONNECTOR registrável no Select

Fase 3:

- [x] LinkSession no overworld (Conector ON)
- [x] Sync peers → Agenda ON/OFF
- [x] Auto-add contato no handshake
- [x] Perfis LOCAL/REMOTE de timeout
- [ ] Chamar `LinkSession_Init()` no boot

Fase 4:

- [x] Menu ação Batalhar/Trocar
- [x] Yes/No remoto (canal `app`, prompt **dentro** da Agenda)
- [x] Warp Colosseum / Trade
- [x] Handoff + rendezvous + retorno ao campo
- [ ] Teste completo netplay RetroArch gpSP

Fase E (co-op):

- [x] `LinkCoop` + canal `presence`
- [x] `Overworld_UpdateCoopPartner`
- [x] Supressão wild/trainers/scripts
- [x] Validação `PresenceLooksSane` + `CoopCoordsInMap`
- [ ] Interpolação / fila de movimento link player
- [ ] Timeout presença (despawn após N frames sem pacote)
- [ ] Whitelist configurável por mapa

Fase 5–6, F, G: conforme seções acima.

---

## 10. Bugs, glitches e limitações conhecidas

Use esta seção para triagem rápida. Cada item indica **sintoma → causa provável → onde olhar**.

### 10.1 Glitches visuais no mapa (dois jogadores)

| Sintoma | Camada | Causa | Onde |
|---------|--------|-------|------|
| NPCs/follower pretos ou “tudo RED” no **Continue** | Follower (pré-cabo) | Spawn no Continue usa caminho diferente do warp; bug de sheets/paleta | `follower-ow-sprites.md`, `SpawnObjectEventOnReturnToField` |
| Retângulos pretos na casa/cerca ao ligar cabo | UI + SIO | Popup/janela em BG0 + `CopyWindowToVram` no mesmo frame que `OpenLink` | **Removido** — não reintroduzir popup de campo |
| Clones RED extras | Cable Club vanilla | `SpawnLinkPlayers` / `gFieldLinkPlayerCount` | Conector **não** deve chamar isso no overworld |
| Avatar remoto “pula” / borracha | Co-op v1 | Posição absoluta a cada 24 frames, sem interpolação | `link_coop.c`, `Overworld_UpdateCoopPartner` |
| Avatar remoto congelado | Co-op v1 | Sem timeout de presença | `link_coop.c` |
| Buraco preto na Agenda ao remover contato | UI Agenda | `DestroyYesNoMenu` + tilemap | `Phone_HandleRemoveConfirm` — Yes/No só dentro da Agenda |

### 10.2 Link / handshake

| Sintoma | Causa provável |
|---------|----------------|
| Nunca conecta | Link do emulador inativo; só um Conector ON; `CanOpenLink()` false (menu/fade) |
| Conecta e cai rápido em netplay | Perfil LOCAL em vez de REMOTE; aumentar budgets |
| *"Partner ROM is a different version."* | `LINK_PROTO_VERSION` diferente entre ROMs |
| Handshake trava em “Please wait” | Barreira/handoff não completou; ver diag SELECT |
| Pós-batalha não retoma online | `LinkSession_EndHandoff()` não encontrou peers; cai em rediscovery |
| `LinkSession_Init` nunca chamado | Estado zero funciona por acidente; handlers/diag podem não resetar entre sessões longas |

### 10.3 Co-op / gameplay

| Limitação | Impacto |
|-----------|---------|
| Só Pallet + Route 1 | Outros mapas: sem avatar, eventos normais |
| Mapas diferentes | Avatar some; cada save independente |
| Scripts/NPCs desligados em co-op ativo | Mapa “morto” para story — intencional v1 |
| RNG sync único | Divergência se consumo de RNG diferir |
| Follower + Conector ON | Follower ainda visível (polish Fase 6: esconder) |

### 10.4 UX / produto

| Item | Nota |
|------|------|
| Conector vs bicicleta no Select | Compartilham o mesmo slot registrável |
| Contatos dummy (RED/BLUE/GREEN) | Ainda seedados em new game; remover na Fase 6 |
| Código não commitado | Risco de perder refatoração link_* + phone + overworld |

---

## 11. Melhorias sugeridas (backlog)

Prioridade = retorno ÷ esforço para **este** projeto.

### Multiplayer (curto prazo — fechar escopo)

| # | Melhoria | Esforço | Notas |
|---|----------|---------|-------|
| M1 | Commitar stack link + co-op + phone refatorado | XS | Proteger trabalho |
| M2 | Chamar `LinkSession_Init()` / `LinkDiag_Reset()` no boot | XS | Higiene |
| M3 | Testar Fase 4 completa em RetroArch gpSP netplay | S | Validar perfil REMOTE |
| M4 | Timeout presença + despawn avatar | S | `lastSeen` em `LinkCoopCtx` |
| M5 | Interpolação movimento link player | M | Fila vanilla em vez de teleport |
| M6 | Whitelist de mapas em dados | S | Tabela ou flag no map header |
| M7 | Mensagem “parceiro saiu do mapa” | S | Canal `coop` ou bit em presence |
| M8 | `LinkSession_SetProfile(REMOTE)` automático | S | Detectar netplay se possível |

### Multiplayer (médio prazo — só se co-op importar)

| # | Melhoria | Esforço | Notas |
|---|----------|---------|-------|
| M9 | Encontros selvagens host-authoritative | L | Host sorteia, guest recebe batalha |
| M10 | Reabrir NPCs/itens de script em co-op | M | Manter cutscenes off |
| M11 | RFU scan (Fase G) | L | gpSP-only, não bloqueante |

### QOL geral da ROM (fora do cabo, alto retorno)

| # | Melhoria | Esforço |
|---|----------|---------|
| Q1 | Corrigir sprites Continue (follower) | M |
| Q2 | Texto rápido / B acelera diálogo | XS |
| Q3 | Running Shoes indoor | XS |
| Q4 | TMs reutilizáveis | S |
| Q5 | Nature/IV/EV no Summary | S |
| Q6 | Segundo slot registrável ou Conector em L/R | S |
| Q7 | Repel reutilizar ao acabar | XS |
| Q8 | Versão do bloco phone no save | S |

### Declarar fora de escopo (evitar scope creep)

- Servidor / matchmaking próprio além do netplay do emulador.
- Chat completo estilo Union Room.
- Hub compartilhado tipo Colosseum free-roam.
- 4 jogadores no Conector antes de 2P estável.
- Ciclo dia/noite, DexNav, split físico/especial — features grandes, não QOL.

---

## 12. Guias de teste

### 12.1 Build

WSL, na raiz do repo:

```bash
make pokefirered.gba -j16
```

ROM: `pokefirered.gba`. Usar **duas cópias** da ROM e **dois saves** (Jogador A / Jogador B) — nomes e IDs diferentes.

---

### 12.2 Fase 1 — uma instância (mGBA)

1. Novo jogo **ou** save com special de give.
2. Mochila → Key Items: CONECTOR e AGENDA.
3. Usar CONECTOR → texto ON. Usar de novo → OFF.
4. Usar AGENDA → lista (dummies RED/BLUE/GREEN), IDs, `[offline]`.
5. B fecha; overworld volta.

**Falhas comuns:** crash ao abrir (window/tilemap), item sem `fieldUseFunc`, pocket errado.

---

### 12.3 Fase 2 — persistência

1. Remover um contato dummy.
2. **Save** no jogo.
3. Fechar mGBA, reabrir o **mesmo** `.sav`.
4. Agenda: lista igual.
5. Registrar CONECTOR no Select; no campo, Select liga/desliga.

---

### 12.4 Fase 3 — duas instâncias mGBA (cabo local)

1. Abrir ROM A. **File → New multiplayer window** → ROM B.
2. Confirmar indicador de link ativo.
3. Dois saves diferentes, overworld (Pallet).
4. **A e B:** Conector ON.
5. Esperar 2–10 s (handshake).
6. Agenda → outro **● online**.
7. Conector OFF de um lado → outro **○ offline**.

**Se não conectar:** link emulador inativo; um só Conector ON; `OpenLink` cancelado (Quest Log, fade, menu).

**Glitches visuais:** ver §10.1. Continue/follower ≠ handshake. Popup de campo + cabo = **nunca mais**.

---

### 12.5 Fase 4 — batalha e troca

Pré-requisito: Fase 3 OK.

**Com Yes/No remoto (fluxo atual):**

1. **A:** Agenda → contato ON → BATTLE.
2. **B:** recebe prompt (Agenda abre sozinha ou já aberta) → Accept.
3. Handoff → Colosseum → batalha → retorno ao mapa.
4. Conector ainda ON; sessão retoma.

**Troca:** idem com TRADE; party atualizada nos dois saves.

**Recusar:** DECLINE → *"The other TRAINER declined."*; sem travar em WAIT.

**Diag:** SELECT na Agenda → contadores SESSION/PACKET/ERROR.

---

### 12.6 Co-op v1 — Pallet Town / Route 1

Pré-requisito: Fase 3 OK; **mesmo mapa** nos dois lados.

1. A e B com Conector ON, ambos em Pallet Town.
2. Ver avatar remoto andando (com saltos — esperado v1).
3. Tentar encontro selvagem → **não deve ocorrer**.
4. Tentar falar com NPC / trainer → **não deve disparar** (supressão).
5. **A** warp para Route 1, **B** fica em Pallet → avatar de A some em B.
6. Ambos Route 1 → avatar volta.

---

### 12.7 RetroArch + gpSP (cabo netpacket)

1. Core Options → **Link Cable Connectivity** → `mul_poke`.
2. Mesma ROM nas duas instâncias.
3. Netplay Host/Join.
4. Repetir 12.4–12.6.
5. Se timeouts frequentes: considerar `LinkSession_SetProfile(LINK_PROFILE_REMOTE)`.

---

### 12.8 RetroArch + gpSP (wireless) — Fase G

1. Core Options → `rfu`.
2. Host/Join netplay.
3. Conector ON nos dois.
4. Comparar com Union Room vanilla no mesmo core.

---

### 12.9 Hardware (opcional)

| Hardware | Teste |
|----------|--------|
| 2 GBA + cabo | Fases 3–4 |
| Wireless Adapter | Fase G |

---

## 13. Critérios de “fase pronta”

| Fase | Pronto quando |
|------|----------------|
| 1 | Agenda abre, itens existem, 1 tester sozinho |
| 2 | Save persiste; Select funciona |
| 3 | 2 mGBA: nomes corretos e online/offline |
| 4 | 1 batalha + 1 troca com Yes/No remoto; retorno ao campo com Conector ON |
| E | Avatar co-op visível Pallet/Route1; wild/scripts off no mesmo mapa |
| 5/G | Scan RFU em gpSP `rfu` **ou** hardware |
| 6 | Sem dummies; ícones ok; follower hide |

---

## 14. Fora de escopo (por enquanto)

- Mundo compartilhado completo (warps sync, quests, cutscenes co-op).
- Servidor / matchmaking além do netplay do emulador.
- Chat completo da Union Room.
- Mais de 2 jogadores no Conector cabo.
- RFU na Agenda antes de cabo/netplay estável.

**Dentro de escopo reduzido:** presença visual em whitelist, batalha/troca pela Agenda, diagnóstico de link.

---

## 15. Próximos passos concretos

Ordem recomendada para **concluir** o multiplayer no escopo 2P cabo/netplay:

1. **Commitar** stack `link_*` + integrações + refatoração `phone.c`.
2. **Validar Fase 4** com Yes/No remoto em 2× mGBA e 1× RetroArch netplay.
3. **Chamar `LinkSession_Init()`** no boot (`Phone_EnsureReady` ou equivalente).
4. **Co-op polish mínimo:** timeout presença (M4) — baixo esforço, melhora muito a percepção.
5. **Declarar Fase E “done enough”** ou investir em interpolação (M5) se co-op for prioridade.
6. **Fase 6:** remover dummies, ícones, esconder follower com Conector ON.
7. **Fase G (RFU)** só se houver demanda e ambiente gpSP disponível.

Teste de aceite mínimo do multiplayer “concluído”:

- 2 instâncias, Conector ON, handshake estável 5+ minutos.
- 1 batalha + 1 troca com accept/decline remotos.
- Retorno ao mapa com sessão retomada.
- Co-op: avatares visíveis em Pallet com supressão de wild (mesmo que movimento seja bruto).
- Diagnóstico SELECT mostra sessões estabelecidas sem avalanche de TIMEOUT em LAN.
