/*
-----------------------------------------------------------------------------
Copyright (c) 2020 - 2022 Qualcomm Technologies, Inc.
All Rights Reserved.
Confidential and Proprietary - Qualcomm Technologies, Inc.
-----------------------------------------------------------------------------
*/
#include <cstring>  // For memcpy

#include "KtxLoader.h"
#include "../../Log/LogUtils.h"

#if !defined( GL_TEXTURE_PROTECTED_EXT )
#define GL_TEXTURE_PROTECTED_EXT    0x8BFA
#endif 

#ifndef MAX
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#endif

namespace QtiGL
{

// L_SwapEndian16: Swaps endianness in an array of 16-bit values
//-----------------------------------------------------------------------------
static void L_SwapEndian16(uint16* pData16, uint32 nCount)
//-----------------------------------------------------------------------------
{
    for (uint32 i=0; i<nCount; ++i)
    {
        uint16 x = *pData16;
        *pData16++ = (x << 8) | (x >> 8);
    }
}

// L_SwapEndian32: Swaps endianness in an array of 32-bit values
//-----------------------------------------------------------------------------
static void L_SwapEndian32(uint32* pData32, uint32 nCount)
//-----------------------------------------------------------------------------
{
    for (uint32 i=0; i<nCount; ++i)
    {
        uint32 x = *pData32;
        *pData32++ = (x << 24) | ((x & 0xFF00) << 8) | ((x & 0xFF0000) >> 8) | (x >> 24);
    }
}

//-----------------------------------------------------------------------------
KtxTexture::KtxTexture()
//-----------------------------------------------------------------------------
{
    m_pData                 = NULL;
    m_nDataSize             = 0;
    m_nStreamBufferIndex    = 0;
}

//-----------------------------------------------------------------------------
KtxTexture::~KtxTexture()
//-----------------------------------------------------------------------------
{
    if(m_pData != NULL)
        delete [] m_pData;
    m_pData = NULL;
}

//-----------------------------------------------------------------------------
uint32 KtxTexture::StreamRead(void* m_pData, unsigned int nSize)
//-----------------------------------------------------------------------------
{
    if (m_nStreamBufferIndex + nSize > m_nStreamBufferSize)
    {
        LOGE("KtxTexture::StreamRead", "    Stream has %d bytes.  Read index is at %d.  Trying to read %d bytes. Failed!!", m_nStreamBufferSize, m_nStreamBufferIndex, nSize);
        return 0;
    }

    memcpy(m_pData, &m_pStreamBuffer[m_nStreamBufferIndex], nSize);
    m_nStreamBufferIndex += nSize;

    return nSize;
}

//-----------------------------------------------------------------------------
uint32 KtxTexture::StreamSkip(unsigned int nSize)
//-----------------------------------------------------------------------------
{
    if (m_nStreamBufferIndex + nSize > m_nStreamBufferSize)
    {
        LOGE("KtxTexture::StreamSkip", "    Stream has %d bytes.  Read index is at %d.  Trying to skip %d bytes. Failed!!", m_nStreamBufferSize, m_nStreamBufferIndex, nSize);
        return 0;
    }

    m_nStreamBufferIndex += nSize;

    return nSize;
}

//-----------------------------------------------------------------------------
TKTXErrorCode KtxTexture::ParseHeader(TKTXHeader* pHeader, TKTXTextureInfo* pTextureInfo)
//-----------------------------------------------------------------------------
{
    uint8     identifierReference[12] = KTX_IDENTIFIER_REF;
    uint32    nMaxMipDimension;

    LOGI("KtxTexture::ParseHeader", "    Reading KTX header...");
    if (StreamRead(pHeader, KTX_HEADER_SIZE) != KTX_HEADER_SIZE)
    {
        LOGE("KtxTexture::ParseHeader", "    Could not read %d bytes of KTX header", KTX_HEADER_SIZE);
        return KTX_HEADER_ERROR;
    }

    // Compare identifier, is this a KTX file? 
    if (memcmp(pHeader->identifier, identifierReference, 12) != 0)
    {
        LOGE("KtxTexture::ParseHeader", "    KTX header has incorrect identifier");
        return KTX_HEADER_ERROR;
    }

    if (pHeader->endianness == KTX_ENDIAN_REF_REV)
    {
        // Convert endianness of header fields if necessary 
        L_SwapEndian32(&pHeader->glType, 12);

        if (pHeader->glTypeSize != 1 &&
            pHeader->glTypeSize != 2 &&
            pHeader->glTypeSize != 4)
        {
            // Only 8, 16, and 32-bit types supported so far 
            LOGE("KtxTexture::ParseHeader", "    KTX file is NOT 8, 16, or 32-bit");
            return KTX_INVALID_VALUE;
        }
    }
    else if (pHeader->endianness != KTX_ENDIAN_REF)
    {
        LOGE("KtxTexture::ParseHeader", "    KTX is wrong endian");
        return KTX_INVALID_VALUE;
    }

    // Check glType and glFormat 
    pTextureInfo->bCompressed = 0;
    if (pHeader->glType == 0 || pHeader->glFormat == 0)
    {
        if (pHeader->glType + pHeader->glFormat != 0)
        {
            // either both or none of glType, glFormat must be zero 
            LOGE("KtxTexture::ParseHeader", "    KTX header has imcompatible glType/glFormat values");
            return KTX_INVALID_VALUE;
        }

        LOGI("KtxTexture::ParseHeader", "    KTX file is compressed");
        pTextureInfo->bCompressed = 1;
    }

    // Check texture dimensions. KTX files can store 8 types of textures:
    // 1D, 2D, 3D, cube, and array variants of these. There is currently
    // no GL extension that would accept 3D array or cube array textures. 
    if ((pHeader->pixelWidth == 0) ||
        (pHeader->pixelDepth > 0 && pHeader->pixelHeight == 0))
    {
        // texture must have width 
        // texture must have height if it has depth
        LOGE("KtxTexture::ParseHeader", "    KTX header has invalid sizes: Width = %d; Height = %d; Depth = %d", pHeader->pixelWidth, pHeader->pixelHeight, pHeader->pixelDepth);
        return KTX_INVALID_VALUE;
    }

    m_nWidth = pHeader->pixelWidth;
    m_nHeight = pHeader->pixelHeight;

    pTextureInfo->nTextureDimensions = 1;
    pTextureInfo->glTarget = GL_TEXTURE_1D;
    if (pHeader->pixelHeight > 0)
    {
        pTextureInfo->nTextureDimensions = 2;
        pTextureInfo->glTarget = GL_TEXTURE_2D;
    }
    if (pHeader->pixelDepth > 0)
    {
        pTextureInfo->nTextureDimensions = 3;
        pTextureInfo->glTarget = GL_TEXTURE_3D;
    }

    if (pHeader->numberOfFaces == 6)
    {
        if (pTextureInfo->nTextureDimensions != 2)
        {
            // cube map needs 2D faces 
            return KTX_INVALID_VALUE;
        }

        LOGI("KtxTexture::ParseHeader", "    KTX file is a cube map");
        pTextureInfo->glTarget = GL_TEXTURE_CUBE_MAP;
    }
    else if (pHeader->numberOfFaces != 1)
    {
        // numberOfFaces must be either 1 or 6 
        LOGI("KtxTexture::ParseHeader", "    KTX has invalid number of faces: %d", pHeader->numberOfFaces);
        return KTX_INVALID_VALUE;
    }

    // load as 2D texture if 1D textures are not supported 
    if (pTextureInfo->nTextureDimensions == 1)
    {
        pTextureInfo->nTextureDimensions = 2;
        pTextureInfo->glTarget = GL_TEXTURE_2D;
        pHeader->pixelHeight = 1;
    }

    if (pHeader->numberOfArrayElements > 0)
    {
        if (pTextureInfo->glTarget == GL_TEXTURE_1D)
        {
            pTextureInfo->glTarget = GL_TEXTURE_1D_ARRAY_EXT;
        }
        else if (pTextureInfo->glTarget == GL_TEXTURE_2D)
        {
            pTextureInfo->glTarget = GL_TEXTURE_2D_ARRAY_EXT;
        }
        else
        {
            // No API for 3D and cube arrays yet 
            LOGE("KtxTexture::ParseHeader", "    KTX is either 3D or cube array.  No support yet.");
            return KTX_UNSUPPORTED_TEXTURE_TYPE;
        }

        pTextureInfo->nTextureDimensions++;
    }

    // reject 3D texture if unsupported 
    if (pTextureInfo->nTextureDimensions == 3)
    {
        LOGE("KtxTexture::ParseHeader", "    KTX is 3D texture.  No support yet.");
        return KTX_UNSUPPORTED_TEXTURE_TYPE;
    }

    // Check number of mipmap levels 
    if (pHeader->numberOfMipmapLevels == 0)
    {
        pHeader->numberOfMipmapLevels = 1;
    }
    nMaxMipDimension = MAX(MAX(pHeader->pixelWidth, pHeader->pixelHeight), pHeader->pixelDepth);
    if (nMaxMipDimension < ((uint32)1 << (pHeader->numberOfMipmapLevels - 1)))
    {
        // Can't have more mip levels than 1 + log2(max(width, height, depth)) 
        LOGE("KtxTexture::ParseHeader", "    KTX has more mip levels that expected (%d): %d", nMaxMipDimension, ((uint32)1 << (pHeader->numberOfMipmapLevels - 1)));
        return KTX_INVALID_VALUE;
    }

    // skip key/value metadata 
    LOGI("KtxTexture::ParseHeader", "    KTX has %d bytes of key value. Skipping...", pHeader->bytesOfKeyValueData);
    if (pHeader->bytesOfKeyValueData != 0 && StreamSkip(pHeader->bytesOfKeyValueData) == 0)
    {
        LOGE("KtxTexture::ParseHeader", "    KTX header could not skip over key value data");
        return KTX_UNEXPECTED_END_OF_STREAM;
    }

    LOGI("KtxTexture::ParseHeader", "    KTX header parsed");
    return KTX_SUCCESS;
}

//-----------------------------------------------------------------------------
TKTXErrorCode KtxTexture::ParseBuffer(void* pBuffer, unsigned int nBufferSize, GLuint* pTexture, GLenum* pTarget, TKTXHeader* pOutHeader, bool isProtected)
//-----------------------------------------------------------------------------
{
    TKTXHeader      tHeader;
    TKTXTextureInfo textureInfo;
    GLuint          nTextureId;

    // Clear out any GL Errors
    GLenum error = GL_INVALID_ENUM;
    while(error != GL_NO_ERROR)
        error = glGetError();

    if(!pOutHeader) pOutHeader = &tHeader;

    m_pStreamBuffer     = (unsigned char*)pBuffer;
    m_nStreamBufferSize = nBufferSize;
    m_nStreamBufferIndex = 0;

    LOGI("KtxTexture::ParseBuffer", "    KTX Stream initialization: %d bytes.", m_nStreamBufferSize);


    if (pTarget == NULL || pTexture == NULL)
    {
        return KTX_INVALID_VALUE;
    }

    TKTXErrorCode nErrorCode = ParseHeader(pOutHeader, &textureInfo);
    if (nErrorCode != KTX_SUCCESS)
    {
        LOGI("KtxTexture::ParseBuffer", "    Texture header parsing failed: %d", nErrorCode);
        return nErrorCode;
    }

    LOGI("KtxTexture::ParseBuffer", "    Texture is (%d x %d)", pOutHeader->pixelWidth, pOutHeader->pixelHeight);

    if (*pTexture)
    {
        nTextureId = *pTexture;
    }
    else
    {
        glGenTextures(1, &nTextureId);
    }
    
    glBindTexture(textureInfo.glTarget, nTextureId);
    if (isProtected) 
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_PROTECTED_EXT, GL_TRUE);
        glTexStorage2D(textureInfo.glTarget, pOutHeader->numberOfMipmapLevels, GL_RGBA8, pOutHeader->pixelWidth,
                       pOutHeader->pixelHeight);
    }
    
    if (textureInfo.glTarget == GL_TEXTURE_CUBE_MAP)
    {
        textureInfo.glTarget = GL_TEXTURE_CUBE_MAP_POSITIVE_X;
    }

    LOGI("KtxTexture::ParseBuffer", "    Texture has %d mip levels", pOutHeader->numberOfMipmapLevels);
    for (uint32 nMipLevel = 0; nMipLevel < pOutHeader->numberOfMipmapLevels; nMipLevel++)
    {
        uint32  nFaceLodSize;
        uint32  nFaceLodSizeRounded;

        GLsizei nWidth  = MAX(1, pOutHeader->pixelWidth  >> nMipLevel);
        GLsizei nHeight = MAX(1, pOutHeader->pixelHeight >> nMipLevel);
        GLsizei nDepth  = MAX(1, pOutHeader->pixelDepth  >> nMipLevel);
        
        LOGI("KtxTexture::ParseBuffer", "       Mip %d: (%d x %d)", nMipLevel, nWidth, nHeight);

        if (StreamRead(&nFaceLodSize, sizeof(uint32)) != sizeof(uint32))
        {
            return KTX_UNEXPECTED_END_OF_STREAM;
        }
        if (pOutHeader->endianness == KTX_ENDIAN_REF_REV)
        {
            L_SwapEndian32(&nFaceLodSize, 1);
        }
        nFaceLodSizeRounded = (nFaceLodSize + 3) & ~(uint32)3;
        if (!m_pData)
        {
            // allocate memory sufficient for the first nMipLevel 
            m_pData = new uint8[nFaceLodSizeRounded];
            m_nDataSize = nFaceLodSizeRounded;
        }
        else if (m_nDataSize < nFaceLodSizeRounded)
        {
            // subsequent levels cannot be larger than the first nMipLevel 
            return KTX_INVALID_VALUE;
        }

        for (uint32 nFace=0; nFace<pOutHeader->numberOfFaces; nFace++)
        {
            if (StreamRead(m_pData, nFaceLodSizeRounded) != nFaceLodSizeRounded)
            {
                return KTX_UNEXPECTED_END_OF_STREAM;
            }

            // Perform endianness conversion on texture m_pData 
            if (pOutHeader->endianness == KTX_ENDIAN_REF_REV && pOutHeader->glTypeSize == 2)
            {
                L_SwapEndian16((uint16*)m_pData, nFaceLodSize / 2);
            }
            else if (pOutHeader->endianness == KTX_ENDIAN_REF_REV && pOutHeader->glTypeSize == 4)
            {
                L_SwapEndian32((uint32*)m_pData, nFaceLodSize / 4);
            }

            if (textureInfo.nTextureDimensions == 1)
            {
                if (textureInfo.bCompressed)
                {
                    //glCompressedTexImage1D not supported in OpenGLES 2.0
                    return KTX_UNSUPPORTED_TEXTURE_TYPE;
                }
                else
                {
                    //glTexImage1D not supported in OpenGLES 2.0
                    return KTX_UNSUPPORTED_TEXTURE_TYPE;
                }
            }
            else if (textureInfo.nTextureDimensions == 2)
            {
                if (pOutHeader->numberOfArrayElements)
                {
                    nHeight = pOutHeader->numberOfArrayElements;
                }
                if (textureInfo.bCompressed)
                {
                    if (isProtected) 
                    {
                        glCompressedTexSubImage2D(textureInfo.glTarget + nFace, nMipLevel,
                                        0, 0, nWidth, nHeight, pOutHeader->glFormat, 
                                        nFaceLodSize, m_pData);
                    } else 
                    {
                        glCompressedTexImage2D(textureInfo.glTarget + nFace, nMipLevel,
                                           pOutHeader->glInternalFormat, nWidth, nHeight, 0,
                                           nFaceLodSize, m_pData);
                    }
                }
                else
                {
                    if (isProtected) 
                    {
                        glTexSubImage2D(textureInfo.glTarget + nFace, nMipLevel,
                                        0, 0, nWidth, nHeight, pOutHeader->glFormat, 
                                        pOutHeader->glType, m_pData);


                    } else
                    {
                        glTexImage2D(textureInfo.glTarget + nFace, nMipLevel,
                                     pOutHeader->glBaseInternalFormat, nWidth, nHeight, 0,
                                     pOutHeader->glFormat, pOutHeader->glType, m_pData);
                    }
                }
            }
            else if (textureInfo.nTextureDimensions == 3)
            {
                if (pOutHeader->numberOfArrayElements)
                {
                    nDepth = pOutHeader->numberOfArrayElements;
                }
                if (textureInfo.bCompressed)
                {
                    //glCompressedTexImage3D not supported in OpenGLES 2.0
                    return KTX_UNSUPPORTED_TEXTURE_TYPE;
                }
                else
                {
                    //glTexImage3D not supported in OpenGLES 2.0
                    return KTX_UNSUPPORTED_TEXTURE_TYPE;
                }
            }

            GLenum error = glGetError();
            if (error != GL_NO_ERROR)
            {
                switch(error)
                {
                case GL_NO_ERROR:           // 0
                    LOGE("KtxTexture::ParseBuffer", "Error (GL_NO_ERROR) creating texture");
                    break;
                case GL_INVALID_ENUM:       // 0x0500
                    LOGE("KtxTexture::ParseBuffer", "Error (GL_INVALID_ENUM) creating texture");
                    break;
                case GL_INVALID_VALUE:      // 0x0501
                    LOGE("KtxTexture::ParseBuffer", "Error (GL_INVALID_VALUE) creating texture");
                    break;
                case GL_INVALID_OPERATION:  // 0x0502
                    LOGE("KtxTexture::ParseBuffer", "Error (GL_INVALID_OPERATION) creating texture");
                    break;
                //case GL_STACK_OVERFLOW:     // 0x0503
                //    LOGE("KtxTexture::ParseBuffer", "Error (GL_STACK_OVERFLOW) creating texture");
                //    break;
                //case GL_STACK_UNDERFLOW:    // 0x0504
                //    LOGE("KtxTexture::ParseBuffer", "Error (GL_STACK_UNDERFLOW) creating texture");
                //    break;
                case GL_OUT_OF_MEMORY:      // 0x0505
                    LOGE("KtxTexture::ParseBuffer", "Error (GL_OUT_OF_MEMORY) creating texture");
                    break;

                default:
                    LOGE("KtxTexture::ParseBuffer", "Error (0x%X) creating texture", error);
                    break;
                }
                return KTX_GL_ERROR;
            }
        }
    }

    *pTarget  = textureInfo.glTarget;
    *pTexture = nTextureId;

    return KTX_SUCCESS;
}

//-----------------------------------------------------------------------------
TKTXErrorCode KtxTexture::LoadKtxFromBuffer(void* pBuffer, unsigned int nBufferSize, GLuint* pTexture, GLenum* pTarget, TKTXHeader* pOutHeader, bool isProtected)
//-----------------------------------------------------------------------------
{
    GLint           nPreviousUnpackAlignment = 4;
    TKTXErrorCode   nErrorCode = KTX_SUCCESS;

    // KTX files require an unpack alignment of 4 
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &nPreviousUnpackAlignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    nErrorCode = ParseBuffer(pBuffer, nBufferSize, pTexture, pTarget, pOutHeader, isProtected);

    // restore previous GL state 
    glPixelStorei(GL_UNPACK_ALIGNMENT, nPreviousUnpackAlignment);

    return nErrorCode;
}

}

