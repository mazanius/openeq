#include "MainWindow.h"

#include "BandStrip.h"
#include "ConfigWriter.h"
#include "DeviceManager.h"
#include "EqModel.h"
#include "GraphicEqPanel.h"
#include "Knob.h"
#include "LedButton.h"
#include "ResponseView.h"
#include "Theme.h"
#include "ValueBox.h"

#include <Response.h>

#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

using namespace openeq;

namespace {

QSettings settings()
{
    return QSettings("openeq", "openeq");
}

const char* kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";

void setPill(QLabel* pill, const QString& text, const char* state)
{
    pill->setText(QString::fromUtf8("● ") + text);
    pill->setProperty("state", state);
    pill->style()->unpolish(pill);
    pill->style()->polish(pill);
}

QFrame* separator()
{
    auto* f = new QFrame;
    f->setObjectName("Separator");
    f->setFixedWidth(1);
    return f;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_model(new EqModel(this))
    , m_devices(new DeviceManager(this))
    , m_writer(new ConfigWriter(m_model, this))
{
    setWindowTitle("openeq");
    setWindowIcon(theme::appIcon());
    resize(1060, 680);
    setMinimumSize(720, 500);

    auto* central = new QWidget;
    central->setObjectName("Region");
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(buildHeader());
    layout->addWidget(buildToolbar());

    m_view = new ResponseView(m_model);
    auto* viewHolder = new QWidget;
    auto* vh = new QHBoxLayout(viewHolder);
    vh->setContentsMargins(6, 6, 6, 6);
    vh->setSpacing(6);
    vh->addWidget(m_view, 1);
    vh->addWidget(buildMasterStrip());
    layout->addWidget(viewHolder, 1);
    layout->addWidget(buildBottom(), 0);
    setCentralWidget(central);

    statusBar()->showMessage(tr("Drag nodes to shape the sound  |  Double-click: add band  |  Scroll on node: Q  |  "
                                "Shift: fine  |  Ctrl+B: bypass"));

    buildTray();
    buildShortcuts();

    connect(m_model, &EqModel::changed, this, &MainWindow::syncControls);
    connect(m_model, &EqModel::changed, this, &MainWindow::markModified);
    connect(m_model, &EqModel::structureChanged, this, &MainWindow::syncControls);
    connect(m_devices, &DeviceManager::devicesChanged, this, &MainWindow::populateDevices);
    connect(m_writer, &ConfigWriter::writeFailed, this, [this](const QString& path) {
        statusBar()->showMessage(tr("Could not save settings to %1").arg(QDir::toNativeSeparators(path)), 6000);
    });

    QSettings s = settings();
    restoreGeometry(s.value("geometry").toByteArray());
    const int range = s.value("rangeDb", 18).toInt();
    m_rangeCombo->setCurrentIndex(std::max(0, m_rangeCombo->findData(range)));

    populatePresets();
    populateDevices();
    syncControls();
}

MainWindow::~MainWindow()
{
    m_writer->flush();
}

// ------------------------------------------------------------------ layout

QWidget* MainWindow::buildHeader()
{
    auto* header = new QWidget;
    header->setObjectName("TopBar");
    auto* h = new QHBoxLayout(header);
    h->setContentsMargins(8, 4, 8, 4);
    h->setSpacing(6);

    auto* icon = new QLabel;
    icon->setPixmap(theme::appIcon().pixmap(16, 16));
    auto* logo = new QLabel("openeq");
    logo->setObjectName("Logo");
    h->addWidget(icon);
    h->addWidget(logo);
    h->addWidget(separator());

    auto* devLabel = new QLabel(tr("Output"));
    devLabel->setObjectName("Dim");
    h->addWidget(devLabel);
    m_deviceCombo = new QComboBox;
    m_deviceCombo->setMinimumWidth(260);
    m_deviceCombo->setToolTip(tr("The headset or speakers openeq is editing"));
    connect(m_deviceCombo, &QComboBox::activated, this,
            [this](int i) { selectDevice(m_deviceCombo->itemData(i).toString()); });
    h->addWidget(m_deviceCombo);

    m_statusPill = new QLabel;
    m_statusPill->setObjectName("Status");
    h->addWidget(m_statusPill);

    m_installButton = new QPushButton;
    connect(m_installButton, &QPushButton::clicked, this, &MainWindow::installOrUninstall);
    h->addWidget(m_installButton);

    h->addStretch();

    m_power = new LedButton(tr("EQ"));
    m_power->setLedColor(theme::kGood);
    m_power->setToolTip(tr("Turn the equalizer on or off (Ctrl+B)"));
    connect(m_power, &QAbstractButton::toggled, this, [this](bool on) {
        if (!m_loading) m_model->setBypass(!on);
    });
    h->addWidget(m_power);
    return header;
}

QWidget* MainWindow::buildToolbar()
{
    auto* bar = new QWidget;
    bar->setObjectName("RegionHeader");
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(8, 3, 8, 3);
    h->setSpacing(4);

    m_paramButton = new QToolButton;
    m_paramButton->setObjectName("SegFirst");
    m_paramButton->setText(tr("Parametric"));
    m_paramButton->setCheckable(true);
    m_graphicButton = new QToolButton;
    m_graphicButton->setObjectName("SegLast");
    m_graphicButton->setText(tr("Graphic"));
    m_graphicButton->setCheckable(true);
    auto* modes = new QButtonGroup(this);
    modes->addButton(m_paramButton);
    modes->addButton(m_graphicButton);
    connect(m_paramButton, &QToolButton::clicked, this, [this] { setGraphicMode(false); });
    connect(m_graphicButton, &QToolButton::clicked, this, [this] { setGraphicMode(true); });
    auto* seg = new QHBoxLayout;
    seg->setSpacing(0);
    seg->addWidget(m_paramButton);
    seg->addWidget(m_graphicButton);
    h->addLayout(seg);
    h->addWidget(separator());

    auto* presetLabel = new QLabel(tr("Preset"));
    presetLabel->setObjectName("Dim");
    h->addWidget(presetLabel);
    m_presetCombo = new QComboBox;
    m_presetCombo->setMinimumWidth(170);
    connect(m_presetCombo, &QComboBox::activated, this, [this](int i) {
        const QString name = m_presetCombo->itemData(i).toString();
        if (!name.isEmpty()) loadPreset(name);
    });
    h->addWidget(m_presetCombo);
    m_modified = new QLabel(QString::fromUtf8("●"));
    m_modified->setToolTip(tr("Modified since the preset was loaded or saved"));
    m_modified->setStyleSheet("color: #f0922e; font-size: 7pt; padding: 0 2px;");
    m_modified->setVisible(false);
    h->addWidget(m_modified);

    auto* save = new QPushButton(tr("Save"));
    save->setToolTip(tr("Save the current curve as a preset (Ctrl+S)"));
    connect(save, &QPushButton::clicked, this, &MainWindow::savePreset);
    h->addWidget(save);

    auto* more = new QToolButton;
    more->setText(QString::fromUtf8("☰"));
    more->setToolTip(tr("Menu"));
    more->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(more);
    menu->addAction(tr("Import preset (.txt, AutoEQ)..."), QKeySequence("Ctrl+O"), this, &MainWindow::importPreset);
    menu->addAction(tr("Export preset..."), this, &MainWindow::exportPreset);
    menu->addAction(tr("Delete preset"), this, &MainWindow::deletePreset);
    menu->addSeparator();
    QAction* autostart = menu->addAction(tr("Start with Windows"));
    autostart->setCheckable(true);
    connect(menu, &QMenu::aboutToShow, this, [this, autostart] { autostart->setChecked(startWithWindows()); });
    connect(autostart, &QAction::toggled, this, &MainWindow::setStartWithWindows);
    menu->addAction(tr("Refresh devices"), m_devices, &DeviceManager::refresh);
    menu->addSeparator();
    menu->addAction(tr("Quit openeq"), this, [this] {
        m_quitting = true;
        qApp->quit();
    });
    more->setMenu(menu);
    h->addWidget(more);

    h->addStretch();

    auto* rangeLabel = new QLabel(tr("Range"));
    rangeLabel->setObjectName("Dim");
    h->addWidget(rangeLabel);
    m_rangeCombo = new QComboBox;
    for (int r : {6, 12, 18, 24}) m_rangeCombo->addItem(QString::fromUtf8("±%1 dB").arg(r), r);
    m_rangeCombo->setToolTip(tr("Graph range"));
    connect(m_rangeCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        const int r = m_rangeCombo->currentData().toInt();
        m_view->setRangeDb(r);
        settings().setValue("rangeDb", r);
    });
    h->addWidget(m_rangeCombo);
    return bar;
}

QWidget* MainWindow::buildMasterStrip()
{
    // Channel-strip style master section next to the graph.
    auto* strip = new QWidget;
    strip->setObjectName("Strip");
    strip->setFixedWidth(92);
    auto* v = new QVBoxLayout(strip);
    v->setContentsMargins(6, 8, 6, 8);
    v->setSpacing(5);

    auto* title = new QLabel(tr("PREAMP"));
    title->setObjectName("Section");
    title->setAlignment(Qt::AlignCenter);
    v->addWidget(title);

    m_preampKnob = new Knob(56);
    m_preampKnob->setRange(-24, 6);
    m_preampKnob->setDefaultValue(0);
    m_preampKnob->setArcFromDefault(true);
    m_preampKnob->setToolTip(tr("Overall gain before the EQ. Cut it to make room for boosts. Double-click: 0 dB"));
    connect(m_preampKnob, &Knob::valueChanged, this, [this](double v) {
        if (!m_loading) m_model->setPreamp(std::round(v * 10) / 10);
    });
    v->addWidget(m_preampKnob, 0, Qt::AlignHCenter);

    m_preampBox = new ValueBox(ValueBox::Decibels);
    connect(m_preampBox, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (!m_loading) m_model->setPreamp(v);
    });
    v->addWidget(m_preampBox);

