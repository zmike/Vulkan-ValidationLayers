/* Copyright (c) 2015-2016 The Khronos Group Inc.
 * Copyright (c) 2015-2016 Valve Corporation
 * Copyright (c) 2015-2016 LunarG, Inc.
 * Copyright (C) 2015-2016 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Author: Courtney Goeltzenleuchter <courtneygo@google.com>
 * Author: Tobin Ehlis <tobine@google.com>
 * Author: Chris Forbes <chrisf@ijw.co.nz>
 * Author: Mark Lobodzinski <mark@lunarg.com>
 */
#ifndef CORE_VALIDATION_ERROR_ENUMS_H_
#define CORE_VALIDATION_ERROR_ENUMS_H_

// Mem Tracker ERROR codes
enum MEM_TRACK_ERROR {
    MEMTRACK_NONE,
    MEMTRACK_INVALID_CB,
    MEMTRACK_INVALID_MEM_OBJ,
    MEMTRACK_INVALID_ALIASING,
    MEMTRACK_INTERNAL_ERROR,
    MEMTRACK_FREED_MEM_REF,
    MEMTRACK_INVALID_OBJECT,
    MEMTRACK_MEMORY_LEAK,
    MEMTRACK_INVALID_STATE,
    MEMTRACK_RESET_CB_WHILE_IN_FLIGHT,
    MEMTRACK_INVALID_FENCE_STATE,
    MEMTRACK_REBIND_OBJECT,
    MEMTRACK_INVALID_USAGE_FLAG,
    MEMTRACK_INVALID_MAP,
    MEMTRACK_INVALID_MEM_TYPE,
    MEMTRACK_INVALID_MEM_REGION,
    MEMTRACK_OBJECT_NOT_BOUND,
};

