#include "app/model_editor_dialog.h"
#include "app/model_import_repair_dialog.h"
#include "app/studio_theme.h"
#include "core/model_fingerprint.h"
#include "core/studio_settings.h"
#include "tests/model_import_repair_test_helpers.h"
#include "tests/model_scale_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFontMetrics>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
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
	QString translate(const char *context, const char *source, const char *, int) const override
	{
		if (QByteArray(context) != "VibeStudioModelImportRepair")
			return {};
		const auto value = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(value, QString(value.size() / 2, '~'));
	}
};
bool capture(QWidget &widget, const QString &name)
{
	const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (evidence.isEmpty())
		return true;
	QImage image(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget.devicePixelRatioF());
	image.fill(Qt::transparent);
	widget.render(&image);
	return image.save(QDir(evidence).filePath(QStringLiteral("import-repair-%1-%2x.png").arg(name).arg(widget.devicePixelRatioF())));
}
} // namespace
int main(int argc, char **argv)
{
	// Test-owned Qt values/signals and widget renders; no input injection or OS capture.
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
	QTemporaryDir temporary(QDir(root).filePath("import-repair-ui-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	const QDir dir(temporary.path());
	StudioSettings::setOverrideFilePath(dir.filePath("settings.ini"));
	const auto input = dir.filePath("damaged.mesh.json");
	const auto bytes = tests::importRepairSource(tests::damagedImportFixture());
	const auto expected = tests::expectedImportRepair();
	QString error;
	bool ok = expect(tests::writeImportRepairFixture(input, bytes), "write damaged source for review");
	ModelImportRepairPlan plan;
	ok &= expect(prepareModelImportRepair(input, &plan, &error), "prepare review fixture");
	if (!ok)
		return 1;
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		Expansion expansion;
		if (scenario)
			app.installTranslator(&expansion);
		applyStudioTheme(app, studioThemeTokens(scenario == 0	? StudioTheme::Dark
												: scenario == 1 ? StudioTheme::HighContrastLight
																: StudioTheme::HighContrastDark,
												UiDensity::Standard, scenario ? 200 : 100));
		ModelImportRepairDialog review(plan, scenario > 0);
		review.setLayoutDirection(scenario == 1 ? Qt::RightToLeft : Qt::LeftToRight);
		review.resize(scenario ? 2200 : 1280, scenario ? 1450 : 850);
		review.show();
		app.processEvents();
		auto *table = review.findChild<QTableView *>("importRepairChanges");
		auto *output = review.findChild<QLineEdit *>("importRepairOutput");
		auto *button = review.findChild<QPushButton *>("saveImportRepair");
		auto *preview = review.findChild<ModelViewport *>("importRepairPreview");
		auto *poses = review.findChild<QComboBox *>("importRepairPose");
		if (!table || !output || !button || !preview || !poses)
			return 1;
		ok &= expect(table->model()->rowCount() == 9 && table->model()->data(table->model()->index(3, 2)).toString() == "2" &&
						 table->model()->data(table->model()->index(6, 1)).toString().contains("+Z"),
					 "review displays original face, pose and fallback details");
		table->setCurrentIndex(table->model()->index(3, 0));
		ok &= expect(preview->frame() == 2 && poses->currentIndex() == 2, "review row navigates to its source pose");
		const auto *detail = review.findChild<QLabel *>("importRepairDetail");
		ok &= expect(detail && detail->wordWrap() && detail->text().contains("collapsed pose") && detail->text().contains("element 5"),
					 "selected change exposes the full description and exact original index at expanded scales");
		ok &= expect(tests::settleModelViewport(*preview), "prepared copy renders");
		ok &=
			expect(modelStateFingerprint(preview->mesh()) == modelStateFingerprint(expected), "preview shows only the exact prepared copy");
		ok &= expect(output->layoutDirection() == Qt::LeftToRight && !output->accessibleDescription().isEmpty() &&
						 output->text().endsWith("damaged-repaired.mesh.json"),
					 "new destination is explicit and bidi-safe");
		output->setText(dir.filePath("bad.md3"));
		ok &= expect(!button->isEnabled(), "invalid source extension cannot be accepted");
		output->setText(dir.filePath(QStringLiteral("scenario%1.mesh.json").arg(scenario)));
		ok &= expect(button->isEnabled() && !button->isDefault(), "saving requires an explicit action");
		for (QWidget *widget : QVector<QWidget *>{table, output, button, poses})
		{
			auto *accessible = QAccessible::queryAccessibleInterface(widget);
			ok &= expect(accessible && !widget->accessibleName().isEmpty() && widget->focusPolicy() != Qt::NoFocus,
						 "review controls have focus policy and accessible names");
		}
		ok &= expect(button->fontMetrics().horizontalAdvance(button->text()) + 24 <= button->width(), "expanded save action fits");
		ok &= expect(review.findChild<QLabel *>("importRepairSummary")->wordWrap() &&
						 review.findChild<QLabel *>("importRepairScope")->wordWrap(),
					 "long review summaries wrap");
		ok &= expect(capture(review, QStringLiteral("review-%1").arg(scenario)), "retain review widget image");
		review.reject();
		ok &= expect(tests::readImportRepairFixture(input) == bytes && !QFileInfo::exists(output->text()),
					 "review cancellation writes nothing");
		ModelEditorDialog editor;
		editor.resize(scenario ? 2200 : 1360, scenario ? 1450 : 960);
		editor.setLayoutDirection(scenario == 1 ? Qt::RightToLeft : Qt::LeftToRight);
		editor.setAccessibility(scenario > 0, true);
		editor.show();
		app.processEvents();
		const auto before = editor.document().revisionFingerprint();
		ok &= expect(editor.findChild<QAction *>("repairImportMesh"), "repair import is discoverable in document actions");
		QTimer::singleShot(0, &editor, [&editor] { editor.cancelOperation(); });
		ok &= expect(!editor.repairSource(input, &error) && editor.document().revisionFingerprint() == before,
					 "worker cancellation preserves the open editor");
		const auto drive = [&](bool accept, const QString &destination, bool changeSource = false) {
			bool seen = false;
			QTimer reviewTimer;
			reviewTimer.setInterval(5);
			QObject::connect(&reviewTimer, &QTimer::timeout, &editor, [&] {
				auto *dialog = editor.findChild<QDialog *>("modelImportRepairDialog");
				if (!dialog || !dialog->isVisible())
					return;
				reviewTimer.stop();
				seen = true;
				dialog->findChild<QLineEdit *>("importRepairOutput")->setText(destination);
				if (changeSource)
					ok &= expect(tests::writeImportRepairFixture(input, bytes + "\n"), "simulate external source edit during review");
				if (accept)
					dialog->findChild<QPushButton *>("saveImportRepair")->click();
				else
					dialog->reject();
			});
			reviewTimer.start();
			const bool result = editor.repairSource(input, &error);
			reviewTimer.stop();
			ok &= expect(seen, "prepared changes require visible review before adoption");
			return result;
		};
		const auto destination = dir.filePath(QStringLiteral("editor%1.mesh.json").arg(scenario));
		ok &= expect(!drive(false, destination) && editor.document().revisionFingerprint() == before && !QFileInfo::exists(destination),
					 "cancelled review keeps the original editor document");
		ok &=
			expect(!drive(true, destination, true) && editor.document().revisionFingerprint() == before && !QFileInfo::exists(destination),
				   "changed input cannot replace the open editor after review");
		ok &= expect(tests::writeImportRepairFixture(input, bytes), "restore owned review fixture");
		ok &= expect(drive(true, destination) && editor.document().path() == destination && !editor.document().isModified() &&
						 modelStateFingerprint(editor.document().mesh()) == modelStateFingerprint(expected),
					 "accepted review saves and opens the exact repaired source");
		auto *mainPreview = editor.findChild<ModelViewport *>("meshPreview");
		ok &= expect(mainPreview && tests::settleModelViewport(*mainPreview), "adopted repair renders in ordinary editor");
		ok &= expect(capture(editor, QStringLiteral("editor-%1").arg(scenario)), "retain adopted editor image");
		ModelEdit edit;
		edit.selection.surface = 0;
		edit.selection.faces = {0};
		edit.translation = {0, 0, 1};
		ok &=
			expect(editor.applyEdit(edit, &error) && editor.document().canUndo(), "repaired copy continues through normal mesh authoring");
		editor.findChild<QAction *>("undoMesh")->trigger();
		ok &= expect(modelStateFingerprint(editor.document().mesh()) == modelStateFingerprint(expected),
					 "undo restores exact repaired baseline");
		ok &= expect(editor.setMesh(expected, &error), "retire fixture recovery state");
		if (scenario)
			app.removeTranslator(&expansion);
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	auto maximum = tests::maximumEditableGrid();
	for (auto &pose : maximum.surfaces[0].frames)
		pose.normals.fill({}, pose.normals.size());
	const auto maximumPath = dir.filePath("maximum.mesh.json"), maximumOutput = dir.filePath("maximum-repaired.mesh.json");
	ok &= expect(tests::writeImportRepairFixture(maximumPath, tests::importRepairSource(maximum)), "write maximum repair workload");
	ModelEditorDialog large;
	large.resize(1600, 1000);
	large.setAccessibility(false, true);
	large.show();
	app.processEvents();
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
	QTimer heartbeat, reviewTimer;
	heartbeat.setInterval(5);
	heartbeat.setTimerType(Qt::PreciseTimer);
	QObject::connect(&heartbeat, &QTimer::timeout, &app, beat);
	reviewTimer.setInterval(5);
	bool sawLargeReview = false;
	QObject::connect(&reviewTimer, &QTimer::timeout, &large, [&] {
		auto *dialog = large.findChild<QDialog *>("modelImportRepairDialog");
		if (!dialog || !dialog->isVisible())
			return;
		reviewTimer.stop();
		sawLargeReview = true;
		auto *table = dialog->findChild<QTableView *>("importRepairChanges");
		ok &= expect(table->model()->rowCount() == modelDocumentMaxFrameVertices &&
						 table->model()->data(table->model()->index(modelDocumentMaxFrameVertices - 1, 3)).toString() == "65535",
					 "maximum repair list formats its last row on demand");
		dialog->findChild<QLineEdit *>("importRepairOutput")->setText(maximumOutput);
		dialog->findChild<QPushButton *>("saveImportRepair")->click();
	});
	heartbeat.start();
	reviewTimer.start();
	ok &= expect(large.repairSource(maximumPath, &error) && sawLargeReview,
				 "maximum repair prepares, reviews, saves and opens through production workers");
	if (auto *preview = large.findChild<ModelViewport *>("meshPreview"))
		ok &= expect(tests::settleModelViewport(*preview, 60000), "maximum repaired preview settles");
	app.processEvents();
	beat();
	reviewTimer.stop();
	heartbeat.stop();
	const double budget = qEnvironmentVariable("VIBESTUDIO_MODELLER_MAX_AUTHORING_GAP_MS").toDouble();
	ok &= expect(beats > 0 && gap / 1e6 <= (budget > 0 ? budget : 300), "maximum import repair remains responsive");
	std::cout << QJsonDocument(QJsonObject{{"stage", "repair-import"},
										   {"elapsedMs", elapsed.nsecsElapsed() / 1e6},
										   {"maxEventGapMs", gap / 1e6},
										   {"beats", beats},
										   {"vertices", 65536},
										   {"triangles", 130050},
										   {"frames", 16},
										   {"rebuiltNormals", modelDocumentMaxFrameVertices}})
					 .toJson(QJsonDocument::Compact)
					 .constData()
			  << '\n';
	ok &= expect(large.setMesh(expected, &error), "retire large recovery state");
	if (!ok)
		std::cerr << error.toStdString() << '\n';
	std::cout << checks << " import repair GUI checks\n";
	return ok ? 0 : 1;
}
