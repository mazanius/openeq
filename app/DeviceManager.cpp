#include "DeviceManager.h"

#include <Endpoints.h>
#include <OpenEqShared.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QPointer>
#include <QStandardPaths>
#include <QTimer>

#include <windows.h>
#include <mmdeviceapi.h>
#include <shellapi.h>

#include <atomic>
#include <thread>

// Forwards endpoint add/remove/default-change notifications to the GUI thread.
class DeviceNotifier final : public IMMNotificationClient {
public:
    explicit DeviceNotifier(DeviceManager* owner) : m_owner(owner)
    {
        m_debounce = new QTimer(owner);
        m_debounce->setSingleShot(true);
        m_debounce->setInterval(300);
        QObject::connect(m_debounce, &QTimer::timeout, owner, &DeviceManager::refresh);
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                       __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&m_enumerator))))
            m_enumerator->RegisterEndpointNotificationCallback(this);
    }
    void shutdown()
    {
        if (m_enumerator) {
            m_enumerator->UnregisterEndpointNotificationCallback(this);
            m_enumerator->Release();
            m_enumerator = nullptr;
        }
        Release();
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient)) {
            *ppv = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_refs; }
    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG r = --m_refs;
        if (r == 0) delete this;
        return r;
    }
    STDMETHODIMP OnDeviceStateChanged(LPCWSTR, DWORD) override { poke(); return S_OK; }
    STDMETHODIMP OnDeviceAdded(LPCWSTR) override { poke(); return S_OK; }
    STDMETHODIMP OnDeviceRemoved(LPCWSTR) override { poke(); return S_OK; }
    STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override
    {
        if (flow == eRender && role == eConsole) poke();
        return S_OK;
    }
    STDMETHODIMP OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    void poke()
    {
        QMetaObject::invokeMethod(m_debounce, qOverload<>(&QTimer::start), Qt::QueuedConnection);
    }

    DeviceManager* m_owner;
    QTimer* m_debounce = nullptr;
    IMMDeviceEnumerator* m_enumerator = nullptr;
    std::atomic<ULONG> m_refs{1};
};

DeviceManager::DeviceManager(QObject* parent)
    : QObject(parent)
{
    m_notifier = new DeviceNotifier(this);
    refresh();
}

DeviceManager::~DeviceManager()
{
    if (m_notifier) m_notifier->shutdown();
}

void DeviceManager::refresh()
{
    QList<Device> list;
    for (const auto& e : openeq::shared::enumerateRenderEndpoints()) {
        Device d;
        d.guid = QString::fromStdWString(e.guid);
        d.name = QString::fromStdWString(e.name);
        d.isDefault = e.isDefault;
        d.installed = openeq::shared::isInstalledOn(e.guid);
        list.push_back(d);
    }
    m_devices = list;
    emit devicesChanged();
}

const Device* DeviceManager::find(const QString& guid) const
{
    for (const auto& d : m_devices)
        if (d.guid == guid) return &d;
    return nullptr;
}

QString DeviceManager::defaultGuid() const
{
    for (const auto& d : m_devices)
        if (d.isDefault) return d.guid;
    return m_devices.isEmpty() ? QString() : m_devices.first().guid;
}

QString DeviceManager::systemConfigPath(const QString& guid)
{
    const QString programData = qEnvironmentVariable("ProgramData", "C:/ProgramData");
    return QDir(programData).filePath(QStringLiteral("openeq/devices/%1.txt").arg(guid));
}

QString DeviceManager::userConfigPath(const QString& guid)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/devices";
    QDir().mkpath(dir);
    return dir + "/" + guid + ".txt";
}

QString DeviceManager::helperPath()
{
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QString::fromWCharArray(openeq::shared::kDeviceCtlExe));
}

void DeviceManager::runHelper(const QStringList& args, std::function<void(int, QString)> done)
{
    const std::wstring exe = QDir::toNativeSeparators(helperPath()).toStdWString();
    std::wstring params;
    for (const QString& a : args) {
        if (!params.empty()) params += L' ';
        params += L'"' + a.toStdWString() + L'"';
    }

    QPointer<DeviceManager> self(this);
    std::thread([self, exe, params, done = std::move(done)] {
        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof sei;
        sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        sei.lpVerb = L"runas";
        sei.lpFile = exe.c_str();
        sei.lpParameters = params.c_str();
        sei.nShow = SW_HIDE;

        int exitCode = -1;
        QString message;
        if (!ShellExecuteExW(&sei)) {
            const DWORD err = GetLastError();
            message = err == ERROR_CANCELLED ? QObject::tr("The administrator prompt was cancelled.")
                                             : QObject::tr("Could not start the openeq helper (error %1).").arg(err);
        } else {
            WaitForSingleObject(sei.hProcess, 120000);
            DWORD code = 1;
            GetExitCodeProcess(sei.hProcess, &code);
            CloseHandle(sei.hProcess);
            exitCode = static_cast<int>(code);
            const QString programData = qEnvironmentVariable("ProgramData", "C:/ProgramData");
            QFile log(QDir(programData).filePath("openeq/devicectl-last.log"));
            if (log.open(QIODevice::ReadOnly)) message = QString::fromUtf8(log.readAll()).trimmed();
        }

        QMetaObject::invokeMethod(
            qApp,
            [self, exitCode, message, done] {
                if (self) self->refresh();
                done(exitCode, message);
            },
            Qt::QueuedConnection);
    }).detach();
}
