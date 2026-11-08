// Periodicity gallery — a manual test bed for every component.
// Owned by the integration phase; it is intentionally minimal until the
// component modules exist.

#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QVBoxLayout>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    auto* central = new QWidget(&window);
    auto* layout = new QVBoxLayout(central);
    layout->addWidget(new QLabel(QStringLiteral("Periodicity gallery — components pending")));
    window.setCentralWidget(central);
    window.resize(960, 640);
    window.setWindowTitle(QStringLiteral("Periodicity Gallery"));
    window.show();

    return app.exec();
}
