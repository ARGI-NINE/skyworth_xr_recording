//
// Created by DELL on 2024/9/27.
//

#ifndef HELLOXR_GFXHELPER_H
#define HELLOXR_GFXHELPER_H

static inline void CheckGlError(const char *file, int line)
{
    for (GLint error = glGetError(); error; error = glGetError()) {
        char *pError;
        switch (error) {
            case GL_NO_ERROR:
                pError = (char *)"GL_NO_ERROR";
                break;
            case GL_INVALID_ENUM:
                pError = (char *)"GL_INVALID_ENUM";
                break;
            case GL_INVALID_VALUE:
                pError = (char *)"GL_INVALID_VALUE";
                break;
            case GL_INVALID_OPERATION:
                pError = (char *)"GL_INVALID_OPERATION";
                break;
            case GL_OUT_OF_MEMORY:
                pError = (char *)"GL_OUT_OF_MEMORY";
                break;
            case GL_INVALID_FRAMEBUFFER_OPERATION:
                pError = (char *)"GL_INVALID_FRAMEBUFFER_OPERATION";
                break;

            default:
                XR_ERROR("glError (0x%x) %s:%d\n", error, file, line);
                return;
        }

        XR_ERROR("glError (%s) %s:%d\n", pError, file, line);
        return;
    }
    return;
}

#define GL(func)                                                               \
    func;                                                                      \
    CheckGlError(__FILE__, __LINE__)

#endif //HELLOXR_GFXHELPER_H
