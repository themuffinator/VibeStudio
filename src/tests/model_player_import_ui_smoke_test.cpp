#include "app/model_assembly_dialog.h"
#include "app/studio_theme.h"
#include "core/model_player_import.h"
#include "tests/model_player_bundle_test_helpers.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRawFont>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTranslator>

#include <functional>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0;
bool expect(bool value, const char *message)
{
	++checks;
	if (!value)
		std::cerr << "FAIL: " << message << '\n';
	return value;
}
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override
	{
		return false;
	}
	QString translate(const char *context, const char *source, const char *, int) const override
	{
		const QByteArray name(context);
		if (name != "ModelAssemblyPlayerImport" && name != "VibeStudioModelPlayerImport" && name != "ModelQ3AnimationDialog")
			return {};
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 2, '~'));
	}
};
// Runs `act` once on the first modal dialog that `match` accepts. Polling,
// because a worker's event loop runs before the picker opens.
void whenModal(const std::function<bool(QDialog *)> &match, const std::function<void(QDialog *)> &act)
{
	auto *timer = new QTimer;
	auto tries = std::make_shared<int>(0);
	QObject::connect(timer, &QTimer::timeout, [=] {
		auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
		if ((dialog && match(dialog)) || ++*tries > 1000)
		{
			timer->stop();
			timer->deleteLater();
			if (dialog && match(dialog))
				act(dialog);
		}
	});
	timer->start(10);
}
bool named(QDialog *dialog, const char *name)
{
	return dialog->objectName() == QLatin1String(name);
}
// A key press where a keyboard user's goes: to the focused widget.
void press(Qt::Key key)
{
	if (auto *target = QApplication::focusWidget())
	{
		QKeyEvent down(QEvent::KeyPress, key, Qt::NoModifier), up(QEvent::KeyRelease, key, Qt::NoModifier);
		QApplication::sendEvent(target, &down);
		QApplication::sendEvent(target, &up);
	}
	QApplication::processEvents();
}
bool until(const std::function<bool()> &condition)
{
	QElapsedTimer timer;
	timer.start();
	while (!condition() && timer.elapsed() < 5000)
	{
		QApplication::processEvents(QEventLoop::AllEvents, 10);
		QThread::msleep(5);
	}
	return condition();
}
QByteArray folderHash(const QString &root)
{
	QCryptographicHash hash(QCryptographicHash::Sha256);
	QStringList files;
	for (QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories); it.hasNext();)
		files << it.next();
	files.sort();
	for (const auto &file : files)
		hash.addData(file.toUtf8() + q3Read(file));
	return hash.result();
}
} // namespace
int main(int argc, char **argv)
{
	std::cout << std::unitbuf;
	std::cerr << std::unitbuf;
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont("Segoe UI", 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return 1;
	QTemporaryDir temporary(QDir(root).filePath("player-import-ui-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(temporary.filePath("settings.ini"));
	QString error;
	PlayerBundleFixture fixture;
	bool ok = expect(fixture.create(temporary.path(), &error), "create the original player fixture");
	ok &= expect(QRawFont::fromFont(app.font()).supportsCharacter('A'), "widget renders have a usable font");
	// A folder package: the published player with red and blue skins, and a
	// second player that has only lower.md3 and an animation.cfg for males.
	auto options = fixture.options;
	options.variants = {ModelPlayerSkinVariant{"red", {std::nullopt, ModelPlayerReference{"team_red_upper.skin"}, std::nullopt}, std::nullopt},
						ModelPlayerSkinVariant{"blue", {std::nullopt, std::nullopt, ModelPlayerReference{"team_blue_head.skin"}}, std::nullopt}};
	ModelPlayerBundle bundle;
	ok &= expect(prepareModelPlayerBundle(fixture.assembly, fixture.resolved, fixture.context, options, fixture.sourcePath, &bundle, &error),
				 "prepare the player package");
	const auto package = temporary.filePath("package");
	QByteArray config;
	for (const auto &file : bundle.files)
	{
		ok &= expect(QDir().mkpath(QFileInfo(package + '/' + file.path).absolutePath()) && q3Write(package + '/' + file.path, file.bytes),
					 "write the package folder");
		if (file.role == "animation")
			config = file.bytes;
	}
	auto male = config;
	male.replace("sex f", "sex m");
	ok &= expect(QDir().mkpath(package + "/models/players/broken") &&
					 q3Write(package + "/models/players/broken/lower.md3", bundle.files.first().bytes) &&
					 q3Write(package + "/models/players/broken/animation.cfg", male),
				 "write an incomplete second player");
	auto archive = std::make_shared<PackageArchive>();
	ok &= expect(archive->load(package, &error), "open the package folder");
	if (!ok)
	{
		std::cerr << qPrintable(error) << '\n';
		return 1;
	}
	const auto before = folderHash(package);
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		std::cout << "Player import UI scenario " << scenario << '\n';
		Expansion expansion;
		if (scenario)
			app.installTranslator(&expansion);
		applyStudioTheme(app, studioThemeTokens(scenario == 0	? StudioTheme::Dark
												: scenario == 1 ? StudioTheme::HighContrastLight
																: StudioTheme::HighContrastDark,
												UiDensity::Standard, scenario ? 200 : 100));
		app.setLayoutDirection(scenario == 1 ? Qt::RightToLeft : Qt::LeftToRight);
		ModelAssemblyDialog editor;
		editor.setAttribute(Qt::WA_DeleteOnClose, false);
		editor.setAccessibility(scenario != 0, true);
		editor.resize(1180, 760);
		editor.show();
		app.processEvents();
		editor.findChild<QCheckBox *>("assemblyRecoveryEnabled")->setChecked(false);
		auto *action = editor.findChild<QAction *>("assemblyOpenPlayer");
		ok &= expect(action && action->isEnabled() && !action->toolTip().isEmpty(), "Open Player is on the assembly toolbar");
		if (!action)
			return 1;
		if (scenario == 0)
		{
			// Without a package there is nothing to open.
			ok &= expect(!editor.openPlayer("synthetic", {}, &error) && error.contains("Open a package") && editor.document().assembly().parts.isEmpty(),
						 "opening a player needs a package");
		}
		editor.setMaterialSource({archive, QStringLiteral("player-import-%1").arg(scenario), {}});
		app.processEvents();
		bool picked = false;
		whenModal([](QDialog *dialog) { return named(dialog, "assemblyOpenPlayerDialog"); },
				  [&](QDialog *dialog) {
					  picked = true;
					  if (scenario)
						  dialog->resize(900, 900);
					  app.processEvents();
					  auto *list = dialog->findChild<QListWidget *>("assemblyPlayerList");
					  auto *skin = dialog->findChild<QComboBox *>("assemblyPlayerSkin");
					  auto *summary = dialog->findChild<QPlainTextEdit *>("assemblyPlayerSummary");
					  auto *open = dialog->findChild<QPushButton *>("assemblyPlayerOpen");
					  ok &= expect(list && skin && summary && open, "the picker exposes stable control identities");
					  if (!list || !skin || !summary || !open)
					  {
						  dialog->reject();
						  return;
					  }
					  for (QWidget *control : std::initializer_list<QWidget *>{list, skin, summary, open})
						  ok &= expect(!control->accessibleName().isEmpty() && !control->accessibleDescription().isEmpty() &&
										   control->focusPolicy() != Qt::NoFocus && control->isVisible(),
									   "each picker control is named, described, focusable and visible");
					  ok &= expect(list->count() == 2 && list->item(0)->text().contains("broken") && list->item(0)->text().contains("cannot be opened") &&
									   list->item(1)->text() == "synthetic" && list->currentRow() == 1,
								   "players are listed with their state in text, and the first that opens is chosen");
					  ok &= expect(skin->count() == 3 && skin->itemText(0) == "default" && skin->itemText(1) == "blue" && skin->itemText(2) == "red" &&
									   open->isEnabled() && summary->toPlainText().contains("red"),
								   "the complete skins and a summary are offered");
					  list->setCurrentRow(0);
					  ok &= expect(!open->isEnabled() && skin->count() == 0 && summary->toPlainText().contains("upper.md3 is missing") &&
									   !list->item(0)->data(Qt::AccessibleDescriptionRole).toString().isEmpty(),
								   "a player that cannot be opened says why and cannot be chosen");
					  list->setCurrentRow(1);
					  ok &= expect(dialog->layout()->minimumSize().width() <= dialog->width() && !summary->horizontalScrollBar()->isVisible(),
								   "the picker fits its text at this scale and translation length");
					  // Keyboard: the list, then the skin, the summary and the buttons.
					  list->setFocus(Qt::TabFocusReason);
					  until([&] { return list->hasFocus(); });
					  press(Qt::Key_Tab);
					  ok &= expect(QApplication::focusWidget() == skin, "Tab moves from the players to the skin");
					  press(Qt::Key_Tab);
					  ok &= expect(QApplication::focusWidget() == summary, "Tab moves from the skin to the summary");
					  press(Qt::Key_Tab);
					  ok &= expect(qobject_cast<QPushButton *>(QApplication::focusWidget()), "Tab leaves the summary for the buttons");
					  skin->setCurrentIndex(skin->findText("red"));
					  open->click();
				  });
		action->trigger();
		ok &= expect(picked && until([&] { return !editor.operationBusy() && editor.document().assembly().parts.size() == 3; }),
					 "the chosen player opens");
		const auto &assembly = editor.document().assembly();
		ok &= expect(assembly.parts.size() == 3 && assembly.parts[0].skin && assembly.parts[0].skin->source.endsWith("lower_red.skin") &&
						 assembly.q3Animation && assembly.q3Animation->config.sex == "f" && until([&] { return editor.previewReady(); }),
					 "the assembly has the package parts, the red skins and the native animation, and previews");
		ok &= expect(editor.document().isModified() && editor.document().path().isEmpty() && !editor.document().canUndo(),
					 "it is a new, unsaved source");
		const auto &publication = editor.playerBundleOptions();
		ok &= expect(publication.modelName == "synthetic" && publication.skinName == "red" && publication.variants.size() == 2 &&
						 publication.variants[0].name == "default" && publication.variants[1].name == "blue" && publication.teamVariants &&
						 publication.iconKind == ModelAssemblySource::Package,
					 "Player Package is filled from the package");
		bool reported = false;
		for (auto *label : editor.findChildren<QLabel *>())
			reported |= label->text().contains("synthetic") && label->text().contains("unsaved");
		ok &= expect(reported, "the status says which player opened and that it is unsaved");
		const auto opened = modelAssemblyJson(assembly);
		// Edits to the opened assembly are ordinary, undoable assembly edits.
		auto head = assembly.parts[2];
		head.translation = {0, 0, 2};
		ok &= expect(editor.applyPart("head", head, &error) && editor.document().canUndo() && editor.undo(&error) &&
						 modelAssemblyJson(editor.document().assembly()) == opened,
					 "an edit to the opened player undoes back to the opened state");
		// Native Animation imports another animation.cfg from the package.
		bool animated = false, chose = false;
		whenModal([](QDialog *dialog) { return named(dialog, "modelQ3AnimationDialog"); },
				  [&](QDialog *dialog) {
					  animated = true;
					  auto *importPackage = dialog->findChild<QPushButton *>("q3ImportPackage");
					  auto *sex = dialog->findChild<QComboBox *>("q3Sex");
					  ok &= expect(importPackage && importPackage->isEnabled() && !importPackage->accessibleDescription().isEmpty() && sex,
								   "Import from Package is offered for the open package");
					  if (!importPackage || !sex)
					  {
						  dialog->reject();
						  return;
					  }
					  whenModal([](QDialog *input) { return qobject_cast<QInputDialog *>(input) != nullptr; },
								[&](QDialog *input) {
									chose = true;
									auto *items = qobject_cast<QInputDialog *>(input);
									ok &= expect(items->comboBoxItems() == QStringList({"models/players/broken/animation.cfg",
																						 "models/players/synthetic/animation.cfg"}),
												 "every animation.cfg of the package is offered by path");
									items->setTextValue("models/players/broken/animation.cfg");
									items->accept();
								});
					  importPackage->click();
					  ok &= expect(chose && sex->currentData().toString() == "m" &&
									   dialog->findChild<QLabel *>("q3Status")->text().contains("models/players/broken/animation.cfg"),
								   "the chosen entry fills the draft and the status names it");
					  dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
				  });
		editor.findChild<QAction *>("assemblyQ3Animation")->trigger();
		ok &= expect(animated && until([&] { return !editor.operationBusy(); }), "the native animation dialog ran");
		ok &= expect(until([&] { return editor.document().assembly().q3Animation && editor.document().assembly().q3Animation->config.sex == "m"; }),
					 "the imported configuration is applied to the assembly");
		ok &= expect(editor.undo(&error) && editor.document().assembly().q3Animation->config.sex == "f", "Undo restores the opened configuration");
		// The package is never a destination.
		ok &= expect(!editor.saveSource(package + "/models/players/synthetic/edited.assembly.json", false, &error) &&
						 error.contains("Write outside the model's input package") &&
						 !QFileInfo::exists(package + "/models/players/synthetic/edited.assembly.json"),
					 "the assembly cannot be saved into its package folder");
		ok &= expect(editor.saveSource(temporary.filePath(QStringLiteral("opened-%1.assembly.json").arg(scenario)), false, &error) &&
						 !editor.document().isModified(),
					 "the opened assembly saves beside the package");
		// A failed check above can leave changes; never wait on the save prompt.
		if (editor.document().isModified())
			whenModal([](QDialog *dialog) { return qobject_cast<QMessageBox *>(dialog) != nullptr; },
					  [](QDialog *dialog) { qobject_cast<QMessageBox *>(dialog)->button(QMessageBox::Discard)->click(); });
		editor.close();
		app.processEvents();
		if (scenario)
			app.removeTranslator(&expansion);
	}
	ok &= expect(folderHash(package) == before, "no file of the package folder changed");
	std::cout << checks << " player import GUI checks\n";
	return ok ? 0 : 1;
}
