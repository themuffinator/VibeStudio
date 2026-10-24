#include "app/application_shell.h"
#include "app/map_viewport.h"
#include "app/studio_sidebar.h"
#include "app/studio_theme.h"
#include "tests/level_geometry_test_helpers.h"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace {
bool ok = true;
bool expect(bool value, const char* message, const QString& detail = {})
{
	if (!value) { ok = false; std::cerr << "FAIL: " << message << ": " << detail.toStdString() << std::endl; } return value;
}
bool until(const std::function<bool()>& ready)
{
	QElapsedTimer elapsed; elapsed.start();
	while (!ready() && elapsed.elapsed() < 30000) { QEventLoop events; QTimer::singleShot(20,&events,&QEventLoop::quit); events.exec(QEventLoop::ExcludeUserInputEvents); }
	return ready();
}
void settle() { for (int pass = 0; pass < 6; ++pass) { QApplication::processEvents(QEventLoop::ExcludeUserInputEvents); } }
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char*,const char* source,const char*,int) const override { return QStringLiteral("[~~%1~~~~]").arg(QString::fromUtf8(source)); }
};
}
int main(int argc, char** argv)
{
	// Owned native-widget properties, signals and QWidget render targets only.
	// No mouse/key events, OS input/capture, external application or game launch.
	qputenv("QT_QPA_PLATFORM","offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR",QDir(qEnvironmentVariable("SystemRoot")).filePath("Fonts").toLocal8Bit());
#endif
	QApplication app(argc,argv); QTemporaryDir temp; if (!temp.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temp.filePath("settings.ini")); StudioSettings settings;
	settings.setRestoreSession(false); settings.setReducedMotion(true); settings.setLevelViewLayoutPreference("four-views");
	LevelMapDocument fixture; QString error;
	if (!tests::createGeometryFixture(2,&fixture,&error)) { return 1; }
	const auto path = temp.filePath("layout.map"); const auto original = serializeLevelMap(fixture).bytes;
	QFile file(path); if (!file.open(QIODevice::WriteOnly) || file.write(original) != original.size()) { return 1; } file.close();
	for (const auto& profile : {QStringLiteral("trenchbroom"),QStringLiteral("netradiant-custom"),QStringLiteral("vibestudio-default")}) {
		for (int scale : {100,200}) {
			std::cout << "Transform layout " << profile.toStdString() << ' ' << scale << '%' << std::endl;
			settings.setSelectedEditorProfileId(profile); settings.setTextScalePercent(scale);
			settings.setTheme(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark); settings.sync();
			applyStudioTheme(app,studioThemeTokens(settings.accessibilityPreferences().theme,UiDensity::Standard,scale));
			Expanded expanded; if (scale == 200) { app.installTranslator(&expanded); app.setLayoutDirection(Qt::RightToLeft); }
			{
				ApplicationShell shell; shell.resize(1600,1000); shell.show(); shell.openPathFromCommandLine(path);
				shell.findChild<QAction*>("shell.mode.levels")->trigger();
				auto* view = shell.findChild<MapViewport*>("mapViewport");
				if (!expect(view && until([&] { return shell.levelDocument().sourcePath == path && view->isEnabled(); }),"map ready")) { return 1; }
				const QVector<LevelMapSelectionRef> chosen{{LevelMapSelectionKind::QuakeBrush,0}};
				view->setSelectionSet(chosen); view->selectionSetChanged(chosen);
				auto* sidebar = shell.findChild<StudioSidebar*>("levelTrailingSidebar");
				if (!expect(sidebar && sidebar->showPage("inspector"),"inspector accessible")) { return 1; }
				auto* page = sidebar->pageAt(sidebar->indexOfPage("inspector"));
				auto* scroll = page->findChild<QScrollArea*>("sidebarPageScroll");
				auto* panel = shell.findChild<QWidget*>("levelTransformPanel");
				QVector<QDoubleSpinBox*> fields;
				for (const auto* row : {"Centre","Size"}) { for (auto axis : {'X','Y','Z'}) {
					fields.append(shell.findChild<QDoubleSpinBox*>(QStringLiteral("levelTransform%1%2").arg(QLatin1String(row)).arg(QLatin1Char(axis))));
				} }
				if (!expect(panel && scroll && !fields.contains(nullptr),"native transform fields present")) { return 1; }
				if (scale == 200) { for (auto* field : fields) { field->setLocale(QLocale(QStringLiteral("ar_EG"))); } }
				const auto history = shell.levelDocument().undoStack.size();
				shell.activateWindow(); settle();
				auto* edit = fields[0]->findChild<QLineEdit*>(); fields[0]->setFocus(Qt::OtherFocusReason); settle();
				expect(fields[0]->hasFocus() || edit->hasFocus(),"owned editor receives focus before reflow");
				const auto pending = fields[0]->locale().toString(96.0,'f',2); edit->setText(pending); edit->setModified(true);
				for (int width : {320,420,560,1000,320}) {
					sidebar->setFixedWidth(width); settle();
					const auto detail = QStringLiteral("%1 scale %2 sidebar %3 panel %4").arg(profile).arg(scale).arg(width).arg(panel->width());
					const bool overflow = scroll->horizontalScrollBar()->maximum() > 0;
					expect(!overflow || width == 320,"ordinary sidebar widths fit without horizontal scrolling",detail);
					expect(panel->width() <= scroll->viewport()->width() || (overflow && scroll->horizontalScrollBar()->isVisible()),
						"extremely narrow panes retain a visible horizontal scroll fallback",detail);
					for (auto* field : fields) {
						const auto area = QRect(field->mapTo(panel,QPoint()),field->size());
						expect(panel->rect().contains(area),"editor stays within panel",detail);
						expect(field->width() >= field->sizeHint().width(),"editor retains space for its complete numeric range",detail+QStringLiteral(" field %1 width %2 minimum %3").arg(field->objectName()).arg(field->width()).arg(field->sizeHint().width()));
						expect(!field->accessibleName().isEmpty() && !field->accessibleDescription().isEmpty() && QAccessible::queryAccessibleInterface(field)
							&& field->focusPolicy() != Qt::NoFocus,"native field accessibility retained",detail);
						const auto inViewport = QRect(field->mapTo(scroll->viewport(),QPoint()),field->size());
						if (!overflow) { expect(inViewport.left() >= 0 && inViewport.right() < scroll->viewport()->width(),"field never clipped horizontally by scrolling",detail); }
					}
					for (auto* label : panel->findChildren<QLabel*>()) {
						if (label->isHidden()) { continue; }
						expect(panel->rect().contains(QRect(label->mapTo(panel,QPoint()),label->size()))
							&& label->height() >= label->heightForWidth(label->width()),"translated caption and note fit",detail);
					}
					expect(edit->text() == pending && (fields[0]->hasFocus() || edit->hasFocus()),"reflow preserves pending text and focus",detail);
					expect(serializeLevelMap(shell.levelDocument()).bytes == original && shell.levelDocument().undoStack.size() == history,
						"reflow cannot commit an edit",detail);
					QVector<QDoubleSpinBox*> tabOrder; QWidget* cursor = fields[0];
					do { if (auto* spin = qobject_cast<QDoubleSpinBox*>(cursor); fields.contains(spin)) { tabOrder.append(spin); } cursor = cursor->nextInFocusChain(); }
					while (cursor && cursor != fields[0] && tabOrder.size() <= fields.size());
					expect(tabOrder == fields,"tab order follows centre XYZ then size XYZ",detail);
					if (overflow) {
						auto* bar = scroll->horizontalScrollBar(); const auto saved = bar->value();
						auto* accessible = QAccessible::queryAccessibleInterface(bar);
						expect(bar->focusPolicy() != Qt::NoFocus && !bar->accessibleName().isEmpty() && !bar->accessibleDescription().isEmpty()
							&& accessible && accessible->role() == QAccessible::ScrollBar,"fallback scrollbar has native accessible state and focus",detail);
						bar->setValue(bar->minimum()); settle(); const int first = fields[0]->mapTo(scroll->viewport(),QPoint()).x();
						bar->setValue(bar->maximum()); settle(); const int last = fields[0]->mapTo(scroll->viewport(),QPoint()).x();
						expect(std::max(first,last) >= 0 && std::min(first,last)+fields[0]->width() <= scroll->viewport()->width(),
							"scroll fallback reaches both edges of a complete native editor",detail); bar->setValue(saved);
					}
				}
				if (profile == "trenchbroom" && scale == 100) {
					for (int liveScale : {125,150,175,200,100}) {
						applyStudioTheme(app,studioThemeTokens(liveScale == 200 ? StudioTheme::HighContrastLight : StudioTheme::Dark,UiDensity::Standard,liveScale));
						settle();
						for (auto* field : fields) {
							expect(field->width() >= field->sizeHint().width() && panel->rect().contains(QRect(field->mapTo(panel,QPoint()),field->size())),
								"live scaling preserves complete numeric editors",QString::number(liveScale));
						}
						expect(edit->text() == pending && (fields[0]->hasFocus() || edit->hasFocus())
							&& serializeLevelMap(shell.levelDocument()).bytes == original && shell.levelDocument().undoStack.size() == history,
							"live scaling preserves pending edit without committing",QString::number(liveScale));
					}
				}
				fields[0]->interpretText(); QMetaObject::invokeMethod(fields[0],"editingFinished",Qt::DirectConnection); settle();
				LevelMapVec3 low,high;
				expect(levelMapSelectionBounds(shell.levelDocument(),&low,&high) && std::abs((low.x+high.x)*0.5-96) < 0.001
					&& shell.levelDocument().undoStack.size() == history+1,"pending value commits once through normal authoring");
				shell.findChild<QAction*>("map.undo")->trigger();
				expect(serializeLevelMap(shell.levelDocument()).bytes == original,"transform undo preserves exact source");
				const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_DIR");
				if (!captures.isEmpty() && profile == "trenchbroom") {
					sidebar->setFixedWidth(420); settle(); scroll->verticalScrollBar()->setValue(0); settle();
					QDir().mkpath(captures); QImage image(sidebar->size(),QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); sidebar->render(&image);
					expect(image.save(QDir(captures).filePath(QStringLiteral("transform-%1.png").arg(scale))),"render actual narrow sidebar");
				}
			}
			if (scale == 200) { app.removeTranslator(&expanded); app.setLayoutDirection(Qt::LeftToRight); }
		}
	}
	return ok ? 0 : 1;
}
