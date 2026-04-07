# Giromin Software

Sistema completo para captura e mapeamento de dados de sensores IMU (Giromin) em performance musical.

O repositório tem duas implementações independentes que podem ser usadas em conjunto ou separadamente:

---

## `Cpp Juce/` — Aplicação Desktop

Aplicação nativa com interface gráfica para receber dados do Giromin e enviar MIDI para DAWs (Ableton, Reaper, etc).

- Recebe OSC UDP (porta 1333) ou MIDI 14-bit do Giromin
- Converte quaternions → ângulos de Euler (6 ordens Tait-Bryan)
- Mapeia até 8 CCs MIDI independentes (7-bit ou 14-bit)
- Interface gráfica com sliders e visualizador 3D de quaternion
- Construído com [JUCE](https://juce.com)

Documentação detalhada: [`Cpp Juce/ARCHITECTURE.md`](Cpp%20Juce/ARCHITECTURE.md)

---

## `Pure Data/` — Patches e Externals

Sistema alternativo em Pure Data para mapeamento OSC → MIDI e controle de áudio em tempo real.

```
Pure Data/
├── pd-externals/   Externals C++ (giromin.euler, .map, .phase, .ema, .peak)
└── pd-patches/     Patches prontos para uso (gesto-1 a gesto-8)
```

- Não depende do JUCE — funciona diretamente com Pure Data + ELSE library
- Samples de áudio disponíveis em: [Google Drive](https://drive.google.com/drive/folders/1FI6EpSIjifoTRLyVYDnfXMbh4aT_9_tN?usp=sharing)

Documentação detalhada: [`Pure Data/README.md`](Pure%20Data/README.md)

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
