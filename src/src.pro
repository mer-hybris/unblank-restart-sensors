# -*- mode: sh -*-

TEMPLATE      = app
TARGET        = unblank-restart-sensors

LIBS         += -Wl,--as-needed
LIBS         += -lrt

QMAKE_CFLAGS += -std=c99
QT -= gui

# FIXME: these are really CPPFLAGS, where to put them in qmake?
QMAKE_CFLAGS += -D_GNU_SOURCE
QMAKE_CFLAGS += -D_FILE_OFFSET_BITS=64

CONFIG       += link_pkgconfig

PKGCONFIG    += glib-2.0
PKGCONFIG    += dbus-glib-1
PKGCONFIG    += mce

SOURCES      += unblank-restart-sensors.c

target.path   = /usr/bin

INSTALLS     += target
