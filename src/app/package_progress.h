#pragma once

#include <QLocale>
#include <QString>

namespace vibestudio {

// Keep numeric fractions in logical order inside translated RTL status text.
QString packageProgressPair(const QString& completed, const QString& total);

// A zero total is unknown. Partial work never reaches the determinate maximum,
// including when adjacent 64-bit byte counts round to the same double.
int packageProgressValue(quint64 completed, quint64 total);

struct PackageByteProgressText {
	QString compact;
	QString exact;
};

// Both amounts use the same binary unit. Partial work rounds down, totals round
// up to a hundredth, and completion uses the same rounding as the total. Exact
// localized byte counts belong in tooltips and accessible descriptions.
PackageByteProgressText packageByteProgressText(quint64 completed, quint64 total, const QLocale& locale = {});

} // namespace vibestudio
