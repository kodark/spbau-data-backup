QT += core
QT += network
QT -= gui
CONFIG += c++11

#QMAKE_CXXFLAGS += -std=c++11
#QMAKE_LFLAGS +=  -std=c++11

!include( $$PWD/../../lib/network_library/network_library.pri ){
    error( "Couldn't find the network_library.pri file!" )
}

#system($$PWD/../protocol/gen.sh)

TARGET = server
CONFIG += console
CONFIG += warn_on
CONFIG -= app_bundle

LIBS += -L/usr/local/lib -lprotobuf


TEMPLATE = app

INCLUDEPATH += \
               $$PWD/../protocol/ \
               $$PWD/../protocol/gen \
               $$PWD/gen

SOURCES += $$PWD/src/main.cpp \
           $$PWD/src/serverclientmanager.cpp \
           $$PWD/src/clientsessiononserver.cpp \
           $$PWD/../protocol/gen/networkMsgStructs.pb.cc \
           $$PWD/gen/serverStructs.pb.cc


HEADERS += \
    $$PWD/src/serverclientmanager.h \
    $$PWD/src/clientsessiononserver.h \
    $$PWD/../protocol/protocol.h \
    $$PWD/../protocol/gen/networkMsgStructs.pb.h \
    $$PWD/gen/serverStructs.pb.h
    
DESTDIR = $$PWD/bin
OBJECTS_DIR = $$PWD/objectfiles
MOC_DIR = $$PWD/mocfiles
