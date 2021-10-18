/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the config.tests of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qwaylandshellsurface_p.h"
#include "qwaylandwindow_p.h"
#include "qwaylandextendedsurface_p.h"
#include "qwaylandinputdevice_p.h"

QT_BEGIN_NAMESPACE

namespace QtWaylandClient {

QWaylandShellSurface::QWaylandShellSurface(QWaylandWindow *window)
                    : m_window(window)
{
}

void QWaylandShellSurface::setWindowFlags(Qt::WindowFlags flags)
{
    Q_UNUSED(flags);
}

void QWaylandShellSurface::sendProperty(const QString &name, const QVariant &value)
{
    Q_UNUSED(name);
    Q_UNUSED(value);
}

QPlatformWindow *QWaylandShellSurface::platformWindow()
{
    return m_window;
}

wl_surface *QWaylandShellSurface::wlSurface()
{
    return m_window ? m_window->wlSurface() : nullptr;
}

void QWaylandShellSurface::resizeFromApplyConfigure(const QSize &sizeWithMargins, const QPoint &offset)
{
    m_window->resizeFromApplyConfigure(sizeWithMargins, offset);
}

void QWaylandShellSurface::repositionFromApplyConfigure(const QPoint &position)
{
    m_window->repositionFromApplyConfigure(position);
}

void QWaylandShellSurface::setGeometryFromApplyConfigure(const QPoint &globalPosition, const QSize &sizeWithMargins)
{
    m_window->setGeometryFromApplyConfigure(globalPosition, sizeWithMargins);
}

void QWaylandShellSurface::applyConfigureWhenPossible()
{
    m_window->applyConfigureWhenPossible();
}

void QWaylandShellSurface::handleActivationChanged(bool activated)
{
    if (activated)
        m_window->display()->handleWindowActivated(m_window);
    else
        m_window->display()->handleWindowDeactivated(m_window);
}

uint32_t QWaylandShellSurface::getSerial(QWaylandInputDevice *inputDevice)
{
    return inputDevice->serial();
}

}

QT_END_NAMESPACE
