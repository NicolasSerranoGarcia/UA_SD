#pragma once

#include "app/options.hpp"
#include "app/status.hpp"

#include <QWidget>
#include <functional>

class QCloseEvent;
class QLabel;
class QDialog;
class QPlainTextEdit;
class QPushButton;

namespace gui {

class Window : public QWidget {
public:
    explicit Window(const app::Options &options);
    void updateStatus(const app::Status &status);
    void setStopHandler(std::function<void()> handler);
    void monitoringFinished();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void showHelp();
    void showLog();
    void refreshLog();

    QString logPath_;
    QDialog *helpDialog_ = nullptr;
    QDialog *logDialog_ = nullptr;
    QPlainTextEdit *logContents_ = nullptr;
    QPushButton *quit_;
    QLabel *phase_;
    QLabel *centralEndpoint_;
    QLabel *centralState_;
    QLabel *centralError_;
    QLabel *health_;
    QLabel *lastHealth_;
    QLabel *lastError_;
    std::function<void()> stopHandler_;
    bool running_ = true;
    bool closing_ = false;
};

} // namespace gui
