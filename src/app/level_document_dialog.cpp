#include "app/level_document_dialog.h"
#include "core/studio_settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>
#include <QVBoxLayout>

namespace vibestudio
{

NewLevelMapDialog::NewLevelMapDialog(QWidget* parent) : QDialog(parent)
{
	setObjectName(QStringLiteral("newLevelMapDialog"));
	setWindowTitle(tr("New Map"));
	setAccessibleName(windowTitle());
	resize(520, 510);
	auto* layout = new QVBoxLayout(this);
	auto* scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto* content = new QWidget;
	auto* form = new QFormLayout(content);
	form->setRowWrapPolicy(QFormLayout::WrapLongRows);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	const auto field = [form](const QString& name, const QString& label, const QString& value, const QString& description) {
		auto* edit = new QLineEdit(value);
		edit->setObjectName(name);
		edit->setAccessibleName(label);
		edit->setAccessibleDescription(description);
		edit->setToolTip(description);
		form->addRow(label, edit);
		return edit;
	};
	m_name =
		field(QStringLiteral("newMapName"), tr("Name"), tr("Untitled"), tr("The document name; choose its file location when saving."));
	m_name->setMaxLength(128);
	m_game = new QComboBox;
	m_game->setObjectName(QStringLiteral("newMapGame"));
	m_game->setAccessibleName(tr("Game format"));
	m_game->setAccessibleDescription(tr("The map dialect and default compiler profile."));
	for (const auto& game :
		 {QStringLiteral("quake"), QStringLiteral("quake2"), QStringLiteral("quake3"), QStringLiteral("doom"), QStringLiteral("hexen")}) {
		const QString name = game == QStringLiteral("quake")	? tr("Quake")
							 : game == QStringLiteral("quake2") ? tr("Quake II")
							 : game == QStringLiteral("quake3") ? tr("Quake III Arena")
							 : game == QStringLiteral("doom")	? tr("Doom (binary)")
																: tr("Hexen (binary)");
		m_game->addItem(name, game);
	}
	m_game->setCurrentIndex(2);
	form->addRow(tr("Game format"), m_game);
	m_preset = new QComboBox;
	m_preset->setObjectName(QStringLiteral("newMapPreset"));
	m_preset->setAccessibleName(tr("Starting geometry"));
	m_preset->setAccessibleDescription(tr("A room with a player start, or an empty document."));
	m_preset->addItem(tr("Room with player start"), true);
	m_preset->addItem(tr("Empty map"), false);
	form->addRow(tr("Starting geometry"), m_preset);
	m_marker = field(QStringLiteral("newMapMarker"), tr("WAD map marker"), QStringLiteral("MAP01"),
					 tr("MAP01 or E1M1, at most eight characters."));
	m_marker->setMaxLength(8);
	m_wall = field(QStringLiteral("newMapWall"), tr("Wall texture"), {},
				   tr("An asset name from your game or package. Empty uses the format default."));
	m_floor = field(QStringLiteral("newMapFloor"), tr("Floor texture"), {},
					tr("Empty uses the wall texture for brush maps, or FLOOR for Doom maps."));
	m_ceiling = field(QStringLiteral("newMapCeiling"), tr("Ceiling texture"), {},
					  tr("Empty uses the wall texture for brush maps, or CEILING for Doom maps."));
	const auto update = [this, form]() {
		const bool doom = m_game->currentIndex() >= 3;
		m_marker->setVisible(doom);
		form->labelForField(m_marker)->setVisible(doom);
		for (auto* edit : {m_wall, m_floor, m_ceiling}) {
			edit->setEnabled(m_preset->currentData().toBool());
		}
		m_wall->setPlaceholderText(doom							 ? QStringLiteral("WALL")
								   : m_game->currentIndex() == 2 ? QStringLiteral("common/caulk")
																 : QStringLiteral("WALL"));
	};
	connect(m_game, &QComboBox::currentIndexChanged, this, update);
	connect(m_preset, &QComboBox::currentIndexChanged, this, update);
	update();
	scroll->setWidget(content);
	layout->addWidget(scroll);
	auto* error = new QLabel;
	error->setObjectName(QStringLiteral("newMapError"));
	error->setWordWrap(true);
	error->setAccessibleName(tr("New map validation"));
	for (auto* edit : {m_name, m_marker, m_wall, m_floor, m_ceiling}) {
		connect(edit, &QLineEdit::textChanged, error, &QLabel::clear);
	}
	connect(m_game, &QComboBox::currentIndexChanged, error, &QLabel::clear);
	connect(m_preset, &QComboBox::currentIndexChanged, error, &QLabel::clear);
	layout->addWidget(error);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText(tr("Create Map"));
	buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("createMapButton"));
	buttons->button(QDialogButtonBox::Ok)->setAccessibleName(tr("Create map"));
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(buttons, &QDialogButtonBox::accepted, this, [this, error]() {
		LevelMapDocument document;
		QString message;
		if (!createLevelMap(request(), &document, &message)) {
			error->setText(message);
			return;
		}
		accept();
	});
	layout->addWidget(buttons);
}

