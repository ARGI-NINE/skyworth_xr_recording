/****************************************************************
 * Copyright (c) 2020-2022 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 ****************************************************************/
#include <unistd.h>     // Need gettid()
#include <sys/syscall.h>
#include "../../Log/LogUtils.h"
#include "Extensions.h"
#include "Shader.h"

// Controls the (extremely verbose) debug messages coming from this shader class,
// which are probably not useful unless you are actively debugging something with shaders
#if !defined(NDEBUG) && 1
#define SHADER_LOGI(...) LOGI(__VA_ARGS__)
#define SHADER_LOGD(...) LOGD(__VA_ARGS__)
#else
#define SHADER_LOGI(...) ((void)0)
#define SHADER_LOGD(...) ((void)0)
#endif

namespace QtiGL
{
    static Attribute gDefaultAttributes[] =
    {
        { kPosition, "position"},
        { kNormal, "normal"},
        { kColor, "color"},
        { kTexcoord0, "texcoord0"},
        { kTexcoord1, "texcoord1"}
    };

    bool IsSamplerType(uint32_t const type)
    {
        if( type == GL_SAMPLER_2D ||
            type == GL_SAMPLER_2D_ARRAY ||
            type == GL_SAMPLER_EXTERNAL_OES ||
            type == GL_SAMPLER_3D ||
            type == GL_SAMPLER_CUBE )
        {
            return true;
        }
        return false;
    }

    uint32_t Shader::gCurrentBoundShader = 0;


    Shader::Shader()
        : mShaderId(0)
        , mVsId(0)
        , mFsId(0)
    {
        mRefCount = 0;
        mUniformMap.Init(32);
    }

