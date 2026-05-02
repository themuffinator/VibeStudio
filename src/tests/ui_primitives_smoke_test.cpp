#include "app/ui_primitives.h"

#include <QApplication>
#include <QString>
#include <QVector>

#include <iostream>

using namespace vibestudio;

namespace {

bool expect(bool condition, const char* message)
{
	if (!condition) {
		std::cerr << message << "\n";
		return false;
	}
	return true;
}

bool hasPrimitive(const QVector<UiPrimitiveDescriptor>& primitives, const QString& id)
{
	for (const UiPrimitiveDescriptor& primitive : primitives) {
		if (primitive.id == id) {
			return true;
		}
	}
	return false;
}

} // namespace

int main(int argc, char** argv)
{
	QApplication app(argc, argv);
	bool ok = true;

	const QVector<UiPrimitiveDescriptor> primitives = uiPrimitiveDescriptors();
	ok &= expect(primitives.size() >= 2, "expected at least two UI primitive descriptors");
	ok &= expect(hasPrimitive(primitives, QStringLiteral("loading-pane")), "missing loading-pane descriptor");
	ok &= expect(hasPrimitive(primitives, QStringLiteral("detail-drawer")), "missing detail-drawer descriptor");

	for (const UiPrimitiveDescriptor& primitive : primitives) {
		ok &= expect(!primitive.id.trimmed().isEmpty(), "primitive id is empty");
		ok &= expect(!primitive.title.trimmed().isEmpty(), "primitive title is empty");
		ok &= expect(!primitive.description.trimmed().isEmpty(), "primitive description is empty");
		ok &= expect(!primitive.useCases.isEmpty(), "primitive use cases are empty");
	}

	LoadingPane loadingPane;
	loadingPane.setState(OperationState::Loading);
	loadingPane.setPlaceholderRows({QStringLiteral("First"), QStringLiteral("Second")});
	loadingPane.setPlaceholderRows({QStringLiteral("Project manifest"), QStringLiteral("Diagnostics")});
	loadingPane.setPlaceholderRows({});
	ok &= expect(loadingPane.placeholderRows().isEmpty(), "empty placeholder rows should use default rendered rows without storing them");

	DetailDrawer drawer;
	drawer.setSections({
		{
			QStringLiteral("summary"),
			QStringLiteral("Summary"),
			QStringLiteral("Smoke test summary"),
			QStringLiteral("Smoke test content"),
			OperationState::Completed,
		},
	});
	drawer.showSection(QStringLiteral("summary"));
	ok &= expect(drawer.currentSectionText() == QStringLiteral("Smoke test content"), "detail drawer should expose selected section content");

	return ok ? 0 : 1;
}
