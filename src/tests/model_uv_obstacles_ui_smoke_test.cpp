#include "app/model_editor_dialog.h"
#include "app/studio_theme.h"
#include "core/model_fingerprint.h"
#include "core/package_archive.h"
#include "core/studio_settings.h"
#include "tests/model_uv_obstacles_test_helpers.h"
#include "tests/model_uv_view_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFont>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTableView>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTranslator>
#include <cmath>
#include <iostream>

using namespace vibestudio;
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
	QString translate(const char *context, const char *text, const char *, int) const override
	{
		if (QByteArray(context) != "VibeStudioModelEditor" && QByteArray(context) != "ModelUvObstacles")
			return {};
		const auto value = QString::fromUtf8(text);
		return QStringLiteral("[%1%2]").arg(value, QString(value.size() / 2, '~'));
	}
};
bool settle(ModelEditorDialog &editor, ModelUvView &uv)
{
	QElapsedTimer elapsed;
	elapsed.start();
	while (editor.materialLoading() && elapsed.elapsed() < 20000)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QThread::msleep(1);
	}
	return !editor.materialLoading() && tests::settleModelUv(uv, 60000);
}
bool render(QWidget &widget, int scenario, const QString &phase)
{
	const auto directory = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (directory.isEmpty())
		return true;
	QImage image(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget.devicePixelRatioF());
	image.fill(Qt::transparent);
	widget.render(&image);
	return image.save(
		QDir(directory).filePath(QString("uv-obstacles-%1-%2-%3x.png").arg(phase).arg(scenario).arg(widget.devicePixelRatioF())));
}
void selectRows(QTableView &table, int first, int last)
{
	table.selectionModel()->select(QItemSelection(table.model()->index(first, 0), table.model()->index(last, 0)),
								   QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}
} // namespace
int main(int argc, char **argv)
{
	// Owned widget values, signals and render targets; no OS input or capture.
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
	QTemporaryDir temporary(QDir(root).filePath("uv-obstacles-ui-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	const auto assets = QDir(temporary.path()).filePath("assets");
	if (!QDir().mkpath(QDir(assets).filePath("models")))
		return 1;
	QImage texture(512, 128, QImage::Format_RGB32);
	for (int y = 0; y < texture.height(); ++y)
		for (int x = 0; x < texture.width(); ++x)
		{
			const bool painted = x < 512 * .35 || (x >= 256 && y >= 96) || (x >= 512 * .8 && y < 64);
			texture.setPixelColor(x, y,
								  painted					? QColor(145, 83, 53)
								  : ((x / 16 + y / 16) % 2) ? QColor(100, 135, 160)
															: QColor(45, 65, 85));
		}
	for (const auto name : {"atlas.png", "alternate.png", "separate.png"})
		if (!texture.save(QDir(assets).filePath(QStringLiteral("models/") + QLatin1String(name)))) return 1;
	auto package = std::make_shared<PackageArchive>();
	QString error;
	if (!package->load(assets, &error))
		return 1;
	bool ok = true;
	const auto original = tests::obstaclePanels();
	const auto originalFingerprint = modelStateFingerprint(original);
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		Expansion expansion;
		if (scenario)
			app.installTranslator(&expansion);
		applyStudioTheme(app, studioThemeTokens(scenario == 0	? StudioTheme::Dark
												: scenario == 1 ? StudioTheme::HighContrastLight
																: StudioTheme::HighContrastDark,
												UiDensity::Standard, scenario ? 200 : 100));
		app.setLayoutDirection(scenario == 1 ? Qt::RightToLeft : Qt::LeftToRight);
		ModelEditorDialog editor;
		editor.setAccessibility(scenario > 0, true);
		ok &= expect(editor.setMesh(original, &error), "open animated UV obstacle fixture");
		editor.resize(scenario ? 2400 : 1400, scenario ? 1650 : 1040);
		editor.setMaterialSource({package, QStringLiteral("uv-obstacles"), {}});
		editor.show();
		app.processEvents();
		auto *width = editor.findChild<QSpinBox *>("meshUvAtlasResolution");
		auto *height = editor.findChild<QSpinBox *>("meshUvAtlasHeight");
		auto *square = editor.findChild<QCheckBox *>("meshUvAtlasSquare");
		auto *padding = editor.findChild<QSpinBox *>("meshUvAtlasPadding");
		auto *pack = editor.findChild<QPushButton *>("packMeshUvAround");
		auto *scale = editor.findChild<QComboBox *>("meshUvObstacleScale");
		auto *table = editor.findChild<QTableView *>("meshComponents");
		auto *uv = editor.findChild<ModelUvView *>("meshUvPreview");
		auto *mode = editor.findChild<QComboBox *>("meshSelectionMode");
		auto *renderMode = editor.findChild<QComboBox *>("meshRenderMode");
		auto *undo = editor.findChild<QAction *>("undoMesh");
		auto *redo = editor.findChild<QAction *>("redoMesh");
		if (!expect(width && height && square && padding && pack && scale && table && uv && mode && renderMode && undo && redo,
					"fixed-region controls exist"))
			return 1;
		renderMode->setCurrentIndex(3);
		for (auto *tabs : editor.findChildren<QTabWidget *>())
			for (int i = 0; i < tabs->count(); ++i)
				if (tabs->widget(i)->isAncestorOf(uv) || tabs->widget(i)->isAncestorOf(width))
					tabs->setCurrentIndex(i);
		ok &= expect(!pack->isEnabled() && !scale->isEnabled() && scale->currentIndex() == 0,
					 "packing needs selected faces and defaults to uniform fit");
		width->setValue(512);
		square->setChecked(false);
		height->setValue(128);
		padding->setValue(3);
		selectRows(*table, 0, 3);
		scale->setCurrentIndex(scenario == 1 ? 1 : 0);
		ok &= expect(pack->isEnabled() && scale->isEnabled() && editor.document().selection().faces == tests::obstacleFaces(),
					 "selected islands enable fixed-region packing");
		const QList<QWidget *> controls{width, height, square, padding, scale, pack};
		for (auto *control : controls)
		{
			auto *accessible = QAccessible::queryAccessibleInterface(control);
			ok &= expect(accessible && !accessible->text(QAccessible::Name).isEmpty() &&
							 !accessible->text(QAccessible::Description).isEmpty() && control->focusPolicy() != Qt::NoFocus,
						 "controls expose accessible names, help and focus");
		}
		for (auto *scroll : editor.findChildren<QScrollArea *>())
		{
			if (!scroll->isAncestorOf(width))
				continue;
			// Let the sidebar page finish laying out at the new text size first.
			app.processEvents();
			const int first = width->mapTo(scroll->widget(), QPoint()).y(),
					  last = pack->mapTo(scroll->widget(), pack->rect().bottomLeft()).y();
			scroll->verticalScrollBar()->setValue((first + last - scroll->viewport()->height()) / 2);
			app.processEvents();
			for (auto *control : controls)
				ok &= expect(scroll->horizontalScrollBar()->maximum() == 0 &&
								 scroll->viewport()->rect().contains(QRect(control->mapTo(scroll->viewport(), QPoint()), control->size())),
							 "dimensions, scale policy and action fit with expanded text and RTL");
		}
		scale->setFocus(Qt::OtherFocusReason);
		ok &= expect(scale->hasFocus(), "scale policy is keyboard focusable");
		ok &= expect(settle(editor, *uv) && editor.findChild<QLabel *>("meshMaterialStatus")->text().contains(QStringLiteral("3/3")),
					 "all fixture materials resolve and the active image reaches the UV view");
		const auto origin = uv->uvToScreen({0, 0}), across = uv->uvToScreen({1, 0}), down = uv->uvToScreen({0, 1});
		ok &=
			expect(std::abs(std::abs((across.x() - origin.x()) / (down.y() - origin.y())) - 4) < .001 && render(editor, scenario, "before"),
				   "render painted regions using the actual rectangular project texture");
		ModelEdit edit;
		edit.kind = ModelEditKind::PackUvAround;
		edit.selection = editor.document().selection();
		edit.uvAtlasResolution = 512;
		edit.uvAtlasHeight = 128;
		edit.uvAtlasPadding = 3;
		edit.uvPreserveScale = scenario == 1;
		auto expected = original;
		ok &= expect(applyModelEdit(&expected, edit, nullptr, &error), "shared core prepares reviewed packing settings");
		bool responsive = false, locked = false;
		QTimer::singleShot(0, &editor, [&] {
			responsive = editor.operationBusy();
			locked = !pack->isEnabled() && !scale->isEnabled();
		});
		pack->click();
		ok &= expect(responsive && locked && !editor.operationBusy() && scale->isEnabled(),
					 "worker shows busy state and restores packing controls");
		const auto fingerprint = modelStateFingerprint(expected);
		ok &= expect(modelStateFingerprint(editor.document().mesh()) == fingerprint && editor.document().selection() == edit.selection,
					 "GUI/core agree on exact UVs and preserved selection");
		pack->setFocus(Qt::OtherFocusReason);
		ok &= expect(settle(editor, *uv) && render(editor, scenario, "after"), "render packed islands beside existing painted regions");
		undo->trigger();
		ok &= expect(modelStateFingerprint(editor.document().mesh()) == originalFingerprint && !editor.document().canUndo(),
					 "one undo restores complete source");
		redo->trigger();
		ok &= expect(modelStateFingerprint(editor.document().mesh()) == fingerprint, "redo restores exact packing");
		QTimer::singleShot(0, &editor, [&] { editor.cancelOperation(); });
		ok &= expect(!editor.applyEdit(edit, &error) && modelStateFingerprint(editor.document().mesh()) == fingerprint,
					 "cancel publishes no partial mapping");
		edit.selection.faces = {0};
		ok &= expect(!editor.applyEdit(edit, &error) && modelStateFingerprint(editor.document().mesh()) == fingerprint,
					 "partial island refusal retains document");
		mode->setCurrentIndex(3);
		ok &= expect(!pack->isEnabled() && !scale->isEnabled(), "tag mode disables fixed-region packing");
		if (qEnvironmentVariable("QT_SCALE_FACTOR") == "2")
			ok &= expect(std::abs(editor.devicePixelRatioF() - 2) < .01, "actual 2x display scaling");
		ok &= expect(editor.setMesh(original, &error), "retire fixture recovery state");
		editor.hide();
		if (scenario)
			app.removeTranslator(&expansion);
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	app.setLayoutDirection(Qt::LeftToRight);
	const auto maximum = tests::maximumObstacleStrip();
	const auto before = modelStateFingerprint(maximum);
	ModelEditorDialog large;
	large.resize(1600, 1000);
	large.setAccessibility(false, true);
	large.show();
	app.processEvents();
	auto *largeUv = large.findChild<ModelUvView *>("meshUvPreview");
	for (auto *tabs : large.findChildren<QTabWidget *>())
		for (int i = 0; i < tabs->count(); ++i)
			if (largeUv && tabs->widget(i)->isAncestorOf(largeUv))
				tabs->setCurrentIndex(i);
	ModelEdit edit;
	edit.kind = ModelEditKind::PackUvAround;
	edit.uvAtlasResolution = 512;
	edit.uvAtlasHeight = 128;
	edit.uvAtlasPadding = 3;
	edit.uvPreserveScale = true;
	for (int i = 0; i < maximum.surfaces[0].triangles.size() - 2; ++i)
		edit.selection.faces.insert(i);
	QElapsedTimer elapsed;
	elapsed.start();
	qint64 previous = 0, gap = 0;
	int beats = 0;
	const auto beat = [&] {
		const auto now = elapsed.nsecsElapsed();
		gap = std::max(gap, now - previous);
		previous = now;
		++beats;
	};
	QTimer heartbeat;
	heartbeat.setInterval(5);
	heartbeat.setTimerType(Qt::PreciseTimer);
	QObject::connect(&heartbeat, &QTimer::timeout, &app, beat);
	heartbeat.start();
	ok &= expect(large.setMesh(maximum, &error) && large.applyEdit(edit, &error),
				 "maximum pose-vertex workload opens and packs on production workers");
	if (largeUv)
		ok &= expect(tests::settleModelUv(*largeUv, 60000), "maximum packed UV preview settles");
	large.findChild<QAction *>("undoMesh")->trigger();
	app.processEvents();
	beat();
	heartbeat.stop();
	ok &= expect(modelStateFingerprint(large.document().mesh()) == before, "maximum undo restores all fixed and moving pose vertices");
	const double budget = qEnvironmentVariable("VIBESTUDIO_MODELLER_MAX_AUTHORING_GAP_MS").toDouble();
	ok &= expect(beats > 0 && gap / 1e6 <= (budget > 0 ? budget : 300), "maximum packing and undo remain responsive");
	std::cout << QJsonDocument(QJsonObject{{"stage", "uv-obstacles"},
										   {"elapsedMs", elapsed.nsecsElapsed() / 1e6},
										   {"maxEventGapMs", gap / 1e6},
										   {"beats", beats},
										   {"vertices", maximum.vertexCount},
										   {"triangles", maximum.triangleCount},
										   {"frames", maximum.frameCount}})
					 .toJson(QJsonDocument::Compact)
					 .constData()
			  << '\n';
	ok &= expect(large.setMesh(original, &error), "retire large recovery state");
	StudioSettings::setOverrideFilePath({});
	if (!ok)
		std::cerr << error.toStdString() << '\n';
	std::cout << checks << " UV obstacle GUI checks\n";
	return ok ? 0 : 1;
}
