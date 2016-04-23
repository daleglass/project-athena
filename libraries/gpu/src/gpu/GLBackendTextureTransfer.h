//
//  Created by Bradley Austin Davis on 2016/04/03
//  Copyright 2013-2016 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <QtGlobal>
#include <QSharedPointer>
#include <GenericQueueThread.h>
#include "GLBackendShared.h"

#ifdef Q_OS_WIN
#define THREADED_TEXTURE_TRANSFER
#endif

class OffscreenGLCanvas;

namespace gpu {

struct TextureTransferPackage {
    std::weak_ptr<Texture> texture;
    GLsync fence;
};

class GLTextureTransferHelper : public GenericQueueThread<TextureTransferPackage> {
public:
    GLTextureTransferHelper();
    ~GLTextureTransferHelper();
    void transferTexture(const gpu::TexturePointer& texturePointer);
    void postTransfer(const gpu::TexturePointer& texturePointer);

protected:
    void setup() override;
    void shutdown() override;
    bool processQueueItems(const Queue& messages) override;
    void do_transfer(GLBackend::GLTexture& texturePointer);

private:
    QSharedPointer<OffscreenGLCanvas> _canvas;
};

}