    v->addSpacing(4);
    m_clipLed = new LedIndicator(tr("CLIP"));
    m_clipLed->setToolTip(tr("Lights when your boosts can push the signal past 0 dBFS"));
    v->addWidget(m_clipLed);

    auto* autoBtn = new QPushButton(tr("Auto"));
    autoBtn->setToolTip(tr("Lower the preamp just enough that the loudest boost cannot clip"));
    connect(autoBtn, &QPushButton::clicked, m_model, &EqModel::autoPreamp);
    v->addWidget(autoBtn);

    m_limiter = new LedButton(tr("LIMIT"));
    m_limiter->setLedColor(theme::kWarn);
    m_limiter->setToolTip(tr("Soft-clip peaks above -2 dBFS instead of distorting"));
    m_limiter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_limiter, &QAbstractButton::toggled, this, [this](bool on) {
        if (!m_loading) m_model->setSoftClip(on);
    });
    v->addWidget(m_limiter);
    v->addStretch();
    return strip;
}

QWidget* MainWindow::buildBottom()
{
    auto* area = new QWidget;
    auto* v = new QVBoxLayout(area);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto* header = new QWidget;
    header->setObjectName("RegionHeader");
    header->setStyleSheet("QWidget#RegionHeader { border-top: 1px solid #191919; }");
    auto* h = new QHBoxLayout(header);
    h->setContentsMargins(8, 2, 8, 2);
    h->setSpacing(6);
    m_bottomTitle = new QLabel(tr("BANDS"));
    m_bottomTitle->setObjectName("Section");
    h->addWidget(m_bottomTitle);
    m_bottomHint = new QLabel(tr("Drag knobs (Shift = fine), double-click to reset, click a readout to type"));
    m_bottomHint->setObjectName("Dim");
    h->addWidget(m_bottomHint);
    h->addStretch();
    auto* reset = new QPushButton(tr("Reset"));
    reset->setToolTip(tr("Back to a flat curve"));
    connect(reset, &QPushButton::clicked, this, &MainWindow::resetCurrent);
    h->addWidget(reset);
    v->addWidget(header);

    m_bottom = new QStackedWidget;
    m_bands = new BandStrip(m_model);
    m_bands->setFixedHeight(136);
    m_bottom->addWidget(m_bands);
    m_graphic = new GraphicEqPanel(m_model);
    m_graphic->setFixedHeight(190);
    m_bottom->addWidget(m_graphic);
    v->addWidget(m_bottom);
    return area;
}

