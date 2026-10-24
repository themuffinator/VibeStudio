#include "app/model_assembly_dialog.h"
#include "core/model_player_import.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace vibestudio
{
namespace
{
struct Text
{
	Q_DECLARE_TR_FUNCTIONS(ModelAssemblyPlayerImport)
};
} // namespace
bool ModelAssemblyDialog::openPlayer(const QString &player, const QString &skin, QString *error)
{
	if (error)
		error->clear();
	play(false);
	const auto refuse = [&](const QString &message) {
		report(message);
		if (error)
			*error = message;
		return false;
	};
	const auto archive = m_materialSource.archive;
	if (!archive || !archive->isOpen())
		return refuse(Text::tr("Open a package or folder before opening a player."));
	ModelQ3PlayerImport imported;
	if (!performWork(
			Text::tr("Open Player"),
			[&](QString *failure, const ModelWorkControl &control) { return importModelQ3Player(*archive, player, skin, &imported, failure, control); },
			error))
		return false;
	// The parts are resolved against the current package; one that changed
	// while the player was read could hold other files.
	if (archive != m_materialSource.archive)
		return refuse(Text::tr("The package changed while the player was read. Open the player again."));
	ModelAssemblyDocument candidate;
	QString failure;
	if (!candidate.setAssembly(imported.assembly, m_document.directory(), &failure))
		return refuse(failure);
	QString inputError;
	if (!adopt(std::move(candidate), &inputError, true))
	{
		if (error)
			*error = inputError;
		return false;
	}
	m_playerBundleOptions = imported.publication;
	QStringList status{Text::tr("Opened player %1 with skin %2 as a new, unsaved assembly. Save it to keep it; the package is not changed.")
						   .arg(imported.player.name, imported.skin)};
	if (!inputError.isEmpty())
		status << Text::tr("Preview unavailable: %1").arg(inputError);
	// The first note repeats what the status says.
	status += imported.notes.mid(1);
	report(status.join(QLatin1Char(' ')));
	if (error)
		*error = inputError;
	return true;
}
void ModelAssemblyDialog::choosePlayer()
{
	if (m_working)
		return;
	play(false);
	const auto source = m_materialSource;
	if (!source.archive || !source.archive->isOpen())
	{
		report(Text::tr("Open a package or folder before opening a player."));
		return;
	}
	QVector<ModelQ3PlayerListing> players;
	QString error;
	if (!performWork(
			Text::tr("Find Players"),
			[&](QString *failure, const ModelWorkControl &control) { return listModelQ3Players(*source.archive, &players, failure, control); },
			&error))
		return;
	if (players.isEmpty())
	{
		report(Text::tr("The current package has no Quake III players under models/players/."));
		return;
	}
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("assemblyOpenPlayerDialog"));
	dialog.setWindowTitle(Text::tr("Open Quake III Player"));
	dialog.setAccessibleName(dialog.windowTitle());
	dialog.resize(560, 520);
	auto *layout = new QVBoxLayout(&dialog);
	const auto field = [&](QWidget *control, const char *id, const QString &name, const QString &description, int stretch) {
		control->setObjectName(QString::fromLatin1(id));
		control->setAccessibleName(name);
		control->setAccessibleDescription(description);
		control->setToolTip(description);
		control->setMinimumWidth(0);
		auto *label = new QLabel(name);
		label->setTextFormat(Qt::PlainText);
		label->setWordWrap(true);
		label->setBuddy(control);
		layout->addWidget(label);
		layout->addWidget(control, stretch);
	};
	auto *list = new QListWidget;
	list->setUniformItemSizes(true);
	field(list, "assemblyPlayerList", Text::tr("Players"),
		  Text::tr("Quake III players under models/players/ in the current package or folder. A player that cannot be opened says why in "
				   "the summary."),
		  2);
	auto *skin = new QComboBox;
	skin->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	skin->setMinimumContentsLength(10);
	field(skin, "assemblyPlayerSkin", Text::tr("Skin"),
		  Text::tr("The skin each part links from the package. Player Package keeps the other complete skins."), 0);
	auto *summary = new QPlainTextEdit;
	summary->setReadOnly(true);
	summary->setTabChangesFocus(true);
	summary->setLineWrapMode(QPlainTextEdit::WidgetWidth);
	summary->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
	field(summary, "assemblyPlayerSummary", Text::tr("Player summary"),
		  Text::tr("The player's skins, detail levels and custom sounds, and anything that stops it from opening."), 3);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
	auto *open = buttons->addButton(Text::tr("Open"), QDialogButtonBox::AcceptRole);
	open->setObjectName(QStringLiteral("assemblyPlayerOpen"));
	open->setAccessibleName(open->text());
	open->setAccessibleDescription(Text::tr("Open the player as a new, unsaved assembly. Unsaved changes to this assembly are offered for saving "
											"first."));
	open->setDefault(true);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	int first = -1;
	for (int row = 0; row < players.size(); ++row)
	{
		const auto &player = players[row];
		// The state is in the text, not only in a colour.
		auto *item = new QListWidgetItem(player.canOpen() ? player.name : Text::tr("%1 (cannot be opened)").arg(player.name), list);
		item->setData(Qt::AccessibleDescriptionRole, modelQ3PlayerListingText(player).join(QLatin1Char('\n')));
		item->setToolTip(player.directory);
		if (first < 0 && player.canOpen())
			first = row;
	}
	const auto update = [&] {
		const int row = list->currentRow();
		skin->clear();
		summary->clear();
		open->setEnabled(false);
		if (row < 0 || row >= players.size())
			return;
		const auto &player = players[row];
		for (const auto &name : player.completeSkins())
			skin->addItem(name, name);
		skin->setEnabled(skin->count() > 0);
		summary->setPlainText(modelQ3PlayerListingText(player).join(QLatin1Char('\n')));
		open->setEnabled(player.canOpen());
	};
	connect(list, &QListWidget::currentRowChanged, &dialog, update);
	connect(list, &QListWidget::itemActivated, &dialog, [&] {
		if (open->isEnabled())
			dialog.accept();
	});
	list->setCurrentRow(std::max(0, first));
	update();
	list->setFocus(Qt::OtherFocusReason);
	if (dialog.exec() != QDialog::Accepted || list->currentRow() < 0 || !players[list->currentRow()].canOpen())
		return;
	const auto name = players[list->currentRow()].name;
	const auto chosen = skin->currentData().toString();
	if (source.archive != m_materialSource.archive || source.revision != m_materialSource.revision)
	{
		report(Text::tr("The package changed while choosing a player. Choose it again."));
		return;
	}
	if (!maybeSave())
		return;
	openPlayer(name, chosen, &error);
}
} // namespace vibestudio
