/****************************************************************
 * Copyright (c) 2020-2022 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 ****************************************************************/
#pragma once

#include <string>
#include <cstdint>
#include <vector>

namespace QtiGL
{
    struct ProgramAttribute
    {
        uint32_t    index;
        int32_t     size;
        uint32_t    type;
        bool        normalized;
        int32_t     stride;
        int32_t     offset;
    };

    class Geometry
    {
    public:
        Geometry();

        void Initialize(ProgramAttribute const* pAttribs, int32_t const nAttribs,
                        uint32_t const* pIndices, int32_t const nIndices,
                        void const* pVertexData, int32_t const bufferSize, int32_t const nVertices);

        void Update(void const* pVertexData, int32_t const bufferSize, int32_t const nVertices);
        void Update(
                void const* pVertexData, int32_t const bufferSize, int32_t const nVertices,
                uint32_t const* pIndices, int32_t const nIndices);

        void Destroy();
        void Submit();
        void Submit(ProgramAttribute const* pAttribs, int32_t const nAttribs);
        void SubmitInstanced(uint32_t instanceCount);

        static bool CreateFromObjFile(std::string const& objFilePath, Geometry** pOutGeometry, int32_t& outNumGeometry,
                bool normalize = false, std::vector<std::string>* outDiffusePaths = nullptr);

        uint32_t GetVbId() { return mVbId; }
        uint32_t GetIbId() { return mIbId; }
        uint32_t GetVaoId() { return mVaoId; }
        int32_t GetVertexCount() { return mVertexCount; }
        int32_t GetIndexCount() { return mIndexCount; }
        uint32_t GetMatIndex() { return mMatIndex; }

    private:
        uint32_t    mVbId;
        uint32_t    mIbId;
        uint32_t    mVaoId;
        int32_t     mVertexCount;
        int32_t     mIndexCount;
        uint32_t    mMatIndex;
    };

}
