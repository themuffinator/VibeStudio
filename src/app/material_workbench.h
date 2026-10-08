#pragma once

// The Materials page: every texture, shader and material of idTech 1-4 in
// the open package or script, a live preview drawn the way the game draws
// it, and two editors over one text: the script itself and a node graph.
//
// The text is the single source: a graph or property edit becomes a text
// edit (core/material_graph.h) applied to the editor as one undo step, and
// every text change is parsed again to update the preview, the graph, the
// images and the problems. Saving writes a loose script, or stages the
// script, the Doom lumps or the Quake II WAL into the open package.

#include "core/material_classic.h"
#include "core/material_graph.h"
#include "core/material_images.h"
#include "core/material_library.h"
#include "core/material_render.h"
#include "core/package_archive.h"

#include <QHash>
#include <QPointer>
#include <QWidget>

#include <functional>
#include <memory>

class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QMenu;
class QSlider;
class QSplitter;
class QTabWidget;
class QTextEdit;
class QTimer;
class QToolButton;
class QTreeWidget;

namespace vibestudio {

class LoadingPane;
class MaterialGraphView;
class MaterialLibraryFilter;
class MaterialLibraryModel;
class MaterialPreviewView;
class MaterialPropertyPanel;
class MaterialScriptHighlighter;
class MaterialTaskLane;
class StudioCodeEditor;
struct MaterialLibraryRows;

// What the workbench asks of the studio around it. A missing hook turns the
// matching action off.
struct MaterialWorkbenchHost {
	// Stages a file into the open package, replacing one at the path.
	std::function<bool(const QByteArray& bytes, const QString& virtualPath, QString* error)> stageFile;
	// Stages a lump into the open WAD, replacing one with the name.
	std::function<bool(const QByteArray& bytes, const QString& lumpName, QString* error)> stageLump;
	std::function<void(const QString& reference)> showTexture;
	std::function<void(const QString& filePath, int line)> openInCode;
};

class MaterialWorkbench final : public QWidget {
	Q_OBJECT

public:
	explicit MaterialWorkbench(QWidget* parent = nullptr);
	~MaterialWorkbench() override;

	void setHost(MaterialWorkbenchHost host);
	// The open package as an immutable snapshot (staged edits included), its
	// path, and a revision that changes when its contents do.
	void setPackage(std::shared_ptr<const PackageArchiveReader> reader, const QString& path, const QString& revision);
	void clearPackage();
	// The revision setPackage() last took; the shell skips copying an unchanged view.
	[[nodiscard]] QString packageRevision() const { return m_revision; }
	// A .shader or .mtr file, read with the open package's images.
	bool openScript(const QString& path, QString* error = nullptr);
	void setReducedMotion(bool reduced);

	[[nodiscard]] bool hasContent() const;
	// One line for the page header.
	[[nodiscard]] QString summary() const;
	// Scanning, reading images or drawing.
	[[nodiscard]] bool isBusy() const;
	// What keeps it busy: "scanning", "images", "drawing" or "parsing".
	[[nodiscard]] QString busyReason() const;
	[[nodiscard]] bool hasUnsavedChanges() const;
	bool save(QString* error = nullptr);
	bool saveAs(const QString& path, QString* error = nullptr);
	void revert();
	bool selectMaterial(const QString& name);
	[[nodiscard]] QString currentMaterial() const { return m_materialName; }
	[[nodiscard]] const MaterialDefinition& currentDefinition() const { return m_definition; }
	[[nodiscard]] MaterialTextKind currentTextKind() const;
	[[nodiscard]] QString currentText() const;
	[[nodiscard]] QString currentDocumentTitle() const { return m_document.title; }
	bool applyGraphEdit(const MaterialGraphEdit& edit, QString* error = nullptr);
	// Adds a new material from a template to the current script (or starts
	// a new one) and selects it.
	bool addMaterialFromTemplate(const QString& templateId, const QString& name, const QString& image, QString* error = nullptr);
	// Asks for a template, a name and an image, then adds the material.
	void showNewMaterialDialog();
	// Filters the library to the materials that read `image` (a package
	// path or a lump name) and selects the first.
	void showMaterialsUsingImage(const QString& image);
	// Selects the material a map names (Quake III maps leave out textures/),
	// now or once the library is read.
	void revealMaterial(const QString& name);

