QT += multimedia widgets multimediawidgets gui concurrent
TEMPLATE = app
TARGET = lab-2
INCLUDEPATH +=
SOURCES += main.cpp

macx {
    QT -= opengl
    DEFINES += QT_NO_OPENGL
    LIBS -= -framework OpenGL -framework AGL
    QMAKE_LFLAGS -= -framework OpenGL -framework AGL
    QMAKE_LIBS_OPENGL =
    QMAKE_USE_OPENGL =
}