void MainWindow::buildTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    m_tray = new QSystemTrayIcon(theme::appIcon(), this);
    m_tray->setToolTip("openeq");
    m_trayMenu = new QMenu(this);
    m_trayMenu->addAction(tr("Open openeq"), this, &MainWindow::showFromTray);
    m_trayMenu->addSeparator();
    QAction* enabled = m_trayMenu->addAction(tr("EQ enabled"));
    enabled->setCheckable(true);
    connect(enabled, &QAction::toggled, this, [this](bool on) { m_model->setBypass(!on); });
    m_trayPresets = m_trayMenu->addMenu(tr("Presets"));
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(tr("Quit"), this, [this] {
        m_quitting = true;
        qApp->quit();
    });
    connect(m_trayMenu, &QMenu::aboutToShow, this, [this, enabled] {
        QSignalBlocker b(enabled);
        enabled->setChecked(!m_model->config().bypass);
        m_trayPresets->clear();
        for (const QString& n : m_presets.builtinNames() + m_presets.userNames())
            m_trayPresets->addAction(n, this, [this, n] { loadPreset(n); });
    });
    m_tray->setContextMenu(m_trayMenu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) showFromTray();
    });
    m_tray->show();
}

void MainWindow::buildShortcuts()
{
    new QShortcut(QKeySequence::Save, this, [this] { savePreset(); });
    new QShortcut(QKeySequence("Ctrl+B"), this, [this] { m_model->setBypass(!m_model->config().bypass); });
    new QShortcut(QKeySequence("Ctrl+N"), this, [this] { addBand(); });
    new QShortcut(QKeySequence("Ctrl+O"), this, [this] { importPreset(); });
}

