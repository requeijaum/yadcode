# Plano — Estado e próximos passos

> Actualizado 2026-09-29. Este ficheiro substituiu o plano KOS inicial,
> cuja Fase 1 já foi executada. O que resta está em `docs/19` §8 e
> `docs/22` §6, e é reproduzido em baixo.

## O que foi feito (2026-09-28 → 29)

- **Docs 19, 20, 21, 22** escritos. O `docs/20` fecha a questão **F**
  (CRC do subcode: `0x1021`, init `0xFFFF`, NOT final) a partir da
  biblioteca da Sega. O `docs/21` fecha o W1 (11 syscalls GDC, 0–10) e
  fecha o W2 (stub, sem leadout). O `docs/22` explica porquê o W3 não é
  alcançável.
- **CRC do subcode** em `fw/src/gd_crc.c` + `gd_crctbl.c` (tabela extraída
  e verificada 256/256), com 13 checks. **Não ligado ao firmware** — falta
  medir em hardware.
- **6 `1ST_READ.BIN`** extraídos de dumps de retail e decompilados
  (2193/2194 funções cada).
- **517 checks**, oito suites, ASan+UBSan limpo.

### Estudo do OpenGDEMU (2026-09-29) — `docs/25`

O `.FW` do GDEMU (98304 B) é **cifrado** (entropia 7,998 bits/byte) e não é
analisável. Em vez disso clonou-se o **OpenGDEMU** (GPL-3.0) para `ref/opengdemu`,
a única implementação **aberta** de hardware de GD-ROM.

- 🟢 **Questão B respondida sem hardware:** o `0xA1` **não aborta** — devolve
  **512 bytes**. O abort está no `0xEC`, imposto pela ATA. Os "80 bytes" da
  pergunta estavam errados desde a formulação.
- 🔴 **Requisito de arranque novo:** o canal de DMA da BIOS **aborta a ~10 KB**,
  e o primeiro read é de **7 sectores (14 KB, o IP.BIN)**. Um emulador que sirva
  `CD_READ` com latência no caminho **não arranca**. Ver `docs/25` §5.
- 🟡 **Questão C estreitada:** o timeout de 10 KB só existe se o host usar DMA.
  Evidência forte, mas o OpenGDEMU assume o DMA em vez de o medir.
- 🟡 **Questão A estreitada:** 6 bytes é o mínimo que a BIOS aceita; o Flycast
  envia 1012 e a origem dessa diferença continua desconhecida.
- ⚠️ **Divergências no nosso firmware** (nenhuma alterada — ver `docs/25` §6-7):
  - IDENTIFY devolve **64 bytes**; o ATAPI manda **512**.
  - `REQ_MODE` (18,8) devolve `"Rev 5.07"` no firmware e `"Rev 6.43"` no `docs/13`;
    um GDEMU stock devolve **`"Rev 6.42"`**. Nenhum dos nossos está confirmado.
  - `REQ_MODE` (0,10) stock = `[00 00 00 00 00 b4 19 00 00 08]` — não verificado
    no nosso lado.
  - `REQ_MODE` com offset/len inesperados: stock **aborta (0x50)**, o nosso responde.
  - `SET_MODE` usa `GD_PHASE_DATA_IN` para um transfer de escrita. Funciona por
    ordem de dispatch, não por desenho. Armadilha para refactors.
- **D, E, G, H inalteradas.** O OpenGDEMU não implementa device select.

## O que está bloqueado

- **A, C, D, E, G** — hardware. Ver `docs/16` §8.
- **H** (device select) — decisão de firmware em espera. O OpenGDEMU não
  implementa, logo não ajuda.
- **W3 / Fase 2** (nomes G1 no C) — não alcançável: o loader acede ao
  barramento por tabela. Ver `docs/22`.

## Próximos passos sugeridos, por valor

1. **Ler a sequência de packets do `1ST_READ.BIN` via assembly.** As
   coordenadas dos registos G1 estão em `docs/22` §4. É o caminho para B
   sem hardware. Custo: leitura manual, Exacta.
2. **Comparar os 6 `1ST_READ.BIN`** para isolar o loader da Sega do
   específico de cada jogo — o contrato que o ODE tem de servir.
3. **Mapear o dispatcher de syscalls na BIOS** (bloco `0x8C000000`–
   `0x8C004000`, a tabela 0–10 do `docs/21`). Mesmo problema de acesso-por-
   tabela, solução: assembly.
4. **H** — se decidires implementar, 4 testes, firmware-only.

**Não fazer agora:** ligar o CRC ao firmware (falta medição); a sniffer de
dados (sem placa, sem teste); recomp 1:1 (impossível — `docs/19` §6).

## Pendente desta sessão (2026-09-29)

- **Corrigir `docs/19`, `21`, `22`** — a inversão IP.BIN/1ST_READ.BIN, as
  contagens G1 alinhadas (1/0/1, não 19/1/3), e a conclusão de que o
  loader não toca no G1. Aviso de estado no topo dos três.
- **`docs/23`** — a tabela de 13 syscalls nos discos de retail,
  byte-idêntica em 4 de 5, mais `fw/tools/gd_systable.c`.
- **Corrigir a wiki** `~/projects/katana-sdk-lab/wiki/` — três erros
  herdados: polinómio "não standard" (é `0x1021`), a sequência de boot, e
  o inventado `SB_G1GDRD = 0x005F749C`.

Método e armadilhas em [docs/24](docs/24-ghidra-re.md); inventário do
backup em `~/backups/yadcode/re-ghidra/MANIFESTO.md`.
