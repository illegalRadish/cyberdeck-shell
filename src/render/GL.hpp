#pragma once

// Raspberry Pi's VideoCore GPU has no desktop OpenGL 3.3: Mesa's V3D driver
// exposes OpenGL ES 3.1, and desktop GL only up to 2.1-3.1. A 3.3 core context
// simply fails to create there, so the Pi build targets GLES 3.0 instead.
//
// GLES 3.0 is a close enough subset that nothing in this renderer changes —
// vertex array objects, GL_CLAMP_TO_EDGE, unsized GL_RGBA internal formats and
// glPixelStorei(GL_UNPACK_ALIGNMENT) are all core there. Only the shader
// version header differs, and Shader.cpp rewrites that at load time.
//
// GLES also avoids needing an extension loader: <GLES3/gl3.h> declares the full
// 3.0 API and libGLESv2 exports it, whereas Linux's <GL/gl.h> stops at 1.1 and
// would require GLEW or glad to reach 3.3.
#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#elif defined(CYBERDECK_USE_GLES)
#include <GLES3/gl3.h>
#else
#include <GL/gl.h>
#endif
