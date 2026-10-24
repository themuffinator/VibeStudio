// Counts and numbers in the modeller as a German or Polish user reads them.
//
// With German number formats, a vertex at x = 0.5 reads "0,5" in the
// component table, as the spin boxes beside it write it, while the table's
// edit role and Copy keep "0.5" for files and the command line; a position in
// a row is set apart with semicolons; the modal readout, the drag readout and
// the curve graph's value use the decimal comma too. A test translator with
// Polish's three plural forms then shows that the status line hands each
// count to its own message: 1, 2 and 5 selected vertices take the singular,
// the few and the many forms. A clip's playback rate reads "12,5 fps" in the
// view's lines. scripts/english_plurals.py checks the modeller sources for a
// count written before a fixed English plural.
#include "app/locale_number.h"
#include "app/model_animation_curve_view.h"
#include "app/model_editor_dialog.h"
#include "app/model_editor_tools.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_rip_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAbstractItemModel>
#include <QAccessible>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFont>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLocale>
#include <QTableView>
#include <QTemporaryDir>
#include <QTranslator>

#include <cstdlib>
#include <iostream>

using namespace vibestudio;
namespace
{
int failures = 0;
bool expect(bool ok, const char *message, const QString &detail = {})
{
	if (!ok)
	{
		++failures;
		std::cerr << "FAIL: " << message;
		if (!detail.isEmpty())
			std::cerr << ": " << detail.toUtf8().constData();
		std::cerr << '\n';
	}
	return ok;
}
void settle()
{
	for (int pass = 0; pass < 3; ++pass)
		QCoreApplication::processEvents();
}
// Polish counts: one, then 2 to 4 (but not 12 to 14), then the rest.
class Polish final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char *context, const char *text, const char *, int n) const override
	{
		if (QByteArray(context) != "VibeStudioModelEditor" || QByteArray(text) != "%Ln vert(ex)(ices)" || n < 0)
			return {};
		const int tens = n % 100, units = n % 10;
		if (n == 1)
			return QStringLiteral("%Ln wierzchołek");
		if (units >= 2 && units <= 4 && (tens < 12 || tens > 14))
			return QStringLiteral("%Ln wierzchołki");
		return QStringLiteral("%Ln wierzchołków");
	}
};
} // namespace

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	// Draws in 3D: skip where no OpenGL or Vulkan renderer starts.
	if (const int skip = test_support::exitCodeWithoutRenderer("model-locale-ui-smoke"); skip >= 0)
		return skip;
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("mesh-locale-ui-XXXXXX")));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath(QStringLiteral("settings.ini")));
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));

	expect(localeNumber(0.5) == QStringLiteral("0,5") && localeNumber(1234.5) == QStringLiteral("1234,5") &&
			   localeNumber(0.25, 3, 'f') == QStringLiteral("0,250"),
		   "numbers take the decimal comma and no group separator, as the spin boxes write them", localeNumber(1234.5));
	expect(localeNumberList({0.5, 1, 2}) == QStringLiteral("0,5; 1; 2"), "a position is set apart with semicolons",
		   localeNumberList({0.5, 1, 2}));

	auto mesh = tests::ripGrid();
	const int corner = tests::ripVertex(mesh, -16, -16);
	mesh.surfaces[0].frames[0].positions[corner].x = 0.5f;
	updateEditableModelMetadata(&mesh);
	QString error;
	ModelEditorDialog editor;
	editor.resize(1600, 1000);
	editor.show();
	settle();
	auto *table = editor.findChild<QTableView *>(QStringLiteral("meshComponents"));
	auto *mode = editor.findChild<QComboBox *>(QStringLiteral("meshSelectionMode"));
	auto *status = editor.findChild<QLabel *>(QStringLiteral("meshStatus"));
	auto *view = editor.findChild<ModelViewport *>(QStringLiteral("meshPreview"));
	auto *tools = editor.tools();
	if (!expect(table && mode && status && view && tools && editor.setMesh(mesh, &error), "the editor opens the grid", error))
		return EXIT_FAILURE;
	settle();

	// The table cell, its edit value and what Copy takes.
	mode->setCurrentIndex(1);
	settle();
	const auto cell = table->model()->index(corner, 1);
	expect(cell.data(Qt::DisplayRole).toString() == QStringLiteral("0,5") && cell.data(Qt::ToolTipRole).toString() == QStringLiteral("0,5"),
		   "the vertex's X reads with a decimal comma", cell.data(Qt::DisplayRole).toString());
	expect(cell.data(Qt::EditRole).toString() == QStringLiteral("0.5"), "its edit value keeps the C locale", cell.data(Qt::EditRole).toString());
	table->setFocus();
	table->setCurrentIndex(cell);
	QKeyEvent copy(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
	QApplication::sendEvent(table, &copy);
	expect(QGuiApplication::clipboard()->text() == QStringLiteral("0.5"), "Copy takes the C locale value", QGuiApplication::clipboard()->text());

	// The modal readout while moving one vertex.
	ModelSelection one;
	one.vertices = {corner};
	tools->applySelection(one);
	settle();
	expect(tools->beginModal(ModelModalOperator::Move, QRectF(view->rect()).center()), "a move starts");
	tools->constrainModal(0, false);
	for (const auto *typed : {"0", ".", "5"})
		tools->typeModal(QString::fromLatin1(typed));
	expect(tools->modalText().contains(QStringLiteral("0,500; 0,000; 0,000")) && tools->modalText().contains(QStringLiteral("0,5")),
		   "the move's readout and the typed value use the decimal comma",
		   tools->modalText());
	tools->finishModal(false);
	settle();

	// The curve graph's value.
	ModelAnimationCurveView graph;
	ModelPoseCurveKey key;
	key.frame = 3;
	key.value = 0.75;
	graph.setCurve({key}, 8, 0, 0);
	auto *access = QAccessible::queryAccessibleInterface(&graph);
	expect(access && access->text(QAccessible::Value) == QStringLiteral("0,75") &&
			   access->text(QAccessible::Description).contains(QStringLiteral("0,75")),
		   "the curve graph's value uses the decimal comma", access ? access->text(QAccessible::Description) : QString());

	// The playback rate in the HUD lines and the Animation summary.
	{
		auto animated = tests::ripGrid();
		animated.frames << animated.frames.first();
		animated.frames.last().name = QStringLiteral("frame001");
		for (auto &surface : animated.surfaces)
			surface.frames << surface.frames.first();
		updateEditableModelMetadata(&animated);
		ModelViewport player;
		player.resize(320, 240);
		player.setMesh(animated);
		player.setFramesPerSecond(12.5);
		expect(player.statusLines().join(QLatin1Char(' ')).contains(QStringLiteral("12,5 fps")), "a paused clip's rate uses the decimal comma",
			   player.statusLines().join(QLatin1Char(' ')));
		player.play();
		expect(player.isPlaying() && player.playbackSummary().contains(QStringLiteral("12,5 fps")) &&
				   player.statusLines().join(QLatin1Char(' ')).contains(QStringLiteral("12,5 fps")),
			   "a playing clip's rate uses the decimal comma", player.playbackSummary());
		player.pause();
	}

	// Polish plural forms for 1, 2 and 5 selected vertices.
	Polish polish;
	app.installTranslator(&polish);
	const QString forms[]{QStringLiteral("1 wierzchołek"), QStringLiteral("2 wierzchołki"), QStringLiteral("5 wierzchołków")};
	int index = 0;
	for (const int count : {1, 2, 5})
	{
		ModelSelection selection;
		for (int v = 0; v < count; ++v)
			selection.vertices.insert(v);
		tools->applySelection(selection);
		settle();
		expect(status->text().contains(QStringLiteral("Selected: ") + forms[index]) &&
				   status->text().startsWith(QStringLiteral("9 wierzchołków")),
			   "each count takes its own plural form", status->text());
		++index;
	}
	app.removeTranslator(&polish);
	QLocale::setDefault(QLocale::c());
	if (failures)
	{
		std::cerr << failures << " check(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "Model locale UI smoke passed.\n";
	return EXIT_SUCCESS;
}
