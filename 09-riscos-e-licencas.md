# 09 — Riscos, licenças e questões legais

## 1. Registo de riscos

| # | Risco | Prob. | Impacto | Evidência | Mitigação |
|---|---|---|---|---|---|
| R1 | **MCD não é gerado → nada funciona** | Alta se ignorado | 🔴 Crítico | [04](04-clock-aica.md) | Gerar no primeiro passo, verificar com scope |
| R2 | **PIO vs DMA por resolver** | Alta | 🟡 Médio | Dev.Box §2.6.3 diz MWDMA-2 ([06b](06b-scope-pio-vs-dma.md)) | Sniffer **antes** de implementar |
| R3 | **Level shifting VA0/VA1** | Alta | 🔴 Crítico | GDEMU/MODE têm level shifters | Desenhar desde o início; jumper configurável |
| R4 | **Conector 52602-0579 indisponível** | Alta | 🔴 Crítico | Autor do Dreamdrive com dificuldade | Fontes alternativas; comprar em stock; adaptador |
| R5 | **Soldar QFN-80 (0,4 mm)** | Alta | 🟡 Médio | 48 GPIO em 10×10 mm | Assembly; ou stamp-format board |
| R6 | **REQ_MODE / REQ_STAT / GET_TOC errados** | Média | 🔴 Crítico | Lidos por quase todos os jogos | Testes golden contra Flycast ([11 §2](11-estrategia-de-validacao.md)) |
| R7 | **Latência do SD estrangula o IORDY** | Média | 🟡 Médio | `tB` = 1250 ns max ([03 §6.1](03-timing-ide.md)) | SDIO + ring pré-carregado; medir |
| R8 | **Comandos 0x70/0x71** | Média | 🟡 Médio | Não documentados | Respostas enlatadas; testar com jogos que dependem |
| R9 | **Subcode desatualizado** | Média | 🟡 Médio | ✅ spec: refresh a 13,3 ms | Gerar continuamente, não sob demanda |
| R10 | **Multi-sessão GD não suportada** | Baixa | 🟢 Baixo | `FillGDSession()` hardcoded | Fase tardia; começar por sessão única |
| R11 | **CD-DA nunca arranca** | Média | 🟡 Médio | Dreamdrive nunca o fez | scoping próprio ([04 §6](04-clock-aica.md)) |
| R12 | **tB violado sob carga** | Baixa | 🟡 Médio | Pré-atenção insuficiente | Dimensionar o ring; nunca servir do SD |
| R13 | **Alimentação 12 V danifica a placa** | Baixa | 🔴 Crítico | A25/B25 ([02 §2.1](02-barramento-g1-pinout.md)) | Multímetro antes de ligar; fusível |
| R14 | **Bugs de DMA de terceiros** | — | 🟡 Médio | `dma_bus_handler` gera o clock no `/RD` do host | Reescrever de raiz ([06b §3.1](06b-scope-pio-vs-dma.md)) |

## 2. Licenças — a parte que é fácil de errar

| Fonte | Licença | Pode reutilizar? |
|---|---|---|
| **Dreamdrive** — `Dreamcast/sw/rp2350/*` | `SPDX-License-Identifier: BSD-2-Clause` nos fontes | ✅ **Sim**, com atribuição, em módulo à parte |
| **Dreamdrive** — raiz / `hw/` | *"Dreamdrive Open Hardware License v1.0"* (não-comercial) | ⚠️ **Hardware restrito.** Separar firmware de hardware |
| **Flycast** | 🔴 **GPL-2.0** (verificado no `LICENSE` da raiz) | ❌ **Não copiar.** Referência de comportamento e de constantes |
| **libKOS** | BSD-2-Clause | ✅ Sim |
| **iceGDROM** | 🔴 **GPL-3.0** (verificado no `COPYING` da raiz) | ❌ **Não copiar.** Referência apenas |
| **ZuluIDE** — `src/`, `rp2350_ide_phy.cpp` | **GPL-3.0 + HSL Exception** | ❌ **Não copiar.** Só *padrões de desenho* |
| **ZuluIDE** — `libzuluide_rp2350b_core1_encrypted.a` | Código fechado | ❌ Nada |
| **ZuluIDE** — `fpga_bitstream.h` | Restrito a hardware RHC | ❌ Nada |
| **MAME** | GPL-2.0+ | ❌ **Não copiar.** Extrair constantes e ler |
| **Linux kernel** (`drivers/cdrom/gdrom.c`) | GPL-2.0 | ❌ **Não copiar.** Ler como referência |
| **A spec SPI Ver.1.30** | Documento proprietário da SEGA | ⚠️ Implementar é fine; **redistribuir a transcrição** é que é duvidoso |

### 2.1 Decisão: Apache-2.0

**Decidido.** O projecto é licenciado **Apache-2.0**, com este raciocínio:

- **Código nosso** pode ser Apache-2.0, sem excepção.
- **Código copiado de uma fonte BSD-2** mantém os seus avisos e vive num módulo à parte,
  também sob os termos da fonte original. Isso é compatível com Apache-2.0.
