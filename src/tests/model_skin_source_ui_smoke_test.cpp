#include "app/model_editor_dialog.h"
#include "app/model_skin_source_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"
#include "tests/model_skin_source_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>

#include <cstdlib>
#include <iostream>

using namespace vibestudio;
namespace
{
bool expect(bool value, const char *message)
{
	if (!value)
	{
		std::cerr << "FAIL: " << message << '\n';
	}
	return value;
}
QByteArray source(const ModelEditorDialog &editor)
{
	return QJsonDocument(editableModelJson(editor.document().mesh())).toJson(QJsonDocument::Compact);
}
void selectRow(QTableView *table, int row)
{
	const auto index = table->model()->index(row, 0);
	table->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}
class Expansion final : public QTranslator
{
  public:
	bool isEmpty() const override { return false; }
	QString translate(const char *context, const char *text, const char *, int) const override
	{
		if (QByteArray(context) != "VibeStudioModelSkinSource")
		{
			return {};
		}
		const auto value = QString::fromUtf8(text);
		return QStringLiteral("[%1%2]").arg(value, QString(value.size() / 2, '~'));
	}
};
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
	QTemporaryDir temporary(QDir(root).filePath("mesh-skin-source-ui-XXXXXX"));
	if (!temporary.isValid())
	{
		return EXIT_FAILURE;
	}
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath("settings.ini"));
	bool ok = true;
	QString error;
	auto reader = std::make_shared<tests::SkinReader>();
	reader->add("textures/skin.lmp", tests::skinLump());
	reader->add("textures/skin.lmp", tests::skinLump(43));
	reader->add("textures/other.lmp", tests::skinLump(77));
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		Expansion expansion;
		if (scenario)
		{
			app.installTranslator(&expansion);
		}
		applyStudioTheme(app, studioThemeTokens(scenario == 0	? StudioTheme::Dark
												: scenario == 1 ? StudioTheme::HighContrastLight
																: StudioTheme::HighContrastDark,
												UiDensity::Standard, scenario ? 200 : 100));
		ModelSkinSourceDialog picker(reader->entries(), 1, 0, .25);
		picker.setLayoutDirection(scenario == 1 ? Qt::RightToLeft : Qt::LeftToRight);
		picker.resize(scenario ? 1560 : 820, scenario ? 780 : 530);
		picker.show();
		app.processEvents();
		auto *table = picker.findChild<QTableView *>("meshSkinSourceEntries");
		auto *filter = picker.findChild<QLineEdit *>("meshSkinSourceFilter");
		auto *operation = picker.findChild<QComboBox *>("meshSkinSourceOperation");
		auto *import = picker.findChild<QPushButton *>("meshSkinSourceImport");
		if (!expect(table && filter && operation && import, "package picker controls exist"))
		{
			return EXIT_FAILURE;
		}
		ok &= expect(!import->isEnabled() && picker.reference().entryIndex == -1 && !reader->entered,
					 "metadata-only picker starts without payload I/O or an implicit selection");
		filter->setText("skin.lmp");
		selectRow(table, 1);
		if (table->model()->rowCount() != 2 || picker.reference().entryIndex != 1 || picker.reference().path != "textures/skin.lmp" ||
			!import->isEnabled())
		{
			std::cerr << "picker scenario=" << scenario << " rows=" << table->model()->rowCount()
					  << " entry=" << picker.reference().entryIndex << " path=" << picker.reference().path.toStdString()
					  << " enabled=" << import->isEnabled() << '\n';
		}
		ok &= expect(table->model()->rowCount() == 2 && picker.reference().entryIndex == 1 &&
						 picker.reference().path == "textures/skin.lmp" && import->isEnabled(),
					 "filtering preserves exact repeated-name occurrence");
		operation->setCurrentIndex(2);
		ok &= expect(picker.kind() == ModelEditKind::AppendMdlSkinMember, "picker carries an explicit append operation");
		filter->setText("missing");
		ok &= expect(!import->isEnabled() && picker.reference().entryIndex == -1, "filter removing selection disables import");
		filter->setText("skin");
		selectRow(table, 1);
		app.processEvents();
		for (auto *control : QList<QWidget *>{table, filter, operation, import})
		{
			const auto *accessible = QAccessible::queryAccessibleInterface(control);
			ok &= expect(accessible && !accessible->text(QAccessible::Name).isEmpty() && !control->accessibleDescription().isEmpty() &&
							 control->focusPolicy() != Qt::NoFocus,
						 "picker controls expose native accessible names and keyboard focus");
			ok &= expect(picker.rect().contains(QRect(control->mapTo(&picker, QPoint()), control->size())),
						 "controls fit normal, expanded and RTL layouts");
		}
		const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
		if (!evidence.isEmpty())
		{
			QImage image(picker.size() * picker.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
			image.setDevicePixelRatio(picker.devicePixelRatioF());
			image.fill(Qt::transparent);
			picker.render(&image);
			ok &= expect(
				image.save(QDir(evidence).filePath(QString("skin-source-picker-%1-%2x.png").arg(scenario).arg(picker.devicePixelRatioF()))),
				"save widget-rendered picker evidence");
		}
		if (qEnvironmentVariableIntValue("QT_SCALE_FACTOR") >= 2)
		{
			ok &= expect(picker.devicePixelRatioF() >= 1.99, "2x run actually uses 2x pixels");
		}
		if (scenario)
		{
			app.removeTranslator(&expansion);
		}
	}
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	ModelEditorDialog editor;
	const auto mesh = decodeModelMesh("fixture.mdl", tests::groupedMdlFixture().bytes);
	ok &= expect(editor.setMesh(mesh, &error), "open native skin target");
	auto *button = editor.findChild<QPushButton *>("importMeshPackageSkin");
	auto *slot = editor.findChild<QComboBox *>("meshMdlSkin");
	auto *member = editor.findChild<QComboBox *>("meshMdlMember");
	auto *duration = editor.findChild<QDoubleSpinBox *>("meshMdlSkinDuration");
	auto *undo = editor.findChild<QAction *>("undoMesh");
	if (!expect(button && slot && member && duration && undo, "inspector package handoff is registered"))
	{
		return EXIT_FAILURE;
	}
	ok &= expect(!button->isEnabled(), "no package means unavailable import");
	editor.setMaterialSource({reader, "first", "quake"});
	ok &= expect(button->isEnabled(), "current package enables import without reopening modeller");
	const auto before = source(editor);
	QTimer::singleShot(0, &editor,
					   [&]
					   {
						   auto *picker = editor.findChild<QDialog *>("meshSkinSourceDialog");
						   if (!picker)
						   {
							   ok = false;
							   return;
						   }
						   selectRow(picker->findChild<QTableView *>("meshSkinSourceEntries"), 1);
						   picker->findChild<QPushButton *>("meshSkinSourceImport")->click();
					   });
	button->click();
	ok &= expect(editor.document().mesh().embeddedSkins.size() == 3 && slot->currentIndex() == 2 &&
					 editor.document().mesh().embeddedSkins.last().indexedFrames[0] == tests::skinLump(43).mid(8) && !reader->readOnGui,
				 "inspector copies chosen occurrence on a worker and selects the new skin");
	undo->trigger();
	ok &= expect(source(editor) == before, "one undo restores complete native source");
	slot->setCurrentIndex(1);
	member->setCurrentIndex(0);
	ok &= expect(editor.importMdlSkinFromPackage({"", 0}, ModelEditKind::ReplaceMdlSkinMember, &error) &&
					 editor.document().mesh().embeddedSkins[1].indexedFrames[0] == tests::skinLump().mid(8),
				 "replace targets current slot and member");
	undo->trigger();
	slot->setCurrentIndex(1);
	duration->setValue(.25);
	ok &= expect(editor.importMdlSkinFromPackage({"", 0}, ModelEditKind::AppendMdlSkinMember, &error) && member->currentIndex() == 1 &&
					 editor.document().mesh().embeddedSkins[1].intervals == QVector<float>{.25f, .5f},
				 "append uses inspector duration and selects new member");
	undo->trigger();
	ok &= expect(source(editor) == before, "append undo preserves original single member");
	reader->lateFailure = true;
	ok &= expect(!editor.importMdlSkinFromPackage({"", 0}, ModelEditKind::AddMdlSkin, &error) && source(editor) == before &&
					 !editor.document().canUndo(),
				 "failed verified read adds no history");
	reader->lateFailure = false;
	reader->delay = true;
	QTimer::singleShot(30, &editor, [&] { editor.cancelOperation(); });
	ok &= expect(!editor.importMdlSkinFromPackage({"", 0}, ModelEditKind::AddMdlSkin, &error) && source(editor) == before &&
					 !editor.operationBusy(),
				 "cancel returns control without adopting worker results");
	reader->delay = false;
	QTimer::singleShot(0, &editor,
					   [&]
					   {
						   auto *picker = editor.findChild<QDialog *>("meshSkinSourceDialog");
						   if (!picker)
						   {
							   ok = false;
							   return;
						   }
						   selectRow(picker->findChild<QTableView *>("meshSkinSourceEntries"), 1);
						   editor.setMaterialSource({reader, "new revision", "quake"});
						   picker->findChild<QPushButton *>("meshSkinSourceImport")->click();
					   });
	button->click();
	ok &= expect(source(editor) == before && !editor.document().canUndo(), "changed package revision invalidates an open picker selection");
	editor.setMaterialSource({});
	ok &= expect(!button->isEnabled() && editor.setMesh(mesh, &error), "closed package disables handoff and retires test recovery");
	if (!ok)
	{
		std::cerr << error.toStdString() << '\n';
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
