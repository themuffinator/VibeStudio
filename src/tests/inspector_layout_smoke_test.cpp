#include "app/ui_primitives.h"
#include "app/property_grid.h"
#include "app/wrapping_action_button.h"
#include "app/studio_theme.h"

#include <QAccessible>
#include <QApplication>
#include <QDir>
#include <QFont>
#include <QImage>
#include <QHeaderView>
#include <QScrollBar>
#include <QStyleOptionButton>
#include <QLabel>
#include <QLayout>
#include <QListWidget>
#include <QPushButton>
#include <QTextEdit>
#include <QTranslator>
#include <QVBoxLayout>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool condition, const QString& message)
{
	if (!condition) { std::cerr << message.toStdString() << '\n'; } return condition;
}
class ExpandedLabels final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		if (QByteArray(context) != "VibeStudioUiPrimitives") { return {}; }
		const QByteArray label(source);
		if (label == "Copy") { return QStringLiteral("Copy the selected diagnostic section"); }
		if (label == "Hide Details") { return QStringLiteral("Hide the current diagnostic details"); }
		if (label == "Show Details") { return QStringLiteral("Show the current diagnostic details"); }
		return {};
	}
};
void settle(QApplication& app)
{
	for (int pass = 0; pass < 5; ++pass) { app.processEvents(QEventLoop::ExcludeUserInputEvents); }
}
}
int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv); bool ok = true;
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	ExpandedLabels translator; app.installTranslator(&translator);
	const QString title = QStringLiteral("Selected package entry diagnostics and authoring metadata");
	const QString subtitle = QStringLiteral("textures/") + QString(100, QLatin1Char('w')) + QStringLiteral("/<source>.png");
	for (const bool embedded : {false, true}) {
		QWidget host; host.setObjectName(QStringLiteral("dockBody")); host.setAttribute(Qt::WA_StyledBackground, true);
		QVBoxLayout hostLayout(&host); hostLayout.setContentsMargins(0, 0, 0, 0);
		DetailDrawer drawer(&host); hostLayout.addWidget(&drawer);
		drawer.setEmbedded(embedded); drawer.setTitle(title); drawer.setSubtitle(subtitle);
		drawer.setSections({{QStringLiteral("preview"), QStringLiteral("Preview details"), QStringLiteral("Current planned content"), QStringLiteral("Retained diagnostic content"), OperationState::Completed},
			{QStringLiteral("metadata"), QStringLiteral("Authoring metadata"), QStringLiteral("Exact original source and staged revision"), QStringLiteral("Retained authoring metadata"), OperationState::Warning}});
		drawer.showSection(QStringLiteral("metadata")); host.show();
		for (const int scale : {100, 125, 150, 175, 200, 100}) {
			applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastLight : StudioTheme::Dark, UiDensity::Standard, scale));
			WrappingActionButton compact(QStringLiteral("[Copy · Copy]"), 0); compact.setProperty("variant", QStringLiteral("ghost"));
			compact.resize(compact.sizeHint()); compact.ensurePolished();
			QStyleOptionButton option; option.initFrom(&compact); option.text = compact.text();
			const auto contentRect = compact.style()->subElementRect(QStyle::SE_PushButtonContents, &option, &compact);
			const auto textHeight = compact.fontMetrics().boundingRect(QRect(0, 0, contentRect.width(), 10000),
				Qt::TextWordWrap | Qt::TextShowMnemonic, compact.text()).height();
			ok &= expect(textHeight <= compact.fontMetrics().height(), "short translated header action stays on one line at its natural size");
			drawer.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
			for (const int width : {320, 900, 320}) {
				host.resize(width, 900); settle(app);
				const auto label = QStringLiteral("embedded=%1 scale=%2 width=%3: ").arg(embedded).arg(scale).arg(width);
				ok &= expect(drawer.width() == width, label + "drawer keeps the requested pane width");
				auto* titleLabel = drawer.findChild<QLabel*>(QStringLiteral("drawerTitle"));
				auto* pathLabel = drawer.findChild<QLabel*>(QStringLiteral("drawerSubtitle"));
				ok &= expect(titleLabel && titleLabel->text() == title && titleLabel->height() >= titleLabel->heightForWidth(titleLabel->width()), label + "wrapped title has its full height");
				ok &= expect(pathLabel && (pathLabel->toolTip() == subtitle || pathLabel->text() == subtitle)
					&& (pathLabel->accessibleDescription() == subtitle || pathLabel->text() == subtitle), label + "full source path remains available to sighted and accessible inspection");
				for (auto* button : drawer.findChildren<QPushButton*>()) {
					if (button->isHidden()) { continue; }
					ok &= expect(drawer.rect().contains(QRect(button->mapTo(&drawer, QPoint()), button->size())), label + "header action stays inside the pane");
					ok &= expect(button->height() >= (button->hasHeightForWidth() ? button->heightForWidth(button->width()) : button->sizeHint().height())
						&& (button->hasHeightForWidth() || button->width() >= button->sizeHint().width()), label + "expanded action text fits without clipping");
					ok &= expect(!button->accessibleName().isEmpty() && button->focusPolicy() != Qt::NoFocus
						&& QAccessible::queryAccessibleInterface(button), label + "header action retains Qt focus and accessibility");
				}
				auto* content = drawer.findChild<QTextEdit*>(QStringLiteral("detailContent"));
				ok &= expect(content && content->height() >= content->fontMetrics().height() * 3
					&& drawer.currentSectionId() == QStringLiteral("metadata") && drawer.currentSectionText() == QStringLiteral("Retained authoring metadata"), label + "reflow preserves usable content and section selection");
				const auto capture = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
				if (!capture.isEmpty()) {
					// Embedded frames inherit their pane background; render the actual parent
					// instead of compositing a transparent standalone child over black.
					QImage image(host.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); host.render(&image);
					ok &= expect(image.save(QDir(capture).filePath(QStringLiteral("detail-layout-%1-%2-%3.png").arg(embedded ? "embedded" : "standalone").arg(scale).arg(width))), "render the actual constrained detail drawer");
				}
			}
		}
		drawer.setExpanded(false); settle(app); drawer.setExpanded(true); settle(app);
		ok &= expect(drawer.currentSectionId() == QStringLiteral("metadata"), "collapse and expand keep the current section");
	}
	app.removeTranslator(&translator);
	applyStudioTheme(app, studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100));
	PropertyGrid grid; grid.setColumnCount(2); grid.setHeaderLabels({QStringLiteral("Property"), QStringLiteral("Value")});
	grid.setAccessibleName(QStringLiteral("Inspector property layout fixture"));
	grid.setEditTriggers(QAbstractItemView::NoEditTriggers);
	grid.header()->setSectionResizeMode(0, QHeaderView::Interactive); grid.header()->setStretchLastSection(true);
	auto* row = new QTreeWidgetItem(&grid, {QStringLiteral("Original source"), subtitle}); row->setToolTip(1, subtitle);
	grid.resize(320, 300); grid.show(); settle(app); grid.header()->resizeSection(0, 240);
	const int chosenWidth = grid.columnWidth(0);
	for (const int scale : {100, 125, 150, 175, 200, 100}) {
		applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastLight : StudioTheme::Dark, UiDensity::Standard, scale));
		grid.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
		grid.setHeaderLabels(scale == 200 ? QStringList{QStringLiteral("Property and original field"), QStringLiteral("Value and recorded content")}
			: QStringList{QStringLiteral("Property"), QStringLiteral("Value")});
		for (const int width : {320, 900}) {
			grid.resize(width, 300); settle(app);
			for (int column = 0; column < 2; ++column) {
				ok &= expect(grid.header()->sectionSize(column) >= grid.header()->sectionSizeHint(column), QStringLiteral("property headings reserve full width: scale=%1 pane=%2 column=%3 actual=%4 hint=%5 minimum=%6").arg(scale).arg(width).arg(column).arg(grid.header()->sectionSize(column)).arg(grid.header()->sectionSizeHint(column)).arg(grid.header()->minimumSectionSize()));
			}
			ok &= expect(grid.width() == width && grid.horizontalScrollBarPolicy() == Qt::ScrollBarAsNeeded,
				"property grid keeps its pane width and permits native horizontal scrolling");
			if (scale == 200 && width == 320) { ok &= expect(grid.horizontalScrollBar()->maximum() > 0, "narrow translated grid can reach both wide columns"); }
			if (scale == 100) { ok &= expect(qAbs(grid.columnWidth(0) - chosenWidth) <= 2, "heading floors do not replace the user's column width after scaling back"); }
			ok &= expect(grid.topLevelItem(0)->text(1) == subtitle && grid.topLevelItem(0)->toolTip(1) == subtitle
				&& grid.focusPolicy() != Qt::NoFocus && QAccessible::queryAccessibleInterface(&grid), "property reflow retains values, detail tooltips and native tree accessibility");
			const auto capture = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
			if (!capture.isEmpty()) {
				QImage image(grid.size(), QImage::Format_ARGB32_Premultiplied); image.fill(grid.palette().color(QPalette::Window)); grid.render(&image);
				ok &= expect(image.save(QDir(capture).filePath(QStringLiteral("property-layout-%1-%2.png").arg(scale).arg(width))), "render constrained metadata headings");
			}
		}
	}
	return ok ? 0 : 1;
}
