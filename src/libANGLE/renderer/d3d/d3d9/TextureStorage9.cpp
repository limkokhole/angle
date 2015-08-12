//
// Copyright (c) 2012-2014 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// TextureStorage9.cpp: Implements the abstract rx::TextureStorage9 class and its concrete derived
// classes TextureStorage9_2D and TextureStorage9_Cube, which act as the interface to the
// D3D9 texture.

#include "libANGLE/renderer/d3d/d3d9/TextureStorage9.h"
#include "libANGLE/renderer/d3d/d3d9/Renderer9.h"
#include "libANGLE/renderer/d3d/d3d9/SwapChain9.h"
#include "libANGLE/renderer/d3d/d3d9/RenderTarget9.h"
#include "libANGLE/renderer/d3d/d3d9/renderer9_utils.h"
#include "libANGLE/renderer/d3d/d3d9/formatutils9.h"
#include "libANGLE/renderer/d3d/TextureD3D.h"
#include "libANGLE/formatutils.h"
#include "libANGLE/Texture.h"

namespace rx
{
TextureStorage9::TextureStorage9(Renderer9 *renderer, DWORD usage)
    : mTopLevel(0),
      mMipLevels(0),
      mTextureWidth(0),
      mTextureHeight(0),
      mInternalFormat(GL_NONE),
      mTextureFormat(D3DFMT_UNKNOWN),
      mRenderer(renderer),
      mD3DUsage(usage),
      mD3DPool(mRenderer->getTexturePool(usage))
{
}

TextureStorage9::~TextureStorage9()
{
}

DWORD TextureStorage9::GetTextureUsage(GLenum internalformat, bool renderTarget)
{
    DWORD d3dusage = 0;

    const gl::InternalFormat &formatInfo = gl::GetInternalFormatInfo(internalformat);
    const d3d9::TextureFormat &d3dFormatInfo = d3d9::GetTextureFormatInfo(internalformat);
    if (formatInfo.depthBits > 0 || formatInfo.stencilBits > 0)
    {
        d3dusage |= D3DUSAGE_DEPTHSTENCIL;
    }
    else if (renderTarget && (d3dFormatInfo.renderFormat != D3DFMT_UNKNOWN))
    {
        d3dusage |= D3DUSAGE_RENDERTARGET;
    }

    return d3dusage;
}


bool TextureStorage9::isRenderTarget() const
{
    return (mD3DUsage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)) != 0;
}

bool TextureStorage9::isManaged() const
{
    return (mD3DPool == D3DPOOL_MANAGED);
}

bool TextureStorage9::supportsNativeMipmapFunction() const
{
    return false;
}

D3DPOOL TextureStorage9::getPool() const
{
    return mD3DPool;
}

DWORD TextureStorage9::getUsage() const
{
    return mD3DUsage;
}

int TextureStorage9::getTopLevel() const
{
    return mTopLevel;
}

int TextureStorage9::getLevelCount() const
{
    return static_cast<int>(mMipLevels) - mTopLevel;
}

gl::Error TextureStorage9::setData(const gl::ImageIndex &index, ImageD3D *image, const gl::Box *destBox, GLenum type,
                                   const gl::PixelUnpackState &unpack, const uint8_t *pixelData)
{
    UNREACHABLE();
    return gl::Error(GL_INVALID_OPERATION);
}

TextureStorage9_2D::TextureStorage9_2D(Renderer9 *renderer, SwapChain9 *swapchain)
    : TextureStorage9(renderer, D3DUSAGE_RENDERTARGET)
{
    IDirect3DTexture9 *surfaceTexture = swapchain->getOffscreenTexture();
    mTexture = surfaceTexture;
    mMipLevels = surfaceTexture->GetLevelCount();

    mInternalFormat = swapchain->GetRenderTargetInternalFormat();

    D3DSURFACE_DESC surfaceDesc;
    surfaceTexture->GetLevelDesc(0, &surfaceDesc);
    mTextureWidth = surfaceDesc.Width;
    mTextureHeight = surfaceDesc.Height;
    mTextureFormat = surfaceDesc.Format;

    mRenderTarget = NULL;
}

