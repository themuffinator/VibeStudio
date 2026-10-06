#include "app/model_editor_dialog.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_scale_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFont>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <cstdlib>
#include <iostream>

using namespace vibestudio;
namespace
{
class MeasuredApplication final : public QApplication
{
  public:
	using QApplication::QApplication;
	void beginStage()
	{
		m_slow.clear();
		m_tracing = qEnvironmentVariableIntValue("VIBESTUDIO_MODELLER_TRACE_AUTHORING_EVENTS") != 0;
	}
	QJsonArray slowEvents() const
	{
		QJsonArray result;
		for (const auto &entry : m_slow)
		{
			result.append(entry);
		}
		return result;
	}
	bool notify(QObject *receiver, QEvent *event) override
	{
		if (QThread::currentThread() != thread() || !m_tracing)
		{
			return QApplication::notify(receiver, event);
		}
		const auto name = receiver->objectName();
		const auto *type = receiver->metaObject()->className();
		const int eventType = int(event->type());
		QElapsedTimer timer;
		timer.start();
		const bool result = QApplication::notify(receiver, event);
		const double elapsed = timer.nsecsElapsed() / 1e6;
		if (elapsed >= 20)
		{
			m_slow.append(QJsonObject{{"object", name}, {"class", QString::fromLatin1(type)}, {"event", eventType}, {"ms", elapsed}});
			std::sort(m_slow.begin(), m_slow.end(), [](const auto &a, const auto &b) { return a["ms"].toDouble() > b["ms"].toDouble(); });
			if (m_slow.size() > 8)
			{
				m_slow.resize(8);
			}
		}
		return result;
	}

  private:
	bool m_tracing = false;
	QVector<QJsonObject> m_slow;
};
bool expect(bool value, const char *message)
{
	if (!value)
	{
		std::cerr << "FAIL: " << message << std::endl;
	}
	return value;
}
} // namespace

