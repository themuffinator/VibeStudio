// Package and Release as a widget: the dialog plans a map release against a
// generated Quake III installation and project, shows what ships and what the
// game provides, records a changelog entry, switches scope, publishes, offers
// the next version, and asks to index a game whose assets are unknown. Every
// control a keyboard reaches has an accessible name. RELEASE_TEST_LOOK,
// RELEASE_TEST_SCALE and RELEASE_TEST_RTL run the same checks in other looks;
// VIBESTUDIO_TEST_SNAPSHOTS=<dir> saves pictures of the states.

#include "app/release_dialog.h"
#include "app/studio_theme.h"

#include "core/game_asset_register.h"
#include "core/release_notes.h"
#include "core/studio_settings.h"
#include "tests/release_test_fixtures.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeWidget>

#include <algorithm>
#include <iostream>

using namespace vibestudio;
namespace fixtures = vibestudio::tests::release;

namespace {

int failures = 0;

bool expect(bool condition, const std::string& message, const QString& detail = QString())
{
	if (!condition) {
		std::cerr << "FAIL: " << message;
		if (!detail.isEmpty()) {
			std::cerr << "\n  " << detail.toStdString();
		}
		std::cerr << '\n';
		++failures;
	}
	return condition;
}

void snapshot(QWidget* widget, const QString& name)
{
	const QString folder = qEnvironmentVariable("VIBESTUDIO_TEST_SNAPSHOTS");
	if (folder.isEmpty()) {
		return;
	}
	QDir().mkpath(folder);
	// Let the layout catch up with the last review first: offscreen, the
	// first grab can settle pending layouts after painting the old ones.
	QTest::qWait(100);
	widget->grab();
	QTest::qWait(20);
	widget->grab().save(QDir(folder).filePath(name + QStringLiteral(".png")));
}

bool waitIdle(ReleaseDialog* dialog, int timeout = 60000)
{
	// The first review starts from the event loop; later ones are queued.
	QTest::qWait(50);
	return QTest::qWaitFor([dialog]() { return !dialog->busy(); }, timeout);
}

// Closed before returning: an open file would block the release's atomic
// changelog update on Windows.
QString readText(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

bool anyEndsWith(const QStringList& values, const QString& end)
{
	return std::any_of(values.cbegin(), values.cend(), [&end](const QString& value) { return value.endsWith(end); });
}

QStringList column(QTreeWidget* tree, int column = 0)
{
	QStringList values;
	for (int i = 0; tree && i < tree->topLevelItemCount(); ++i) {
		values << tree->topLevelItem(i)->text(column);
	}
	return values;
}

void tick(QListWidget* list, const QString& pathEnd, bool on)
{
	for (int i = 0; list && i < list->count(); ++i) {
		QListWidgetItem* item = list->item(i);
		if (item->data(Qt::UserRole).toString().endsWith(pathEnd)) {
			item->setCheckState(on ? Qt::Checked : Qt::Unchecked);
		}
	}
}

} // namespace

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	const QString look = qEnvironmentVariable("RELEASE_TEST_LOOK");
	const StudioTheme theme = look == QStringLiteral("high-contrast-light") ? StudioTheme::HighContrastLight
		: look == QStringLiteral("high-contrast-dark")						  ? StudioTheme::HighContrastDark
		: look == QStringLiteral("light")									  ? StudioTheme::Light
																			  : StudioTheme::Dark;
	const int scale = qEnvironmentVariableIsSet("RELEASE_TEST_SCALE") ? qEnvironmentVariableIntValue("RELEASE_TEST_SCALE") : 100;
	applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, std::clamp(scale, 100, 200)));
	if (qEnvironmentVariableIntValue("RELEASE_TEST_RTL") == 1) {
		QApplication::setLayoutDirection(Qt::RightToLeft);
	}
	QTemporaryDir temp;
	StudioSettings::setOverrideFilePath(QDir(temp.path()).filePath(QStringLiteral("settings.ini")));
	const QString game = QDir(temp.path()).filePath(QStringLiteral("q3"));
	const QString project = QDir(temp.path()).filePath(QStringLiteral("mymod"));
	expect(fixtures::makeQuake3Installation(game) && fixtures::makeQuake3Project(project), "the fixtures are written");
	const GameInstallationProfile installation = fixtures::profile(QStringLiteral("q3-ui"), QStringLiteral("quake3"), game);
	const GameAssetRegisterBuildResult built = buildGameAssetRegister({installation, {}, {}});
	QString error;
	expect(built.succeeded && saveGameAssetRegister(built.registerData, gameAssetRegisterPath(installation.id), &error), "the game is indexed", error + built.error);

	int saves = 0;
	int published = 0;
	int indexed = 0;
	QString revealed;
	QString lastSavedVersion;
	ReleaseDialogHooks hooks;
	hooks.project = [&project]() {
		ProjectManifest manifest;
		loadProjectManifest(project, &manifest);
		return manifest;
	};
	hooks.installation = [&installation](GameInstallationProfile* out) {
		*out = installation;
		return true;
	};
	hooks.saveReleaseSettings = [&](const ProjectReleaseSettings& release, const QString& gameKey, QString* failure) {
		ProjectManifest manifest;
		loadProjectManifest(project, &manifest);
		manifest.gameKey = gameKey;
		manifest.release.version = release.version;
		lastSavedVersion = release.version;
		++saves;
		return saveProjectManifest(manifest, failure);
	};
	hooks.indexInstallation = [&](const QString& id, std::function<void(bool)> done) {
		++indexed;
		const GameAssetRegisterBuildResult again = buildGameAssetRegister({installation, {}, {}});
		done(again.succeeded && saveGameAssetRegister(again.registerData, gameAssetRegisterPath(id)));
	};
	hooks.reveal = [&revealed](const QString& path) { revealed = path; };
	hooks.published = [&published](const ReleasePublishResult&) { ++published; };

	// The offscreen screen is 800 by 600; size the dialog as a desktop at this
	// text scale would.
	const QSize desktop = QSize(1180, 780) * (std::clamp(scale, 100, 200) / 100.0);
	auto* dialog = new ReleaseDialog(nullptr, hooks);
	dialog->resize(desktop);
	dialog->setSelection(ReleaseScope::Maps, {QDir(project).filePath(QStringLiteral("maps/arena1.map"))});
	dialog->show();
	expect(waitIdle(dialog), "the first review finishes");
	auto* included = dialog->findChild<QTreeWidget*>(QStringLiteral("releaseIncluded"));
	auto* provided = dialog->findChild<QTreeWidget*>(QStringLiteral("releaseProvided"));
	auto* problems = dialog->findChild<QTreeWidget*>(QStringLiteral("releaseProblems"));
	auto* publish = dialog->findChild<QPushButton*>(QStringLiteral("releasePublish"));
	auto* notes = dialog->findChild<QPlainTextEdit*>(QStringLiteral("releaseNotes"));
	auto* readme = dialog->findChild<QPlainTextEdit*>(QStringLiteral("releaseReadme"));
	auto* items = dialog->findChild<QListWidget*>(QStringLiteral("releaseItems"));
	auto* scope = dialog->findChild<QComboBox*>(QStringLiteral("releaseScope"));
	auto* version = dialog->findChild<QLineEdit*>(QStringLiteral("releaseVersion"));
	auto* overrideChip = dialog->findChild<QToolButton*>(QStringLiteral("releaseOverrideChip"));
	auto* notice = dialog->findChild<QWidget*>(QStringLiteral("releaseNotice"));
	expect(included && provided && problems && publish && notes && readme && items && scope && version && overrideChip && notice, "the dialog's parts are found");
	if (failures > 0) {
		return 1;
	}
	expect(dialog->plan().canPublish() && included->topLevelItemCount() == 10, "the map review lists the ten files that ship", column(included).join(QLatin1Char(' ')));
	expect(anyEndsWith(column(provided), QStringLiteral("base_wall/glass")) && column(provided, 2).contains(QStringLiteral("models/mapobjects/barrel.md3")),
		"stock shaders and models are listed as provided by the game", column(provided).join(QLatin1Char(' ')));
	expect(overrideChip->text().contains(QStringLiteral("1")), "the override chip counts the replaced file", overrideChip->text());
	expect(publish->isEnabled(), "Publish is offered for a ready release");
	expect(notes->toPlainText().contains(QStringLiteral("# The Pit 1.0.0")) && readme->toPlainText().contains(QStringLiteral("Title")),
		"the notes and readme are generated", notes->toPlainText().left(200));
	expect(!notice->isVisible(), "an indexed game raises no notice");
	expect(items->count() == 1 && items->item(0)->checkState() == Qt::Checked, "the chosen map is ticked");
	snapshot(dialog, QStringLiteral("release-review"));

	// Recording a change regenerates the notes.
	auto* changeText = dialog->findChild<QLineEdit*>(QStringLiteral("releaseChangeText"));
	auto* addChange = dialog->findChild<QPushButton*>(QStringLiteral("releaseAddChange"));
	auto* tabs = dialog->findChild<QTabWidget*>(QStringLiteral("releaseTabs"));
	tabs->setCurrentIndex(3);
	changeText->setText(QStringLiteral("A ramp to the upper deck."));
	addChange->click();
	QTest::qWait(50);
	expect(readText(QDir(project).filePath(QStringLiteral("CHANGELOG.md"))).contains(QStringLiteral("A ramp to the upper deck.")), "the change is written to the changelog");
	expect(notes->toPlainText().contains(QStringLiteral("A ramp to the upper deck.")), "the notes include the recorded change");
	snapshot(dialog, QStringLiteral("release-notes"));

	// Another scope plans other items.
	scope->setCurrentIndex(scope->findData(QStringLiteral("textures")));
	tick(items, QStringLiteral("textures/mymod"), true);
	expect(waitIdle(dialog), "the texture review finishes");
	expect(column(included).contains(QStringLiteral("scripts/mymod.shader")) && column(included).contains(QStringLiteral("textures/mymod/unused.tga")),
		"a texture release ships the folder and its shaders", column(included).join(QLatin1Char(' ')));
	scope->setCurrentIndex(scope->findData(QStringLiteral("maps")));
	expect(items->count() == 1 && items->item(0)->checkState() == Qt::Checked, "the map is still ticked after a visit to another scope");
	expect(waitIdle(dialog) && included->topLevelItemCount() == 10, "back to the map", column(included).join(QLatin1Char(' ')));

	// Publish, then the next version is offered.
	const QString output = QDir(temp.path()).filePath(QStringLiteral("out"));
	dialog->setOutputDirectory(output);
	expect(publish->isEnabled(), "Publish is offered with an output folder");
	publish->click();
	expect(QTest::qWaitFor([dialog]() { return !dialog->busy(); }, 60000), "publishing finishes");
	const ReleasePublishResult& result = dialog->lastResult();
	expect(result.succeeded && QFileInfo::exists(result.packagePath) && QFileInfo::exists(result.archivePath), "the release is written", result.error);
	expect(saves == 1 && lastSavedVersion == QStringLiteral("1.0.0") && published == 1, "the settings are saved and the shell told");
	expect(QFileInfo::exists(releaseRecordPath(project, QStringLiteral("1.0.0"))), "the release is recorded");
	expect(!publish->isEnabled(), "a released version cannot be published again as it is");
	expect(publish->toolTip().contains(QStringLiteral("already been released")) && publish->accessibleDescription() == publish->toolTip(),
		"the disabled Publish button says why, to the eye and to screen readers", publish->toolTip());
	auto* unreleased = dialog->findChild<QListWidget*>(QStringLiteral("releaseUnreleased"));
	expect(unreleased && unreleased->count() == 1 && !(unreleased->item(0)->flags() & Qt::ItemIsEnabled)
			&& readText(result.changelogPath).contains(QStringLiteral("## [1.0.0]")),
		"the recorded change moves under the released version", result.warnings.join(QLatin1Char(' ')));
	snapshot(dialog, QStringLiteral("release-published"));
	auto* reveal = dialog->findChild<QPushButton*>(QStringLiteral("releaseReveal"));
	reveal->click();
	expect(revealed == result.outputDirectory, "Show Folder reveals the release folder");
	version->setText(bumpReleaseVersion(QStringLiteral("1.0.0"), QStringLiteral("patch")));
	emit version->textEdited(version->text());
	expect(waitIdle(dialog) && publish->isEnabled(), "the next version can publish");

	// Every control a keyboard reaches says what it is.
	QStringList unnamed;
	for (QWidget* widget : dialog->findChildren<QWidget*>()) {
		if (!widget->isVisible() || widget->focusPolicy() == Qt::NoFocus || qobject_cast<QTabBar*>(widget)) {
			continue;
		}
		const auto* button = qobject_cast<QAbstractButton*>(widget);
		if (widget->accessibleName().isEmpty() && !(button && !button->text().isEmpty()) && !widget->inherits("QScrollBar")
			&& widget->objectName() != QStringLiteral("qt_scrollarea_viewport")) {
			unnamed << widget->metaObject()->className() + (widget->objectName().isEmpty() ? QString() : QLatin1Char('#') + widget->objectName());
		}
	}
	expect(unnamed.isEmpty(), "focusable controls have accessible names", unnamed.join(QLatin1Char(' ')));
	delete dialog;

	// A game without an index asks to be indexed, and plans again once it is.
	QFile::remove(gameAssetRegisterPath(installation.id));
	auto* second = new ReleaseDialog(nullptr, hooks);
	second->resize(desktop);
	second->setSelection(ReleaseScope::Maps, {QDir(project).filePath(QStringLiteral("maps/arena1.map"))});
	second->show();
	expect(waitIdle(second), "the unindexed review finishes");
	auto* secondNotice = second->findChild<QWidget*>(QStringLiteral("releaseNotice"));
	auto* index = second->findChild<QPushButton*>(QStringLiteral("releaseIndexGame"));
	expect(secondNotice && secondNotice->isVisible() && index, "an unindexed game raises the index notice");
	expect(!second->plan().stockChecked, "without the index the stock files are not checked");
	snapshot(second, QStringLiteral("release-index-notice"));
	if (index) {
		index->click();
		expect(waitIdle(second) && indexed == 1 && second->plan().stockChecked && !secondNotice->isVisible(), "indexing from the notice reviews again against the game");
	}
	delete second;

	if (failures > 0) {
		std::cerr << failures << " failure(s)\n";
		return 1;
	}
	std::cout << "release dialog UI smoke passed\n";
	return 0;
}
