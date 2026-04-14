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
class.sources = giromin.angulos.cpp \
                giromin.suavizar.cpp \
                giromin.map.cpp \
                giromin.centro.cpp \
                giromin.pico.cpp \
                giromin.fusao.cpp

# C++14 para lambdas e features modernas (não usadas aqui, mas boa prática)
# -Wno-cast-function-type-mismatch só existe no clang 16+ (macOS); ignorar em outros compiladores
ifeq ($(shell $(CXX) -Wno-cast-function-type-mismatch -x c++ -E - < /dev/null > /dev/null 2>&1; echo $$?),0)
cflags += -std=c++14 -Wno-cast-function-type-mismatch
else
cflags += -std=c++14 -Wno-cast-function-type
endif

# Windows: linkar estaticamente as runtimes do MinGW para não depender de DLLs externas
ifeq ($(OS),Windows_NT)
ldflags += -static-libgcc -static-libstdc++
endif

# Sem datafiles por enquanto
datafiles =

include pd-lib-builder/Makefile.pdlibbuilder
