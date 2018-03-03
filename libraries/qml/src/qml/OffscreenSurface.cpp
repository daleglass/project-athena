//
//  Created by Bradley Austin Davis on 2015-05-13
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
#include "OffscreenSurface.h"

#include <unordered_set>
#include <unordered_map>

#include <QtCore/QThread>
#include <QtQml/QtQml>
#include <QtQml/QQmlEngine>
#include <QtQml/QQmlComponent>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QQuickRenderControl>

#include <GLMHelpers.h>

#include <gl/OffscreenGLCanvas.h>
#include <shared/ReadWriteLockable.h>

#include "Logging.h"
#include "impl/SharedObject.h"
#include "impl/TextureCache.h"

using namespace hifi::qml;
using namespace hifi::qml::impl;

static uvec2 clampSize(const uvec2& size, uint32_t maxDimension) {
    return glm::clamp(size, glm::uvec2(1), glm::uvec2(maxDimension));
}

static QSize clampSize(const QSize& qsize, uint32_t maxDimension) {
    return fromGlm(clampSize(toGlm(qsize), maxDimension));
}

const QmlContextObjectCallback OffscreenSurface::DEFAULT_CONTEXT_CALLBACK = [](QQmlContext*, QQuickItem*) {};

void OffscreenSurface::initializeEngine(QQmlEngine* engine) {
}

using namespace hifi::qml::impl;

size_t OffscreenSurface::getUsedTextureMemory() {
    return SharedObject::getTextureCache().getUsedTextureMemory();
}

void OffscreenSurface::setSharedContext(QOpenGLContext* sharedContext) {
    SharedObject::setSharedContext(sharedContext);
}

std::function<void(uint32_t, void*)> OffscreenSurface::getDiscardLambda() {
    return [](uint32_t texture, void* fence) {
        SharedObject::getTextureCache().releaseTexture({ texture, static_cast<GLsync>(fence) });
    };
}

OffscreenSurface::OffscreenSurface()
    : _sharedObject(new impl::SharedObject()) {
}

OffscreenSurface::~OffscreenSurface() {
    disconnect(qApp);
    _sharedObject->destroy();
}

bool OffscreenSurface::fetchTexture(TextureAndFence& textureAndFence) {
    if (!_sharedObject) {
        return false;
    }
    hifi::qml::impl::TextureAndFence typedTextureAndFence;
    bool result = _sharedObject->fetchTexture(typedTextureAndFence);
    textureAndFence = typedTextureAndFence;
    return result;
}

void OffscreenSurface::resize(const QSize& newSize_) {
    const uint32_t MAX_OFFSCREEN_DIMENSION = 4096;
    _sharedObject->setSize(clampSize(newSize_, MAX_OFFSCREEN_DIMENSION));
}

QQuickItem* OffscreenSurface::getRootItem() {
    return _sharedObject->getRootItem();
}

void OffscreenSurface::clearCache() {
    _sharedObject->getContext()->engine()->clearComponentCache();
}

QPointF OffscreenSurface::mapToVirtualScreen(const QPointF& originalPoint) {
    return _mouseTranslator(originalPoint);
}

///////////////////////////////////////////////////////
//
// Event handling customization
//

bool OffscreenSurface::filterEnabled(QObject* originalDestination, QEvent* event) const {
    if (!_sharedObject || _sharedObject->getWindow() == originalDestination) {
        return false;
    }
    // Only intercept events while we're in an active state
    if (_sharedObject->isPaused()) {
        return false;
    }
    return true;
}

