#include "app/ui_primitives.h"
#include "app/studio_layout.h"
#include "app/studio_theme.h"

#include <QApplication>
#include <QEvent>
#include <QDir>
#include <QEventLoop>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPersistentModelIndex>
#include <QScrollBar>
#include <QString>
#include <QTextEdit>
#include <QTextCursor>
#include <QTextDocument>
#include <QVector>

#include <iostream>
#include <algorithm>

using namespace vibestudio;

namespace {

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << "\n";
		return false;
	}
	return true;
}

bool hasPrimitive(const QVector<UiPrimitiveDescriptor>& primitives, const QString& id)
{
	for (const UiPrimitiveDescriptor& primitive : primitives) {
		if (primitive.id == id) {
			return true;
		}
	}
	return false;
}

class ThemeProbe final : public QWidget {
public:
	using QWidget::QWidget;
	int styleChanges = 0;
protected:
	void changeEvent(QEvent* event) override
	{
		if (event->type() == QEvent::StyleChange) { ++styleChanges; }
		QWidget::changeEvent(event);
	}
};

bool checkThemeTransitions(QApplication& app, DetailDrawer& drawer)
{
	bool ok = true;
	ThemeProbe root;
	QVector<ThemeProbe*> probes{&root};
	for (int depth = 0; depth < 24; ++depth) { probes.append(new ThemeProbe(probes.back())); }
	QLineEdit local(probes.back());
	const auto localStyle = QStringLiteral("QLineEdit { color: #234567; }");
	local.setStyleSheet(localStyle);
	local.setText(QStringLiteral("Retained text")); local.setSelection(1, 4);
	const auto theme = studioThemeTokens(StudioTheme::Dark, UiDensity::Standard, 100);
	applyStudioTheme(app, theme);
	root.ensurePolished();
	for (auto* probe : probes) { probe->ensurePolished(); probe->styleChanges = 0; }
	applyStudioTheme(app, theme);
	for (const auto* probe : probes) {
		ok &= expect(probe->styleChanges == 0, "unchanged theme does not restyle nested widgets");
	}
	for (const auto next : {StudioTheme::HighContrastDark, StudioTheme::HighContrastLight, StudioTheme::Light, StudioTheme::Dark}) {
		for (auto* probe : probes) { probe->styleChanges = 0; }
		const auto tokens = studioThemeTokens(next, UiDensity::Standard, next == StudioTheme::Dark ? 100 : 200);
		applyStudioTheme(app, tokens); app.processEvents(QEventLoop::ExcludeUserInputEvents);
		int maximum = 0;
		for (const auto* probe : probes) {
			maximum = std::max(maximum, probe->styleChanges);
			ok &= expect(probe->styleChanges > 0 && probe->styleChanges <= 4,
				"theme change does bounded restyling at every widget depth");
			ok &= expect(qAbs(probe->font().pointSizeF() - tokens.metrics.baseFontPoints) < 0.1
				&& probe->palette().color(QPalette::Window) == studioPalette(tokens).color(QPalette::Window),
				"nested widgets inherit the new theme font and palette");
		}
		std::cerr << "Theme transition " << themeId(next).toStdString() << " deepest_style_changes=" << probes.back()->styleChanges
			<< " maximum_style_changes=" << maximum << '\n';
		ok &= expect(local.styleSheet() == localStyle && local.palette().color(QPalette::Text) == QColor(QStringLiteral("#234567"))
			&& local.text() == QStringLiteral("Retained text") && local.selectedText() == QStringLiteral("etai"),
			"theme transition retains local styling, text and selection");
		auto* content = drawer.findChild<QTextEdit*>(QStringLiteral("detailContent"));
		ok &= expect(content && qAbs(content->font().pointSizeF() - tokens.metrics.baseFontPoints) < 0.1
			&& qAbs(content->document()->defaultFont().pointSizeF() - tokens.metrics.baseFontPoints) < 0.1
			&& drawer.currentSectionText() == QStringLiteral("Smoke test content"),
			"theme transitions retain the detail content and scale its fixed-pitch font");
	}
	return ok;
}