// Draw State ERROR codes
enum DRAW_STATE_ERROR {
    DRAWSTATE_NONE,
    DRAWSTATE_INTERNAL_ERROR,
    DRAWSTATE_NO_PIPELINE_BOUND,
    DRAWSTATE_INVALID_SET,
    DRAWSTATE_INVALID_RENDER_AREA,
    DRAWSTATE_INVALID_LAYOUT,
    DRAWSTATE_INVALID_IMAGE_LAYOUT,
    DRAWSTATE_INVALID_PIPELINE,
    DRAWSTATE_INVALID_PIPELINE_CREATE_STATE,
    DRAWSTATE_INVALID_COMMAND_BUFFER,
    DRAWSTATE_INVALID_BARRIER,
    DRAWSTATE_INVALID_BUFFER,
    DRAWSTATE_INVALID_IMAGE,
    DRAWSTATE_INVALID_BUFFER_VIEW,
    DRAWSTATE_INVALID_IMAGE_VIEW,
    DRAWSTATE_INVALID_QUERY,
    DRAWSTATE_INVALID_QUERY_POOL,
    DRAWSTATE_INVALID_DESCRIPTOR_POOL,
    DRAWSTATE_INVALID_COMMAND_POOL,
    DRAWSTATE_INVALID_FENCE,
    DRAWSTATE_INVALID_EVENT,
    DRAWSTATE_INVALID_SAMPLER,
    DRAWSTATE_INVALID_FRAMEBUFFER,
    DRAWSTATE_INVALID_DEVICE_MEMORY,
    DRAWSTATE_VTX_INDEX_OUT_OF_BOUNDS,
    DRAWSTATE_VTX_INDEX_ALIGNMENT_ERROR,
    DRAWSTATE_OUT_OF_MEMORY,
    DRAWSTATE_INVALID_DESCRIPTOR_SET,
    DRAWSTATE_DESCRIPTOR_TYPE_MISMATCH,
    DRAWSTATE_DESCRIPTOR_STAGEFLAGS_MISMATCH,
    DRAWSTATE_DESCRIPTOR_UPDATE_OUT_OF_BOUNDS,
    DRAWSTATE_DESCRIPTOR_POOL_EMPTY,
    DRAWSTATE_CANT_FREE_FROM_NON_FREE_POOL,
    DRAWSTATE_INVALID_WRITE_UPDATE,
    DRAWSTATE_INVALID_COPY_UPDATE,
    DRAWSTATE_INVALID_UPDATE_STRUCT,
    DRAWSTATE_NUM_SAMPLES_MISMATCH,
    DRAWSTATE_NO_END_COMMAND_BUFFER,
    DRAWSTATE_NO_BEGIN_COMMAND_BUFFER,
    DRAWSTATE_COMMAND_BUFFER_SINGLE_SUBMIT_VIOLATION,
    DRAWSTATE_INVALID_SECONDARY_COMMAND_BUFFER,
    DRAWSTATE_VIEWPORT_NOT_BOUND,
    DRAWSTATE_SCISSOR_NOT_BOUND,
    DRAWSTATE_LINE_WIDTH_NOT_BOUND,
    DRAWSTATE_DEPTH_BIAS_NOT_BOUND,
    DRAWSTATE_BLEND_NOT_BOUND,
    DRAWSTATE_DEPTH_BOUNDS_NOT_BOUND,
    DRAWSTATE_STENCIL_NOT_BOUND,
    DRAWSTATE_INDEX_BUFFER_NOT_BOUND,
    DRAWSTATE_PIPELINE_LAYOUTS_INCOMPATIBLE,
    DRAWSTATE_RENDERPASS_INCOMPATIBLE,
    DRAWSTATE_FRAMEBUFFER_INCOMPATIBLE,
    DRAWSTATE_INVALID_FRAMEBUFFER_CREATE_INFO,
    DRAWSTATE_INVALID_RENDERPASS,
    DRAWSTATE_INVALID_RENDERPASS_CMD,
    DRAWSTATE_NO_ACTIVE_RENDERPASS,
    DRAWSTATE_INVALID_IMAGE_USAGE,
    DRAWSTATE_INVALID_ATTACHMENT_INDEX,
    DRAWSTATE_DESCRIPTOR_SET_NOT_UPDATED,
    DRAWSTATE_DESCRIPTOR_SET_NOT_BOUND,
    DRAWSTATE_INVALID_DYNAMIC_OFFSET_COUNT,
    DRAWSTATE_CLEAR_CMD_BEFORE_DRAW,
    DRAWSTATE_BEGIN_CB_INVALID_STATE,
    DRAWSTATE_INVALID_CB_SIMULTANEOUS_USE,
    DRAWSTATE_INVALID_COMMAND_BUFFER_RESET,
    DRAWSTATE_VIEWPORT_SCISSOR_MISMATCH,
    DRAWSTATE_INVALID_IMAGE_ASPECT,
    DRAWSTATE_MISSING_ATTACHMENT_REFERENCE,
    DRAWSTATE_SAMPLER_DESCRIPTOR_ERROR,
    DRAWSTATE_INCONSISTENT_IMMUTABLE_SAMPLER_UPDATE,
    DRAWSTATE_IMAGEVIEW_DESCRIPTOR_ERROR,
    DRAWSTATE_BUFFERVIEW_DESCRIPTOR_ERROR,
    DRAWSTATE_BUFFERINFO_DESCRIPTOR_ERROR,
    DRAWSTATE_DYNAMIC_OFFSET_OVERFLOW,
    DRAWSTATE_DOUBLE_DESTROY,
    DRAWSTATE_OBJECT_INUSE,
    DRAWSTATE_QUEUE_FORWARD_PROGRESS,
    DRAWSTATE_INVALID_BUFFER_MEMORY_OFFSET,
    DRAWSTATE_INVALID_TEXEL_BUFFER_OFFSET,
    DRAWSTATE_INVALID_UNIFORM_BUFFER_OFFSET,
    DRAWSTATE_INVALID_STORAGE_BUFFER_OFFSET,
    DRAWSTATE_INDEPENDENT_BLEND,
    DRAWSTATE_DISABLED_LOGIC_OP,
    DRAWSTATE_INVALID_QUEUE_INDEX,
    DRAWSTATE_INVALID_QUEUE_FAMILY,
    DRAWSTATE_IMAGE_TRANSFER_GRANULARITY,
    DRAWSTATE_PUSH_CONSTANTS_ERROR,
    DRAWSTATE_INVALID_SUBPASS_INDEX,
    DRAWSTATE_SWAPCHAIN_NO_SYNC_FOR_ACQUIRE,
    DRAWSTATE_SWAPCHAIN_INVALID_IMAGE,
    DRAWSTATE_SWAPCHAIN_IMAGE_NOT_ACQUIRED,
    DRAWSTATE_SWAPCHAIN_ALREADY_EXISTS,
    DRAWSTATE_SWAPCHAIN_WRONG_SURFACE,
    DRAWSTATE_SWAPCHAIN_CREATE_BEFORE_QUERY,
    DRAWSTATE_SWAPCHAIN_UNSUPPORTED_QUEUE,
    DRAWSTATE_SWAPCHAIN_BAD_IMAGE_COUNT,
    DRAWSTATE_SWAPCHAIN_BAD_EXTENTS,
    DRAWSTATE_SWAPCHAIN_BAD_PRE_TRANSFORM,
    DRAWSTATE_SWAPCHAIN_BAD_COMPOSITE_ALPHA,
    DRAWSTATE_SWAPCHAIN_BAD_LAYER_COUNT,
    DRAWSTATE_SWAPCHAIN_BAD_USAGE_FLAGS,
    DRAWSTATE_SWAPCHAIN_TOO_MANY_IMAGES,
    DRAWSTATE_SWAPCHAIN_BAD_PRESENT_MODE,
};

