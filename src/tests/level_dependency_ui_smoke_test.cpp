#include "app/level_dependency_dialog.h"
#include "app/studio_theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>

#include <iostream>

using namespace vibestudio;

namespace {

bool expect(bool value, const char* message)
{
	if (!value) { std::cerr << message << '\n'; }
	return value;
}

bool waitForScan(QDialog* dialog)
{
	QEventLoop loop;
	QTimer poll;
	QTimer timeout;
	timeout.setSingleShot(true);
	bool finished = false;
	QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
		if (!dialog->findChild<QLabel*>(QStringLiteral("dependencySummary"))->text().contains(QChar(0x2026))) {
			finished = true;
			loop.quit();
		}
	});
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	poll.start(10);
	timeout.start(10000);
	loop.exec();
	return finished;
}

} // namespace

int main(int argc, char** argv)
{
	// No OS input, window capture, clipboard changes, or game processes. This
	// test exercises widget state and paints the widget into its own QImage.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	if (qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR") && !qEnvironmentVariableIsEmpty("SystemRoot")) {
		qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
	}
#endif
	QApplication application(argc, argv);
#ifdef Q_OS_WIN
	application.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temp;
	if (!temp.isValid()) { return EXIT_FAILURE; }
	QFile file(QDir(temp.path()).filePath(QStringLiteral("present.wav")));
	if (!file.open(QIODevice::WriteOnly) || file.write("fixture") != 7) { return EXIT_FAILURE; }
	file.close();
	PackageArchive archive;
	QString error;
	if (!archive.load(temp.path(), &error)) { return EXIT_FAILURE; }
	LevelMapDocument document;
	document.format = LevelMapFormat::Quake3Map;
	document.mapName = QStringLiteral("Dependency fixture with expanded translated text");
	LevelMapEntity entity;
	entity.id = 1;
	entity.properties = {{QStringLiteral("noise"), QStringLiteral("present.wav"), 0}, {QStringLiteral("model"), QStringLiteral("models/missing.md3"), 0}};
	document.entities << entity;
	bool ok = true;
	for (int scale : {100, 200}) {
		applyStudioTheme(application, studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark, UiDensity::Standard, scale));
		auto* dialog = showLevelDependencyDialog(nullptr, document, archive, {});
		if (scale == 200) { dialog->setLayoutDirection(Qt::RightToLeft); }
		ok &= expect(waitForScan(dialog), "asynchronous dependency scan should finish");
		auto* tree = dialog->findChild<QTreeWidget*>(QStringLiteral("levelDependencies"));
		auto* filter = dialog->findChild<QLineEdit*>(QStringLiteral("dependencyFilter"));
		auto* problems = dialog->findChild<QCheckBox*>(QStringLiteral("dependencyProblemsOnly"));
		auto* exportButton = dialog->findChild<QPushButton*>(QStringLiteral("exportLevelAssets"));
		ok &= expect(tree && tree->topLevelItemCount() == 2, "dependency browser must expose both results");
		ok &= expect(!tree->accessibleName().isEmpty() && tree->focusPolicy() != Qt::NoFocus && filter->focusPolicy() != Qt::NoFocus, "results and filter must be named and keyboard-focusable");
		ok &= expect(!exportButton->isEnabled(), "missing assets must disable export");
		problems->setChecked(true);
		int visible = 0;
		for (int i = 0; i < tree->topLevelItemCount(); ++i) { visible += !tree->topLevelItem(i)->isHidden(); }
		ok &= expect(visible == 1, "problem filter must hide resolved results");
		problems->setChecked(false);
		filter->setText(QStringLiteral("entity:1"));
		ok &= expect(!tree->topLevelItem(0)->isHidden() && !tree->topLevelItem(1)->isHidden(), "object selectors must be searchable");
		filter->setText(QStringLiteral("missing"));
		tree->setCurrentItem(tree->topLevelItem(0));
		ok &= expect(dialog->findChild<QPlainTextEdit*>()->toPlainText().contains(QStringLiteral("entity:1")), "detail panel must expose map attribution");
		filter->clear();
		const QString captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
		if (!captures.isEmpty()) {
			QImage image(dialog->size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			dialog->render(&image);
			ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("dependencies-%1.png").arg(scale))), "save render-target evidence");
		}
		delete dialog;
	}
	// A caller can close the inspector immediately while the worker owns its
	// snapshot. Deferred completion must never address the destroyed dialog.
	delete showLevelDependencyDialog(nullptr, document, archive, {});
	QEventLoop finish;
	QTimer::singleShot(100, &finish, &QEventLoop::quit);
	finish.exec();
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