void MainWindow::loadDemo(const QString& path)
{
    m_writer->setTargets(QString(), QString()); // never touch device/user files
    if (auto cfg = PresetManager::importFile(path)) {
        m_loading = true;
        m_model->setConfig(*cfg);
        m_loading = false;
        populatePresets(QString::fromStdString(cfg->name));
    }
}

void MainWindow::showFromTray()
{
    show();
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    raise();
    activateWindow();
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    settings().setValue("geometry", saveGeometry());
    m_writer->flush();
    if (m_tray && !m_quitting) {
        hide();
        e->ignore();
        QSettings s = settings();
        if (!s.value("trayHintShown", false).toBool()) {
            m_tray->showMessage("openeq", tr("openeq keeps running in the tray. Your EQ stays active either way."),
                                theme::appIcon(), 4000);
            s.setValue("trayHintShown", true);
        }
        return;
    }
    e->accept();
}

// ----------------------------------------------------------------- devices

void MainWindow::populateDevices()
{
    const QString keep = m_currentGuid.isEmpty() ? settings().value("device").toString() : m_currentGuid;
    {
        QSignalBlocker b(m_deviceCombo);
        m_deviceCombo->clear();
        for (const Device& d : m_devices->devices())
            m_deviceCombo->addItem(d.name + (d.isDefault ? tr("  (default)") : QString()), d.guid);
    }
    if (m_devices->devices().isEmpty()) {
        m_currentGuid.clear();
        updateDeviceState();
        return;
    }
    QString target = m_devices->find(keep) ? keep : m_devices->defaultGuid();
    m_deviceCombo->setCurrentIndex(m_deviceCombo->findData(target));
    if (target != m_currentGuid) selectDevice(target);
    else updateDeviceState();
}

void MainWindow::selectDevice(const QString& guid)
{
    m_writer->flush();
    m_currentGuid = guid;
    settings().setValue("device", guid);

    const Device* dev = m_devices->find(guid);
    const bool installed = dev && dev->installed;
    const QString userPath = DeviceManager::userConfigPath(guid);
    const QString systemPath = DeviceManager::systemConfigPath(guid);

    std::optional<EqConfig> cfg;
    if (installed && QFile::exists(systemPath)) cfg = PresetManager::importFile(systemPath);
    if (!cfg && QFile::exists(userPath)) cfg = PresetManager::importFile(userPath);
    if (!cfg) cfg = EqModel::defaultParametric();

    m_writer->setTargets(userPath, installed ? systemPath : QString());
    m_loading = true;
    m_model->setConfig(*cfg);
    m_loading = false;
    m_modified->setVisible(false);
    populatePresets(QString::fromStdString(cfg->name));
    updateDeviceState();
}

