QT = core network

greaterThan(QT_MAJOR_VERSION, 5): QT += core5compat

CONFIG += c++17
CONFIG += console
CONFIG -= app_bundle

TARGET = QtAuthNet
DESTDIR = release

SOURCES += \
    src/qtauthnet.cpp

HEADERS += \
    src/qtauthnet.h \
    src/qtauthnet_client.h \
    src/qtauthnet_session.h \
    src/qtauthnet_global.h
