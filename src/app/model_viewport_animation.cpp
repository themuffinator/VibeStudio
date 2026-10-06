#include "app/model_viewport.h"

#include "core/model_tags.h"

#include <QTimer>

#include <algorithm>
#include <cmath>

namespace vibestudio
{
void ModelViewport::resetPlaybackClock()
{
	m_animationStart = m_nativeMdl ? m_mdlPlayback.seconds : m_frame - m_rangeFirst + m_frameBlend;
	if (m_playing)
	{
		m_animationClock.start();
	}
	else
	{
		m_animationClock.invalidate();
	}
}

void ModelViewport::applyAnimationSample(const ModelAnimationSample &sample)
{
	if (sample.frame == m_frame && sample.nextFrame == m_blendFrame && sample.fraction == m_frameBlend)
	{
		return;
	}
	const bool frameChanged = sample.frame != m_frame;
	m_frame = sample.frame;
	m_blendFrame = sample.nextFrame;
	m_frameBlend = sample.fraction;
	invalidateProjection();
	if (frameChanged)
	{
		// Avoid rebuilding editor tables or announcing 60 accessibility changes
		// per second solely because the transient fraction advanced.
		setAccessibleDescription(accessibleSummary());
		Q_EMIT this->frameChanged(m_frame);
	}
	update();
}

void ModelViewport::setFrame(int frame)
{
	if (m_frameTotal <= 0)
	{
		return;
	}
	finishEditTransform(false);
	clearMdlPlayback();
	const int target = std::clamp(frame, 0, m_frameTotal - 1);
	if (target < m_rangeFirst || target >= m_rangeFirst + m_rangeCount)
	{
		m_animation.clear();
		m_animationIndex = -1;
		m_rangeFirst = 0;
		m_rangeCount = m_frameTotal;
		Q_EMIT animationChanged(m_animation);
	}
	invalidateRaster(true);
	applyAnimationSample({target, m_rangeFirst + (target - m_rangeFirst + 1) % m_rangeCount, 0});
	resetPlaybackClock();
}

int ModelViewport::frame() const { return m_frame; }
int ModelViewport::frameCount() const { return m_frameTotal; }

void ModelViewport::stepFrame(int delta)
{
	if (m_frameTotal <= 0 || m_rangeCount <= 0)
	{
		return;
	}
	finishEditTransform(false);
	clearMdlPlayback();
	const int offset = int(((qint64(m_frame) - m_rangeFirst + delta) % m_rangeCount + m_rangeCount) % m_rangeCount);
	invalidateRaster(true);
	applyAnimationSample({m_rangeFirst + offset, m_rangeFirst + (offset + 1) % m_rangeCount, 0});
	resetPlaybackClock();
}

bool ModelViewport::setAnimation(const QString &name)
{
	if (name.isEmpty())
	{
		return setAnimationIndex(-1);
	}
	for (int index = 0; index < m_mesh.animations.size(); ++index)
	{
		if (m_mesh.animations[index].name == name)
		{
			return setAnimationIndex(index);
		}
	}
	return false;
}

bool ModelViewport::setAnimationIndex(int index)
{
	if (index < -1 || index >= m_mesh.animations.size())
	{
		return false;
	}
	finishEditTransform(false);
	clearMdlPlayback();
	m_animationIndex = index;
	m_frameBlend = 0;
	if (index < 0)
	{
		m_animation.clear();
		m_rangeFirst = 0;
		m_rangeCount = m_frameTotal;
	}
	else
	{
		const auto &clip = m_mesh.animations[index];
		m_animation = clip.name;
		m_rangeFirst = std::clamp(clip.firstFrame, 0, std::max(0, m_frameTotal - 1));
		m_rangeCount = std::clamp(clip.frameCount, 1, std::max(1, m_frameTotal - m_rangeFirst));
		m_frame = m_rangeFirst;
		if (clip.framesPerSecond > 0)
		{
			m_fps = clip.framesPerSecond;
		}
	}
	m_blendFrame = m_rangeCount > 0 ? m_rangeFirst + (m_frame - m_rangeFirst + 1) % m_rangeCount : m_frame;
	const bool wasPlaying = m_playing;
	if (m_rangeCount <= 1)
	{
		m_playing = false;
	}
	resetPlaybackClock();
	updatePlaybackTimer();
	invalidateRaster(true);
	invalidateProjection();
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT animationChanged(m_animation);
	Q_EMIT frameChanged(m_frame);
	if (wasPlaying != m_playing)
	{
		Q_EMIT playbackChanged(m_playing);
	}
	return true;
}

int ModelViewport::animationIndex() const { return m_animationIndex; }
QString ModelViewport::animation() const { return m_animation; }
QStringList ModelViewport::animationNames() const
{
	QStringList names;
	for (const auto &clip : m_mesh.animations)
	{
		names << clip.name;
	}
	return names;
}

void ModelViewport::play()
{
	if (m_playing || m_reducedMotion || !m_hasMesh || !canAnimate())
	{
		return;
	}
	finishEditTransform(false);
	finishSurfaceStroke(false);
	m_playing = true;
	resetPlaybackClock();
	updatePlaybackTimer();
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT playbackChanged(true);
	if (m_nativeMdl)
	{
		Q_EMIT mdlPlaybackChanged();
	}
}

void ModelViewport::pause()
{
	if (!m_playing)
	{
		return;
	}
	m_playing = false;
	m_frameBlend = 0;
	resetPlaybackClock();
	updatePlaybackTimer();
	// Retire in-flight fractional renders before exact-pose editing resumes.
	invalidateRaster(true);
	invalidateProjection();
	setAccessibleDescription(accessibleSummary());
	update();
	Q_EMIT playbackChanged(false);
	if (m_nativeMdl)
	{
		Q_EMIT mdlPlaybackChanged();
	}
}

void ModelViewport::togglePlayback()
{
	if (m_playing)
	{
		pause();
	}
	else
	{
		play();
	}
}
bool ModelViewport::isPlaying() const { return m_playing; }

void ModelViewport::setFramesPerSecond(double fps)
{
	if (!std::isfinite(fps))
	{
		return;
	}
	const double clamped = std::clamp(fps, .001, 1000.0);
	if (clamped == m_fps)
	{
		return;
	}
	m_fps = clamped;
	resetPlaybackClock();
	updatePlaybackTimer();
	setAccessibleDescription(accessibleSummary());
	update();
}
double ModelViewport::framesPerSecond() const { return m_fps; }

void ModelViewport::setAnimationInterpolation(bool enabled)
{
	if (enabled == m_interpolateAnimation)
	{
		return;
	}
	m_interpolateAnimation = enabled;
	m_frameBlend = 0;
	resetPlaybackClock();
	updatePlaybackTimer();
	invalidateRaster(true);
	invalidateProjection();
	setAccessibleDescription(accessibleSummary());
	update();
}
bool ModelViewport::animationInterpolation() const { return m_interpolateAnimation; }
ModelAnimationSample ModelViewport::animationSample() const { return {m_frame, m_blendFrame, m_frameBlend}; }

bool ModelViewport::seekAnimation(double seconds)
{
	if (m_nativeMdl)
	{
		auto request = m_mdlPlayback;
		request.seconds = seconds;
		ModelMdlPlaybackSample sample;
		if (!sampleModelMdl(m_mesh, request, &sample))
		{
			return false;
		}
		finishEditTransform(false);
		m_mdlPlayback = request;
		invalidateRaster(true);
		applyMdlSample(sample);
		resetPlaybackClock();
		return true;
	}
	ModelAnimationSample sample;
	if (!m_hasMesh || !std::isfinite(seconds) || seconds < 0 ||
		!sampleModelAnimation(m_rangeFirst, m_rangeCount, seconds * m_fps, m_playing && m_interpolateAnimation, &sample))
	{
		return false;
	}
	finishEditTransform(false);
	invalidateRaster(true);
	applyAnimationSample(sample);
	resetPlaybackClock();
	return true;
}

void ModelViewport::updatePlaybackTimer()
{
	if (!m_timer)
	{
		return;
	}
	if (m_playing && !m_reducedMotion && m_hasMesh && canAnimate())
	{
		m_timer->setInterval(m_nativeMdl || m_interpolateAnimation ? 16 : std::max(16, int(std::ceil(1000.0 / m_fps))));
		if (!m_timer->isActive())
		{
			m_timer->start();
		}
	}
	else
	{
		m_timer->stop();
	}
}

void ModelViewport::advanceFrame()
{
	if (!m_playing || m_reducedMotion || !canAnimate() || !m_animationClock.isValid())
	{
		return;
	}
	ModelAnimationSample sample;
	const double elapsed = m_animationClock.nsecsElapsed() / 1e9;
	if (m_nativeMdl)
	{
		m_mdlPlayback.seconds = m_animationStart + elapsed;
		ModelMdlPlaybackSample native;
		if (sampleModelMdl(m_mesh, m_mdlPlayback, &native))
		{
			applyMdlSample(native);
		}
		return;
	}
	if (sampleModelAnimation(m_rangeFirst, m_rangeCount, m_animationStart + elapsed * m_fps, m_interpolateAnimation, &sample))
	{
		applyAnimationSample(sample);
	}
}

QString ModelViewport::interpolationWarning() const
{
	if (m_nativeMdl || !m_playing || !m_interpolateAnimation)
	{
		return {};
	}
	for (const auto &surface : m_mesh.surfaces)
	{
		if (m_frame >= surface.frames.size() || m_blendFrame >= surface.frames.size() ||
			surface.frames[m_frame].positions.size() != surface.frames[m_blendFrame].positions.size())
		{
			return tr("Some surfaces have incompatible poses and remain on their stored frame.");
		}
	}
	if (!m_showTags)
	{
		return {};
	}
	for (const auto &tag : m_mesh.tags)
	{
		if (tag.frameIndex != m_frame)
		{
			continue;
		}
		const auto next = findModelTag(m_mesh, tag.name, m_blendFrame);
		ModelTag checked;
		if (!next || !interpolateModelTag(tag, *next, 0.5, &checked))
		{
			return tr("Some attachment poses are incompatible and hidden between frames.");
		}
	}
	return {};
}

bool ModelViewport::canAnimate() const { return m_rangeCount > 1 || (m_nativeMdl && m_mdlPlaybackSkins.size() > 1); }

bool ModelViewport::setMdlPlayback(const ModelMdlPlayback &playback, const QVector<QImage> &skins, QString *error)
{
	ModelMdlPlaybackSample sample;
	if (!m_hasMesh || !sampleModelMdl(m_mesh, playback, &sample, error))
	{
		return false;
	}
	const qint64 pixels = qint64(m_mesh.mdl.skinSize.width()) * m_mesh.mdl.skinSize.height();
	if (skins.size() != m_mesh.embeddedSkins[playback.skin].indexedFrames.size() || pixels < 1 ||
		pixels > modelMdlMaxSkinPixels / skins.size() ||
		std::any_of(
			skins.cbegin(), skins.cend(), [this](const QImage &image)
			{ return image.isNull() || image.size() != m_mesh.mdl.skinSize || image.format() != QImage::Format_ARGB32_Premultiplied; }))
	{
		if (error)
		{
			*error = tr("Prepare the selected MDL skin's bounded opaque images before playback.");
		}
		return false;
	}
	pause();
	finishEditTransform(false);
	finishSurfaceStroke(false);
	m_nativeMdl = true;
	m_mdlPlayback = playback;
	m_mdlPlaybackSkins = skins;
	m_animation.clear();
	m_animationIndex = -1;
	const auto &group = m_mesh.mdl.frameGroups[playback.nativeFrame];
	m_rangeFirst = group.firstFrame;
	m_rangeCount = group.frameCount();
	invalidateRaster(true);
	applyMdlSample(sample);
	invalidateProjection();
	resetPlaybackClock();
	updatePlaybackTimer();
	setAccessibleDescription(accessibleSummary());
	Q_EMIT animationChanged(m_animation);
	Q_EMIT mdlPlaybackChanged();
	update();
	return true;
}

void ModelViewport::clearMdlPlayback()
{
	if (!m_nativeMdl)
	{
		return;
	}
	pause();
	m_nativeMdl = false;
	m_mdlPlaybackSkins.clear();
	m_mdlSample = {};
	m_rangeFirst = 0;
	m_rangeCount = m_frameTotal;
	m_blendFrame = m_frameTotal ? (m_frame + 1) % m_frameTotal : 0;
	m_frameBlend = 0;
	resetPlaybackClock();
	updatePlaybackTimer();
	invalidateRaster(true);
	invalidateProjection();
	setAccessibleDescription(accessibleSummary());
	Q_EMIT mdlPlaybackChanged();
	update();
}

QImage ModelViewport::mdlPlaybackSkin() const { return m_nativeMdl ? m_mdlPlaybackSkins.value(m_mdlSample.skinMember) : QImage{}; }

void ModelViewport::setMdlSkinVisible(bool visible)
{
	if (m_mdlSkinVisible == visible)
	{
		return;
	}
	m_mdlSkinVisible = visible;
	invalidateRaster(true);
	invalidateProjection();
	update();
}

void ModelViewport::applyMdlSample(const ModelMdlPlaybackSample &sample)
{
	const bool changed = sample.frame != m_mdlSample.frame || sample.skinMember != m_mdlSample.skinMember;
	const bool skinChanged = sample.skinMember != m_mdlSample.skinMember;
	m_mdlSample = sample;
	applyAnimationSample({sample.frame, sample.frame, 0});
	if (skinChanged)
	{
		invalidateRaster();
	}
	if (changed)
	{
		setAccessibleDescription(accessibleSummary());
		Q_EMIT mdlPlaybackChanged();
		update();
	}
}

QString ModelViewport::mdlPlaybackSummary() const
{
	return tr("%1 · native frame %2 · pose %3 · skin %4, member %5 · %6")
		.arg(m_mdlPlayback.timing == ModelMdlTiming::Stored ? tr("MDL stored timing") : tr("Original GLQuake timing"))
		.arg(m_mdlPlayback.nativeFrame)
		.arg(m_frame)
		.arg(m_mdlPlayback.skin)
		.arg(m_mdlSample.skinMember)
		.arg(m_reducedMotion ? tr("Reduced motion: seek manually")
			 : m_playing	 ? tr("Playing")
							 : tr("Paused"));
}
} // namespace vibestudio
