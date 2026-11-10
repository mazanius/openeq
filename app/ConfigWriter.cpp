#include "ConfigWriter.h"

#include "EqModel.h"

#include <QFileInfo>
#include <QSaveFile>

ConfigWriter::ConfigWriter(EqModel* model, QObject* parent)
    : QObject(parent)
    , m_model(model)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(33);
    connect(&m_timer, &QTimer::timeout, this, &ConfigWriter::flush);
    connect(m_model, &EqModel::changed, this, &ConfigWriter::schedule);
}

void ConfigWriter::setTargets(const QString& userPath, const QString& systemPath)
{
    flush();
    m_userPath = userPath;
    m_systemPath = systemPath;
}

void ConfigWriter::schedule()
{
    m_dirty = true;
    if (!m_timer.isActive()) m_timer.start();
}

void ConfigWriter::flush()
{
    m_timer.stop();
    if (!m_dirty) return;
    m_dirty = false;
    const QByteArray text = QByteArray::fromStdString(openeq::serialize(m_model->config()));
    if (!m_userPath.isEmpty() && !writeAtomically(m_userPath, text)) emit writeFailed(m_userPath);
    if (!m_systemPath.isEmpty() && !writeAtomically(m_systemPath, text)) emit writeFailed(m_systemPath);
}

bool ConfigWriter::writeAtomically(const QString& path, const QByteArray& text)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(text);
    return f.commit();
}