    bool Shader::Initialize(int32_t const numVertStrings, char const** pVertSrc, int32_t const numFragStrings, char const** pFragSrc, char const* pVertDbgName, char const* pFragDbgName)
    {
        static char errMsg[4096];
        int32_t result;

        if (pVertDbgName && pFragDbgName)
            LOGD("Shader::Initialize", "Loading Shader: %s <=> %s", pVertDbgName, pFragDbgName);
        else
            LOGD("Shader::Initialize", "Loading Shader: Unknown <=> Unknown");

        mVsId = glCreateShader( GL_VERTEX_SHADER );
        if (0 == mVsId)
            return false;
        glShaderSource(mVsId, numVertStrings, pVertSrc, 0);
        glCompileShader( mVsId );
        glGetShaderiv( mVsId, GL_COMPILE_STATUS, &result );
        if ( result == GL_FALSE )
        {
            errMsg[0] = 0;
            glGetShaderInfoLog( mVsId, sizeof(errMsg), 0, errMsg);
            if (pVertDbgName)
                LOGE("Shader::Initialize", "%s : %s\n", pVertDbgName, errMsg);
            else
                LOGE("Shader::Initialize", "Compile Error : %s\n", errMsg);
            return false;
        }

        mFsId = glCreateShader( GL_FRAGMENT_SHADER );
        if (0 == mFsId)
            return false;
        glShaderSource(mFsId, numFragStrings, pFragSrc, 0);
        glCompileShader( mFsId );
        glGetShaderiv( mFsId, GL_COMPILE_STATUS, &result );
        if( result == GL_FALSE )
        {
            errMsg[0] = 0;
            glGetShaderInfoLog( mFsId, sizeof(errMsg), 0, errMsg);
            if (pFragDbgName)
                LOGE("Shader::Initialize", "%s : %s\n", pFragDbgName, errMsg);
            else
                LOGE("Shader::Initialize", "Compile Error : %s\n", errMsg);
            return false;
        }

        mShaderId = glCreateProgram();
        glAttachShader( mShaderId, mVsId );
        glAttachShader( mShaderId, mFsId );

        for ( uint32_t i = 0; i < sizeof( gDefaultAttributes ) / sizeof( gDefaultAttributes[0] ); i++ )
        {
            SHADER_LOGI("Shader::Initialize", "Shader %d: %s => %d", mShaderId, gDefaultAttributes[i].name, gDefaultAttributes[i].location);
            glBindAttribLocation(mShaderId, gDefaultAttributes[i].location, gDefaultAttributes[i].name);
        }

        glLinkProgram( mShaderId );
        glGetProgramiv( mShaderId, GL_LINK_STATUS, &result);
        if ( result == GL_FALSE )
        {
            errMsg[0] = 0;
            glGetProgramInfoLog( mShaderId, sizeof(errMsg), 0, errMsg);
            if (pVertDbgName && pFragDbgName)
                LOGE("Shader::Initialize", "Link (%s,%s) : %s\n", pVertDbgName, pFragDbgName, errMsg);
            else
                LOGE("Shader::Initialize", "Link Error : %s\n", errMsg);
            return false;
        }

        SHADER_LOGI("Shader::Initialize", "    Shader (Handle = %d) Linked: %s <=> %s", mShaderId, pVertDbgName, pFragDbgName);

        int32_t curTextureUnit = 0;
        int32_t nActiveUniforms;
        glGetProgramiv( mShaderId, GL_ACTIVE_UNIFORMS, &nActiveUniforms );
        SHADER_LOGD("Shader::Initialize", "    Shader (Handle = %d) has %d active uniforms", mShaderId, nActiveUniforms);

        glUseProgram( mShaderId );

        char nameBuffer[MAX_UNIFORM_NAME_LENGTH];
        for ( int32_t i = 0; i < nActiveUniforms; i++ )
        {
            int32_t uniformNameLength, uniformSize;
            GLenum uniformType;
            glGetActiveUniform( mShaderId, i, MAX_UNIFORM_NAME_LENGTH - 1, &uniformNameLength, &uniformSize, &uniformType, &nameBuffer[0]);
            nameBuffer[uniformNameLength] = 0;
            uint32_t location = glGetUniformLocation( mShaderId, nameBuffer );

            Uniform uniform;
            uniform.location = location;
            uniform.type = uniformType;

            if (uniformSize > 1)
                nameBuffer[uniformNameLength - 3] = '\0';

            strncpy( uniform.name, nameBuffer, MAX_UNIFORM_NAME_LENGTH );

            switch (uniformType)
            {
            case GL_FLOAT_VEC2:             // 0x8B50
                SHADER_LOGI("Shader::Initialize", "    Uniform (%s) is a type GL_FLOAT_VEC2 => Bound to location %d", nameBuffer, location);
                break;
            case GL_FLOAT_VEC3:             // 0x8B51
                SHADER_LOGI("Shader::Initialize", "    Uniform (%s) is a type GL_FLOAT_VEC3 => Bound to location %d", nameBuffer, location);
                break;
            case GL_FLOAT_VEC4:             // 0x8B52
                SHADER_LOGI("Shader::Initialize", "    Uniform (%s) is a type GL_FLOAT_VEC4 => Bound to location %d", nameBuffer, location);
                break;
            case GL_FLOAT_MAT2:
                SHADER_LOGI("Shader::Initialize", "    Uniform (%s) is a type GL_FLOAT_MAT2 => Bound to location %d", nameBuffer, location);
                break;
            case GL_FLOAT_MAT4:             // 0x8B5C
                SHADER_LOGI("Shader::Initialize", "    Uniform (%s) is a type GL_FLOAT_MAT4 => Bound to location %d", nameBuffer, location);
                break;
            case GL_SAMPLER_2D:             // 0x8B5E
                SHADER_LOGI("Shader::Initialize", "    Uniform (%s) is a type GL_SAMPLER_2D => Bound to location %d", nameBuffer, location);
                break;
            case GL_SAMPLER_CUBE:           // 0x8B60
                SHADER_LOGI("Shader::Initialize", "    Uniform (%s) is a type GL_SAMPLER_CUBE => Bound to location %d", nameBuffer, location);
                break;
            case GL_SAMPLER_EXTERNAL_OES:   // 0x8D66
                SHADER_LOGI("Shader::Initialize", "    Uniform (%s) is a type GL_SAMPLER_EXTERNAL_OES => Bound to location %d", nameBuffer, location);
                break;
            case GL_SAMPLER_2D_ARRAY:       // 0x8DC1
                SHADER_LOGI("Shader::Initialize", "    Uniform (%s) is a type GL_SAMPLER_2D_ARRAY => Bound to location %d", nameBuffer, location);
                break;

            default:
                SHADER_LOGI("Shader::Initialize", "    Uniform (%s) is a type 0x%x => Bound to location %d", nameBuffer, uniformType, location);
                break;
            }


            if (IsSamplerType(uniformType))
            {
                glUniform1i( location, curTextureUnit);
                uniform.textureUnit = curTextureUnit++;
                SHADER_LOGI("Shader::Initialize", "        Uniform (%s) is a texture in unit %d", nameBuffer, uniform.textureUnit);
            }
            else
            {
                uniform.textureUnit = 0;
                SHADER_LOGI("Shader::Initialize", "        Uniform (%s) is NOT a texture in unit %d", nameBuffer, uniform.textureUnit);
            }

            SHADER_LOGI("Shader::Initialize", "    Inserting (%s) into uniform map...", nameBuffer);
            mUniformMap.Insert( &nameBuffer[0], uniform );
        }

        glUseProgram( 0 );

        // Always want this log message
        if (pVertDbgName && pFragDbgName)
            LOGI("Shader::Initialize", "Loaded Shader (%d): %s <=> %s", mShaderId, pVertDbgName, pFragDbgName);
        else
            LOGI("Shader::Initialize", "Loaded Shader (%d): Unknown <=> Unknown", mShaderId);

        return true;
    }

