/****************************************************************
 * Copyright (c) 2020-2022 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 ****************************************************************/
#include <GLES3/gl3.h>
#include <cassert>

#include "../../../ThirdParty/tinyobjloader/tiny_obj_loader.h"
#include <vector>
#include <string>

#include "Geometry.h"
#include "Shader.h"
#include "../../Log/LogUtils.h"

namespace QtiGL
{

    Geometry::Geometry()
        : mVbId(0)
        , mIbId(0)
        , mVaoId(0)
        , mVertexCount(0)
        , mIndexCount(0)
        , mMatIndex(UINT_MAX)
    {

    }

    void Geometry::Initialize(ProgramAttribute const* pAttribs, int32_t const nAttribs,
                              uint32_t const* pIndices, int32_t const nIndices,
                              void const* pVertexData, int32_t const bufferSize, int32_t const nVertices)
    {
        //Create the VBO
        glGenBuffers( 1, &mVbId);
        if (mVbId == 0) return;  // GL context not available
        glBindBuffer( GL_ARRAY_BUFFER, mVbId );
        glBufferData( GL_ARRAY_BUFFER, bufferSize, pVertexData, GL_STATIC_DRAW);
        glBindBuffer( GL_ARRAY_BUFFER, 0);

        //Create the Index Buffer
        glGenBuffers( 1, &mIbId);
        if (mIbId == 0) return;
        glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, mIbId );
        glBufferData( GL_ELEMENT_ARRAY_BUFFER, nIndices * sizeof(uint32_t), pIndices, GL_STATIC_DRAW);
        glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0);

        //Create the VAO
        glGenVertexArrays( 1, &mVaoId );
        if (mVaoId == 0) return;

        glBindVertexArray( mVaoId );

        glBindBuffer( GL_ARRAY_BUFFER, mVbId );

        for ( int32_t i = 0; i < nAttribs; i++ )
        {
            glEnableVertexAttribArray( pAttribs[i].index );
            glVertexAttribPointer(pAttribs[i].index, pAttribs[i].size,
                pAttribs[i].type, pAttribs[i].normalized,
                pAttribs[i].stride, (void*)(uint64_t)(pAttribs[i].offset));
        }

        glBindBuffer(GL_ARRAY_BUFFER, 0);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mIbId);

        glBindVertexArray( 0 );

        mVertexCount = nVertices;
        mIndexCount = nIndices;
    }

    void Geometry::Update(void const* pVertexData, int32_t const bufferSize, int32_t const nVertices)
    {
        glBindBuffer(GL_ARRAY_BUFFER, mVbId);
        glBufferData(GL_ARRAY_BUFFER, bufferSize, pVertexData, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void Geometry::Update(
            void const* pVertexData, int32_t const bufferSize, int32_t const nVertices,
            uint32_t const* pIndices, int32_t const nIndices)
    {
        Update(pVertexData, bufferSize, nVertices);
        glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, mIbId );
        glBufferData( GL_ELEMENT_ARRAY_BUFFER, nIndices * sizeof(uint32_t), pIndices, GL_STATIC_DRAW);
        glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    void Geometry::Destroy()
    {
        if (mIbId > 0)
            glDeleteBuffers( 1, &mIbId );
        if (mVbId > 0)
            glDeleteBuffers( 1, &mVbId );

        mVbId = 0;
        mIbId = 0;
        mVaoId = 0;
        mVertexCount = 0;
        mIndexCount = 0;
    }

    void Geometry::Submit()
    {
        glBindVertexArray( mVaoId );
        glDrawElements(GL_TRIANGLES, mIndexCount, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray( 0 );
    }

    void Geometry::Submit(ProgramAttribute const* pAttribs, int32_t const nAttribs)
    {
        glBindBuffer(GL_ARRAY_BUFFER, mVbId);

        for (int32_t i = 0; i < nAttribs; i++)
        {
            glEnableVertexAttribArray(pAttribs[i].index);
            glVertexAttribPointer(pAttribs[i].index, pAttribs[i].size,
                pAttribs[i].type, pAttribs[i].normalized,
                pAttribs[i].stride, (void*)(uint64_t)(pAttribs[i].offset));
        }

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mIbId);

        glDrawElements(GL_TRIANGLES, mIndexCount, GL_UNSIGNED_INT, nullptr);


        for (int32_t i = 0; i < nAttribs; i++)
        {
            glDisableVertexAttribArray(pAttribs[i].index);
        }

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    void Geometry::SubmitInstanced(uint32_t instanceCount)
    {
        glBindVertexArray( mVaoId );
        glDrawElementsInstanced(GL_TRIANGLES, mIndexCount, GL_UNSIGNED_INT, nullptr, instanceCount);
        glBindVertexArray( 0 );
    }

    bool Geometry::CreateFromObjFile(std::string const& objFilePath, Geometry** pOutGeometry, int32_t& outNumGeometry,
            bool normalize, std::vector<std::string>* outDiffusePaths)
    {
        std::vector<tinyobj::shape_t>       shapes;
        std::vector<tinyobj::material_t>    materials;
        std::string err;

        std::string materialPath = objFilePath.substr(0, objFilePath.find_last_of('/')) + "/";
        bool ret = tinyobj::LoadObj(shapes, materials, err, objFilePath.c_str(), materialPath.c_str());
        if (!ret)
        {
            LOGE("CreateFromObjFile", "%s", err.c_str());
            *pOutGeometry = nullptr;
            outNumGeometry = 0;
            return false;
        }

        ProgramAttribute attribs[3];

        *pOutGeometry = new Geometry[(int32_t)shapes.size()];
        LOGD("CreateFromObjFile", "Found %d shapes", (int32_t)shapes.size());
        LOGD("CreateFromObjFile", "Found %d materials", (int32_t)materials.size());

        if (outDiffusePaths)
        {
            for (auto& mat : materials)
                outDiffusePaths->push_back(materialPath + mat.diffuse_texname);
        }

        glm::mat4 normalizeMatrix(1.0f);

        if (normalize)
        {
            glm::vec3 max(std::numeric_limits<float>::min());
            glm::vec3 min(std::numeric_limits<float>::max());

            for (size_t i = 0; i < shapes.size(); i++)
            {
                for (size_t j = 0; j < shapes[i].mesh.positions.size() / 3; j++)
                {
                    max.x = glm::max(max.x, shapes[i].mesh.positions[3 * j + 0]);
                    max.y = glm::max(max.y, shapes[i].mesh.positions[3 * j + 1]);
                    max.z = glm::max(max.z, shapes[i].mesh.positions[3 * j + 2]);
                    min.x = glm::min(min.x, shapes[i].mesh.positions[3 * j + 0]);
                    min.y = glm::min(min.y, shapes[i].mesh.positions[3 * j + 1]);
                    min.z = glm::min(min.z, shapes[i].mesh.positions[3 * j + 2]);
                }
            }

            glm::vec3 scale = max - min;
            float dim = glm::max(scale.x, glm::max(scale.y, scale.z));
            glm::vec3 center = (max + min) / 2.0f;

            normalizeMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / dim)) * glm::translate(glm::mat4(1.0f), -center);
        }

        for (size_t i = 0; i < shapes.size(); i++)
        {
            if (!materials.empty())
                (*pOutGeometry)[i].mMatIndex = shapes[i].mesh.material_ids[0];

            int vertexSize = 0;
            int nAttribs = 0;

            if (shapes[i].mesh.positions.size() == 0 ||
                shapes[i].mesh.normals.size() == 0)
            {
                LOGE("CreateFromObjFile", "OBJ must contain at least positions and normals");
                *pOutGeometry = nullptr;
                outNumGeometry = 0;
                return false;
            }

            nAttribs = 3;
            vertexSize = 8 * sizeof(float);

            attribs[0].index = kPosition;
            attribs[0].size = 3;
            attribs[0].type = GL_FLOAT;
            attribs[0].normalized = false;
            attribs[0].stride = vertexSize;
            attribs[0].offset = 0;

            attribs[1].index = kNormal;
            attribs[1].size = 3;
            attribs[1].type = GL_FLOAT;
            attribs[1].normalized = false;
            attribs[1].stride = vertexSize;
            attribs[1].offset = 3 * sizeof(float);

            attribs[2].index = kTexcoord0;
            attribs[2].size = 2;
            attribs[2].type = GL_FLOAT;
            attribs[2].normalized = false;
            attribs[2].stride = vertexSize;
            attribs[2].offset = 6 * sizeof(float);

            float* pVbData = new float[vertexSize * shapes[i].mesh.positions.size()];
            int32_t vbIndex = 0;

            for (size_t j = 0; j < shapes[i].mesh.positions.size() / 3; j++)
            {
                glm::vec4 pos(shapes[i].mesh.positions[3 * j + 0], shapes[i].mesh.positions[3 * j + 1], shapes[i].mesh.positions[3 * j + 2], 1.0f);
                pos = normalizeMatrix * pos;

                pVbData[vbIndex++] = pos.x;
                pVbData[vbIndex++] = pos.y;
                pVbData[vbIndex++] = pos.z;

                pVbData[vbIndex++] = shapes[i].mesh.normals[3 * j + 0];
                pVbData[vbIndex++] = shapes[i].mesh.normals[3 * j + 1];
                pVbData[vbIndex++] = shapes[i].mesh.normals[3 * j + 2];

                if (!shapes[i].mesh.texcoords.empty())
                {
                    pVbData[vbIndex++] = shapes[i].mesh.texcoords[2 * j + 0];
                    pVbData[vbIndex++] = shapes[i].mesh.texcoords[2 * j + 1];
                }
                else
                {
                    pVbData[vbIndex++] = 0.0f;
                    pVbData[vbIndex++] = 0.0f;
                }
            }

            (*pOutGeometry)[i].Initialize(&attribs[0], nAttribs,
                &shapes[i].mesh.indices[0], shapes[i].mesh.indices.size(),
                (const void*)pVbData, vertexSize * shapes[i].mesh.positions.size(), shapes[i].mesh.positions.size());

            LOGD("CreateFromObjFile", "OBJ Geom Initialized, idx count:%d, pos count:%d", (int32_t)shapes[i].mesh.indices.size(), (int32_t)shapes[i].mesh.positions.size());
            delete[] pVbData;
        }

        outNumGeometry = shapes.size();

        return true;
    }

}
