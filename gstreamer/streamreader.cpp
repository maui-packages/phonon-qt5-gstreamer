/*  This file is part of the KDE project.

Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies).

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

#include "streamreader.h"

QT_BEGIN_NAMESPACE
#ifndef QT_NO_PHONON_ABSTRACTMEDIASTREAM
namespace Phonon
{
namespace Gstreamer
{

StreamReader::StreamReader(const Phonon::MediaSource &source, MediaObject *parent)
    :  m_pos(0)
    , m_size(0)
    , m_seekable(false)
    , m_mediaObject(parent)
{
    connectToSource(source);
}

int StreamReader::currentBufferSize() const
{
    return m_buffer.size();
}

void StreamReader::writeData(const QByteArray &data) {
    QMutexLocker locker(&m_mutex);

    m_buffer += data;

    m_waitingForData.wakeAll();

    if (m_mediaObject->state() != Phonon::BufferingState &&
        m_mediaObject->state() != Phonon::LoadingState)
        enoughData();
}

void StreamReader::setCurrentPos(qint64 pos)
{
    QMutexLocker locker(&m_mutex);

    m_pos = pos;
    seekStream(pos);
    m_buffer.clear();
}

quint64 StreamReader::currentPos() const
{
    return m_pos;
}

bool StreamReader::read(quint64 pos, int length, char * buffer)
{
    QMutexLocker locker(&m_mutex);

    if (currentPos() != pos) {
        if (!streamSeekable())
            return false;
        setCurrentPos(pos);
    }

    while (currentBufferSize() < length) {
        int oldSize = currentBufferSize();
        needData();

        m_waitingForData.wait(&m_mutex);

        if (oldSize == currentBufferSize()) {
            return false; // We didn't get any data
        }
    }

    qMemCopy(buffer, m_buffer.data(), length);
    m_pos += length;
    //truncate the buffer
    m_buffer = m_buffer.mid(length);
    return true;
}

void StreamReader::endOfData()
{
    m_waitingForData.wakeAll();
}

void StreamReader::setStreamSize(qint64 newSize) {
    m_size = newSize;
}

qint64 StreamReader::streamSize() const {
    return m_size;
}

void StreamReader::setStreamSeekable(bool seekable) {
    m_seekable = seekable;
}

bool StreamReader::streamSeekable() const {
    return m_seekable;
}

}
}
#endif //QT_NO_PHONON_ABSTRACTMEDIASTREAM

QT_END_NAMESPACE