    void Shader::Destroy()
    {
        if (mShaderId != 0)
        {
            glDeleteProgram(mShaderId);
        }

        if (mVsId != 0)
        {
            glDeleteShader(mVsId);
        }

        if (mFsId != 0)
        {
            glDeleteShader(mFsId);
        }

        mShaderId = 0;
        mVsId = 0;
        mFsId = 0;
    }

    void Shader::Bind()
    {
        // Here is the problem: gCurrentBoundShader is global across all threads!
        // This means there are collisions between threads that are not really collisions.
        // Therefore we can't check this for the spacewarp branch :(
        // if (gCurrentBoundShader != 0)
        // {
        //     LOGW("Shader::Bind", "(ThreadID = 0x%08x) Shader (Handle = %d) is already bound while binding new shader (Handle = %d)", gettid(), gCurrentBoundShader, mShaderId);
        // }

        if (mRefCount != 0)
        {
            LOGW("Shader::Bind", "(ThreadID = 0x%08x) Shader (Handle = %d) is being bound without being unbound. Bind count = %d", gettid(), mShaderId, mRefCount);
        }
        mRefCount = mRefCount + 1;

        gCurrentBoundShader = mShaderId;

        glUseProgram( mShaderId );
    }

    void Shader::Unbind()
    {
        if (mRefCount == 0)
        {
            LOGW("Shader::Unbind", "Shader (Handle = %d) is being unbound without being bound.", mShaderId);
        }
        else
        {
            mRefCount = mRefCount - 1;
        }

        gCurrentBoundShader = 0;

        // LOGE("Unbinding Shader (Handle = %d). Bind count = %d", mShaderId, mRefCount);
        glUseProgram(0);

        // Unbind all samplers
        UniformMap::Iterator uniformIter = mUniformMap.GetIterator();
        while (!uniformIter.End())
        {
            Uniform oneEntry = uniformIter.Current();

            if (IsSamplerType(oneEntry.type))
            {
                glBindSampler(oneEntry.textureUnit, 0);
            }

            uniformIter.Next();
        }
    }

    void Shader::SetUniformMat2(char const* name, glm::mat2& matrix)
    {
        Uniform  uniform;
        if (mUniformMap.Find( name, &uniform))
        {
            SetUniformMat2(uniform.location, matrix);
        }
    }

    void Shader::SetUniformMat2(int32_t const location, glm::mat2& matrix)
    {
        glUniformMatrix2fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
    }

    void Shader::SetUniformMat2fv(char const* name, uint32_t const count, float *pData)
    {
        Uniform  uniform;
        if (mUniformMap.Find(name, &uniform))
        {
            SetUniformMat2fv(uniform.location, count, pData);
        }
    }

    void Shader::SetUniformMat2fv(int32_t const location, uint32_t const count, float *pData)
    {
        glUniformMatrix2fv(location, count, GL_FALSE, pData);
    }

    void Shader::SetUniformMat3(char const* name, glm::mat3& matrix)
    {
        Uniform  uniform;
        if (mUniformMap.Find(name, &uniform))
        {
            SetUniformMat3(uniform.location, matrix);
        }
    }

    void Shader::SetUniformMat3(int32_t location, glm::mat3& matrix)
    {
        glUniformMatrix3fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
    }

    void Shader::SetUniformMat4(const char* name, glm::mat4& matrix)
    {
        Uniform  uniform;
        if ( mUniformMap.Find( name, &uniform ) )
        {
            SetUniformMat4(uniform.location, matrix);
        }
    }

    void Shader::SetUniformMat4(int32_t location, glm::mat4& matrix)
    {
        glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
    }

    void Shader::SetUniformMat4fv(char const* name, uint32_t const count, float *pData)
    {
        Uniform  uniform;
        if (mUniformMap.Find(name, &uniform))
        {
            SetUniformMat4fv(uniform.location, count, pData);
        }
    }

    void Shader::SetUniformMat4fv(int32_t location, uint32_t const count, float *pData)
    {
        glUniformMatrix4fv(location, count, GL_FALSE, pData);
    }