LevelMapCreateRequest NewLevelMapDialog::request() const
{
	LevelMapCreateRequest result;
	result.game = m_game->currentData().toString();
	result.name = m_name->text();
	result.mapName = m_marker->text().trimmed().toUpper();
	result.starterRoom = m_preset->currentData().toBool();
	result.wallTexture = m_wall->text().trimmed();
	result.floorTexture = m_floor->text().trimmed();
	result.ceilingTexture = m_ceiling->text().trimmed();
	return result;
}

struct LevelRecoveryWriter::Result {
	QString id, path, error;
	quint64 revision = 0;
};

LevelRecoveryWriter::LevelRecoveryWriter(QString directory, QObject* parent)
	: QObject(parent), m_directory(std::move(directory)), m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
}

LevelRecoveryWriter::~LevelRecoveryWriter()
{
	finished = {};
	if (m_thread) {
		m_thread->requestInterruption();
		m_thread->wait();
		delete m_thread;
		m_thread = nullptr;
	}
	removeRetired();
}

void LevelRecoveryWriter::removeRetired()
{
	for (auto it = m_retired.begin(); it != m_retired.end();) {
		if (m_thread && m_result && m_result->id == *it) {
			++it;
			continue;
		}
		QString error;
		if (removeLevelMapRecovery(m_directory, *it, &error)) {
			it = m_retired.erase(it);
		} else {
			if (finished) {
				finished({}, error);
			}
			++it;
		}
	}
}

void LevelRecoveryWriter::retire()
{
	m_retired.insert(m_id);
	m_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	m_hasCheckpoint = false;
	m_savedRevision = 0;
	removeRetired();
}

bool LevelRecoveryWriter::checkpoint(const LevelMapDocument& document)
{
	if (m_thread || document.format == LevelMapFormat::Unknown || document.editState != QStringLiteral("modified") ||
		(m_hasCheckpoint && m_savedRevision == document.revision)) {
		return false;
	}
	auto snapshot = document;
	snapshot.undoStack.clear();
	snapshot.redoStack.clear();
	auto result = std::make_shared<Result>();
	result->id = m_id;
	result->revision = document.revision;
	m_result = result;
	const QString directory = m_directory;
	m_thread = QThread::create([snapshot = std::move(snapshot), result, directory]() {
		result->path = writeLevelMapRecovery(snapshot, directory, result->id, &result->error,
											 []() { return QThread::currentThread()->isInterruptionRequested(); });
	});
	connect(m_thread, &QThread::finished, this, [this, result]() {
		m_thread->deleteLater();
		m_thread = nullptr;
		const bool current = result->id == m_id;
		if (current && !result->path.isEmpty()) {
			m_savedRevision = result->revision;
			m_hasCheckpoint = true;
		}
		removeRetired();
		if (current && finished) {
			finished(result->path, result->error);
		}
	});
	m_thread->start();
	return true;
}

QString levelMapRecoveryDirectory()
{
	const QString root = StudioSettings::overrideFilePath().isEmpty()
							 ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
							 : QFileInfo(StudioSettings::overrideFilePath()).absolutePath();
	return QDir(root).absoluteFilePath(QStringLiteral("map-recovery"));
}

