#include "app/application_shell.h"
#include "app/model_editor_dialog.h"
#include "app/model_preview_worker.h"
#include "app/model_viewport.h"
#include "app/studio_theme.h"
#include "app/ui_primitives.h"
#include "core/model_archive.h"
#include "core/model_document.h"
#include "core/studio_settings.h"
#include "tests/model_animation_test_helpers.h"
#include "tests/model_skin_source_test_helpers.h"
#include "tests/model_viewport_test_helpers.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <QTreeWidget>
#include <algorithm>
#include <cstdlib>
#include <iostream>

using namespace vibestudio;
namespace
{
class ExpandedLabels final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char *context, const char *source, const char *, int) const override
	{
		if (!QByteArray(context).endsWith("ApplicationShell"))
		{
			return {};
		}
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 2, '~'));
	}
};
bool expect(bool value, const char *message)
{
	if (!value)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return value;
}
bool waitFor(const std::function<bool()> &ready, int timeout = 15000)
{
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < timeout)
	{
		QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
		QThread::msleep(1);
	}
	return ready();
}
void integer(QByteArray &bytes, int at, qint32 value) { qToLittleEndian(value, bytes.data() + at); }
bool write(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
bool capture(QWidget &widget, const QString &name)
{
	QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
	const auto directory = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (directory.isEmpty())
	{
		return true;
	}
	QImage image(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget.devicePixelRatioF());
	image.fill(Qt::transparent);
	widget.render(&image);
	return image.save(QDir(directory).filePath(name + ".png"));
}
QByteArray largeSkinMdl()
{
	const auto source = tests::groupedMdlFixture().bytes;
	auto bytes = source.left(84);
	integer(bytes, 48, 1);
	integer(bytes, 52, 1024);
	integer(bytes, 56, 1024);
	tests::mdlInteger(bytes, 0);
	bytes += QByteArray(1024 * 1024, '\1');
	// Two original grouped skin records occupy bytes 84..199.
	bytes += source.mid(200);
	return bytes;
}
QByteArray excessiveMdlGroups()
{
	// One native group expands to 4,195,328 vertices: the header alone only
	// promises 1,024. The decoder must reject before allocating pose geometry.
	auto bytes = tests::groupedMdlFixture().bytes.left(84);
	integer(bytes, 48, 0);
	integer(bytes, 60, 1024);
	integer(bytes, 64, 1);
	integer(bytes, 68, 1);
	bytes += QByteArray(1024 * 12, '\0');
	for (int value : {1, 0, 1, 2, 1, 4097})
	{
		tests::mdlInteger(bytes, value);
	}
	bytes += QByteArray(8, '\0');
	for (int f = 0; f < 4097; ++f)
	{
		tests::mdlNumber(bytes, float(f + 1));
	}
	bytes += QByteArray((24 + 1024 * 4) * 4097, '\0');
	return bytes;
}
QByteArray excessiveMd3Surfaces()
{
	// Two 300-vertex surfaces provide one pose each, padded to 8,192 model
	// frames. Individually legal, their combined expansion exceeds the cap.
	QByteArray bytes(108 + 8192 * 56, '\0');
	bytes.replace(0, 4, "IDP3");
	integer(bytes, 4, 15);
	integer(bytes, 76, 8192);
	integer(bytes, 84, 2);
	integer(bytes, 92, 108);
	integer(bytes, 100, bytes.size());
	for (int s = 0; s < 2; ++s)
	{
		QByteArray surface(108 + 300 * 16, '\0');
		surface.replace(0, 4, "IDP3");
		integer(surface, 72, 1);
		integer(surface, 80, 300);
		integer(surface, 88, 108);
		integer(surface, 92, 108);
		integer(surface, 96, 108);
		integer(surface, 100, 108 + 300 * 8);
		integer(surface, 104, surface.size());
		bytes += surface;
	}
	integer(bytes, 104, bytes.size());
	return bytes;
}
bool cancellation(const QString &name, const QByteArray &bytes)
{
	bool ok = true;
	int checkpoints = 0;
	ModelWorkControl count;
	count.progress = [&](ModelWorkPhase, qint64, qint64) { ++checkpoints; };
	const auto complete = decodeModelMesh(name, bytes, nullptr, count);
	ok &= expect(complete.geometryAvailable && complete.error.isEmpty() && checkpoints >= 8, "native fixture exercises decode loops");
	const QSet<int> stops{1, 2, 3, 5, checkpoints / 4, checkpoints / 2, checkpoints - 2, checkpoints - 1, checkpoints};
	for (int stop : stops)
	{
		int reached = 0;
		ModelWorkControl control;
		control.progress = [&](ModelWorkPhase, qint64, qint64) { ++reached; };
		control.cancelled = [&] { return reached >= stop; };
		const auto result = decodeModelMesh(name, bytes, nullptr, control);
		ok &= expect(reached == stop && result.error.contains("cancel", Qt::CaseInsensitive) && !result.geometryAvailable &&
						 result.surfaces.isEmpty() && result.frames.isEmpty() && result.embeddedSkins.isEmpty() && result.tags.isEmpty(),
					 "cancellation within decoding or bounds work publishes no partial geometry, skins, tags or frames");
	}
	std::cout << name.toStdString() << ": " << checkpoints << " checkpoints, " << stops.size() << " cancellation boundaries\n";
	return ok;
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
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath("mesh-native-preview-XXXXXX"));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	const auto path = [&](const char *name) { return QDir(temporary.path()).filePath(QString::fromLatin1(name)); };
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
	QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, temporary.path());
	StudioSettings::setOverrideFilePath(path("settings.ini"));
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	bool ok = true;
	QString error;
	auto source = tests::animationFixture();
	source.tags.erase(std::remove_if(source.tags.begin(), source.tags.end(), [](const ModelTag &tag) { return tag.name != "tag_mount"; }),
					  source.tags.end());
	source.surfaces[0].skinPaths = {"models/red.pcx"};
	source.surfaces[1].skinPaths = {"models/blue.png"};
	updateEditableModelMetadata(&source);
	const auto md3 = exportEditableModel(source, "md3", 0, &error);
	auto single = source;
	single.tags.clear();
	single.surfaces.resize(1);
	updateEditableModelMetadata(&single);
	const auto md2 = exportEditableModel(single, "md2", 0, &error);
	const auto mdl = tests::groupedMdlFixture().bytes;
	ok &= expect(!md3.isEmpty() && !md2.isEmpty(), "native interchange fixtures export");
	for (const auto &format : {QString("md2"), QString("md3")})
	{
		auto heavy = format == "md2" ? single : source;
		for (auto &surface : heavy.surfaces)
		{
			surface.triangles.fill(surface.triangles.first(), 1900);
		}
		updateEditableModelMetadata(&heavy);
		ok &= cancellation("heavy." + format, exportEditableModel(heavy, format, 0, &error));
	}
	ok &= cancellation("skin.mdl", largeSkinMdl());
	const auto expanded = decodeModelMesh("expanded.mdl", excessiveMdlGroups());
	ok &= expect(expanded.error.contains("frame vertices") && expanded.surfaces.isEmpty(), "MDL cap applies after native groups expand");
	const auto aggregate = decodeModelMesh("aggregate.md3", excessiveMd3Surfaces());
	ok &= expect(aggregate.error.contains("total frame vertices") && aggregate.frames.isEmpty() && aggregate.surfaces.isEmpty(),
				 "MD3 aggregate cap includes surfaces padded to the model frame count");
	{
		tests::SkinReader reader;
		reader.add("fixture.md3", md3);
		for (int scenario = 0; scenario < 5; ++scenario)
		{
			reader.metadata[0].sizeBytes = scenario == 0 ? modelFileByteLimit + 1
														 : md3.size() + (scenario == 1	 ? -1
																		 : scenario == 2 ? 1
																						 : 0);
			reader.lateFailure = scenario == 3;
			ModelWorkControl control;
			bool cancel = false;
			control.progress = [&](ModelWorkPhase phase, qint64 done, qint64)
			{ cancel |= scenario == 4 && phase == ModelWorkPhase::Reading && done > 0; };
			control.cancelled = [&] { return cancel; };
			const auto result = decodeModelMeshFromArchive(reader, "fixture.md3", {}, control);
			ok &= expect(!result.error.isEmpty() && result.surfaces.isEmpty(),
						 "native archive read refuses size, verification and cancellation failures");
		}
		reader.lateFailure = false;
		reader.metadata[0].sizeBytes = md3.size();
		reader.add("FIXTURE.md3", md3);
		ok &= expect(decodeModelMeshFromArchive(reader, "fixture.md3").error.contains("repeated"),
					 "native paths cannot choose an arbitrary occurrence");
		QByteArray retained("retained");
		ModelArchiveReader bounded(reader);
		ok &=
			expect(!bounded.readEntryAt(0, &retained, &error, 16) && retained == "retained", "failed bounded read preserves caller bytes");
	}
	QDir().mkpath(path("assets/models"));
	QDir().mkpath(path("assets/gfx"));
	QImage red(16, 16, QImage::Format_RGB32), blue(16, 16, QImage::Format_RGB32);
	red.fill(Qt::red);
	blue.fill(Qt::blue);
	QByteArray palette(768, '\0');
	for (int i = 0; i < 256; ++i)
	{
		palette[i * 3] = char(i);
		palette[i * 3 + 1] = char(255 - i);
	}
	ok &= expect(write(path("assets/models/fixture.md3"), md3) && write(path("assets/models/fixture.md2"), md2) &&
					 write(path("assets/models/fixture.mdl"), mdl) && write(path("assets/gfx/palette.lmp"), palette) &&
					 red.save(path("assets/models/red.png")) && blue.save(path("assets/models/blue.png")),
				 "write native geometry and independent palette/material fixtures");
	QByteArray mdc(108, '\0');
	mdc.replace(0, 4, "IDPC");
	integer(mdc, 4, 2);
	integer(mdc, 76, 7);
	integer(mdc, 84, 2);
	ok &= expect(write(path("assets/models/header.mdc"), mdc), "write metadata-only fixture");
	auto archive = std::make_shared<PackageArchive>();
	ok &= expect(archive->load(path("assets"), &error), "load immutable native snapshot");
	{
		ModelPreviewWorker worker;
		QVector<ModelPreviewResult> results;
		worker.completed = [&](const auto &result) { results << result; };
		for (const auto &suffix : {QString("mdl"), QString("md2"), QString("md3"), QString("mdc")})
		{
			results.clear();
			const auto name = suffix == "mdc" ? "models/header.mdc" : "models/fixture." + suffix;
			worker.request({archive, "native", "quake"}, name, suffix);
			ok &= expect(waitFor([&] { return !worker.busy(); }) && results.size() == 1 && results[0].error.isEmpty() &&
							 !results[0].metadata.assetRawLines.isEmpty(),
						 "native worker returns geometry and raw header metadata");
			if (results.size() != 1)
			{
				continue;
			}
			const auto &r = results[0];
			if (suffix == "mdl")
			{
				ok &= expect(r.mesh.mdl.palette == palette && !r.mesh.mdl.paletteGenerated && r.mesh.mdl.frameGroups.size() == 2 &&
								 r.mesh.embeddedSkins.size() == 2 && r.mesh.embeddedSkins[0].indexedFrames.size() == 2 &&
								 r.assets.readyCount() == 1 && r.assets.materials[0].image.pixelColor(1, 0) == QColor(1, 254, 0),
							 "worker retains exact native groups, indices and package palette");
			}
			else if (suffix == "mdc")
			{
				ok &= expect(!r.mesh.geometryAvailable && r.mesh.frameCount == 7 && r.mesh.surfaceCount == 2 && !r.mesh.warnings.isEmpty(),
							 "metadata-only files keep declared counts without pretending geometry is available");
			}
			else
			{
				ok &= expect(r.mesh.frameCount == 3 && r.assets.readyCount() == (suffix == "md3" ? 2 : 1) &&
								 r.assets.materials[0].imagePath == "models/red.png",
							 "native per-surface images retain alternate extension lookup");
				if (suffix == "md3")
				{
					ok &= expect(r.mesh.tags.size() == 3 && r.mesh.tags[2].origin.z == 19, "worker retains attachment poses");
				}
			}
		}
		results.clear();
		auto slow = std::make_shared<tests::SkinReader>();
		slow->add("slow.mdl", mdl);
		slow->delay = true;
		worker.request({slow, "retired", "quake"}, "slow.mdl", "old");
		ok &= expect(waitFor([&] { return slow->entered.load(); }), "slow native read starts away from widgets");
		worker.request({archive, "latest", "quake"}, "models/fixture.md3", "latest");
		ok &= expect(waitFor([&] { return !worker.busy(); }) && results.size() == 1 && results[0].key == "latest" && !slow->readOnGui,
					 "latest native selection replaces retired snapshot without GUI I/O");
		results.clear();
		slow->entered = false;
		worker.request({slow, "cancel", "quake"}, "slow.mdl", "cancel");
		ok &= expect(waitFor([&] { return slow->entered.load(); }), "native cancellation read starts");
		worker.cancel();
		ok &= expect(waitFor([&] { return !worker.busy(); }) && results.size() == 1 && results[0].cancelled &&
						 results[0].mesh.frames.isEmpty(),
					 "native Cancel publishes one empty result and discards late completion");
		slow->entered = false;
		worker.request({slow, "closing", "quake"}, "slow.mdl", "closing");
		ok &= expect(waitFor([&] { return slow->entered.load(); }), "native worker destruction interrupts active read");
	}
	{
		const bool scaled = qEnvironmentVariableIntValue("QT_SCALE_FACTOR") == 2;
		ExpandedLabels expansion;
		if (scaled)
		{
			auto preferences = StudioSettings().accessibilityPreferences();
			preferences.theme = StudioTheme::HighContrastLight;
			preferences.textScalePercent = 200;
			preferences.reducedMotion = true;
			StudioSettings().setAccessibilityPreferences(preferences);
			app.installTranslator(&expansion);
		}
		ApplicationShell shell;
		if (scaled)
		{
			ok &= expect(shell.devicePixelRatioF() == 2, "native browser uses actual 2x device pixels");
			shell.setLayoutDirection(Qt::RightToLeft);
		}
		shell.resize(scaled ? 2200 : 1450, scaled ? 1400 : 960);
		shell.show();
		shell.openPathFromCommandLine(path("assets"));
		if (auto *action = shell.findChild<QAction *>("shell.mode.models"))
		{
			action->trigger();
		}
		auto *entries = shell.findChild<QListWidget *>("modelEntries");
		auto *viewport = shell.findChild<ModelViewport *>("modelViewport");
		auto *cancel = shell.findChild<QPushButton *>("cancelModelPreview");
		auto *edit = shell.findChild<QPushButton *>("openModelEditor");
		auto *exportFrame = shell.findChild<QPushButton *>("exportModelFrame");
		LoadingPane *state = nullptr;
		for (auto *frame : shell.findChildren<QFrame *>("loadingPane"))
		{
			if (frame->property("modelBrowserState").toBool())
			{
				state = dynamic_cast<LoadingPane *>(frame);
			}
		}
		ok &= expect(state && (!scaled || state->reducedMotion()),
					 "native loading pane retains shared styling and reduced motion preferences");
		ok &= expect(entries && viewport && cancel && edit && exportFrame && !cancel->accessibleName().isEmpty() &&
						 cancel->focusPolicy() != Qt::NoFocus,
					 "native browser exposes accessible cancellation");
		if (entries && viewport && cancel && edit && exportFrame)
		{
			const auto select = [&](const QString &name)
			{
				for (int i = 0; i < entries->count(); ++i)
				{
					if (entries->item(i)->data(Qt::UserRole).toString() == name)
					{
						entries->setCurrentRow(i);
						entries->itemClicked(entries->item(i));
						return true;
					}
				}
				return false;
			};
			ok &= expect(select("models/fixture.md3") && cancel->isVisible() && !exportFrame->isEnabled() && state && state->isVisible() &&
							 state->state() == OperationState::Loading,
						 "native loading clears stale geometry and disables export");
			ok &= expect(shell.rect().contains(QRect(cancel->mapTo(&shell, QPoint()), cancel->size())),
						 "Cancel Preview stays inside the window under scaling and label expansion");
			if (auto *action = shell.findChild<QAction *>("shell.mode.models")) { action->trigger(); }
			ok &= expect(cancel->isVisible() && state && state->isVisible() && state->state() == OperationState::Loading,
						 "returning to Models keeps the current preview request and loading state");
			edit->click();
			ok &= expect(!shell.findChild<QDialog *>("modelEditorDialog"),
						 "opening while native preview is pending cannot adopt a placeholder");
			cancel->click();
			ok &= expect(!viewport->hasMesh() && !cancel->isVisible() && state && state->isVisible() &&
							 state->state() == OperationState::Cancelled,
						 "real native Cancel retires pending work and leaves a visible cancellation state");
			ok &= expect(capture(shell, "native-cancelled"), "capture visible native cancellation and retry guidance");
			if (auto *action = shell.findChild<QAction *>("shell.mode.models")) { action->trigger(); }
			ok &= expect(!viewport->hasMesh() && !cancel->isVisible() && state && state->isVisible() &&
							 state->state() == OperationState::Cancelled,
						 "returning to Models preserves cancellation until the selected model is retried");
			ok &= expect(select("models/fixture.mdl") && select("models/fixture.md2") && select("models/fixture.md3") &&
							 waitFor([&] { return viewport->mesh().sourcePath == "models/fixture.md3" && viewport->hasMesh(); }),
						 "rapid native selection shows only the latest model");
			ok &= expect(viewport->mesh().frameCount == 3 && viewport->mesh().tagCount == 1 && viewport->hasSkin() &&
							 exportFrame->isEnabled(),
						 "native browser keeps frames, attachments, skin rendering and export enablement");
			viewport->setFrame(2);
			ok &= expect(viewport->frame() == 2, "native browser can step to another stored pose");
			ok &= expect(tests::settleModelViewport(*viewport) && capture(shell, "native-browser"),
						 "render native browser evidence from Qt widgets");
			edit->click();
			auto *editor = static_cast<ModelEditorDialog *>(shell.findChild<QDialog *>("modelEditorDialog"));
			ok &= expect(editor && editor->document().mesh().tags.size() == 3 && editor->document().mesh().frames.size() == 3 &&
							 editor->document().mesh().surfaces.size() == 2,
						 "Edit Mesh adopts the completed native model with every pose and surface");
			if (editor)
			{
				editor->close();
			}
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
			ok &= expect(select("models/header.mdc") && waitFor([&] { return viewport->mesh().format == ModelMeshFormat::Mdc; }) &&
							 !exportFrame->isEnabled(),
						 "metadata-only selection cannot export fabricated geometry");
			const QString metadataDetail = state ? state->detail() : QString();
			ok &= expect(state && state->isVisible() && state->state() == OperationState::Warning && !metadataDetail.isEmpty(),
						 "completed metadata preview explains the unavailable geometry");
			edit->click();
			ok &= expect(!shell.findChild<QDialog *>("modelEditorDialog"),
						 "metadata-only selection cannot open a placeholder cube");
			ok &= expect(state && state->isVisible(), "metadata-only selection keeps its explanation visible");
			ok &= expect(state && state->state() == OperationState::Warning,
						 "metadata-only selection reports unavailable geometry as a warning");
			if (auto *action = shell.findChild<QAction *>("shell.mode.textures")) { action->trigger(); }
			if (auto *action = shell.findChild<QAction *>("shell.mode.models")) { action->trigger(); }
			ok &= expect(state && state->isVisible() && state->state() == OperationState::Warning &&
							 state->detail() == metadataDetail && !cancel->isVisible(),
						 "leaving and returning to Models preserves the selected metadata warning without decoding again");
			ok &= expect(capture(shell, "native-metadata"), "capture the retained metadata warning");
		}
		shell.close();
	}
	StudioSettings::setOverrideFilePath({});
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