TextureStorage9_2D::TextureStorage9_2D(Renderer9 *renderer, GLenum internalformat, bool renderTarget, GLsizei width, GLsizei height, int levels)
    : TextureStorage9(renderer, GetTextureUsage(internalformat, renderTarget))
{
    mTexture = NULL;
    mRenderTarget = NULL;

    mInternalFormat = internalformat;

    const d3d9::TextureFormat &d3dFormatInfo = d3d9::GetTextureFormatInfo(internalformat);
    mTextureFormat = d3dFormatInfo.texFormat;

    d3d9::MakeValidSize(false, d3dFormatInfo.texFormat, &width, &height, &mTopLevel);
    mTextureWidth = width;
    mTextureHeight = height;
    mMipLevels = mTopLevel + levels;
}

TextureStorage9_2D::~TextureStorage9_2D()
{
    SafeRelease(mTexture);
    SafeDelete(mRenderTarget);
}

// Increments refcount on surface.
// caller must Release() the returned surface
gl::Error TextureStorage9_2D::getSurfaceLevel(GLenum target,
                                              int level,
                                              bool dirty,
                                              IDirect3DSurface9 **outSurface)
{
    ASSERT(target == GL_TEXTURE_2D);
    UNUSED_ASSERTION_VARIABLE(target);

    IDirect3DBaseTexture9 *baseTexture = NULL;
    gl::Error error = getBaseTexture(&baseTexture);
    if (error.isError())
    {
        return error;
    }

    IDirect3DTexture9 *texture = static_cast<IDirect3DTexture9*>(baseTexture);

    HRESULT result = texture->GetSurfaceLevel(level + mTopLevel, outSurface);

    ASSERT(SUCCEEDED(result));
    if (FAILED(result))
    {
        return gl::Error(GL_OUT_OF_MEMORY, "Failed to get the surface from a texture, result: 0x%X.", result);
    }

    // With managed textures the driver needs to be informed of updates to the lower mipmap levels
    if (level + mTopLevel != 0 && isManaged() && dirty)
    {
        texture->AddDirtyRect(NULL);
    }

    return gl::Error(GL_NO_ERROR);
}

gl::Error TextureStorage9_2D::getRenderTarget(const gl::ImageIndex &index, RenderTargetD3D **outRT)
{
    ASSERT(index.mipIndex == 0);

    if (!mRenderTarget && isRenderTarget())
    {
        IDirect3DSurface9 *surface = NULL;
        gl::Error error = getSurfaceLevel(GL_TEXTURE_2D, index.mipIndex, false, &surface);
        if (error.isError())
        {
            return error;
        }

        mRenderTarget =
            new TextureRenderTarget9(surface, mInternalFormat, static_cast<GLsizei>(mTextureWidth),
                                     static_cast<GLsizei>(mTextureHeight), 1, 0);
    }

    ASSERT(outRT);
    *outRT = mRenderTarget;
    return gl::Error(GL_NO_ERROR);
}

gl::Error TextureStorage9_2D::generateMipmap(const gl::ImageIndex &sourceIndex, const gl::ImageIndex &destIndex)
{
    IDirect3DSurface9 *upper = NULL;
    gl::Error error = getSurfaceLevel(GL_TEXTURE_2D, sourceIndex.mipIndex, false, &upper);
    if (error.isError())
    {
        return error;
    }

    IDirect3DSurface9 *lower = NULL;
    error = getSurfaceLevel(GL_TEXTURE_2D, destIndex.mipIndex, true, &lower);
    if (error.isError())
    {
        SafeRelease(upper);
        return error;
    }

    ASSERT(upper && lower);
    error = mRenderer->boxFilter(upper, lower);

    SafeRelease(upper);
    SafeRelease(lower);

    return error;
}

