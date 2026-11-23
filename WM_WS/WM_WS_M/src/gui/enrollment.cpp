#include "gui/enrollment.hpp"

#include "app/options.hpp"
#include "registry/enrollment.hpp"
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <thread>

namespace gui {

int runEnrollmentDialog(const app::Options &options) {
    const registry::Enrollment enrollment(options);
    QDialog dialog;
    dialog.setObjectName("registryDialog");
    dialog.setWindowTitle(QStringLiteral("Watering station enrollment"));
    auto *layout = new QFormLayout(&dialog);
    layout->addRow("Station ID", new QLabel(QString::fromStdString(options.wsId)));
    layout->addRow("City", new QLabel(QString::fromStdString(options.location)));
    auto *status = new QLabel;
    status->setObjectName("registryStatus");
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    layout->addRow("Enrollment", status);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto *registerButton = buttons->addButton("Register", QDialogButtonBox::ActionRole);
    registerButton->setObjectName("registerStation");
    auto *revokeButton = buttons->addButton("Deregister", QDialogButtonBox::ActionRole);
    revokeButton->setObjectName("deregisterStation");
    auto *refresh = buttons->addButton("Refresh", QDialogButtonBox::ActionRole);
    refresh->setObjectName("refreshEnrollment");
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addRow(buttons);
    dialog.resize(500, dialog.sizeHint().height());

    std::jthread worker;
    const auto setBusy = [&](bool busy) {
        registerButton->setEnabled(!busy);
        revokeButton->setEnabled(!busy);
        refresh->setEnabled(!busy);
    };
    const auto run = [&](registry::Action action) {
        setBusy(true);
        status->setText("Contacting Registry…");
        worker = std::jthread([&, action] {
            const auto result = QString::fromStdString(enrollment.perform(action));
            QMetaObject::invokeMethod(&dialog, [&, result] {
                status->setText(result);
                setBusy(false);
            }, Qt::QueuedConnection);
        });
    };

    QObject::connect(registerButton, &QPushButton::clicked, &dialog, [&] {
        run(registry::Action::Register);
    });
    QObject::connect(revokeButton, &QPushButton::clicked, &dialog, [&] {
        if (QMessageBox::question(&dialog, "Deregister station", "Revoke this station's credential?",
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
            run(registry::Action::Deregister);
    });
    QObject::connect(refresh, &QPushButton::clicked, &dialog, [&] { run(registry::Action::Verify); });
    run(registry::Action::Verify);
    dialog.exec();
    worker.join();
    return 0;
}

} // namespace gui
