/**
Copyright � 2018 nicegraf contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the �Software�), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED �AS IS�, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "nicegraf_util.h"
#include "nicegraf_internal.h"

#include <assert.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#pragma comment(lib, "ws2_32.lib")
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

void ngf_util_create_default_graphics_pipeline_data(
    ngf_pipeline_layout *layout,
    const ngf_irect2d *window_size,
    ngf_util_graphics_pipeline_data *result) {
  ngf_blend_info bi = {
    .enable = false,
    .sfactor = NGF_BLEND_FACTOR_ONE,
    .dfactor = NGF_BLEND_FACTOR_ZERO
  };
  result->blend_info = bi;
  ngf_stencil_info default_stencil = {
    .fail_op = NGF_STENCIL_OP_KEEP,
    .pass_op = NGF_STENCIL_OP_KEEP,
    .depth_fail_op = NGF_STENCIL_OP_KEEP,
    .compare_op = NGF_EQUAL,
    .compare_mask = 0,
    .write_mask = 0,
    .reference = 0 
  };
  ngf_depth_stencil_info dsi = {
    .min_depth = 0.0f,
    .max_depth = 1.0f,
    .stencil_test = false,
    .depth_test = false,
    .depth_write = false,
    .depth_compare = NGF_LESS,
    .front_stencil = default_stencil,
    .back_stencil = default_stencil
  };
  result->depth_stencil_info = dsi;
  ngf_vertex_input_info vii = {
    .nattribs = 0,
    .nvert_buf_bindings = 0
  };
  result->vertex_input_info = vii;
  ngf_multisample_info msi = {
    .multisample = false,
    .alpha_to_coverage = false
  };
  result->multisample_info = msi;
  ngf_rasterization_info ri = {
    .cull_mode = NGF_CULL_MODE_BACK,
    .discard = false,
    .front_face = NGF_FRONT_FACE_COUNTER_CLOCKWISE,
    .line_width = 1.0f,
    .polygon_mode = NGF_POLYGON_MODE_FILL
  };
  result->rasterization_info = ri;
  ngf_tessellation_info ti = {
    .patch_vertices = 0u
  };
  result->tessellation_info = ti;
  result->scissor = result->viewport = *window_size;
  ngf_graphics_pipeline_info gpi = {
    .blend = &result->blend_info,
    .depth_stencil = &result->depth_stencil_info,
    .dynamic_state_mask = 0u,
    .input_info = &result->vertex_input_info,
    .primitive_type = NGF_PRIMITIVE_TYPE_TRIANGLE_LIST,
    .multisample = &result->multisample_info,
    .shader_stages = {NULL},
    .nshader_stages = 0u,
    .rasterization = &result->rasterization_info,
    .layout = layout,
    .scissor = &result->scissor,
    .viewport = &result->viewport,
    .tessellation = &result->tessellation_info
  };
  result->pipeline_info = gpi;
}

ngf_error ngf_util_create_simple_layout_data(
    ngf_descriptor_info *desc,
    uint32_t ndesc,
    ngf_util_layout_data *result) {
  assert(desc);
  assert(result);
  ngf_error err = NGF_ERROR_OK;
  ngf_descriptors_layout_info ds_layout_info = {
    .descriptors = desc,
    .ndescriptors = ndesc
  };
  result->ndescriptors_layouts = 1u;
  result->descriptors_layouts = NGF_ALLOC(ngf_descriptors_layout*);
  err = ngf_create_descriptors_layout(&ds_layout_info,
                                      &(result->descriptors_layouts[0]));
  if (err != NGF_ERROR_OK) return err;
  ngf_pipeline_layout_info pipeline_layout_info = {
    .ndescriptors_layouts = 1u,
    .descriptors_layouts = &result->descriptors_layouts[0]
  };
  err = ngf_create_pipeline_layout(&pipeline_layout_info,
                                   &result->pipeline_layout);
  if (err != NGF_ERROR_OK) {
    ngf_destroy_descriptors_layout(result->descriptors_layouts[0]);
    NGF_FREE(result->descriptors_layouts);
    return err;
  }
  return err;
}

ngf_error ngf_util_create_layout_data(uint32_t **stage_layouts,
                                      uint32_t nstages,
                                      ngf_util_layout_data *result) {
  assert(stage_layouts);
  assert(result); 
  result->pipeline_layout = NULL;
  result->descriptors_layouts = NULL;
  result->ndescriptors_layouts = 0u;
  ngf_descriptors_layout_info *descriptor_set_layouts = NULL;
  uint32_t *descriptor_count_estimates = NULL;
  ngf_error err = NGF_ERROR_OK;
  
  uint32_t set_count = 0u;
  for (uint32_t i = 0u; i < nstages; ++i)
    set_count = NGF_MAX(set_count, ntohl(stage_layouts[i][0]));

  descriptor_set_layouts = NGF_ALLOCN(ngf_descriptors_layout_info, set_count);
  descriptor_count_estimates = NGF_ALLOCN(uint32_t, set_count);
  if (descriptor_set_layouts == NULL || descriptor_count_estimates == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_util_create_layout_data_cleanup;
  }
  memset(descriptor_count_estimates, 0, sizeof(uint32_t) * set_count);
  memset(descriptor_set_layouts, 0,
         sizeof(ngf_descriptors_layout_info) * set_count);

  for (uint32_t i = 0u; i < nstages; ++i) {
    uint32_t nres = ntohl(stage_layouts[i][1]);
    for (uint32_t r = 0u; r < nres; ++r) {
      uint32_t set = ntohl(stage_layouts[i][2 + 3 * r]);
      descriptor_count_estimates[set]++;
    }
  }

  for (uint32_t s = 0u; s < set_count; ++s) {
    descriptor_set_layouts[s].descriptors =
      NGF_ALLOCN(ngf_descriptor_info, descriptor_count_estimates[s]);
    if (descriptor_set_layouts[s].descriptors == NULL) {
      err = NGF_ERROR_OUTOFMEM;
      goto ngf_util_create_layout_data_cleanup;
    }
  }

  for (uint32_t i = 0u; i < nstages; ++i) {
    uint32_t nres = ntohl(stage_layouts[i][1]);
    for (uint32_t r = 0u; r < nres; ++r) {
      uint32_t set = ntohl(stage_layouts[i][2 + 3 * r + 0u]);
      uint32_t type = ntohl(stage_layouts[i][2 + 3 * r + 1u]);
      uint32_t binding = ntohl(stage_layouts[i][2 + 3 * r + 2u]);
      ngf_descriptors_layout_info *set_layout = &descriptor_set_layouts[set];
      bool found = false;
      for (uint32_t d = 0u; !found && d < set_layout->ndescriptors; ++d) {
        if (set_layout->descriptors[d].type == type &&
            set_layout->descriptors[d].id == binding) {
          found = true; 
        }
      }
      if (!found) {
        assert(set_layout->ndescriptors < descriptor_count_estimates[set]);
        uint32_t d = set_layout->ndescriptors++;
        set_layout->descriptors[d].type = type;
        set_layout->descriptors[d].id = binding;
      }
    }
  }

  result->ndescriptors_layouts = set_count;
  result->descriptors_layouts = NGF_ALLOCN(ngf_descriptors_layout*, set_count);
  if (result->descriptors_layouts == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_util_create_layout_data_cleanup;
  }

  for (uint32_t s = 0u; s < set_count; ++s) {
    err = ngf_create_descriptors_layout(&descriptor_set_layouts[s],
                                        &result->descriptors_layouts[s]);
    if (err != NGF_ERROR_OK) goto ngf_util_create_layout_data_cleanup;
  }

  ngf_pipeline_layout_info pipeline_layout_info;
  pipeline_layout_info.ndescriptors_layouts = set_count;
  pipeline_layout_info.descriptors_layouts = result->descriptors_layouts;
  err = ngf_create_pipeline_layout(&pipeline_layout_info,
                                   &result->pipeline_layout);
  if (err != NGF_ERROR_OK) goto ngf_util_create_layout_data_cleanup;

ngf_util_create_layout_data_cleanup:
  if (err != NGF_ERROR_OK) {
    ngf_destroy_pipeline_layout(result->pipeline_layout);
    for (uint32_t i = 0u; i < result->ndescriptors_layouts; ++i) 
      ngf_destroy_descriptors_layout(result->descriptors_layouts[i]);
  }
  for (uint32_t i = 0u;
      descriptor_count_estimates != NULL &&
        descriptor_set_layouts != NULL &&
        i < set_count;
       ++i) {
    NGF_FREEN(descriptor_set_layouts->descriptors,
              descriptor_count_estimates[i]);
  }
  NGF_FREEN(descriptor_set_layouts, set_count);
  NGF_FREEN(descriptor_count_estimates, set_count);
  return err;
}