gl::Error TextureStorage9_2D::getBaseTexture(IDirect3DBaseTexture9 **outTexture)
{
    // if the width or height is not positive this should be treated as an incomplete texture
    // we handle that here by skipping the d3d texture creation
    if (mTexture == NULL && mTextureWidth > 0 && mTextureHeight > 0)
    {
        ASSERT(mMipLevels > 0);

        IDirect3DDevice9 *device = mRenderer->getDevice();
        HRESULT result = device->CreateTexture(static_cast<unsigned int>(mTextureWidth),
                                               static_cast<unsigned int>(mTextureHeight),
                                               static_cast<unsigned int>(mMipLevels), getUsage(),
                                               mTextureFormat, getPool(), &mTexture, NULL);

        if (FAILED(result))
        {
            ASSERT(result == D3DERR_OUTOFVIDEOMEMORY || result == E_OUTOFMEMORY);
            return gl::Error(GL_OUT_OF_MEMORY, "Failed to create 2D storage texture, result: 0x%X.", result);
        }
    }

    *outTexture = mTexture;
    return gl::Error(GL_NO_ERROR);
}

gl::Error TextureStorage9_2D::copyToStorage(TextureStorage *destStorage)
{
    ASSERT(destStorage);

    TextureStorage9_2D *dest9 = GetAs<TextureStorage9_2D>(destStorage);

    int levels = getLevelCount();
    for (int i = 0; i < levels; ++i)
    {
        IDirect3DSurface9 *srcSurf = NULL;
        gl::Error error = getSurfaceLevel(GL_TEXTURE_2D, i, false, &srcSurf);
        if (error.isError())
        {
            return error;
        }

        IDirect3DSurface9 *dstSurf = NULL;
        error = dest9->getSurfaceLevel(GL_TEXTURE_2D, i, true, &dstSurf);
        if (error.isError())
        {
            SafeRelease(srcSurf);
            return error;
        }

        error = mRenderer->copyToRenderTarget(dstSurf, srcSurf, isManaged());

        SafeRelease(srcSurf);
        SafeRelease(dstSurf);

        if (error.isError())
        {
            return error;
        }
    }

    return gl::Error(GL_NO_ERROR);
}

TextureStorage9_Cube::TextureStorage9_Cube(Renderer9 *renderer, GLenum internalformat, bool renderTarget, int size, int levels, bool hintLevelZeroOnly)
    : TextureStorage9(renderer, GetTextureUsage(internalformat, renderTarget))
{
    mTexture = NULL;
    for (int i = 0; i < CUBE_FACE_COUNT; ++i)
    {
        mRenderTarget[i] = NULL;
    }

    mInternalFormat = internalformat;

    const d3d9::TextureFormat &d3dFormatInfo = d3d9::GetTextureFormatInfo(internalformat);
    mTextureFormat = d3dFormatInfo.texFormat;

    int height = size;
    d3d9::MakeValidSize(false, d3dFormatInfo.texFormat, &size, &height, &mTopLevel);
    mTextureWidth = size;
    mTextureHeight = size;
    mMipLevels = mTopLevel + levels;
}

TextureStorage9_Cube::~TextureStorage9_Cube()
{
    SafeRelease(mTexture);

    for (int i = 0; i < CUBE_FACE_COUNT; ++i)
    {
        SafeDelete(mRenderTarget[i]);
    }
}

// Increments refcount on surface.
// caller must Release() the returned surface
gl::Error TextureStorage9_Cube::getSurfaceLevel(GLenum target,
                                                int level,
                                                bool dirty,
                                                IDirect3DSurface9 **outSurface)
{
    IDirect3DBaseTexture9 *baseTexture = NULL;
    gl::Error error = getBaseTexture(&baseTexture);
    if (error.isError())
    {
        return error;
    }

    IDirect3DCubeTexture9 *texture = static_cast<IDirect3DCubeTexture9*>(baseTexture);

    D3DCUBEMAP_FACES face = gl_d3d9::ConvertCubeFace(target);
    HRESULT result        = texture->GetCubeMapSurface(face, level, outSurface);

    ASSERT(SUCCEEDED(result));
    if (FAILED(result))
    {
        return gl::Error(GL_OUT_OF_MEMORY, "Failed to get the surface from a texture, result: 0x%X.", result);
    }

    // With managed textures the driver needs to be informed of updates to the lower mipmap levels
    if (level != 0 && isManaged() && dirty)
    {
        texture->AddDirtyRect(face, NULL);
    }

    return gl::Error(GL_NO_ERROR);
}

