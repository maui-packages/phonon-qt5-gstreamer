/*  This file is part of the KDE project.

    Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies).
    Copyright (C) 2011 Trever Fischer <tdfischer@fedoraproject.org>
    Copyright (C) 2011 Harald Sitter <sitter@kde.org>

    This library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 2.1 or 3 of the License.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this library.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <cmath>
#include <gst/interfaces/navigation.h>
#include <gst/interfaces/propertyprobe.h>
#include "mediaobject.h"
#include "backend.h"
#include "streamreader.h"
#include "phonon-config-gstreamer.h"
#include "gsthelper.h"
#include "pipeline.h"

#include <QtCore/QByteRef>
#include <QtCore/QEvent>
#include <QtCore/QFile>
#include <QtCore/QPointer>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/QVector>
#include <QtGui/QApplication>

#define ABOUT_TO_FINNISH_TIME 2000
#define MAX_QUEUE_TIME 20 * GST_SECOND

QT_BEGIN_NAMESPACE

namespace Phonon
{
namespace Gstreamer
{

MediaObject::MediaObject(Backend *backend, QObject *parent)
        : QObject(parent)
        , MediaNode(backend, AudioSource | VideoSource)
        , m_resumeState(false)
        , m_oldState(Phonon::LoadingState)
        , m_oldPos(0)
        , m_state(Phonon::StoppedState)
        , m_pendingState(Phonon::LoadingState)
        , m_tickTimer(new QTimer(this))
        , m_prefinishMark(0)
        , m_transitionTime(0)
        , m_isStream(false)
        , m_prefinishMarkReachedNotEmitted(true)
        , m_aboutToFinishEmitted(false)
        , m_loading(false)
        , m_totalTime(-1)
        , m_error(Phonon::NoError)
        , m_pipeline(0)
        , m_autoplayTitles(true)
        , m_availableTitles(0)
        , m_currentTitle(1)
        , m_pendingTitle(1)
        , m_waitingForNextSource(false)
        , m_waitingForPreviousSource(false)
{
    qRegisterMetaType<GstCaps*>("GstCaps*");
    qRegisterMetaType<State>("State");
    qRegisterMetaType<GstMessage*>("GstMessage*");

    static int count = 0;
    m_name = "MediaObject" + QString::number(count++);

    if (!m_backend->isValid()) {
        setError(tr("Cannot start playback. \n\nCheck your GStreamer installation and make sure you "
                    "\nhave libgstreamer-plugins-base installed."), Phonon::FatalError);
    } else {
        m_root = this;
        m_pipeline = new Pipeline(this);
        m_isValid = true;

        connect(m_pipeline, SIGNAL(aboutToFinish()),
                this, SLOT(handleAboutToFinish()), Qt::DirectConnection);
        connect(m_pipeline, SIGNAL(eos()),
                this, SLOT(handleEndOfStream()));
        connect(m_pipeline, SIGNAL(warning(QString)),
                this, SLOT(logWarning(QString)));
        connect(m_pipeline, SIGNAL(durationChanged(qint64)),
                this, SLOT(handleDurationChange(qint64)));
        connect(m_pipeline, SIGNAL(buffering(int)),
                this, SIGNAL(bufferStatus(int)));
        connect(m_pipeline, SIGNAL(stateChanged(GstState,GstState)),
                this, SLOT(handleStateChange(GstState,GstState)));
        connect(m_pipeline, SIGNAL(errorMessage(QString,Phonon::ErrorType)),
                this, SLOT(setError(QString,Phonon::ErrorType)));
        connect(m_pipeline, SIGNAL(metaDataChanged(QMultiMap<QString,QString>)),
                this, SIGNAL(metaDataChanged(QMultiMap<QString,QString>)));
        connect(m_pipeline, SIGNAL(availableMenusChanged(QList<MediaController::NavigationMenu>)),
                this, SIGNAL(availableMenusChanged(QList<MediaController::NavigationMenu>)));
        connect(m_pipeline, SIGNAL(videoAvailabilityChanged(bool)),
                this, SIGNAL(hasVideoChanged(bool)));
        connect(m_pipeline, SIGNAL(seekableChanged(bool)),
                this, SIGNAL(seekableChanged(bool)));
        connect(m_pipeline, SIGNAL(streamChanged()),
                this, SLOT(handleStreamChange()));

        connect(m_tickTimer, SIGNAL(timeout()), SLOT(emitTick()));
    }
    connect(this, SIGNAL(stateChanged(Phonon::State, Phonon::State)),
            this, SLOT(notifyStateChange(Phonon::State, Phonon::State)));
}

MediaObject::~MediaObject()
{
    if (m_pipeline) {
        GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(m_pipeline->element()));
        g_signal_handlers_disconnect_matched(bus, G_SIGNAL_MATCH_DATA, 0, 0, 0, 0, this);
        delete m_pipeline;
    }
}

void MediaObject::saveState()
{
    //Only first resumeState is respected
    if (m_resumeState)
        return;

    if (m_pendingState == Phonon::PlayingState || m_pendingState == Phonon::PausedState) {
        m_resumeState = true;
        m_oldState = m_pendingState;
        m_oldPos = getPipelinePos();
    }
}

void MediaObject::resumeState()
{
    if (m_resumeState)
        QMetaObject::invokeMethod(this, "setState", Qt::QueuedConnection, Q_ARG(State, m_oldState));
}

/**
 * !reimp
 */