int main(int argc, char **argv)
{
	// Direct widget/signal calls and widget-owned rendering, with isolated settings.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	MeasuredApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("authoring-latency-XXXXXX")));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath(QStringLiteral("settings.ini")));
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	bool ok = true;
	const auto mesh = tests::maximumEditableGrid();
	ok &= expect(validateEditableModel(mesh).isEmpty(), "maximum authoring fixture is valid");
	ModelEditorDialog editor;
	editor.resize(1600, 1000);
	editor.show();
	app.processEvents();
	auto *viewport = editor.findChild<ModelViewport *>(QStringLiteral("meshPreview"));
	auto *table = editor.findChild<QTableView *>(QStringLiteral("meshComponents"));
	auto *mode = editor.findChild<QComboBox *>(QStringLiteral("meshSelectionMode"));
	auto *frame = editor.findChild<QComboBox *>(QStringLiteral("meshFrame"));
	if (!expect(viewport && table && mode && frame, "authoring controls exist"))
	{
		return EXIT_FAILURE;
	}
	const auto requestedScale = qEnvironmentVariable("QT_SCALE_FACTOR").toDouble();
	if (requestedScale > 0)
	{
		ok &= expect(std::abs(viewport->devicePixelRatioF() - requestedScale) < .01, "requested display scale is active");
	}
	const QJsonObject scene{{"devicePixelRatio", viewport->devicePixelRatioF()},
							{"theme", "dark"},
							{"textScalePercent", 100},
							{"vertices", mesh.vertexCount},
							{"triangles", mesh.triangleCount},
							{"frames", mesh.frameCount}};
	std::cout << "SCENE " << QJsonDocument(scene).toJson(QJsonDocument::Compact).constData() << std::endl;
	viewport->setShowGrid(false);
	viewport->setShowAxes(false);
	ok &= expect(tests::settleModelViewport(*viewport), "initial editor preview settles");
	const auto measure = [&](const char *name, auto operation)
	{
		std::cout << "BEGIN " << name << std::endl;
		app.beginStage();
		QElapsedTimer clock;
		clock.start();
		qint64 previous = 0, maximumGap = 0;
		int beats = 0;
		const auto beat = [&]
		{
			const auto now = clock.nsecsElapsed();
			maximumGap = std::max(maximumGap, now - previous);
			previous = now;
			++beats;
		};
		QTimer heartbeat;
		heartbeat.setTimerType(Qt::PreciseTimer);
		heartbeat.setInterval(5);
		QObject::connect(&heartbeat, &QTimer::timeout, &app, beat);
		heartbeat.start();
		operation();
		const double callMs = clock.nsecsElapsed() / 1e6;
		ok &= expect(tests::settleModelViewport(*viewport, 60000), name);
		app.processEvents();
		beat();
		QJsonObject record{{"stage", QString::fromLatin1(name)},
						   {"callMs", callMs},
						   {"settledMs", clock.nsecsElapsed() / 1e6},
						   {"maxEventGapMs", maximumGap / 1e6},
						   {"heartbeats", beats}};
		if (!app.slowEvents().isEmpty())
		{
			record.insert(QStringLiteral("slowEvents"), app.slowEvents());
		}
		std::cout << QJsonDocument(record).toJson(QJsonDocument::Compact).constData() << std::endl;
		const int budget = qEnvironmentVariableIntValue("VIBESTUDIO_MODELLER_MAX_AUTHORING_GAP_MS");
		if (budget > 0)
		{
			ok &= expect(maximumGap / 1e6 <= budget, "configured authoring event-loop budget");
		}
	};
	QString error;
	measure("editor-set-mesh",
			[&]
			{
				ok &= expect(editor.setMesh(mesh, &error), qPrintable(error));
				viewport->setOrbit(0, 90);
			});
	int resets = 0;
	QObject::connect(table->model(), &QAbstractItemModel::modelReset, &app, [&] { ++resets; });
	const auto capture = [&](const QString &stage)
	{
		const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
		if (evidence.isEmpty())
		{
			return;
		}
		const double ratio = editor.devicePixelRatioF();
		QImage image(editor.size() * ratio, QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(ratio);
		editor.render(&image);
		ok &= expect(QDir().mkpath(evidence) && image.save(QDir(evidence).filePath(QStringLiteral("maximum-authoring-%1.png").arg(stage))),
					 "save widget-owned authoring evidence");
	};
	ok &= expect(table->model()->rowCount() == mesh.triangleCount, "all faces reach the component table");
	measure("select-all-faces", [&] { table->selectAll(); });
	ok &= expect(editor.document().selection().faces.size() == mesh.triangleCount, "all faces reach document selection");
	measure("frame-with-all-faces", [&] { frame->setCurrentIndex(15); });
	ok &= expect(editor.document().selection().faces.size() == mesh.triangleCount && viewport->frame() == 15,
				 "frame refresh preserves complete selection");
	ok &= expect(resets == 0 && table->selectionModel()->selection().size() == 1,
				 "face selection and pose updates retain the table model and one compact range");
	measure("clear-faces", [&] { table->clearSelection(); });
	measure("pick-one-face", [&] { viewport->trianglePicked(0, 65024, int(ModelViewportPick::Replace)); });
	ok &= expect(editor.document().selection().faces == QSet<int>{65024}, "viewport pick reaches document and table");
	ok &= expect(resets == 0 && table->selectionModel()->selection().first().top() == 65024,
				 "viewport selection updates existing rows without resetting the inspector");
	viewport->trianglePicked(0, 65024, int(ModelViewportPick::Replace));
	ok &= expect(!viewport->isRendering() && resets == 0, "unchanged component pick preserves the completed preview and inspector");
	measure("vertex-mode", [&] { mode->setCurrentIndex(1); });
	ok &= expect(table->model()->rowCount() == mesh.vertexCount, "all vertices reach the component table");
	ok &= expect(resets == 1 && table->model()->data(table->model()->index(0, 3)).toString() == QStringLiteral("15"),
				 "mode change resets once and exposes the current vertex pose");
	measure("vertex-pick-16",
			[&]
			{
				for (int i = 0; i < 16; ++i)
				{
					const int vertex = 128 * 256 + 110 + i;
					ok &=
						expect(viewport->vertexAt(viewport->vertexScreenPosition(0, vertex), .1).valid, "exact maximum-scene vertex pick");
				}
			});
	measure("select-all-vertices", [&] { table->selectAll(); });
	ok &= expect(editor.document().selection().vertices.size() == mesh.vertexCount, "all vertices reach document selection");
	const int vertexColumnWidth = table->columnWidth(1) + 25;
	table->setColumnWidth(1, vertexColumnWidth);
	measure("frame-with-all-vertices", [&] { frame->setCurrentIndex(0); });
	ok &= expect(table->columnWidth(1) == vertexColumnWidth && table->horizontalHeader()->sectionResizeMode(1) == QHeaderView::Interactive,
				 "pose updates preserve user-resized component columns");
	ok &= expect(resets == 1 && table->selectionModel()->selection().size() == 1 &&
					 table->model()->data(table->model()->index(0, 3)).toString() == QStringLiteral("0"),
				 "vertex pose update changes values without discarding selected rows");
	measure("vertex-orbit", [&] { viewport->setOrbit(25, 65); });
	capture(QStringLiteral("vertices"));
	measure("edge-mode", [&] { mode->setCurrentIndex(2); });
	ok &= expect(table->model()->rowCount() == 195585, "all unique edges reach the component table");
	measure("select-all-edges", [&] { table->selectAll(); });
	ok &= expect(editor.document().selection().edges.size() == 195585, "all edges reach document selection");
	measure("frame-with-all-edges", [&] { frame->setCurrentIndex(15); });
	ok &= expect(editor.document().selection().edges.size() == 195585, "frame refresh preserves complete edge selection");
	ok &= expect(resets == 2 && table->selectionModel()->selection().size() == 1 &&
					 table->model()->data(table->model()->index(0, 4)).toInt() == 1,
				 "edge table keeps its ordered topology and compact selection");
	capture(QStringLiteral("edges"));
	measure("clear-edges", [&] { table->clearSelection(); });
	auto *axes = editor.findChild<QComboBox *>(QStringLiteral("meshTransformSpace"));
	if (!expect(axes != nullptr, "transform axes control exists")) return EXIT_FAILURE;
	measure("selection-axes-with-no-components", [&] { axes->setCurrentIndex(1); });
	measure("selection-axes-last-face", [&] {
		mode->setCurrentIndex(0);
		viewport->trianglePicked(0, mesh.triangleCount - 1, int(ModelViewportPick::Replace));
	});
	ok &= expect(editor.document().selection().faces == QSet<int>{mesh.triangleCount - 1}, "last face supplies selection axes after a complete scan");
	measure("selection-axes-frame", [&] { frame->setCurrentIndex(0); });
	measure("custom-axes", [&] { axes->setCurrentIndex(2); });
	ok &= expect(!editor.document().isModified(), "selection and view operations leave the source clean");
	editor.close();
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
