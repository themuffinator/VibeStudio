#include "app/level_build_package_dialog.h"
#include "core/studio_settings.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMutex>
#include <QMutexLocker>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <atomic>
namespace vibestudio {
struct LevelBuildPackageDialog::Work {
	std::atomic_bool cancel{false};
	QMutex mutex;
	QString phase;
	qint64 done = 0, total = 0;
	LevelBuildArtifacts review;
	LevelBuildPackageResult result;
	LevelBuildDeploymentPlan deploymentReview;
	LevelBuildDeploymentResult deploymentResult;
};
LevelBuildPackageDialog::LevelBuildPackageDialog(LevelBuildWorkspace workspace, QWidget* parent)
	: QDialog(parent), m_workspace(std::move(workspace)) {
	setObjectName("levelBuildPackageDialog");
	setWindowTitle(tr("Publish Prepared Build"));
	setAccessibleName(windowTitle());
	resize(940, 680);
	m_reducedMotion = StudioSettings().accessibilityPreferences().reducedMotion;
	auto* layout = new QVBoxLayout(this);
	auto* scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto* contents = new QWidget;
	auto* column = new QVBoxLayout(contents);
	column->setSizeConstraint(QLayout::SetMinAndMaxSize);
	m_deploymentFields = new QWidget;
	auto* deploymentForm = new QFormLayout(m_deploymentFields);
	deploymentForm->setContentsMargins(0, 0, 0, 0);
	deploymentForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_installationLabel = new QLabel;
	m_installationLabel->setTextFormat(Qt::PlainText);
	m_installationLabel->setWordWrap(true);
	m_installationLabel->setAccessibleName(tr("Target installation"));
	deploymentForm->addRow(tr("Installation"), m_installationLabel);
	m_gameDirectory = new QLineEdit;
	m_gameDirectory->setObjectName("buildDeploymentDirectory");
	m_gameDirectory->setLayoutDirection(Qt::LeftToRight);
	m_gameDirectory->setAccessibleName(tr("Game folder"));
	m_gameDirectory->setAccessibleDescription(tr("One folder name inside the installation. Review again after changing it."));
	deploymentForm->addRow(tr("Game folder"), m_gameDirectory);
	m_pakSlot = new QSpinBox;
	m_pakSlot->setObjectName("buildDeploymentPakSlot");
	m_pakSlot->setRange(-1, qMax(0, levelBuildPakMaximumSlot(m_workspace.target)));
	m_pakSlot->setSpecialValueText(tr("Automatic"));
	m_pakSlot->setValue(-1);
	m_pakSlot->setAccessibleName(tr("PAK slot"));
	m_pakSlot->setAccessibleDescription(
		tr("Automatic reuses this map's remembered slot or chooses the first loadable free slot. Review again after changing it."));
	m_pakSlot->setToolTip(m_pakSlot->accessibleDescription());
	deploymentForm->addRow(tr("PAK slot"), m_pakSlot);
	deploymentForm->setRowVisible(m_pakSlot, levelBuildPakMaximumSlot(m_workspace.target) >= 0);
	m_allowDeployment = new QCheckBox(tr("Allow this asset deployment"));
	m_allowDeployment->setObjectName("buildDeploymentAllow");
	m_allowDeployment->setAccessibleDescription(
		tr("Allow this write to the read-only installation. Its saved permission stays unchanged."));
	deploymentForm->addRow(m_allowDeployment);
	m_launch = new QCheckBox(tr("Launch after deployment"));
	m_launch->setObjectName("buildDeploymentLaunch");
	m_launch->setAccessibleDescription(tr("Launch the reviewed executable in windowed mode after the package is verified."));
	deploymentForm->addRow(m_launch);
	column->addWidget(m_deploymentFields);
	m_deploymentFields->hide();
	auto* form = new QFormLayout;
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	m_output = new QLineEdit;
	m_output->setObjectName("buildPackageOutput");
	m_output->setLayoutDirection(Qt::LeftToRight);
	m_output->setAccessibleName(tr("Package output path"));
	m_output->setAccessibleDescription(tr("Choose an output outside the captured workspace and original asset sources."));
	form->addRow(tr("Package"), m_output);
	m_browse = new QPushButton(tr("Choose Output…"));
	m_browse->setAccessibleName(tr("Choose the package destination"));
	form->addRow(QString(), m_browse);
	connect(m_browse, &QPushButton::clicked, this, [this] {
		const auto path = QFileDialog::getSaveFileName(this, tr("Publish Prepared Build"), m_output->text(),
													   m_workspace.target == QStringLiteral("quake3") ? tr("Quake III packages (*.pk3)")
																									  : tr("Quake packages (*.pak)"),
													   nullptr, QFileDialog::DontConfirmOverwrite);
		if (!path.isEmpty()) {
			m_output->setText(path);
		}
	});
	m_source = new QCheckBox(tr("Include build sources"));
	m_source->setObjectName("buildPackageSource");
	m_source->setAccessibleDescription(tr(
		"Also include the captured editable map and its generated texture WAD, when present. Compiler diagnostics stay in the workspace."));
	form->addRow(m_source);
	m_overwrite = new QCheckBox(tr("Replace an existing package and keep a backup"));
	m_overwrite->setObjectName("buildPackageOverwrite");
	m_overwrite->setAccessibleDescription(tr("Replacement preserves the previous package as a verified .bak file."));
	form->addRow(m_overwrite);
	m_compression = new QComboBox;
	m_compression->setObjectName("buildPackageCompression");
	m_compression->setAccessibleName(tr("Package compression"));
	m_compression->addItem(tr("Default"), "default");
	m_compression->addItem(tr("Fast"), "fast");
	m_compression->addItem(tr("Best"), "best");
	m_compression->addItem(tr("Uncompressed"), "store");
	form->addRow(tr("Compression"), m_compression);
	column->addLayout(form);
	m_status = new QLabel(tr("Verifying captured inputs and compiler outputs…"));
	m_status->setObjectName("buildPackageStatus");
	m_status->setTextFormat(Qt::PlainText);
	m_status->setWordWrap(true);
	m_status->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
	m_status->setFocusPolicy(Qt::StrongFocus);
	m_status->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
	m_status->setAccessibleName(tr("Build package status"));
	column->addWidget(m_status);
	m_detailsButton = new QPushButton(tr("Details"));
	m_detailsButton->setObjectName("buildPackageDetailsToggle");
	m_detailsButton->setCheckable(true);
	m_detailsButton->setAccessibleDescription(tr("Show the complete build review and retained warnings."));
	column->addWidget(m_detailsButton);
	m_details = new QPlainTextEdit;
	m_details->setObjectName("buildPackageDetails");
	m_details->setAccessibleName(tr("Build package details"));
	m_details->setAccessibleDescription(tr("Complete review, build identifiers and warnings. Read-only and selectable."));
	m_details->setReadOnly(true);
	m_details->setMinimumHeight(fontMetrics().lineSpacing() * 4);
	m_details->setMaximumHeight(fontMetrics().lineSpacing() * 8);
	m_details->hide();
	column->addWidget(m_details);
	connect(m_detailsButton, &QPushButton::toggled, m_details, &QWidget::setVisible);
	m_progress = new QProgressBar;
	m_progress->setTextVisible(false);
	m_progress->setAccessibleName(tr("Build package progress"));
	column->addWidget(m_progress);
	m_files = new QTableWidget(0, 3);
	m_files->setObjectName("buildPackageFiles");
	m_files->setAccessibleName(tr("Reviewed package files"));
	m_files->setHorizontalHeaderLabels({tr("Path"), tr("Source"), tr("Bytes")});
	m_files->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_files->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_files->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_files->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	m_files->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
	m_files->setMinimumHeight(fontMetrics().lineSpacing() * 9);
	m_files->setWordWrap(false);
	column->addWidget(m_files, 1);
	auto* pageForm = new QFormLayout;
	m_page = new QSpinBox;
	m_page->setObjectName("buildPackagePage");
	m_page->setRange(1, 1);
	m_page->setAccessibleName(tr("Package file page"));
	pageForm->addRow(tr("Page"), m_page);
	column->addLayout(pageForm);
	scroll->setWidget(contents);
	layout->addWidget(scroll, 1);
	auto* buttons = new QDialogButtonBox;
	m_reviewButton = buttons->addButton(tr("Review Again"), QDialogButtonBox::ActionRole);
	m_reviewButton->setObjectName("buildPackageReview");
	m_publish = buttons->addButton(tr("Publish %1").arg(m_workspace.packageSuffix().toUpper()), QDialogButtonBox::ActionRole);
	m_publish->setObjectName("buildPackagePublish");
	m_publish->setAccessibleName(tr("Publish the reviewed build package"));
	m_cancel = buttons->addButton(QDialogButtonBox::Cancel);
	m_cancel->setAccessibleName(tr("Cancel the package operation or close this review"));
	layout->addWidget(buttons);
	connect(m_cancel, &QPushButton::clicked, this, &LevelBuildPackageDialog::reject);
	connect(m_reviewButton, &QPushButton::clicked, this, [this] { start(false); });
	connect(m_publish, &QPushButton::clicked, this, [this] { start(true); });
	connect(m_output, &QLineEdit::textChanged, this, [this] { updateControls(); });
	connect(m_gameDirectory, &QLineEdit::textChanged, this, [this] {
		m_deploymentReview = {};
		if (m_installation) {
			m_status->setText(tr("Game folder changed. Review again before deployment."));
		}
		updateControls();
	});
	connect(m_allowDeployment, &QCheckBox::toggled, this, [this] { updateControls(); });
	connect(m_pakSlot, &QSpinBox::valueChanged, this, [this] {
		m_deploymentReview = {};
		if (m_installation) {
			m_status->setText(tr("PAK slot changed. Review again before deployment."));
		}
		updateControls();
	});
	connect(m_launch, &QCheckBox::toggled, this, [this] { updateControls(); });
	connect(m_overwrite, &QCheckBox::toggled, this, [this] { updateControls(); });
	connect(m_source, &QCheckBox::toggled, this, [this] { refreshRows(); });
	connect(m_page, &QSpinBox::valueChanged, this, [this] { refreshRows(); });
	m_poll = new QTimer(this);
	m_poll->setInterval(50);
	connect(m_poll, &QTimer::timeout, this, [this] {
		if (!m_thread || !m_work) {
			return;
		}
		QMutexLocker lock(&m_work->mutex);
		if (m_work->cancel) {
			return;
		}
		m_status->setText(m_work->phase);
		if (m_work->total > 0) {
			m_progress->setRange(0, 1000);
			m_progress->setValue(int(1000.0 * std::clamp(m_work->done, qint64(0), m_work->total) / m_work->total));
		} else {
			m_progress->setRange(0, m_reducedMotion ? 1 : 0);
		}
	});
	updateControls();
	QTimer::singleShot(0, this, [this] { start(false); });
}
LevelBuildPackageDialog::~LevelBuildPackageDialog() {
	if (m_work) {
		m_work->cancel = true;
	}
	// The worker owns its immutable inputs and shared result. It can finish safely
	// after the dialog is destroyed; never block the GUI while cancellation settles.
}
void LevelBuildPackageDialog::setOutputPath(const QString& path) { m_output->setText(path); }
void LevelBuildPackageDialog::setPublishedHandler(std::function<void(const LevelBuildPackageResult&)> handler) {
	m_published = std::move(handler);
}
void LevelBuildPackageDialog::configureDeployment(GameInstallationProfile installation, const QString& gameDirectory, bool launchAfter) {
	if (m_thread) {
		return;
	}
	m_installation = std::move(installation);
	m_deploymentFields->show();
	m_installationLabel->setText(m_installation->displayName + QLatin1Char('\n') + QDir::toNativeSeparators(m_installation->rootPath));
	m_gameDirectory->setText(gameDirectory.isEmpty() ? defaultGameDirectory(*m_installation) : gameDirectory);
	m_allowDeployment->setVisible(m_installation->readOnly);
	m_launch->setChecked(launchAfter);
	m_output->setReadOnly(true);
	m_browse->hide();
	setWindowTitle(tr("Deploy Prepared Build"));
	setAccessibleName(windowTitle());
	m_output->setAccessibleDescription(tr("The reviewed package destination inside the selected game folder."));
	updateControls();
}
void LevelBuildPackageDialog::setDeploymentHandler(std::function<void(const LevelBuildDeploymentResult&)> handler) {
	m_deployed = std::move(handler);
}
const LevelBuildDeploymentResult& LevelBuildPackageDialog::lastDeploymentResult() const { return m_deploymentResult; }
bool LevelBuildPackageDialog::reviewReady() const {
	return m_review.verified && (!m_installation || m_deploymentReview.ready) && !m_thread;
}
bool LevelBuildPackageDialog::busy() const { return m_thread != nullptr; }
const LevelBuildPackageResult& LevelBuildPackageDialog::lastResult() const { return m_result; }
void LevelBuildPackageDialog::updateControls() {
	m_detailsButton->setEnabled(!m_thread && !m_details->toPlainText().isEmpty());
	m_reviewButton->setEnabled(!m_thread);
	m_publish->setEnabled(reviewReady() && !m_output->text().trimmed().isEmpty());
	if (m_installation) {
		m_publish->setText(m_launch->isChecked() ? tr("Deploy and Launch") : tr("Deploy Assets"));
		m_publish->setAccessibleName(m_publish->text());
		m_publish->setEnabled(reviewReady() && (!m_installation->readOnly || m_allowDeployment->isChecked()) &&
							  (!m_deploymentReview.packageExists || m_overwrite->isChecked()) &&
							  (!m_launch->isChecked() || m_deploymentReview.launch.runnable));
	}
	m_cancel->setEnabled(true);
	for (auto* field : QVector<QWidget*>{m_output, m_source, m_overwrite, m_compression, m_browse, m_gameDirectory, m_pakSlot,
										 m_allowDeployment, m_launch}) {
		field->setEnabled(!m_thread);
	}
	m_compression->setEnabled(!m_thread && m_workspace.target == QStringLiteral("quake3"));
	if (m_workspace.target != QStringLiteral("quake3")) {
		m_compression->setCurrentIndex(m_compression->findData(QStringLiteral("store")));
		m_compression->setToolTip(tr("PAK stores files without compression."));
	}
	m_progress->setVisible(m_thread != nullptr);
}
void LevelBuildPackageDialog::setDetails(const QString& text, int warningCount) {
	m_details->setPlainText(text);
	m_detailsButton->setText(warningCount > 0 ? tr("Details (%n warning(s))", nullptr, warningCount) : tr("Details"));
}
void LevelBuildPackageDialog::refreshRows() {
	QVector<QPair<LevelBuildInput, QString>> rows;
	if (m_review.verified) {
		for (const auto& input : m_workspace.inputs) {
			const bool source =
				QDir(m_workspace.directory).filePath(input.path) == m_workspace.inputPath() || input.path == m_workspace.textureWadPath();
			if (!source || m_source->isChecked()) {
				rows.append({input, source ? tr("Source map") : tr("Captured asset")});
			}
		}
		for (const auto& output : m_review.outputs) {
			const auto kind = levelBuildArtifactKind(output.path, m_workspace.mapName, m_workspace.target);
			QString role;
			if (kind == LevelBuildArtifactKind::CompiledMap) {
				role = tr("Compiled map");
			} else if (kind == LevelBuildArtifactKind::GeneratedShader) {
				role = tr("Generated shader");
			} else if (kind == LevelBuildArtifactKind::ExternalLighting) {
				role = tr("External lighting");
			} else if (kind == LevelBuildArtifactKind::ExternalLightmap) {
				role = tr("External lightmap");
			} else {
				continue;
			}
			rows.append({output, role});
		}
	}
	std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.first.path < b.first.path; });
	const QSignalBlocker blocked(m_page);
	m_page->setMaximum(std::max(1, int((rows.size() + 299) / 300)));
	const qsizetype first = (m_page->value() - 1) * 300;
	const auto count = std::clamp(rows.size() - first, qsizetype(0), qsizetype(300));
	m_files->setRowCount(int(count));
	for (int row = 0; row < count; ++row) {
		const auto& value = rows[first + row];
		const auto path = value.first.path.mid(m_workspace.assetPrefix().size());
		for (int column = 0; column < 3; ++column) {
			const auto label = column == 0 ? path : column == 1 ? value.second : locale().toString(qulonglong(value.first.bytes));
			auto* item = new QTableWidgetItem(label);
			item->setToolTip(label);
			m_files->setItem(row, column, item);
		}
	}
	m_files->setAccessibleDescription(tr("%n file(s) selected; up to 300 shown per page.", nullptr, int(rows.size())));
}
void LevelBuildPackageDialog::start(bool publish) {
	if (m_thread || (publish && (!reviewReady() || !m_publish->isEnabled()))) {
		return;
	}
	LevelBuildPackageRequest request;
	request.outputPath = m_output->text().trimmed();
	request.expectedRecordSha256 = m_review.recordSha256;
	request.includeSourceMap = m_source->isChecked();
	request.allowOverwrite = m_overwrite->isChecked();
	deflateLevelFromId(m_compression->currentData().toString(), &request.compression);
	LevelBuildDeploymentOptions deploymentOptions;
	deploymentOptions.allowReadOnlyWrite = m_allowDeployment->isChecked();
	deploymentOptions.allowOverwrite = request.allowOverwrite;
	deploymentOptions.includeSourceMap = request.includeSourceMap;
	deploymentOptions.compression = request.compression;
	deploymentOptions.launch = m_launch->isChecked();
	const auto reviewed = m_deploymentReview;
	auto work = std::make_shared<Work>();
	m_work = work;
	work->phase = publish ? tr("Publishing the reviewed build…") : tr("Verifying captured inputs and compiler outputs…");
	m_status->setText(work->phase);
	m_detailsButton->setChecked(false);
	setDetails({}, 0);
	if (publish) {
		m_result = {};
		m_deploymentResult = {};
	} else {
		m_review = {};
		m_deploymentReview = {};
		refreshRows();
	}
	m_thread = QThread::create([work, workspace = m_workspace, request, publish, installation = m_installation,
								folder = m_gameDirectory->text(), pakSlot = m_pakSlot->value(), reviewed, deploymentOptions] {
		PackageReadControl control;
		control.isCancelled = [work] { return work->cancel.load(); };
		control.progress = [work](const QString& phase, qint64 done, qint64 total) {
			QMutexLocker lock(&work->mutex);
			work->phase = phase;
			work->done = done;
			work->total = total;
		};
		if (installation) {
			if (publish) {
				work->deploymentResult = deployLevelBuild(workspace, reviewed, deploymentOptions, control);
				work->result = work->deploymentResult.publication;
			} else {
				work->deploymentReview = planLevelBuildDeployment(workspace, *installation, folder, control, pakSlot);
				work->review = work->deploymentReview.artifacts;
			}
		} else if (publish) {
			work->result = publishLevelBuildPackage(workspace, request, control);
		} else {
			work->review = inspectLevelBuildArtifacts(workspace, control);
		}
	});
	connect(m_thread, &QThread::finished, this, [this, work, publish] {
		m_thread = nullptr;
		m_poll->stop();
		if (publish) {
			QStringList warnings;
			m_result = work->result;
			m_deploymentResult = work->deploymentResult;
			m_status->setText(m_result.succeeded() ? tr("Published: %1\nSHA-256: %2").arg(m_result.write.outputPath, m_result.write.sha256)
							  : m_result.error.isEmpty() ? tr("Publication cancelled. The destination was kept.")
														 : m_result.error);
			if (m_result.succeeded() && !m_result.write.backupPath.isEmpty()) {
				m_status->setText(m_status->text() + QLatin1Char('\n') + tr("Previous package: %1").arg(m_result.write.backupPath));
			}
			warnings += m_result.write.warnings;
			if (m_result.succeeded() && m_published) {
				m_published(m_result);
			}
			if (m_installation) {
				if (m_deploymentResult.launched) {
					m_status->setText(m_status->text() + QLatin1Char('\n') +
									  tr("Game started; process %1.").arg(m_deploymentResult.processId));
				}
				if (!m_deploymentResult.error.isEmpty()) {
					m_status->setText((m_result.write.outputCommitted ? m_status->text() + QLatin1Char('\n') : QString()) +
									  m_deploymentResult.error);
				}
				warnings += m_deploymentResult.warnings;
				// Every attempt needs a fresh destination review before another write.
				m_deploymentReview = {};
				if (m_deployed) {
					m_deployed(m_deploymentResult);
				}
			}
			warnings.removeDuplicates();
			setDetails(m_status->text() + QLatin1Char('\n') + warnings.join(QLatin1Char('\n')), warnings.size());
		} else {
			m_review = work->review;
			const auto reviewText = levelBuildArtifactsText(m_review);
			m_status->setText(reviewText.section(QLatin1Char('\n'), 0, 0));
			setDetails(reviewText, m_review.warnings.size());
			if (m_installation) {
				m_deploymentReview = work->deploymentReview;
				m_output->setText(m_deploymentReview.packagePath);
				const auto deploymentText = levelBuildDeploymentPlanText(m_deploymentReview);
				m_status->setText(deploymentText.section(QLatin1Char('\n'), 0, 2));
				setDetails(deploymentText + QLatin1Char('\n') + reviewText, m_deploymentReview.warnings.size() + m_review.warnings.size());
			}
			refreshRows();
		}
		updateControls();
	});
	connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
	updateControls();
	m_poll->start();
	m_thread->start();
}
void LevelBuildPackageDialog::reject() {
	if (m_thread) {
		m_work->cancel = true;
		m_cancel->setEnabled(false);
		m_status->setText(tr("Stopping the current operation safely…"));
		return;
	}
	QDialog::reject();
}
} // namespace vibestudio
