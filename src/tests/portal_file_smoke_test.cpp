// Checks the portal file reader behind Build > Load Portal File: the PRT1,
// PRT1-AM and PRT2 headers in ericw-tools' orders, q3map's extra solid faces,
// and the files it refuses.

#include "core/portal_file.h"

#include <QCoreApplication>

#include <cmath>
#include <iostream>

using namespace vibestudio;

namespace {

bool expect(bool condition, const char* message, const QString& detail = QString())
{
	if (!condition) {
		std::cerr << "FAIL: " << message;
		if (!detail.isEmpty()) {
			std::cerr << " (" << detail.toStdString() << ")";
		}
		std::cerr << "\n";
	}
	return condition;
}

} // namespace

int main(int argc, char* argv[])
{
	QCoreApplication app(argc, argv);
	bool ok = true;

	// Quake's qbsp without detail: leaves and portals.
	const PortalFile quake = parsePortalFile(QByteArrayLiteral("PRT1\n3\n2\n"
															   "4 0 1 (0 0 0 ) (0 64 0 ) (0 64 64 ) (0 0 64 )\n"
															   "4 1 2 (64 0 0 ) (64 64 0 ) (64 64 64 ) (64 0 64 )\n"));
	ok &= expect(quake.valid && quake.format == QStringLiteral("PRT1") && quake.leafCount == 3 && quake.portals.size() == 2, "PRT1 reads two portals",
		quake.error);
	ok &= expect(quake.portals.size() == 2 && quake.portals.at(1).size() == 4 && std::abs(quake.portals.at(1).at(2).z - 64) < 1e-9,
		"each portal keeps its points in order");
	ok &= expect(quake.mins.valid && std::abs(quake.mins.x) < 1e-9 && std::abs(quake.maxs.x - 64) < 1e-9, "the portals' bounds are measured");

	// q3map: clusters, portals and solid faces, a hint flag on each portal,
	// and the faces after them.
	const PortalFile q3 = parsePortalFile(QByteArrayLiteral("PRT1\r\n2\r\n1\r\n1\r\n"
															"3 0 1 0 (0 0 0 ) (16 0 0 ) (16 16 0 )\r\n"
															"3 0 (0 0 8 ) (16 0 8 ) (16 16 8 )\r\n"));
	ok &= expect(q3.valid && q3.portals.size() == 1, "q3map's portal is read and its solid face is not", q3.error);

	// ericw-tools with detail: PRT2 is leaves, clusters, portals, then the
	// leaves' clusters; PRT1-AM is clusters, portals, leaves.
	const PortalFile prt2 = parsePortalFile(QByteArrayLiteral("PRT2\n4\n2\n1\n"
															  "3 0 1 (0 0 0 ) (8 0 0 ) (8 8 0 )\n"
															  "0\n0\n1\n1\n"));
	ok &= expect(prt2.valid && prt2.leafCount == 4 && prt2.clusterCount == 2 && prt2.portals.size() == 1, "PRT2 counts and portals", prt2.error);
	const PortalFile am = parsePortalFile(QByteArrayLiteral("PRT1-AM\n2\n1\n5\n"
															"3 0 1 (0 0 0 ) (8 0 0 ) (8 8 0 )\n"
															"0\n0\n1\n1\n1\n"));
	ok &= expect(am.valid && am.clusterCount == 2 && am.leafCount == 5 && am.portals.size() == 1, "PRT1-AM orders clusters, portals, leaves", am.error);

	// Refusals.
	ok &= expect(!parsePortalFile(QByteArray()).valid, "an empty file is refused");
	ok &= expect(!parsePortalFile(QByteArrayLiteral("PORTALS\n1\n1\n")).valid, "an unknown magic is refused");
	const PortalFile short_ = parsePortalFile(QByteArrayLiteral("PRT1\n2\n2\n3 0 1 (0 0 0 ) (8 0 0 ) (8 8 0 )\n"));
	ok &= expect(!short_.valid && short_.portals.isEmpty() && !short_.error.isEmpty(), "a file with fewer portals than promised is refused");
	ok &= expect(!parsePortalFile(QByteArrayLiteral("PRT1\n2\n1\n4 0 1 (0 0 0 ) (8 0 0 ) (8 8 0 )\n")).valid, "a portal missing a point is refused");
	ok &= expect(!loadPortalFile(QStringLiteral("does-not-exist.prt")).valid, "a missing file is refused");

	if (ok) {
		std::cout << "Portal file smoke test passed.\n";
	}
	return ok ? 0 : 1;
}