State MediaObject::state() const
{
    return m_state;
}

/**
 * !reimp
 */
bool MediaObject::hasVideo() const
{
    return m_pipeline->videoIsAvailable();
}

/**
 * !reimp
 */
bool MediaObject::isSeekable() const
{
    return m_pipeline->isSeekable();
}

/**
 * !reimp
 */
qint64 MediaObject::currentTime() const
{
    if (m_resumeState)
        return m_oldPos;

    switch (state()) {
    case Phonon::PausedState:
    case Phonon::BufferingState:
    case Phonon::PlayingState:
        return getPipelinePos();
    case Phonon::StoppedState:
    case Phonon::LoadingState:
        return 0;
    case Phonon::ErrorState:
        break;
    }
    return -1;
}

/**
 * !reimp
 */
qint32 MediaObject::tickInterval() const
{
    return m_tickInterval;
}

/**
 * !reimp
 */
void MediaObject::setTickInterval(qint32 newTickInterval)
{
    m_tickInterval = newTickInterval;
    if (m_tickInterval <= 0)
        m_tickTimer->setInterval(50);
    else
        m_tickTimer->setInterval(newTickInterval);
}

/**
 * !reimp
 */
void MediaObject::play()
{
    requestState(Phonon::PlayingState);
}

/**
 * !reimp
 */
QString MediaObject::errorString() const
{
    return m_errorString;
}

/**
 * !reimp
 */
Phonon::ErrorType MediaObject::errorType() const
{
    return m_error;
}

void MediaObject::setError(const QString &errorString, Phonon::ErrorType error)
{
    m_errorString = errorString;
    m_error = error;
    requestState(Phonon::ErrorState);
}

qint64 MediaObject::totalTime() const
{
    return m_totalTime;
}

qint32 MediaObject::prefinishMark() const
{
    return m_prefinishMark;
}

qint32 MediaObject::transitionTime() const
{
    return m_transitionTime;
}

void MediaObject::setTransitionTime(qint32 time)
{
    m_transitionTime = time;
}

qint64 MediaObject::remainingTime() const
{
    return totalTime() - currentTime();
}

MediaSource MediaObject::source() const
{
    return m_source;
}

void MediaObject::setNextSource(const MediaSource &source)
{
    qDebug() << "Got next source. Waiting for end of current.";
    m_waitingForNextSource = true;
    m_waitingForPreviousSource = false;
    m_pipeline->setSource(source);
    m_aboutToFinishWait.wakeAll();
}

