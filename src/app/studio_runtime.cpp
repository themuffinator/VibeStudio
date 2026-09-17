#include "app/studio_runtime.h"

#include "core/localization.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QLinearGradient>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QStandardPaths>
#include <QTextStream>
#include <QTranslator>

#include <algorithm>
#include <cstdio>
#include <utility>

namespace vibestudio {

namespace {

QString runtimeText(const char* source)
{
	return QCoreApplication::translate("VibeStudioRuntime", source);
}

QTranslator*& studioTranslator()
{
	static QTranslator* translator = nullptr;
	return translator;
}

QTranslator*& qtBaseTranslator()
{
	static QTranslator* translator = nullptr;
	return translator;
}

struct SessionLogState {
	QMutex mutex;
	QString path;
	QStringList recent;
	QtMessageHandler previousHandler = nullptr;
	bool installed = false;
	int maxRecent = 400;
};

SessionLogState& sessionLogState()
{
	static SessionLogState state;
	return state;
}

QString messageTypeToken(QtMsgType type)
{
	switch (type) {
	case QtDebugMsg:
		return QStringLiteral("debug");
	case QtInfoMsg:
		return QStringLiteral("info");
	case QtWarningMsg:
		return QStringLiteral("warning");
	case QtCriticalMsg:
		return QStringLiteral("critical");
	case QtFatalMsg:
		return QStringLiteral("fatal");
	}
	return QStringLiteral("message");
}

void sessionMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
	SessionLogState& state = sessionLogState();

	const QString line = QStringLiteral("%1 [%2] %3")
		.arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate), messageTypeToken(type), message);

	{
		const QMutexLocker locker(&state.mutex);
		state.recent.push_back(line);
		while (state.recent.size() > state.maxRecent) {
			state.recent.removeFirst();
		}
		if (!state.path.isEmpty() && type != QtDebugMsg) {
			QFile file(state.path);
			if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
				QTextStream stream(&file);
				stream.setEncoding(QStringConverter::Utf8);
				stream << line << '\n';
			}
		}
	}

	if (state.previousHandler) {
		state.previousHandler(type, context, message);
	} else if (type != QtDebugMsg) {
		std::fputs(qPrintable(line + QLatin1Char('\n')), stderr);
	}
}

void appendUniqueExistingDirectory(QStringList* paths, const QString& candidate)
{
	if (!paths || candidate.isEmpty()) {
		return;
	}
	const QString normalized = QDir::cleanPath(candidate);
	if (normalized.isEmpty() || paths->contains(normalized)) {
		return;
	}
	paths->push_back(normalized);
}

QStringList qmCandidateFileNames(const QString& localeName)
{
	QStringList names;
	const QString normalized = normalizedLocalizationTargetId(localeName);
	const QString requested = localeName.trimmed();

	auto addName = [&names](const QString& locale) {
		if (locale.isEmpty()) {
			return;
		}
		const QString fileName = QStringLiteral("vibestudio_%1.qm").arg(locale);
		if (!names.contains(fileName)) {
			names.push_back(fileName);
		}
	};

	addName(requested);
	addName(normalized);

	// A regional locale falls back to its base language: pt_BR -> pt.
	for (const QString& locale : {requested, normalized}) {
		const qsizetype separator = locale.indexOf(QLatin1Char('_'));
		if (separator > 0) {
			addName(locale.left(separator));
		}
	}

	return names;
}

} // namespace

void configureHighDpiBehavior()
{
	// Qt 6 always enables high-DPI scaling; what still matters for painted
	// surfaces is the rounding policy. PassThrough keeps fractional scale
	// factors (150%, 175%) exact so QPainter geometry, hit-testing, and
	// nearest-neighbour texture previews stay aligned with what is drawn.
	QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
}

QStringList translationSearchPaths()
{
	QStringList paths;

	const QByteArray override = qgetenv("VIBESTUDIO_I18N_DIR");
	if (!override.isEmpty()) {
		appendUniqueExistingDirectory(&paths, QString::fromLocal8Bit(override));
	}

	const QString applicationDir = QCoreApplication::applicationDirPath();
	if (!applicationDir.isEmpty()) {
		appendUniqueExistingDirectory(&paths, applicationDir + QStringLiteral("/i18n"));
		// Development layout: builddir/src/vibestudio -> builddir/i18n.
		appendUniqueExistingDirectory(&paths, applicationDir + QStringLiteral("/../i18n"));
		// Installed layout: prefix/bin/vibestudio -> prefix/share/vibestudio/i18n.
		appendUniqueExistingDirectory(&paths, applicationDir + QStringLiteral("/../share/vibestudio/i18n"));
		// Portable package layout.
		appendUniqueExistingDirectory(&paths, applicationDir + QStringLiteral("/../../i18n"));
	}

	appendUniqueExistingDirectory(&paths, QDir::currentPath() + QStringLiteral("/i18n"));

	return paths;
}

