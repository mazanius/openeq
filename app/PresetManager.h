#pragma once
// Built-in presets plus user presets stored as .txt in %AppData%\openeq\presets.

#include <EqConfig.h>

#include <QString>
#include <QStringList>

#include <optional>

class PresetManager {
public:
    PresetManager();

    QStringList builtinNames() const;
    QStringList userNames() const;
    bool isBuiltin(const QString& name) const;

    std::optional<openeq::EqConfig> load(const QString& name) const;
    bool save(const QString& name, openeq::EqConfig cfg) const;
    bool remove(const QString& name) const;

    static std::optional<openeq::EqConfig> importFile(const QString& path, QString* error = nullptr);
    static bool exportFile(const QString& path, const openeq::EqConfig& cfg);

private:
    QString pathFor(const QString& name) const;
    QString m_dir;
};
