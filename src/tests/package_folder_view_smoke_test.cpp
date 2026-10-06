#include "app/package_entry_view.h"
#include "app/package_folder_view.h"
#include "app/studio_theme.h"
#include "package_browser_test_fixture.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFont>
#include <QIdentityProxyModel>
#include <QImage>
#include <QScrollBar>
#include <QTextLayout>
#include <QThread>
#include <QTranslator>
#include <iostream>

using namespace vibestudio;
namespace {
bool expect(bool condition, const char* message) { if (!condition) { std::cerr << message << '\n'; } return condition; }
template<class Predicate> bool until(Predicate predicate) {
	QElapsedTimer elapsed; elapsed.start();
	while (!predicate() && elapsed.elapsed() < 30000) { QCoreApplication::processEvents(); QThread::msleep(1); }
	return predicate();
}
class Expanded final : public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override {
		return QByteArray(context).contains("PackageFolder")
			? QStringLiteral("[%1 — expanded]").arg(QString::fromUtf8(source)) : QString();
	}
};
class CountPresentation final : public QIdentityProxyModel {
public:
	mutable int reads = 0;
	QVariant data(const QModelIndex& index, int role) const override { ++reads; return QIdentityProxyModel::data(index, role); }
	void multiData(const QModelIndex& index, QModelRoleDataSpan roles) const override {
		reads += static_cast<int>(roles.size()); QIdentityProxyModel::multiData(index, roles);
	}
};
}

int main(int argc, char** argv)
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef Q_OS_WIN
	qputenv("QT_QPA_FONTDIR", QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("Fonts")).toLocal8Bit());
#endif
	QApplication application(argc, argv);
#ifdef Q_OS_WIN
	application.setFont(QFont(QStringLiteral("Segoe UI"), 10));