void MainWindow::updateDeviceState()
{
    const Device* dev = m_devices->find(m_currentGuid);
    m_installButton->setEnabled(dev && !m_busy);
    if (!dev) {
        setPill(m_statusPill, tr("No output device"), "inactive");
        m_installButton->setText(tr("Install"));
        return;
    }
    if (!dev->installed) {
        setPill(m_statusPill, tr("Not installed"), "inactive");
        m_installButton->setText(tr("Install"));
        m_installButton->setObjectName("Primary");
        m_statusPill->setToolTip(tr("Edits are saved, but you won't hear them until openeq is installed on this device."));
    } else if (m_model->config().bypass) {
        setPill(m_statusPill, tr("Bypassed"), "bypass");
        m_installButton->setText(tr("Uninstall"));
        m_installButton->setObjectName("");
        m_statusPill->setToolTip(tr("openeq is installed but turned off."));
    } else {
        setPill(m_statusPill, tr("Active"), "active");
        m_installButton->setText(tr("Uninstall"));
        m_installButton->setObjectName("");
        m_statusPill->setToolTip(tr("Changes are applied live to this device."));
    }
    m_installButton->style()->unpolish(m_installButton);
    m_installButton->style()->polish(m_installButton);
    if (m_tray) m_tray->setIcon(theme::appIcon(dev->installed && !m_model->config().bypass));
}

void MainWindow::installOrUninstall()
{
    const Device* dev = m_devices->find(m_currentGuid);
    if (!dev || m_busy) return;
    const bool install = !dev->installed;
    const QString name = dev->name;

    const QString question =
        install ? tr("Install openeq on \"%1\"?\n\nWindows will ask for administrator permission, and sound will cut out "
                     "for a moment while the Windows Audio service restarts.")
                      .arg(name)
                : tr("Remove openeq from \"%1\"?\n\nThe device's original audio effects are restored. Sound will cut "
                     "out for a moment while the Windows Audio service restarts.")
                      .arg(name);
    if (QMessageBox::question(this, install ? tr("Install openeq") : tr("Uninstall openeq"), question) !=
        QMessageBox::Yes)
        return;

    m_writer->flush();
    m_busy = true;
    updateDeviceState();
    statusBar()->showMessage(install ? tr("Installing...") : tr("Uninstalling..."));
    QApplication::setOverrideCursor(Qt::BusyCursor);

    const QString guid = m_currentGuid;
    m_devices->runHelper({install ? "install" : "uninstall", guid}, [this, install, guid, name](int code, QString log) {
        QApplication::restoreOverrideCursor();
        m_busy = false;
        const bool inactive = install && code == 4; // installed, playback fine, but Windows didn't start openeq
        const bool ok = code == 0 || inactive;
        if (ok && install) {
            // The device folder exists now: hand the APO our current curve.
            ConfigWriter::writeAtomically(DeviceManager::systemConfigPath(guid),
                                          QByteArray::fromStdString(serialize(m_model->config())));
        }
        if (guid == m_currentGuid) {
            const Device* dev = m_devices->find(guid);
            m_writer->setTargets(DeviceManager::userConfigPath(guid),
                                 dev && dev->installed ? DeviceManager::systemConfigPath(guid) : QString());
        }
        updateDeviceState();
        if (inactive) {
            QMessageBox box(QMessageBox::Information, tr("openeq"),
                            tr("openeq is installed and your sound works, but Windows did not start the EQ on "
                               "\"%1\" yet, so you may not hear changes.")
                                .arg(name),
                            QMessageBox::Ok, this);
            box.setInformativeText(tr("Try playing something and moving a band. If nothing changes, this device's "
                                      "driver may not allow third-party effects; click Uninstall to remove openeq."));
            box.setDetailedText(log);
            box.exec();
        } else if (ok) {
            statusBar()->showMessage(install ? tr("openeq is active on %1").arg(name)
                                             : tr("openeq was removed from %1").arg(name),
                                     8000);
        } else {
            statusBar()->clearMessage();
            QMessageBox box(QMessageBox::Warning, tr("openeq"),
                            install ? tr("openeq could not be installed on this device.")
                                    : tr("openeq could not be removed from this device."),
                            QMessageBox::Ok, this);
            box.setDetailedText(log);
            box.setInformativeText(log.contains("restored to its original settings")
                                       ? tr("Sound stopped working with openeq on this device, so openeq undid the "
                                            "install. Your device is back to how it was.")
                                       : log.section('\n', -2, -2));
            box.exec();
        }
    });
}

// ----------------------------------------------------------------- presets

