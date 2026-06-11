#include <shmtu/cas_ocr/gui/launch_options.h>
#include <shmtu/cas_ocr/gui/logging.h>
#include <shmtu/cas_ocr/gui/main_window.h>
#include <shmtu/cas_ocr/version.h>

#include <curl/curl.h>

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QMessageBox>

#include <csignal>
#include <sstream>

int main(int argc, char* argv[]) {
    using shmtu::cas::ocr::gui::LaunchOptions;
    using shmtu::cas::ocr::gui::MainWindow;
    using shmtu::cas::ocr::gui::installCrashHandlers;
    using shmtu::cas::ocr::gui::logMessage;

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("SHMTU CAS OCR"));
    app.setApplicationVersion(QString::fromUtf8(SHMTU_CAS_OCR_GUI_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QString::fromUtf8("SHMTU CAS OCR GUI"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption model_dir_option(QStringList{QStringLiteral("model-dir")},
                                        QString::fromUtf8("initial model directory"),
                                        QStringLiteral("path"));
    QCommandLineOption precision_option(QStringList{QStringLiteral("precision")},
                                        QString::fromUtf8("initial model precision"),
                                        QStringLiteral("precision"));
    QCommandLineOption use_gpu_option(QStringList{QStringLiteral("use-gpu")},
                                      QString::fromUtf8("enable GPU acceleration by default"));
    QCommandLineOption model_version_option(QStringList{QStringLiteral("model-version")},
                                           QString::fromUtf8("Model version: v1 or v2 (default: v2)"),
                                           QStringLiteral("version"),
                                           QStringLiteral("v2"));
    QCommandLineOption v2_tag_option(QStringList{QStringLiteral("v2-tag")},
                                    QString::fromUtf8("V2 release tag, e.g. v2.0.5"),
                                    QStringLiteral("tag"));
    QCommandLineOption v2_backbone_option(QStringList{QStringLiteral("v2-backbone")},
                                         QString::fromUtf8("V2 backbone, e.g. mobilenet_v3_small"),
                                         QStringLiteral("backbone"));

    parser.addOption(model_dir_option);
    parser.addOption(precision_option);
    parser.addOption(use_gpu_option);
    parser.addOption(model_version_option);
    parser.addOption(v2_tag_option);
    parser.addOption(v2_backbone_option);
    parser.process(app);

    LaunchOptions launch_options;
    if (parser.isSet(model_dir_option)) {
        launch_options.model_dir = parser.value(model_dir_option).toStdString();
    }
    if (parser.isSet(precision_option)) {
        const auto precision = parser.value(precision_option);
        if (precision != QStringLiteral("fp16") && precision != QStringLiteral("fp32")) {
            QMessageBox::critical(nullptr, QString::fromUtf8("参数错误"),
                                  QString::fromUtf8("Unsupported precision '%1'. Use fp16 or fp32.")
                                      .arg(precision));
            return 1;
        }
        launch_options.precision = precision.toStdString();
    }
    launch_options.use_gpu = launch_options.use_gpu || parser.isSet(use_gpu_option);

    if (parser.isSet(model_version_option)) {
        const auto version = parser.value(model_version_option).toStdString();
        launch_options.model_version = shmtu::cas::ocr::model_version_from_string(version);
    }
    if (parser.isSet(v2_tag_option)) {
        launch_options.v2_tag = parser.value(v2_tag_option).toStdString();
    }
    if (parser.isSet(v2_backbone_option)) {
        launch_options.v2_backbone = parser.value(v2_backbone_option).toStdString();
    }

    installCrashHandlers();
    {
        std::ostringstream oss;
        oss << "main: GUI starting"
            << ", model_dir=" << launch_options.model_dir
            << ", precision=" << launch_options.precision
            << ", use_gpu=" << (launch_options.use_gpu ? "true" : "false")
            << ", model_version=" << shmtu::cas::ocr::model_version_to_string(launch_options.model_version)
            << ", v2_tag=" << (launch_options.v2_tag.empty() ? "(auto)" : launch_options.v2_tag)
            << ", v2_backbone=" << (launch_options.v2_backbone.empty() ? "(default)" : launch_options.v2_backbone);
        logMessage(oss.str());
    }

#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
#endif

    const auto curl_init_rc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (curl_init_rc != CURLE_OK) {
        QMessageBox::critical(
            nullptr,
            QString::fromUtf8("启动失败"),
            QString::fromUtf8("curl_global_init failed: %1")
                .arg(QString::fromUtf8(curl_easy_strerror(curl_init_rc))));
        return 1;
    }
    logMessage("main: curl_global_init succeeded");

    MainWindow window(launch_options);
    window.show();
    logMessage("main: main window shown");

    const int exit_code = app.exec();
    logMessage("main: cleaning up curl");
    curl_global_cleanup();
    return exit_code;
}
