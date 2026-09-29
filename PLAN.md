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

## O que está bloqueado

- **A, B, C, D, E, G** — hardware. Ver `docs/16` §8.
- **H** (device select) — decisão de firmware em espera.
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