    void Shader::SetUniformVec4(char const* name, glm::vec4& vector)
    {
        Uniform uniform;
        if (mUniformMap.Find( name, &uniform))
        {
            SetUniformVec4(uniform.location, vector);
        }
    }

    void Shader::SetUniformVec4(int32_t location, glm::vec4& vector)
    {
        glUniform4fv(location, 1, glm::value_ptr(vector));
    }

    void Shader::SetUniformVec4fv(char const* name, uint32_t const count, float* pData)
    {
        Uniform uniform;
        if (mUniformMap.Find( name, &uniform))
        {
            glUniform4fv(uniform.location, count, pData);
        }
    }

    void Shader::SetUniformVec4fv(int32_t const location, uint32_t const count, float* pData)
    {
        glUniform4fv(location, count, pData);
    }

    void Shader::SetUniformVec3(char const* name, glm::vec3& vector)
    {
        Uniform uniform;
        if (mUniformMap.Find(name, &uniform))
        {
            SetUniformVec3(uniform.location, vector);
        }
    }

    void Shader::SetUniformVec3(int32_t location, glm::vec3& vector)
    {
        glUniform3fv(location, 1, glm::value_ptr(vector));
    }

    void Shader::SetUniformVec2(char const* name, glm::vec2& vector)
    {
        Uniform uniform;
        if (mUniformMap.Find(name, &uniform))
        {
            SetUniformVec2(uniform.location, vector);
        }
    }

    void Shader::SetUniformVec2(int32_t location, glm::vec2& vector)
    {
        glUniform2fv(location, 1, glm::value_ptr(vector));
    }

    void Shader::SetUniform1ui(char const* name, uint32_t const value)
    {
        Uniform uniform;
        if (mUniformMap.Find(name, &uniform))
        {
            SetUniform1ui(uniform.location, value);
        }
    }

    void Shader::SetUniform1ui(int32_t location, uint32_t const value)
    {
        glUniform1ui(location, value);
    }

    void Shader::SetUniform1f(char const* name, float const value)
    {
        Uniform uniform;
        if (mUniformMap.Find(name, &uniform))
        {
            SetUniform1f(uniform.location, value);
        }
    }

    void Shader::SetUniform1f(int32_t const location, float const value)
    {
        glUniform1f(location, value);
    }


    void Shader::SetUniform1i(const char* name, int32_t const value)
    {
        Uniform uniform;
        if (mUniformMap.Find(name, &uniform))
        {
            SetUniform1i(uniform.location, value);
        }
    }

    void Shader::SetUniform1i(int32_t const location, int32_t const value)
    {
        glUniform1i(location, value);
    }


    void Shader::SetUniform1fv(const char* name, uint32_t const  count, float *pData)
    {
        Uniform  uniform;
        if (mUniformMap.Find(name, &uniform))
        {
            SetUniform1fv(uniform.location, count, pData);
        }
    }

    void Shader::SetUniform1fv(int32_t const location, uint32_t const count, float *pData)
    {
        glUniform1fv(location, count, pData);
    }

    void Shader::SetUniformSampler(char const* name, uint32_t const samplerId, uint32_t const samplerType, GLuint const samplerObjId)
    {
        Uniform uniform;
        if(mUniformMap.Find( name, &uniform))
        {
            switch (samplerType)
            {
            case GL_TEXTURE_2D:
                SHADER_LOGD("Shader::SetUniformSampler", "    %s: Texture Unit = %d; Sampler = %d; Type = GL_TEXTURE_2D", name, uniform.textureUnit, samplerId);
                break;

            case GL_TEXTURE_CUBE_MAP:
                SHADER_LOGD("Shader::SetUniformSampler", "    %s: Texture Unit = %d; Sampler = %d; Type = GL_TEXTURE_CUBE_MAP", name, uniform.textureUnit, samplerId);
                break;

            case GL_TEXTURE_EXTERNAL_OES:
                SHADER_LOGD("Shader::SetUniformSampler", "    %s: Texture Unit = %d; Sampler = %d; Type = GL_TEXTURE_EXTERNAL_OES", name, uniform.textureUnit, samplerId);
                break;

            default:
                SHADER_LOGD("Shader::SetUniformSampler", "    %s: Texture Unit = %d; Sampler = %d; Type = %d", name, uniform.textureUnit, samplerId, samplerType);
                break;
            }

            // It turns out the driver does not handle samplers on image textures :(
            if (samplerObjId != 0 && samplerType != GL_TEXTURE_EXTERNAL_OES)
            {
                glBindSampler(uniform.textureUnit, samplerObjId);
            }

            glActiveTexture(GL_TEXTURE0 + uniform.textureUnit);
            glBindTexture(samplerType, samplerId);
        }
    }
}
