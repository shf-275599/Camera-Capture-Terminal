QT += core gui widgets network
CONFIG += c++11 release
CONFIG -= debug
TEMPLATE = app
TARGET = qt-camare
SOURCES += main.cpp facedetector.cpp facestabilizer.cpp expressionrecognizer.cpp
HEADERS += facedetector.h facestabilizer.h expressionrecognizer.h
INCLUDEPATH += D:/OpenCV/mingw32-4.10.0/include
LIBS += -LD:/OpenCV/mingw32-4.10.0/x86/mingw/lib -lopencv_objdetect4100 -lopencv_dnn4100 -lopencv_imgproc4100 -lopencv_core4100
QMAKE_CXXFLAGS += -finput-charset=UTF-8 -fexec-charset=UTF-8 -mstackrealign