bool checkDetailRefresh(QApplication& app)
{
	bool ok = true;
	QStringList lines;
	for (int i = 0; i < 1000; ++i) { lines << QStringLiteral("Diagnostic row %1: retained content").arg(i); }
	const QString text = lines.join('\n');
	for (const int scale : {100, 200}) {
		applyStudioTheme(app, studioThemeTokens(scale == 100 ? StudioTheme::Dark : StudioTheme::HighContrastDark, UiDensity::Standard, scale));
		DetailDrawer drawer; drawer.resize(680, 400);
		drawer.setLayoutDirection(scale == 100 ? Qt::LeftToRight : Qt::RightToLeft);
		QVector<DetailSection> sections{{"summary", "Summary", "State", "Ready", OperationState::Completed},
			{"log", "Log", "Diagnostics", text, OperationState::Warning}};
		drawer.setSections(sections); drawer.showSection("log"); drawer.show();
		app.processEvents(QEventLoop::ExcludeUserInputEvents);
		auto* content = drawer.findChild<QTextEdit*>("detailContent");
		if (!expect(content != nullptr, "detail content exists")) { return false; }
		QTextCursor cursor(content->document()); cursor.setPosition(7); cursor.setPosition(24, QTextCursor::KeepAnchor);
		content->setTextCursor(cursor);
		auto* scroll = content->verticalScrollBar(); scroll->setValue(scroll->maximum() / 2);
		const int position = scroll->value();
		int changes = 0;
		const auto connection = QObject::connect(content->document(), &QTextDocument::contentsChanged, &drawer, [&] { ++changes; });
		auto* chooser = drawer.findChild<QListWidget*>("detailSections");
		if (!expect(chooser && chooser->count() == 2, "detail section chooser is available")) { return false; }
		const QPersistentModelIndex logRow = chooser->model()->index(1, 0);
		int resets = 0, updates = 0;
		const auto resetConnection = QObject::connect(chooser->model(), &QAbstractItemModel::modelReset, &drawer, [&] { ++resets; });
		const auto updateConnection = QObject::connect(chooser->model(), &QAbstractItemModel::dataChanged, &drawer, [&] { ++updates; });
		drawer.setSections(sections); drawer.showSection("log");
		ok &= expect(logRow.isValid() && resets == 0 && updates == 0,
			"repeated detail context keeps the reader's section row without redundant model notifications");
		sections[0].content = "New summary"; sections[0].summary = "New state"; sections[0].state = OperationState::Failed;
		drawer.setSections(sections); drawer.showSection("log");
		app.processEvents(QEventLoop::ExcludeUserInputEvents);
		ok &= expect(changes == 0 && drawer.currentSectionId() == "log" && drawer.currentSectionText() == text &&
			content->textCursor().selectedText() == cursor.selectedText() && scroll->value() == position && position > 0,
			"unchanged detail text keeps its selection and reading position during other context updates");
		ok &= expect(logRow.isValid() && logRow.data(Qt::UserRole) == "log" && resets == 0
			&& chooser->item(0)->data(Qt::AccessibleTextRole).toString().contains("New state")
			&& chooser->item(0)->data(Qt::UserRole + 1) == "failed",
			"changed detail metadata updates accessibly while keeping the active section row");
		sections[1].content = "Updated diagnostics"; drawer.setSections(sections);
		ok &= expect(changes > 0 && drawer.currentSectionId() == "log" && content->toPlainText() == "Updated diagnostics",
			"changed active content replaces the old diagnostic text");
		drawer.showSection("summary");
		ok &= expect(content->toPlainText() == "New summary", "switching sections displays current content");
		ok &= expect(logRow.isValid() && resets == 0, "section navigation preserves existing chooser rows");
		drawer.setSections({sections[1], sections[0]});
		ok &= expect(drawer.currentSectionId() == "summary" && chooser->currentRow() == 1
			&& content->toPlainText() == "New summary", "reordered sections retain the reader's section by identity");
		drawer.setSections({sections[1]});
		ok &= expect(drawer.currentSectionId() == "log" && content->toPlainText() == "Updated diagnostics",
			"removing the selected section chooses a valid remaining section");
		drawer.setSections({});
		ok &= expect(drawer.currentSectionId().isEmpty() && drawer.currentSectionText().isEmpty(), "empty details cannot retain stale content");
		drawer.setSections(sections); drawer.showSection("log");
		ok &= expect(content->toPlainText() == "Updated diagnostics", "reopening details after clear restores current content");
		QObject::disconnect(connection); QObject::disconnect(resetConnection); QObject::disconnect(updateConnection);
	}
	return ok;
}