	// Parts, for the shell's focus handling and for tests.
	[[nodiscard]] MaterialPreviewView* preview() const { return m_preview; }
	[[nodiscard]] MaterialGraphView* graphView() const { return m_graph; }
	[[nodiscard]] MaterialPropertyPanel* propertyPanel() const { return m_properties; }
	[[nodiscard]] StudioCodeEditor* editor() const { return m_editor; }
	[[nodiscard]] QListView* libraryView() const { return m_libraryView; }
	[[nodiscard]] QLineEdit* filterField() const { return m_filter; }
	[[nodiscard]] MaterialLibraryModel* libraryModel() const { return m_model; }
	[[nodiscard]] QTabWidget* editorTabs() const { return m_tabs; }
	[[nodiscard]] QTreeWidget* problemsList() const { return m_problems; }
	[[nodiscard]] QTreeWidget* imagesList() const { return m_imageList; }

Q_SIGNALS:
	// hasContent() or summary() changed.
	void contentChanged();
	void statusMessage(const QString& message);
	void unsavedChangesChanged(bool unsaved);

protected:
	void changeEvent(QEvent* event) override;
	void showEvent(QShowEvent* event) override;

private:
	// The text being edited and where saving puts it.
	struct Document {
		QString key;
		MaterialTextKind kind = MaterialTextKind::None;
		MaterialEngine engine = MaterialEngine::Unknown;
		QString title;
		// A loose file written in place, or a package entry or lump staged.
		QString filePath;
		QString virtualPath;
		// Quake II: the WAL the header text belongs to.
		QByteArray walBytes;
		QString savedText;
	};

	void buildUi();
	QWidget* buildLibraryPanel();
	QWidget* buildPreviewPanel();
	QWidget* buildEditorTabs();
	void retranslate();
	void refreshPlayButton();
	void refreshViewModeButton();
	void refreshTabTexts();
	void applyTheme();

	void requestScan();
	void startScan();
	void applyLibrary(std::shared_ptr<MaterialLibrary> library, const MaterialLibraryRows& rows, int looseScript, const QString& select);
	void updateLibraryCount();
	void updateVisibleRows();
	[[nodiscard]] MaterialImageSource imageSource() const;
	[[nodiscard]] QStringList packageImages() const;

	void selectEntry(int entry);
	[[nodiscard]] Document documentFor(int entry, const MaterialDefinition& definition) const;
	void switchDocument(const Document& document);
	void setEditorText(const QString& text);
	void replaceEditorText(const QString& text);
	void scheduleReparse();
	void reparse(bool scrollToDefinition);
	void updateViews(bool materialChanged, bool scrollToDefinition);
	void resolveImages(bool materialChanged);
	void pushPreview(bool materialChanged);
	void updateGraph();
	void updateProblems(const QVector<MaterialDiagnostic>& diagnostics);
	void updateImagesTab();
	void updateDetails();
	void updateNotes();
	void updateTimeline();
	void rebuildLightingMenu();
	void applyRenderOptions();
	void showLine(int line);
	void refreshDocumentState();
	void chooseSaveAs();
	bool writeLooseFile(const QString& path, const QByteArray& bytes, QString* error);
	[[nodiscard]] std::shared_ptr<const MaterialTableSet> tablesFor(const MaterialScript& script) const;
	[[nodiscard]] double cycleSeconds() const;
	// Reads a package file by path, ignoring case.
	bool readPackageFile(const QString& path, QByteArray* bytes, QString* error = nullptr) const;
	[[nodiscard]] bool packageIsWad() const;
	[[nodiscard]] QString freeScriptPath(MaterialEngine engine) const;
	void rebuildEngineFilter();
	[[nodiscard]] QSize libraryGridSize() const;
	void rebuildAddNodeMenu(const QVector<MaterialGraphNodeTemplate>& templates);

