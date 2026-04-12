# Giromin — Pure Data

Sistema de mapeamento de sensores IMU (Giromin) para MIDI e áudio em Pure Data.

Recebe dados OSC do Giromin via UDP, converte quaternions para ângulos de Euler, filtra e mapeia para MIDI CC, notas e controle de samples.

---

## Estrutura

```
giromin-software/
├── pd-externals/          Externals C++ compiláveis
│   ├── Makefile
│   ├── pd-lib-builder/    (submódulo git)
│   ├── giromin.angulos.cpp  Quaternion → ângulos de Euler
│   ├── giromin.suave.cpp    Filtro EMA assimétrico
│   ├── giromin.map.cpp    Mapeamento [-1,1] → [0,1] com autorange e invert
│   ├── giromin.centro.cpp  Deslocamento de fase cíclica
│   └── giromin.pico.cpp   Detector de picos locais
│
└── pd-patches/            Patches Pure Data
    ├── giromin.pd         Patch principal (entrada OSC, roteamento)
    ├── giromin-osc-receiver.pd   Recepção OSC UDP 1333
    ├── giromin-device.pd         Abstração por dispositivo (usa $1 como ID)
    ├── gesto-1.pd         Euler → CC MIDI + botão → Nota
    ├── gesto-2.pd         Sample com velocidade e volume por Euler
    ├── gesto-3.pd         Taxa de rotação → volume de sample + filtro
    ├── gesto-4.pd         Pico de aceleração → Nota MIDI + sample
    ├── gesto-5.pd  ...
    ├── gesto-8.pd
    └── samples/           Arquivos de áudio (ver link abaixo)
```

---

## Dependências

- **Pure Data** 0.55+ — [msp.ucsd.edu/software.html](https://msp.ucsd.edu/software.html)
- **ELSE library** — instalar via `deken` (Menu Help → Find Externals → buscar `else`)
- **pd-lib-builder** — incluído como submódulo em `pd-externals/pd-lib-builder/`

---

## Compilar os Externals

```bash
cd pd-externals

# macOS
make PDINCLUDEDIR=/Applications/Pd-0.56-2.app/Contents/Resources/src

# Linux
make PDINCLUDEDIR=/usr/include/pd

# Limpar
make clean
```

Os binários `.pd_darwin` (macOS) ou `.pd_linux` ficam na mesma pasta. O patch principal usa `[declare -path ../pd-externals]` para encontrá-los automaticamente.

---

## Externals

### `giromin.angulos`
Converte quaternion (w x y z) → 3 ângulos de Euler Tait-Bryan.

```
[giromin.angulos xyz]
  inlet 0:  lista "w x y z"
  outlet 0: euler_first  [-π, π]
  outlet 1: euler_last   [-π, π]
  outlet 2: euler_mid    [-π/2, π/2]

Mensagem: order xyz  (ou xzy yxz yzx zxy zyx)
```

### `giromin.map`
Mapeia valores para [0, 1] com range ajustável, inversão e autorange.

```
[giromin.map -1 1]
  inlet 0 (hot):  float / mensagem "invert" / "autorange 1" / "autorange 0"
  inlet 1 (cold): in_min
  inlet 2 (cold): in_max
  outlet 0: valor mapeado [0, 1]
  outlet 1: ar_min (durante autorange)
  outlet 2: ar_max (durante autorange)
```

### `giromin.centro`
Desloca o centro de dados cíclicos, retornando o delta pelo caminho mais curto.

```
[giromin.centro]          (amplitude default = 1, para dados em [-1,1])
[giromin.centro 3.14159]  (para Euler em radianos)
  inlet 0 (hot):  float / mensagem "center" / "center <f>"
  inlet 1 (cold): amplitude
  outlet 0: delta em [-amplitude, amplitude]
```

### `giromin.suave`
Filtro EMA assimétrico. `slide=1` = pass-through, valores maiores = mais lento.

```
[giromin.suave 1 20]   (rise=1 = imediato, fall=20 = ~20 steps para descer)
  inlet 0 (hot):  float
  inlet 1 (cold): rise (steps de subida)
  inlet 2 (cold): fall (steps de descida)
  outlet 0: valor filtrado

Mensagem: reset
```

### `giromin.pico`
Detecta picos locais acima de um threshold. Emite o valor do pico e depois 0 após o debounce.

```
[giromin.pico 0.5 1000]   (threshold=0.5, debounce=1000ms)
  inlet 0 (hot):  float
  inlet 1 (cold): threshold
  inlet 2 (cold): debounce_ms
  outlet 0: peak_val no momento do pico, 0 após debounce_ms

Mensagem: reset
```

---

## Protocolo OSC

Porta UDP: **1333**

| Path | Dado |
|---|---|
| `/giromin/{id}/q` | quaternion: `w x y z` (4 floats) |
| `/giromin/{id}/a/x` `/a/y` `/a/z` | acelerômetro (float) |
| `/giromin/{id}/g/x` `/g/y` `/g/z` | giroscópio (float) |
| `/giromin/{id}/b1` `/b2` | botões (float 0 ou 1) |

Para monitorar OSC: [Protokol](https://hexler.net/protokol)

---

## Samples

Os arquivos de áudio usados nos patches estão disponíveis em:

**[Google Drive — Samples Giromin](https://drive.google.com/drive/folders/1FI6EpSIjifoTRLyVYDnfXMbh4aT_9_tN?usp=sharing)**

Baixar e colocar em `pd-patches/samples/`.

Os arquivos de áudio estão no `.gitignore` e não são versionados.
