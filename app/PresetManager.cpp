#include "PresetManager.h"

#include "ConfigWriter.h"
#include "EqModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

using namespace openeq;

namespace {

struct Builtin {
    const char* name;
    EqConfig (*make)();
};

EqConfig parametric(const char* name, double preamp, std::vector<Band> bands)
{
    EqConfig c;
    c.name = name;
    c.preampDb = preamp;
    c.bands = std::move(bands);
    return c;
}

const Builtin kBuiltins[] = {
    {"Flat", [] { return EqModel::defaultParametric(); }},
    {"Bass Boost", [] {
         return parametric("Bass Boost", -6.0, {{FilterType::LowShelf, 105, 6.0, 0.71, true},
                                                 {FilterType::Peak, 300, -1.0, 1.0, true}});
     }},
    {"Warm", [] {
         return parametric("Warm", -3.0, {{FilterType::LowShelf, 150, 3.0, 0.71, true},
                                           {FilterType::HighShelf, 8000, -2.0, 0.71, true}});
     }},
    {"Bright", [] {
         return parametric("Bright", -4.5, {{FilterType::Peak, 3000, 1.5, 1.0, true},
                                             {FilterType::HighShelf, 6000, 4.0, 0.71, true}});
     }},
    {"Vocal Presence", [] {
         return parametric("Vocal Presence", -3.0, {{FilterType::Peak, 200, -2.0, 1.0, true},
                                                     {FilterType::Peak, 2500, 3.0, 1.2, true},
                                                     {FilterType::Peak, 5000, 1.5, 2.0, true}});
     }},
    {"Footsteps (Gaming)", [] {
         return parametric("Footsteps (Gaming)", -4.0, {{FilterType::LowShelf, 120, -4.0, 0.71, true},
                                                         {FilterType::Peak, 2000, 4.0, 1.0, true},
                                                         {FilterType::Peak, 4500, 3.0, 1.5, true}});
     }},
    {"V-Shape", [] {
         EqConfig c = EqConfig::makeGraphic();
         c.name = "V-Shape";
         const double gains[] = {5, 4, 2, 0, -2, -2, 0, 2, 4, 5};
         for (size_t i = 0; i < c.bands.size(); ++i) c.bands[i].gainDb = gains[i];
         c.preampDb = -5.5;
         return c;
     }},
};

} // namespace

PresetManager::PresetManager()
{
    m_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/presets";
    QDir().mkpath(m_dir);
}

QStringList PresetManager::builtinNames() const
{
    QStringList names;
    for (const auto& b : kBuiltins) names << QString::fromUtf8(b.name);
    return names;
}

QStringList PresetManager::userNames() const
{
    QStringList names;
    for (const QFileInfo& fi : QDir(m_dir).entryInfoList({"*.txt"}, QDir::Files, QDir::Name | QDir::IgnoreCase))
        if (!isBuiltin(fi.completeBaseName())) names << fi.completeBaseName();
    return names;
}

bool PresetManager::isBuiltin(const QString& name) const
{
    return builtinNames().contains(name, Qt::CaseInsensitive);
}

QString PresetManager::pathFor(const QString& name) const
{
    QString safe = name;
    for (QChar& c : safe)
        if (QStringLiteral("\\/:*?\"<>|").contains(c)) c = '_';
    return m_dir + "/" + safe + ".txt";
}

std::optional<EqConfig> PresetManager::load(const QString& name) const
{
    for (const auto& b : kBuiltins)
        if (name.compare(QString::fromUtf8(b.name), Qt::CaseInsensitive) == 0) return b.make();
    auto cfg = importFile(pathFor(name));
    if (cfg) cfg->name = name.toStdString();
    return cfg;
}

bool PresetManager::save(const QString& name, EqConfig cfg) const
{
    if (name.trimmed().isEmpty() || isBuiltin(name)) return false;
    cfg.name = name.toStdString();
    cfg.bypass = false;
    return ConfigWriter::writeAtomically(pathFor(name), QByteArray::fromStdString(serialize(cfg)));
}

bool PresetManager::remove(const QString& name) const
{
    if (isBuiltin(name)) return false;
    return QFile::remove(pathFor(name));
}

std::optional<EqConfig> PresetManager::importFile(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return std::nullopt;
    }
    if (f.size() > 256 * 1024) {
        if (error) *error = QObject::tr("File is too large to be an EQ preset.");
        return std::nullopt;
    }
    const QByteArray data = f.readAll();
    auto cfg = parse(std::string_view(data.constData(), static_cast<size_t>(data.size())));
    if (!cfg) {
        if (error) *error = QObject::tr("No EQ settings were found in this file.");
        return std::nullopt;
    }
    if (cfg->name.empty()) cfg->name = QFileInfo(path).completeBaseName().toStdString();
    // Imported graphic presets must still have the 10 fixed bands.
    if (cfg->mode == EqMode::Graphic && cfg->bands.size() != kGraphicFreqs.size()) cfg->mode = EqMode::Parametric;
    return cfg;
}

bool PresetManager::exportFile(const QString& path, const EqConfig& cfg)
{
    return ConfigWriter::writeAtomically(path, QByteArray::fromStdString(serialize(cfg)));
}
