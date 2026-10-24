QT += core gui widgets network
CONFIG += c++11 release
CONFIG -= debug
TEMPLATE = app
TARGET = qt-camare
SOURCES += main.cpp facedetector.cpp
HEADERS += facedetector.h
INCLUDEPATH += D:/OpenCV/mingw32/include
LIBS += -LD:/OpenCV/mingw32/x86/mingw/lib -lopencv_objdetect3420 -lopencv_imgproc3420 -lopencv_core3420
QMAKE_CXXFLAGS += -finput-charset=UTF-8 -fexec-charset=UTF-8