- **Código GPL não é copiado** — nem do iceGDROM, nem do MAME, nem do Linux, nem do
  ZuluIDE. São lidos para conferir comportamento e reimplementados a partir da spec.

⚠️ **Porquê Apache-2.0 e não MIT:** o MIT **não contém nenhuma concessão de patentes**. Um
ODE é um projecto de hardware que atravessa folhas de terceiros, e é exactamente o perfil
de litígio de patentes. A Apache-2.0 dá a concessão explícita e tem cláusula de
terminação da concessão se houver litígio. Custa ~2 KB de texto extra.

⚠️ **Isto obriga a reimplementar**, não só no iceGDROM/MAME, mas em todo o lado onde a
atribuição e o comentário original sejam a documentação. Concretamente: a máquina de
estados de `rv32/source/ide.c` e o `do_command()` do MAME têm de ser reescritos a partir
do texto da spec Ver.1.30, que é a fonte primária lawful.

⚠️ **Copyright não cobre ideias, só expressão.** Ler código GPL e escrever a sua própria
implementação é legítimo — mas em direito dos EUA aplica-se o teste *"reads like the
original"*, e o software de computador é subjecto a ele. Na dúvida, implemente a partir do
**documento da Sega**, não a partir do código de terceiros.

⚠️ **A spec SPI é um documento proprietário da SEGA.** Ler e implementar é fine; redistribuir
a transcrição completa pode não ser. O Dreamdrive inclui
`Dreamcast/sw/rp2350/Sega Pack Interface.md` (27 KB) sob o header BSD-2 — é uma questão
que vale a pena ter em atenção antes de publicar.

⚠️ **MAME é GPL-2.0+.** As constantes e o comportamento do `gdrom.cpp` são a melhor
documentação executável que existe, mas **copiar código para um projecto com licença
permissiva é um erro de licença.** Extrair constantes e verificar comportamento é
aceitável; copiar funções não.

## 3. Questões legais / éticas

1. **`IP.BIN`.** O Dreamdrive embute ~4 KB de dados do IP.BIN do BIOS. Isto é conteúdo
   proprietário da SEGA. **Preferir um dump de BIOS do próprio console** (o usuário é
   dono do hardware) e não redistribuir o blob. Ver [11 §7](11-estrategia-de-validacao.md).
2. **A BIOS é copyrighted.** Um ODE que responde a `0x70`/`0x71` com o blob certo
   desbloqueia conteúdo que normalmente exigiria a BIOS original. Isto é **in ambíguo**:
   - É o que todos os ODEs fazem (GDEMU, MODE, iceGDROM) e é prática de long-standing.
   - Mas o GDEMU produz apenas o suficiente para a BIOS *original* funcionar, não para
     contornar a protecção.
   - **Não tentar desbloquear conteúdo.** Fazer o device aparente o suficiente para a BIOS
     do utilizador funcionar, e documentar isso.
3. **O modelo Dreamdrive é emulação, não emulação de SCSI.** Não estamos a contornar
   DRM; estamos a substituir um dispositivo de armazenamento. A linha é a mesma que qualquer
   outro ODE.
4. **A Sega é Known: o GD-ROM é read-only e o ODE é read-only.** Nada de escrita. Se
   aparecer `0xC1`/`0xC2` no bus, é outro dispositivo (ver [12](12-correcoes-ao-briefing.md)).

## 4. Riscos de processo

| Risco | Nota |
|---|---|
| **Scope creep** | CD-DA, DMA, multi-sessão, menu — cada um é um projecto. Ver [08 §8](08-arquitetura-proposta.md) para a ordem |
| **Documentar mais do que construir** | Esta fase de estudo tem de ter um fim. O próximo passo tem de ser código |
| **Dependência de um único repo** | O Dreamdrive está parado desde 2025-03 e não compila. Não ficar à espera |
| **Referência de código errada** | O `dma_bus_handler` é elegantemente escrito e **errado**. Verificar sempre contra a spec |

## 5. Decisões que precisam de ser tomadas (por quem decide: você)

| # | Decisão | Opções | Impacto |
|---|---|---|---|
| D1 | Licença do projecto | ✅ **Decidido: Apache-2.0** (§2.1) | BSD-2 do Dreamdrive e libKOS reusáveis; GPL (Flycast, MAME, Linux, ZuluIDE, iceGDROM) só como referência |
| D2 | Risco de PCB | Placa própria com level shifting vs. adaptador com fios | Horas vs meses |
| D3 | Nível de ambição | Repo de referência (não funcional) vs ODE funcional | Tempo ×3 |
| D4 | Timeline | Só estudo vs firmware completo | — |
| D5 | Menu | Reimplementar o `dreammenu.c` (KOS/SH4) vs usar uma imagem SD que já o tenha | Escopo |

---

Ver também: [12-correcoes-ao-briefing](12-correcoes-ao-briefing.md) ·
[08-arquitetura-proposta](08-arquitetura-proposta.md) ·
[10-viabilidade-pinos-e-pcb](10-viabilidade-pinos-e-pcb.md)
