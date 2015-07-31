/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the QtWaylandCompositor module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL3$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPLv3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or later as published by the Free
** Software Foundation and appearing in the file LICENSE.GPL included in
** the packaging of this file. Please review the following information to
** ensure the GNU General Public License version 2.0 requirements will be
** met: http://www.gnu.org/licenses/gpl-2.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qwlsurface_p.h"

#include "qwaylandsurface.h"
#include "qwaylandsurface_p.h"
#include "qwaylandview.h"
#include "qwaylandview_p.h"
#include "qwaylandoutput.h"
#include "qwlcompositor_p.h"
#include "qwlinputdevice_p.h"
#include "qwlextendedsurface_p.h"
#include "qwlregion_p.h"
#include "qwlsubsurface_p.h"
#include "qwlsurfacebuffer_p.h"
#include "qwaylandoutput.h"
#include "qwaylandsurface_p.h"

#include <QtCore/QDebug>
#include <QTouchEvent>
#include <QGuiApplication>
#include <QScreen>

#include <wayland-server.h>

QT_BEGIN_NAMESPACE

namespace QtWayland {

class FrameCallback {
public:
    FrameCallback(Surface *surf, wl_resource *res)
        : surface(surf)
        , resource(res)
        , canSend(false)
    {
#if WAYLAND_VERSION_MAJOR < 1 || (WAYLAND_VERSION_MAJOR == 1 && WAYLAND_VERSION_MINOR <= 2)
        res->data = this;
        res->destroy = destroyCallback;
#else
        wl_resource_set_implementation(res, 0, this, destroyCallback);
#endif
    }
    ~FrameCallback()
    {
    }
    void destroy()
    {
        if (resource)
            wl_resource_destroy(resource);
        else
            delete this;
    }
    void send(uint time)
    {
        wl_callback_send_done(resource, time);
        wl_resource_destroy(resource);
    }
    static void destroyCallback(wl_resource *res)
    {
#if WAYLAND_VERSION_MAJOR < 1 || (WAYLAND_VERSION_MAJOR == 1 && WAYLAND_VERSION_MINOR <= 2)
        FrameCallback *_this = static_cast<FrameCallback *>(res->data);
#else
        FrameCallback *_this = static_cast<FrameCallback *>(wl_resource_get_user_data(res));
#endif
        _this->surface->removeFrameCallback(_this);
        delete _this;
    }
    Surface *surface;
    wl_resource *resource;
    bool canSend;
};

static QRegion infiniteRegion() {
    return QRegion(QRect(QPoint(std::numeric_limits<int>::min(), std::numeric_limits<int>::min()),
                         QPoint(std::numeric_limits<int>::max(), std::numeric_limits<int>::max())));
}

Surface::Surface(struct wl_client *client, uint32_t id, int version, QWaylandCompositor *compositor, QWaylandSurface *surface)
    : QtWaylandServer::wl_surface(client, id, version)
    , m_compositor(compositor->handle())
    , m_waylandSurface(surface)
    , m_primaryOutput(0)
    , m_buffer(0)
    , m_surfaceMapped(false)
    , m_subSurface(0)
    , m_inputPanelSurface(0)
    , m_inputRegion(infiniteRegion())
    , m_isCursorSurface(false)
    , m_destroyed(false)
    , m_contentOrientation(Qt::PrimaryOrientation)
    , m_visibility(QWindow::Hidden)
    , m_role(0)
    , m_roleHandler(0)
{
    m_pending.buffer = 0;
    m_pending.newlyAttached = false;
    m_pending.inputRegion = infiniteRegion();
}

Surface::~Surface()
{
    delete m_subSurface;

    m_bufferRef = QWaylandBufferRef();

    for (int i = 0; i < m_bufferPool.size(); i++)
        m_bufferPool[i]->setDestroyIfUnused(true);

    foreach (FrameCallback *c, m_pendingFrameCallbacks)
        c->destroy();
    foreach (FrameCallback *c, m_frameCallbacks)
        c->destroy();
}

bool Surface::setRole(const SurfaceRole *role, wl_resource *errorResource, uint32_t errorCode)
{
    if (m_role && m_role != role) {
        wl_resource_post_error(errorResource, errorCode, "Cannot assign role %s to wl_surface@%d, already has role %s\n", role->name,
                               wl_resource_get_id(resource()->handle), m_role->name);
        return false;
    }
    m_role = role;
    return true;
}

Surface *Surface::fromResource(struct ::wl_resource *resource)
{
    return static_cast<Surface *>(Resource::fromResource(resource)->surface_object);
}

bool Surface::mapped() const
{
    return m_buffer && bool(m_buffer->waylandBufferHandle());
}

QSize Surface::size() const
{
    return m_size;
}

void Surface::setSize(const QSize &size)
{
    if (size != m_size) {
        m_opaqueRegion = QRegion();
        m_size = size;
        m_waylandSurface->sizeChanged();
    }
}

QRegion Surface::inputRegion() const
{
    return m_inputRegion;
}

QRegion Surface::opaqueRegion() const
{
    return m_opaqueRegion;
}

void Surface::sendFrameCallback()
{
    uint time = m_compositor->currentTimeMsecs();
    foreach (FrameCallback *callback, m_frameCallbacks) {
        if (callback->canSend) {
            callback->send(time);
            m_frameCallbacks.removeOne(callback);
        }
    }
}

void Surface::removeFrameCallback(FrameCallback *callback)
{
    m_pendingFrameCallbacks.removeOne(callback);
    m_frameCallbacks.removeOne(callback);
}

QWaylandSurface * Surface::waylandSurface() const
{
    return m_waylandSurface;
}

QPoint Surface::lastMousePos() const
{
    return m_lastLocalMousePos;
}

void Surface::setSubSurface(SubSurface *subSurface)
{
    m_subSurface = subSurface;
}

SubSurface *Surface::subSurface() const
{
    return m_subSurface;
}

void Surface::setInputPanelSurface(InputPanelSurface *inputPanelSurface)
{
    m_inputPanelSurface = inputPanelSurface;
}

InputPanelSurface *Surface::inputPanelSurface() const
{
    return m_inputPanelSurface;
}

Compositor *Surface::compositor() const
{
    return m_compositor;
}

void Surface::setPrimaryOutput(Output *output)
{
    if (m_primaryOutput == output)
        return;

    QWaylandOutput *new_output = output ? output->waylandOutput() : Q_NULLPTR;
    QWaylandOutput *old_output = m_primaryOutput ? m_primaryOutput->waylandOutput() : Q_NULLPTR;
    m_primaryOutput = output;

    waylandSurface()->primaryOutputChanged(new_output, old_output);
}

Output *Surface::primaryOutput() const
{
    return m_primaryOutput;
}

/*!
 * Sets the backbuffer for this surface. The back buffer is not yet on
 * screen and will become live during the next swapBuffers().
 *
 * The backbuffer represents the current state of the surface for the
 * purpose of GUI-thread accessible properties such as size and visibility.
 */
void Surface::setBackBuffer(SurfaceBuffer *buffer, const QRegion &damage)
{
    m_buffer = buffer;
    m_bufferRef = QWaylandBufferRef(m_buffer);

    if (m_buffer) {
        bool valid = m_buffer->waylandBufferHandle() != 0;
        if (valid)
            setSize(m_buffer->size());

        m_damage = m_damage.intersected(QRect(QPoint(), m_size));
        emit m_waylandSurface->damaged(m_damage);
    }

    m_damage = damage;

    QWaylandSurfacePrivate *priv = QWaylandSurfacePrivate::get(waylandSurface());
    for (int i = 0; i < priv->views.size(); i++) {
        priv->views.at(i)->attach(m_bufferRef);
    }

    emit m_waylandSurface->configure(m_bufferRef.hasBuffer());
    if (!m_pending.offset.isNull())
        emit m_waylandSurface->offsetForNextFrame(m_pending.offset);
}

void Surface::setMapped(bool mapped)
{
    if (!m_surfaceMapped && mapped) {
        m_surfaceMapped = true;
        emit m_waylandSurface->mapped();
    } else if (!mapped && m_surfaceMapped) {
        m_surfaceMapped = false;
        emit m_waylandSurface->unmapped();
    }
}

SurfaceBuffer *Surface::createSurfaceBuffer(struct ::wl_resource *buffer)
{
    SurfaceBuffer *newBuffer = 0;
    for (int i = 0; i < m_bufferPool.size(); i++) {
        if (!m_bufferPool[i]->isRegisteredWithBuffer()) {
            newBuffer = m_bufferPool[i];
            newBuffer->initialize(buffer);
            break;
        }
    }

    if (!newBuffer) {
        newBuffer = new SurfaceBuffer(this);
        newBuffer->initialize(buffer);
        m_bufferPool.append(newBuffer);
        if (m_bufferPool.size() > 3)
            qWarning() << "Increased buffer pool size to" << m_bufferPool.size() << "for surface with title:" << title() << "className:" << className();
    }

    return newBuffer;
}

Qt::ScreenOrientation Surface::contentOrientation() const
{
    return m_contentOrientation;
}

void Surface::notifyViewsAboutDestruction()
{
    foreach (QWaylandView *view, m_waylandSurface->views()) {
        QWaylandViewPrivate::get(view)->markSurfaceAsDestroyed(m_waylandSurface);
    }
}

void Surface::surface_destroy_resource(Resource *)
{
    notifyViewsAboutDestruction();

    m_destroyed = true;
    m_waylandSurface->destroy();
    emit m_waylandSurface->surfaceDestroyed();
}

void Surface::surface_destroy(Resource *resource)
{
    wl_resource_destroy(resource->handle);
}

void Surface::surface_attach(Resource *, struct wl_resource *buffer, int x, int y)
{
    if (m_pending.buffer)
        m_pending.buffer->disown();
    m_pending.buffer = createSurfaceBuffer(buffer);
    m_pending.offset = QPoint(x, y);
    m_pending.newlyAttached = true;
}

void Surface::surface_damage(Resource *, int32_t x, int32_t y, int32_t width, int32_t height)
{
    m_pending.damage = m_pending.damage.united(QRect(x, y, width, height));
}

void Surface::surface_frame(Resource *resource, uint32_t callback)
{
    struct wl_resource *frame_callback = wl_resource_create(resource->client(), &wl_callback_interface, wl_callback_interface.version, callback);
    m_pendingFrameCallbacks << new FrameCallback(this, frame_callback);
}

void Surface::surface_set_opaque_region(Resource *, struct wl_resource *region)
{
    m_opaqueRegion = region ? Region::fromResource(region)->region() : QRegion();
}

void Surface::surface_set_input_region(Resource *, struct wl_resource *region)
{
    if (region) {
        m_pending.inputRegion = Region::fromResource(region)->region();
    } else {
        m_pending.inputRegion = infiniteRegion();
    }
}

void Surface::surface_commit(Resource *)
{
    if (m_pending.buffer || m_pending.newlyAttached) {
        setBackBuffer(m_pending.buffer, m_pending.damage);
    }

    m_pending.buffer = 0;
    m_pending.offset = QPoint();
    m_pending.newlyAttached = false;
    m_pending.damage = QRegion();

    if (m_buffer)
        m_buffer->setCommitted();

    m_frameCallbacks << m_pendingFrameCallbacks;
    m_pendingFrameCallbacks.clear();

    m_inputRegion = m_pending.inputRegion.intersected(QRect(QPoint(), m_size));

    emit m_waylandSurface->redraw();

    if (primaryOutput())
        primaryOutput()->waylandOutput()->update();
}

void Surface::surface_set_buffer_transform(Resource *resource, int32_t orientation)
{
    Q_UNUSED(resource);
    QScreen *screen = QGuiApplication::primaryScreen();
    bool isPortrait = screen->primaryOrientation() == Qt::PortraitOrientation;
    Qt::ScreenOrientation oldOrientation = m_contentOrientation;
    switch (orientation) {
        case WL_OUTPUT_TRANSFORM_90:
            m_contentOrientation = isPortrait ? Qt::InvertedLandscapeOrientation : Qt::PortraitOrientation;
            break;
        case WL_OUTPUT_TRANSFORM_180:
            m_contentOrientation = isPortrait ? Qt::InvertedPortraitOrientation : Qt::InvertedLandscapeOrientation;
            break;
        case WL_OUTPUT_TRANSFORM_270:
            m_contentOrientation = isPortrait ? Qt::LandscapeOrientation : Qt::InvertedPortraitOrientation;
            break;
        default:
            m_contentOrientation = Qt::PrimaryOrientation;
    }
    if (m_contentOrientation != oldOrientation)
        emit waylandSurface()->contentOrientationChanged();
}

void Surface::frameStarted()
{
    foreach (FrameCallback *c, m_frameCallbacks)
        c->canSend = true;
}

void Surface::setClassName(const QString &className)
{
    if (m_className != className) {
        m_className = className;
        emit waylandSurface()->classNameChanged();
    }
}

void Surface::setTitle(const QString &title)
{
    if (m_title != title) {
        m_title = title;
        emit waylandSurface()->titleChanged();
    }
}

} // namespace Wayland

QT_END_NAMESPACE