qint64 MediaObject::getPipelinePos() const
{
    Q_ASSERT(m_pipeline);

    // Note some formats (usually mpeg) do not allow us to accurately seek to the
    // beginning or end of the file so we 'fake' it here rather than exposing the front end to potential issues.
    //
    return m_pipeline->position();
}

/*
 * !reimp
 */
void MediaObject::setSource(const MediaSource &source)
{
    if (!isValid())
        return;

    qDebug() << "Setting new source";
    m_source = source;
    m_pipeline->setSource(source);
    m_aboutToFinishWait.wakeAll();
    //emit currentSourceChanged(source);
}

// Called when we are ready to leave the loading state
void MediaObject::loadingComplete()
{
    if (m_pipeline->videoIsAvailable()) {
        MediaNodeEvent event(MediaNodeEvent::VideoAvailable);
        notify(&event);
    }
    link();
}

void MediaObject::getStreamInfo()
{
    m_pipeline->updateNavigation();

    if (m_source.discType() == Phonon::Cd) {
        gint64 titleCount;
        GstFormat format = gst_format_get_by_nick("track");
        if (m_pipeline->queryDuration(&format, &titleCount)) {
        //check if returned format is still "track",
        //gstreamer sometimes returns the total time, if tracks information is not available.
            if (qstrcmp(gst_format_get_name(format), "track") == 0)  {
                int oldAvailableTitles = m_availableTitles;
                m_availableTitles = (int)titleCount;
                if (m_availableTitles != oldAvailableTitles) {
                    emit availableTitlesChanged(m_availableTitles);
                    m_backend->logMessage(QString("Available titles changed: %0").arg(m_availableTitles), Backend::Info, this);
                }
            }
        }
    }
}

void MediaObject::setPrefinishMark(qint32 newPrefinishMark)
{
    m_prefinishMark = newPrefinishMark;
    if (currentTime() < totalTime() - m_prefinishMark) // not about to finish
        m_prefinishMarkReachedNotEmitted = true;
}

void MediaObject::pause()
{
    requestState(Phonon::PausedState);
}

void MediaObject::stop()
{
    requestState(Phonon::StoppedState);
}

void MediaObject::seek(qint64 time)
{
    if (!isValid())
        return;

    if (m_waitingForNextSource) {
        qDebug() << "Seeking back within old source";
        m_waitingForNextSource = false;
        m_waitingForPreviousSource = true;
        m_pipeline->setSource(m_source, true);
    }
    m_pipeline->seekToMSec(time);
    m_lastTime = 0;
}

void MediaObject::handleStreamChange()
{
    if (m_waitingForPreviousSource) {
        m_waitingForPreviousSource = false;
    } else {
        m_source = m_pipeline->currentSource();
        m_waitingForNextSource = false;
        emit currentSourceChanged(m_pipeline->currentSource());
    }
}

void MediaObject::handleDurationChange(qint64 duration)
{
    m_totalTime = duration;
    emit totalTimeChanged(duration);
}

void MediaObject::emitTick()
{
    if (m_resumeState) {
        return;
    }

    qint64 currentTime = getPipelinePos();
    // We don't get any other kind of notification when we change DVD chapters, so here's the best place...
    // TODO: Verify that this is fixed with playbin2 and that we don't need to manually update the
    // time when playing a DVD.
    //updateTotalTime();
    emit tick(currentTime);

    if (m_state == Phonon::PlayingState) {
        if (currentTime >= totalTime() - m_prefinishMark) {
            if (m_prefinishMarkReachedNotEmitted) {
                m_prefinishMarkReachedNotEmitted = false;
                emit prefinishMarkReached(totalTime() - currentTime);
            }
        }
    }
}

/**
 * Triggers playback after a song has completed in the current media queue
 */
