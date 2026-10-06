#include "app/level_dependency_dialog.h"
#include "app/package_subset_dialog.h"

#include "core/level_dependencies.h"
#include "core/package_staging.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <atomic>
#include <memory>
#include <utility>

namespace vibestudio {
namespace {


struct WorkState {
	std::atomic_bool cancel {false};
	std::atomic_int completed {0};
	std::atomic_int total {0};
	LevelDependencyReport dependencies;
	PackageWriteReport write;
};

// The worker owns only value snapshots. Closing the dialog cancels it without
// waiting in the UI or allowing a late callback into a destroyed widget.
void ownWorker(QThread* worker, QDialog* dialog, const std::shared_ptr<WorkState>& state)
{
	QObject::connect(dialog, &QObject::destroyed, worker, [state]() { state->cancel = true; });
	QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
	QObject::connect(qApp, &QCoreApplication::aboutToQuit, worker, [worker, state]() {
		state->cancel = true;
		worker->wait();
	});
	worker->start();
}

QString kindName(const QString& kind)
{
	if (kind == QStringLiteral("texture")) { return QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Texture / shader"); }
	if (kind == QStringLiteral("shader-image")) { return QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Shader image"); }
	if (kind == QStringLiteral("model")) { return QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Model"); }
	if (kind == QStringLiteral("model-material")) { return QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Model material"); }
	if (kind == QStringLiteral("sound")) { return QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Sound / music"); }
	if (kind == QStringLiteral("doom-wall")) { return QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Doom wall"); }
	if (kind == QStringLiteral("doom-flat")) { return QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Doom flat"); }
	if (kind == QStringLiteral("doom-input")) { return QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Doom material input"); }
	return kind;
}

} // namespace

QDialog* showPackageSubsetDialog(QWidget* parent, std::shared_ptr<const PackageArchiveReader> archive, const QStringList& paths)
{
	PackageSelectionRequest selection; selection.entries = paths;
	auto* dialog = new PackageSubsetDialog(std::move(archive), std::move(selection), parent);
	dialog->show(); return dialog;
}

QDialog* showLevelDependencyDialog(QWidget* parent, const LevelMapDocument& document, std::shared_ptr<const PackageArchiveReader> archive,
	std::function<void(const QStringList&)> selectObjects)
{
	auto* dialog = new QDialog(parent);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setObjectName(QStringLiteral("levelDependencyDialog"));
	dialog->setWindowTitle(QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Level Dependencies"));
	dialog->setAccessibleName(QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Level dependencies"));
	dialog->resize(1000, 650);
	auto* layout = new QVBoxLayout(dialog);
	auto* context = new QLabel(QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Map: %1 · Assets: %2").arg(document.mapName, QFileInfo(archive->sourcePath()).fileName()));
	context->setTextFormat(Qt::PlainText);
	context->setWordWrap(true);
	context->setToolTip(archive->sourcePath());
	context->setAccessibleName(QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Dependency scan sources"));
	layout->addWidget(context);
	auto* summary = new QLabel(QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Checking dependencies for %1…").arg(document.mapName));
	summary->setObjectName(QStringLiteral("dependencySummary"));
	summary->setTextFormat(Qt::PlainText);
	summary->setWordWrap(true);
	summary->setAccessibleName(QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Dependency scan status"));
	layout->addWidget(summary);
	auto* progress = new QProgressBar;
	progress->setAccessibleName(QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Dependency scan progress"));
	progress->setRange(0, 0);
	layout->addWidget(progress);
	auto* filterRow = new QHBoxLayout;
	auto* filter = new QLineEdit;
	filter->setObjectName(QStringLiteral("dependencyFilter"));
	filter->setAccessibleName(QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Filter dependencies"));
	filter->setPlaceholderText(QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Filter by reference, file, or map object"));
	filter->setClearButtonEnabled(true);
	auto* problemsOnly = new QCheckBox(QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Problems only"));
	problemsOnly->setObjectName(QStringLiteral("dependencyProblemsOnly"));
	problemsOnly->setAccessibleName(QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Show only dependency problems"));
	filterRow->addWidget(filter, 1);
	filterRow->addWidget(problemsOnly);
	layout->addLayout(filterRow);
	auto* tree = new QTreeWidget;
	tree->setObjectName(QStringLiteral("levelDependencies"));
	tree->setAccessibleName(QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Level dependency results"));
	tree->setAccessibleDescription(QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Select a dependency to inspect its searched paths and map objects. Activate a row to select its objects in the map."));
	tree->setHeaderLabels({QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Reference"), QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Kind"), QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Status"), QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Provided by")});
	tree->setRootIsDecorated(false);
	tree->setAlternatingRowColors(true);
	tree->header()->setSectionResizeMode(QHeaderView::Interactive);
	tree->header()->setStretchLastSection(true);
	tree->setColumnWidth(0, 290);
	tree->setColumnWidth(1, 145);
	tree->setColumnWidth(2, 130);
	layout->addWidget(tree, 1);
	auto* details = new QPlainTextEdit;
	details->setReadOnly(true);
	details->setAccessibleName(QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Dependency details and scan coverage"));
	details->setMaximumHeight(dialog->fontMetrics().lineSpacing() * 9 + 16);
	layout->addWidget(details);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	auto* selectButton = buttons->addButton(QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Select in Map"), QDialogButtonBox::ActionRole);
	selectButton->setAccessibleName(QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Select dependency users in the map"));
	selectButton->setEnabled(false);
	auto* copyButton = buttons->addButton(QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Copy JSON"), QDialogButtonBox::ActionRole);
	copyButton->setAccessibleName(QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Copy dependency report as JSON"));
	copyButton->setEnabled(false);
	auto* exportButton = buttons->addButton(QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Export Assets…"), QDialogButtonBox::ActionRole);
	exportButton->setObjectName(QStringLiteral("exportLevelAssets"));
	exportButton->setAccessibleName(QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Export resolved level assets"));
	exportButton->setToolTip(QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Review the resolved files and export them as a new asset package. The map and BSP are not included."));
	exportButton->setEnabled(false);
	auto* cancelButton = buttons->addButton(QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Cancel Scan"), QDialogButtonBox::ActionRole);
	cancelButton->setAccessibleName(QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Cancel dependency scan"));
	layout->addWidget(buttons);
	QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
	auto state = std::make_shared<WorkState>();
	const auto applyFilter = [=]() {
		for (int i = 0; i < tree->topLevelItemCount(); ++i) {
			auto* item = tree->topLevelItem(i);
			item->setHidden((problemsOnly->isChecked() && !item->data(0, Qt::UserRole + 1).toBool())
				|| !item->data(0, Qt::UserRole + 2).toString().contains(filter->text(), Qt::CaseInsensitive));
		}
	};
	QObject::connect(filter, &QLineEdit::textChanged, dialog, applyFilter);
	QObject::connect(problemsOnly, &QCheckBox::toggled, dialog, applyFilter);
	QObject::connect(tree, &QTreeWidget::currentItemChanged, dialog, [=](QTreeWidgetItem* item) {
		if (!item) { selectButton->setEnabled(false); return; }
		const int index = item->data(0, Qt::UserRole).toInt();
		const LevelDependency& dependency = state->dependencies.dependencies.at(index);
		selectButton->setEnabled(!dependency.selectors.isEmpty());
		details->setPlainText(QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Map objects: %1\nRequired by: %2\nSearched: %3\n%4")
			.arg(dependency.selectors.join(QStringLiteral(", ")), dependency.requiredBy.join(QStringLiteral(", ")),
				dependency.candidates.join(QStringLiteral(", ")), dependency.note) + QLatin1Char('\n') + state->dependencies.limitations.join(QLatin1Char('\n')));
	});
	const auto select = [=]() {
		if (auto* item = tree->currentItem(); item && selectObjects) {
			selectObjects(state->dependencies.dependencies.at(item->data(0, Qt::UserRole).toInt()).selectors);
		}
	};
	QObject::connect(selectButton, &QPushButton::clicked, dialog, select);
	QObject::connect(tree, &QTreeWidget::itemActivated, dialog, [select]() { select(); });
	QObject::connect(copyButton, &QPushButton::clicked, dialog, [state]() {
		QApplication::clipboard()->setText(QString::fromUtf8(QJsonDocument(levelDependencyReportJson(state->dependencies)).toJson()));
	});
	QObject::connect(exportButton, &QPushButton::clicked, dialog, [=]() {
		showPackageSubsetDialog(dialog, archive, state->dependencies.resolvedPaths);
	});
	QObject::connect(cancelButton, &QPushButton::clicked, dialog, [=]() {
		state->cancel = true;
		cancelButton->setEnabled(false);
		summary->setText(QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Cancelling dependency scan…"));
	});
	auto* timer = new QTimer(dialog);
	timer->setInterval(100);
	QObject::connect(timer, &QTimer::timeout, dialog, [=]() {
		const int total = state->total.load();
		if (total > 0) { progress->setRange(0, total); progress->setValue(state->completed.load()); }
	});
	timer->start();
	auto* worker = QThread::create([=]() {
		state->dependencies = inspectLevelDependencies(document, *archive, [state](int done, int total) {
			state->completed = done;
			state->total = total;
			return !state->cancel.load();
		});
	});
	QObject::connect(worker, &QThread::finished, dialog, [=]() {
		timer->stop();
		cancelButton->setEnabled(false);
		progress->setRange(0, 1);
		progress->setValue(state->dependencies.complete ? 1 : 0);
		const LevelDependencyReport& report = state->dependencies;
		summary->setText(report.cancelled ? QCoreApplication::translate("VibeStudioLevelDependencyDialog", "Dependency scan cancelled.")
			: QCoreApplication::translate("VibeStudioLevelDependencyDialog", "References: %1 · Files: %2 · Bytes: %3 · Problems: %4%5").arg(report.dependencies.size())
				.arg(report.resolvedPaths.size()).arg(report.totalBytes).arg(report.problemCount)
				.arg(report.complete ? QString() : QCoreApplication::translate("VibeStudioLevelDependencyDialog", " · Incomplete scan")));
		for (int i = 0; i < report.dependencies.size(); ++i) {
			const LevelDependency& dependency = report.dependencies.at(i);
			auto* item = new QTreeWidgetItem({dependency.reference, kindName(dependency.kind), levelDependencyStatusName(dependency.status), dependency.resolvedPath});
			item->setData(0, Qt::UserRole, i);
			item->setData(0, Qt::UserRole + 1, dependency.status != LevelDependencyStatus::Resolved && dependency.status != LevelDependencyStatus::Builtin);
			item->setData(0, Qt::UserRole + 2, dependency.reference + QLatin1Char(' ') + dependency.resolvedPath + QLatin1Char(' ') + dependency.selectors.join(QLatin1Char(' ')));
			item->setToolTip(0, dependency.note);
			tree->addTopLevelItem(item);
		}
		tree->resizeColumnToContents(1);
		tree->resizeColumnToContents(2);
		details->setPlainText((report.warnings + report.limitations).join(QLatin1Char('\n')));
		copyButton->setEnabled(true);
		exportButton->setEnabled(report.canExport());
		if (!report.exportSupported && !report.limitations.isEmpty()) { exportButton->setToolTip(report.limitations.last()); }
		applyFilter();
	});
	ownWorker(worker, dialog, state);
	dialog->show();
	return dialog;
}

} // namespace vibestudio
