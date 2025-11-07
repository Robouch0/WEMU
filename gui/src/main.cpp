#include <QApplication>
#include <QMainWindow>
#include <QPushButton>

#include "cpuInterface.hpp"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QMainWindow window;
    window.setWindowTitle("Qt GUI Test");

    auto button = new QPushButton("Hello, Qt (cutie :)!", &window);
    window.setCentralWidget(button);

    window.resize(1920, 1080);
    window.show();
    return app.exec();
}
