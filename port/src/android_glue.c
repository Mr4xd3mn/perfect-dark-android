/*
 * Android glue for the Perfect Dark port.
 *
 * The SDL2 Android integration does all the heavy lifting: its
 * org.libsdl.app.SDLActivity creates the surface, SDL's own JNI code attaches it
 * to the video driver, and SDL's SDLMain thread calls the port's SDL_main.
 * Nothing has to be done here for the window or the entry point.
 *
 * What is needed are stub implementations for fixed-function / OES OpenGL
 * symbols that SDL2's loader references but which do not exist in OpenGL ES
 * 2.0 core. SDL2 never calls the fixed-function ones on its GLES2 path, and
 * the OES framebuffer wrappers are mapped onto the GLES2 core entry points
 * below.
 */

#include <GLES2/gl2.h>

void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    (void)red; (void)green; (void)blue; (void)alpha;
}

void glOrthof(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top,
              GLfloat zNear, GLfloat zFar)
{
    (void)left; (void)right; (void)bottom; (void)top; (void)zNear; (void)zFar;
}

void glMatrixMode(GLenum mode)
{
    (void)mode;
}

void glLoadIdentity(void)
{
}

void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{
    (void)size; (void)type; (void)stride; (void)ptr;
}

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{
    (void)size; (void)type; (void)stride; (void)ptr;
}

void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{
    (void)size; (void)type; (void)stride; (void)ptr;
}

void glEnableClientState(GLenum array)
{
    (void)array;
}

void glDisableClientState(GLenum array)
{
    (void)array;
}

void glTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
    (void)target; (void)pname; (void)param;
}

void glDrawTexfOES(GLfloat x, GLfloat y, GLfloat z, GLfloat width, GLfloat height)
{
    (void)x; (void)y; (void)z; (void)width; (void)height;
}

/* OES framebuffer wrappers mapped to GLES2 core entry points. */
void glGenFramebuffersOES(GLsizei n, GLuint *ids)
{
    glGenFramebuffers(n, ids);
}

void glDeleteFramebuffersOES(GLsizei n, const GLuint *ids)
{
    glDeleteFramebuffers(n, ids);
}

void glBindFramebufferOES(GLenum target, GLuint framebuffer)
{
    glBindFramebuffer(target, framebuffer);
}

GLenum glCheckFramebufferStatusOES(GLenum target)
{
    return glCheckFramebufferStatus(target);
}

void glFramebufferTexture2DOES(GLenum target, GLenum attachment, GLenum textarget,
                               GLuint texture, GLint level)
{
    glFramebufferTexture2D(target, attachment, textarget, texture, level);
}

void glBlendEquationOES(GLenum mode)
{
    glBlendEquation(mode);
}

void glBlendEquationSeparateOES(GLenum modeRGB, GLenum modeAlpha)
{
    glBlendEquationSeparate(modeRGB, modeAlpha);
}

void glBlendFuncSeparateOES(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha)
{
    glBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);
}
