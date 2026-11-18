# Echo canceller and preprocessor from SpeexDSP 1.2.1 (https://github.com/xiph/speexdsp),
# used to remove the stream's own audio from the microphone. See COPYING for its license.

QT       -= core gui

TARGET = speexdsp
TEMPLATE = lib

# Build a static library
CONFIG += staticlib

# Include global qmake defs
include(../globaldefs.pri)

# Older GCC versions defaulted to GNU89
*-g++ {
    QMAKE_CFLAGS += -std=gnu99
}

DEFINES += HAVE_CONFIG_H
INCLUDEPATH += $$PWD $$PWD/include

# Upstream code that we don't modify
*-g++|*-clang|*clang* {
    QMAKE_CFLAGS_WARN_ON += -Wno-unused-parameter -Wno-sign-compare
}
win32-msvc* {
    QMAKE_CFLAGS += /wd4244 /wd4305
}

SOURCES += \
    libspeexdsp/fftwrap.c \
    libspeexdsp/filterbank.c \
    libspeexdsp/mdf.c \
    libspeexdsp/preprocess.c \
    libspeexdsp/smallft.c

HEADERS += \
    config.h \
    include/speex/speex_echo.h \
    include/speex/speex_preprocess.h \
    include/speex/speexdsp_config_types.h \
    include/speex/speexdsp_types.h
