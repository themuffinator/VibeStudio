#include "app/level_udmf_dialog.h"
#include "core/studio_settings.h"
#include <QAbstractTableModel>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTableView>
#include <QThread>
#include <QVBoxLayout>
#include <atomic>
#include <exception>

namespace vibestudio {
namespace {
QString text(const char* value) { return QCoreApplication::translate("LevelUdmfDialog", value); }
} // namespace
class UdmfPropertyModel final : public QAbstractTableModel {
  public:
	using QAbstractTableModel::QAbstractTableModel;
	QString object;
	const QVector<LevelUdmfProperty>* properties = nullptr;
	QMap<QString, LevelUdmfPropertyEdit> pending;
	QStringList added;
	int rowCount(const QModelIndex& parent = {}) const override {
		return parent.isValid() ? 0 : int((properties ? properties->size() : 0) + added.size());
	}
	int columnCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : 3; }
	QString key(int row) const {
		return properties && row < properties->size() ? properties->at(row).name : added.value(row - (properties ? properties->size() : 0));
	}
	QString literal(int row) const {
		const auto k = key(row);
		if (pending.contains(k)) {
			return pending[k].literal;
		}
		return properties && row < properties->size() ? properties->at(row).literal : QString();
	}
	QVariant data(const QModelIndex& index, int role) const override {
		if (!index.isValid() || (role != Qt::DisplayRole && role != Qt::AccessibleTextRole && role != Qt::ToolTipRole)) {
			return {};
		}
		const auto k = key(index.row());
		if (index.column() == 0) {
			return k;
		}
		if (index.column() == 1) {
			return literal(index.row());
		}
		if (!pending.contains(k)) {
			return {};
		}
		return pending[k].remove ? text(QT_TRANSLATE_NOOP("LevelUdmfDialog", "Remove")) : text(QT_TRANSLATE_NOOP("LevelUdmfDialog", "Set"));
	}
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
		if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
			return {};
		}
		return section == 0	  ? text(QT_TRANSLATE_NOOP("LevelUdmfDialog", "Property"))
			   : section == 1 ? text(QT_TRANSLATE_NOOP("LevelUdmfDialog", "Literal value"))
							  : text(QT_TRANSLATE_NOOP("LevelUdmfDialog", "Change"));
	}
	void reset(const QString& selector, const QVector<LevelUdmfProperty>* props) {
		beginResetModel();
		object = selector;
		properties = props;
		pending.clear();
		added.clear();
		endResetModel();
	}
	void stage(const QString& key, const QString& literal, bool remove) {
		bool exists = added.contains(key);
		if (properties) {
			for (const auto& p : *properties) {
				if (p.name == key) {
					exists = true;
					break;
				}
			}
		}
		beginResetModel();
		if (!exists) {
			added << key;
		}
		pending[key] = {object, key, literal, remove};
		endResetModel();
	}
};
struct UdmfEditTask {
	std::atomic_bool cancel{false};
	LevelMapDocument candidate;
	QString error;
	bool succeeded = false;
};
LevelUdmfDialog::LevelUdmfDialog(const LevelMapDocument& document, QWidget* parent) : QDialog(parent), m_source(document) {
	setObjectName(QStringLiteral("levelUdmfDialog"));
	setWindowTitle(tr("UDMF Properties"));
	setAccessibleName(windowTitle());
	setAccessibleDescription(tr("Edit native and extension properties of the selected text map. Changes form one undo step."));
	resize(720, 600);
	auto* layout = new QVBoxLayout(this);
	m_controls = new QWidget(this);
	auto* contents = new QVBoxLayout(m_controls);
	contents->setContentsMargins(0, 0, 0, 0);
	auto* objectRow = new QHBoxLayout;
	auto* objectLabel = new QLabel(tr("&Object"));
	m_object = new QLineEdit;
	m_object->setObjectName(QStringLiteral("udmfObject"));
	m_object->setAccessibleName(tr("UDMF object"));
	m_object->setToolTip(tr("global or a block selector such as vertex:0, linedef:0, sidedef:0, sector:0 or thing:0. Extension blocks use "
							"the same type:index form."));
	objectLabel->setBuddy(m_object);
	objectRow->addWidget(objectLabel);
	objectRow->addWidget(m_object, 1);
	auto* load = new QPushButton(tr("&Load"));
	load->setObjectName(QStringLiteral("udmfLoad"));
	objectRow->addWidget(load);
	contents->addLayout(objectRow);
	m_model = new UdmfPropertyModel(this);
	m_table = new QTableView;
	m_table->setObjectName(QStringLiteral("udmfProperties"));
	m_table->setAccessibleName(tr("UDMF properties and pending changes"));
	m_table->setModel(m_model);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setWordWrap(false);
	m_table->verticalHeader()->setVisible(false);
	m_table->verticalHeader()->setDefaultSectionSize(fontMetrics().height() + 12);
	m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
	m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	m_table->horizontalHeader()->resizeSection(
		0,
		std::max(fontMetrics().horizontalAdvance(tr("Property")), fontMetrics().horizontalAdvance(QStringLiteral("textureceiling"))) + 32);
	m_table->horizontalHeader()->resizeSection(2, fontMetrics().horizontalAdvance(tr("Change")) + 32);
	contents->addWidget(m_table, 1);
	auto* form = new QFormLayout;
	m_key = new QLineEdit;
	m_key->setMaxLength(128);
	m_key->setObjectName(QStringLiteral("udmfKey"));
	m_key->setAccessibleName(tr("Property name"));
	form->addRow(tr("&Property"), m_key);
	m_literal = new QPlainTextEdit;
	m_literal->setObjectName(QStringLiteral("udmfLiteral"));
	m_literal->setAccessibleName(tr("UDMF literal value"));
	m_literal->setAccessibleDescription(
		tr("One number, true, false, keyword or quoted string. Keep quotes around texture names and other strings."));
	m_literal->setMaximumHeight(fontMetrics().lineSpacing() * 5 + 16);
	form->addRow(tr("&Value"), m_literal);
	contents->addLayout(form);
	auto* editRow = new QHBoxLayout;
	auto* set = new QPushButton(tr("&Set Property"));
	set->setObjectName(QStringLiteral("udmfSet"));
	auto* remove = new QPushButton(tr("&Remove Property"));
	remove->setObjectName(QStringLiteral("udmfRemove"));
	auto* reset = new QPushButton(tr("Reset Changes"));
	reset->setObjectName(QStringLiteral("udmfReset"));
	editRow->addWidget(set);
	editRow->addWidget(remove);
	editRow->addStretch();
	editRow->addWidget(reset);
	contents->addLayout(editRow);
	layout->addWidget(m_controls, 1);
	m_status = new QLabel;
	m_status->setWordWrap(true);
	m_status->setObjectName(QStringLiteral("udmfStatus"));
	m_status->setAccessibleName(tr("UDMF edit status"));
	layout->addWidget(m_status);
	m_progress = new QProgressBar;
	m_progress->setAccessibleName(tr("UDMF validation progress"));
	m_progress->setTextVisible(false);
	m_progress->setRange(0, StudioSettings().accessibilityPreferences().reducedMotion ? 1 : 0);
	m_progress->hide();
	layout->addWidget(m_progress);
	m_buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
	m_buttons->button(QDialogButtonBox::Apply)->setObjectName(QStringLiteral("udmfApply"));
	m_buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("udmfCancel"));
	layout->addWidget(m_buttons);
	connect(load, &QPushButton::clicked, this, [this] { loadObject(); });
	connect(m_object, &QLineEdit::returnPressed, this, [this] { loadObject(); });
	connect(set, &QPushButton::clicked, this, [this] { stage(false); });
	connect(remove, &QPushButton::clicked, this, [this] { stage(true); });
	connect(reset, &QPushButton::clicked, this, [this] {
		m_model->reset(m_model->object, m_model->properties);
		loadObject();
	});
	connect(m_buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this] { apply(); });
	connect(m_buttons, &QDialogButtonBox::rejected, this, &LevelUdmfDialog::reject);
	connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this](const QModelIndex& index) {
		if (index.isValid()) {
			m_key->setText(m_model->key(index.row()));
			m_literal->setPlainText(m_model->literal(index.row()));
		}
	});
	m_object->setText(document.selectedObjectId < 0 || document.selectionKind == LevelMapSelectionKind::None
						  ? QStringLiteral("global")
						  : levelMapSelectionRefId({document.selectionKind, document.selectedObjectId}));
	loadObject();
	m_object->setFocus();
}
void LevelUdmfDialog::setApplyHandler(std::function<bool(const LevelMapDocument&, QString*)> handler) { m_apply = std::move(handler); }
void LevelUdmfDialog::loadObject() {
	if (!m_model->pending.isEmpty()) {
		m_status->setText(tr("Apply or reset pending changes before loading another object."));
		return;
	}
	const auto selector = m_object->text().trimmed().toLower();
	const QVector<LevelUdmfProperty>* props = nullptr;
	if (m_source.doomUdmf) {
		if (selector == QStringLiteral("global")) {
			props = &m_source.doomUdmf->globals;
		} else {
			for (const auto& block : m_source.doomUdmf->blocks) {
				if (block.selector() == selector) {
					props = &block.properties;
					break;
				}
			}
		}
	}
	if (!props) {
		m_status->setText(tr("UDMF object '%1' was not found.").arg(selector));
		return;
	}
	m_model->reset(selector, props);
	m_key->clear();
	m_literal->clear();
	m_status->setText(
		tr("%1 · namespace %2 · %3 properties").arg(selector, m_source.doomUdmf->nameSpace).arg(locale().toString(props->size())));
	m_buttons->button(QDialogButtonBox::Apply)->setEnabled(false);
}
void LevelUdmfDialog::stage(bool remove) {
	if (!m_model->properties) {
		return;
	}
	const auto key = m_key->text().trimmed().toLower();
	if (key.isEmpty() || m_literal->toPlainText().size() > 1024 * 1024 || m_model->pending.size() >= 4096) {
		m_status->setText(tr("Provide a property name. Edits are limited to 4,096 properties and 1 MiB per value."));
		return;
	}
	m_model->stage(key, m_literal->toPlainText(), remove);
	m_status->setText(tr("%1 pending changes. Apply validates the map and records one undo step; rebuild nodes after saving.")
						  .arg(locale().toString(m_model->pending.size())));
	m_buttons->button(QDialogButtonBox::Apply)->setEnabled(true);
}
void LevelUdmfDialog::setBusy(bool busy) {
	m_controls->setEnabled(!busy);
	m_progress->setVisible(busy);
	m_buttons->button(QDialogButtonBox::Apply)->setEnabled(!busy && !m_model->pending.isEmpty());
}
void LevelUdmfDialog::apply() {
	if (m_task || m_model->pending.isEmpty()) {
		return;
	}
	const auto state = std::make_shared<UdmfEditTask>();
	state->candidate = m_source;
	m_task = state;
	QVector<LevelUdmfPropertyEdit> edits;
	for (const auto& edit : m_model->pending) {
		edits << edit;
	}
	setBusy(true);
	m_status->setText(tr("Validating UDMF changes…"));
	auto* worker = QThread::create([state, edits] {
		try {
			state->succeeded =
				editLevelMapUdmfProperties(&state->candidate, edits, &state->error, [state] { return state->cancel.load(); });
		} catch (const std::exception&) {
			state->error = text(QT_TRANSLATE_NOOP("LevelUdmfDialog", "UDMF validation failed. The map was not changed."));
		}
	});
	connect(worker, &QThread::finished, worker, &QObject::deleteLater);
	connect(worker, &QThread::finished, this, [this, state] {
		m_task.reset();
		setBusy(false);
		if (state->cancel) {
			QDialog::reject();
			return;
		}
		if (!state->succeeded) {
			m_status->setText(state->error);
			return;
		}
		QString error;
		if (m_apply && !m_apply(state->candidate, &error)) {
			m_status->setText(error);
			return;
		}
		accept();
	});
	worker->start();
}
void LevelUdmfDialog::reject() {
	if (m_task) {
		m_task->cancel = true;
		m_status->setText(tr("Cancelling UDMF validation…"));
		return;
	}
	QDialog::reject();
}
} // namespace vibestudio
