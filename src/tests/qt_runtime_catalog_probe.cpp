#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QTranslator>

#include <iostream>

// Runtime package acceptance helper. Reads the selected package and SDK catalogs
// through Qt itself; does not open windows, access devices or inject user input.
int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	if (argc != 4) {
		std::cerr << "Usage: qt_runtime_catalog_probe <app-catalogs> <qt-catalogs> <sdk-qt-catalogs>\n";
		return 2;
	}
	const QDir application(QString::fromLocal8Bit(argv[1]));
	const QDir runtime(QString::fromLocal8Bit(argv[2]));
	const QDir sdk(QString::fromLocal8Bit(argv[3]));
	QJsonArray appCatalogs, qtCatalogs;
	bool valid = true;
	for (const auto& name : application.entryList({QStringLiteral("vibestudio_*.qm")}, QDir::Files)) {
		QTranslator reader;
		const bool loaded = reader.load(application.filePath(name));
		valid &= loaded;
		appCatalogs.append(QJsonObject{{"name", name}, {"loaded", loaded}});
	}
	int translatedButtons = 0;
	for (const auto& name : runtime.entryList({QStringLiteral("qtbase_*.qm")}, QDir::Files)) {
		const QString locale = name.mid(7, name.size() - 10);
		QTranslator merged, source;
		// Same locale/prefix lookup contract used by app/studio_runtime.cpp.
		const bool loaded = merged.load(QLocale(locale), QStringLiteral("qtbase"), QStringLiteral("_"), runtime.path());
		const bool originalLoaded = source.load(sdk.filePath(name));
		bool same = loaded && originalLoaded;
		for (const char* button : {"Cancel", "Open", "Save", "Close", "Yes", "No"}) {
			same &= merged.translate("QPlatformTheme", button) == source.translate("QPlatformTheme", button);
		}
		const QString cancel = merged.translate("QPlatformTheme", "Cancel");
		if (!cancel.isEmpty() && cancel != QStringLiteral("Cancel")) { ++translatedButtons; }
		valid &= same;
		qtCatalogs.append(QJsonObject{{"name", name}, {"loaded", loaded}, {"matchesSdkButtons", same}, {"cancel", cancel}});
	}
	QTranslator english;
	valid &= english.load(application.filePath(QStringLiteral("vibestudio_en.qm"))) && app.installTranslator(&english);
	const QString singular = QCoreApplication::translate("vibestudio::AudioAnalysisDialog",
	    "Peak %1 dBFS · RMS %2 dBFS · %n sample(s) above full scale", nullptr, 1);
	const QString plural = QCoreApplication::translate("vibestudio::AudioAnalysisDialog",
	    "Peak %1 dBFS · RMS %2 dBFS · %n sample(s) above full scale", nullptr, 5);
	valid &= appCatalogs.size() == 21 && !qtCatalogs.isEmpty() && translatedButtons > 0 &&
	         singular.contains(QStringLiteral("1 sample above")) && plural.contains(QStringLiteral("5 samples above"));
	std::cout << QJsonDocument(QJsonObject{{"qtVersion", qVersion()}, {"applicationCatalogs", appCatalogs},
	    {"qtCatalogs", qtCatalogs}, {"translatedCancelButtons", translatedButtons},
	    {"audioSingular", singular}, {"audioPlural", plural}, {"passed", valid}}).toJson().constData();
	return valid ? 0 : 1;
}
