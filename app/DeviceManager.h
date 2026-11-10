#pragma once
// Render endpoints, install state, and the elevated helper.

#include <QList>
#include <QObject>
#include <QString>

#include <functional>

struct Device {
    QString guid;   // "{...}" lower case
    QString name;
    bool isDefault = false;
    bool installed = false;
};

class DeviceNotifier;

class DeviceManager : public QObject {
    Q_OBJECT
public:
    explicit DeviceManager(QObject* parent = nullptr);
    ~DeviceManager() override;

    QList<Device> devices() const { return m_devices; }
    const Device* find(const QString& guid) const;
    QString defaultGuid() const;

    // %ProgramData%\openeq\devices\{guid}.txt: what the APO reads.
    static QString systemConfigPath(const QString& guid);
    // %AppData%\openeq\devices\{guid}.txt: the UI's own copy (works before install).
    static QString userConfigPath(const QString& guid);
    static QString helperPath();

    // Runs openeq-devicectl elevated (UAC prompt) without blocking the UI.
    // done(exitCode, message) is called on the GUI thread. exitCode is the
    // helper's (0 ok, 4 installed but not active, ...) or -1 if it never ran.
    void runHelper(const QStringList& args, std::function<void(int, QString)> done);

public slots:
    void refresh();

signals:
    void devicesChanged();

private:
    QList<Device> m_devices;
    DeviceNotifier* m_notifier = nullptr;
};