// Shader Checker ERROR codes
enum SHADER_CHECKER_ERROR {
    SHADER_CHECKER_NONE,
    SHADER_CHECKER_INTERFACE_TYPE_MISMATCH,
    SHADER_CHECKER_OUTPUT_NOT_CONSUMED,
    SHADER_CHECKER_INPUT_NOT_PRODUCED,
    SHADER_CHECKER_NON_SPIRV_SHADER,
    SHADER_CHECKER_INCONSISTENT_SPIRV,
    SHADER_CHECKER_UNKNOWN_STAGE,
    SHADER_CHECKER_INCONSISTENT_VI,
    SHADER_CHECKER_MISSING_DESCRIPTOR,
    SHADER_CHECKER_BAD_SPECIALIZATION,
    SHADER_CHECKER_MISSING_ENTRYPOINT,
    SHADER_CHECKER_PUSH_CONSTANT_OUT_OF_RANGE,
    SHADER_CHECKER_PUSH_CONSTANT_NOT_ACCESSIBLE_FROM_STAGE,
    SHADER_CHECKER_DESCRIPTOR_TYPE_MISMATCH,
    SHADER_CHECKER_DESCRIPTOR_NOT_ACCESSIBLE_FROM_STAGE,
    SHADER_CHECKER_FEATURE_NOT_ENABLED,
    SHADER_CHECKER_BAD_CAPABILITY,
    SHADER_CHECKER_MISSING_INPUT_ATTACHMENT,
    SHADER_CHECKER_INPUT_ATTACHMENT_TYPE_MISMATCH,
};

// Device Limits ERROR codes
enum DEV_LIMITS_ERROR {
    DEVLIMITS_NONE,
    DEVLIMITS_INVALID_INSTANCE,
    DEVLIMITS_INVALID_PHYSICAL_DEVICE,
    DEVLIMITS_MISSING_QUERY_COUNT,
    DEVLIMITS_MUST_QUERY_COUNT,
    DEVLIMITS_INVALID_FEATURE_REQUESTED,
    DEVLIMITS_COUNT_MISMATCH,
    DEVLIMITS_INVALID_QUEUE_CREATE_REQUEST,
};
#endif // CORE_VALIDATION_ERROR_ENUMS_H_
