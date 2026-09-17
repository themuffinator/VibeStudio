#pragma once

// Application runtime services shared by the GUI entry point and the shell:
// high-DPI setup, runtime translation loading, layout direction, and session
// logging.
//
// These live in src/app rather than src/core because they install process-wide
// Qt application state (translators, message handlers, layout direction) which
// only an application entry point should own.

#include <QIcon>
#include <QString>
#include <QStringList>

class QCoreApplication;

namespace vibestudio {

struct TranslationLoadResult {
	bool installed = false;
	QString requestedLocale;
	QString resolvedLocale;
	QString catalogPath;
	bool rightToLeft = false;
	QStringList searchedPaths;
	QStringList warnings;
};

// Applies the high-DPI and rendering attributes the studio's painted widgets
// depend on. Must be called before the QApplication is constructed where Qt
// requires it, and is safe to call once only.
void configureHighDpiBehavior();

// Resolves and installs the .qm catalog for `localeName`, replacing any
// previously installed VibeStudio translator. An empty or unknown locale falls
// back to the source language without reporting failure.
TranslationLoadResult installStudioTranslations(QCoreApplication& app, const QString& localeName);

// Applies the layout direction implied by a locale. Returns true when the
// direction changed.
bool applyLayoutDirectionForLocale(const QString& localeName);

// Directories searched for compiled translation catalogs, in order.
QStringList translationSearchPaths();

// The application/window icon, painted at several sizes rather than shipped as
// a binary asset so the repository stays free of opaque image blobs.
QIcon studioApplicationIcon();

// Installs a Qt message handler that mirrors warnings and above into a
// rotating session log under the user's application data directory, so a crash
// or a failed compile leaves a diagnostic trail. Returns the log file path.
QString installSessionLogging();
QString sessionLogFilePath();
QStringList recentSessionLogLines(int maxLines = 200);

} // namespace vibestudio