// A notice's state edge and its wide margin sit on the leading side in both
// directions; a style sheet border-left would stay on the left in RTL.
bool checkNoticeEdge(QApplication& app)
{
	bool ok = true;
	for (const auto theme : {StudioTheme::Dark, StudioTheme::HighContrastLight}) {
		const auto tokens = studioThemeTokens(theme, UiDensity::Standard, 100);
		applyStudioTheme(app, tokens);
		int insets[2][2] = {};
		for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
			const bool mirrored = direction == Qt::RightToLeft;
			NoticeBar notice;
			notice.setLayoutDirection(direction);
			notice.showNotice(QStringLiteral("failed"), QStringLiteral("The studio closed unexpectedly last time."),
				QStringLiteral("A report was kept; the files that were open were not reopened."));
			notice.addAction(QStringLiteral("View Report"));
			notice.resize(760, notice.sizeHint().height());
			app.processEvents(QEventLoop::ExcludeUserInputEvents);
			QImage image(notice.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			notice.render(&image);
			const int middle = image.height() / 2;
			const QRgb edge = tokens.colors.danger.rgb();
			const QRgb leading = image.pixel(mirrored ? image.width() - 1 : 0, middle);
			const QRgb trailing = image.pixel(mirrored ? 0 : image.width() - 1, middle);
			ok &= expect(leading == edge && trailing != edge, "a notice paints its state edge on its leading side only");
			auto* icon = notice.findChild<QLabel*>(QStringLiteral("noticeIcon"));
			auto* close = notice.findChild<QWidget*>(QStringLiteral("noticeClose"));
			if (!expect(icon && close, "notice glyph and close button exist")) {
				return false;
			}
			const auto leadingInset = [&](const QWidget* child) { return mirrored ? notice.width() - 1 - child->geometry().right() : child->x(); };
			const auto trailingInset = [&](const QWidget* child) { return mirrored ? child->x() : notice.width() - 1 - child->geometry().right(); };
			insets[mirrored][0] = leadingInset(icon);
			insets[mirrored][1] = trailingInset(close);
			ok &= expect(insets[mirrored][0] > insets[mirrored][1], "the notice's wide margin clears the edge on its leading side");
			const auto root = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
			ok &= expect(root.isEmpty() || image.save(QDir(root).filePath(QStringLiteral("notice-%1-%2.png")
				.arg(themeId(theme), mirrored ? QStringLiteral("rtl") : QStringLiteral("ltr")))), "write notice capture");
		}
		ok &= expect(insets[0][0] == insets[1][0] && insets[0][1] == insets[1][1],
			"a right-to-left notice mirrors the left-to-right spacing of its glyph and close button");
	}
	return ok;
}

} // namespace

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	bool ok = true;

	const QVector<UiPrimitiveDescriptor> primitives = uiPrimitiveDescriptors();
	ok &= expect(primitives.size() >= 2, "expected at least two UI primitive descriptors");
	ok &= expect(hasPrimitive(primitives, QStringLiteral("loading-pane")), "missing loading-pane descriptor");
	ok &= expect(hasPrimitive(primitives, QStringLiteral("detail-drawer")), "missing detail-drawer descriptor");

	for (const UiPrimitiveDescriptor& primitive : primitives) {
		ok &= expect(!primitive.id.trimmed().isEmpty(), "primitive id is empty");
		ok &= expect(!primitive.title.trimmed().isEmpty(), "primitive title is empty");
		ok &= expect(!primitive.description.trimmed().isEmpty(), "primitive description is empty");
		ok &= expect(!primitive.useCases.isEmpty(), "primitive use cases are empty");
	}

	LoadingPane loadingPane;
	loadingPane.setState(OperationState::Loading);
	loadingPane.setPlaceholderRows({QStringLiteral("First"), QStringLiteral("Second")});
	loadingPane.setPlaceholderRows({QStringLiteral("Project manifest"), QStringLiteral("Diagnostics")});
	loadingPane.setPlaceholderRows({});
	ok &= expect(loadingPane.placeholderRows().isEmpty(), "empty placeholder rows should use default rendered rows without storing them");

	DetailDrawer drawer;
	drawer.setSections({
		{
			QStringLiteral("summary"),
			QStringLiteral("Summary"),
			QStringLiteral("Smoke test summary"),
			QStringLiteral("Smoke test content"),
			OperationState::Completed,
		},
	});
	drawer.showSection(QStringLiteral("summary"));
	ok &= expect(drawer.currentSectionText() == QStringLiteral("Smoke test content"), "detail drawer should expose selected section content");
	drawer.show();
	app.processEvents(QEventLoop::ExcludeUserInputEvents);
	auto* content = drawer.findChild<QTextEdit*>(QStringLiteral("detailContent"));
	for (const qreal points : {10.5, 21.0, 10.5}) {
		QFont font = app.font(); font.setPointSizeF(points); app.setFont(font);
		app.setStyleSheet(QStringLiteral("QFrame { background: #101010; color: #ffffff; border-radius: %1px; }").arg(points));
		app.processEvents(QEventLoop::ExcludeUserInputEvents);
		ok &= expect(content && qAbs(content->font().pointSizeF() - points) < 0.1
			&& qAbs(content->document()->defaultFont().pointSizeF() - points) < 0.1,
			"detail text follows live application font and stylesheet changes in both directions");
	}

	ok &= checkThemeTransitions(app, drawer);
	ok &= checkDetailRefresh(app);
	ok &= checkNoticeEdge(app);
	return ok ? 0 : 1;
}
