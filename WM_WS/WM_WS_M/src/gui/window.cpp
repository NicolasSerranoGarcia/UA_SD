#include "gui/window.hpp"

#include <QCloseEvent>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QStyle>
#include <QTextCursor>
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
    layout->setRowWrapPolicy(QFormLayout::WrapLongRows);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addRow(title, label);
    return label;
}

QString phaseText(app::Phase phase) {
    switch (phase) {
    case app::Phase::Resolving: return QStringLiteral("Resolving Central");
    case app::Phase::Registering: return QStringLiteral("Registering and activating");
    case app::Phase::Monitoring: return QStringLiteral("Monitoring Engine");
    case app::Phase::Retrying: return QStringLiteral("Reconnecting to Engine");
    case app::Phase::Failed: return QStringLiteral("Monitor could not continue");
    case app::Phase::Stopped: return QStringLiteral("Stopped");
    }
    return {};
}

void setIndicator(QLabel *label, const char *state) {
    label->setProperty("state", state);
    label->style()->unpolish(label);
    label->style()->polish(label);
}

} // namespace

Window::Window(const app::Options &options)
    : logPath_(QFileInfo(QString::fromStdString(options.logFile)).absoluteFilePath()) {
    setWindowTitle(QStringLiteral("Watering station monitor - %1").arg(QString::fromStdString(options.wsId)));
    resize(700, 760);
    // Use the assignment's desktop panel as a reference for compact rows and status cells.
    setStyleSheet(QStringLiteral(R"(
        QWidget { background: #ededed; color: #20252b; font-family: "Segoe UI", "Noto Sans", "DejaVu Sans", sans-serif; font-size: 10pt; }
        QLabel { background: transparent; }
        QLabel#heading { font-size: 12pt; font-weight: bold; border-bottom: 1px solid #b4b9bf; padding-bottom: 10px; }
        QLabel#phase { color: #414b57; padding: 4px 0; }
        QGroupBox { background: #ffffff; border: 1px solid #b4b9bf; margin-top: 10px; padding: 16px 12px 10px; }
        QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; color: #343d48; }
        QLabel[state] { padding: 6px 8px; border: 1px solid #adb3ba; background: #d8dce0; color: #20252b; }
        QLabel[state="healthy"] { background: #b8dfbf; border-color: #7cae86; }
        QLabel[state="fault"] { background: #f0baba; border-color: #c78686; }
        QPushButton { background: #f8f8f8; border: 1px solid #a4aab2; padding: 6px 12px; }
        QPushButton:hover { background: #e4eaf0; }
        QPushButton:pressed { background: #d4dde7; }
        QPushButton:focus { border: 1px solid #2c6399; }
        QPushButton:disabled { color: #777f89; border-color: #c1c5ca; }
        QPlainTextEdit { background: #ffffff; color: #20252b; border: 1px solid #adb3ba; padding: 8px; selection-background-color: #c4dcf3; selection-color: #172c42; }
        QScrollArea { border: none; }
    )"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("WaterManagement - Station Monitor"));
    title->setObjectName("heading");
    auto titleFont = title->font();
    titleFont.setPointSize(12);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);
    phase_ = new QLabel;
    phase_->setObjectName("phase");
    phase_->setWordWrap(true);
    phase_->setTextFormat(Qt::PlainText);
    layout->addWidget(phase_);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto *content = new QWidget;
    auto *panels = new QVBoxLayout(content);
    panels->setContentsMargins(0, 0, 0, 0);
    panels->setSpacing(12);
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);

    auto *station = new QGroupBox(QStringLiteral("Station"));
    auto *stationRows = new QFormLayout(station);
    addRow(stationRows, "Station ID", "wsId", QString::fromStdString(options.wsId));
    addRow(stationRows, "Location", "location", QString::fromStdString(options.location));
    panels->addWidget(station);

    auto *engine = new QGroupBox(QStringLiteral("Engine supervision"));
    auto *engineRows = new QFormLayout(engine);
    addRow(engineRows, "Endpoint", "engineEndpoint", options.engineHostname.empty() ? endpointText(options.engineEndpoint) :
           QStringLiteral("%1:%2").arg(QString::fromStdString(options.engineHostname)).arg(options.engineEndpoint.portHostOrder()));
    health_ = addRow(engineRows, "Health", "engineHealth", "Waiting for a health response");
    lastHealth_ = addRow(engineRows, "Last health response", "lastHealth", "No health response yet");
    panels->addWidget(engine);

    auto *central = new QGroupBox(QStringLiteral("Central connection"));
    auto *centralRows = new QFormLayout(central);
    addRow(centralRows, "Configured endpoint", "centralHost",
           QStringLiteral("%1:%2").arg(QString::fromStdString(options.centralHostname)).arg(options.centralPort));
    centralEndpoint_ = addRow(centralRows, "Resolved endpoint", "centralEndpoint", "Pending DNS resolution");
    centralState_ = addRow(centralRows, "Connection", "centralState", "Waiting for Central");
    centralError_ = addRow(centralRows, "Latest Central error", "centralError", "None");
    panels->addWidget(central);

    auto *diagnostics = new QGroupBox(QStringLiteral("Diagnostics"));
    auto *diagnosticRows = new QFormLayout(diagnostics);
    lastError_ = addRow(diagnosticRows, "Latest Monitor / Engine error", "lastError", "None");
    addRow(diagnosticRows, "Activity log", "logPath", QDir::toNativeSeparators(logPath_));
    panels->addWidget(diagnostics);
    panels->addStretch();

    auto *notice = new QLabel(QStringLiteral("Status updates automatically. Use Engine to control watering and simulate or repair a fault."));
    notice->setWordWrap(true);
    layout->addWidget(notice);
    auto *actions = new QHBoxLayout;
    auto *help = new QPushButton(QStringLiteral("&Help"));
    help->setObjectName("helpButton");
    auto *helpShortcut = new QShortcut(QKeySequence(Qt::Key_F1), this);
    helpShortcut->setContext(Qt::ApplicationShortcut);
    connect(helpShortcut, &QShortcut::activated, this, &Window::showHelp);
    help->setToolTip(QStringLiteral("Monitor controls and status meanings (F1)"));
    connect(help, &QPushButton::clicked, this, &Window::showHelp);
    actions->addWidget(help);
    auto *log = new QPushButton(QStringLiteral("Open Activity &Log"));
    log->setObjectName("logButton");
    auto *logShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_L), this);
    logShortcut->setContext(Qt::ApplicationShortcut);
    connect(logShortcut, &QShortcut::activated, this, &Window::showLog);
    log->setToolTip(QStringLiteral("Read the latest activity log (Ctrl+L)"));
    connect(log, &QPushButton::clicked, this, &Window::showLog);
    actions->addWidget(log);
    actions->addStretch();
    quit_ = new QPushButton(QStringLiteral("&Quit Monitor"));
    quit_->setObjectName("quitButton");
    auto *quitShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q), this);
    quitShortcut->setContext(Qt::ApplicationShortcut);
    connect(quitShortcut, &QShortcut::activated, this, &QWidget::close);
    quit_->setToolTip(QStringLiteral("Stop monitoring and close the window (Ctrl+Q)"));
    connect(quit_, &QPushButton::clicked, this, &QWidget::close);
    actions->addWidget(quit_);
    layout->insertLayout(1, actions);
    updateStatus({});
}

void Window::updateStatus(const app::Status &status) {
    const bool stopped = status.phase == app::Phase::Stopped || status.phase == app::Phase::Failed;
    centralState_->setText(stopped ? "Not reporting to Central" : status.centralAvailable ?
        "Available" : "Unavailable - retrying; new irrigation must wait");
    setIndicator(centralState_, !stopped && status.centralAvailable ? "healthy" : "disconnected");
    centralError_->setText(status.centralError.empty() ? QStringLiteral("None") : QString::fromStdString(status.centralError));
    if (!closing_)
        phase_->setText(phaseText(status.phase));
    if (status.centralEndpoint)
        centralEndpoint_->setText(endpointText(*status.centralEndpoint));
    health_->setText(stopped ? "Not monitoring Engine" : status.health == app::Health::Ok ? "Healthy" :
                    status.health == app::Health::Ko ? "Fault detected - repair needed" : "Waiting for a health response");
    setIndicator(health_, !stopped && status.health == app::Health::Ok ? "healthy" :
                         !stopped && status.health == app::Health::Ko ? "fault" : "disconnected");
    if (status.lastHealth) {
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            status.lastHealth->time_since_epoch()).count();
        lastHealth_->setText(QDateTime::fromMSecsSinceEpoch(milliseconds).toString("yyyy-MM-dd HH:mm:ss"));
    }
    lastError_->setText(status.lastError.empty() ? QStringLiteral("None") : QString::fromStdString(status.lastError));
}

void Window::showHelp() {
    if (!helpDialog_) {
        helpDialog_ = new QDialog(this);
        helpDialog_->setObjectName("helpDialog");
        helpDialog_->setWindowTitle(QStringLiteral("Monitor help"));
        helpDialog_->resize(520, 380);
        auto *layout = new QVBoxLayout(helpDialog_);
        auto *text = new QLabel(QStringLiteral(
            "Monitor checks Engine's health and reports it to Central. Status updates automatically.\n\n"
            "Green: a healthy Engine response or an available Central connection.\n"
            "Red: Engine reported a fault. Repair it from Engine; Monitor then requests activation again.\n"
            "Gray: waiting, disconnected, or stopped.\n\n"
            "If Central is unavailable, Monitor retries automatically. Existing irrigation can finish, but new starts must wait.\n\n"
            "Use Engine to start or stop watering and simulate or repair faults.\n\n"
            "F1: Help   |   Ctrl+L: Activity log   |   Ctrl+Q: Quit Monitor\n\n"
            "Quitting Monitor disconnects supervision. Engine will stop watering when it detects the lost connection."));
        text->setTextFormat(Qt::PlainText);
        text->setWordWrap(true);
        layout->addWidget(text);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
        connect(buttons, &QDialogButtonBox::rejected, helpDialog_, &QDialog::reject);
        layout->addWidget(buttons);
    }
    helpDialog_->show();
    helpDialog_->raise();
    helpDialog_->activateWindow();
}

void Window::showLog() {
    if (!logDialog_) {
        logDialog_ = new QDialog(this);
        logDialog_->setObjectName("logDialog");
        logDialog_->setWindowTitle(QStringLiteral("Monitor activity log"));
        logDialog_->resize(760, 480);
        auto *layout = new QVBoxLayout(logDialog_);
        auto *path = new QLabel(QDir::toNativeSeparators(logPath_));
        path->setTextFormat(Qt::PlainText);
        path->setWordWrap(true);
        path->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(path);
        layout->addWidget(new QLabel(QStringLiteral("Latest 64 KiB. Select Refresh to read new entries.")));
        logContents_ = new QPlainTextEdit;
        logContents_->setObjectName("logContents");
        logContents_->setReadOnly(true);
        layout->addWidget(logContents_);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
        auto *refresh = buttons->addButton(QStringLiteral("&Refresh"), QDialogButtonBox::ActionRole);
        refresh->setObjectName("refreshLogButton");
        connect(refresh, &QPushButton::clicked, this, &Window::refreshLog);
        connect(buttons, &QDialogButtonBox::rejected, logDialog_, &QDialog::reject);
        layout->addWidget(buttons);
    }
    refreshLog();
    logDialog_->show();
    logDialog_->raise();
    logDialog_->activateWindow();
}

void Window::refreshLog() {
    QFile file(logPath_);
    if (!file.open(QIODevice::ReadOnly)) {
        logContents_->setPlainText(QStringLiteral("The activity log could not be opened.\n%1\n\nCheck the path and file permissions, then select Refresh.").arg(file.errorString()));
        return;
    }
    constexpr qint64 limit = 64 * 1024;
    const auto start = qMax(qint64{0}, file.size() - limit);
    if (!file.seek(start)) {
        logContents_->setPlainText(QStringLiteral("The activity log could not be read. Select Refresh to try again."));
        return;
    }
    auto contents = file.read(limit);
    // Omit a partial first line when the log is larger than the preview limit.
    if (start > 0) {
        const auto newline = contents.indexOf('\n');
        if (newline >= 0)
            contents.remove(0, newline + 1);
    }
    if (file.error() != QFileDevice::NoError)
        logContents_->setPlainText(QStringLiteral("The activity log could not be read. Select Refresh to try again.\n%1").arg(file.errorString()));
    else
        logContents_->setPlainText(contents.isEmpty() ? QStringLiteral("No activity has been recorded yet.") : QString::fromUtf8(contents));
    logContents_->moveCursor(QTextCursor::End);
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
            quit_->setEnabled(false);
            phase_->setText(QStringLiteral("Stopping - waiting for pending network operations. Engine will stop watering after disconnection."));
            if (stopHandler_)
                stopHandler_();
        }
        return;
    }
    if (helpDialog_)
        helpDialog_->close();
    if (logDialog_)
        logDialog_->close();
    event->accept();
}

} // namespace gui
