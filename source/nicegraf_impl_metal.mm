/**
Copyright © 2018 nicegraf contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the Software), to deal
in the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED AS IS, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "nicegraf.h"
#include "nicegraf_internal.h"
#include <new>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#if defined(TARGET_OS_MAC)
#import <AppKit/AppKit.h>
#define _NGF_VIEW_TYPE NSView
#elif defined(TARGET_OS_IPHONE)
#import <UIKit/UIKit.h>
#define _NGF_VIEW_TYPE UIView
#endif

id<MTLDevice> MTL_DEVICE = nil;

struct ngf_context {
  id<MTLDevice> device = nil;
  CAMetalLayer *layer = nil;
  id<MTLCommandQueue> queue = nil;
  bool is_current = false;
  ngf_swapchain_info swapchain_info;
};

NGF_THREADLOCAL ngf_context *CURRENT_CONTEXT = nullptr;

static MTLPixelFormat get_mtl_pixel_format(ngf_image_format fmt) {
  static const MTLPixelFormat pixel_format[NGF_IMAGE_FORMAT_UNDEFINED] = {
    MTLPixelFormatR8Unorm,
    MTLPixelFormatRG8Unorm,
    MTLPixelFormatInvalid, // RGB8, Metal does not support.
    MTLPixelFormatRGBA8Unorm,
    MTLPixelFormatInvalid, // RGB8, Metal does not support.
    MTLPixelFormatRGBA8Unorm_sRGB,
    MTLPixelFormatInvalid, // RGB8, Metal does not support.
    MTLPixelFormatBGRA8Unorm,
    MTLPixelFormatInvalid, // RGB8, Metal does not support.
    MTLPixelFormatBGRA8Unorm_sRGB,
    MTLPixelFormatR32Float,
    MTLPixelFormatRG32Float,
    MTLPixelFormatInvalid, // RGB32F, Metal does not support.
    MTLPixelFormatRGBA32Float,
    MTLPixelFormatR16Float,
    MTLPixelFormatRG16Float,
    MTLPixelFormatInvalid, // RGB16F, Metal does not support.
    MTLPixelFormatRGBA16Float,
    MTLPixelFormatDepth32Float,
    MTLPixelFormatDepth16Unorm,
    MTLPixelFormatDepth24Unorm_Stencil8,
  };
  return fmt >= NGF_IMAGE_FORMAT_UNDEFINED
             ? MTLPixelFormatInvalid
             : pixel_format[fmt];
}

ngf_error ngf_initialize(ngf_device_preference dev_pref) {
  id<NSObject> dev_observer = nil;
  const NSArray<id<MTLDevice>> *devices =
      MTLCopyAllDevicesWithObserver(&dev_observer,
                                    ^(id<MTLDevice> d,
                                      MTLDeviceNotificationName n){});
  bool found_device = false;
  for (uint32_t d = 0u; !found_device && d < devices.count; ++d) {
    MTL_DEVICE = devices[d++];
    found_device = (dev_pref != NGF_DEVICE_PREFERENCE_DISCRETE) ||
                   !MTL_DEVICE.lowPower;
  }

  return found_device ? NGF_ERROR_OK : NGF_ERROR_INITIALIZATION_FAILED;
}

CAMetalLayer* _ngf_create_swapchain(ngf_swapchain_info &swapchain_info,
                                   id<MTLDevice> device) {
  CAMetalLayer *layer = [CAMetalLayer layer];
  layer.device = device;
  CGSize size;
  size.width = swapchain_info.width;
  size.height = swapchain_info.height;
  layer.drawableSize = size; 
  layer.pixelFormat = get_mtl_pixel_format(swapchain_info.cfmt);
  if (@available(macOS 10.13.2, *)) {
    layer.maximumDrawableCount = swapchain_info.capacity_hint;
  }
  if (@available(macOS 10.13, *)) {
    layer.displaySyncEnabled =
          (swapchain_info.present_mode == NGF_PRESENTATION_MODE_IMMEDIATE);
  }
  _NGF_VIEW_TYPE *view=
        CFBridgingRelease((void*)swapchain_info.native_handle);
  [view setLayer:layer];
  swapchain_info.native_handle = (uintptr_t)(CFBridgingRetain(view));

  return layer;
}

ngf_error ngf_create_context(const ngf_context_info *info,
                             ngf_context **result) {
  *result = NGF_ALLOC(ngf_context);
  ngf_context *ctx = *result;
  if (ctx == nullptr) {
    return NGF_ERROR_OUTOFMEM;
  }
  new(ctx) ngf_context();

  ctx->device = MTL_DEVICE;
  if (info->shared_context != nullptr) {
    ctx->queue = info->shared_context->queue;
  } else {
    ctx->queue = [ctx->device newCommandQueue];
  }

  if (info->swapchain_info) {
    ctx->swapchain_info = *(info->swapchain_info);
    ctx->layer = _ngf_create_swapchain(ctx->swapchain_info, ctx->device);
  }
  
  return NGF_ERROR_OK;
}

void ngf_destroy_context(ngf_context *ctx) {
  // TODO: unset current context
  ctx->~ngf_context();
  NGF_FREE(ctx);
}

ngf_error ngf_resize_context(ngf_context *ctx,
                             uint32_t new_width,
                             uint32_t new_height) {
  ctx->swapchain_info.width = new_width;
  ctx->swapchain_info.height = new_height;
  ctx->layer = _ngf_create_swapchain(ctx->swapchain_info, ctx->device);
  return NGF_ERROR_OK;
}

ngf_error ngf_set_context(ngf_context *ctx) {
  if(CURRENT_CONTEXT != NULL) {
    return NGF_ERROR_CALLER_HAS_CURRENT_CONTEXT;
  } else if (ctx->is_current) {
    return NGF_ERROR_CONTEXT_ALREADY_CURRENT;
  }
  CURRENT_CONTEXT = ctx;
  ctx->is_current = true;
  return NGF_ERROR_OK;
}
