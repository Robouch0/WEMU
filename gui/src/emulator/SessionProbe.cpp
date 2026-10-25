#include "SessionProbe.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QTextStream>
#include <QTimer>
#include <memory>

#include "EmulatorLauncher.hpp"
#include "GameView.hpp"

void installSessionProbe(QCoreApplication &application, EmulatorLauncher &launcher, GameView &view)
{
    const auto path = qEnvironmentVariable("QT_WEMU_SESSION_PROBE");
    if (path.isEmpty())
        return;
    QFile configFile(path);
    if (!configFile.open(QIODevice::ReadOnly))
        qFatal("Cannot open desktop session probe configuration");
    const auto config = QJsonDocument::fromJson(configFile.readAll()).object();
    const auto rpx = config["rpx"].toString();
    const auto output = config["output"].toString();
    const auto limit = config["frames"].toInt(4200), measureFrom = config["measureFrom"].toInt(3200);
    QFile script(config["script"].toString());
    if (!QFileInfo(rpx).isFile() || output.isEmpty() || limit <= measureFrom || measureFrom < 1 || !script.open(QIODevice::ReadOnly) ||
        !QDir().mkpath(output))
        qFatal("Invalid desktop session probe configuration");
    struct Probe {
            QJsonArray events;
            qsizetype next{};
            QElapsedTimer clock;
            QFile csv;
            QMap<quint64, double> received;
            double first{}, last{};
            unsigned measured{};
            bool stopping{};
    };
    auto probe = std::make_shared<Probe>();
    probe->events = QJsonDocument::fromJson(script.readAll()).array();
    if (probe->events.isEmpty())
        qFatal("Empty desktop session input script");
    probe->csv.setFileName(output + "/frames.csv");
    if (!probe->csv.open(QIODevice::WriteOnly))
        qFatal("Cannot open desktop frame measurements");
    probe->csv.write("sequence,received_seconds,presented_seconds,ack_ms,reported_fps\n");
    probe->clock.start();
    QObject::connect(&launcher, &EmulatorLauncher::frameReady, &application,
                     [probe, &launcher, output, measureFrom, limit](const QImage &image, quint64 sequence) {
                         if (!sequence)
                             return;
                         const auto now = double(probe->clock.nsecsElapsed()) / 1e9;
                         probe->received.insert(sequence, now);
                         while (probe->next < probe->events.size() && quint64(probe->events[probe->next].toObject()["frame"].toInt()) <= sequence) {
                             const auto event = probe->events[probe->next++].toObject();
                             const auto left = event["left_stick"].toArray(), right = event["right_stick"].toArray();
                             Core::Gfx::GamepadState pad;
                             pad.leftStick = {float(left[0].toDouble()), float(left[1].toDouble())};
                             pad.rightStick = {float(right[0].toDouble()), float(right[1].toDouble())};
                             launcher.setScriptedInput(event["buttons"].toInt(), pad);
                         }
                         if (sequence == 2600 || sequence == 2700 || sequence == 2800 || (sequence >= 3000 && sequence % 300 == 0))
                             image.save(output + QString("/frame_%1.ppm").arg(sequence, 5, 10, QChar('0')), "PPM");
                         if (sequence >= quint64(measureFrom) && sequence <= quint64(limit)) {
                             if (!probe->measured)
                                 probe->first = now;
                             probe->last = now;
                             ++probe->measured;
                         }
                         if (sequence % 100 == 0)
                             qInfo("[DESKTOP_PROBE] frame=%llu seconds=%.3f fps=%.2f", static_cast<unsigned long long>(sequence), now,
                                   launcher.presentFps());
                     });
    QObject::connect(&view, &GameView::framePresented, &application, [probe, &launcher, limit, output](quint64 sequence) {
        const auto now = double(probe->clock.nsecsElapsed()) / 1e9;
        if (auto it = probe->received.find(sequence); it != probe->received.end()) {
            QTextStream row(&probe->csv);
            row.setRealNumberPrecision(12);
            row << sequence << ',' << *it << ',' << now << ',' << (now - *it) * 1000 << ',' << launcher.presentFps() << '\n';
            probe->received.erase(it);
        }
        if (sequence >= quint64(limit) && !probe->stopping) {
            probe->stopping = true;
            probe->csv.flush();
            const QJsonObject summary{{"frames", qint64(sequence)},
                                      {"measured_frames", int(probe->measured)},
                                      {"fps", (probe->measured - 1) / (probe->last - probe->first)},
                                      {"core_log", launcher.logPath()},
                                      {"full_desktop", true}};
            QFile file(output + "/summary.json");
            if (file.open(QIODevice::WriteOnly))
                file.write(QJsonDocument(summary).toJson());
            qInfo() << "[DESKTOP_PROBE]" << summary;
            launcher.stop();
        }
    });
    QObject::connect(&launcher, &EmulatorLauncher::runningChanged, &application, [probe, &launcher, &application] {
        if (probe->stopping && !launcher.running())
            application.quit();
    });
    QObject::connect(&launcher, &EmulatorLauncher::errorChanged, &application, [&launcher, &application] {
        if (!launcher.error().isEmpty()) {
            qWarning() << "[DESKTOP_PROBE]" << launcher.error();
            launcher.stop();
            application.exit(2);
        }
    });
    QTimer::singleShot(400000, &application, [&launcher, &application] {
        launcher.stop();
        application.exit(2);
    });
    QTimer::singleShot(300, &application, [&launcher, rpx] {
        launcher.launch(rpx, "Smash desktop probe", QFileInfo(rpx).absoluteDir().absoluteFilePath("../content"));
        launcher.toggleFps();
    });
}
