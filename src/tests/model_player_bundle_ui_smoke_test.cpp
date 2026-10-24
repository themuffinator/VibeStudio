#include "app/model_assembly_dialog.h"
#include "app/model_skin_source_dialog.h"
#include "app/studio_theme.h"
#include "core/audio_clip.h"
#include "tests/model_player_bundle_test_helpers.h"
#include "tests/model_skin_source_test_helpers.h"

#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRawFont>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <QTreeWidget>

#include <array>
#include <iostream>

using namespace vibestudio;
using namespace vibestudio::tests;
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
		if (QByteArray(context) != "ModelPlayerBundleDialog" && QByteArray(context) != "VibeStudioModelSkinSource")
			return {};
		const auto text = QString::fromUtf8(source);
		return QStringLiteral("[%1%2]").arg(text, QString(text.size() / 2, '~'));
	}
};
bool render(QWidget &widget, const QString &name)
{
	const auto evidence = qEnvironmentVariable("VIBESTUDIO_MODELLER_EVIDENCE");
	if (evidence.isEmpty())
		return true;
	QImage image(widget.size() * widget.devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(widget.devicePixelRatioF());
	image.fill(Qt::transparent);
	widget.render(&image);
	return image.save(QDir(evidence).filePath(name + QStringLiteral("-%1x.png").arg(widget.devicePixelRatioF())));
}
} // namespace
int main(int argc, char **argv)
{
	std::cout << std::unitbuf;
	std::cerr << std::unitbuf;
	// Exercise owned widget APIs and render targets, without OS input or capture.
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
	QTemporaryDir temporary(QDir(root).filePath("player-bundle-ui-XXXXXX"));
	if (!temporary.isValid())
		return 1;
	StudioSettings::setOverrideFilePath(temporary.filePath("settings.ini"));
	QString error;
	PlayerBundleFixture fixture;
	bool ok = expect(fixture.create(temporary.path(), &error), "create original player fixtures");
	ok &= expect(QRawFont::fromFont(app.font()).supportsCharacter('A'), "widget renders have a usable font");
	if (!ok)
		return 1;
	const auto originalSource = q3Read(fixture.sourcePath), originalSkin = q3Read(temporary.filePath("lower.skin"));
	// Team skins from .skin files, and a custom sound.
	AudioClip sound;
	sound.channels = 1;
	sound.sampleRate = 22050;
	sound.samples.fill(.25f, 2205);
	ok &= expect(q3Write(temporary.filePath("upper_red_source.skin"), "body,textures/player/b.tga\n") &&
					 q3Write(temporary.filePath("head_blue_source.skin"), "body,textures/player/b.tga\n") &&
					 q3Write(temporary.filePath("death.wav"), encodeAudioWav(sound, &error)),
				 "write team skin and sound sources");
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		std::cout << "Player UI scenario " << scenario << ": open\n";
		Expansion expansion;
		if (scenario)
			app.installTranslator(&expansion);
		applyStudioTheme(app, studioThemeTokens(scenario == 0	? StudioTheme::Dark
												: scenario == 1 ? StudioTheme::HighContrastLight
																: StudioTheme::HighContrastDark,
												UiDensity::Standard, scenario ? 200 : 100));
		app.setLayoutDirection(scenario == 1 ? Qt::RightToLeft : Qt::LeftToRight);
		ModelAssemblyDialog editor;
		editor.setAttribute(Qt::WA_DeleteOnClose, false);
		editor.setAccessibility(scenario != 0, true);
		editor.resize(1180, 760);
		editor.show();
		app.processEvents();
		editor.findChild<QCheckBox *>("assemblyRecoveryEnabled")->setChecked(false);
		editor.setMaterialSource({fixture.context.archive, "player-fixture", {}});
		ok &= expect(editor.openSource(fixture.sourcePath, &error) && editor.previewReady(), "load native assembly with linked skin");
		auto *action = editor.findChild<QAction *>("assemblyPlayerBundle");
		ok &= expect(action && action->isEnabled(), "player package is reachable from assembly toolbar");
		if (!action)
			return 1;
		bool reviewed = false, picked = false;
		QTimer::singleShot(0, &editor, [&] {
			auto *dialog = dynamic_cast<QDialog *>(QApplication::activeModalWidget());
			if (!dialog || dialog->objectName() != "playerBundleDialog")
				return;
			dialog->resize(scenario ? 1100 : 740, scenario ? 1000 : 800);
			auto *name = dialog->findChild<QLineEdit *>("playerBundleName");
			auto *skin = dialog->findChild<QLineEdit *>("playerBundleSkin");
			auto *head = dialog->findChild<QComboBox *>("playerBundleHead");
			auto *kind = dialog->findChild<QComboBox *>("playerBundleIconKind");
			auto *icon = dialog->findChild<QLineEdit *>("playerBundleIcon");
			auto *index = dialog->findChild<QSpinBox *>("playerBundleIconIndex");
			auto *browse = dialog->findChild<QPushButton *>("playerBundleBrowseIcon");
			auto *review = dialog->findChild<QPushButton *>("playerBundleReview");
			auto *publish = dialog->findChild<QPushButton *>("playerBundleExport");
			auto *scroll = dialog->findChild<QScrollArea *>("playerBundleScroll");
			auto *files = dialog->findChild<QTreeWidget *>("playerBundleFiles");
			auto *details = dialog->findChild<QPlainTextEdit *>("playerBundleDetails");
			ok &= expect(name && skin && head && kind && icon && index && browse && review && publish && scroll && files && details,
						 "review surface exposes stable control identities");
			if (!ok)
			{
				dialog->reject();
				return;
			}
			for (QWidget *control : std::array<QWidget *, 9>{name, skin, head, kind, icon, index, browse, review, publish})
			{
				const auto *accessible = QAccessible::queryAccessibleInterface(control);
				ok &= expect(accessible && !accessible->text(QAccessible::Name).isEmpty() && !control->accessibleDescription().isEmpty() &&
								 control->focusPolicy() != Qt::NoFocus,
							 "package controls expose names, descriptions and normal focus navigation");
			}
			ok &= expect(!publish->isEnabled() && !index->isEnabled() && head->currentData() == "head",
						 "initial form chooses head and prevents unreviewed publication");
			name->setText("synthetic");
			icon->setText(fixture.iconPath);
			app.processEvents();
			scroll->ensureWidgetVisible(name);
			name->setFocus();
			app.processEvents();
			ok &= expect(name->hasFocus() && name->layoutDirection() == Qt::LeftToRight && icon->layoutDirection() == Qt::LeftToRight &&
							 index->layoutDirection() == Qt::LeftToRight,
						 "IDs, paths and indices retain focus and logical direction under RTL");
			ok &= expect(dialog->width() <= (scenario ? 1100 : 740) && render(*dialog, QStringLiteral("player-%1-options").arg(scenario)),
						 "scrollable form fits scaled and expanded labels");
			review->click();
			ok &= expect(publish->isEnabled() && files->topLevelItemCount() == 12 && details->toPlainText().contains("non-team"),
						 "actual Review action captures complete package and discloses target limits");
			for (int i = 0; i < files->topLevelItemCount(); ++i)
				ok &= expect(files->topLevelItem(i)->data(0, Qt::AccessibleDescriptionRole).toString().contains("SHA-256"),
							 "file identities are accessible without hover");
			ok &= expect(!QFileInfo::exists(temporary.filePath("synthetic.pk3")) && q3Read(fixture.sourcePath) == originalSource &&
							 q3Read(temporary.filePath("lower.skin")) == originalSkin,
						 "review creates no output and leaves sources unchanged");
			files->setCurrentItem(files->topLevelItem(0));
			ok &= expect(details->toPlainText().contains(files->topLevelItem(0)->text(0)) && details->toPlainText().contains("SHA-256") &&
							 files->textElideMode() == Qt::ElideMiddle,
						 "keyboard row selection exposes complete file path and fingerprint while long names preserve both ends");
			app.processEvents();
			scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
			app.processEvents();
			ok &= expect(render(*dialog, QStringLiteral("player-%1-review").arg(scenario)),
						 "review file list, hashes and limitations render at scale");
			// Team skins, more skins, detail levels and custom sounds.
			auto *team = dialog->findChild<QCheckBox *>("playerBundleTeam");
			auto *skins = dialog->findChild<QTableWidget *>("playerBundleSkins");
			auto *addSkin = dialog->findChild<QPushButton *>("playerBundleAddSkin");
			auto *removeSkin = dialog->findChild<QPushButton *>("playerBundleRemoveSkin");
			auto *detail = dialog->findChild<QComboBox *>("playerBundleDetail");
			auto *levels = dialog->findChild<QSpinBox *>("playerBundleLodLevels");
			auto *ratio = dialog->findChild<QDoubleSpinBox *>("playerBundleLodRatio");
			auto *sounds = dialog->findChild<QTableWidget *>("playerBundleSounds");
			auto *status = dialog->findChild<QLabel *>("playerBundleStatus");
			ok &= expect(team && skins && addSkin && removeSkin && detail && levels && ratio && sounds && status,
						 "skins, detail levels and sounds are part of the review surface");
			if (!ok)
			{
				dialog->reject();
				return;
			}
			for (QWidget *control : std::array<QWidget *, 8>{team, skins, addSkin, removeSkin, detail, levels, ratio, sounds})
				ok &= expect(!control->accessibleName().isEmpty() && !control->accessibleDescription().isEmpty() && control->focusPolicy() != Qt::NoFocus,
							 "the new package controls are named, described and focusable");
			ok &= expect(sounds->rowCount() == 13 && sounds->item(0, 0)->text() == "death1" && skins->rowCount() == 0 && !levels->isEnabled() &&
							 skins->layoutDirection() == Qt::LeftToRight && !skins->tabKeyNavigation() && !sounds->tabKeyNavigation(),
						 "the 13 custom sounds are listed and generated levels follow the detail choice");
			team->setChecked(true);
			review->click();
			ok &= expect(!publish->isEnabled() && files->topLevelItemCount() == 0 && status->text().contains("red"),
						 "team skins without red or blue materials are refused with the reason");
			const std::array<std::array<QString, 2>, 2> rows{{{"red", temporary.filePath("upper_red_source.skin")},
															 {"blue", temporary.filePath("head_blue_source.skin")}}};
			for (int row = 0; row < 2; ++row)
			{
				addSkin->click();
				skins->item(row, 0)->setText(rows[row][0]);
				skins->item(row, row == 0 ? 2 : 3)->setText(rows[row][1]);
			}
			detail->setCurrentIndex(1);
			levels->setValue(1);
			sounds->item(0, 1)->setText(temporary.filePath("death.wav"));
			ok &= expect(levels->isEnabled() && ratio->isEnabled() && skins->rowCount() == 2, "skin rows and generated levels can be set");
			review->click();
			ok &= expect(publish->isEnabled() && files->topLevelItemCount() == 24 && details->toPlainText().contains("Team games use the red and blue skins"),
						 "the review publishes three skins with icons, a level per part and the custom sound");
			scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum() / 2);
			app.processEvents();
			ok &= expect(render(*dialog, QStringLiteral("player-%1-skins").arg(scenario)), "skins, levels and sounds render at scale");
			team->setChecked(false);
			skins->setCurrentCell(1, 0);
			removeSkin->click();
			skins->setCurrentCell(0, 0);
			removeSkin->click();
			detail->setCurrentIndex(0);
			sounds->item(0, 1)->setText({});
			ok &= expect(skins->rowCount() == 0 && !levels->isEnabled() && !publish->isEnabled(), "skin rows are removed and changes invalidate the review");
			skin->setText("alternate");
			ok &= expect(!publish->isEnabled() && files->topLevelItemCount() == 0 && details->toPlainText().isEmpty(),
						 "editing an option invalidates reviewed bytes visibly");
			kind->setCurrentIndex(1);
			ok &= expect(index->isEnabled(), "package icon enables exact occurrence selector");
			QTimer::singleShot(0, dialog, [&] {
				auto *picker = dynamic_cast<ModelSkinSourceDialog *>(QApplication::activeModalWidget());
				if (!picker)
					return;
				picker->resize(scenario ? 1100 : 820, scenario ? 700 : 530);
				auto *table = picker->findChild<QTableView *>("meshSkinSourceEntries");
				picker->findChild<QLineEdit *>("meshSkinSourceFilter")->setText("textures/player/a.tga");
				app.processEvents();
				table->selectionModel()->setCurrentIndex(table->model()->index(0, 0),
														 QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
				app.processEvents();
				ok &= expect(!picker->findChild<QComboBox *>("meshSkinSourceOperation") && table->model()->rowCount() == 1 &&
								 render(*picker, QStringLiteral("player-%1-picker").arg(scenario)),
							 "image picker has exact metadata selection without mesh edit controls");
				auto *accept = picker->findChild<QPushButton *>("meshSkinSourceImport");
				ok &= expect(accept->isEnabled(), "selected readable image enables picker acceptance");
				if (!accept->isEnabled())
				{
					picker->reject();
					return;
				}
				picked = true;
				accept->click();
			});
			browse->click();
			std::cout << "Player UI scenario " << scenario << ": package image selected\n";
			ok &= expect(picked && icon->text() == "textures/player/a.tga" && index->value() >= 0,
						 "image picker returns exact package icon reference");
			review->click();
			ok &= expect(publish->isEnabled() && files->topLevelItemCount() == 12, "selected package image completes native player review");
			reviewed = true;
			dialog->reject();
		});
		action->trigger();
		std::cout << "Player UI scenario " << scenario << ": direct publish\n";
		ok &= expect(reviewed && picked, "complete real dialog review and image selection exercised");
		ModelPlayerBundle prepared;
		ok &= expect(editor.preparePlayerBundle(fixture.options, &prepared, &error), "public GUI workflow prepares reviewed package");
		PackageWriteReport written;
		const auto output = temporary.filePath(QStringLiteral("gui-player-%1.pk3").arg(scenario));
		ok &= expect(editor.exportPlayerBundle(prepared, output, false, &written, &error) && written.outputCommitted &&
						 written.determinismVerified && QFileInfo::exists(output),
					 "GUI publication uses verified atomic writer");
		editor.setMaterialSource({fixture.context.archive, "new-revision-same-archive", {}});
		ok &= expect(!editor.exportPlayerBundle(prepared, output, true, &written, &error) && !written.outputCommitted &&
						 !written.blockedMessages.isEmpty(),
					 "changed revision invalidates a review even when the immutable archive pointer is reused");
		editor.setMaterialSource({fixture.context.archive, "player-fixture", {}});
		auto changed = editor.document().assembly().parts[2];
		changed.translation.x += 1;
		ok &= expect(editor.applyPart("head", changed, &error) && !editor.exportPlayerBundle(prepared, output, true, &written, &error) &&
						 editor.undo(&error),
					 "changed assembly refuses stale player review and undo restores context");
		auto delayed = std::make_shared<SkinReader>();
		for (const auto &entry : fixture.context.archive->entries())
		{
			QByteArray bytes;
			if (entry.kind == PackageEntryKind::File && fixture.context.archive->readEntryBytes(entry.virtualPath, &bytes, &error))
				delayed->add(entry.virtualPath, bytes);
		}
		editor.setMaterialSource({delayed, "replacement-player-context", {}});
		std::cout << "Player UI scenario " << scenario << ": worker cancellation\n";
		ok &= expect(!editor.exportPlayerBundle(prepared, output, true, &written, &error),
					 "changed package context refuses stale player review");
		auto options = fixture.options;
		options.iconKind = ModelAssemblySource::Package;
		options.iconSource = "textures/player/a.tga";
		delayed->delay = true;
		delayed->readOnGui = false;
		bool busy = false;
		const auto fingerprint = modelAssemblyFingerprint(editor.document().assembly());
		const auto receipt = QJsonDocument(modelPlayerBundleJson(prepared)).toJson();
		QTimer::singleShot(20, &editor, [&] {
			busy = editor.operationBusy();
			editor.cancelOperation();
		});
		ok &= expect(!editor.preparePlayerBundle(options, &prepared, &error) && busy && !delayed->readOnGui &&
						 modelAssemblyFingerprint(editor.document().assembly()) == fingerprint &&
						 QJsonDocument(modelPlayerBundleJson(prepared)).toJson() == receipt,
					 "cancellable worker leaves assembly and prior review intact while GUI remains responsive");
		delayed->delay = false;
		std::cout << "Player UI scenario " << scenario << ": finish\n";
		ok &= expect(q3Read(fixture.sourcePath) == originalSource && q3Read(temporary.filePath("lower.skin")) == originalSkin,
					 "publishing and cancellation never rewrite authoring sources");
		if (scenario)
			app.removeTranslator(&expansion);
	}
	std::cout << checks << " player bundle GUI checks\n";
	if (!ok)
		std::cerr << qPrintable(error) << '\n';
	return ok ? 0 : 1;
}