QString chooseLevelMapRecovery(QWidget* parent, const QString& directory)
{
	QDialog dialog(parent);
	dialog.setWindowTitle(QCoreApplication::translate("VibeStudioLevelRecovery", "Recover Maps"));
	dialog.resize(680, 420);
	dialog.setAccessibleName(dialog.windowTitle());
	auto* layout = new QVBoxLayout(&dialog);
	auto* automatic = new QCheckBox(QCoreApplication::translate("VibeStudioLevelRecovery", "Save recovery checkpoints every minute"));
	automatic->setAccessibleName(automatic->text());
	automatic->setToolTip(QCoreApplication::translate(
		"VibeStudioLevelRecovery", "Writes local recovery copies of modified maps. Use Save to update the actual map file."));
	automatic->setChecked(StudioSettings().levelRecoveryEnabled());
	QObject::connect(automatic, &QCheckBox::toggled, &dialog, [](bool enabled) {
		StudioSettings settings;
		settings.setLevelRecoveryEnabled(enabled);
		settings.sync();
	});
	layout->addWidget(automatic);
	auto* list = new QListWidget;
	list->setObjectName(QStringLiteral("mapRecoveryList"));
	list->setAccessibleName(dialog.windowTitle());
	list->setWordWrap(true);
	layout->addWidget(list);
	auto* detail = new QLabel;
	detail->setWordWrap(true);
	detail->setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
	detail->setAccessibleName(QCoreApplication::translate("VibeStudioLevelRecovery", "Recovery details"));
	layout->addWidget(detail);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel);
	auto* restore = buttons->button(QDialogButtonBox::Open);
	restore->setText(QCoreApplication::translate("VibeStudioLevelRecovery", "Restore"));
	auto* remove =
		buttons->addButton(QCoreApplication::translate("VibeStudioLevelRecovery", "Delete Checkpoint"), QDialogButtonBox::ActionRole);
	const auto refresh = [&]() {
		list->clear();
		for (const auto& record : listLevelMapRecoveries(directory)) {
			auto* item = new QListWidgetItem(QStringLiteral("%1 — %2\n%3")
												 .arg(record.mapName.isEmpty() ? QFileInfo(record.path).fileName() : record.mapName,
													  record.writtenUtc.toLocalTime().toString(Qt::ISODate),
													  record.sourcePath.isEmpty()
														  ? QCoreApplication::translate("VibeStudioLevelRecovery", "Unsaved map")
														  : QDir::toNativeSeparators(record.sourcePath)),
											 list);
			item->setData(Qt::UserRole, record.path);
			item->setData(Qt::UserRole + 1, record.isValid());
			item->setData(Qt::UserRole + 2, record.isValid()
												? QCoreApplication::translate(
													  "VibeStudioLevelRecovery",
													  "%1 bytes. Restoring opens unsaved edits; it does not replace the original file.")
													  .arg(record.payloadBytes)
												: record.error);
		}
		if (list->count()) {
			list->setCurrentRow(0);
		} else {
			detail->setText(QCoreApplication::translate("VibeStudioLevelRecovery", "No recovery checkpoints are available."));
		}
	};
	QObject::connect(list, &QListWidget::currentItemChanged, &dialog, [&](QListWidgetItem* item) {
		restore->setEnabled(item && item->data(Qt::UserRole + 1).toBool());
		remove->setEnabled(item);
		if (item) {
			detail->setText(item->data(Qt::UserRole + 2).toString());
		}
	});
	restore->setEnabled(false);
	remove->setEnabled(false);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	QObject::connect(remove, &QPushButton::clicked, &dialog, [&]() {
		const auto* item = list->currentItem();
		if (!item) {
			return;
		}
		if (QMessageBox::question(
				&dialog, dialog.windowTitle(),
				QCoreApplication::translate("VibeStudioLevelRecovery",
											"Delete this recovery checkpoint? The original map file will remain unchanged."),
				QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
			return;
		}
		QString error;
		if (!removeLevelMapRecovery(directory, QFileInfo(item->data(Qt::UserRole).toString()).completeBaseName(), &error)) {
			detail->setText(error);
			return;
		}
		refresh();
	});
	layout->addWidget(buttons);
	refresh();
	return dialog.exec() == QDialog::Accepted && list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : QString();
}

} // namespace vibestudio
