# Makefile para externals do Giromin em Pure Data
# Usa pd-lib-builder: https://github.com/pure-data/pd-lib-builder
#
# SETUP (apenas uma vez):
#   git clone https://github.com/pure-data/pd-lib-builder.git pd-lib-builder
#
# COMPILAÇÃO:
#   macOS:   make PDINCLUDEDIR=/Applications/Pd-0.56-2.app/Contents/Resources/src
#   Linux:   make PDINCLUDEDIR=/usr/include/pd
#   Windows: make PDINCLUDEDIR="C:/Program Files/Pd/src"
#
# INSTALAÇÃO (copia para a pasta de externals do PD):
#   make install PDLIBDIR=~/Library/Pd   (macOS)
#   make install PDLIBDIR=~/.local/lib/pd  (Linux)
#
# TESTE RÁPIDO sem instalar:
#   Abra giromin-main.pd — ele inclui [declare -path ../pd-externals]
#   que diz ao PD para procurar os externals nesta pasta.
#
# LIMPEZA:
#   make clean

lib.name = giromin

# Um binary por arquivo .cpp (giromin.euler.X e giromin.ema.X)
class.sources = giromin.euler.cpp \
                giromin.ema.cpp \
                giromin.map.cpp \
                giromin.phase.cpp \
                giromin.peak.cpp

# C++14 para lambdas e features modernas (não usadas aqui, mas boa prática)
cflags += -std=c++14 -Wno-cast-function-type-mismatch

# Sem datafiles por enquanto
datafiles =

include pd-lib-builder/Makefile.pdlibbuilder