bool OffscreenSurface::eventFilter(QObject* originalDestination, QEvent* event) {
    if (!filterEnabled(originalDestination, event)) {
        return false;
    }
#ifdef DEBUG
    // Don't intercept our own events, or we enter an infinite recursion
    {
        auto rootItem = _sharedObject->getRootItem();
        auto quickWindow = _sharedObject->getWindow();
        QObject* recurseTest = originalDestination;
        while (recurseTest) {
            Q_ASSERT(recurseTest != rootItem && recurseTest != quickWindow);
            recurseTest = recurseTest->parent();
        }
    }
#endif

    switch (event->type()) {
        case QEvent::KeyPress:
        case QEvent::KeyRelease: {
            event->ignore();
            if (QCoreApplication::sendEvent(_sharedObject->getWindow(), event)) {
                return event->isAccepted();
            }
            break;
        }

        case QEvent::Wheel: {
            QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);
            QPointF transformedPos = mapToVirtualScreen(wheelEvent->pos());
            QWheelEvent mappedEvent(transformedPos, wheelEvent->delta(), wheelEvent->buttons(), wheelEvent->modifiers(),
                                    wheelEvent->orientation());
            mappedEvent.ignore();
            if (QCoreApplication::sendEvent(_sharedObject->getWindow(), &mappedEvent)) {
                return mappedEvent.isAccepted();
            }
            break;
        }
        case QEvent::MouseMove: {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            QPointF transformedPos = mapToVirtualScreen(mouseEvent->localPos());
            QMouseEvent mappedEvent(mouseEvent->type(), transformedPos, mouseEvent->screenPos(), mouseEvent->button(),
                                    mouseEvent->buttons(), mouseEvent->modifiers());
            mappedEvent.ignore();
            if (QCoreApplication::sendEvent(_sharedObject->getWindow(), &mappedEvent)) {
                return mappedEvent.isAccepted();
            }
            break;
        }

#if defined(Q_OS_ANDROID)
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd: {
            QTouchEvent *originalEvent = static_cast<QTouchEvent *>(event);
            QTouchEvent fakeEvent(*originalEvent);
            auto newTouchPoints = fakeEvent.touchPoints();
            for (size_t i = 0; i < newTouchPoints.size(); ++i) {
                const auto &originalPoint = originalEvent->touchPoints()[i];
                auto &newPoint = newTouchPoints[i];
                newPoint.setPos(originalPoint.pos());
            }
            fakeEvent.setTouchPoints(newTouchPoints);
            if (QCoreApplication::sendEvent(_sharedObject->getWindow(), &fakeEvent)) {
                qInfo() << __FUNCTION__ << "sent fake touch event:" << fakeEvent.type()
                        << "_quickWindow handled it... accepted:" << fakeEvent.isAccepted();
                return false; //event->isAccepted();
            }
            break;
        }
        case QEvent::InputMethod:
        case QEvent::InputMethodQuery: {
            if (_sharedObject->getWindow() && _sharedObject->getWindow()->activeFocusItem()) {
                event->ignore();
                if (QCoreApplication::sendEvent(_sharedObject->getWindow()->activeFocusItem(), event)) {
                    return event->isAccepted();
                }
                return false;
            }
            break;
        }
#endif
        default:
            break;
    }

    return false;
}

void OffscreenSurface::pause() {
    _sharedObject->pause();
}

void OffscreenSurface::resume() {
    _sharedObject->resume();
}

bool OffscreenSurface::isPaused() const {
    return _sharedObject->isPaused();
}

void OffscreenSurface::setProxyWindow(QWindow* window) {
    _sharedObject->setProxyWindow(window);
}

QObject* OffscreenSurface::getEventHandler() {
    return getWindow();
}

QQuickWindow* OffscreenSurface::getWindow() {
    return _sharedObject->getWindow();
}

QSize OffscreenSurface::size() const {
    return _sharedObject->getSize();
}

QQmlContext* OffscreenSurface::getSurfaceContext() {
    return _sharedObject->getContext();
}

void OffscreenSurface::setMaxFps(uint8_t maxFps) {
    _sharedObject->setMaxFps(maxFps);
}

void OffscreenSurface::load(const QUrl& qmlSource, QQuickItem* parent, const QJSValue& callback) {
    loadInternal(qmlSource, false, parent, [callback](QQmlContext* context, QQuickItem* newItem) {
        QJSValue(callback).call(QJSValueList() << context->engine()->newQObject(newItem));
    });
}

void OffscreenSurface::load(const QUrl& qmlSource, bool createNewContext, const QmlContextObjectCallback& callback) {
    loadInternal(qmlSource, createNewContext, nullptr, callback);
}

void OffscreenSurface::loadInNewContext(const QUrl& qmlSource, const QmlContextObjectCallback& callback) {
    load(qmlSource, true, callback);
}

void OffscreenSurface::load(const QUrl& qmlSource, const QmlContextObjectCallback& callback) {
    load(qmlSource, false, callback);
}

void OffscreenSurface::load(const QString& qmlSourceFile, const QmlContextObjectCallback& callback) {
    return load(QUrl(qmlSourceFile), callback);
}

