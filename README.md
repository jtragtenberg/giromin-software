# Giromin Software

Sistema completo para captura e mapeamento de dados de sensores IMU (Giromin) em performance musical.

O repositório tem duas implementações independentes que podem ser usadas em conjunto ou separadamente:

---

## JUCE / C++ — Aplicação Desktop

Aplicação nativa com interface gráfica para receber dados do Giromin e enviar MIDI para DAWs (Ableton, Reaper, etc).

**Pasta:** `Giromin/`

- Recebe OSC UDP (porta 1333) ou MIDI 14-bit do Giromin
- Converte quaternions → ângulos de Euler (6 ordens Tait-Bryan)
- Mapeia até 8 CCs MIDI independentes (7-bit ou 14-bit)
- Interface gráfica com sliders, visualizador 3D de quaternion
- Construído com [JUCE](https://juce.com)

Documentação detalhada: [`ARCHITECTURE.md`](ARCHITECTURE.md)

---

## Pure Data — Patches e Externals

Sistema alternativo em Pure Data para mapeamento OSC → MIDI e controle de áudio em tempo real.

**Pastas:** `pd-externals/` e `pd-patches/`

- Externals C++ compiláveis: `giromin.euler`, `giromin.map`, `giromin.phase`, `giromin.ema`, `giromin.peak`
- 8 patches de gesto prontos para uso
- Não depende do JUCE — funciona diretamente com Pure Data + ELSE library

Documentação detalhada: [`README-pd.md`](README-pd.md)

---

## Protocolo OSC (comum às duas implementações)

Porta UDP: **1333**

| Path | Dado |
|---|---|
| `/giromin/{id}/q` | quaternion: `w x y z` |
| `/giromin/{id}/a/x` `/a/y` `/a/z` | acelerômetro |
| `/giromin/{id}/g/x` `/g/y` `/g/z` | giroscópio |
| `/giromin/{id}/b1` `/b2` | botões (0 ou 1) |

Para monitorar OSC: [Protokol](https://hexler.net/protokol)