void MainWindow::populatePresets(const QString& select)
{
    QSignalBlocker b(m_presetCombo);
    m_presetCombo->clear();
    for (const QString& n : m_presets.builtinNames()) m_presetCombo->addItem(n, n);
    const QStringList user = m_presets.userNames();
    if (!user.isEmpty()) {
        m_presetCombo->insertSeparator(m_presetCombo->count());
        for (const QString& n : user) m_presetCombo->addItem(n, n);
    }
    int idx = m_presetCombo->findData(select);
    if (idx < 0 && !select.isEmpty()) {
        // Curve came from a device file or import that isn't a saved preset.
        m_presetCombo->insertItem(0, select, QString());
        idx = 0;
    }
    m_presetCombo->setCurrentIndex(std::max(0, idx));
}

void MainWindow::loadPreset(const QString& name)
{
    auto cfg = m_presets.load(name);
    if (!cfg) {
        QMessageBox::warning(this, tr("openeq"), tr("Could not load preset \"%1\".").arg(name));
        return;
    }
    cfg->bypass = m_model->config().bypass;
    m_loading = true;
    m_model->setConfig(*cfg);
    m_loading = false;
    m_modified->setVisible(false);
    populatePresets(name);
    statusBar()->showMessage(tr("Loaded preset \"%1\"").arg(name), 4000);
}

void MainWindow::savePreset()
{
    QString current = QString::fromStdString(m_model->config().name);
    if (m_presets.isBuiltin(current) || current.isEmpty()) current = tr("My preset");
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Save preset"), tr("Preset name:"), QLineEdit::Normal, current, &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    if (m_presets.isBuiltin(name)) {
        QMessageBox::information(this, tr("openeq"), tr("\"%1\" is a built-in preset. Please pick another name.").arg(name));
        return;
    }
    if (m_presets.userNames().contains(name, Qt::CaseInsensitive) &&
        QMessageBox::question(this, tr("Save preset"), tr("Replace the existing preset \"%1\"?").arg(name)) !=
            QMessageBox::Yes)
        return;
    if (!m_presets.save(name, m_model->config())) {
        QMessageBox::warning(this, tr("openeq"), tr("The preset could not be saved."));
        return;
    }
    m_loading = true;
    m_model->setName(name);
    m_loading = false;
    m_modified->setVisible(false);
    populatePresets(name);
    statusBar()->showMessage(tr("Saved preset \"%1\"").arg(name), 4000);
}

void MainWindow::deletePreset()
{
    const QString name = m_presetCombo->currentData().toString();
    if (name.isEmpty() || m_presets.isBuiltin(name)) {
        QMessageBox::information(this, tr("openeq"), tr("Built-in presets can't be deleted."));
        return;
    }
    if (QMessageBox::question(this, tr("Delete preset"), tr("Delete the preset \"%1\"?").arg(name)) != QMessageBox::Yes)
        return;
    m_presets.remove(name);
    populatePresets(QString::fromStdString(m_model->config().name));
}

void MainWindow::importPreset()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import preset"), QString(),
                                                      tr("EQ presets (*.txt);;All files (*)"));
    if (path.isEmpty()) return;
    QString error;
    auto cfg = PresetManager::importFile(path, &error);
    if (!cfg) {
        QMessageBox::warning(this, tr("Import preset"), error);
        return;
    }
    cfg->bypass = m_model->config().bypass;
    m_loading = true;
    m_model->setConfig(*cfg);
    m_loading = false;
    populatePresets(QString::fromStdString(cfg->name));
    m_modified->setVisible(true);
    statusBar()->showMessage(tr("Imported %1 bands from %2. Click Save to keep it as a preset.")
                                 .arg(cfg->bands.size())
                                 .arg(QFileInfo(path).fileName()),
                             8000);
}

void MainWindow::exportPreset()
{
    QString name = QString::fromStdString(m_model->config().name);
    if (name.isEmpty()) name = "openeq preset";
    const QString path = QFileDialog::getSaveFileName(this, tr("Export preset"), name + ".txt", tr("EQ presets (*.txt)"));
    if (path.isEmpty()) return;
    if (!PresetManager::exportFile(path, m_model->config()))
        QMessageBox::warning(this, tr("Export preset"), tr("The file could not be written."));
    else statusBar()->showMessage(tr("Exported to %1").arg(QDir::toNativeSeparators(path)), 5000);
}

void MainWindow::resetCurrent()
{
    EqConfig cfg = m_model->isGraphic() ? EqConfig::makeGraphic() : EqModel::defaultParametric();
    cfg.name = "Flat";
    cfg.bypass = m_model->config().bypass;
    m_loading = true;
    m_model->setConfig(cfg);
    m_loading = false;
    m_modified->setVisible(false);
    populatePresets("Flat");
}

