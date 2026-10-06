#include "app/level_build_workspace_dialog.h"
#include "core/studio_settings.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMutex>
#include <QMutexLocker>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <atomic>

namespace vibestudio {
struct LevelBuildWorkspaceDialog::Work {
	std::atomic_bool cancel{false};
	QMutex mutex;
	QString phase;
	qint64 completed = 0, total = 0;
	LevelBuildWorkspace result;
};

LevelBuildWorkspaceDialog::LevelBuildWorkspaceDialog(const LevelMapDocument& document, std::shared_ptr<const PackageArchiveReader> archive,
													 QWidget* parent)
	: QDialog(parent), m_document(document), m_archive(std::move(archive)) {
	setObjectName(QStringLiteral("levelBuildWorkspaceDialog"));
	setWindowTitle(tr("Prepare Build Workspace"));
	setAccessibleName(windowTitle());
	resize(800, 580);
	m_reducedMotion = StudioSettings().accessibilityPreferences().reducedMotion;
	auto* layout = new QVBoxLayout(this);
	auto* scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto* contents = new QWidget;
	auto* column = new QVBoxLayout(contents);
	auto* context = new QLabel(tr("Map: %1\nAssets: %2")
								   .arg(document.sourcePath.isEmpty() ? document.mapName : document.sourcePath,
										m_archive ? m_archive->sourcePath() : tr("No package open")));
	context->setTextFormat(Qt::PlainText);
	context->setWordWrap(true);
	context->setAccessibleName(tr("Build snapshot sources"));
	column->addWidget(context);
	auto* form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_target = new QComboBox;
	m_target->setObjectName("levelBuildTarget");
	m_target->setAccessibleName(tr("Build target game"));
	m_target->setAccessibleDescription(tr("Quake and Quake II share classic MAP syntax. Choose the game that will load this build."));
	if (document.format == LevelMapFormat::QuakeMap) {
		m_target->addItem(tr("Quake · PAK"), QStringLiteral("quake"));
		m_target->addItem(tr("Quake II · PAK"), QStringLiteral("quake2"));
	} else {
		m_target->addItem(tr("Quake III · PK3"), QStringLiteral("quake3"));
	}
	m_target->setCurrentIndex(m_target->findData(levelBuildTargetForDocument(document)));
	form->addRow(tr("Target"), m_target);
	m_name = new QLineEdit(QStringLiteral("studio_build"));
	m_name->setMaxLength(64);
	m_name->setObjectName("levelBuildMapName");
	m_name->setAccessibleName(tr("Compiled map name"));
	m_name->setAccessibleDescription(tr("Use letters, numbers, hyphens or underscores."));
	m_name->setLayoutDirection(Qt::LeftToRight);
	form->addRow(tr("Map name"), m_name);
	m_directory = new QLineEdit;
	m_directory->setObjectName("levelBuildDirectory");
	m_directory->setAccessibleName(tr("New build workspace directory"));
	m_directory->setAccessibleDescription(tr("A new directory outside the source asset folder or package draft."));
	m_directory->setLayoutDirection(Qt::LeftToRight);
	form->addRow(tr("Workspace"), m_directory);
	m_browse = new QPushButton(tr("Choose Parent Folder…"));
	m_browse->setAccessibleName(tr("Choose the build workspace parent folder"));
	form->addRow(QString(), m_browse);
	connect(m_browse, &QPushButton::clicked, this, [this] {
		const auto parent =
			QFileDialog::getExistingDirectory(this, tr("Build Workspace Parent"), QFileInfo(m_directory->text()).absolutePath());
		if (!parent.isEmpty()) {
			m_directory->setText(QDir(parent).filePath(m_name->text()));
		}
	});
	m_limit = new QSpinBox;
	m_limit->setRange(1, 1024 * 1024);
	m_limit->setValue(4096);
	m_limit->setSuffix(tr(" MiB"));
	m_limit->setObjectName("levelBuildByteLimit");
	m_limit->setAccessibleName(tr("Maximum snapshot size"));
	m_limit->setAccessibleDescription(tr("The complete copied asset snapshot and current map must fit within this size."));
	form->addRow(tr("Size limit"), m_limit);
	column->addLayout(form);
	m_status = new QLabel(tr("Ready to capture the current map and complete asset snapshot."));
	m_status->setObjectName("levelBuildStatus");
	m_status->setAccessibleName(tr("Build preparation status"));
	m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText);
	column->addWidget(m_status);
	m_progress = new QProgressBar;
	m_progress->setTextVisible(false);
	m_progress->setAccessibleName(tr("Build preparation progress"));
	m_progress->hide();
	column->addWidget(m_progress);
	m_details = new QPlainTextEdit;
	m_details->setReadOnly(true);
	m_details->setAccessibleName(tr("Prepared inputs and dependency details"));
	m_details->setObjectName("levelBuildDetails");
	m_details->setMinimumHeight(fontMetrics().lineSpacing() * 6);
	column->addWidget(m_details, 1);
	scroll->setWidget(contents);
	layout->addWidget(scroll, 1);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
	m_prepare = buttons->addButton(tr("Prepare"), QDialogButtonBox::ActionRole);
	m_prepare->setObjectName("levelBuildPrepare");
	m_prepare->setAccessibleName(tr("Prepare the build workspace"));
	m_use = buttons->addButton(tr("Use in Build"), QDialogButtonBox::AcceptRole);
	m_use->setObjectName("levelBuildUse");
	m_use->setAccessibleName(tr("Use the prepared workspace in Build"));
	m_use->setEnabled(false);
	buttons->button(QDialogButtonBox::Cancel)->setAccessibleName(tr("Cancel build preparation"));
	connect(m_prepare, &QPushButton::clicked, this, [this] { prepare(); });
	connect(buttons, &QDialogButtonBox::accepted, this, &LevelBuildWorkspaceDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &LevelBuildWorkspaceDialog::reject);
	layout->addWidget(buttons);
	connect(m_directory, &QLineEdit::textChanged, this, [this] { changed(); });
	connect(m_name, &QLineEdit::textChanged, this, [this] { changed(); });
	connect(m_limit, &QSpinBox::valueChanged, this, [this] { changed(); });
	connect(m_target, &QComboBox::currentIndexChanged, this, [this] { changed(); });
	m_poll = new QTimer(this);
	m_poll->setInterval(40);
	connect(m_poll, &QTimer::timeout, this, [this] {
		if (!m_work || m_closed) {
			return;
		}
		QMutexLocker lock(&m_work->mutex);
		QString status = m_work->phase;
		if (m_work->total > 0) {
			m_progress->setRange(0, 1000);
			m_progress->setValue(static_cast<int>(1000.0 * std::clamp(m_work->completed, qint64(0), m_work->total) / m_work->total));
			status += QStringLiteral("\n") + QChar(0x2066) + locale().toString(m_work->completed) + QStringLiteral(" / ") +
					  locale().toString(m_work->total) + QChar(0x2069);
		} else {
			m_progress->setRange(0, m_reducedMotion ? 1 : 0);
		}
		m_status->setText(status);
	});
}
LevelBuildWorkspaceDialog::~LevelBuildWorkspaceDialog() {
	if (m_work) {
		m_work->cancel = true;
	}
}
void LevelBuildWorkspaceDialog::setDestination(const QString& directory, const QString& mapName) {
	m_directory->setText(directory);
	m_name->setText(mapName);
}
void LevelBuildWorkspaceDialog::setApplyHandler(std::function<bool(const LevelBuildWorkspace&, QString*)> handler) {
	m_apply = std::move(handler);
}
bool LevelBuildWorkspaceDialog::isReady() const { return m_result.ready; }
const LevelBuildWorkspace& LevelBuildWorkspaceDialog::workspace() const { return m_result; }
void LevelBuildWorkspaceDialog::changed() {
	if (!m_thread) {
		m_result = {};
		m_use->setEnabled(false);
		m_prepare->setEnabled(true);
	}
}
void LevelBuildWorkspaceDialog::prepare() {
	if (m_thread || !m_archive) {
		return;
	}
	LevelBuildWorkspaceRequest request;
	request.directory = m_directory->text().trimmed();
	request.mapName = m_name->text().trimmed();
	request.target = m_target->currentData().toString();
	request.maximumBytes = quint64(m_limit->value()) * 1024 * 1024;
	auto work = std::make_shared<Work>();
	work->phase = tr("Preparing the build workspace…");
	m_work = work;
	m_result = {};
	m_use->setEnabled(false);
	m_prepare->setEnabled(false);
	for (auto* field : QVector<QWidget*>{m_name, m_directory, m_limit, m_browse, m_target}) {
		field->setEnabled(false);
	}
	m_progress->setRange(0, m_reducedMotion ? 1 : 0);
	m_progress->show();
	m_status->setText(tr("Preparing the build workspace…"));
	m_details->clear();
	m_thread = QThread::create([work, document = m_document, archive = m_archive, request] {
		PackageReadControl control;
		control.isCancelled = [work] { return work->cancel.load(); };
		control.progress = [work](const QString& phase, qint64 done, qint64 total) {
			QMutexLocker lock(&work->mutex);
			work->phase = phase;
			work->completed = done;
			work->total = total;
		};
		work->result = prepareLevelBuildWorkspace(document, *archive, request, control);
	});
	connect(m_thread, &QThread::finished, this, [this, work] {
		m_thread = nullptr;
		m_poll->stop();
		m_progress->hide();
		if (m_closed) {
			return;
		}
		m_result = work->result;
		m_status->setText(levelBuildWorkspaceText(m_result));
		m_details->setPlainText(levelDependencyReportText(m_result.dependencies) + QStringLiteral("\n\n") +
								QString::fromUtf8(QJsonDocument(levelBuildWorkspaceJson(m_result)).toJson()));
		m_use->setEnabled(m_result.ready);
		m_prepare->setEnabled(!m_result.ready);
		for (auto* field : QVector<QWidget*>{m_name, m_directory, m_limit, m_browse, m_target}) {
			field->setEnabled(true);
		}
	});
	connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
	m_poll->start();
	m_thread->start();
}
void LevelBuildWorkspaceDialog::accept() {
	if (!m_result.ready || m_thread) {
		return;
	}
	QString error;
	if (m_apply && !m_apply(m_result, &error)) {
		m_status->setText(error);
		return;
	}
	QDialog::accept();
}
void LevelBuildWorkspaceDialog::reject() {
	m_closed = true;
	if (m_work) {
		m_work->cancel = true;
	}
	QDialog::reject();
}
} // namespace vibestudio
