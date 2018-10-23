﻿/**
Copyright © 2018 nicegraf contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the “Software”), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#define _CRT_SECURE_NO_WARNINGS
#include "nicegraf.h"
#include "nicegraf_internal.h"
#include "gl_43_core.h"
#define EGLAPI // prevent __declspec(dllimport) issue on Windows
#include "EGL/egl.h"
#include "EGL/eglext.h"
#if defined(WIN32)
#include <malloc.h>
#else
#include <alloca.h>
#endif
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if defined(WIN32)
#if defined(alloca)
#undef alloca
#endif
#define alloca _alloca
#endif

struct ngf_context {
  EGLDisplay dpy;
  EGLContext ctx;
  EGLConfig cfg;
  EGLSurface surface;
};

struct ngf_shader_stage {
  GLuint glprogram;
  GLenum gltype;
  GLenum glstagebit;
};

struct ngf_buffer {
  GLuint glbuffer;
  GLint bind_point;
  GLint bind_point_read;
  GLenum access_type;
};

struct ngf_descriptors_layout {
  ngf_descriptors_layout_info info;
};

struct ngf_descriptor_set {
  ngf_descriptor_write *bind_ops;
  GLuint *target_bindings;
  uint32_t nslots;
};

struct ngf_pipeline_layout {
  ngf_pipeline_layout_info info;
};

struct ngf_image {
  GLuint glimage;
  GLenum bind_point;
  bool is_renderbuffer;
  bool is_multisample;
  GLenum glformat;
  GLenum gltype;
};

struct ngf_sampler {
  GLuint glsampler;
};

struct ngf_graphics_pipeline {
  uint32_t id;
  GLuint program_pipeline;
  GLuint vao;
  ngf_irect2d viewport;
  ngf_irect2d scissor;
  ngf_rasterization_info rasterization;
  ngf_multisample_info multisample;
  ngf_depth_stencil_info depth_stencil;
  ngf_blend_info blend;
  uint32_t dynamic_state_mask;
  ngf_tessellation_info tessellation;
  ngf_vertex_buf_binding_desc *vert_buf_bindings;
  uint32_t nvert_buf_bindings;
  GLenum primitive_type;
  ngf_pipeline_layout *layout;
};

typedef struct {
  ngf_dynamic_state_command *dynamic_state_cmds;
  uint32_t ndynamic_state_cmds;
  uint32_t ndescriptor_set_bind_ops;
  ngf_descriptor_set_bind_op *descriptor_set_bind_ops;
  ngf_draw_mode mode;
  GLenum primitive;
  ngf_index_buf_bind_op index_buf_bind_op;
  ngf_vertex_buf_bind_op *vertex_buf_bind_ops;
  uint32_t nvertex_buf_bind_ops;
  uint32_t first_element;
  uint32_t nelements;
  uint32_t ninstances;
} ngf_draw_subop;

struct ngf_draw_op {
  const ngf_graphics_pipeline *pipeline;
  ngf_draw_subop *subops;
  uint32_t nsubops;
};

struct ngf_render_target {
  GLuint framebuffer;
  ngf_attachment_type attachment_types[7];
  uint32_t nattachments;
};

struct ngf_pass {
  ngf_clear_info *clears;
  ngf_attachment_load_op *loadops;
  uint32_t nloadops;
};

static GLenum gl_shader_stage(ngf_stage_type stage) {
  static const GLenum stages[] = {
    GL_VERTEX_SHADER,
    GL_TESS_CONTROL_SHADER,
    GL_TESS_EVALUATION_SHADER,
    GL_GEOMETRY_SHADER,
    GL_FRAGMENT_SHADER
  };
  return stages[stage];
}

static GLenum gl_shader_stage_bit(ngf_stage_type stage) {
  static const GLenum stages[] = {
    GL_VERTEX_SHADER_BIT,
    GL_TESS_CONTROL_SHADER_BIT,
    GL_TESS_EVALUATION_SHADER_BIT,
    GL_GEOMETRY_SHADER_BIT,
    GL_FRAGMENT_SHADER_BIT
  };
  return stages[stage];
}

static GLenum gl_type(ngf_type t) {
  static const GLenum types[] = {
    GL_BYTE,
    GL_UNSIGNED_BYTE,
    GL_SHORT,
    GL_UNSIGNED_SHORT,
    GL_INT,
    GL_UNSIGNED_INT,
    GL_FLOAT,
    GL_HALF_FLOAT,
    GL_DOUBLE
  };
  
  return types[t];
}

static GLenum gl_poly_mode(ngf_polygon_mode m) {
  static const GLenum poly_mode[] = {
    GL_FILL,
    GL_LINE,
    GL_POINT
  };
  return poly_mode[(size_t)(m)];
}

static GLenum gl_cull_mode(ngf_cull_mode m) {
  static const GLenum cull_mode[] = {
    GL_BACK,
    GL_FRONT,
    GL_FRONT_AND_BACK
  };
  return cull_mode[(size_t)(m)];
}

static GLenum gl_face(ngf_front_face_mode m) {
  static const GLenum face[] = {
    GL_CCW,
    GL_CW
  };
  return face[(size_t)(m)];
}

static GLenum gl_compare(ngf_compare_op op) {
  static const GLenum compare[] = {
    GL_NEVER,
    GL_LESS,
    GL_LEQUAL,
    GL_EQUAL,
    GL_GEQUAL,
    GL_GREATER,
    GL_NOTEQUAL,
    GL_ALWAYS
  };

  return compare[(size_t)(op)];
}

static GLenum gl_stencil_op(ngf_stencil_op op) {
  static const GLenum o[] = {
    GL_KEEP,
    GL_ZERO,
    GL_REPLACE,
    GL_INCR,
    GL_INCR_WRAP,
    GL_DECR,
    GL_DECR_WRAP,
    GL_INVERT
  };

  return o[(size_t)(op)];
}

static GLenum gl_blendfactor(ngf_blend_factor f) {
  static const GLenum factor[] = {
    GL_ZERO,
    GL_ONE,
    GL_SRC_COLOR,
    GL_ONE_MINUS_SRC_COLOR,
    GL_DST_COLOR,
    GL_ONE_MINUS_DST_COLOR,
    GL_SRC_ALPHA,
    GL_ONE_MINUS_SRC_ALPHA,
    GL_DST_ALPHA,
    GL_ONE_MINUS_DST_ALPHA,
    GL_CONSTANT_COLOR,
    GL_ONE_MINUS_CONSTANT_COLOR,
    GL_CONSTANT_ALPHA,
    GL_ONE_MINUS_CONSTANT_ALPHA
  };
  return factor[(size_t)(f)];
}

static GLenum get_gl_primitive_type(ngf_primitive_type p) {
  static const GLenum primitives[] = {
    GL_TRIANGLES,
    GL_TRIANGLE_STRIP,
    GL_TRIANGLE_FAN,
    GL_LINES,
    GL_LINE_STRIP,
    GL_PATCHES
  };
  return primitives[p];
}

typedef struct {
  GLenum internal_format;
  GLenum format;
  GLenum type;
  uint8_t rbits;
  uint8_t bbits;
  uint8_t gbits;
  uint8_t abits;
  uint8_t dbits;
  uint8_t sbits;
  bool    srgb;
} glformat;

static glformat get_glformat(ngf_image_format f) {
  static const glformat formats[] = {
    {GL_R8, GL_RED, GL_UNSIGNED_BYTE, 8, 0, 0, 0, 0, 0, false},
    {GL_RG8, GL_RG, GL_UNSIGNED_BYTE, 8, 8, 0, 0, 0, 0, false},
    {GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, 8, 8, 8, 0, 0, 0, false},
    {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 8, 8, 8, 8, 0, 0, false},
    {GL_SRGB8, GL_RGB, GL_UNSIGNED_BYTE, 8, 8, 8, 0, 0, 0, true},
    {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, 8, 8, 8, 8, 0, 0, true},
    {GL_RGB8, GL_BGR, GL_UNSIGNED_BYTE, 8, 8, 8, 0, 0, 0, false},
    {GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE, 8, 8, 8, 8, 0, 0, false},
    {GL_SRGB8, GL_BGR, GL_UNSIGNED_BYTE, 8, 8, 8, 0, 0, 0, true},
    {GL_SRGB8_ALPHA8, GL_BGRA, GL_UNSIGNED_BYTE, 8, 8, 8, 8, 0, 0, true},
    {GL_R32F, GL_RED, GL_FLOAT, 32, 0, 0, 0, 0, 0, false},
    {GL_RG32F, GL_RG, GL_FLOAT, 32, 32, 0, 0, 0, 0, false},
    {GL_RGB32F, GL_RGB, GL_FLOAT, 32, 32, 32, 0, 0, 0,  false},
    {GL_RGBA32F, GL_RGB, GL_FLOAT, 32, 32, 32, 32, 0, 0,  false},
    {GL_R16F, GL_RED, GL_HALF_FLOAT, 16, 0, 0, 0, 0, 0, false},
    {GL_RG16F, GL_RG, GL_HALF_FLOAT, 16, 16, 0, 0, 0, 0, false},
    {GL_RGB16F, GL_RGB, GL_HALF_FLOAT, 16, 16, 16, 0, 0, 0, false},
    {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 16, 16, 16, 16, 0, 0, false},
    {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, 0, 0, 0, 0, 32, 0,
     false},
    {GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_FLOAT, 0, 0, 0, 0, 16, 0,
     false},
    {GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_FLOAT, 0, 0, 0, 0, 24, 8,
     false},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, false}
  };
  return formats[f];
}

static GLenum gl_filter(ngf_sampler_filter f) {
  static const GLenum filters[] = {
    GL_NEAREST,
    GL_LINEAR,
    GL_LINEAR_MIPMAP_NEAREST,
    GL_LINEAR_MIPMAP_LINEAR
  };

  return filters[f];
}

static GLenum gl_wrap(ngf_sampler_wrap_mode e) {
  static const GLenum modes[] = {
    GL_CLAMP_TO_EDGE,
    GL_CLAMP_TO_BORDER,
    GL_REPEAT,
    GL_MIRRORED_REPEAT
  };
  return modes[e];
}

void (*NGF_DEBUG_CALLBACK)(const char *message, const void *userdata) = NULL;
void *NGF_DEBUG_USERDATA = NULL;

void GL_APIENTRY ngf_gl_debug_callback(GLenum source,
                                       GLenum type,
                                       GLuint id,
                                       GLenum severity,
                                       GLsizei length,
                                       const GLchar* message,
                                       const void* userdata) {
  if (NGF_DEBUG_CALLBACK) {
    NGF_DEBUG_CALLBACK(message, userdata);
  }
}

ngf_error ngf_initialize(ngf_device_preference dev_pref) {return NGF_ERROR_OK;}

ngf_error ngf_create_context(const ngf_context_info *info,
                             ngf_context **result) {
  assert(info);
  assert(result);
  
  ngf_error err_code = NGF_ERROR_OK;
  ngf_swapchain_info *swapchain_info = info->swapchain_info;
  ngf_context *shared = info->shared_context;

  *result = NGF_ALLOC(ngf_context);
  ngf_context *ctx = *result;
  if (ctx == NULL) {
    err_code = NGF_ERROR_OUTOFMEM;
    goto ngf_create_context_cleanup;
  }

  // Connect to a display.
  eglBindAPI(EGL_OPENGL_API);
  ctx->dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  assert(ctx->dpy != EGL_NO_DISPLAY);
  int egl_maj, egl_min;
  if (eglInitialize(ctx->dpy, &egl_maj, &egl_min) == EGL_FALSE) {
    err_code = NGF_ERROR_CONTEXT_CREATION_FAILED;
    goto ngf_create_context_cleanup;
  }

  // Set present mode.
  if (swapchain_info != NULL) {
    if (swapchain_info->present_mode == NGF_PRESENTATION_MODE_FIFO) {
      eglSwapInterval(ctx->dpy, 1);
    } else if (swapchain_info->present_mode == NGF_PRESENTATION_MODE_IMMEDIATE) {
      eglSwapInterval(ctx->dpy, 0);
    }
  }

  // Choose EGL config.
  EGLint config_attribs[28];
  size_t a = 0;
  config_attribs[a++] = EGL_CONFORMANT; config_attribs[a++] = EGL_OPENGL_BIT;
  if (swapchain_info != NULL) {
    const glformat color_format = get_glformat(swapchain_info->cfmt);
    const glformat depth_stencil_format = get_glformat(swapchain_info->dfmt);
    config_attribs[a++] = EGL_COLOR_BUFFER_TYPE;
    config_attribs[a++] = EGL_RGB_BUFFER;
    config_attribs[a++] = EGL_RED_SIZE;
    config_attribs[a++] = color_format.rbits;
    config_attribs[a++] = EGL_GREEN_SIZE;
    config_attribs[a++] = color_format.gbits;
    config_attribs[a++] = EGL_BLUE_SIZE;
    config_attribs[a++] = color_format.bbits;
    config_attribs[a++] = EGL_ALPHA_SIZE;
    config_attribs[a++] = color_format.abits;
    config_attribs[a++] = EGL_DEPTH_SIZE;
    config_attribs[a++] = depth_stencil_format.dbits;
    config_attribs[a++] = EGL_STENCIL_SIZE;
    config_attribs[a++] = depth_stencil_format.sbits;
    config_attribs[a++] = EGL_SAMPLE_BUFFERS;
    config_attribs[a++] = swapchain_info->nsamples > 0 ? 1 : 0;
    config_attribs[a++] = EGL_SAMPLES;
    config_attribs[a++] = swapchain_info->nsamples;
    config_attribs[a++] = EGL_SURFACE_TYPE;
    config_attribs[a++] = EGL_WINDOW_BIT;
  } 
  config_attribs[a++] = EGL_NONE;
  EGLint num = 0;
  if(!eglChooseConfig(ctx->dpy, config_attribs, &ctx->cfg, 1, &num)) {
    err_code = NGF_ERROR_CONTEXT_CREATION_FAILED;
    goto ngf_create_context_cleanup;
  }

  // Create context with chosen config.
  EGLint is_debug = info->debug;
  EGLint context_attribs[] = {
    EGL_CONTEXT_MAJOR_VERSION, 4,
    EGL_CONTEXT_MINOR_VERSION, 3,
    EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
    EGL_CONTEXT_OPENGL_DEBUG, is_debug,
    EGL_NONE
  };
  EGLContext egl_shared = shared ? shared->ctx : EGL_NO_CONTEXT;
  ctx->ctx =
    eglCreateContext(ctx->dpy, ctx->cfg, egl_shared, context_attribs);
  if (ctx->ctx == EGL_NO_CONTEXT) {
    err_code = NGF_ERROR_CONTEXT_CREATION_FAILED;
    goto ngf_create_context_cleanup;
  }

  // Create surface if necessary.
  if (swapchain_info) {
    const glformat color_format = get_glformat(swapchain_info->cfmt);
    EGLint egl_surface_attribs[] = {
      EGL_RENDER_BUFFER, swapchain_info->capacity_hint <= 1
                             ? EGL_SINGLE_BUFFER
                             : EGL_BACK_BUFFER,
      EGL_GL_COLORSPACE_KHR, color_format.srgb ? EGL_GL_COLORSPACE_SRGB_KHR
                                               : EGL_GL_COLORSPACE_LINEAR_KHR,
      EGL_NONE
    };
    ctx->surface = eglCreateWindowSurface(ctx->dpy,
                                          ctx->cfg,
                                          swapchain_info->native_handle,
                                          egl_surface_attribs);
    if (ctx->surface == EGL_NO_SURFACE) {
      err_code = NGF_ERROR_SWAPCHAIN_CREATION_FAILED;
      goto ngf_create_context_cleanup;
    }
  }

ngf_create_context_cleanup:
  if (err_code != NGF_ERROR_OK) {
    ngf_destroy_context(ctx);
  }
  *result = ctx;
  return err_code;
}

ngf_error ngf_resize_context(ngf_context *ctx,
                             size_t new_width,
                             size_t new_height) {
  return NGF_ERROR_OK;
}

ngf_error ngf_set_context(ngf_context *ctx) {
  assert(ctx);
  bool result = eglMakeCurrent(ctx->dpy, ctx->surface, ctx->surface, ctx->ctx);
  return result ? NGF_ERROR_OK : NGF_ERROR_INVALID_CONTEXT;
}

void ngf_destroy_context(ngf_context *ctx) {
  if (ctx) {
    if (ctx->ctx != EGL_NO_CONTEXT) {
      eglDestroyContext(ctx->dpy, ctx->ctx);
    }
    if (ctx->surface != EGL_NO_SURFACE) {
      eglDestroySurface(ctx->dpy, ctx->surface);
    }
    eglTerminate(ctx->dpy);
    NGF_FREE(ctx);
  }
}

ngf_error ngf_create_shader_stage(const ngf_shader_stage_info *info,
                                  ngf_shader_stage **result) {
  assert(info);
  assert(result);
  ngf_error err = NGF_ERROR_OK;
  ngf_shader_stage *stage = NULL;
  const GLenum gl_stage_type = gl_shader_stage(info->type);
  GLuint shader = glCreateShader(gl_stage_type);
  GLint size = (GLint)info->content_length;
  glShaderSource(shader, 1, &info->content, &size);
  glCompileShader(shader);
  GLint compile_status;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);
  if (compile_status != GL_TRUE) {
    if (NGF_DEBUG_CALLBACK) {
      // Note: theoretically, the OpenGL debug callback extension should
      // invoke the debug callback on shader compilation failure.
      // In practice, it varies between vendors, so we just force-call the
      // debug callback here to make sure it is always invoked. Sadness...
      // You should probably be validating your shaders through glslang as
      // one of the build steps anyways...
      GLint info_log_length = 0;
      glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_log_length);
      char *info_log = malloc(info_log_length + 1);
      info_log[info_log_length] = '\0';
      glGetShaderInfoLog(shader, info_log_length, &info_log_length, info_log);
      if (info->debug_name) {
        char msg[100];
        snprintf(msg, NGF_ARRAYSIZE(msg) - 1, "Error compiling %s",
                 info->debug_name);
        NGF_DEBUG_CALLBACK(msg, NGF_DEBUG_USERDATA);
      }
      NGF_DEBUG_CALLBACK(info_log, NGF_DEBUG_USERDATA);
      free(info_log);
    }
    err = NGF_ERROR_CREATE_SHADER_STAGE_FAILED;
    goto ngf_create_shader_stage_cleanup;
  }
  *result = NGF_ALLOC(ngf_shader_stage);
  stage = *result;
  if (stage == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_shader_stage_cleanup;
  }
  stage->gltype = gl_stage_type;
  stage->glstagebit = gl_shader_stage_bit(info->type);
  stage->glprogram = glCreateProgram();
  glProgramParameteri(stage->glprogram, GL_PROGRAM_SEPARABLE, GL_TRUE);
  glAttachShader(stage->glprogram, shader);
  glLinkProgram(stage->glprogram);
  GLint link_status;
  glGetProgramiv(stage->glprogram, GL_LINK_STATUS, &link_status);
  glDetachShader(stage->glprogram, shader);
  if (link_status != GL_TRUE) {
    if (NGF_DEBUG_CALLBACK) {
      // See previous comment about debug callback.
      GLint info_log_length = 0;
      glGetProgramiv(stage->glprogram, GL_INFO_LOG_LENGTH, &info_log_length);
      char *info_log = malloc(info_log_length + 1);
      info_log[info_log_length] = '\0';
      glGetProgramInfoLog(stage->glprogram, info_log_length, &info_log_length,
                          info_log);
      if (info->debug_name) {
        char msg[100];
        snprintf(msg, NGF_ARRAYSIZE(msg) - 1, "Error linking %s",
                 info->debug_name);
        NGF_DEBUG_CALLBACK(msg, NGF_DEBUG_USERDATA);
      }
      NGF_DEBUG_CALLBACK(info_log, NGF_DEBUG_USERDATA);
      free(info_log);
    }
    err = NGF_ERROR_CREATE_SHADER_STAGE_FAILED;
    goto ngf_create_shader_stage_cleanup;
  }

ngf_create_shader_stage_cleanup:
  if (shader != GL_NONE) glDeleteShader(shader);
  if (err != NGF_ERROR_OK) {
    ngf_destroy_shader_stage(stage);
  }
  return err;
}

void ngf_destroy_shader_stage(ngf_shader_stage *stage) {
  if (stage != NULL) {
    glDeleteProgram(stage->glprogram);
  }
}

ngf_error ngf_create_descriptors_layout(const ngf_descriptors_layout_info *info,
                                        ngf_descriptors_layout **result) {
  assert(info);
  assert(result);
  ngf_error err = NGF_ERROR_OK;
  *result = NGF_ALLOC(ngf_descriptors_layout);
  ngf_descriptors_layout *layout = *result;
  if (layout == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_descriptors_layout_cleanup;
  }

  layout->info.ndescriptors = info->ndescriptors;
  layout->info.descriptors = NGF_ALLOCN(ngf_descriptor_info,
                                        info->ndescriptors);
  if (layout->info.descriptors == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_descriptors_layout_cleanup;
  }
  memcpy(layout->info.descriptors,
         info->descriptors,
         sizeof(ngf_descriptor_info) * info->ndescriptors);

ngf_create_descriptors_layout_cleanup:
  if (err != NGF_ERROR_OK) {
    ngf_destroy_descriptors_layout(layout);
  }
  return err;
}

void ngf_destroy_descriptors_layout(ngf_descriptors_layout *layout) {
  if (layout != NULL) {
    if (layout->info.ndescriptors > 0 &&
        layout->info.descriptors) {
        NGF_FREEN(layout->info.descriptors, layout->info.ndescriptors);
    }
    NGF_FREE(layout);
  }
}

ngf_error ngf_create_pipeline_layout(const ngf_pipeline_layout_info *info,
                                     ngf_pipeline_layout **result) {
  assert(info);
  assert(result);
  ngf_error err = NGF_ERROR_OK;
  *result = NGF_ALLOC(ngf_pipeline_layout);
  ngf_pipeline_layout *layout = *result;
  if (layout == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_pipeline_layout_cleanup;
  }

  layout->info.ndescriptors_layouts = info->ndescriptors_layouts;
  layout->info.descriptors_layouts =
      NGF_ALLOCN(ngf_descriptors_layout*, info->ndescriptors_layouts);
  if (layout->info.descriptors_layouts == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_pipeline_layout_cleanup;
  }
  memcpy((*result)->info.descriptors_layouts,
         info->descriptors_layouts,
         sizeof(ngf_descriptors_layout_info*) * info->ndescriptors_layouts);

ngf_create_pipeline_layout_cleanup:
  if (err != NGF_ERROR_OK) {
    ngf_destroy_pipeline_layout(layout);
  }

  return err;
}

void ngf_destroy_pipeline_layout(ngf_pipeline_layout *layout) {
  if (layout != NULL) {
    if (layout->info.ndescriptors_layouts > 0 &&
        layout->info.descriptors_layouts != NULL) {
      NGF_FREEN(layout->info.descriptors_layouts,
                layout->info.ndescriptors_layouts);
    }
    NGF_FREE(layout);
  }
}

ngf_error ngf_create_descriptor_set(const ngf_descriptors_layout *layout,
                                    ngf_descriptor_set **result) {
  assert(layout);
  assert(result);

  ngf_error err = NGF_ERROR_OK;
  *result = NGF_ALLOC(ngf_descriptor_set);
  ngf_descriptor_set *set = *result;
  if (set == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_descriptor_set_cleanup;
  }

  set->nslots = layout->info.ndescriptors;
  set->bind_ops = NGF_ALLOCN(ngf_descriptor_write, layout->info.ndescriptors);
  if (set->bind_ops == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_descriptor_set_cleanup;
  }

  set->target_bindings = NGF_ALLOCN(GLuint, set->nslots);
  if (set->target_bindings == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_descriptor_set_cleanup;
  }

  for (size_t s = 0; s < set->nslots; ++s) {
    set->bind_ops[s].type = layout->info.descriptors[s].type;
    set->target_bindings[s] = layout->info.descriptors[s].id;
  }

ngf_create_descriptor_set_cleanup:
  if (err != NGF_ERROR_OK) {
    ngf_destroy_descriptor_set(set);
  }

  return err;
}

void ngf_destroy_descriptor_set(ngf_descriptor_set *set) {
  if (set != NULL) {
    if (set->nslots > 0 && set->bind_ops) {
      NGF_FREEN(set->bind_ops, set->nslots);
    }
    if (set->nslots > 0 && set->target_bindings) {
      NGF_FREEN(set->target_bindings, set->nslots);
    }
    NGF_FREE(set);
  }
}

ngf_error ngf_apply_descriptor_writes(const ngf_descriptor_write *writes,
                                      const uint32_t nwrites,
                                      ngf_descriptor_set *set) {
  for (size_t s = 0; s < nwrites; ++s) {
    const ngf_descriptor_write *write = &(writes[s]);
    bool found_binding = false;
    for (uint32_t s = 0u; s < set->nslots; ++s) {
      if (set->bind_ops[s].type == write->type &&
          set->target_bindings[s] == write->binding) {
        set->bind_ops[s] = *write;
        found_binding = true;
        break;
      }
    }
    if (!found_binding) return NGF_ERROR_INVALID_BINDING;
  }
  return NGF_ERROR_OK;
}

ngf_error ngf_create_graphics_pipeline(const ngf_graphics_pipeline_info *info,
                                       ngf_graphics_pipeline **result) {
  static uint32_t global_id = 0;
  ngf_error err = NGF_ERROR_OK;

  *result = NGF_ALLOC(ngf_graphics_pipeline);
  ngf_graphics_pipeline *pipeline = *result;
  if (pipeline == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_pipeline_cleanup;
  }

  // Copy over some state.
  pipeline->viewport = *(info->viewport);
  pipeline->scissor = *(info->scissor);
  pipeline->rasterization = *(info->rasterization);
  pipeline->multisample = *(info->multisample);
  pipeline->depth_stencil = *(info->depth_stencil);
  pipeline->blend = *(info->blend);
  pipeline->tessellation = *(info->tessellation);
  
  ngf_vertex_input_info *input = info->input_info;

  // Copy over vertex buffer binding information.
  pipeline->nvert_buf_bindings = input->nvert_buf_bindings;
  if (input->nvert_buf_bindings > 0) {
    pipeline->vert_buf_bindings =
        NGF_ALLOCN(ngf_vertex_buf_binding_desc,
                   input->nvert_buf_bindings);
    if (pipeline->vert_buf_bindings == NULL) {
      err = NGF_ERROR_OUTOFMEM;
      goto ngf_create_pipeline_cleanup;
    }
    memcpy(pipeline->vert_buf_bindings,
           input->vert_buf_bindings,
           sizeof(ngf_vertex_buf_binding_desc) * input->nvert_buf_bindings);
  } else {
    pipeline->vert_buf_bindings = NULL;
  }

  // Store attribute format in VAO.
  glGenVertexArrays(1, &pipeline->vao);
  glBindVertexArray(pipeline->vao);
  for (size_t a = 0; a < input->nattribs; ++a) {
    const ngf_vertex_attrib_desc *attrib = &(input->attribs[a]);
    glEnableVertexAttribArray(attrib->location);
    glVertexAttribFormat(attrib->location,
                         attrib->size,
                         gl_type(attrib->type),
                         attrib->normalized,
                         attrib->offset);
    glVertexAttribBinding(attrib->location, attrib->binding);
  }
  for (uint32_t b = 0; b < pipeline->nvert_buf_bindings; ++b) {
    glVertexBindingDivisor(pipeline->vert_buf_bindings[b].binding,
                           pipeline->vert_buf_bindings[b].input_rate);
  }
  glBindVertexArray(0);
  pipeline->primitive_type = get_gl_primitive_type(info->primitive_type);

  // Create and configure the program pipeline object with the provided
  // shader stages.
  if (info->nshader_stages >= NGF_ARRAYSIZE(info->shader_stages)) {
    err = NGF_ERROR_OUT_OF_BOUNDS;
    goto ngf_create_pipeline_cleanup;
  }
  glGenProgramPipelines(1, &pipeline->program_pipeline);
  for (size_t s = 0; s < info->nshader_stages; ++s) {
    glUseProgramStages(pipeline->program_pipeline,
                       info->shader_stages[s]->glstagebit,
                       info->shader_stages[s]->glprogram);
  }

  // Set pipeline layout.
  pipeline->layout = info->layout;

  // Set dynamic state mask.
  pipeline->dynamic_state_mask = info->dynamic_state_mask;

  // Assign a unique id to the pipeline.
  pipeline->id = ++global_id;

ngf_create_pipeline_cleanup:
  if (err != NGF_ERROR_OK) {
    ngf_destroy_graphics_pipeline(pipeline);
  } 
  return err;
}

void ngf_destroy_graphics_pipeline(ngf_graphics_pipeline *pipeline) {
  if (pipeline) {
    if (pipeline->nvert_buf_bindings > 0 &&
        pipeline->vert_buf_bindings) {
      NGF_FREEN(pipeline->vert_buf_bindings, pipeline->nvert_buf_bindings);
    }
    glDeleteProgramPipelines(1, &pipeline->program_pipeline);
    glDeleteVertexArrays(1, &pipeline->vao);
    NGF_FREEN(pipeline, 1);
  }
}

ngf_error ngf_create_image(const ngf_image_info *info, ngf_image **result) {
  assert(info);
  assert(result);

  *result = NGF_ALLOC(ngf_image);
  ngf_image *image = *result;
  if (image == NULL) {
    return NGF_ERROR_OUTOFMEM;
  }

  glformat glf = get_glformat(info->format);
  image->glformat = glf.format;
  image->gltype = glf.type;
  image->is_multisample = info->nsamples > 1;
  if (info->usage_hint & NGF_IMAGE_USAGE_SAMPLE_FROM ||
      info->nmips > 1 ||
      info->extent.depth > 1 ||
      info->type != NGF_IMAGE_TYPE_IMAGE_2D) {
    image->is_renderbuffer = false;
    if (info->type == NGF_IMAGE_TYPE_IMAGE_2D &&
        info->extent.depth <= 1) {
      image->bind_point =
        info->nsamples > 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
    } else if (info->type == NGF_IMAGE_TYPE_IMAGE_2D &&
               info->extent.depth > 1) {
      image->bind_point =
          info->nsamples > 1
            ? GL_TEXTURE_2D_MULTISAMPLE_ARRAY
            : GL_TEXTURE_2D_ARRAY;
    } else if (info->type == NGF_IMAGE_TYPE_IMAGE_3D) {
      image->bind_point = GL_TEXTURE_3D;
    }
    glGenTextures(1, &(image->glimage));
    glBindTexture(image->bind_point, image->glimage);
    if (image->bind_point == GL_TEXTURE_2D) {
      glTexStorage2D(image->bind_point,
                     info->nmips,
                     glf.internal_format,
                     info->extent.width,
                     info->extent.height);
    } else if (image->bind_point == GL_TEXTURE_2D_ARRAY ||
               image->bind_point == GL_TEXTURE_3D) { 
      glTexStorage3D(image->bind_point,
                     info->nmips,
                     glf.internal_format,
                     info->extent.width,
                     info->extent.height,
                     info->extent.depth);
    } else if (image->bind_point == GL_TEXTURE_2D_MULTISAMPLE) {
      glTexStorage2DMultisample(image->bind_point,
                                info->nsamples,
                                glf.internal_format,
                                info->extent.width,
                                info->extent.height,
                                GL_TRUE);
    } else if (image->bind_point == GL_TEXTURE_2D_MULTISAMPLE_ARRAY) {
      glTexStorage3DMultisample(image->bind_point,
                                info->nsamples,
                                glf.internal_format,
                                info->extent.width,
                                info->extent.height,
                                info->extent.depth,
                                GL_TRUE);
    }  
  } else {
    image->is_renderbuffer = true;
    image->bind_point = GL_RENDERBUFFER;
    (glGenRenderbuffers(1, &(image->glimage)));
    (glBindRenderbuffer(GL_RENDERBUFFER, image->glimage));
    if (info->nsamples <= 1) {
      glRenderbufferStorage(image->bind_point,
                            glf.internal_format,
                            info->extent.width,
                            info->extent.height);
    } else {
      glRenderbufferStorageMultisample(image->bind_point,
                                       info->nsamples,
                                       glf.internal_format,
                                       info->extent.width,
                                       info->extent.height);
    }
  }
  return NGF_ERROR_OK;
}

void ngf_destroy_image(ngf_image *image) {
  if (image != NULL) {
    if (!image->is_renderbuffer) {
      glDeleteTextures(1, &(image->glimage));
    } else {
      glDeleteRenderbuffers(1, &(image->glimage));
    }
    NGF_FREE(image);
  }
}

ngf_error ngf_populate_image(ngf_image *image,
                             uint32_t level,
                             ngf_offset3d offset,
                             ngf_extent3d dimensions,
                             const void *data) {
  assert(image);
  assert(data);
  if (image->is_multisample || image->is_renderbuffer) {
    return NGF_ERROR_CANT_POPULATE_IMAGE;
  } else {
    glBindTexture(image->bind_point, image->glimage);
    if (image->bind_point == GL_TEXTURE_2D) {
      glTexSubImage2D(image->bind_point,
                      level,
                      offset.x,
                      offset.y,
                      dimensions.width,
                      dimensions.height,
                      image->glformat,
                      image->gltype,
                      data);
    } else {
      glTexSubImage3D(image->bind_point,
                      level,
                      offset.x,
                      offset.y,
                      offset.z,
                      dimensions.width,
                      dimensions.height,
                      dimensions.depth,
                      image->glformat,
                      image->gltype,
                      data);
    }
  }
  return NGF_ERROR_OK;
}

ngf_error ngf_create_sampler(const ngf_sampler_info *info,
                             ngf_sampler **result) {
  assert(info);
  assert(result);

  *result = NGF_ALLOC(ngf_sampler);
  ngf_sampler *sampler = *result;
  if (sampler == NULL) {
    return NGF_ERROR_OUTOFMEM;
  }
  
  glGenSamplers(1, &(sampler->glsampler));
  glSamplerParameteri(sampler->glsampler, GL_TEXTURE_MIN_FILTER, gl_filter(info->min_filter));
  glSamplerParameteri(sampler->glsampler, GL_TEXTURE_MAG_FILTER, gl_filter(info->mag_filter));
  glSamplerParameteri(sampler->glsampler, GL_TEXTURE_WRAP_S, gl_wrap(info->wrap_s));
  glSamplerParameteri(sampler->glsampler, GL_TEXTURE_WRAP_T, gl_wrap(info->wrap_t));
  glSamplerParameteri(sampler->glsampler, GL_TEXTURE_WRAP_R, gl_wrap(info->wrap_r));
  glSamplerParameterf(sampler->glsampler, GL_TEXTURE_MIN_LOD, info->lod_min);
  glSamplerParameterf(sampler->glsampler, GL_TEXTURE_MAX_LOD, info->lod_max);
  glSamplerParameterf(sampler->glsampler, GL_TEXTURE_LOD_BIAS, info->lod_bias);
  glSamplerParameterfv(sampler->glsampler, GL_TEXTURE_BORDER_COLOR, info->border_color);
  // TODO: anisotropic filtering

  return NGF_ERROR_OK;
}

void ngf_destroy_sampler(ngf_sampler *sampler) {
  assert(sampler);
  glDeleteSamplers(1, &(sampler->glsampler));
  NGF_FREE(sampler);
}

ngf_error ngf_default_render_target(ngf_render_target **result) {
  static ngf_render_target default_target;
  default_target.framebuffer = 0;
  default_target.attachment_types[0] = NGF_ATTACHMENT_COLOR;
  default_target.attachment_types[1] = NGF_ATTACHMENT_DEPTH;
  default_target.attachment_types[2] = NGF_ATTACHMENT_STENCIL;
  default_target.nattachments = 3;
  *result = &default_target;
  return NGF_ERROR_OK;
}

ngf_error ngf_create_render_target(const ngf_render_target_info *info,
                                   ngf_render_target **result) {

  assert(info);
  assert(result);
  assert(info->nattachments < 7);

  ngf_error err = NGF_ERROR_OK;
  *result = NGF_ALLOC(ngf_render_target);
  ngf_render_target *render_target = *result;
  if (render_target == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_render_target_cleanup;
  }

  glGenFramebuffers(1, &(render_target->framebuffer));
  GLint old_framebuffer;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, render_target->framebuffer);
  size_t ncolor_attachment = 0;
  render_target->nattachments = info->nattachments;
  for (size_t i = 0; i < info->nattachments; ++i) {
    const ngf_attachment *a = &(info->attachments[i]);
    render_target->attachment_types[i] = a->type;
    GLenum attachment;
    switch (a->type) {
    case NGF_ATTACHMENT_COLOR:
      attachment = GL_COLOR_ATTACHMENT0 + (ncolor_attachment++);
      break;
    case NGF_ATTACHMENT_DEPTH:
      attachment = GL_DEPTH_ATTACHMENT;
      break;
    case NGF_ATTACHMENT_DEPTH_STENCIL:
      attachment = GL_DEPTH_STENCIL_ATTACHMENT;
      break;
    case NGF_ATTACHMENT_STENCIL:
      attachment = GL_STENCIL_ATTACHMENT;
      break;
    default:
      assert(0);
    }
    if (!a->image_ref.image->is_renderbuffer &&
        (a->image_ref.layered ||
         a->image_ref.image->bind_point != GL_TEXTURE_2D_ARRAY)) {
      glFramebufferTexture(GL_FRAMEBUFFER,
                           attachment,
                           a->image_ref.image->glimage,
                           a->image_ref.mip_level);
    } else if (!a->image_ref.image->is_renderbuffer) {
      glFramebufferTextureLayer(GL_FRAMEBUFFER,
                                attachment,
                                a->image_ref.image->glimage,
                                a->image_ref.mip_level,
                                a->image_ref.layer);
    } else {
      glBindRenderbuffer(GL_RENDERBUFFER, 0);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER,
                                attachment,
                                GL_RENDERBUFFER,
                                a->image_ref.image->glimage);
    }
  }

  GLenum fb_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  bool fb_ok = fb_status == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, old_framebuffer);
  if (!fb_ok) {
    err = NGF_ERROR_INCOMPLETE_RENDER_TARGET;
    goto ngf_create_render_target_cleanup;
  }

ngf_create_render_target_cleanup:
  if (err != NGF_ERROR_OK) {
    ngf_destroy_render_target(render_target);
  }
  return err;
}

void ngf_destroy_render_target(ngf_render_target *render_target) {
  if (render_target != NULL) {
    glDeleteFramebuffers(1, &(render_target->framebuffer));
    NGF_FREE(render_target);
  }
}

ngf_error ngf_resolve_render_target(const ngf_render_target *src,
                                    ngf_render_target *dst,
                                    const ngf_irect2d *src_rect) {
  GLint prev_fbo;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, src->framebuffer);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst->framebuffer);
  glBlitFramebuffer(src_rect->x, src_rect->y,
                    src_rect->x + src_rect->width,
                    src_rect->y + src_rect->height,
                    src_rect->x, src_rect->y,
                    src_rect->x + src_rect->width,
                    src_rect->y + src_rect->height,
                    GL_COLOR_BUFFER_BIT,
                    GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
  return NGF_ERROR_OK;
}

ngf_error ngf_create_buffer(const ngf_buffer_info *info, ngf_buffer **result) {
  assert(info);
  assert(result);

  *result = NGF_ALLOC(ngf_buffer);
  ngf_buffer *buffer = *result;
  if (buffer == NULL) {
    return NGF_ERROR_OUTOFMEM;
  }

  glGenBuffers(1, &(buffer->glbuffer));
  switch (info->type) {
  case NGF_BUFFER_TYPE_VERTEX:
    buffer->bind_point = GL_ARRAY_BUFFER;
    buffer->bind_point_read = GL_ARRAY_BUFFER_BINDING;
    break;
  case NGF_BUFFER_TYPE_INDEX:
    buffer->bind_point = GL_ELEMENT_ARRAY_BUFFER;
    buffer->bind_point_read = GL_ELEMENT_ARRAY_BUFFER_BINDING;
    break;
  case NGF_BUFFER_TYPE_UNIFORM:
    buffer->bind_point = GL_UNIFORM_BUFFER;
    buffer->bind_point_read = GL_UNIFORM_BUFFER_BINDING;
    break;
  default:
    assert(0);
  }
  static GLenum access_types[3][3] = {
    {GL_STATIC_DRAW, GL_STATIC_READ, GL_STATIC_COPY},
    {GL_DYNAMIC_DRAW, GL_DYNAMIC_READ, GL_DYNAMIC_COPY},
    {GL_STREAM_DRAW, GL_DYNAMIC_READ, GL_DYNAMIC_COPY},
  };
  assert(info->access_freq< 3 && info->access_type < 3);
  buffer->access_type = access_types[(size_t)(info->access_freq)]
                                    [(size_t)(info->access_type)]; 

  if (info->access_freq != NGF_BUFFER_USAGE_STATIC) {
    glBindBuffer(buffer->bind_point, buffer->glbuffer);
    glBufferData(buffer->bind_point,
                 info->size,
                 NULL,
                 buffer->access_type);
  }
  return NGF_ERROR_OK;
}

ngf_error ngf_populate_buffer(ngf_buffer *buf,
                              size_t offset,
                              size_t size,
                              void *data) {
  assert(buf);
  assert(data);
  glBindBuffer(buf->bind_point, buf->glbuffer);
  if (buf->access_type == GL_STATIC_DRAW ||
      buf->access_type == GL_STATIC_COPY ||
      buf->access_type == GL_STATIC_READ) {
    glBufferData(buf->bind_point, size, data, buf->access_type);
  } else {
    glBufferSubData(buf->bind_point, offset, size, data);
  }
  return NGF_ERROR_OK;
}

void ngf_destroy_buffer(ngf_buffer *buffer) {
  if (buffer != NULL) {
    glDeleteBuffers(1, &(buffer->glbuffer));
    NGF_FREE(buffer);
  }
}

ngf_error ngf_create_draw_op(const ngf_draw_op_info *info,
                             ngf_draw_op **result) {
  assert(info);
  assert(result);

  ngf_error err = NGF_ERROR_OK;
  *result = NGF_ALLOC(ngf_draw_op);
  ngf_draw_op *op = *result;

  if (op == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_draw_op_cleanup;
  }

  op->pipeline = info->pipeline;
  op->nsubops = info->nsubops;
  op->subops = NGF_ALLOCN(ngf_draw_subop, info->nsubops);
  if (op->subops == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_draw_op_cleanup;
  }
  memset(op->subops, 0, sizeof(ngf_draw_subop) * info->nsubops);

  for (size_t s = 0; s < info->nsubops; ++s) {
    ngf_draw_subop *subop = &(op->subops[s]);
    const ngf_draw_subop_info *subop_info = &(info->subops[s]);
    subop->ndynamic_state_cmds = subop_info->ndynamic_state_cmds;
    if (subop_info->ndynamic_state_cmds > 0) {
      subop->dynamic_state_cmds =
        NGF_ALLOCN(ngf_dynamic_state_command, subop_info->ndynamic_state_cmds);
      if (subop->dynamic_state_cmds == NULL) {
        err = NGF_ERROR_OUTOFMEM;
        goto ngf_create_draw_op_cleanup;
      }
      memcpy(subop->dynamic_state_cmds,
             subop_info->dynamic_state_cmds,
             sizeof(ngf_dynamic_state_command) * subop->ndynamic_state_cmds);
    }

    subop->ndescriptor_set_bind_ops = subop_info->ndescriptor_set_bind_ops;
    subop->descriptor_set_bind_ops =
        NGF_ALLOCN(ngf_descriptor_set_bind_op, subop->ndescriptor_set_bind_ops);
    if (subop->descriptor_set_bind_ops == NULL) {
      err = NGF_ERROR_OUTOFMEM;
      goto ngf_create_draw_op_cleanup;
    }
    memcpy(subop->descriptor_set_bind_ops,
           subop_info->descriptor_set_bind_ops,
           sizeof(ngf_descriptor_set_bind_op) * subop->ndescriptor_set_bind_ops);
    subop->mode = subop_info->mode;
    subop->first_element = subop_info->first_element;
    subop->nelements = subop_info->nelements;
    subop->ninstances = subop_info->ninstances;

    if (subop->mode == NGF_DRAW_MODE_INDEXED ||
        subop->mode == NGF_DRAW_MODE_INDEXED_INSTANCED) {
      const ngf_buffer *index_buffer = subop_info->index_buf_bind_op->buffer;
      ngf_type element_type = subop_info->index_buf_bind_op->type; 
      if (index_buffer->bind_point != GL_ELEMENT_ARRAY_BUFFER ||
          (element_type != NGF_TYPE_UINT16 && element_type != NGF_TYPE_UINT32)) {
        err = NGF_ERROR_INVALID_INDEX_BUFFER_BINDING;
        goto ngf_create_draw_op_cleanup;
      }
      subop->index_buf_bind_op = *(subop_info->index_buf_bind_op);
    }

    subop->vertex_buf_bind_ops = NGF_ALLOCN(ngf_vertex_buf_bind_op,
                                            subop_info->nvertex_buf_bind_ops);
    if (subop->vertex_buf_bind_ops == NULL) {
      err = NGF_ERROR_OUTOFMEM;
      goto ngf_create_draw_op_cleanup;
    }
    subop->nvertex_buf_bind_ops = subop_info->nvertex_buf_bind_ops;
    memcpy(subop->vertex_buf_bind_ops,
           subop_info->vertex_buf_bind_ops,
           sizeof(ngf_vertex_buf_bind_op) * subop->nvertex_buf_bind_ops);
  }
  op->nsubops = info->nsubops;
ngf_create_draw_op_cleanup:
  if (err != NGF_ERROR_OK) {
    ngf_destroy_draw_op(op);
  }
  return err;
}

void ngf_destroy_draw_op(ngf_draw_op *op) {
  if (op != NULL) {
    for (size_t i = 0; i < op->nsubops; ++i) {
      if (op->subops[i].dynamic_state_cmds &&
          op->subops[i].ndynamic_state_cmds > 0) {
        NGF_FREEN(op->subops[i].dynamic_state_cmds,
                  op->subops[i].ndynamic_state_cmds);
      }
      if (op->subops[i].descriptor_set_bind_ops &&
          op->subops[i].ndescriptor_set_bind_ops > 0) {
        NGF_FREEN(op->subops[i].descriptor_set_bind_ops,
                  op->subops[i].ndescriptor_set_bind_ops);
      }
      if (op->subops[i].nvertex_buf_bind_ops > 0 &&
          op->subops[i].vertex_buf_bind_ops != NULL) {
        NGF_FREEN(op->subops[i].vertex_buf_bind_ops,
                  op->subops[i].nvertex_buf_bind_ops);
      }
    }
    if (op->nsubops > 0 && op->subops) {
      NGF_FREEN(op->subops, op->nsubops);
    }
    NGF_FREE(op);
  }
}

ngf_error ngf_create_pass(const ngf_pass_info *info, ngf_pass **result) {
  assert(info);
  assert(result);

  ngf_error err = NGF_ERROR_OK;
  *result = NGF_ALLOC(ngf_pass);
  ngf_pass *pass = *result;
  if (pass == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_pass_cleanup;
  }
  pass->nloadops = info->nloadops;
  pass->clears = NGF_ALLOCN(ngf_clear_info, info->nloadops);
  if (pass->clears == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_pass_cleanup;
  }
  memcpy(pass->clears, info->clears, sizeof(ngf_clear_info) * pass->nloadops);
  pass->loadops = NGF_ALLOCN(ngf_attachment_load_op, info->nloadops);
  if (pass->loadops == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_pass_cleanup;
  }
  memcpy(pass->loadops,
         info->loadops,
         sizeof(ngf_attachment_load_op) * pass->nloadops);

ngf_create_pass_cleanup:
  if (err != NGF_ERROR_OK) {
    ngf_destroy_pass(pass);
  }
  return err;
}

void ngf_destroy_pass(ngf_pass *pass) {
  if (pass != NULL) {
    if (pass->nloadops > 0 && pass->clears) {
      NGF_FREEN(pass->clears, pass->nloadops);
    }
    if (pass->nloadops > 0 && pass->loadops) {
      NGF_FREEN(pass->loadops, pass->nloadops);
    }
    NGF_FREE(pass);
  }
}

static ngf_graphics_pipeline PIPELINE_CACHE;
ngf_error ngf_execute_pass(const ngf_pass *pass,
                           const ngf_render_target *rt,
                           ngf_draw_op **drawops,
                           const uint32_t ndrawops) {
  assert(pass);
  assert(rt);
  assert(ndrawops == 0u || (ndrawops > 0u && drawops));

  glBindFramebuffer(GL_FRAMEBUFFER, rt->framebuffer);
  // TODO: assert renderpass <-> rendertarget compatibility.
  GLbitfield clear_mask = 0;
  for (size_t o = 0; o < pass->nloadops; ++o) {
    const ngf_attachment_load_op op = pass->loadops[o];
    const ngf_clear_info *clear = &(pass->clears[o]);
    if (op == NGF_LOAD_OP_CLEAR) {
      switch (rt->attachment_types[o]) {
      case NGF_ATTACHMENT_COLOR:
        clear_mask |= GL_COLOR_BUFFER_BIT;
        glClearColor(clear->clear_color[0],
                     clear->clear_color[1],
                     clear->clear_color[2],
                     clear->clear_color[3]);
        break;
      case NGF_ATTACHMENT_DEPTH:
        clear_mask |= GL_DEPTH_BUFFER_BIT;
        glClearDepth(clear->clear_depth);
        break;
      case NGF_ATTACHMENT_STENCIL:
        clear_mask |= GL_STENCIL_BUFFER_BIT;
        glClearStencil(clear->clear_stencil);
        break;
      case NGF_ATTACHMENT_DEPTH_STENCIL:
        clear_mask |= (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glClearDepth(clear->clear_depth);
        glClearStencil(clear->clear_stencil);
        break;
      }
    }
  }
  glClear(clear_mask);

  for (size_t o = 0; o < ndrawops; ++o) {
    ngf_draw_op *op = drawops[o];
    const ngf_graphics_pipeline *pipeline = op->pipeline;
    if (PIPELINE_CACHE.id != pipeline->id) {
      PIPELINE_CACHE.id = pipeline->id;
      glBindProgramPipeline(pipeline->program_pipeline);
      if (!NGF_STRUCT_EQ(pipeline->viewport, PIPELINE_CACHE.viewport)) {
        glViewport(pipeline->viewport.x,
                   pipeline->viewport.y,
                   pipeline->viewport.width,
                   pipeline->viewport.height);
      }

      if (!NGF_STRUCT_EQ(pipeline->scissor, PIPELINE_CACHE.scissor)) {
        glScissor(pipeline->scissor.x,
                  pipeline->scissor.y,
                  pipeline->scissor.width,
                  pipeline->scissor.height);
      }

      const ngf_rasterization_info *rast = &(pipeline->rasterization);
      ngf_rasterization_info *cached_rast = &(PIPELINE_CACHE.rasterization);
      if (cached_rast->discard != rast->discard) {
        if (rast->discard) {
          glEnable(GL_RASTERIZER_DISCARD);
        } else {
          glDisable(GL_RASTERIZER_DISCARD);
        }
      }
      if (cached_rast->polygon_mode != rast->polygon_mode) {
        glPolygonMode(GL_FRONT_AND_BACK, gl_poly_mode(rast->polygon_mode));
      }
      if (cached_rast->cull_mode != rast->cull_mode) {
        glCullFace(gl_cull_mode(rast->cull_mode));
      }
      if (cached_rast->front_face != rast->front_face) {
        glFrontFace(gl_face(rast->front_face));
      }
      if (cached_rast->line_width != rast->line_width) {
        glLineWidth(rast->line_width);
      }

      if (PIPELINE_CACHE.multisample.multisample !=
          pipeline->multisample.multisample) {
        if (pipeline->multisample.multisample) {
          glEnable(GL_MULTISAMPLE);
        } else {
          glDisable(GL_MULTISAMPLE);
        }
      }

      if (PIPELINE_CACHE.multisample.alpha_to_coverage !=
          pipeline->multisample.alpha_to_coverage) {
        if (pipeline->multisample.alpha_to_coverage) {
          glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        } else {
          glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        }
      }

      const ngf_depth_stencil_info *depth_stencil = &(pipeline->depth_stencil);
      ngf_depth_stencil_info *cached_depth_stencil =
          &(PIPELINE_CACHE.depth_stencil);
      if (cached_depth_stencil->depth_test != depth_stencil->depth_test) {
        if (depth_stencil->depth_test) {
          glEnable(GL_DEPTH_TEST);
          glDepthFunc(gl_compare(depth_stencil->depth_compare));
        } else {
          glDisable(GL_DEPTH_TEST);
        }
      }
      if (cached_depth_stencil->depth_write != depth_stencil->depth_write) {
        if (depth_stencil->depth_write) {
          glDepthMask(GL_TRUE);
        } else {
          glDepthMask(GL_FALSE);
        }
      }
      if (cached_depth_stencil->stencil_test != depth_stencil->stencil_test ||
          !NGF_STRUCT_EQ(cached_depth_stencil->back_stencil,
                         depth_stencil->back_stencil) ||
          !NGF_STRUCT_EQ(cached_depth_stencil->front_stencil,
                         depth_stencil->front_stencil)) {
        if (depth_stencil->stencil_test) {
          glEnable(GL_STENCIL_TEST);
          glStencilFuncSeparate(
            GL_FRONT,
            gl_compare(depth_stencil->front_stencil.compare_op),
            depth_stencil->front_stencil.reference,
            depth_stencil->front_stencil.compare_mask);
          glStencilOpSeparate(
            GL_FRONT,
            gl_stencil_op(depth_stencil->front_stencil.fail_op),
            gl_stencil_op(depth_stencil->front_stencil.depth_fail_op),
            gl_stencil_op(depth_stencil->front_stencil.pass_op));
          glStencilMaskSeparate(GL_FRONT,
                                depth_stencil->front_stencil.write_mask);
          glStencilFuncSeparate(
            GL_BACK,
            gl_compare(depth_stencil->back_stencil.compare_op),
            depth_stencil->back_stencil.reference,
            depth_stencil->back_stencil.compare_mask);
          glStencilOpSeparate(
            GL_BACK,
            gl_stencil_op(depth_stencil->back_stencil.fail_op),
            gl_stencil_op(depth_stencil->back_stencil.depth_fail_op),
            gl_stencil_op(depth_stencil->back_stencil.pass_op));
          glStencilMaskSeparate(GL_BACK,
                                depth_stencil->back_stencil.write_mask);
        } else { 
          glDisable(GL_STENCIL_TEST);
        }
      }
      if (cached_depth_stencil->min_depth != depth_stencil->min_depth ||
          cached_depth_stencil->max_depth != depth_stencil->max_depth) {
        glDepthRangef(depth_stencil->min_depth, depth_stencil->max_depth);
      }

      const ngf_blend_info *blend = &(pipeline->blend);
      ngf_blend_info *cached_blend = &(PIPELINE_CACHE.blend);
      if (cached_blend->enable != blend->enable ||
          cached_blend->sfactor != blend->sfactor ||
          cached_blend->dfactor != blend->dfactor) {
        if (blend->enable) {
          glEnable(GL_BLEND);
          glBlendFunc(gl_blendfactor(blend->sfactor),
                      gl_blendfactor(blend->dfactor));
        } else {
          glDisable(GL_BLEND);
        }
      }

      if (PIPELINE_CACHE.tessellation.patch_vertices !=
          pipeline->tessellation.patch_vertices) {
        glPatchParameteri(GL_PATCH_VERTICES, pipeline->tessellation.patch_vertices);
      }
      glBindVertexArray(pipeline->vao);
      PIPELINE_CACHE = *pipeline;
    }

    for (size_t s = 0; s < op->nsubops; ++s) {
      const ngf_draw_subop *subop = &(op->subops[s]);
      // Set dynamic pipeline state.
      for (size_t i = 0; i < subop->ndynamic_state_cmds; ++i) {
        const ngf_dynamic_state_command *cmd = &(subop->dynamic_state_cmds[i]);
        switch (cmd->state) {
        case NGF_DYNAMIC_STATE_VIEWPORT:
          glViewport(cmd->viewport.x,
                     cmd->viewport.y,
                     cmd->viewport.width,
                     cmd->viewport.height);
          break;
        case NGF_DYNAMIC_STATE_SCISSOR:
          glScissor(cmd->scissor.x,
                    cmd->scissor.y,
                    cmd->scissor.width,
                    cmd->scissor.height);
          break;
        case NGF_DYNAMIC_STATE_LINE_WIDTH:
          glLineWidth(cmd->line_width);
          break;
        case NGF_DYNAMIC_STATE_BLEND_CONSTANTS:
          glBlendFunc(cmd->blend_factors.sfactor,
                      cmd->blend_factors.dfactor);
          break;
        case NGF_DYNAMIC_STATE_STENCIL_WRITE_MASK:
          glStencilMaskSeparate(GL_FRONT, cmd->stencil_write_mask.front);
          glStencilMaskSeparate(GL_BACK, cmd->stencil_write_mask.back);
          break;
        case NGF_DYNAMIC_STATE_STENCIL_COMPARE_MASK: {
          GLint back_func, front_func, front_ref, back_ref;
          glGetIntegerv(GL_STENCIL_BACK_FUNC, &back_func);
          glGetIntegerv(GL_STENCIL_FUNC, &front_func);
          glGetIntegerv(GL_STENCIL_BACK_REF, &back_ref);
          glGetIntegerv(GL_STENCIL_REF, &front_ref);
          glStencilFuncSeparate(GL_FRONT,
                                front_func,
                                front_ref,
                                cmd->stencil_compare_mask.front);
          glStencilFuncSeparate(GL_BACK,
                                back_func,
                                back_ref,
                                cmd->stencil_compare_mask.back);
          break;
          }
        case NGF_DYNAMIC_STATE_STENCIL_REFERENCE: {
          GLint back_func, front_func, front_mask, back_mask;
          glGetIntegerv(GL_STENCIL_BACK_FUNC, &back_func);
          glGetIntegerv(GL_STENCIL_FUNC, &front_func);
          glGetIntegerv(GL_STENCIL_BACK_VALUE_MASK, &back_mask);
          glGetIntegerv(GL_STENCIL_VALUE_MASK, &front_mask);
          glStencilFuncSeparate(GL_FRONT,
                                front_func,
                                cmd->stencil_reference.front,
                                front_mask);
          glStencilFuncSeparate(GL_BACK,
                                back_func,
                                cmd->stencil_reference.back,
                                back_mask);         
          break;
          }
        }
      }

      // Bind resources.
      for (size_t i = 0; i < subop->ndescriptor_set_bind_ops; ++i) {
        const ngf_descriptor_set_bind_op *descriptor_set_bind_op =
            &(subop->descriptor_set_bind_ops[i]);
        if (descriptor_set_bind_op->slot >
            pipeline->layout->info.ndescriptors_layouts) {
          return NGF_ERROR_INVALID_RESOURCE_SET_BINDING;
        }
        const ngf_descriptors_layout *descriptors_layout =
            pipeline->layout->info.descriptors_layouts[descriptor_set_bind_op->slot];
        const ngf_descriptor_set *set = descriptor_set_bind_op->set;
        for (size_t j = 0; j < set->nslots; ++j) {
          const ngf_descriptor_write *rbop = &(set->bind_ops[j]);
          uint32_t binding_index = set->target_bindings[j];
          // TODO: assert compatibility w/ descriptor set layout?
          switch (rbop->type) {
          case NGF_DESCRIPTOR_UNIFORM_BUFFER: {
            const ngf_descriptor_write_buffer *buf_bind_op =
                &(rbop->op.buffer_bind);
            glBindBufferRange(
              GL_UNIFORM_BUFFER,
              binding_index,
              buf_bind_op->buffer->glbuffer,
              buf_bind_op->offset,
              buf_bind_op->range);
            break;
            }
          case NGF_DESCRIPTOR_LOADSTORE_IMAGE: {
            const ngf_descriptor_write_image_sampler *img_bind_op =
                &(rbop->op.image_sampler_bind);
            glBindImageTexture(binding_index,
                               img_bind_op->image_subresource.image->glimage,
                               img_bind_op->image_subresource.mip_level,
                               img_bind_op->image_subresource.layered,
                               img_bind_op->image_subresource.layer,
                               GL_READ_ONLY, // TODO: fix
                               GL_RGB8); // TODO: fix
            break;
            }
          case NGF_DESCRIPTOR_TEXTURE: {
            const ngf_descriptor_write_image_sampler *img_bind_op =
                &(rbop->op.image_sampler_bind);
            glActiveTexture(binding_index);
            glBindTexture(img_bind_op->image_subresource.image->bind_point,
                          img_bind_op->image_subresource.image->glimage);
            break;
            }
          case NGF_DESCRIPTOR_SAMPLER: {
            const ngf_descriptor_write_image_sampler *img_bind_op =
                &(rbop->op.image_sampler_bind);
            glBindSampler(binding_index, img_bind_op->sampler->glsampler);
            break;
            }
          case NGF_DESCRIPTOR_TEXTURE_AND_SAMPLER: {
            const ngf_descriptor_write_image_sampler *img_bind_op =
                &(rbop->op.image_sampler_bind);
            glActiveTexture(GL_TEXTURE0 + binding_index);
            glBindTexture(img_bind_op->image_subresource.image->bind_point,
                          img_bind_op->image_subresource.image->glimage);
            glBindSampler(binding_index, img_bind_op->sampler->glsampler); 
            break;
            }
          default:
            assert(0);
          }
        }
      }

      for (uint32_t b = 0; b < subop->nvertex_buf_bind_ops; ++b) {
        const ngf_vertex_buf_bind_op *op = &(subop->vertex_buf_bind_ops[b]);
        GLsizei stride = 0;
        bool found_binding = false;
        for (uint32_t b = 0;
             !found_binding && b < pipeline->nvert_buf_bindings;
             ++b) {
          if (pipeline->vert_buf_bindings[b].binding == op->binding) {
            stride = pipeline->vert_buf_bindings[b].stride;
            found_binding = true;
          }
        }
        assert(found_binding);
        glBindVertexBuffer(op->binding,
                           op->buffer->glbuffer,
                           op->offset,
                           stride); 
      }

      if (subop->nelements > 0u) {
        if (subop->mode == NGF_DRAW_MODE_DIRECT) {
          glDrawArrays(PIPELINE_CACHE.primitive_type, subop->first_element,
                       subop->nelements);
        } else if (subop->mode == NGF_DRAW_MODE_DIRECT_INSTANCED) {
          glDrawArraysInstanced(PIPELINE_CACHE.primitive_type,
                                subop->first_element,
                                subop->nelements,
                                subop->ninstances);
        } else if (subop->mode == NGF_DRAW_MODE_INDEXED) {
          glBindBuffer(subop->index_buf_bind_op.buffer->bind_point,
                       subop->index_buf_bind_op.buffer->glbuffer);
          glDrawElements(PIPELINE_CACHE.primitive_type,
                         subop->nelements,
                         gl_type(subop->index_buf_bind_op.type),
                         (void*)(uintptr_t)subop->first_element);
        } else if (subop->mode == NGF_DRAW_MODE_INDEXED_INSTANCED) {
          glBindBuffer(subop->index_buf_bind_op.buffer->bind_point,
                       subop->index_buf_bind_op.buffer->glbuffer);
          glDrawElementsInstanced(PIPELINE_CACHE.primitive_type,
                                  subop->nelements,
                                  gl_type(subop->index_buf_bind_op.type),
                                  (void*)(uintptr_t)subop->first_element,
                                  subop->ninstances);
        }
      }
    }
  }
  return NGF_ERROR_OK;
}

void ngf_finish() {
  glFlush();
  glFinish();
}

void ngf_debug_message_callback(void *userdata,
                                void(*callback)(const char*, const void*)) {

  glDebugMessageControl(GL_DONT_CARE,
                        GL_DONT_CARE,
                        GL_DONT_CARE,
                        0,
                        NULL,
                        GL_TRUE);
  glEnable(GL_DEBUG_OUTPUT);
  NGF_DEBUG_CALLBACK = callback;
  NGF_DEBUG_USERDATA = userdata;
  glDebugMessageCallback(ngf_gl_debug_callback, userdata);
}

void ngf_insert_log_message(const char *message) {
  glDebugMessageInsert(
    GL_DEBUG_SOURCE_APPLICATION,
    GL_DEBUG_TYPE_MARKER,
    0,
    GL_DEBUG_SEVERITY_NOTIFICATION,
    strlen(message),
    message);
}

void ngf_begin_debug_group(const char *title) {
  glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, strlen(title), title);
}

void ngf_end_debug_group() {
  glPopDebugGroup();
}

ngf_error ngf_begin_frame(ngf_context *ctx) { return NGF_ERROR_OK; }

ngf_error ngf_end_frame(ngf_context *ctx) {
  return eglSwapBuffers(ctx->dpy, ctx->surface)
      ? NGF_ERROR_OK
      : NGF_ERROR_END_FRAME_FAILED;
}