void OffscreenSurface::loadInternal(const QUrl& qmlSource,
                                    bool createNewContext,
                                    QQuickItem* parent,
                                    const QmlContextObjectCallback& callback) {
    if (QThread::currentThread() != thread()) {
        qFatal("Called load on a non-surface thread");
    }
    // Synchronous loading may take a while; restart the deadlock timer
    QMetaObject::invokeMethod(qApp, "updateHeartbeat", Qt::DirectConnection);

    if (!getRootItem()) {
        _sharedObject->create(this);
    }

    QUrl finalQmlSource = qmlSource;
    if ((qmlSource.isRelative() && !qmlSource.isEmpty()) || qmlSource.scheme() == QLatin1String("file")) {
        finalQmlSource = getSurfaceContext()->resolvedUrl(qmlSource);
    }

    if (!getRootItem()) {
        _sharedObject->setObjectName(finalQmlSource.toString());
    }

    auto targetContext = contextForUrl(finalQmlSource, parent, createNewContext);
    auto qmlComponent = new QQmlComponent(getSurfaceContext()->engine(), finalQmlSource, QQmlComponent::PreferSynchronous);
    if (qmlComponent->isLoading()) {
        connect(qmlComponent, &QQmlComponent::statusChanged, this,
                [=](QQmlComponent::Status) { finishQmlLoad(qmlComponent, targetContext, parent, callback); });
        return;
    }

    finishQmlLoad(qmlComponent, targetContext, parent, callback);
}

void OffscreenSurface::finishQmlLoad(QQmlComponent* qmlComponent,
                                     QQmlContext* qmlContext,
                                     QQuickItem* parent,
                                     const QmlContextObjectCallback& callback) {
    disconnect(qmlComponent, &QQmlComponent::statusChanged, this, 0);
    if (qmlComponent->isError()) {
        for (const auto& error : qmlComponent->errors()) {
            qCWarning(qmlLogging) << error.url() << error.line() << error;
        }
        qmlComponent->deleteLater();
        return;
    }

    QObject* newObject = qmlComponent->beginCreate(qmlContext);
    if (qmlComponent->isError()) {
        for (const auto& error : qmlComponent->errors()) {
            qCWarning(qmlLogging) << error.url() << error.line() << error;
        }
        if (!getRootItem()) {
            qFatal("Unable to finish loading QML root");
        }
        qmlComponent->deleteLater();
        return;
    }

    if (!newObject) {
        if (!getRootItem()) {
            qFatal("Could not load object as root item");
            return;
        }
        qCWarning(qmlLogging) << "Unable to load QML item";
        return;
    }

    qmlContext->engine()->setObjectOwnership(this, QQmlEngine::CppOwnership);

    // All quick items should be focusable
    QQuickItem* newItem = qobject_cast<QQuickItem*>(newObject);
    if (newItem) {
        // Make sure we make items focusable (critical for
        // supporting keyboard shortcuts)
        newItem->setFlag(QQuickItem::ItemIsFocusScope, true);
    }

    bool rootCreated = getRootItem() != nullptr;

    // Make sure we will call callback for this codepath
    // Call this before qmlComponent->completeCreate() otherwise ghost window appears
    // If we already have a root, just set a couple of flags and the ancestry
    if (rootCreated) {
        callback(qmlContext, newItem);
        if (!parent) {
            parent = getRootItem();
        }
        // Allow child windows to be destroyed from JS
        QQmlEngine::setObjectOwnership(newObject, QQmlEngine::JavaScriptOwnership);
        newObject->setParent(parent);
        newItem->setParentItem(parent);
    } else {
        // The root item is ready. Associate it with the window.
        _sharedObject->setRootItem(newItem);
    }

    qmlComponent->completeCreate();
    qmlComponent->deleteLater();

    onItemCreated(qmlContext, newItem);

    if (!rootCreated) {
        connect(newItem, SIGNAL(sendToScript(QVariant)), this, SIGNAL(fromQml(QVariant)));
        onRootCreated();
        emit rootItemCreated(newItem);
        // Call this callback after rootitem is set, otherwise VrMenu wont work
        callback(qmlContext, newItem);
    }
}

QQmlContext* OffscreenSurface::contextForUrl(const QUrl& qmlSource, QQuickItem* parent, bool forceNewContext) {
    QQmlContext* targetContext = parent ? QQmlEngine::contextForObject(parent) : getSurfaceContext();
    if (!targetContext) {
        targetContext = getSurfaceContext();
    }

    if (getRootItem() && forceNewContext) {
        targetContext = new QQmlContext(targetContext, targetContext->engine());
    }

    return targetContext;
}