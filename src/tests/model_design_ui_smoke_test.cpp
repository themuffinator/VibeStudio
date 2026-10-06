#include "app/application_shell.h"
#include "app/model_design_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "tests/model_viewport_test_helpers.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabWidget>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>

#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char* message)
{
	if (!value) {
		std::cerr << message << '\n';
	}
	return value;
}
class ExpandedModelLabels final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		if (QByteArray(context) != "VibeStudioModelDesignDialog") {
			return {};
		}
		const QString text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 3, QLatin1Char('~')));
	}
};
} // namespace
int main(int argc, char** argv)
{
	// Direct widget state and command calls only; no keyboard/mouse injection,
	// OS capture, external applications, or game data.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temp;
	if (!temp.isValid()) {
		return EXIT_FAILURE;
	}
	PackageArchive archive;
	PackageStagingModel staging;
	QString error;
	bool ok = expect(archive.load(temp.path(), &error) && staging.loadBaseArchive(archive, &error), "open empty authoring package");
	for (int scale : {100, 200}) {
		ExpandedModelLabels expansion;
		if (scale == 200) {
			app.installTranslator(&expansion);
		}
		applyStudioTheme(app,
		                 studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark, UiDensity::Standard, scale));
		auto* dialog = new ModelDesignDialog;
		if (scale == 200) {
			dialog->setLayoutDirection(Qt::RightToLeft);
			dialog->resize(1500, 1000);
		}
		LevelMapDocument map;
		map.format = LevelMapFormat::Quake3Map;
		map.sourcePath = QStringLiteral("fixture.map");
		LevelMapEntity world;
		world.id = 0;
		world.className = QStringLiteral("worldspawn");
		world.properties = {{QStringLiteral("classname"), world.className, 0}};
		map.entities << world;
		dialog->context = [&]() { return ModelDesignContext{temp.path(), map.sourcePath, true, true}; };
		int handoffs = 0;
		dialog->handoff = [&](const ModelDesign& design, const QString& path, bool place, const LevelMapVec3& origin, bool replace,
		                      QString* problem) {
			++handoffs;
			return stageModelDesign(design, path, &staging, place ? &map : nullptr, origin, replace, problem);
		};
		dialog->show();
		dialog->refreshContext();
		app.processEvents();
		auto* list = dialog->findChild<QListWidget*>(QStringLiteral("designParts"));
		auto* preview = dialog->findChild<ModelViewport*>(QStringLiteral("designPreview"));
		preview->setHighContrast(scale == 200);
		auto* stage = dialog->findChild<QAction*>(QStringLiteral("stageModelDesign"));
		auto* place = dialog->findChild<QAction*>(QStringLiteral("placeModelDesign"));
		auto* properties = dialog->findChild<QTabWidget*>(QStringLiteral("designProperties"));
		ok &= expect(properties && properties->count() == 3, "part, surface and handoff controls have separate tabs");
		for (int tab = 0; tab < properties->count(); ++tab) {
			ok &= expect(properties->tabBar()->rect().contains(properties->tabBar()->tabRect(tab)),
			             "all property tabs remain readable with enlarged and expanded text");
		}
		ok &= expect(list && preview && preview->hasMesh() && !list->accessibleName().isEmpty() && list->focusPolicy() != Qt::NoFocus,
		             "designer starts with accessible, focusable parts and visible geometry");
		dialog->addPart(QStringLiteral("cylinder"));
		dialog->undo();
		ok &=
		    expect(!dialog->isWindowModified() && dialog->design().parts.size() == 1, "undo to the loaded design clears the dirty marker");
		dialog->redo();
		dialog->duplicatePart();
		ok &= expect(dialog->design().parts.size() == 3 && list->count() == 3, "part commands update model and list together");
		dialog->removePart();
		dialog->undo();
		ok &= expect(dialog->design().parts.size() == 3 && list->currentRow() == 2, "undo restores removed part and selection");
		dialog->redo();
		ok &= expect(dialog->design().parts.size() == 2, "redo reapplies part removal");
		preview->setOrbit(53, 27);
		list->setCurrentRow(0);
		ok &= expect(preview->highlightedTriangleCount() == 12, "parts list selection highlights the corresponding surface");
		preview->triangleClicked(1, 12);
		ok &= expect(list->currentRow() == 1 && preview->highlightedTriangleCount() == 48 && preview->yaw() == 53 && preview->pitch() == 27,
		             "viewport picks select a whole part and preserve the camera");
		list->setCurrentRow(0);
		auto* size = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("designSizeX"));
		size->setValue(96);
		ok &= expect(dialog->design().parts[0].size.x == 96 && preview->mesh().surfaces[0].vertexCount == 24,
		             "property edit updates the authoring document and preview");
		auto* roll = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("designRoll"));
		auto* pitch = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("designPitch"));
		roll->setValue(23);
		pitch->setValue(-31);
		ok &= expect(dialog->design().parts[0].roll == 23 && dialog->design().parts[0].pitch == -31 && !pitch->accessibleName().isEmpty() &&
		                 pitch->focusPolicy() != Qt::NoFocus,
		             "accessible part controls author X and Y rotation");
		properties->setCurrentIndex(1);
		app.processEvents();
		auto* uvScale = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("designUvScaleU"));
		auto* uvOffset = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("designUvOffsetV"));
		auto* uvRotation = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("designUvRotation"));
		uvScale->setValue(-2);
		uvOffset->setValue(0.25);
		uvRotation->setValue(45);
		const auto uvDesign = modelDesignJson(dialog->design());
		dialog->findChild<QPushButton*>(QStringLiteral("resetDesignUv"))->click();
		ok &= expect(dialog->design().parts[0].uvScale.u == 1 && dialog->design().parts[0].uvRotation == 0,
		             "reset UVs restores the identity mapping");
		dialog->undo();
		ok &= expect(modelDesignJson(dialog->design()) == uvDesign && list->currentRow() == 0, "reset UVs is a single undo step");
		auto* renderMode = dialog->findChild<QComboBox*>(QStringLiteral("designRenderMode"));
		renderMode->setCurrentIndex(2);
		ok &= expect(preview->hasSkin() && preview->renderMode() == ModelViewportRenderMode::Textured,
		             "UV checker preview uses the transformed mesh coordinates");
		uvScale->setValue(0);
		ok &= expect(!stage->isEnabled() && !place->isEnabled() && !preview->hasMesh(), "zero UV scale blocks invalid exports");
		dialog->undo();
		ok &= expect(preview->hasMesh() && preview->hasSkin(), "repairing a UV error restores the checker preview");
		auto* material = dialog->findChild<QLineEdit*>(QStringLiteral("designMaterial"));
		material->setText(QStringLiteral("../escape"));
		ok &= expect(!stage->isEnabled() && !place->isEnabled(), "invalid design disables package/map writes");
		material->setText(QStringLiteral("textures/props/test"));
		ok &= expect(material->height() >= material->fontMetrics().height() + 8 && size->height() >= size->fontMetrics().height() + 8,
		             "scaled property controls must leave room for text and padding");
		auto* path = dialog->findChild<QLineEdit*>(QStringLiteral("designPackagePath"));
		path->setText(QStringLiteral("models/props/ui%1.md3").arg(scale));
		dialog->findChild<QDoubleSpinBox*>(QStringLiteral("designPlacementX"))->setValue(128);
		ok &= expect(stage->isEnabled() && place->isEnabled(), "valid design and context enable handoff");
		place->trigger();
		ok &= expect(handoffs == 1 && map.entities.size() == 2 && map.entities.last().origin.x == 128,
		             "UI command uses the shared stage/placement service");
		ok &= expect(dialog->findChild<QLabel*>(QStringLiteral("designStatus"))->text().contains(QStringLiteral("ui%1.md3").arg(scale)),
		             "handoff status names the asset");
		dialog->context = []() { return ModelDesignContext(); };
		dialog->refreshContext();
		ok &= expect(!place->isEnabled() && !stage->isEnabled(), "missing project context disables staging and placement");
		const QString captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
		if (!captures.isEmpty()) {
			for (int tab = 0; tab < properties->count(); ++tab) {
				properties->setCurrentIndex(tab);
				auto* scroll = qobject_cast<QScrollArea*>(properties->currentWidget());
				for (int position : {0, 1}) {
					scroll->verticalScrollBar()->setValue(position ? scroll->verticalScrollBar()->maximum() : 0);
					app.processEvents();
					QImage image(dialog->size(), QImage::Format_ARGB32_Premultiplied);
					image.fill(Qt::transparent);
					ok &= expect(tests::settleModelViewport(*preview), "designer preview completes before evidence capture");
					dialog->render(&image);
					ok &= expect(image.save(QDir(captures).filePath(
					                 QStringLiteral("model-design-%1-tab%2-%3.png").arg(scale).arg(tab).arg(position))),
					             "save each property tab with widget rendering, without desktop capture");
				}
			}
		}
		// A source may carry more precision than displayed spin boxes. Editing
		// a name must preserve it, and cancelling via undo must restore clean state.
		auto precise = dialog->design();
		precise.parts[0].yaw = 1.234567891;
		precise.parts[0].origin.x = 0.000000123f;
		precise.parts[0].size.x = 1.0f / 64;
		ok &= expect(dialog->setDesign(precise, &error), "load precise design");
		dialog->findChild<QLineEdit*>(QStringLiteral("designPartName"))->setText(QStringLiteral("renamed"));
		ok &= expect(dialog->design().parts[0].yaw == precise.parts[0].yaw &&
		                 dialog->design().parts[0].origin.x == precise.parts[0].origin.x &&
		                 dialog->design().parts[0].size.x == precise.parts[0].size.x,
		             "unrelated field edits preserve source precision");
		delete dialog;
		if (scale == 200) {
			app.removeTranslator(&expansion);
		}
	}
	// Exercise the actual shell callbacks, including the dependency worker,
	// without synthesizing any mouse or keyboard events.
	const QString assetRoot = QDir(temp.path()).filePath(QStringLiteral("assets"));
	QDir().mkpath(QDir(assetRoot).filePath(QStringLiteral("textures/common")));
	QImage texture(8, 8, QImage::Format_ARGB32);
	texture.fill(Qt::gray);
	ok &= expect(texture.save(QDir(assetRoot).filePath(QStringLiteral("textures/common/caulk.png"))), "write generated texture fixture");
	PackageArchive fixtureArchive;
	PackageStagingModel fixturePlan;
	ok &= expect(fixtureArchive.load(assetRoot, &error) && fixturePlan.loadBaseArchive(fixtureArchive, &error), "prepare shell package");
	PackageWriteRequest packageWrite;
	packageWrite.destinationPath = QDir(temp.path()).filePath(QStringLiteral("fixtures.pk3"));
	ok &= expect(fixturePlan.writeArchive(packageWrite).succeeded(), "write shell package");
	const QString mapPath = QDir(temp.path()).filePath(QStringLiteral("fixture.map"));
	QFile mapFile(mapPath);
	ok &= expect(mapFile.open(QIODevice::WriteOnly), "open shell map output");
	mapFile.write("// Q3Radiant\n{\n\"classname\" \"worldspawn\"\n}\n");
	mapFile.close();
	StudioSettings::setOverrideFilePath(QDir(temp.path()).filePath(QStringLiteral("shell-settings.ini")));
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	auto* shell = new ApplicationShell;
	shell->openPathFromCommandLine(packageWrite.destinationPath);
	shell->openPathFromCommandLine(mapPath);
	auto* designButton = shell->findChild<QPushButton*>(QStringLiteral("openModelDesigner"));
	ok &= expect(designButton != nullptr, "Models must expose the authoring action");
	if (designButton) {
		designButton->click();
	}
	auto* designer = static_cast<ModelDesignDialog*>(shell->findChild<QDialog*>(QStringLiteral("modelDesignDialog")));
	ok &= expect(designer != nullptr, "Models action must open the connected designer");
	if (designer) {
		auto* place = designer->findChild<QAction*>(QStringLiteral("placeModelDesign"));
		ok &= expect(place->isEnabled(), "loaded package and Quake III map enable shell handoff");
		place->trigger();
		auto* preview = shell->findChild<ModelViewport*>(QStringLiteral("mapPreview3D"));
		// Level geometry/material resolution now runs on its own worker. Wait
		// for the same expected handoff geometry without injecting user input.
		if (preview && preview->mesh().triangleCount != 12) {
			QEventLoop loop;
			QTimer poll, timeout;
			timeout.setSingleShot(true);
			QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
				if (preview->mesh().triangleCount == 12) { loop.quit(); }
			});
			QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
			poll.start(15);
			timeout.start(10000);
			loop.exec();
		}
		ok &= expect(preview && preview->mesh().triangleCount == 12, "shell handoff refreshes the level prop geometry");
		shell->findChild<QPushButton*>(QStringLiteral("inspectLevelDependencies"))->click();
		auto* dependencies = shell->findChild<QDialog*>(QStringLiteral("levelDependencyDialog"));
		if (dependencies) {
			auto* exportAssets = dependencies->findChild<QPushButton*>(QStringLiteral("exportLevelAssets"));
			QEventLoop loop;
			QTimer poll, timeout;
			timeout.setSingleShot(true);
			QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
				if (exportAssets->isEnabled()) {
					loop.quit();
				}
			});
			QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
			poll.start(15);
			timeout.start(10000);
			loop.exec();
			ok &= expect(exportAssets->isEnabled(), "shell dependency audit must see staged generated models and their materials");
			delete dependencies;
		} else {
			ok &= expect(false, "shell dependency action must open the audit");
		}
	}
	delete shell;
	app.processEvents();
	StudioSettings::setOverrideFilePath({});
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
