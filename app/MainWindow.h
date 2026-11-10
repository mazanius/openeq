#pragma once

#include "PresetManager.h"

#include <QMainWindow>
#include <QString>

class BandStrip;
class Knob;
class LedButton;
class LedIndicator;
class ValueBox;
class ConfigWriter;
class DeviceManager;
class EqModel;
class GraphicEqPanel;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QMenu;
class QPushButton;
class QSlider;
class QStackedWidget;
class QSystemTrayIcon;
class QToolButton;
class ResponseView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void showFromTray();
    // Developer aid: show a curve from a file without writing it to any device.
    void loadDemo(const QString& path);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    QWidget* buildHeader();
    QWidget* buildToolbar();
    QWidget* buildMasterStrip();
    QWidget* buildBottom();
    void buildTray();
    void buildShortcuts();

    void populateDevices();
    void selectDevice(const QString& guid);
    void updateDeviceState();
    void installOrUninstall();

    void populatePresets(const QString& select = {});
    void loadPreset(const QString& name);
    void savePreset();
    void deletePreset();
    void importPreset();
    void exportPreset();
    void resetCurrent();
    void addBand();

    void setGraphicMode(bool graphic);
    void syncControls();
    void markModified();
    void setStartWithWindows(bool on);
    bool startWithWindows() const;

    EqModel* m_model;
    DeviceManager* m_devices;
    ConfigWriter* m_writer;
    PresetManager m_presets;

    ResponseView* m_view = nullptr;
    BandStrip* m_bands = nullptr;
    GraphicEqPanel* m_graphic = nullptr;
    QStackedWidget* m_bottom = nullptr;

    QComboBox* m_deviceCombo = nullptr;
    QLabel* m_statusPill = nullptr;
    QPushButton* m_installButton = nullptr;
    LedButton* m_power = nullptr;

    QToolButton* m_paramButton = nullptr;
    QToolButton* m_graphicButton = nullptr;
    QComboBox* m_presetCombo = nullptr;
    QLabel* m_modified = nullptr;
    Knob* m_preampKnob = nullptr;
    ValueBox* m_preampBox = nullptr;
    LedButton* m_limiter = nullptr;
    LedIndicator* m_clipLed = nullptr;
    QLabel* m_bottomTitle = nullptr;
    QLabel* m_bottomHint = nullptr;
    QComboBox* m_rangeCombo = nullptr;

    QSystemTrayIcon* m_tray = nullptr;
    QMenu* m_trayMenu = nullptr;
    QMenu* m_trayPresets = nullptr;

    QString m_currentGuid;
    bool m_loading = false;
    bool m_busy = false;
    bool m_quitting = false;
};
