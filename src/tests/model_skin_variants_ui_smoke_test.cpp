// The Mesh Editor's Skin Variants…: named, described and keyboard-reachable
// controls; adding a variant and giving surfaces their own material in the
// working copy; the viewport preview leaving the document unchanged; Apply as
// one undo step; the Quake III .skin and Doom 3 skin exports through the core
// writers, including a Doom 3 refusal the review explains. Also under
// right-to-left layout and 200% text.
#include "app/model_editor_dialog.h"
#include "app/model_skin_variants_dialog.h"
#include "app/studio_theme.h"
#include "core/model_skin_bindings.h"
#include "core/model_skin_variants.h"
#include "core/studio_settings.h"
#include "tests/model_q3_animation_test_helpers.h"
#include "tests/render_test_support.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdlib>
#include <functional>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;

namespace
{
int failures = 0;
bool expect(bool condition, const char *message)
{
	if (!condition)
	{
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
	return condition;
}
void onNextDialog(QObject *context, std::function<void(ModelSkinVariantsDialog *)> handle)
{
	auto *timer = new QTimer(context);
	timer->setInterval(20);
	QObject::connect(timer, &QTimer::timeout, context, [timer, handle] {
		if (auto *dialog = dynamic_cast<ModelSkinVariantsDialog *>(QApplication::activeModalWidget()))
		{
			timer->stop();
			timer->deleteLater();
			handle(dialog);
		}
	});
	timer->start();
}
bool render(QWidget &widget, const QString &name)
{
	const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (evidence.isEmpty())
		return true;
	QImage image(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget.devicePixelRatioF());
	image.fill(Qt::transparent);
	widget.render(&image);
	return image.save(QDir(evidence).filePath(name + QStringLiteral(".png")));
}
ModelMesh prop()
{
	auto mesh = q3Model();
	mesh.tags.clear();
	auto body = mesh.surfaces[0];
	body.name = QStringLiteral("body_1");
	body.skinPaths = {QStringLiteral("models/prop/body.tga")};
	auto head = body;
	head.name = QStringLiteral("head");
	head.skinPaths = {QStringLiteral("models/prop/head.tga")};
	auto visor = body;
	visor.name = QStringLiteral("visor");
	mesh.surfaces = {body, head, visor};
	updateEditableModelMetadata(&mesh);
	return mesh;
}
int row(QTableWidget *table, const QString &surface)
{
	for (int i = 0; i < table->rowCount(); ++i)
		if (table->item(i, 0)->text() == surface)
			return i;
	return -1;
}

void checkDialog(ModelEditorDialog *editor, const QString &directory, int scenario)
{
	QString error;
	expect(editor->setMesh(prop(), &error), "open the prop");
	const auto revision = editor->document().revisionFingerprint();
	bool named = false, added = false, bound = false, previewKept = false, cleared = false, applied = false, q3 = false, refused = false,
		 doom3 = false, review = false;
	const auto q3Path = QDir(directory).filePath(QStringLiteral("prop_red.skin"));
	const auto doom3Path = QDir(directory).filePath(QStringLiteral("prop_red_d3.skin"));
	onNextDialog(editor, [&](ModelSkinVariantsDialog *dialog) {
		dialog->resize(scenario ? 1100 : 760, scenario ? 1000 : 720);
		auto *list = dialog->findChild<QListWidget *>(QStringLiteral("meshSkinVariantList"));
		auto *name = dialog->findChild<QLineEdit *>(QStringLiteral("meshSkinVariantName"));
		auto *material = dialog->findChild<QLineEdit *>(QStringLiteral("meshSkinVariantMaterial"));
		auto *modelPath = dialog->findChild<QLineEdit *>(QStringLiteral("meshSkinVariantModelPath"));
		auto *surfaces = dialog->findChild<QTableWidget *>(QStringLiteral("meshSkinVariantSurfaces"));
		auto *preview = dialog->findChild<QComboBox *>(QStringLiteral("meshSkinVariantPreview"));
		auto *findings = dialog->findChild<QPlainTextEdit *>(QStringLiteral("meshSkinVariantReview"));
		auto *add = dialog->findChild<QPushButton *>(QStringLiteral("meshSkinVariantAdd"));
		auto *set = dialog->findChild<QPushButton *>(QStringLiteral("meshSkinVariantSet"));
		auto *clear = dialog->findChild<QPushButton *>(QStringLiteral("meshSkinVariantClear"));
		auto *apply = dialog->findChild<QPushButton *>(QStringLiteral("meshSkinVariantApply"));
		auto *exportQ3 = dialog->findChild<QPushButton *>(QStringLiteral("meshSkinVariantExportQ3"));
		auto *exportDoom3 = dialog->findChild<QPushButton *>(QStringLiteral("meshSkinVariantExportDoom3"));
		const QList<QWidget *> controls{list, name, material, modelPath, surfaces, preview, findings, add, set, clear, apply, exportQ3, exportDoom3};
		named = !controls.contains(nullptr);
		for (auto *control : controls)
			named = named && !control->accessibleName().isEmpty() && !control->accessibleDescription().isEmpty() &&
					control->focusPolicy() != Qt::NoFocus;
		if (!named)
		{
			dialog->reject();
			return;
		}
		named = named && !add->isEnabled() && !apply->isEnabled() && list->count() == 0 && surfaces->rowCount() == 3 &&
				name->layoutDirection() == Qt::LeftToRight && material->layoutDirection() == Qt::LeftToRight && !surfaces->tabKeyNavigation();
		name->setText(QStringLiteral("default"));
		const bool reserved = !add->isEnabled();
		name->setText(QStringLiteral("red"));
		add->click();
		added = reserved && list->count() == 1 && list->currentItem() && list->currentItem()->text() == QStringLiteral("red") &&
				preview->count() == 2 && apply->isEnabled();
		surfaces->selectRow(row(surfaces, QStringLiteral("body_1")));
		material->setText(QStringLiteral("models/prop/body_red.tga"));
		set->click();
		surfaces->selectRow(row(surfaces, QStringLiteral("visor")));
		material->setText(QStringLiteral("models/prop/body_red.tga"));
		set->click();
		bound = dialog->variants().size() == 1 && dialog->variants()[0].bindings.size() == 2 &&
				surfaces->item(row(surfaces, QStringLiteral("body_1")), 2)->text() == QStringLiteral("models/prop/body_red.tga") &&
				!surfaces->item(row(surfaces, QStringLiteral("body_1")), 0)->data(Qt::AccessibleDescriptionRole).toString().isEmpty();
		review = findings->toPlainText().contains(QStringLiteral("\"body\",\"models/prop/body_red.tga\"")) &&
				 findings->toPlainText().contains(QStringLiteral("Doom 3"));
		preview->setCurrentIndex(1);
		QApplication::processEvents();
		previewKept = editor->document().revisionFingerprint() == revision;
		render(*dialog, QStringLiteral("skin-variants-%1").arg(scenario));
		// Doom 3 skins map materials: keeping visor while body_1 changes the
		// material they share is refused, and the review says why.
		surfaces->selectRow(row(surfaces, QStringLiteral("visor")));
		clear->click();
		cleared = dialog->variants()[0].bindings.size() == 1 && surfaces->item(row(surfaces, QStringLiteral("visor")), 2)->text().isEmpty() &&
				  findings->toPlainText().contains(QStringLiteral("visor"));
		refused = !dialog->exportSkin(doom3Path, true, &error) && error.contains(QStringLiteral("visor")) && !QFileInfo::exists(doom3Path);
		applied = dialog->apply(&error) && !apply->isEnabled() && editor->document().mesh().skinVariants == dialog->variants() &&
				  editor->document().canUndo();
		q3 = dialog->exportSkin(q3Path, false, &error);
		surfaces->selectRow(row(surfaces, QStringLiteral("visor")));
		material->setText(QStringLiteral("models/prop/body_red.tga"));
		set->click();
		modelPath->setText(QStringLiteral("models/md5/prop.md5mesh"));
		doom3 = dialog->exportSkin(doom3Path, true, &error);
		dialog->reject();
	});
	auto *open = editor->findChild<QPushButton *>(QStringLiteral("meshSkinVariants"));
	expect(open && open->isEnabled() && !open->accessibleDescription().isEmpty(), "Skin Variants… is reachable from the surface inspector");
	if (!open)
		return;
	open->click();
	expect(named, "the dialog's controls are named, described, focusable and left-to-right for paths");
	expect(added, "default is reserved and Add creates the red variant in the working copy");
	expect(bound, "Set Material binds the selected surfaces in the working copy and describes each row");
	expect(review, "the review shows the variant's Quake III .skin and Doom 3 declaration");
	expect(previewKept, "showing a variant in the viewport leaves the document unchanged");
	expect(cleared, "Use Primary returns a surface to its primary material and the review explains the Doom 3 conflict");
	expect(refused, "a Doom 3 skin that would change a kept surface is refused without writing");
	expect(applied, "Apply keeps the variants in the source as one undo step");
	ModelMesh red;
	QByteArray expected;
	expect(q3 && applyModelSkinVariant(editor->document().mesh(), editor->document().mesh().skinVariants[0], &red, &error) &&
			   writeModelQ3Skin(red, &expected, &error) && q3Read(q3Path) == expected,
		   "Export .skin writes the core writer's bytes");
	QVector<ModelDoom3Skin> skins;
	expect(doom3 && parseModelDoom3Skins(q3Read(doom3Path), &skins, &error) && skins.size() == 1 &&
			   skins[0].name == QStringLiteral("skins/models/md5/prop_red") && skins[0].models == QStringList{QStringLiteral("models/md5/prop.md5mesh")},
		   "Export Doom 3 Skin writes the working copy's declaration for the named model");
	expect(editor->document().mesh().skinVariants.size() == 1 && editor->document().mesh().skinVariants[0].bindings.size() == 1,
		   "closing without Apply keeps the applied list, not the later working copy");
	editor->findChild<QAction *>(QStringLiteral("undoMesh"))->trigger();
	expect(editor->document().mesh().skinVariants.isEmpty() && editor->document().revisionFingerprint() == revision,
		   "one undo removes the applied variants");
}
} // namespace

int main(int argc, char **argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	if (const int skip = vibestudio::test_support::exitCodeWithoutRenderer("model-skin-variants-ui-smoke"); skip >= 0)
		return skip;
	const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_TMP_ROOT");
	if (root.isEmpty() || !QDir().mkpath(root))
		return EXIT_FAILURE;
	QTemporaryDir temporary(QDir(root).filePath(QStringLiteral("skin-variants-ui-XXXXXX")));
	if (!temporary.isValid())
		return EXIT_FAILURE;
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath(QStringLiteral("settings.ini")));
	for (int scenario = 0; scenario < 2; ++scenario)
	{
		// The second pass mirrors the layout and doubles the text.
		app.setLayoutDirection(scenario ? Qt::RightToLeft : Qt::LeftToRight);
		applyStudioTheme(app, studioThemeTokens(scenario ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scenario ? 200 : 100));
		const auto directory = QDir(temporary.path()).filePath(QStringLiteral("pass%1").arg(scenario));
		QDir().mkpath(directory);
		auto *editor = new ModelEditorDialog;
		editor->resize(1400, 900);
		editor->show();
		QApplication::processEvents();
		checkDialog(editor, directory, scenario);
		delete editor;
	}
	if (failures > 0)
	{
		std::cerr << failures << " check(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "skin variant UI checks passed\n";
	return EXIT_SUCCESS;
}
