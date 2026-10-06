#include "app/package_staging_view.h"
#include "app/studio_theme.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFont>
#include <QIdentityProxyModel>
#include <QImage>
#include <QScrollBar>
#include <QThread>
#include <QTimer>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; } return value; }
template<class Predicate> bool until(Predicate predicate)
{
	QElapsedTimer elapsed; elapsed.start();
	while (!predicate() && elapsed.elapsed() < 15000) { QCoreApplication::processEvents(); QThread::msleep(1); }
	return predicate();
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override
	{
		return QByteArray(context).endsWith("PackageStagingView") ? QStringLiteral("[%1 — expanded]").arg(QString::fromUtf8(source)) : QString();
	}
};
class CountPresentation final : public QIdentityProxyModel {
public:
	mutable int formatted = 0, layout = 0;
	QVariant data(const QModelIndex& index, int role) const override
	{
		if (role == Qt::UserRole + 10) { ++layout; }
		if (role == Qt::DisplayRole || role == Qt::ToolTipRole || role == Qt::AccessibleTextRole || role == Qt::AccessibleDescriptionRole) { ++formatted; }
		return QIdentityProxyModel::data(index, role);
	}
	void multiData(const QModelIndex& index, QModelRoleDataSpan roles) const override
	{
		for (auto& role : roles) { role.setData(data(index, role.role())); }
	}
};
}

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication app(argc, argv);
#ifdef Q_OS_WIN
	app.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	QVector<PackageStageOperation> operations;
	for (int row = 0; row < 50000; ++row) {
		PackageStageOperation edit; edit.id = QStringLiteral("operation-%1").arg(row);
		edit.virtualPath = row < 2 ? QStringLiteral("THINGS") : QStringLiteral("assets/%1.bin").arg(row, 6, 10, QLatin1Char('0'));
		edit.sourceOrdinal = row; edit.type = row < 2 ? PackageStageOperationType::Replace : PackageStageOperationType::Rename;
		edit.hasInlineBytes = row < 2; if (edit.hasInlineBytes) { edit.inlineBytes = "abc"; }
		else { edit.targetVirtualPath = QStringLiteral("renamed/%1.bin").arg(row, 6, 10, QLatin1Char('0')); }
		edit.sourceFilePath = QStringLiteral("fixture/source/%1.bin").arg(row); operations << edit;
	}
	QVector<PackageStageConflict> conflicts;
	for (int row = 0; row < 20000; ++row) { conflicts << PackageStageConflict{{}, QStringLiteral("blocked/%1").arg(row), QStringLiteral("Diagnostic %1: target already exists; select this row for the full explanation.").arg(row), row % 2 == 0}; }
	const QVector<PackageStagingRow> before{{QStringLiteral("Staging blocked\n50,000 operations, 20,000 conflicts, 10,000 blockers\n50,000 base files -> 50,000 staged files\n150,000 B before, 150,000 B after"), QStringLiteral("failed")}};
	const QVector<PackageStagingRow> after{{QStringLiteral("After: Binary\n150,000 B / 50,000 files"), QStringLiteral("completed")}};
	bool ok = true;
	for (const int scale : {100, 200}) {
		Expanded translator; if (scale == 200) { app.installTranslator(&translator); }
		applyStudioTheme(app, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale));
		app.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
		PackageStagingView view; view.resize(scale == 200 ? 1100 : 780, scale == 200 ? 900 : 650);
		view.setAccessibleName(QStringLiteral("Package staging summary"));
		QString detail; int detailUpdates = 0; QObject::connect(&view, &PackageStagingView::detailsChanged, &app, [&](const QString& value) { detail = value; ++detailUpdates; });
		QElapsedTimer elapsed; elapsed.start(); view.setContents(before, operations, conflicts, after);
		std::cout << "Install 70,000 records at " << scale << "%: " << elapsed.elapsed() << " ms\n";
		ok &= expect(view.model()->rowCount() == 70002 && view.model()->rowCount(view.model()->index(0, 0)) == 0,
			"every operation/conflict is exposed without paging and child indexes are empty");
		view.setCurrentIndex(view.model()->index(2, 0));
		ok &= expect(view.selectedOperationIds() == QStringList{"operation-1"} && detail.contains("source entry 2")
			&& detail.contains("Generated asset") && detail.contains("source") && detail.contains("Conflict policy"),
			"exact duplicate identity and full generated/source details reach the current-row detail pane");
		const auto first = view.model()->index(1, 0), second = view.model()->index(2, 0);
		ok &= expect(first.data(Qt::AccessibleTextRole) != second.data(Qt::AccessibleTextRole)
			&& second.data(Qt::ToolTipRole).toString().contains("Unstage") && view.focusPolicy() != Qt::NoFocus,
			"duplicate operations expose distinct accessible identities and discoverable actions");
		const int unchangedDetails = detailUpdates; view.refreshPresentation();
		view.setContents(before, operations, conflicts, after);
		ok &= expect(detailUpdates == unchangedDetails && view.currentIndex() == second && view.selectedOperationIds() == QStringList{"operation-1"}, "unchanged refresh keeps current and selected operations");
		view.selectAll(); ok &= expect(view.selectedOperationIds().size() == 50000, "Select All addresses every operation and excludes diagnostics/overview");
		view.clearSelection();
		CountPresentation probe; auto* original = view.model(); probe.setSourceModel(original); view.setModel(&probe);
		view.show(); int ticks = 0; QTimer timer; timer.setInterval(0); QObject::connect(&timer, &QTimer::timeout, &app, [&] { ++ticks; }); timer.start();
		const auto render = [&](const QString& name) {
			const auto directory = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT"); if (directory.isEmpty()) { return true; }
			QImage image(view.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); view.render(&image);
			return image.save(QDir(directory).filePath(QStringLiteral("package-staging-%1-%2.png").arg(name).arg(scale)));
		};
		ok &= expect(until([&] { view.scrollToTop(); const auto index = probe.index(2, 0); return ticks > 1 && view.viewport()->rect().contains(view.visualRect(index).center()) && view.indexAt(view.visualRect(index).center()) == index; }) && render("overview"), "render current overview and duplicate operations");
		for (int row : {50000, 70000}) {
			const auto last = probe.index(row, 0);
			ok &= expect(until([&] { view.scrollTo(last, QAbstractItemView::PositionAtCenter); return view.viewport()->rect().contains(view.visualRect(last).center()) && view.indexAt(view.visualRect(last).center()) == last; }),
				"batched native layout reaches the last operation and last conflict");
			view.setCurrentIndex(last);
			ok &= expect(render(row == 50000 ? "last-operation" : "last-conflict"), "render final staging records");
		}
		timer.stop();
		ok &= expect(probe.formatted < 5000 && ticks > 1 && view.horizontalScrollBar()->maximum() == 0,
			"large staging layout dispatches events and formats only a bounded visible subset");
		std::cout << "70,000 records at " << scale << "%: " << probe.formatted << " presentation requests, " << probe.layout << " constant-size requests, " << ticks << " event ticks\n";
		ok &= expect(probe.index(50000, 0).data(Qt::UserRole).toString() == QStringLiteral("renamed/049999.bin")
			&& probe.index(70000, 0).data(Qt::AccessibleTextRole).toString().contains("Diagnostic 19999")
			&& !probe.index(70000, 0).data(Qt::UserRole + 5).isValid(), "last-row reveal paths and complete conflict diagnostics retain their semantics");
		view.setModel(original);
		auto changed = operations; changed[1].virtualPath = QStringLiteral("CHANGED");
		ok &= expect(original->index(2, 0).data(Qt::UserRole).toString() == QStringLiteral("THINGS"), "a copied operation vector cannot mutate the displayed snapshot");
		view.setContents(before, changed, conflicts, after);
		ok &= expect(!view.currentIndex().isValid() && detail.isEmpty() && view.selectedOperationIds().isEmpty(), "new state clears stale selection and details");
		view.showMessage(QStringLiteral("Package closed"));
		ok &= expect(original->rowCount() == 1 && original->flags(original->index(0, 0)) == Qt::NoItemFlags && !original->index(0, 0).data(Qt::ForegroundRole).isValid() && view.selectedOperationIds().isEmpty(), "closing releases old records and leaves no actionable rows");
		if (scale == 200) { app.removeTranslator(&translator); }
	}
	return ok ? 0 : 1;
}