void MediaObject::beginPlay()
{
    setSource(m_nextSource);
    m_nextSource = MediaSource();
    m_pendingState = Phonon::PlayingState;
}

Phonon::State MediaObject::translateState(GstState state) const
{
    switch (state) {
        case GST_STATE_PLAYING:
            return Phonon::PlayingState;
        case GST_STATE_PAUSED:
            return Phonon::PausedState;
        case GST_STATE_READY:
            return Phonon::StoppedState;
        case GST_STATE_NULL:
            return Phonon::LoadingState;
    }
    return Phonon::ErrorState;
}

void MediaObject::handleStateChange(GstState oldState, GstState newState)
{
    Phonon::State prevPhononState = m_state;
    prevPhononState = translateState(oldState);
    m_state = translateState(newState);
    qDebug() << "Moving from" << GstHelper::stateName(oldState) << prevPhononState << "to" << GstHelper::stateName(newState) << m_state;
    if (GST_STATE_TRANSITION(oldState, newState) == GST_STATE_CHANGE_NULL_TO_READY)
        loadingComplete();
    if (newState == GST_STATE_PLAYING)
        m_tickTimer->start();
    else
        m_tickTimer->stop();

    if (newState == GST_STATE_READY)
        emit tick(0);

    emit stateChanged(m_state, prevPhononState);
}

void MediaObject::handleEndOfStream()
{
    emit finished();
    m_pipeline->setState(GST_STATE_READY);
}

void MediaObject::invalidateGraph()
{
}

// Notifes the pipeline about state changes in the media object
void MediaObject::notifyStateChange(Phonon::State newstate, Phonon::State oldstate)
{
    Q_UNUSED(oldstate);
    MediaNodeEvent event(MediaNodeEvent::StateChanged, &newstate);
    notify(&event);
}

#ifndef QT_NO_PHONON_MEDIACONTROLLER
//interface management
bool MediaObject::hasInterface(Interface iface) const
{
    return iface == AddonInterface::TitleInterface || iface == AddonInterface::NavigationInterface;
}

QVariant MediaObject::interfaceCall(Interface iface, int command, const QList<QVariant> &params)
{
    if (hasInterface(iface)) {

        switch (iface)
        {
        case TitleInterface:
            switch (command)
            {
            case availableTitles:
                return _iface_availableTitles();
            case title:
                return _iface_currentTitle();
            case setTitle:
                _iface_setCurrentTitle(params.first().toInt());
                break;
            case autoplayTitles:
                return m_autoplayTitles;
            case setAutoplayTitles:
                m_autoplayTitles = params.first().toBool();
                break;
            }
            break;
                default:
            break;
        case NavigationInterface:
            switch(command)
            {
                case availableMenus:
                    return QVariant::fromValue<QList<MediaController::NavigationMenu> >(_iface_availableMenus());
                case setMenu:
                    _iface_jumpToMenu(params.first().value<Phonon::MediaController::NavigationMenu>());
                    break;
            }
            break;
        }
    }
    return QVariant();
}
#endif

QList<MediaController::NavigationMenu> MediaObject::_iface_availableMenus() const
{
    return m_pipeline->availableMenus();
}

void MediaObject::_iface_jumpToMenu(MediaController::NavigationMenu menu)
{
#if GST_VERSION >= GST_VERSION_CHECK(0,10,23,0)
    GstNavigationCommand command;
    switch(menu) {
    case MediaController::RootMenu:
        command = GST_NAVIGATION_COMMAND_DVD_ROOT_MENU;
        break;
    case MediaController::TitleMenu:
        command = GST_NAVIGATION_COMMAND_DVD_TITLE_MENU;
        break;
    case MediaController::AudioMenu:
        command = GST_NAVIGATION_COMMAND_DVD_AUDIO_MENU;
        break;
    case MediaController::SubtitleMenu:
        command = GST_NAVIGATION_COMMAND_DVD_SUBPICTURE_MENU;
        break;
    case MediaController::ChapterMenu:
        command = GST_NAVIGATION_COMMAND_DVD_CHAPTER_MENU;
        break;
    case MediaController::AngleMenu:
        command = GST_NAVIGATION_COMMAND_DVD_ANGLE_MENU;
        break;
    default:
        return;
    }

    GstElement *target = gst_bin_get_by_interface(GST_BIN(m_pipeline->element()), GST_TYPE_NAVIGATION);
    if (target)
        gst_navigation_send_command(GST_NAVIGATION(target), command);
#endif
}

