#include "app/package_progress.h"

#include <QCoreApplication>

#include <algorithm>
#include <array>

namespace vibestudio {
namespace {
QString isolate(const QString& text)
{
	return QChar(0x2066) + text + QChar(0x2069);
}

QString quantity(quint64 bytes, quint64 divisor, bool roundUp, const QLocale& locale)
{
	if (divisor == 1) { return locale.toString(bytes); }
	const auto remainder = bytes % divisor;
	// Reduce 100/divisor to 25/(divisor/4), then split the remainder.
	// Even at EiB, the only product is below 25 * 2^58, so uint64 fits.
	const auto quarter = divisor / 4;
	const auto fraction = (remainder % quarter) * 25;
	const auto hundredths = (bytes / divisor) * 100 + (remainder / quarter) * 25
		+ fraction / quarter + (roundUp && fraction % quarter != 0 ? 1 : 0);
	if (bytes > 0 && hundredths == 0) { return QLatin1Char('<') + locale.toString(0.01, 'f', 2); }
	if (hundredths % 100 == 0) { return locale.toString(hundredths / 100); }
	// Unit selection bounds this integer to 102400; no 64-bit byte count passes
	// through floating point when producing the compact or exact quantities.
	return locale.toString(static_cast<double>(hundredths) / 100.0, 'f', hundredths % 10 == 0 ? 1 : 2);
}
} // namespace

QString packageProgressPair(const QString& completed, const QString& total)
{
	return isolate(completed + QStringLiteral(" / ") + total);
}

int packageProgressValue(quint64 completed, quint64 total)
{
	if (total == 0) { return 0; }
	if (completed >= total) { return 1000; }
	return std::min(999, static_cast<int>(1000.0 * completed / total));
}

PackageByteProgressText packageByteProgressText(quint64 completed, quint64 total, const QLocale& locale)
{
	// IEC binary unit symbols are stable technical identifiers.
	static constexpr std::array units {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
	const auto largest = std::max(completed, total);
	quint64 divisor = 1;
	size_t unit = 0;
	while (unit + 1 < units.size() && largest / divisor >= 1024) { divisor *= 1024; ++unit; }
	QString amounts = quantity(completed, divisor, total > 0 && completed >= total, locale);
	if (total > 0) { amounts += QStringLiteral(" / ") + quantity(total, divisor, true, locale); }
	PackageByteProgressText result;
	//: %1 is a localized amount or fraction; %2 is an IEC unit such as KiB.
	result.compact = isolate(QCoreApplication::translate("VibeStudioPackageProgress", "%1 %2").arg(amounts, QString::fromLatin1(units[unit])));
	result.exact = total > 0
		? QCoreApplication::translate("VibeStudioPackageProgress", "Bytes: %1").arg(packageProgressPair(locale.toString(completed), locale.toString(total)))
		: QCoreApplication::translate("VibeStudioPackageProgress", "Bytes: %1; total unknown").arg(isolate(locale.toString(completed)));
	return result;
}

} // namespace vibestudio
