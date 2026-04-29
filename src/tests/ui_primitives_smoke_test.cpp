#include "app/ui_primitives.h"

#include <QCoreApplication>
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
	QCoreApplication app(argc, argv);
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

	return ok ? 0 : 1;
}
