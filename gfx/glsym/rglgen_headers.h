#ifndef RGLGEN_HEADERS_H__
#define RGLGEN_HEADERS_H__

#ifdef HAVE_EGL
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif

#if defined(HAVE_PSGL)
#include <PSGL/psgl.h>
#include <GLES/glext.h>
#elif defined(HAVE_OPENGL_MODERN)
#include <GL3/gl3.h>
#include <GL3/gl3ext.h>
#elif defined(HAVE_OPENGLES3)
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h> // There are no GLES3 extensions yet.
#elif defined(HAVE_OPENGLES2)
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#elif defined(HAVE_OPENGLES1)
#include <GLES/gl.h>
#include <GLES/glext.h>
#else
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#ifndef GL_MAP_WRITE_BIT
#define GL_MAP_WRITE_BIT 0x0002
#endif

#ifndef GL_MAP_INVALIDATE_BUFFER_BIT
#define GL_MAP_INVALIDATE_BUFFER_BIT 0x0008
#endif

#ifndef GL_RED_INTEGER
#define GL_RED_INTEGER 0x8D94
#endif

#endif