gl::Error TextureStorage9_Cube::getRenderTarget(const gl::ImageIndex &index, RenderTargetD3D **outRT)
{
    ASSERT(outRT);
    ASSERT(index.mipIndex == 0);
    ASSERT(index.layerIndex >= 0 && index.layerIndex < CUBE_FACE_COUNT);

    if (mRenderTarget[index.layerIndex] == NULL && isRenderTarget())
    {
        IDirect3DSurface9 *surface = NULL;
        gl::Error error = getSurfaceLevel(GL_TEXTURE_CUBE_MAP_POSITIVE_X + index.layerIndex,
                                          mTopLevel + index.mipIndex, false, &surface);
        if (error.isError())
        {
            return error;
        }

        mRenderTarget[index.layerIndex] =
            new TextureRenderTarget9(surface, mInternalFormat, static_cast<GLsizei>(mTextureWidth),
                                     static_cast<GLsizei>(mTextureHeight), 1, 0);
    }

    *outRT = mRenderTarget[index.layerIndex];
    return gl::Error(GL_NO_ERROR);
}

gl::Error TextureStorage9_Cube::generateMipmap(const gl::ImageIndex &sourceIndex, const gl::ImageIndex &destIndex)
{
    IDirect3DSurface9 *upper = NULL;
    gl::Error error = getSurfaceLevel(sourceIndex.type, sourceIndex.mipIndex, false, &upper);
    if (error.isError())
    {
        return error;
    }

    IDirect3DSurface9 *lower = NULL;
    error = getSurfaceLevel(destIndex.type, destIndex.mipIndex, true, &lower);
    if (error.isError())
    {
        SafeRelease(upper);
        return error;
    }

    ASSERT(upper && lower);
    error = mRenderer->boxFilter(upper, lower);

    SafeRelease(upper);
    SafeRelease(lower);

    return error;
}

gl::Error TextureStorage9_Cube::getBaseTexture(IDirect3DBaseTexture9 **outTexture)
{
    // if the size is not positive this should be treated as an incomplete texture
    // we handle that here by skipping the d3d texture creation
    if (mTexture == NULL && mTextureWidth > 0 && mTextureHeight > 0)
    {
        ASSERT(mMipLevels > 0);
        ASSERT(mTextureWidth == mTextureHeight);

        IDirect3DDevice9 *device = mRenderer->getDevice();
        HRESULT result = device->CreateCubeTexture(
            static_cast<unsigned int>(mTextureWidth), static_cast<unsigned int>(mMipLevels),
            getUsage(), mTextureFormat, getPool(), &mTexture, NULL);

        if (FAILED(result))
        {
            ASSERT(result == D3DERR_OUTOFVIDEOMEMORY || result == E_OUTOFMEMORY);
            return gl::Error(GL_OUT_OF_MEMORY, "Failed to create cube storage texture, result: 0x%X.", result);
        }
    }

    *outTexture = mTexture;
    return gl::Error(GL_NO_ERROR);
}

gl::Error TextureStorage9_Cube::copyToStorage(TextureStorage *destStorage)
{
    ASSERT(destStorage);

    TextureStorage9_Cube *dest9 = GetAs<TextureStorage9_Cube>(destStorage);

    int levels = getLevelCount();
    for (int f = 0; f < CUBE_FACE_COUNT; f++)
    {
        for (int i = 0; i < levels; i++)
        {
            IDirect3DSurface9 *srcSurf = NULL;
            gl::Error error =
                getSurfaceLevel(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, i, false, &srcSurf);
            if (error.isError())
            {
                return error;
            }

            IDirect3DSurface9 *dstSurf = NULL;
            error = dest9->getSurfaceLevel(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, i, true, &dstSurf);
            if (error.isError())
            {
                SafeRelease(srcSurf);
                return error;
            }

            error = mRenderer->copyToRenderTarget(dstSurf, srcSurf, isManaged());

            SafeRelease(srcSurf);
            SafeRelease(dstSurf);

            if (error.isError())
            {
                return error;
            }
        }
    }

    return gl::Error(GL_NO_ERROR);
}

}