int MediaObject::_iface_availableTitles() const
{
    return m_availableTitles;
}

int MediaObject::_iface_currentTitle() const
{
    return m_currentTitle;
}

void MediaObject::_iface_setCurrentTitle(int title)
{
    /*m_backend->logMessage(QString("setCurrentTitle %0").arg(title), Backend::Info, this);
    if ((title == m_currentTitle) || (title == m_pendingTitle))
        return;

    m_pendingTitle = title;

    if (m_state == Phonon::PlayingState || m_state == Phonon::StoppedState) {
        setTrack(m_pendingTitle);
    } else {
        setState(Phonon::StoppedState);
    }*/
}

void MediaObject::setTrack(int title)
{
    if (((m_state != Phonon::PlayingState) && (m_state != Phonon::StoppedState)) || (title < 1) || (title > m_availableTitles))
        return;

    //let's seek to the beginning of the song
    GstFormat trackFormat = gst_format_get_by_nick("track");
    m_backend->logMessage(QString("setTrack %0").arg(title), Backend::Info, this);
    if (gst_element_seek_simple(m_pipeline->element(), trackFormat, GST_SEEK_FLAG_FLUSH, title - 1)) {
        m_currentTitle = title;
        emit titleChanged(title);
        emit totalTimeChanged(totalTime());
    }
}

void MediaObject::logWarning(const QString &msg)
{
    m_backend->logMessage(msg, Backend::Warning);
}

void MediaObject::handleBuffering(int percent)
{
    Q_ASSERT(0);
    m_backend->logMessage(QString("Stream buffering %0").arg(percent), Backend::Debug, this);
    if (m_state != Phonon::BufferingState)
        emit stateChanged(m_state, Phonon::BufferingState);
    else if (percent == 100)
        emit stateChanged(Phonon::BufferingState, m_state);
}

QMultiMap<QString, QString> MediaObject::metaData()
{
    return m_pipeline->metaData();
}

void MediaObject::setMetaData(QMultiMap<QString, QString> newData)
{
    m_pipeline->setMetaData(newData);
}

void MediaObject::handleMouseOverChange(bool active)
{
    MediaNodeEvent mouseOverEvent(MediaNodeEvent::VideoMouseOver, &active);
    notify(&mouseOverEvent);
}

void MediaObject::requestState(Phonon::State state)
{
    switch (state) {
        case Phonon::PlayingState:
            m_pipeline->setState(GST_STATE_PLAYING);
            break;
        case Phonon::PausedState:
            m_pipeline->setState(GST_STATE_PAUSED);
            break;
        case Phonon::StoppedState:
            m_pipeline->setState(GST_STATE_READY);
            break;
        case Phonon::ErrorState:
            // Use ErrorState to represent a fatal error
            m_pipeline->setState(GST_STATE_NULL);
            break;
    }
}

void MediaObject::handleAboutToFinish()
{
    qDebug() << "About to finish";
    emit aboutToFinish();
    m_aboutToFinishLock.lock();
    m_aboutToFinishWait.wait(&m_aboutToFinishLock);
    qDebug() << "Finally got a source";
    m_aboutToFinishLock.unlock();
}

} // ns Gstreamer
} // ns Phonon

QT_END_NAMESPACE

#include "moc_mediaobject.cpp"
