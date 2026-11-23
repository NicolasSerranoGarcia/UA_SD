#include "app/options.hpp"
#include "gui/enrollment.hpp"

#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <iostream>

int main(int argc, char *argv[]) {
    const auto options = app::parseOptions(argc, argv, std::cerr);
    if (!options || !*options)
        return 1;
    QApplication app(argc, argv);
    QElapsedTimer deadline;
    deadline.start();
    QTimer timer;
    int stage = 0;
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        if (deadline.elapsed() > 15000) {
            dialog->reject();
            return;
        }
        if (auto *confirmation = qobject_cast<QMessageBox *>(dialog)) {
            confirmation->button(QMessageBox::Yes)->click();
            return;
        }
        const auto *status = dialog->findChild<QLabel *>("registryStatus");
        if (!status)
            return;
        if (stage == 0 && status->text().startsWith("Not registered.")) {
            stage = 1;
            auto *button = dialog->findChild<QPushButton *>("registerStation");
            QTimer::singleShot(0, button, &QPushButton::click);
        } else if (stage == 1 && status->text().startsWith("Registered.")) {
            stage = 2;
            auto *button = dialog->findChild<QPushButton *>("deregisterStation");
            QTimer::singleShot(0, button, &QPushButton::click);
        } else if (stage == 2 && status->text().startsWith("Deregistered.")) {
            stage = 3;
            dialog->accept();
        }
    });
    timer.start(25);
    gui::runEnrollmentDialog(**options);
    if (stage != 3) {
        std::cerr << "Qt Registry flow did not complete. Stage: " << stage << '\n';
        return 1;
    }
    return 0;
}
