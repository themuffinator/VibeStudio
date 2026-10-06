#include "app/texture_generation_dialog.h"

#include "core/studio_settings.h"

#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

#include <atomic>

namespace vibestudio {

struct TextureGenerationDialog::Work {
	std::atomic_bool discard = false;
	QThread* thread = nullptr;
	QVector<GeneratedTexture> textures;
	QVector<TextureGenerationSpec> specs;
	IdTechPaletteResolution palette;
};

namespace {

QByteArray pngBytes(const QImage& image)
{
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	image.save(&buffer, "PNG");
	return bytes;
}

// A texture tiled 2x2 so a seam would show as a cross.
QPixmap tiledPreview(const QImage& image, int side)
{
	QImage tiles(side, side, QImage::Format_ARGB32_Premultiplied);
	tiles.fill(Qt::black);
	QPainter painter(&tiles);
	const QImage cell = image.scaled(side / 2, side / 2, Qt::IgnoreAspectRatio, Qt::FastTransformation);
	for (int y = 0; y < 2; ++y) {
		for (int x = 0; x < 2; ++x) {
			painter.drawImage(x * side / 2, y * side / 2, cell);
		}
	}
	painter.end();
	return QPixmap::fromImage(tiles);
}

} // namespace

TextureGenerationDialog::TextureGenerationDialog(QWidget* parent, TextureGenerationDialogHooks hooks)
	: QDialog(parent)
	, m_hooks(std::move(hooks))
	, m_work(std::make_shared<Work>())
	, m_client(std::make_unique<AiImageClient>())
{
	setObjectName(QStringLiteral("textureGenerationDialog"));
	setWindowTitle(tr("Generate Texture"));
	setAccessibleName(windowTitle());
	setAccessibleDescription(tr("Describe a texture, draw variants with the image model or start from a picture, review them tiled, then save one where the game reads it."));
	resize(1080, 720);
	auto* root = new QVBoxLayout(this);
	auto* splitter = new QSplitter(Qt::Horizontal);
	splitter->setChildrenCollapsible(false);
	root->addWidget(splitter, 1);

	auto* form = new QWidget;
	auto* formLayout = new QVBoxLayout(form);
	formLayout->setContentsMargins(0, 0, 8, 0);
	auto* promptLabel = new QLabel(tr("&Describe the texture"));
	m_prompt = new QPlainTextEdit;
	m_prompt->setObjectName(QStringLiteral("textureGenerationPrompt"));
	m_prompt->setAccessibleName(tr("Texture description"));
	m_prompt->setPlaceholderText(tr("For example: rusted riveted metal plate with a yellow hazard stripe"));
	m_prompt->setTabChangesFocus(true);
	m_prompt->setMaximumHeight(96);
	promptLabel->setBuddy(m_prompt);
	formLayout->addWidget(promptLabel);
	formLayout->addWidget(m_prompt);

	auto* options = new QFormLayout;
	options->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	m_game = new QComboBox;
	m_game->setObjectName(QStringLiteral("textureGenerationGame"));
	m_game->setAccessibleName(tr("Game"));
	for (const TextureGameProfile& profile : textureGameProfiles()) {
		m_game->addItem(profile.displayName, profile.id);
	}
	const QString game = m_hooks.defaultGame ? m_hooks.defaultGame() : QString();
	TextureGameProfile wanted;
	if (textureGameProfileForId(game, &wanted)) {
		m_game->setCurrentIndex(std::max(0, m_game->findData(wanted.id)));
	}
	options->addRow(tr("&Game:"), m_game);
	m_surface = new QComboBox;
	m_surface->setAccessibleName(tr("Surface"));
	const QVector<QPair<QString, QString>> surfaces = {
		{tr("Wall"), QStringLiteral("wall")},
		{tr("Floor"), QStringLiteral("floor")},
		{tr("Ceiling"), QStringLiteral("ceiling")},
		{tr("Trim strip"), QStringLiteral("trim")},
		{tr("Panel"), QStringLiteral("panel")},
		{tr("Liquid"), QStringLiteral("liquid")},
		{tr("Sky"), QStringLiteral("sky")},
	};
	for (const auto& surface : surfaces) {
		m_surface->addItem(surface.first, surface.second);
	}
	options->addRow(tr("&Surface:"), m_surface);
	m_style = new QLineEdit;
	m_style->setAccessibleName(tr("Art direction"));
	m_style->setPlaceholderText(tr("Optional: gothic, worn, hand-painted…"));
	options->addRow(tr("St&yle:"), m_style);
	m_size = new QComboBox;
	m_size->setAccessibleName(tr("Texture size"));
	m_size->addItem(tr("The game's usual size"), QSize());
	for (const QSize& size : {QSize(32, 32), QSize(64, 64), QSize(128, 128), QSize(256, 256), QSize(512, 512), QSize(64, 128), QSize(128, 64), QSize(128, 256)}) {
		m_size->addItem(QStringLiteral("%1 × %2").arg(size.width()).arg(size.height()), size);
	}
	options->addRow(tr("Si&ze:"), m_size);
	m_missing = new QComboBox;
	m_missing->setObjectName(QStringLiteral("textureGenerationMissing"));
	m_missing->setAccessibleName(tr("Textures the open map is missing"));
	m_missing->setToolTip(tr("Pick one to make it under the name the map already uses, as one variant so the name stays exact."));
	options->addRow(tr("&Missing:"), m_missing);
	connect(m_missing, &QComboBox::activated, this, [this](int index) {
		const QString name = m_missing->itemData(index).toString();
		if (!name.isEmpty()) {
			useMissingTexture(name);
		}
	});
	m_name = new QLineEdit;
	m_name->setObjectName(QStringLiteral("textureGenerationName"));
	m_name->setAccessibleName(tr("Texture name"));
	m_name->setPlaceholderText(tr("From the description"));
	options->addRow(tr("&Name:"), m_name);
	m_directory = new QLineEdit;
	m_directory->setAccessibleName(tr("Texture folder"));
	m_directory->setPlaceholderText(QStringLiteral("vibestudio"));
	m_directory->setToolTip(tr("Quake II and III textures live in a folder under textures/; maps name them folder/name."));
	options->addRow(tr("&Folder:"), m_directory);
	m_wad = new QLineEdit;
	m_wad->setAccessibleName(tr("WAD for the texture"));
	m_wad->setPlaceholderText(tr("wads/vibestudio_generated.wad in the project"));
	m_wad->setToolTip(tr("Quake textures go into a WAD2 and Doom flats and patches into a PWAD; it is created when missing."));
	options->addRow(tr("&WAD:"), m_wad);
	m_variants = new QSpinBox;
	m_variants->setObjectName(QStringLiteral("textureGenerationVariantCount"));
	m_variants->setAccessibleName(tr("Variants"));
	m_variants->setRange(1, 4);
	m_variants->setValue(2);
	m_variants->setToolTip(tr("How many pictures to draw and compare."));
	options->addRow(tr("&Variants:"), m_variants);
	formLayout->addLayout(options);
	m_seamless = new QCheckBox(tr("Make it tile seamlessly"));
	m_seamless->setChecked(true);
	m_companions = new QCheckBox(tr("Companion maps for source ports (normal, gloss, glow)"));
	m_companions->setToolTip(tr("DarkPlaces, FTE and QuakeSpasm-family ports read _norm, _gloss and _glow beside a texture; ioquake3's renderer reads _n and _s."));
	m_dither = new QCheckBox(tr("Dither to the palette"));
	m_fullbrights = new QCheckBox(tr("Let bright details glow (Quake fullbright colours)"));
	for (QCheckBox* box : {m_seamless, m_companions, m_dither, m_fullbrights}) {
		box->setAccessibleName(box->text());
		formLayout->addWidget(box);
	}

	auto* source = new QGroupBox(tr("Where the picture comes from"));
	auto* sourceLayout = new QVBoxLayout(source);
	m_modelSource = new QRadioButton(tr("The &image model"));
	m_modelSource->setObjectName(QStringLiteral("textureGenerationModelSource"));
	m_modelSource->setAccessibleName(tr("Draw with the image model"));
	m_modelSource->setChecked(true);
	m_sourceStatus = new QLabel;
	m_sourceStatus->setObjectName(QStringLiteral("textureGenerationSourceStatus"));
	m_sourceStatus->setWordWrap(true);
	m_sourceStatus->setTextFormat(Qt::PlainText);
	m_restyle = new QCheckBox(tr("Restyle the texture open on the Textures page"));
	m_restyle->setAccessibleName(m_restyle->text());
	m_restyle->setToolTip(tr("Send that texture to the model with the description, instead of drawing from nothing."));
	m_pictureSource = new QRadioButton(tr("A &picture of my own (no AI)"));
	m_pictureSource->setObjectName(QStringLiteral("textureGenerationPictureSource"));
	m_pictureSource->setAccessibleName(tr("Start from a picture"));
	auto* pictureRow = new QHBoxLayout;
	m_picture = new QLineEdit;
	m_picture->setObjectName(QStringLiteral("textureGenerationPicture"));
	m_picture->setAccessibleName(tr("Picture file"));
	m_picture->setPlaceholderText(tr("A photo, scan, or painting"));
	m_browsePicture = new QPushButton(tr("&Browse…"));
	m_browsePicture->setAccessibleName(tr("Choose a picture"));
	pictureRow->addWidget(m_picture, 1);
	pictureRow->addWidget(m_browsePicture);
	m_previewRequest = new QPushButton(tr("Preview Re&quest…"));
	m_previewRequest->setAccessibleName(tr("Preview the image request"));
	m_previewRequest->setToolTip(tr("Show exactly what would be sent to the image model, without sending it."));
	sourceLayout->addWidget(m_modelSource);
	sourceLayout->addWidget(m_sourceStatus);
	sourceLayout->addWidget(m_restyle);
	sourceLayout->addWidget(m_previewRequest, 0, Qt::AlignLeft);
	sourceLayout->addWidget(m_pictureSource);
	sourceLayout->addLayout(pictureRow);
	formLayout->addWidget(source);

	auto* actions = new QHBoxLayout;
	m_generate = new QPushButton(tr("&Generate"));
	m_generate->setObjectName(QStringLiteral("textureGenerationGenerate"));
	m_generate->setAccessibleName(tr("Generate the texture"));
	m_generate->setToolTip(tr("Draw or load the picture, then make it game-ready (Ctrl+Enter)."));
	m_generate->setDefault(true);
	m_cancel = new QPushButton(tr("Cancel"));
	m_cancel->setAccessibleName(tr("Cancel generation"));
	m_cancel->setEnabled(false);
	actions->addWidget(m_generate);
	actions->addWidget(m_cancel);
	actions->addStretch(1);
	formLayout->addLayout(actions);
	m_status = new QLabel(tr("Describe the texture and press Generate."));
	m_status->setObjectName(QStringLiteral("textureGenerationStatus"));
	m_status->setAccessibleName(tr("Generation status"));
	m_status->setWordWrap(true);
	m_status->setTextFormat(Qt::PlainText);
	m_status->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
	m_progress = new QProgressBar;
	m_progress->setAccessibleName(tr("Generation progress"));
	m_progress->setVisible(false);
	formLayout->addWidget(m_status);
	formLayout->addWidget(m_progress);
	formLayout->addStretch(1);
	splitter->addWidget(form);

	auto* review = new QWidget;
	auto* reviewLayout = new QVBoxLayout(review);
	reviewLayout->setContentsMargins(8, 0, 0, 0);
	auto* variantsLabel = new QLabel(tr("Va&riants, tiled 2 × 2 so seams show"));
	m_variantList = new QListWidget;
	m_variantList->setObjectName(QStringLiteral("textureGenerationVariants"));
	m_variantList->setAccessibleName(tr("Generated variants"));
	m_variantList->setViewMode(QListView::IconMode);
	m_variantList->setIconSize(QSize(192, 192));
	m_variantList->setResizeMode(QListView::Adjust);
	m_variantList->setMovement(QListView::Static);
	m_variantList->setSpacing(8);
	m_variantList->setMinimumHeight(240);
	variantsLabel->setBuddy(m_variantList);
	reviewLayout->addWidget(variantsLabel);
	reviewLayout->addWidget(m_variantList, 2);
	m_companionStrip = new QLabel;
	m_companionStrip->setObjectName(QStringLiteral("textureGenerationCompanions"));
	m_companionStrip->setAccessibleName(tr("Companion maps"));
	m_companionStrip->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	reviewLayout->addWidget(m_companionStrip);
	m_details = new QPlainTextEdit;
	m_details->setObjectName(QStringLiteral("textureGenerationDetails"));
	m_details->setAccessibleName(tr("Variant details"));
	m_details->setReadOnly(true);
	reviewLayout->addWidget(m_details, 1);
	splitter->addWidget(review);
	splitter->setSizes({400, 680});

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	m_save = buttons->addButton(tr("&Save to Project"), QDialogButtonBox::ActionRole);
	m_save->setObjectName(QStringLiteral("textureGenerationSave"));
	m_save->setAccessibleName(tr("Save the selected variant where the game reads it"));
	m_saveApply = buttons->addButton(tr("Save and &Apply to Map Selection"), QDialogButtonBox::ActionRole);
	m_saveApply->setAccessibleName(tr("Save the selected variant and put it on the selected faces"));
	m_openEditor = buttons->addButton(tr("Open in &Texture Editor"), QDialogButtonBox::ActionRole);
	m_openEditor->setAccessibleName(tr("Edit the selected variant in the Texture Editor"));
	for (QPushButton* button : {m_save, m_saveApply, m_openEditor}) {
		button->setEnabled(false);
	}
	root->addWidget(buttons);

	connect(buttons, &QDialogButtonBox::rejected, this, &TextureGenerationDialog::reject);
	connect(m_generate, &QPushButton::clicked, this, &TextureGenerationDialog::generate);
	connect(m_cancel, &QPushButton::clicked, this, &TextureGenerationDialog::cancelGeneration);
	connect(m_game, &QComboBox::currentIndexChanged, this, &TextureGenerationDialog::updateGameControls);
	connect(m_modelSource, &QRadioButton::toggled, this, &TextureGenerationDialog::refreshSourceStatus);
	connect(m_previewRequest, &QPushButton::clicked, this, &TextureGenerationDialog::showRequestPreview);
	connect(m_browsePicture, &QPushButton::clicked, this, [this] {
		const QString path = QFileDialog::getOpenFileName(this, tr("Choose a Picture"), QString(), tr("Pictures (*.png *.jpg *.jpeg *.tga *.bmp *.webp)"));
		if (!path.isEmpty()) {
			m_picture->setText(QDir::toNativeSeparators(path));
			m_pictureSource->setChecked(true);
		}
	});
	connect(m_variantList, &QListWidget::currentRowChanged, this, &TextureGenerationDialog::showSelected);
	connect(m_save, &QPushButton::clicked, this, [this] { saveSelected(false); });
	connect(m_saveApply, &QPushButton::clicked, this, [this] { saveSelected(true); });
	connect(m_openEditor, &QPushButton::clicked, this, [this] {
		const int row = m_variantList->currentRow();
		if (row >= 0 && row < m_textures.size() && m_hooks.openInEditor) {
			m_hooks.openInEditor(m_textures[row].image, m_textures[row].name);
		}
	});
	auto* shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
	connect(shortcut, &QShortcut::activated, this, &TextureGenerationDialog::generate);
	updateGameControls();
	refreshSourceStatus();
}

TextureGenerationDialog::~TextureGenerationDialog()
{
	m_work->discard = true;
	if (m_work->thread) {
		m_work->thread->disconnect(this);
		m_work->thread->wait();
	}
}

void TextureGenerationDialog::setPrompt(const QString& prompt)
{
	m_prompt->setPlainText(prompt);
}

bool TextureGenerationDialog::busy() const
{
	return m_busy;
}

const QVector<GeneratedTexture>& TextureGenerationDialog::textures() const
{
	return m_textures;
}

void TextureGenerationDialog::updateGameControls()
{
	TextureGameProfile profile;
	textureGameProfileForId(m_game->currentData().toString(), &profile);
	const bool quake = profile.id == QStringLiteral("quake");
	const bool folders = profile.id == QStringLiteral("quake2") || profile.id == QStringLiteral("quake3");
	const bool wad = quake || profile.id == QStringLiteral("doom") || profile.id == QStringLiteral("heretic") || profile.id == QStringLiteral("hexen");
	m_fullbrights->setEnabled(quake);
	m_directory->setEnabled(folders);
	m_wad->setEnabled(wad);
	m_dither->setEnabled(!profile.paletteId.isEmpty());
	m_companions->setEnabled(!profile.normalSuffix.isEmpty());
}

void TextureGenerationDialog::refreshSourceStatus()
{
	const AiImageConnection connection = resolveAiImageConnection(StudioSettings().aiAutomationPreferences());
	m_sourceStatus->setText(aiImageConnectionBlockText(connection));
	const bool model = m_modelSource->isChecked();
	m_restyle->setEnabled(model && m_hooks.currentTexture);
	m_previewRequest->setEnabled(model && !m_busy);
	m_picture->setEnabled(!model);
	refreshMissingTextures();
}

void TextureGenerationDialog::refreshMissingTextures()
{
	const QStringList names = m_hooks.missingTextures ? m_hooks.missingTextures() : QStringList();
	const QSignalBlocker blocker(m_missing);
	m_missing->clear();
	m_missing->addItem(names.isEmpty() ? tr("No missing textures to make") : tr("Choose one of %n", nullptr, int(names.size())), QString());
	for (const QString& name : names) {
		m_missing->addItem(name, name);
	}
	m_missing->setEnabled(!names.isEmpty());
}

void TextureGenerationDialog::useMissingTexture(const QString& name)
{
	// Quake II and III name a folder too; Quake marks liquids with *.
	QString base = name.trimmed();
	const qsizetype slash = base.lastIndexOf(QLatin1Char('/'));
	if (slash > 0) {
		m_directory->setText(base.left(slash));
		base = base.mid(slash + 1);
	}
	const QString lower = base.toLower();
	QString surface = QStringLiteral("wall");
	if (lower.startsWith(QLatin1Char('*')) || lower.contains(QStringLiteral("water")) || lower.contains(QStringLiteral("lava")) || lower.contains(QStringLiteral("slime"))) {
		surface = QStringLiteral("liquid");
	} else if (lower.startsWith(QStringLiteral("sky"))) {
		surface = QStringLiteral("sky");
	} else if (lower.contains(QStringLiteral("floor")) || lower.contains(QStringLiteral("flr")) || lower.contains(QStringLiteral("ground"))) {
		surface = QStringLiteral("floor");
	} else if (lower.contains(QStringLiteral("ceil")) || lower.contains(QStringLiteral("roof"))) {
		surface = QStringLiteral("ceiling");
	} else if (lower.contains(QStringLiteral("trim")) || lower.contains(QStringLiteral("border"))) {
		surface = QStringLiteral("trim");
	}
	m_surface->setCurrentIndex(std::max(0, m_surface->findData(surface)));
	m_name->setText(base);
	m_variants->setValue(1);
	if (m_prompt->toPlainText().trimmed().isEmpty()) {
		// The name's words as a start: metal5_2 reads as "metal".
		static const QRegularExpression notLetters(QStringLiteral("[^A-Za-z]+"));
		const QString words = base.split(notLetters, Qt::SkipEmptyParts).join(QLatin1Char(' '));
		if (!words.isEmpty()) {
			m_prompt->setPlainText(words);
		}
	}
	m_status->setText(tr("Making %1 under the name the map uses. Describe it, then Generate.").arg(name));
}

TextureGenerationSpec TextureGenerationDialog::specFromControls() const
{
	TextureGenerationSpec spec;
	spec.prompt = m_prompt->toPlainText().trimmed();
	spec.game = m_game->currentData().toString();
	spec.surface = m_surface->currentData().toString();
	spec.style = m_style->text().trimmed();
	spec.size = m_size->currentData().toSize();
	spec.name = m_name->text().trimmed();
	spec.directory = m_directory->text().trimmed();
	spec.seamless = m_seamless->isChecked();
	spec.companions = m_companions->isEnabled() && m_companions->isChecked();
	spec.dither = m_dither->isEnabled() && m_dither->isChecked();
	spec.fullbrights = m_fullbrights->isEnabled() && m_fullbrights->isChecked();
	return spec;
}

AiImageRequest TextureGenerationDialog::imageRequest(const TextureGenerationSpec& spec, const AiImageConnection& connection) const
{
	AiImageRequest request;
	request.connectorId = connection.connectorId;
	request.model = connection.model;
	request.endpoint = connection.endpoint;
	request.prompt = textureGenerationPrompt(spec);
	request.negativePrompt = textureGenerationNegativePrompt(spec);
	request.size = textureGenerationRequestSize(spec);
	request.count = m_variants->value();
	request.tileable = spec.seamless;
	if (m_restyle->isEnabled() && m_restyle->isChecked() && m_hooks.currentTexture) {
		const QImage current = m_hooks.currentTexture();
		if (!current.isNull()) {
			request.sourceImage = pngBytes(current.convertToFormat(QImage::Format_ARGB32));
		}
	}
	return request;
}

void TextureGenerationDialog::generate()
{
	if (m_busy) {
		return;
	}
	const TextureGenerationSpec spec = specFromControls();
	if (m_pictureSource->isChecked()) {
		const QString path = QDir::fromNativeSeparators(m_picture->text().trimmed());
		const QImage picture(path);
		if (picture.isNull()) {
			m_status->setText(path.isEmpty() ? tr("Choose a picture to start from.") : tr("Could not read the picture %1.").arg(QDir::toNativeSeparators(path)));
			return;
		}
		if (m_hooks.beginTask) {
			m_task = m_hooks.beginTask(tr("Generate Texture"), tr("Making a texture from %1.").arg(QFileInfo(path).fileName()));
		}
		QJsonObject provenance {{QStringLiteral("provider"), QStringLiteral("none")}, {QStringLiteral("source"), QFileInfo(path).fileName()}};
		process(spec, {picture}, provenance, {});
		return;
	}
	if (spec.prompt.isEmpty()) {
		m_status->setText(tr("Describe the texture for the image model to draw."));
		m_prompt->setFocus();
		return;
	}
	const AiImageConnection connection = resolveAiImageConnection(StudioSettings().aiAutomationPreferences());
	refreshSourceStatus();
	if (!connection.ready()) {
		m_status->setText(aiImageConnectionBlockText(connection));
		return;
	}
	const AiImageRequest request = imageRequest(spec, connection);
	if (!connection.local && m_hooks.confirmSend) {
		AiHttpRequest http;
		QString error;
		if (!buildAiImageHttpRequest(request, QString(), &http, &error)) {
			m_status->setText(error);
			return;
		}
		if (!m_hooks.confirmSend(connection.connectorId, connection.displayName, connection.endpoint, http)) {
			m_status->setText(tr("Nothing was sent."));
			return;
		}
	}
	if (m_hooks.beginTask) {
		m_task = m_hooks.beginTask(tr("Generate Texture"), tr("Asking %1 (%2) for %n picture(s).", nullptr, request.count).arg(connection.displayName, connection.model));
	}
	setBusy(true, tr("Asking %1 to draw %n picture(s)…", nullptr, request.count).arg(connection.displayName));
	m_progress->setRange(0, request.count);
	m_progress->setValue(0);
	QString error;
	const bool started = m_client->send(request, aiImageConnectionApiKey(connection), [this, spec, connection](const AiImageResponse& response) {
		QVector<QImage> pictures;
		QJsonArray revised;
		for (const AiGeneratedImage& image : response.images) {
			QImage picture;
			picture.loadFromData(image.bytes);
			if (!picture.isNull()) {
				pictures << picture;
			}
			if (!image.revisedPrompt.isEmpty()) {
				revised.append(image.revisedPrompt);
			}
		}
		if (pictures.isEmpty()) {
			const bool cancelled = response.failure == AiChatFailure::Cancelled;
			const QString message = cancelled ? tr("Cancelled. Nothing was drawn.") : tr("The image model drew nothing usable: %1").arg(response.errorMessage);
			setBusy(false, message);
			finishTask(false, cancelled, message);
			return;
		}
		QJsonObject provenance {
			{QStringLiteral("provider"), QJsonObject {
				{QStringLiteral("connector"), connection.connectorId},
				{QStringLiteral("model"), response.model.isEmpty() ? connection.model : response.model},
				{QStringLiteral("host"), QUrl(connection.endpoint).host()},
				{QStringLiteral("inputTokens"), response.inputTokens},
				{QStringLiteral("outputTokens"), response.outputTokens},
				{QStringLiteral("elapsedMs"), double(response.elapsedMsecs)},
			}},
			{QStringLiteral("prompt"), textureGenerationPrompt(spec)},
			{QStringLiteral("negativePrompt"), textureGenerationNegativePrompt(spec)},
			{QStringLiteral("revisedPrompts"), revised},
		};
		QStringList notes;
		if (!response.ok) {
			notes << tr("Only %n picture(s) came back: %1", nullptr, int(pictures.size())).arg(response.errorMessage);
		}
		process(spec, pictures, provenance, notes);
	}, &error, [this](int done, int total) {
		m_progress->setRange(0, total);
		m_progress->setValue(done);
	});
	if (!started) {
		setBusy(false, error);
		finishTask(false, false, error);
	}
}

void TextureGenerationDialog::process(const TextureGenerationSpec& spec, const QVector<QImage>& pictures, const QJsonObject& provenance, const QStringList& notes)
{
	setBusy(true, tr("Making %n picture(s) game-ready…", nullptr, int(pictures.size())));
	m_progress->setRange(0, 0);
	m_provenance = provenance;
	const auto work = m_work;
	work->discard = false;
	TextureGameProfile profile;
	textureGameProfileForId(spec.game, &profile);
	const TexturePreviewSource source = m_hooks.paletteSource && !profile.paletteId.isEmpty() ? m_hooks.paletteSource() : TexturePreviewSource();
	QThread* thread = QThread::create([work, spec, pictures, source, profile] {
		work->textures.clear();
		work->specs.clear();
		work->palette = profile.paletteId.isEmpty() ? IdTechPaletteResolution() : resolveTexturePreviewPalette(source, profile.paletteId);
		for (int index = 0; index < pictures.size() && !work->discard; ++index) {
			const TextureGenerationSpec variant = textureGenerationVariantSpec(spec, index, int(pictures.size()));
			work->specs << variant;
			work->textures << processGeneratedTexture(pictures[index], variant, work->palette);
		}
	});
	work->thread = thread;
	connect(thread, &QThread::finished, this, [this, work, thread, notes] {
		thread->deleteLater();
		work->thread = nullptr;
		if (work->discard) {
			setBusy(false, tr("Cancelled."));
			finishTask(false, true, tr("Cancelled."));
			return;
		}
		m_textures = work->textures;
		m_specs = work->specs;
		showTextures(notes);
	});
	thread->start();
}

void TextureGenerationDialog::showTextures(const QStringList& notes)
{
	m_variantList->clear();
	QStringList failures;
	for (int index = 0; index < m_textures.size(); ++index) {
		const GeneratedTexture& texture = m_textures[index];
		if (!texture.ok) {
			failures << texture.error;
			continue;
		}
		// The same picture when selected: the style would otherwise tint it
		// with the highlight colour, and the colours are what is being judged.
		QIcon icon;
		const QPixmap tiled = tiledPreview(texture.preview, 192);
		icon.addPixmap(tiled, QIcon::Normal);
		icon.addPixmap(tiled, QIcon::Selected);
		auto* item = new QListWidgetItem(icon, tr("%1 (%2 × %3)").arg(texture.mapName).arg(texture.image.width()).arg(texture.image.height()));
		item->setData(Qt::UserRole, index);
		item->setToolTip(tr("Seam %1 (was %2): about 1 reads as seamless.").arg(texture.seamScoreAfter, 0, 'f', 2).arg(texture.seamScoreBefore, 0, 'f', 2));
		item->setData(Qt::AccessibleDescriptionRole, item->toolTip());
		m_variantList->addItem(item);
	}
	const bool any = m_variantList->count() > 0;
	if (any) {
		m_variantList->setCurrentRow(0);
	}
	QStringList status;
	status << (any ? tr("%n variant(s) ready. Pick one, then save it.", nullptr, m_variantList->count()) : tr("No variant could be made game-ready."));
	status += failures;
	status += notes;
	setBusy(false, status.join(QLatin1Char('\n')));
	finishTask(any, false, status.value(0));
}

void TextureGenerationDialog::showSelected()
{
	const int row = m_variantList->currentRow();
	const QListWidgetItem* item = row >= 0 ? m_variantList->item(row) : nullptr;
	const int index = item ? item->data(Qt::UserRole).toInt() : -1;
	const bool valid = index >= 0 && index < m_textures.size() && m_textures[index].ok;
	m_save->setEnabled(valid && !m_busy);
	m_saveApply->setEnabled(valid && !m_busy && m_hooks.applyToMap);
	m_openEditor->setEnabled(valid && !m_busy && m_hooks.openInEditor);
	if (!valid) {
		m_details->clear();
		m_companionStrip->clear();
		return;
	}
	const GeneratedTexture& texture = m_textures[index];
	QStringList lines;
	lines << tr("Map name: %1").arg(texture.mapName);
	lines << tr("Format: %1, %n byte(s)", nullptr, int(texture.encoded.bytes.size())).arg(textureExportFormatId(texture.exportOptions.format));
	lines << tr("Seam: %1 before, %2 after (about 1 reads as seamless)").arg(texture.seamScoreBefore, 0, 'f', 2).arg(texture.seamScoreAfter, 0, 'f', 2);
	for (const QString& step : texture.steps) {
		lines << QStringLiteral("- %1").arg(step);
	}
	for (const QString& warning : texture.warnings) {
		lines << tr("Warning: %1").arg(warning);
	}
	m_details->setPlainText(lines.join(QLatin1Char('\n')));
	if (texture.companions.isEmpty()) {
		m_companionStrip->setText(tr("No companion maps."));
		m_companionStrip->setAccessibleDescription(m_companionStrip->text());
		return;
	}
	// The companions side by side, each named under it.
	const int side = 96;
	QImage strip(int(texture.companions.size()) * (side + 8), side + 18, QImage::Format_ARGB32_Premultiplied);
	strip.fill(Qt::transparent);
	QPainter painter(&strip);
	QStringList names;
	for (int companion = 0; companion < texture.companions.size(); ++companion) {
		const GeneratedTextureMap& map = texture.companions[companion];
		QImage shown = map.image;
		if (map.kind == QStringLiteral("normal")) {
			shown = shown.convertToFormat(QImage::Format_RGB32);
		}
		painter.drawImage(QRect(companion * (side + 8), 0, side, side), shown);
		painter.setPen(palette().color(QPalette::WindowText));
		painter.drawText(QRect(companion * (side + 8), side + 2, side, 16), Qt::AlignCenter, map.suffix);
		names << QStringLiteral("%1 (%2)").arg(map.kind, map.suffix);
	}
	painter.end();
	m_companionStrip->setPixmap(QPixmap::fromImage(strip));
	m_companionStrip->setAccessibleDescription(tr("Companion maps: %1").arg(names.join(QStringLiteral(", "))));
}

bool TextureGenerationDialog::saveSelected(bool apply)
{
	const int row = m_variantList->currentRow();
	const QListWidgetItem* item = row >= 0 ? m_variantList->item(row) : nullptr;
	const int index = item ? item->data(Qt::UserRole).toInt() : -1;
	if (index < 0 || index >= m_textures.size() || !m_textures[index].ok) {
		return false;
	}
	TextureGenerationOutput output;
	output.folder = m_hooks.outputFolder ? m_hooks.outputFolder() : QDir::currentPath();
	const QString wad = QDir::fromNativeSeparators(m_wad->text().trimmed());
	output.wadPath = wad.isEmpty() || QDir::isAbsolutePath(wad) ? wad : QDir(output.folder).filePath(wad);
	output.provenance = m_provenance;
	TextureGenerationWriteReport report = writeGeneratedTexture(m_textures[index], m_specs[index], output);
	if (!report.ok && report.alreadyExists) {
		const auto answer = QMessageBox::question(this, tr("Replace the Texture?"),
			tr("%1\n\nReplace what is there with this variant?").arg(report.error), QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
		if (answer != QMessageBox::Yes) {
			m_status->setText(tr("Nothing was written."));
			return false;
		}
		output.replaceExisting = true;
		report = writeGeneratedTexture(m_textures[index], m_specs[index], output);
	}
	if (!report.ok) {
		m_status->setText(report.error);
		return false;
	}
	QStringList status;
	status << tr("Saved %1 to %n file(s).", nullptr, int(report.writtenPaths.size())).arg(report.mapTextureName);
	status += report.notes;
	if (m_hooks.written) {
		m_hooks.written(report.writtenPaths);
	}
	if (apply && m_hooks.applyToMap) {
		QString error;
		status << (m_hooks.applyToMap(report.mapTextureName, &error) ? tr("Applied %1 to the selected faces.").arg(report.mapTextureName) : error);
	}
	m_status->setText(status.join(QLatin1Char('\n')));
	return true;
}

void TextureGenerationDialog::setBusy(bool busy, const QString& status)
{
	m_busy = busy;
	m_status->setText(status);
	m_progress->setVisible(busy);
	m_cancel->setEnabled(busy);
	m_generate->setEnabled(!busy);
	m_previewRequest->setEnabled(!busy && m_modelSource->isChecked());
	showSelected();
}

void TextureGenerationDialog::finishTask(bool succeeded, bool cancelled, const QString& summary)
{
	if (!m_task.isEmpty() && m_hooks.endTask) {
		m_hooks.endTask(m_task, succeeded, cancelled, summary);
	}
	m_task.clear();
}

void TextureGenerationDialog::cancelGeneration()
{
	if (!m_busy) {
		return;
	}
	if (m_client->busy()) {
		m_client->cancel();
		return;
	}
	m_work->discard = true;
	m_status->setText(tr("Cancelling…"));
}

void TextureGenerationDialog::reject()
{
	if (m_busy) {
		cancelGeneration();
		return;
	}
	QDialog::reject();
}

void TextureGenerationDialog::showRequestPreview()
{
	const TextureGenerationSpec spec = specFromControls();
	const AiImageConnection connection = resolveAiImageConnection(StudioSettings().aiAutomationPreferences());
	AiHttpRequest http;
	QString error;
	const bool built = buildAiImageHttpRequest(imageRequest(spec, connection), QString(), &http, &error);
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("textureGenerationRequestPreview"));
	dialog.setWindowTitle(tr("Image Request"));
	dialog.setAccessibleName(dialog.windowTitle());
	dialog.resize(760, 520);
	auto* layout = new QVBoxLayout(&dialog);
	auto* explanation = new QLabel(built ? tr("Nothing has been sent. This is the request Generate would send, without your API key. %1").arg(aiImageConnectionBlockText(connection))
										 : error);
	explanation->setWordWrap(true);
	layout->addWidget(explanation);
	auto* text = new QPlainTextEdit;
	text->setObjectName(QStringLiteral("textureGenerationRequestText"));
	text->setAccessibleName(tr("The image request"));
	text->setReadOnly(true);
	text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	text->setPlainText(built ? describeAiHttpRequest(http) : QString());
	layout->addWidget(text, 1);
	auto* raw = new QCheckBox(tr("Show the raw request"));
	raw->setEnabled(built);
	connect(raw, &QCheckBox::toggled, text, [text, http](bool shown) { text->setPlainText(describeAiHttpRequest(http, shown ? AiRequestView::Raw : AiRequestView::Readable)); });
	layout->addWidget(raw);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	dialog.exec();
}

} // namespace vibestudio
