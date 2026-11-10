#pragma once
// Writes the model to disk for the selected device, at most ~30 times a
// second while the user drags, always ending with the latest state.

#include <QObject>
#include <QString>
#include <QTimer>

class EqModel;

class ConfigWriter : public QObject {
    Q_OBJECT
public:
    ConfigWriter(EqModel* model, QObject* parent = nullptr);

    // systemPath may be empty when openeq is not installed on the device.
    void setTargets(const QString& userPath, const QString& systemPath);
    void flush();

    // Atomic replace of path with text. Returns false on failure.
    static bool writeAtomically(const QString& path, const QByteArray& text);

signals:
    void writeFailed(const QString& path);

private:
    void schedule();

    EqModel* m_model;
    QTimer m_timer;
    QString m_userPath;
    QString m_systemPath;
    bool m_dirty = false;
};
