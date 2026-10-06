#include "app/package_copy_budget_dialog.h"
#include "app/package_copy_sessions_dialog.h"
#include "app/studio_theme.h"
#include "core/studio_settings.h"

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionSpinBox>
#include <QTemporaryDir>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; } return value; }
class Expanded final : public QTranslator {
public:
	bool enabled = false;
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override {
		return enabled && QByteArray(context) == "PackageCopyBudgetDialog" ? QStringLiteral("[%1 — expanded]").arg(QString::fromUtf8(source)) : QString();
	}
};
}
int main(int argc, char** argv)
{
	// Direct Qt actions/properties and widget renders only; no native input/capture.
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QTemporaryDir temporary; if (!temporary.isValid()) { return 1; }
	StudioSettings::setOverrideFilePath(temporary.filePath(QStringLiteral("settings.ini")));
	auto budget = std::make_shared<PackageCopyBudget>(); auto reservation = budget->reserve(256ull * 1024 * 1024, 100, 140, {});
	if (!reservation) { return 1; } reservation->retain();
	auto failedCleanup = budget->reserve(1, 1, 1, {});
	if (!failedCleanup) { return 1; } failedCleanup->retain(true);
	Expanded translator; app.installTranslator(&translator); bool ok = true;
	for (int scale : {100, 200}) {
		StudioSettings().setPackageCopyLimits({}); translator.enabled = scale == 200;
		applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastLight : StudioTheme::Dark, UiDensity::Standard, scale));
		app.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
		PackageCopyBudgetDialog dialog(budget, temporary.path()); dialog.show(); app.processEvents();
		auto* apply = dialog.findChild<QPushButton*>(QStringLiteral("applyPackageCopyLimits"));
		auto* close = dialog.findChild<QPushButton*>(QStringLiteral("closePackageCopyLimits"));
		auto* bytes = dialog.findChild<QSpinBox*>(QStringLiteral("packageCopyMaximumMiB"));
		auto* files = dialog.findChild<QSpinBox*>(QStringLiteral("packageCopyMaximumFiles"));
		auto* usage = dialog.findChild<QLabel*>(QStringLiteral("packageCopyUsage"));
		auto* status = dialog.findChild<QLabel*>(QStringLiteral("packageCopyBudgetStatus"));
		if (!expect(apply && close && bytes && files && usage && status, "copy storage controls exist")) { return 1; }
		ok &= expect(!bytes->accessibleName().isEmpty() && bytes->focusPolicy() != Qt::NoFocus && !usage->accessibleName().isEmpty()
			&& usage->text().contains(QStringLiteral("256.0")) && usage->text().contains(QChar(0x2068))
			&& usage->text().contains(QStringLiteral("Cleanup failures")), "usage and cleanup failures expose accessible text with isolated numeric values");
		if (scale == 200) {
			ok &= expect(dialog.fontMetrics().height() >= 24 && dialog.layoutDirection() == Qt::RightToLeft
				&& dialog.palette().color(QPalette::Window).lightness() > 128, "storage fixture uses actual large high-contrast RTL text");
		}
		bytes->setValue(1); files->setValue(3); apply->click(); app.processEvents();
		ok &= expect(StudioSettings().packageCopyLimits().maximumBytes == 1024 * 1024 && StudioSettings().packageCopyLimits().maximumFiles == 3
			&& budget->usage().bytes == 256ull * 1024 * 1024 + 1 && status->text().contains("saved") && usage->text().contains("reached a limit"),
			"lowering persisted limits reports over-budget usage without deleting reservations");
		for (auto* button : {apply, close}) {
			ok &= expect(!button->accessibleName().isEmpty() && button->focusPolicy() != Qt::NoFocus
				&& dialog.rect().contains(QRect(button->mapTo(&dialog, QPoint()), button->size())), "persistent Apply and Close fit at each scale");
		}
		for (auto* caption : dialog.findChildren<QLabel*>()) {
			if (!caption->buddy()) { continue; }
			ok &= expect(caption->height() >= caption->heightForWidth(caption->width()), "wrapped window limit labels have their full required height");
		}
		const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
		if (!captures.isEmpty()) {
			QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog.render(&image);
			ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-copy-storage-%1.png").arg(scale))), "render storage limits");
		}
		if (auto* scroll = dialog.findChild<QScrollArea*>()) {
			for (auto* field : dialog.findChildren<QSpinBox*>()) {
				scroll->ensureWidgetVisible(field); app.processEvents();
				ok &= expect(scroll->viewport()->rect().intersects(QRect(field->mapTo(scroll->viewport(), QPoint()), field->size())), "every overflowing setting stays reachable through the scroll area");
				QStyleOptionSpinBox option; option.initFrom(field); option.rect = field->rect(); option.frame = field->hasFrame();
				option.buttonSymbols = field->buttonSymbols(); option.stepEnabled = QAbstractSpinBox::StepUpEnabled | QAbstractSpinBox::StepDownEnabled;
				for (const auto part : {QStyle::SC_SpinBoxUp, QStyle::SC_SpinBoxDown, QStyle::SC_SpinBoxEditField}) {
					const auto rect = field->style()->subControlRect(QStyle::CC_SpinBox, &option, part, field);
					const bool inside = !rect.isEmpty() && field->rect().contains(rect);
					if (!inside) { std::cerr << "Spin control bounds at " << scale << "%: " << rect.x() << ',' << rect.y() << ',' << rect.width() << ',' << rect.height() << " in " << field->width() << ',' << field->height() << '\n'; }
					ok &= expect(inside, "spin edit and step controls stay inside their widget at each scale/direction");
				}
			}
		}
		if (!captures.isEmpty()) {
			QImage image(dialog.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); dialog.render(&image);
			ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-copy-storage-fields-%1.png").arg(scale))), "render reachable copy limit fields after scrolling");
		}
		auto* review = dialog.findChild<QPushButton*>(QStringLiteral("reviewPackageCopySessions"));
		if (!expect(review && !review->accessibleName().isEmpty() && review->focusPolicy() != Qt::NoFocus, "budget dialog exposes the retained-session review action")) { return 1; }
		review->click(); app.processEvents();
		auto* sessions = dialog.findChild<QDialog*>(QStringLiteral("packageCopySessionsDialog"));
		ok &= expect(sessions && sessions->isVisible() && sessions->layoutDirection() == dialog.layoutDirection(), "review action opens the managed-store dialog with inherited layout direction");
		if (sessions) { sessions->reject(); app.processEvents(); }
		close->click(); ok &= expect(!dialog.isVisible(), "Close leaves the dialog with reservations intact");
	}
	app.removeTranslator(&translator); return ok ? 0 : 1;
}
