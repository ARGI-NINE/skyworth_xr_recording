/****************************************************************
 * Copyright (c) 2020-2022 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 ****************************************************************/
#pragma once

#include <cstdint>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "../../DataStructures/cpp/HashTable.hpp"

#define MAX_UNIFORM_NAME_LENGTH 64

namespace QtiGL
{
    enum AttributeLocation
    {
        kPosition = 0,
        kNormal = 1,
        kColor = 2,
        kTexcoord0 = 3,
        kTexcoord1 = 4
    } ;

    struct Attribute
    {
        AttributeLocation    location;
        char const*          name;
    };

    struct Uniform
    {
        uint32_t     location;
        GLenum       type;
        char         name[MAX_UNIFORM_NAME_LENGTH];
        uint32_t     textureUnit;
    };

    class Shader
    {
    public:
        Shader();

        bool Initialize(int32_t const numVertStrings, char const** pVertSrc, int32_t const numFragStrings, char const** pFragSrc, char const* pVertDbgName = nullptr, char const* pFragDbgName = nullptr);
        void Destroy();
        void Bind();
        void Unbind();

        void SetUniformMat2(char const* name, glm::mat2& matrix);
        void SetUniformMat2(int32_t const location, glm::mat2& matrix);
        void SetUniformMat2fv(char const* name, uint32_t const count, float *pData);
        void SetUniformMat2fv(int32_t const location, uint32_t const count, float *pData);
        void SetUniformMat3(char const* name, glm::mat3& matrix);
        void SetUniformMat3(int32_t location, glm::mat3& matrix);
        void SetUniformMat4(char const* name, glm::mat4& matrix);
        void SetUniformMat4(int32_t location, glm::mat4& matrix);
        void SetUniformMat4fv(char const* name, uint32_t const count, float *pData);
        void SetUniformMat4fv(int32_t location, uint32_t const count, float *pData);
        void SetUniformVec4(char const* name, glm::vec4& vector);
        void SetUniformVec4(int32_t const location, glm::vec4& vector);
        void SetUniformVec4fv(char const* name, uint32_t const count, float* pData);
        void SetUniformVec4fv(int32_t const location, uint32_t const count, float* pData);
        void SetUniformVec3(char const* name, glm::vec3& vector);
        void SetUniformVec3(int32_t const location, glm::vec3& vector);
        void SetUniformVec2(char const* name, glm::vec2& vector);
        void SetUniformVec2(int32_t const location, glm::vec2& vector);
        void SetUniform1ui(char const* name, uint32_t const value);
        void SetUniform1ui(int32_t const location, uint32_t const value);
        void SetUniform1f(char const* name, float const value);
        void SetUniform1f(int32_t const location, float const value);
        void SetUniform1i(const char* name, int32_t const value);
        void SetUniform1i(int32_t const location, int32_t const value);
        void SetUniform1fv(const char* name, uint32_t const  count, float *pData);
        void SetUniform1fv(int32_t const location, uint32_t const count, float *pData);
        void SetUniformSampler(char const* name, uint32_t const samplerId, uint32_t const samplerType, GLuint const samplerObjId);

        uint32_t GetShaderId() { return mShaderId; }

    private:
        static uint32_t gCurrentBoundShader;

        typedef QtiDataStructure::HashTable<unsigned int, Uniform, QtiDataStructure::DjB2Hash> UniformMap;
        //typedef HashTable<unsigned int, unsigned int, DjB2Hash> SamplerMap;

        uint32_t    mRefCount;

        uint32_t    mShaderId;
        uint32_t    mVsId;
        uint32_t    mFsId;
        UniformMap  mUniformMap;
    };
}
