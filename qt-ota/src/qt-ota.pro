QT += widgets network
CONFIG += c++11 release
CONFIG -= debug
TARGET = qt-ota
TEMPLATE = app
SOURCES += main.cpp
QMAKE_CXXFLAGS += -finput-charset=UTF-8 -fexec-charset=UTF-8
