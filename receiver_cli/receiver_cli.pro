QT += core network
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = receiver_cli
DESTDIR = ../bin

SOURCES += \
    main.cpp
