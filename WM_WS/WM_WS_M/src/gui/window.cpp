#include "gui/window.hpp"

#include <QCloseEvent>
#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>
#include <utility>

namespace gui {
namespace {

QString endpointText(const ninttp::IPv4Endpoint &endpoint) {
    const auto address = endpoint.addressHostOrder();
    return QStringLiteral("%1.%2.%3.%4:%5")
        .arg((address >> 24) & 0xff).arg((address >> 16) & 0xff)
        .arg((address >> 8) & 0xff).arg(address & 0xff).arg(endpoint.portHostOrder());
}

QLabel *addRow(QFormLayout *layout, const QString &title, const char *name, const QString &text) {
    auto *label = new QLabel(text);
    label->setObjectName(name);
    label->setTextFormat(Qt::PlainText);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    layout->addRow(title, label);
    return label;
}

QString phaseText(app::Phase phase) {
    switch (phase) {
    case app::Phase::Resolving: return QStringLiteral("Resolving Central");
    case app::Phase::Registering: return QStringLiteral("Registering and activating");
    case app::Phase::Monitoring: return QStringLiteral("Monitoring Engine");
    case app::Phase::Retrying: return QStringLiteral("Reconnecting to Engine");
    case app::Phase::Failed: return QStringLiteral("Startup failed");
    case app::Phase::Stopped: return QStringLiteral("Stopped");
    }
    return {};
}

} // namespace

Window::Window(const app::Options &options) {
    setWindowTitle(QStringLiteral("Watering station monitor — %1").arg(QString::fromStdString(options.wsId)));
    resize(600, 460);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);

    auto *title = new QLabel(QStringLiteral("Watering station monitor"));
    auto titleFont = title->font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);
    phase_ = new QLabel;
    phase_->setObjectName("phase");
    layout->addWidget(phase_);

    auto *station = new QGroupBox(QStringLiteral("Station"));
    auto *stationRows = new QFormLayout(station);
    addRow(stationRows, "WS ID", "wsId", QString::fromStdString(options.wsId));
    addRow(stationRows, "Location", "location", QString::fromStdString(options.location));
    layout->addWidget(station);

    auto *central = new QGroupBox(QStringLiteral("Central"));
    auto *centralRows = new QFormLayout(central);
    addRow(centralRows, "Configured endpoint", "centralHost",
           QStringLiteral("%1:%2").arg(QString::fromStdString(options.centralHostname)).arg(options.centralPort));
    centralEndpoint_ = addRow(centralRows, "Resolved endpoint", "centralEndpoint", "Pending DNS resolution");
    layout->addWidget(central);

    auto *engine = new QGroupBox(QStringLiteral("Engine"));
    auto *engineRows = new QFormLayout(engine);
    addRow(engineRows, "Endpoint", "engineEndpoint", endpointText(options.engineEndpoint));
    health_ = addRow(engineRows, "Health", "engineHealth", "Unknown");
    lastHealth_ = addRow(engineRows, "Last health response", "lastHealth", "No health response yet");
    layout->addWidget(engine);

    auto *diagnostics = new QGroupBox(QStringLiteral("Diagnostics"));
    auto *diagnosticRows = new QFormLayout(diagnostics);
    lastError_ = addRow(diagnosticRows, "Latest startup / Engine error", "lastError", "None");
    layout->addWidget(diagnostics);
    layout->addStretch();
    updateStatus({});
}

void Window::updateStatus(const app::Status &status) {
    if (!closing_)
        phase_->setText(phaseText(status.phase));
    if (status.centralEndpoint)
        centralEndpoint_->setText(endpointText(*status.centralEndpoint));
    health_->setText(status.health == app::Health::Ok ? "OK" : status.health == app::Health::Ko ? "KO" : "Unknown");
    if (status.lastHealth) {
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            status.lastHealth->time_since_epoch()).count();
        lastHealth_->setText(QDateTime::fromMSecsSinceEpoch(milliseconds).toString("yyyy-MM-dd HH:mm:ss"));
    }
    lastError_->setText(status.lastError.empty() ? QStringLiteral("None") : QString::fromStdString(status.lastError));
}

void Window::setStopHandler(std::function<void()> handler) {
    stopHandler_ = std::move(handler);
}

void Window::monitoringFinished() {
    running_ = false;
    if (closing_)
        close();
}

void Window::closeEvent(QCloseEvent *event) {
    if (running_) {
        event->ignore();
        if (!closing_) {
            closing_ = true;
            phase_->setText(QStringLiteral("Stopping — waiting for pending network operations"));
            if (stopHandler_)
                stopHandler_();
        }
        return;
    }
    event->accept();
}

} // namespace gui
