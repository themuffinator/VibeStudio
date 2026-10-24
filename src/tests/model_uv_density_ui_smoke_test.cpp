#include "app/model_editor_dialog.h"
#include "app/model_uv_density_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/level_material_test_helpers.h"
#include "tests/model_uv_density_test_helpers.h"
#include "tests/model_uv_view_test_helpers.h"
#include "tests/render_test_support.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>
using namespace vibestudio;
using namespace vibestudio::tests;
namespace
{
int checks = 0, failures = 0;
bool expect(bool value, const char *text)
{
	++checks;
	if (!value)
	{
		++failures;
		std::cerr << "FAIL: " << text << '\n';
	}
	return value;
}
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char *context, const char *text, const char *, int) const override
	{
		if (QByteArray(context) != "ModelUvDensityDialog")
			return {};
		const auto s = QString::fromUtf8(text);
		return QString("[%1 %2]").arg(s, QString("~~ ").repeated(s.size() / 6));
	}
};
bool render(QWidget &w, const QString &name)
{
	const auto root = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (root.isEmpty())
		return true;
	QImage image(w.size() * w.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(w.devicePixelRatioF());
	image.fill(Qt::transparent);
	w.render(&image);
	return image.save(QDir(root).filePath(QString("density-%1-%2x.png").arg(name).arg(w.devicePixelRatioF())));
}
void configure(ModelUvDensityDialog &d)
{
	d.findChild<QSpinBox *>("uvDensityWidth")->setValue(100);
	d.findChild<QSpinBox *>("uvDensityHeight")->setValue(100);
	d.findChild<QDoubleSpinBox *>("uvDensityTarget")->setValue(5);
}
ModelUvDensityDialog *active(QApplication &app)
{
	for (auto *w : app.topLevelWidgets())
		if (auto *d = dynamic_cast<ModelUvDensityDialog *>(w); d && d->isVisible())
			return d;
	return nullptr;
}
} // namespace
int main(int argc, char **argv)
{
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
	QTemporaryDir temp(QDir(root).filePath("uv-density-ui-XXXXXX"));
	if (!temp.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini"));
	if (const int unavailable = test_support::exitCodeWithoutRenderer("UV density UI"); unavailable >= 0)
		return unavailable;
	const auto mesh = densityFixture();
	const auto bytes = uvIslandBytes(mesh);
	QImage texture(64, 32, QImage::Format_ARGB32);
	texture.fill(QColor(100, 110, 120));
	QString error;
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		Expansion expansion;
		if (scenario)
			app.installTranslator(&expansion);
		app.setLayoutDirection(scenario == 2 ? Qt::RightToLeft : Qt::LeftToRight);
		applyStudioTheme(app, studioThemeTokens(scenario == 0	? StudioTheme::Dark
												: scenario == 1 ? StudioTheme::HighContrastLight
																: StudioTheme::HighContrastDark,
												UiDensity::Standard, scenario ? 200 : 100));
		ModelUvDensityDialog d(mesh, {0, {}, {0, 2}}, 0, texture, {1024, 512}, "textures/stone");
		d.show();
		QCoreApplication::processEvents();
		auto *tabs = d.findChild<QTabWidget *>("uvDensityTabs");
		auto *apply = d.findChild<QPushButton *>("uvDensityApply");
		auto *view = d.findChild<ModelUvView *>("uvDensityPreview");
		expect(!d.ready() && !apply->isEnabled(), "Apply waits for a validated preview");
		d.accept();
		expect(d.result() != QDialog::Accepted, "programmatic acceptance also requires preview");
		expect(d.findChild<QSpinBox *>("uvDensityWidth")->value() == 1024 && d.findChild<QSpinBox *>("uvDensityHeight")->value() == 512,
			   "original material dimensions override downsampled preview");
		configure(d);
		d.findChild<QPushButton *>("uvDensityUseMaterial")->click();
		expect(d.findChild<QSpinBox *>("uvDensityWidth")->value() == 1024, "restore material dimensions");
		configure(d);
		for (const auto *id : {"uvDensityWidth", "uvDensityHeight", "uvDensityFrame", "uvDensityScope", "uvDensityTarget", "uvDensityMode",
							   "uvDensityUseMeasured", "uvDensityInspect", "uvDensityReview", "uvDensityApply", "uvDensityResults",
							   "uvDensityDetails", "uvDensityPreview", "uvDensityView"})
		{
			auto *w = d.findChild<QWidget *>(id);
			expect(w && !w->accessibleName().isEmpty() && !w->accessibleDescription().isEmpty(),
				   "controls expose accessible names and instructions");
			if (w)
			{
				auto *a = QAccessible::queryAccessibleInterface(w);
				expect(a && !a->text(QAccessible::Name).isEmpty(), "accessible interface names controls");
			}
		}
		expect(render(d, QString("settings-%1").arg(scenario)), "render density settings");
		d.inspect();
		expect(!d.ready() && !apply->isEnabled() && d.before().islands.size() == 2 &&
				   std::abs(d.before().density - std::sqrt(250.)) < .0001,
			   "inspect measures without making Apply available");
		d.findChild<QPushButton *>("uvDensityUseMeasured")->click();
		expect(std::abs(d.findChild<QDoubleSpinBox *>("uvDensityTarget")->value() - std::sqrt(250.)) < .000001,
			   "measured combined density can become target");
		configure(d);
		d.preview();
		expect(d.ready() && apply->isEnabled() && d.after().islands.size() == 2 && std::abs(d.after().density - 5) < .0001,
			   "preview matches target");
		expect(uvIslandBytes(mesh) == bytes && uvSamePoses(mesh.surfaces[0], d.previewMesh().surfaces[0]),
			   "preview leaves input and all geometry unchanged");
		auto *results = d.findChild<QTableView *>("uvDensityResults");
		expect(results->model()->rowCount() == 2 && results->model()->columnCount() == 6 &&
				   !results->model()->data(results->model()->index(0, 5)).toString().isEmpty(),
			   "virtual table exposes every island and textual status");
		results->setCurrentIndex(results->model()->index(1, 0));
		expect(!d.findChild<QLineEdit *>("uvDensityDetails")->text().isEmpty(), "full measurement details remain copyable");
		expect(render(d, QString("results-%1").arg(scenario)), "render density results");
		tabs->setCurrentIndex(2);
		QCoreApplication::processEvents();
		expect(settleModelUv(*view) && render(d, QString("preview-%1").arg(scenario)), "render reviewed UV result");
		const auto origin = view->uvToScreen({0, 0});
		expect(std::abs(std::abs(view->uvToScreen({1, 0}).x() - origin.x()) - std::abs(view->uvToScreen({0, 1}).y() - origin.y())) < 1,
			   "UV preview uses explicit square dimensions despite rectangular material preview");
		const auto afterPreview = view->pixmap().toImage();
		d.findChild<QComboBox *>("uvDensityView")->setCurrentIndex(1);
		expect(settleModelUv(*view) && render(d, QString("original-%1").arg(scenario)) && view->pixmap().toImage() != afterPreview,
			   "compare original UV coverage");
		d.findChild<QComboBox *>("uvDensityMode")->setCurrentIndex(1);
		expect(!d.ready() && !apply->isEnabled() && d.before().islands.size() == 2,
			   "mode change invalidates review but retains measurement");
		d.preview();
		expect(d.ready() && std::abs(d.after().islands[1].density / d.after().islands[0].density - 2) < .0001,
			   "group mode preserves relative island density");
		ModelDocument document;
		expect(document.setMesh(mesh, &error) && document.edit(d.edit(), &error) &&
				   uvIslandBytes(document.mesh()) == uvIslandBytes(d.previewMesh()) && document.undo() &&
				   uvIslandBytes(document.mesh()) == bytes,
			   "review replays exactly and undoes");
		d.findChild<QSpinBox *>("uvDensityFrame")->setValue(1);
		expect(!d.ready() && d.before().islands.isEmpty(), "reference pose invalidates measurement");
		d.inspect();
		expect(std::abs(d.before().density - std::sqrt(250.) / 2) < .0001, "reference pose measures correct geometry");
		tabs->setCurrentIndex(0);
		d.resize(900, 600);
		QCoreApplication::processEvents();
		expect(d.width() <= 900 && d.height() <= 600, "dialog fits compact logical display");
		for (const auto *id : {"uvDensityInspect", "uvDensityReview", "uvDensityApply"})
		{
			auto *w = d.findChild<QWidget *>(id);
			const QRect box(w->mapTo(&d, QPoint()), w->size());
			expect(d.rect().contains(box), "dialog action remains within window at enlarged text scale");
		}
		auto *scroll = d.findChild<QScrollArea *>("uvDensitySettingsScroll");
		for (auto *w : scroll->findChildren<QWidget *>())
			if (qobject_cast<QSpinBox *>(w) || qobject_cast<QDoubleSpinBox *>(w) || qobject_cast<QComboBox *>(w) ||
				qobject_cast<QPushButton *>(w))
			{
				scroll->ensureWidgetVisible(w);
				QCoreApplication::processEvents();
				expect(scroll->viewport()->rect().contains(QRect(w->mapTo(scroll->viewport(), QPoint()), w->size())),
					   "settings remain reachable by scrolling");
			}
		expect(render(d, QString("compact-%1").arg(scenario)), "render compact density settings");
		d.resize(500, 600);
		QCoreApplication::processEvents();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
		QCoreApplication::processEvents();
		expect(d.width() <= 500 && d.height() <= 600, "density dialog fits a narrow logical display");
		if (scenario)
			expect(d.findChild<QDialogButtonBox *>()->orientation() == Qt::Vertical, "actions stack at enlarged text on a narrow display");
		for (auto *button : d.findChild<QDialogButtonBox *>()->buttons())
			expect(d.rect().contains(QRect(button->mapTo(&d, QPoint()), button->size())),
				   "narrow display retains every action inside window");
		tabs->setCurrentIndex(1);
		QCoreApplication::processEvents();
		for (int c = 0; c < results->model()->columnCount(); ++c)
		{
			expect(results->horizontalHeader()->sectionSize(c) >=
					   results->fontMetrics().horizontalAdvance(results->model()->headerData(c, Qt::Horizontal).toString()),
				   "result headings are sized for readable scrolling");
		}
		expect(render(d, QString("narrow-results-%1").arg(scenario)), "render narrow scrolling results");
		d.reject();
		if (scenario)
			app.removeTranslator(&expansion);
	}
	app.setLayoutDirection(Qt::LeftToRight);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	ModelUvDensityDialog missing(mesh, {}, 0, texture, {}, {});
	expect(!missing.findChild<QPushButton *>("uvDensityUseMaterial")->isEnabled() &&
			   missing.findChild<QSpinBox *>("uvDensityWidth")->value() == 512,
		   "missing material uses explicit default size rather than preview dimensions");
	configure(missing);
	missing.preview();
	expect(missing.ready() && missing.after().faces.size() == 4, "empty initial selection defaults to entire surface");
	ModelUvDensityDialog selected(mesh, {0, {}, {0}}, 0, texture, {100, 100}, {});
	configure(selected);
	selected.preview();
	expect(selected.ready() && selected.after().faces == QSet<int>{0, 1}, "selected component expands to complete island");
	auto bad = mesh;
	bad.surfaces[0].texCoords[2] = bad.surfaces[0].texCoords[0];
	ModelUvDensityDialog invalid(bad, {0, {}, {0}}, 0, texture, {100, 100}, {});
	configure(invalid);
	invalid.preview();
	expect(!invalid.ready() && invalid.before().islands.size() == 1 && !invalid.findChild<QLabel *>("uvDensityStatus")->text().isEmpty(),
		   "invalid UVs retain repair measurements and refuse Apply");
	ModelUvDensityDialog closing(mesh, {}, 0, texture, {100, 100}, {});
	bool cancelled = false;
	QTimer::singleShot(0,
					   [&]
					   {
						   cancelled = closing.busy();
						   closing.preview();
						   closing.accept();
						   closing.reject();
					   });
	closing.preview();
	expect(cancelled && !closing.busy() && !closing.ready() && closing.result() == QDialog::Rejected,
		   "close cancels and joins worker; nested review and acceptance cannot commit");
	ModelEditorDialog editor;
	expect(editor.setMesh(mesh, &error), "editor accepts density fixture");
	editor.show();
	auto *action = editor.findChild<QAction *>("meshUvDensity");
	expect(action && editor.findChild<QAction *>("inspectMeshUvDensity"), "density is reachable from Tools and UV toolbar");
	QByteArray reviewed;
	QTimer::singleShot(0,
					   [&]
					   {
						   auto *d = active(app);
						   if (!expect(d != nullptr, "editor opens density dialog"))
							   return;
						   configure(*d);
						   d->preview();
						   expect(d->ready(), "editor previews density");
						   reviewed = uvIslandBytes(d->previewMesh());
						   if (d->ready())
							   d->accept();
						   else
							   d->reject();
					   });
	if (action)
		action->trigger();
	expect(editor.document().canUndo() && uvIslandBytes(editor.document().mesh()) == reviewed,
		   "editor commits exactly the reviewed density result");
	editor.findChild<QAction *>("undoMesh")->trigger();
	expect(uvIslandBytes(editor.document().mesh()) == bytes, "editor density undo");
	editor.findChild<QAction *>("redoMesh")->trigger();
	expect(uvIslandBytes(editor.document().mesh()) == reviewed, "editor density redo");
	QByteArray replacement;
	QTimer::singleShot(0,
					   [&]
					   {
						   auto *d = active(app);
						   if (!expect(d != nullptr, "editor opens density review again"))
							   return;
						   configure(*d);
						   d->preview();
						   auto changed = mesh;
						   changed.surfaces[0].name = "replacement";
						   editor.setMesh(changed, &error);
						   replacement = uvIslandBytes(editor.document().mesh());
						   d->accept();
					   });
	if (action)
		action->trigger();
	expect(uvIslandBytes(editor.document().mesh()) == replacement && !editor.document().canUndo(),
		   "changed document refuses stale reviewed operation");
	// Follow the real package resolver into the editor, including original size
	// metadata retained when the image itself is reduced for preview.
	QImage material(2048, 1024, QImage::Format_RGB32);
	material.fill(Qt::blue);
	QByteArray png;
	QBuffer buffer(&png);
	expect(buffer.open(QIODevice::WriteOnly) && material.save(&buffer, "PNG"), "encode synthetic package material");
	expect(putMaterialFile(temp.filePath("assets/models/density.png"), png), "write package material fixture");
	auto archive = std::make_shared<PackageArchive>();
	expect(archive->load(temp.filePath("assets"), &error), "open synthetic package material source");
	auto textured = mesh;
	textured.surfaces[0].skinPaths = {"models/density.png"};
	updateEditableModelMetadata(&textured);
	expect(editor.setMesh(textured, &error), "load material-linked mesh");
	editor.setMaterialSource({archive, "density-fixture", {}});
	editor.findChild<QComboBox *>("meshRenderMode")->setCurrentIndex(3);
	QElapsedTimer loading;
	loading.start();
	while (editor.materialLoading() && loading.elapsed() < 20000)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QThread::msleep(1);
	}
	expect(!editor.materialLoading(), "package material preparation completes");
	QTimer::singleShot(0,
					   [&]
					   {
						   auto *d = active(app);
						   if (!expect(d != nullptr, "editor opens density with package material"))
							   return;
						   expect(d->findChild<QSpinBox *>("uvDensityWidth")->value() == 2048 &&
									  d->findChild<QSpinBox *>("uvDensityHeight")->value() == 1024 &&
									  d->findChild<QLabel *>("uvDensityMaterial")->text().contains("models/density.png"),
								  "editor forwards original package dimensions and resolved path");
						   d->reject();
					   });
	if (action)
		action->trigger();
	editor.close();
	std::cout << checks << " UV density UI checks; " << failures << " failures\n";
	return failures ? 1 : 0;
}
