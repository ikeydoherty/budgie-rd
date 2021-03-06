/*
 * This file is part of budgie-rd
 *
 * Copyright © 2017-2018 Budgie Desktop Developers
 *
 * budgie-rd is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <QWaylandOutput>
#include <QWaylandOutputMode>

#include "compositor.h"
#include "view.h"

using Budgie::RenderLayer;

Budgie::Compositor::Compositor() : m_compositor(new QWaylandCompositor())
{
    m_wl_shell.reset(new QWaylandWlShell(m_compositor.data()));
    m_xdg_shell_v5.reset(new QWaylandXdgShellV5(m_compositor.data()));

    // We'll sort out our windows when we're created
    connect(m_compositor.data(),
            &QWaylandCompositor::createdChanged,
            this,
            &Budgie::Compositor::onCreated);

    // Respond to wl_surface creation
    connect(m_compositor.data(),
            &QWaylandCompositor::surfaceCreated,
            this,
            &Budgie::Compositor::surfaceCreated);

    // Respond to wl_shell_surface association
    connect(m_wl_shell.data(),
            &QWaylandWlShell::wlShellSurfaceCreated,
            this,
            &Budgie::Compositor::wlShellSurfaceCreated);

    // Respond to xdg (v5) shell creation
    connect(m_xdg_shell_v5.data(),
            &QWaylandXdgShellV5::xdgSurfaceCreated,
            this,
            &Budgie::Compositor::xdgSurfaceCreated);
}

void Budgie::Compositor::run()
{
    auto output = new QWaylandOutput(m_compositor.data(), nullptr);
    // Add fake output, 1024x768 @ 60hz
    QWaylandOutputMode mode(QSize(1024, 768), 60000);
    output->setPosition(QPoint(0, 0));
    output->addMode(mode, true);

    // Create our output manager
    m_window.reset(new Budgie::CompositorWindow(this, output));

    // Set stuff in motion now
    m_compositor->create();
}

QWaylandCompositor *Budgie::Compositor::compositor()
{
    return m_compositor.data();
}

void Budgie::Compositor::onCreated()
{
    // Again, just demo crap. Our compositor is created so set the default output
    // and display mode, and make it active.
    auto output = m_window->output();
    output->setCurrentMode(output->modes()[0]);
    m_compositor->setDefaultOutput(output);
    m_window->show();
}

/**
 * A new toplevel surface has been created
 *
 * It should be noted that the ->surface() of each extension surface "belongs"
 * to the toplevel QWaylandSurface, so we can implement parenting through this
 * system.
 */
void Budgie::Compositor::surfaceCreated(QWaylandSurface *surface)
{
    /* Store the surface now */
    auto item = new Budgie::CompositorSurfaceItem(surface);
    m_surfaces.insert(surface, QSharedPointer<Budgie::CompositorSurfaceItem>(item));

    if (item->surface()->isCursorSurface()) {
        qDebug() << "New cursor surface:" << surface;
    } else {
        qDebug() << "New wl_surface: " << surface;
    }

    // Make sure we nuke the guy
    connect(surface, &QWaylandSurface::destroyed, this, &Budgie::Compositor::surfaceDestroyed);

    // Now map this guy. Ideally we'd only map in xdg/wl_shell so we'd then know
    // which monitor to display it.
    auto view = m_window->mapSurface(item);
    if (!view) {
        return;
    }
    view->setPrimary();
}

/**
 * New wl_shell_surface has been created.
 */
void Budgie::Compositor::wlShellSurfaceCreated(QWaylandWlShellSurface *surface)
{
    auto parent = getSurfaceItem(surface->surface());
    if (!parent) {
        qWarning() << "Cannot set wlShell on unknown surface";
        return;
    }
    parent->setShellSurface(surface);
    qDebug() << "New wl_shell_surface" << surface << " with real surface: " << surface->surface();

    // FIXME: Don't hack
    parent->setLayer(RenderLayer::APPLICATION);
    assignLayer(parent);

    m_window->scheduleDraw();
}

/**
 * New XDG surface has been created
 */
void Budgie::Compositor::xdgSurfaceCreated(QWaylandXdgSurfaceV5 *surface)
{
    auto parent = getSurfaceItem(surface->surface());
    if (!parent) {
        qWarning() << "Cannot set xdgShell on unknown surface";
        return;
    }
    parent->setXdgSurfacev5(surface);
    qDebug() << "New XDG surface" << surface << " with real surface: " << surface->surface();
}

void Budgie::Compositor::surfaceDestroyed()
{
    auto surface = static_cast<QWaylandSurface *>(sender());
    auto surface_wrapper = m_surfaces.value(surface, nullptr);
    if (!surface_wrapper) {
        qWarning() << "Acounting error, unknown surface: " << surface;
        return;
    }

    qDebug() << "Surface removed: " << surface_wrapper.data();
    removeLayer(surface_wrapper.data());
    m_window->unmapSurface(surface_wrapper.data());
    m_surfaces.remove(surface);

    m_window->scheduleDraw();
}

/**
 * Get the corresponding surface item for the given Wayland surface
 */
Budgie::CompositorSurfaceItem *Budgie::Compositor::getSurfaceItem(QWaylandSurface *surface)
{
    auto ret = m_surfaces.value(surface, nullptr);
    if (ret.isNull()) {
        return nullptr;
    };
    return ret.data();
}

/**
 * Used by each window to build a list of renderables (pre-ordered) that should
 * be displayed and rendered in their current draw cycle.
 */
QList<Budgie::CompositorSurfaceItem *> Budgie::Compositor::getRenderables(
    Budgie::CompositorWindow *window)
{
    QList<Budgie::CompositorSurfaceItem *> drawables;

    // FIXME: Be less stupid
    for (int layer = RenderLayer::BACKGROUND; layer < RenderLayer::CURSOR; layer++) {
        for (Budgie::CompositorSurfaceItem *item : m_renderables[static_cast<RenderLayer>(layer)]) {
            // TODO: Only return items within this window!
            drawables << item;
        }
    }

    return drawables;
}

void Budgie::Compositor::assignLayer(Budgie::CompositorSurfaceItem *item)
{
    m_renderables[item->layer()].append(item);
}

void Budgie::Compositor::removeLayer(Budgie::CompositorSurfaceItem *item)
{
    m_renderables[item->layer()].removeAll(item);
}

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=4 tabstop=4 expandtab:
 * :indentSize=4:tabSize=4:noTabs=true:
 */