	MaterialWorkbenchHost m_host;
	std::shared_ptr<const PackageArchiveReader> m_reader;
	QString m_packagePath;
	QString m_revision;
	QString m_looseFile;
	std::shared_ptr<MaterialLibrary> m_library;
	std::shared_ptr<MaterialImageCache> m_cache;
	int m_looseScript = -1;
	bool m_scanning = false;
	bool m_scanPending = false;
	bool m_reducedMotion = false;

	Document m_document;
	// Unsaved text of documents switched away from, by document key.
	QHash<QString, QString> m_buffers;
	QString m_materialName;
	// A material asked for before the library was read.
	QString m_requestedName;
	MaterialEngine m_engine = MaterialEngine::Unknown;
	// The library's definition, for materials with no text (Quake textures,
	// implicit shaders) and as the base classic edits change.
	MaterialDefinition m_baseDefinition;
	MaterialDefinition m_definition;
	QString m_definitionProblem;
	std::shared_ptr<const MaterialImageSet> m_images;
	QStringList m_imageReferences;
	std::shared_ptr<const MaterialTableSet> m_tables;
	bool m_shapeChosen = false;
	bool m_applyingText = false;
	bool m_lastModified = false;
	// The next parse shows a different material, not a new version of it.
	bool m_materialChangePending = false;
	// The text the definition was last parsed from.
	QString m_parsedText;
	bool m_developerDefault = false;
	int m_problemCount = 0;
	int m_imageCount = 0;
	// Lower-case package paths to their real spelling, for the reader below.
	mutable QHash<QString, QString> m_pathIndex;
	mutable const PackageArchiveReader* m_pathIndexReader = nullptr;

	MaterialTaskLane* m_scanLane = nullptr;
	MaterialTaskLane* m_imageLane = nullptr;
	QTimer* m_reparseTimer = nullptr;
	QTimer* m_visibleTimer = nullptr;

	LoadingPane* m_state = nullptr;
	QLineEdit* m_filter = nullptr;
	QComboBox* m_engineFilter = nullptr;
	QToolButton* m_viewMode = nullptr;
	QToolButton* m_animateSwatches = nullptr;
	QToolButton* m_newMaterial = nullptr;
	QToolButton* m_save = nullptr;
	QToolButton* m_revert = nullptr;
	QLabel* m_libraryCount = nullptr;
	QListView* m_libraryView = nullptr;
	MaterialLibraryModel* m_model = nullptr;
	MaterialLibraryFilter* m_proxy = nullptr;

	QLabel* m_materialTitle = nullptr;
	MaterialPreviewView* m_preview = nullptr;
	QToolButton* m_play = nullptr;
	QToolButton* m_restart = nullptr;
	QComboBox* m_speed = nullptr;
	QSlider* m_timeline = nullptr;
	QLabel* m_timeLabel = nullptr;
	QComboBox* m_shape = nullptr;
	QToolButton* m_lighting = nullptr;
	QMenu* m_lightingMenu = nullptr;
	QLabel* m_notes = nullptr;

	QTabWidget* m_tabs = nullptr;
	MaterialGraphView* m_graph = nullptr;
	MaterialPropertyPanel* m_properties = nullptr;
	QToolButton* m_addNode = nullptr;
	QMenu* m_addNodeMenu = nullptr;
	QLabel* m_documentLabel = nullptr;
	StudioCodeEditor* m_editor = nullptr;
	MaterialScriptHighlighter* m_highlighter = nullptr;
	QTreeWidget* m_imageList = nullptr;
	QTreeWidget* m_problems = nullptr;
	QTextEdit* m_details = nullptr;
	QSplitter* m_mainSplitter = nullptr;
	QSplitter* m_editorSplitter = nullptr;
};

} // namespace vibestudio