void MainWindow::addBand()
{
    if (!m_model->canAddBand()) {
        statusBar()->showMessage(m_model->isGraphic() ? tr("Switch to Parametric to add bands.")
                                                      : tr("openeq supports up to %1 bands.").arg(kMaxBands),
                                 4000);
        return;
    }
    // Put the new band in the widest gap (on a log scale) between existing ones.
    std::vector<double> fs{20.0};
    for (const Band& b : m_model->config().bands) fs.push_back(b.freq);
    fs.push_back(20000.0);
    std::sort(fs.begin(), fs.end());
    double bestGap = 0, freq = 1000;
    for (size_t i = 1; i < fs.size(); ++i) {
        const double gap = std::log10(fs[i]) - std::log10(fs[i - 1]);
        if (gap > bestGap) {
            bestGap = gap;
            freq = std::sqrt(fs[i] * fs[i - 1]);
        }
    }
    m_model->addBand(Band{FilterType::Peak, std::round(freq), 0.0, 1.41, true});
}

// ----------------------------------------------------------------- state

void MainWindow::setGraphicMode(bool graphic)
{
    if (graphic == m_model->isGraphic()) return;
    if (graphic) {
        const bool trivial = std::all_of(m_model->config().bands.begin(), m_model->config().bands.end(),
                                         [](const Band& b) { return !filterUsesGain(b.type) || b.gainDb == 0.0; });
        if (!trivial &&
            QMessageBox::question(this, tr("Switch to Graphic"),
                                  tr("Graphic mode uses 10 fixed bands. openeq will approximate your current curve; "
                                     "the exact parametric bands will be replaced.\n\nContinue?")) != QMessageBox::Yes) {
            syncControls();
            return;
        }
        m_model->convertToGraphic();
    } else {
        m_model->convertToParametric();
    }
}

void MainWindow::syncControls()
{
    const EqConfig& cfg = m_model->config();
    const bool wasLoading = m_loading;
    m_loading = true;
    const bool graphic = m_model->isGraphic();
    m_paramButton->setChecked(!graphic);
    m_graphicButton->setChecked(graphic);
    m_bottom->setCurrentIndex(graphic ? 1 : 0);
    m_bottom->setFixedHeight(graphic ? 190 : 136);
    m_bottomTitle->setText(graphic ? tr("GRAPHIC EQ") : tr("BANDS"));
    m_bottomHint->setText(graphic ? tr("Drag faders (scroll = 0.1 dB), click a readout to type")
                                  : tr("Drag knobs (Shift = fine), double-click to reset, click a readout to type"));
    m_power->setChecked(!cfg.bypass);
    m_preampKnob->setValue(cfg.preampDb);
    m_preampBox->setValueQuietly(cfg.preampDb);
    m_limiter->setChecked(cfg.softClip);
    m_loading = wasLoading;

    const double headroom = m_model->headroomDb();
    if (cfg.bypass || headroom <= 0.05) {
        m_clipLed->setText(tr("CLIP"));
        m_clipLed->setState(theme::kDanger, false);
        m_clipLed->setToolTip(tr("No clipping risk"));
    } else if (!cfg.softClip) {
        m_clipLed->setText(QString("+%1").arg(headroom, 0, 'f', 1));
        m_clipLed->setState(theme::kDanger, true);
        m_clipLed->setToolTip(tr("Boosts exceed the preamp cut by %1 dB and may clip. Click Auto or enable LIMIT.")
                                  .arg(headroom, 0, 'f', 1));
    } else {
        m_clipLed->setText(tr("LIMIT"));
        m_clipLed->setState(theme::kWarn, true);
        m_clipLed->setToolTip(tr("The limiter is catching up to %1 dB of peaks").arg(headroom, 0, 'f', 1));
    }
    updateDeviceState();
}

void MainWindow::markModified()
{
    if (!m_loading) m_modified->setVisible(true);
}

bool MainWindow::startWithWindows() const
{
    return QSettings(kRunKey, QSettings::NativeFormat).contains("openeq");
}

void MainWindow::setStartWithWindows(bool on)
{
    QSettings run(kRunKey, QSettings::NativeFormat);
    if (on)
        run.setValue("openeq",
                     QString("\"%1\" --minimized").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath())));
    else run.remove("openeq");
}
