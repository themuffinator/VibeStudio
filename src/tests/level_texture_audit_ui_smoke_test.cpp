#include "app/level_texture_audit_panel.h"
#include "app/studio_theme.h"

#include <QAccessible>
#include <QApplication>
#include <QDir>
#include <QDebug>
#include <QElapsedTimer>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QTranslator>
#include <QVBoxLayout>
#include <atomic>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; } return value; }
bool until(const std::function<bool()>& done)
{
	QElapsedTimer timer; timer.start();
	while (!done() && timer.elapsed() < 10000) { QApplication::processEvents(QEventLoop::ExcludeUserInputEvents); QThread::msleep(1); }
	return done();
}
LevelMapDocument map(const QString& texture = QStringLiteral("studio/wall"))
{
	LevelMapDocument document; document.format = LevelMapFormat::Quake3Map; document.engineFamily = QStringLiteral("idTech3");
	LevelMapBrush brush; LevelMapBrushFace face; face.textureName = texture; brush.faces = {face}; document.brushes = {brush}; return document;
}
class Reader final : public PackageArchiveReader {
public:
	mutable std::atomic_bool reading = false, release = false, onGui = false;
	mutable std::atomic_int active = 0, maximumActive = 0, stops = 0, reads = 0;
	bool open = true, fail = false;
	const QByteArray bytes = "textures/studio/wall { { map textures/studio/image } }";
	PackageArchiveFormat format() const override { return PackageArchiveFormat::Pk3; }
	QString sourcePath() const override { return QStringLiteral("immutable-audit-fixture"); }
	bool isOpen() const override { return open; }
	QString errorString() const override { return open ? QString() : QStringLiteral("planned view admission refused"); }
	QVector<PackageEntry> entries() const override {
		PackageEntry entry; entry.virtualPath = QStringLiteral("scripts/studio.shader"); entry.sizeBytes = bytes.size(); return {entry};
	}
	bool readEntryBytes(const QString&, QByteArray*, QString*, qint64) const override { return false; }
	bool streamEntryAt(qsizetype, const std::function<bool(QByteArrayView)>& sink, QString* error, const std::function<bool()>& cancelled) const override {
		++reads; const int count = ++active; maximumActive = qMax(maximumActive.load(), count); reading = true;
		struct Leave { std::atomic_int& count; ~Leave() { --count; } } leave{active};
		onGui = QThread::currentThread() == qApp->thread();
		QElapsedTimer timer; timer.start();
		while (!release && timer.elapsed() < 10000) {
			if (cancelled && cancelled()) { ++stops; return false; } QThread::msleep(1);
		}
		if ((cancelled && cancelled()) || !sink(bytes)) { ++stops; return false; }
		if (fail && error) { *error = QStringLiteral("final shader integrity check failed"); }
		return !fail;
	}
};
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override {
		return QByteArray(context) == "VibeStudioLevelTextureAudit" ? QStringLiteral("[%1 · %1]").arg(QString::fromUtf8(source)) : QString();
	}
};
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
	for (const int scale : {100, 200}) {
		Expanded translator; if (scale == 200) { app.installTranslator(&translator); }
		applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastLight : StudioTheme::Dark, UiDensity::Standard, scale));
		app.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
		QWidget host; host.setObjectName(QStringLiteral("dockBody")); host.setAttribute(Qt::WA_StyledBackground, true);
		QVBoxLayout layout(&host); layout.setContentsMargins(0, 0, 0, 0);
		LevelTextureAuditPanel panel; layout.addWidget(&panel); host.resize(360, 320); host.show();
		auto reader = std::make_shared<Reader>();
		auto* cancel = panel.findChild<QPushButton*>(QStringLiteral("cancelLevelTextureAudit"));
		auto* retry = panel.findChild<QPushButton*>(QStringLiteral("retryLevelTextureAudit"));
		auto* status = panel.findChild<QLabel*>(QStringLiteral("levelTextureAuditStatus"));
		const auto render = [&](const QString& suffix) {
			for (int pass = 0; pass < 5; ++pass) { host.layout()->activate(); app.processEvents(QEventLoop::ExcludeUserInputEvents); }
			bool good = host.width() == 360 && status && status->height() >= status->heightForWidth(status->width());
			auto* accessibleStatus = status ? QAccessible::queryAccessibleInterface(status) : nullptr;
			good &= accessibleStatus && accessibleStatus->role() == QAccessible::StaticText
				&& accessibleStatus->text(QAccessible::Name) == status->text() && !status->text().isEmpty();
			for (auto* button : {cancel, retry}) {
				good &= button && !button->accessibleName().isEmpty() && button->focusPolicy() != Qt::NoFocus && QAccessible::queryAccessibleInterface(button);
				if (button && button->isVisible()) { good &= panel.rect().contains(QRect(button->mapTo(&panel, QPoint()), button->size())); }
			}
			const auto capture = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
			if (!capture.isEmpty()) {
				QImage image(host.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); host.render(&image);
				good &= image.save(QDir(capture).filePath(QStringLiteral("texture-audit-%1-%2.png").arg(suffix).arg(scale)));
			}
			if (!good) { std::cerr << "Layout scale=" << scale << " state=" << suffix.toStdString() << " width=" << host.width() << " status-height=" << status->height() << " required=" << status->heightForWidth(status->width()) << '\n'; }
			if (!good) {
				QString details;
				{
				QDebug log(&details);
				log << "host" << host.size() << host.minimumSize() << host.sizeHint() << host.layout()->minimumSize();
				log << "panel" << panel.size() << panel.minimumSize() << panel.sizeHint() << panel.minimumSizeHint() << panel.hasHeightForWidth() << panel.heightForWidth(panel.width());
				log << "layout" << panel.layout()->geometry() << panel.layout()->minimumSize() << panel.layout()->sizeHint() << panel.layout()->heightForWidth(panel.width());
				for (auto* child : panel.findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
					log << child->metaObject()->className() << child->objectName() << child->isVisible() << child->geometry() << child->minimumSize() << child->sizeHint() << child->minimumSizeHint() << child->sizePolicy() << child->heightForWidth(child->width());
				}
				}
				std::cerr << details.toStdString() << '\n';
			}
			return good;
		};
		panel.setSource("first", map(), reader, true);
		ok &= expect(until([&] { return reader->reading.load(); }) && panel.state() == OperationState::Loading && cancel->isVisible(), "audit reads run asynchronously with an available Cancel control");
		ok &= expect(render("running"), "scaled translated progress remains reachable and renders");
		cancel->click();
		ok &= expect(until([&] { return reader->active.load() == 0; }) && panel.state() == OperationState::Cancelled && !panel.result(), "Cancel keeps a final cancelled state and discards old results");
		ok &= expect(render("cancelled"), "cancelled state and Retry remain readable");
		reader->release = true; retry->click();
		ok &= expect(until([&] { return panel.result().has_value(); }) && panel.result()->complete && panel.result()->resolvedCount == 1 && !reader->onGui && reader->maximumActive == 1, "Retry completes on a worker with at most one active audit");
		ok &= expect(render("complete"), "Retry completion preserves the constrained width and readable status");
		reader->release = false; reader->reading = false;
		panel.setSource("old", map(), reader, true);
		ok &= expect(until([&] { return reader->reading.load(); }), "hold the old context on its reader");
		auto latest = std::make_shared<Reader>(); latest->release = true;
		panel.setSource("superseded", map(), latest, true);
		panel.setSource("latest", map(QStringLiteral("studio/missing")), latest, true);
		ok &= expect(until([&] { return panel.result().has_value(); }) && panel.result()->missingCount == 1 && latest->reads == 1, "only the latest pending context runs and can publish its result");
		reader->reading = false; panel.setSource("late-cancel", map(), reader, true);
		ok &= expect(until([&] { return reader->reading.load(); }), "prepare completion-before-delivery cancellation");
		QThread* thread = nullptr;
		for (auto* candidate : panel.findChildren<QThread*>(QString(), Qt::FindDirectChildrenOnly)) { if (candidate->isRunning()) { thread = candidate; break; } }
		reader->release = true;
		ok &= expect(thread && thread->wait(10000), "join the finished worker without delivering its queued completion");
		cancel->click(); app.processEvents(QEventLoop::ExcludeUserInputEvents);
		ok &= expect(panel.state() == OperationState::Cancelled && !panel.result(), "late Cancel suppresses queued successful results");
		auto bad = std::make_shared<Reader>(); bad->release = true; bad->fail = true;
		panel.setSource("bad-shader", map(), bad, true);
		ok &= expect(until([&] { return panel.result().has_value(); }) && panel.state() == OperationState::Failed && !panel.result()->complete, "failed shader integrity is visibly incomplete");
		ok &= expect(render("incomplete"), "incomplete audit status is readable at both scales");
		bad->open = false; const int readCount = bad->reads;
		panel.setSource("refused-plan", map(), bad, true);
		ok &= expect(panel.state() == OperationState::Failed && panel.error().contains("admission refused") && bad->reads == readCount && !panel.result(), "refused planned views never fall back to original bytes");
		auto* refusedStatus = QAccessible::queryAccessibleInterface(status);
		ok &= expect(refusedStatus && refusedStatus->text(QAccessible::Description).contains("admission refused"), "refused-plan reason remains available through the native accessibility interface");
		reader->release = false; reader->reading = false;
		{
			LevelTextureAuditPanel closing; closing.setSource("closing", map(), reader, true);
			ok &= expect(until([&] { return reader->reading.load(); }), "start an audit before panel destruction");
		}
		ok &= expect(reader->active == 0, "destruction cancels and joins the owned reader");
		if (scale == 200) { app.removeTranslator(&translator); }
	}
	return ok ? 0 : 1;
}
