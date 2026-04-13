QT = core network

greaterThan(QT_MAJOR_VERSION, 5): QT += core5compat

CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = QtAuthNet
DESTDIR = release

INCLUDEPATH += src

SOURCES += \
    src/qtauthnet.cpp \
    src/qtauthnet_client.cpp \
    src/qtauthnet_session.cpp

HEADERS += \
    src/qtauthnet_global.h \
    src/qtauthnet.h \
    src/qtauthnet_client.h \
    src/qtauthnet_session.h
