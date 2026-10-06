#include "app/application_shell.h"
#include "app/model_design_dialog.h"
#include "app/model_editor_dialog.h"
#include "app/model_assembly_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFont>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdlib>
#include <iostream>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

using namespace vibestudio;
namespace
{
bool expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return condition;
}
} // namespace
int main(int argc, char **argv)
{
#if defined(_MSC_VER) && defined(_DEBUG)
	for (int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT})
	{
		_CrtSetReportMode(type, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
	}
#endif
	// Public widget signals only: this does not inject user input or capture the screen.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont("Segoe UI", 10));
#endif
	QElapsedTimer elapsed;
	elapsed.start();
	const auto timing = [&](const char *stage)
	{
		if (qEnvironmentVariableIsSet("VIBESTUDIO_MODELLER_TIMING"))
		{
			std::cout << "TIMING " << stage << ' ' << elapsed.elapsed() << " ms" << std::endl;
		}
	};
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
	{
		return EXIT_FAILURE;
	}
	QTemporaryDir temporary(QDir(root).filePath("mesh-shell-XXXXXX"));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	bool ok = true;
	QString error;
	ModelDesign design;
	ModelDesignPart part;
	part.primitive = QStringLiteral("plane");
	design.parts << part;
	const auto plane = buildModelDesignMesh(design);
	{
		// Match main.cpp: apply the saved theme before constructing the shell.
		const auto preferences = StudioSettings().accessibilityPreferences();
		applyStudioTheme(app, studioThemeTokens(preferences.theme, preferences.density, preferences.textScalePercent));
		timing("shell-construction-start");
		ApplicationShell shell;
		timing("shell-construction-done");
		shell.show();
		auto *assemblyButton = shell.findChild<QPushButton *>(QStringLiteral("openModelAssembly"));
		ok &= expect(assemblyButton != nullptr, "Models exposes linked assembly authoring");
		if (assemblyButton) { assemblyButton->click(); }
		auto *assembly = static_cast<ModelAssemblyDialog *>(shell.findChild<QDialog *>(QStringLiteral("modelAssemblyDialog")));
		ok &= expect(assembly && bool(assembly->editBakedPose), "assembly provides the ordinary mesh editor handoff");
		if (assembly) { assembly->close(); }
		app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
		auto *open = shell.findChild<QPushButton *>(QStringLiteral("openModelEditor"));
		ok &= expect(open != nullptr, "Models exposes an editor action");
		if (open)
		{
			open->click();
		}
		app.processEvents();
		ok &= expect(shell.findChild<QDialog *>(QStringLiteral("modelEditorDialog")) != nullptr,
					 "Models opens the mesh editor through the shell");
		timing("shell-editor-open");
		if (auto *editor = shell.findChild<QDialog *>(QStringLiteral("modelEditorDialog")))
		{
			editor->close();
		}
		app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
		auto *designButton = shell.findChild<QPushButton *>(QStringLiteral("openModelDesigner"));
		ok &= expect(designButton != nullptr, "Models exposes primitive-to-mesh authoring");
		if (designButton)
		{
			designButton->click();
		}
		QPointer<ModelDesignDialog> designer =
			static_cast<ModelDesignDialog *>(shell.findChild<QDialog *>(QStringLiteral("modelDesignDialog")));
		ok &= expect(designer && bool(designer->editMesh), "primitive designer provides the shared mesh handoff");
		if (designer && designer->editMesh && open)
		{
			auto busyMesh = plane;
			busyMesh.surfaces[0].triangles.fill(plane.surfaces[0].triangles[0], 4096);
			updateEditableModelMetadata(&busyMesh);
			bool reentered = false;
			QTimer::singleShot(0, &shell,
							   [&]
							   {
								   auto *preparing =
									   static_cast<ModelEditorDialog *>(shell.findChild<QDialog *>(QStringLiteral("modelEditorDialog")));
								   reentered = preparing && preparing->operationBusy();
								   open->click();
								   ok &= expect(shell.findChildren<QDialog *>(QStringLiteral("modelEditorDialog")).size() == 1,
												"shell reentry finds the modeller before initial preparation finishes");
							   });
			designer->editMesh(busyMesh);
			timing("shell-baked-mesh-open");
			QPointer<ModelEditorDialog> editor =
				static_cast<ModelEditorDialog *>(shell.findChild<QDialog *>(QStringLiteral("modelEditorDialog")));
			ok &= expect(reentered && editor && editor->document().mesh().triangleCount == 4096,
						 "baked geometry is prepared and adopted through the registered editor");
			if (editor)
			{
				bool deferred = false;
				QTimer::singleShot(0, &shell, [&] { deferred = !shell.close() && editor && editor->operationBusy(); });
				ModelEdit normals;
				normals.kind = ModelEditKind::RecalculateNormals;
				ok &= expect(!editor->applyEdit(normals, &error) && deferred,
							 "shell close cancels document work before closing other authoring dialogs");
				for (int step = 0; step < 4; ++step)
				{
					app.processEvents();
					app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
				}
				ok &= expect(!editor && !designer && !shell.isVisible(),
							 "one studio-close request resumes after mesh work and closes the other surfaces");
			}
			if (designer)
			{
				designer->close();
			}
			app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
		}
	}
	StudioSettings::setOverrideFilePath(QString());
	timing("complete");
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