TranslationLoadResult installStudioTranslations(QCoreApplication& app, const QString& localeName)
{
	TranslationLoadResult result;
	result.requestedLocale = localeName.trimmed();
	result.resolvedLocale = normalizedLocalizationTargetId(result.requestedLocale);
	result.rightToLeft = isRightToLeftLocale(result.resolvedLocale);
	result.searchedPaths = translationSearchPaths();

	QTranslator*& translator = studioTranslator();
	if (translator) {
		app.removeTranslator(translator);
		delete translator;
		translator = nullptr;
	}

	QTranslator*& baseTranslator = qtBaseTranslator();
	if (baseTranslator) {
		app.removeTranslator(baseTranslator);
		delete baseTranslator;
		baseTranslator = nullptr;
	}

	// The source language needs no catalog; installing one would only add a
	// lookup miss for every string.
	const bool sourceLanguage = result.resolvedLocale.isEmpty() || result.resolvedLocale == QStringLiteral("en");

	if (!sourceLanguage) {
		const QStringList fileNames = qmCandidateFileNames(result.requestedLocale);
		auto* candidate = new QTranslator(&app);
		bool loaded = false;
		for (const QString& directory : std::as_const(result.searchedPaths)) {
			for (const QString& fileName : std::as_const(fileNames)) {
				const QString path = QDir(directory).filePath(fileName);
				if (!QFileInfo::exists(path)) {
					continue;
				}
				if (candidate->load(path)) {
					result.catalogPath = QDir::cleanPath(path);
					loaded = true;
					break;
				}
				result.warnings.push_back(
					runtimeText("Translation catalog could not be loaded: %1").arg(QDir::cleanPath(path)));
			}
			if (loaded) {
				break;
			}
		}

		if (loaded && app.installTranslator(candidate)) {
			translator = candidate;
			result.installed = true;
		} else {
			delete candidate;
			if (!loaded) {
				result.warnings.push_back(
					runtimeText("No compiled translation catalog (.qm) was found for %1; using the source language.")
						.arg(result.resolvedLocale));
			}
		}

		// Qt's own dialogs and standard buttons have their own catalogs.
		auto* qtCandidate = new QTranslator(&app);
		const QString qtCatalogDir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
		if (!qtCatalogDir.isEmpty()
			&& qtCandidate->load(QLocale(result.resolvedLocale), QStringLiteral("qtbase"), QStringLiteral("_"), qtCatalogDir)
			&& app.installTranslator(qtCandidate)) {
			baseTranslator = qtCandidate;
		} else {
			delete qtCandidate;
		}
	}

	return result;
}

bool applyLayoutDirectionForLocale(const QString& localeName)
{
	const bool rightToLeft = isRightToLeftLocale(normalizedLocalizationTargetId(localeName));
	const Qt::LayoutDirection desired = rightToLeft ? Qt::RightToLeft : Qt::LeftToRight;
	if (QGuiApplication::layoutDirection() == desired) {
		return false;
	}
	QGuiApplication::setLayoutDirection(desired);
	return true;
}

QIcon studioApplicationIcon()
{
	static QIcon icon = []() {
		QIcon built;
		for (const int size : {16, 24, 32, 48, 64, 128, 256}) {
			QPixmap pixmap(size, size);
			pixmap.fill(Qt::transparent);

			QPainter painter(&pixmap);
			painter.setRenderHint(QPainter::Antialiasing, true);

			const qreal inset = size * 0.06;
			const QRectF plate(inset, inset, size - inset * 2.0, size - inset * 2.0);
			const qreal radius = size * 0.22;

			QLinearGradient gradient(plate.topLeft(), plate.bottomRight());
			gradient.setColorAt(0.0, QColor(0x27, 0x5d, 0x86));
			gradient.setColorAt(1.0, QColor(0x14, 0x1a, 0x21));
			painter.setPen(Qt::NoPen);
			painter.setBrush(gradient);
			painter.drawRoundedRect(plate, radius, radius);

			// A chevron reading as both a "V" and a build-stage arrow.
			QPen stroke(QColor(0xe8, 0xed, 0xf2));
			stroke.setWidthF(std::max(1.0, size * 0.11));
			stroke.setCapStyle(Qt::RoundCap);
			stroke.setJoinStyle(Qt::MiterJoin);
			painter.setPen(stroke);
			painter.setBrush(Qt::NoBrush);

			QPolygonF chevron;
			chevron << QPointF(plate.left() + plate.width() * 0.26, plate.top() + plate.height() * 0.30)
				<< QPointF(plate.center().x(), plate.top() + plate.height() * 0.72)
				<< QPointF(plate.left() + plate.width() * 0.74, plate.top() + plate.height() * 0.30);
			painter.drawPolyline(chevron);

			painter.end();
			built.addPixmap(pixmap);
		}
		return built;
	}();
	return icon;
}

QString installSessionLogging()
{
	SessionLogState& state = sessionLogState();
	{
		const QMutexLocker locker(&state.mutex);
		if (state.installed) {
			return state.path;
		}

		const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
		if (!root.isEmpty()) {
			QDir directory(root);
			if (directory.mkpath(QStringLiteral("logs"))) {
				const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd"));
				state.path = directory.filePath(QStringLiteral("logs/vibestudio-%1.log").arg(stamp));
			}
		}
		state.installed = true;
	}

	state.previousHandler = qInstallMessageHandler(sessionMessageHandler);
	return state.path;
}

QString sessionLogFilePath()
{
	SessionLogState& state = sessionLogState();
	const QMutexLocker locker(&state.mutex);
	return state.path;
}

QStringList recentSessionLogLines(int maxLines)
{
	SessionLogState& state = sessionLogState();
	const QMutexLocker locker(&state.mutex);
	if (maxLines <= 0 || state.recent.size() <= maxLines) {
		return state.recent;
	}
	return state.recent.mid(state.recent.size() - maxLines);
}

} // namespace vibestudio
