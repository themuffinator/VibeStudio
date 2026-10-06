#include "app/asset_views.h"
#include "app/studio_theme.h"
#include "app/texture_canvas.h"
#include "app/texture_editor_dialog.h"
#include "core/studio_settings.h"

#include <QAbstractButton>
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QEventLoop>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>

#include <iostream>

using namespace vibestudio;

namespace {
bool expect(bool value, const QString& message)
{
	if (!value) { std::cerr << message.toStdString() << '\n'; }
	return value;
}

bool settled(TextureEditorDialog& editor)
{
	QEventLoop loop; QTimer poll, timeout; timeout.setSingleShot(true);
	QObject::connect(&poll, &QTimer::timeout, &loop, [&]() { if (!editor.isBusy()) { loop.quit(); } });
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	poll.start(5); timeout.start(15000); if (editor.isBusy()) { loop.exec(); }
	return !editor.isBusy();
}

class ExpandedTranslator final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		if (!QByteArray(context).startsWith("VibeStudioTexture")) { return {}; }
		return QStringLiteral("[%1 expanded]").arg(QString::fromUtf8(source));
	}
};
}

int main(int argc, char** argv)
{
	// Read native accessibility/focus metadata and invoke canvas commands only.
	// Do not synthesize input, change the live clipboard or capture the desktop.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
	QTemporaryDir temporary;
	if (!temporary.isValid()) { return EXIT_FAILURE; }
	StudioSettings::setOverrideFilePath(QDir(temporary.path()).filePath(QStringLiteral("profile.ini")));
	bool ok = true;
	int pass = 0;
	for (const auto theme : {StudioTheme::Dark, StudioTheme::HighContrastDark, StudioTheme::HighContrastLight}) {
		const bool enlarged = pass != 0;
		ExpandedTranslator expanded;
		if (enlarged) { app.installTranslator(&expanded); }
		applyStudioTheme(app, studioThemeTokens(theme, UiDensity::Standard, enlarged ? 200 : 100));
		{
			TextureEditorDialog editor;
			editor.resize(enlarged ? QSize(1450, 1000) : QSize(1180, 800));
			editor.setLayoutDirection(enlarged ? Qt::RightToLeft : Qt::LeftToRight);
			QImage source(16, 16, QImage::Format_ARGB32); source.fill(QColor(30, 80, 140, 160));
			ok &= expect(editor.setImage(source, QStringLiteral("fixture.png")), QStringLiteral("load accessibility fixture"));
			editor.show(); app.processEvents();
			auto* sections = editor.findChild<QComboBox*>(QStringLiteral("texturePropertySection"));
			auto* canvas = editor.findChild<TextureCanvas*>(QStringLiteral("textureCanvas"));
			if (!sections || !canvas) { return EXIT_FAILURE; }
			QSet<QWidget*> focusChain;
			auto* next = editor.nextInFocusChain();
			while (next != &editor && !focusChain.contains(next)) { focusChain.insert(next); next = next->nextInFocusChain(); }
			ok &= expect(next == &editor, QStringLiteral("focus chain returns to the editor without trapping navigation"));
			QSet<QWidget*> inspected;
			for (int index = 0; index < sections->count(); ++index) {
				sections->setCurrentIndex(index); app.processEvents();
				// Opening Recovery starts a real asynchronous inventory. Audit the
				// settled controls before invoking authoring commands on the canvas.
				if (!expect(settled(editor), QStringLiteral("inspector operation completes: ") + sections->currentText())) { return EXIT_FAILURE; }
				for (auto* field : editor.findChildren<QWidget*>()) {
					const bool control = qobject_cast<QAbstractButton*>(field) || qobject_cast<QComboBox*>(field)
						|| qobject_cast<QSpinBox*>(field) || qobject_cast<QLineEdit*>(field) || qobject_cast<QListWidget*>(field)
						|| qobject_cast<PaletteSwatchView*>(field) || field == canvas;
					if (!control || !field->isVisibleTo(&editor) || inspected.contains(field) || field->objectName() == QStringLiteral("qt_spinbox_lineedit")) { continue; }
					inspected.insert(field);
					auto* accessible = QAccessible::queryAccessibleInterface(field);
					const QString name = accessible ? accessible->text(QAccessible::Name) : QString{};
					const auto diagnostic = QStringLiteral("section %1, %2 (%3)").arg(sections->currentText(), QString::fromLatin1(field->metaObject()->className()), name);
					ok &= expect(accessible && !name.isEmpty(), QStringLiteral("missing accessible name: ") + diagnostic);
					ok &= expect((field->focusPolicy() & Qt::TabFocus) && focusChain.contains(field), QStringLiteral("control cannot receive Tab focus: ") + diagnostic);
					ok &= expect(accessible && (!field->isEnabled() || accessible->state().focusable), QStringLiteral("enabled control lacks accessible focus state: ") + diagnostic);
				}
				for (auto* scroll : editor.findChildren<QScrollArea*>()) {
					if (scroll->isVisibleTo(&editor)) { ok &= expect(scroll->horizontalScrollBar()->maximum() == 0, QStringLiteral("inspector clips horizontally: ") + sections->currentText()); }
				}
			}
			ok &= expect(inspected.size() >= 75, QStringLiteral("audit includes every inspector section and both toolbars"));
			auto* accessible = QAccessible::queryAccessibleInterface(canvas);
			ok &= expect(accessible && accessible->role() == QAccessible::Graphic, QStringLiteral("canvas exposes the graphic accessibility role"));
			canvas->setCursorPixel({4, 5});
			const auto readout = accessible->text(QAccessible::Description);
			ok &= expect(readout.contains(QStringLiteral("4, 5")) && readout.contains(QStringLiteral("#a01e508c")), QStringLiteral("spoken readout contains the current pixel and RGBA"));
			canvas->setTool(TextureCanvas::Tool::Line); canvas->applyAtCursor(); canvas->setCursorPixel({9, 10});
			ok &= expect(canvas->hasPendingShape() && accessible->text(QAccessible::Description).contains(QStringLiteral("9, 10")), QStringLiteral("spoken readout follows the pending endpoint"));
			canvas->cancelGesture();
			ok &= expect(!canvas->hasPendingShape() && accessible->text(QAccessible::Description).contains(QStringLiteral("#a01e508c")) && !editor.hasUnsavedChanges(), QStringLiteral("cancelling a shape restores the pixel readout without editing"));
			auto* palette = editor.findChild<PaletteSwatchView*>();
			auto* paletteAccessible = QAccessible::queryAccessibleInterface(palette);
			ok &= expect(paletteAccessible && paletteAccessible->role() == QAccessible::ColorChooser, QStringLiteral("palette exposes the color chooser role"));
			palette->setSelectedIndex(7);
			ok &= expect(paletteAccessible->text(QAccessible::Description).contains(QStringLiteral("selected index 7")), QStringLiteral("palette selection updates its spoken index and color"));
			ImagePreviewView preview; preview.setImage(source, QStringLiteral("RGBA fixture"));
			auto* previewAccessible = QAccessible::queryAccessibleInterface(&preview);
			ok &= expect(previewAccessible && previewAccessible->role() == QAccessible::Graphic && !previewAccessible->text(QAccessible::Description).isEmpty(), QStringLiteral("shared image previews expose a graphic role and image summary"));
			for (const auto* id : {"newTexture", "openTexture", "saveTexture", "saveTextureAs", "undoTexture", "redoTexture"}) {
				auto* action = editor.findChild<QAction*>(QString::fromLatin1(id));
				ok &= expect(action && !action->shortcut().isEmpty() && action->shortcutContext() == Qt::WidgetWithChildrenShortcut && editor.actions().contains(action), QStringLiteral("document shortcut is scoped to its editor: ") + QString::fromLatin1(id));
			}
			std::cout << "theme=" << pass << " inspected_controls=" << inspected.size() << " focus_chain_widgets=" << focusChain.size() << '\n';
		}
		if (enlarged) { app.removeTranslator(&expanded); }
		++pass;
	}
	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