#endif
	auto source = std::make_shared<tests::BrowserFixture>();
	for (int row = 0; row < 50000; ++row) {
		PackageEntry entry; entry.virtualPath = QStringLiteral("folders/%1.dir/item.txt").arg(row, 6, 10, QLatin1Char('0'));
		entry.sizeBytes = 7; entry.sourceOrdinal = row; source->rows.append(entry);
	}
	PackageEntry warning; warning.virtualPath = QStringLiteral("folders/000010.dir"); warning.kind = PackageEntryKind::Directory;
	warning.note = QStringLiteral("Fixture directory warning"); source->rows.append(warning);
	PackageEntry empty; empty.virtualPath = QStringLiteral("empty"); empty.kind = PackageEntryKind::Directory; source->rows.append(empty);
	PackageArchive archive; QString error; bool ok = true;
	if (!expect(archive.loadSnapshot(source, &error), "admit large folder fixture")) { return 1; }
	for (const int scale : {100, 200}) {
		Expanded translator; if (scale == 200) { application.installTranslator(&translator); }
		applyStudioTheme(application, studioThemeTokens(scale == 200 ? StudioTheme::HighContrastDark : StudioTheme::Dark, UiDensity::Standard, scale));
		application.setLayoutDirection(scale == 200 ? Qt::RightToLeft : Qt::LeftToRight);
		PackageEntryView entries; PackageFolderView tree; tree.resize(780, 620); tree.show();
		QString revision = QStringLiteral("first");
		const auto synchronize = [&] {
			if (const auto index = entries.browserIndex(revision)) {
				tree.showFolders(index, revision, QStringLiteral("Folder fixture.pk3"), QStringLiteral("50,000 folders and an empty directory"), true);
			} else { tree.showMessage(entries.statusText(), entries.statusText()); }
		};
		QObject::connect(&entries, &PackageEntryView::stateChanged, &tree, synchronize);
		QObject::connect(&entries, &PackageEntryView::entriesReady, &tree, synchronize);
		entries.showEntries(archive, revision, QString(), QString());
		ok &= expect(entries.busy() && !tree.readyFor(revision) && !tree.folderIndex(QStringLiteral("empty")).isValid(),
			"pending folder metadata never exposes old actionable paths");
		if (!expect(until([&] { return !entries.busy(); }) && tree.readyFor(revision), "entry worker supplies the shared folder snapshot")) { return 1; }
		const auto snapshot = entries.browserIndex(revision);
		ok &= expect(snapshot && snapshot->folders.size() == 50003 && snapshot->summary.totalSizeBytes == 350000 && source->payloadReads == 0,
			"shared index contains all implied and empty directories without reading payloads");
		const auto root = tree.folderIndex(QString());
		ok &= expect(root.isValid() && !root.parent().isValid() && tree.model()->rowCount() == 1
			&& tree.model()->rowCount(root) == 2 && tree.model()->rowCount(tree.folderIndex(QStringLiteral("empty"))) == 0,
			"root and explicit empty folders have native tree relationships");
		bool relationships = true;
		for (const auto& node : snapshot->folders) {
			const auto index = tree.folderIndex(node.path);
			const auto parent = node.parent < 0 ? QModelIndex() : tree.folderIndex(snapshot->folders.at(node.parent).path);
			relationships &= index.isValid() && index.parent() == parent && index.row() == node.row
				&& tree.model()->index(static_cast<int>(node.row), 0, parent) == index && index.data(Qt::UserRole).toString() == node.path;
		}
		ok &= expect(relationships, "every folder round-trips through parent, row and exact path lookup");
		const auto warned = tree.folderIndex(QStringLiteral("folders/000010.dir"));
		ok &= expect(warned.data(Qt::AccessibleDescriptionRole).toString() == warning.note
			&& warned.data(Qt::ToolTipRole).toString().contains(warning.note) && warned.data(Qt::UserRole + 1).toString() == QStringLiteral("warning")
			&& warned.data(Qt::AccessibleTextRole).toString().contains(warning.virtualPath)
			&& tree.focusPolicy() != Qt::NoFocus && !tree.accessibleName().isEmpty(), "folder warnings remain readable and accessible without relying on color");
		CountPresentation probe; probe.setSourceModel(tree.model()); tree.setModel(&probe); probe.reads = 0;
		const auto parent = probe.mapFromSource(tree.folderIndex(QStringLiteral("folders")));
		tree.expand(parent.parent()); tree.expand(parent);
		const auto last = probe.index(49999, 0, parent); tree.setCurrentIndex(last); tree.scrollTo(last);
		ok &= expect(until([&] {
			const auto rectangle = tree.visualRect(last);
			return rectangle.isValid() && tree.viewport()->rect().intersects(rectangle) && tree.indexAt(rectangle.center()) == last;
		}), "last of 50,000 folders is visible before accepting its render");
		const auto captures = qEnvironmentVariable("VIBESTUDIO_TEST_CAPTURE_ROOT");
		if (!captures.isEmpty()) {
			QImage image(tree.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); tree.render(&image);
			ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-folder-large-%1.png").arg(scale))), "save native folder view render");
		}
		ok &= expect(probe.rowCount(parent) == 50000 && probe.reads < 5000, "folder presentation requests stay bounded by visible rows");
		std::cout << "Folder tree at " << scale << "%: " << probe.rowCount(parent) << " siblings, " << probe.reads << " presentation requests\n";
		if (scale == 200) {
			const auto text = last.data(Qt::DisplayRole).toString();
			QTextLayout layout(text, tree.font()); QTextOption options; options.setTextDirection(Qt::RightToLeft); layout.setTextOption(options);
			layout.beginLayout(); auto line = layout.createLine(); line.setLineWidth(tree.viewport()->width()); layout.endLayout();
			ok &= expect(text.contains(QStringLiteral("049999.dir"))
				&& line.cursorToX(text.indexOf(QStringLiteral("049999")) + 2) < line.cursorToX(text.indexOf(QStringLiteral("dir")) + 1)
				&& last.data(Qt::UserRole).toString() == QStringLiteral("folders/049999.dir"), "numeric folder names retain visual and logical order in RTL");
		}
		tree.setModel(probe.sourceModel()); tree.selectFolder(QStringLiteral("folders/049999.dir")); application.processEvents();
		int resets = 0, changes = 0;
		QObject::connect(tree.model(), &QAbstractItemModel::modelReset, &tree, [&] { ++resets; });
		QObject::connect(tree.model(), &QAbstractItemModel::dataChanged, &tree, [&] { ++changes; });
		const auto scroll = tree.verticalScrollBar()->value();
		entries.showEntries(archive, revision, QString(), QStringLiteral("item.txt")); entries.cancel();
		ok &= expect(entries.canRetry() && tree.readyFor(revision) && entries.browserIndex(revision) == snapshot
			&& tree.selectedFolder() == QStringLiteral("folders/049999.dir") && tree.verticalScrollBar()->value() == scroll
			&& resets == 0 && changes == 0, "query cancellation reuses folder metadata without resetting selection, scrolling or presentation");
		entries.retry();
		ok &= expect(until([&] { return !entries.busy(); }) && entries.entryCount() == 50000 && entries.browserIndex(revision) == snapshot,
			"query retry retains the exact immutable folder and composition snapshot");
		auto deepSource = std::make_shared<tests::BrowserFixture>();
		QStringList components;
		for (int level = 0; level < 96; ++level) { components << QStringLiteral("level-%1.dir").arg(level, 3, 10, QLatin1Char('0')); }
		const auto deepPath = components.join(QLatin1Char('/'));
		PackageEntry deepFile; deepFile.virtualPath = deepPath + QStringLiteral("/leaf.txt"); deepSource->rows.append(deepFile);
		PackageArchive deepArchive;
		ok &= expect(deepArchive.loadSnapshot(deepSource, &error), "admit a deep folder hierarchy within the shared path limit");
		revision = QStringLiteral("replacement"); entries.showEntries(deepArchive, revision, QString(), QString());
		ok &= expect(!tree.readyFor(QStringLiteral("first")) && !tree.readyFor(revision) && !tree.folderIndex(warning.virtualPath).isValid(),
			"new document revisions invalidate folder identities immediately");
		ok &= expect(until([&] { return !entries.busy(); }) && tree.selectFolder(deepPath), "select a deeply nested folder through exact path lookup");
		const auto deep = tree.folderIndex(deepPath);
		tree.doItemsLayout(); application.processEvents(); tree.scrollTo(deep); application.processEvents();
		const auto deepRectangle = tree.visualRect(deep);
		const auto visible = tree.viewport()->rect().intersected(deepRectangle);
		const auto caption = deep.data(Qt::DisplayRole).toString();
		ok &= expect(deepRectangle.isValid() && visible.width() >= tree.fontMetrics().horizontalAdvance(caption)
			&& tree.indexAt(visible.center()) == deep, "a 96-level folder remains visible and readable through horizontal navigation");
		std::cout << "Deep folder at " << scale << "%: row width=" << deepRectangle.width() << " visible width=" << visible.width()
			<< " caption width=" << tree.fontMetrics().horizontalAdvance(caption) << " horizontal range=" << tree.horizontalScrollBar()->maximum()
			<< " horizontal position=" << tree.horizontalScrollBar()->value() << " row x=" << deepRectangle.x() << '\n';
		if (!captures.isEmpty()) {
			QImage image(tree.size(), QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); tree.render(&image);
			ok &= expect(image.save(QDir(captures).filePath(QStringLiteral("package-folder-deep-%1.png").arg(scale))), "save deep folder navigation render");
		}
		entries.showMessage(QStringLiteral("Closed")); application.processEvents();
		ok &= expect(!tree.readyFor(revision) && tree.model()->rowCount() == 1
			&& !tree.model()->flags(tree.model()->index(0, 0)).testFlag(Qt::ItemIsSelectable)
			&& tree.model()->index(0, 0).data(Qt::AccessibleTextRole).toString() == QStringLiteral("Closed"), "closed tree exposes a readable status without actionable folders");
		if (scale == 200) { application.removeTranslator(&translator); }
	}
	return ok ? 0 : 1;
}
