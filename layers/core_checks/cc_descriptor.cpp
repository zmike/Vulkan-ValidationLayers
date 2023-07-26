/* Copyright (c) 2015-2023 The Khronos Group Inc.
 * Copyright (c) 2015-2023 Valve Corporation
 * Copyright (c) 2015-2023 LunarG, Inc.
 * Copyright (C) 2015-2023 Google Inc.
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
 */

#include <valarray>

#include "error_message/validation_error_enums.h"
#include "core_validation.h"
#include "state_tracker/descriptor_sets.h"
#include "cc_buffer_address.h"
#include "generated/spirv_grammar_helper.h"

using DescriptorSet = cvdescriptorset::DescriptorSet;
using DescriptorSetLayout = cvdescriptorset::DescriptorSetLayout;
using DescriptorSetLayoutDef = cvdescriptorset::DescriptorSetLayoutDef;
using DescriptorSetLayoutId = cvdescriptorset::DescriptorSetLayoutId;

template <typename DSLayoutBindingA, typename DSLayoutBindingB>
bool ImmutableSamplersAreEqual(const DSLayoutBindingA &b1, const DSLayoutBindingB &b2) {
    if (b1.pImmutableSamplers == b2.pImmutableSamplers) {
        return true;
    } else if (b1.pImmutableSamplers && b2.pImmutableSamplers) {
        if ((b1.descriptorType == b2.descriptorType) &&
            ((b1.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER) || (b1.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)) &&
            (b1.descriptorCount == b2.descriptorCount)) {
            for (uint32_t i = 0; i < b1.descriptorCount; ++i) {
                if (b1.pImmutableSamplers[i] != b2.pImmutableSamplers[i]) {
                    return false;
                }
            }
            return true;
        } else {
            return false;
        }
    } else {
        // One pointer is null, the other is not
        return false;
    }
}

// If our layout is compatible with bound_dsl, return true,
//  else return false and fill in error_msg will description of what causes incompatibility
bool CoreChecks::VerifySetLayoutCompatibility(const DescriptorSetLayout &layout_dsl, const DescriptorSetLayout &bound_dsl,
                                              std::string &error_msg) const {
    // Short circuit the detailed check.
    if (layout_dsl.IsCompatible(&bound_dsl)) return true;

    // Do a detailed compatibility check of this lhs def (referenced by layout_dsl), vs. the rhs (layout and def)
    // Should only be run if trivial accept has failed, and in that context should return false.
    VkDescriptorSetLayout layout_dsl_handle = layout_dsl.GetDescriptorSetLayout();
    VkDescriptorSetLayout bound_dsl_handle = bound_dsl.GetDescriptorSetLayout();
    DescriptorSetLayoutDef const *layout_ds_layout_def = layout_dsl.GetLayoutDef();
    DescriptorSetLayoutDef const *bound_ds_layout_def = bound_dsl.GetLayoutDef();

    // Check descriptor counts
    const auto bound_total_count = bound_ds_layout_def->GetTotalDescriptorCount();
    if (layout_ds_layout_def->GetTotalDescriptorCount() != bound_ds_layout_def->GetTotalDescriptorCount()) {
        std::stringstream error_str;
        error_str << report_data->FormatHandle(layout_dsl_handle) << " from pipeline layout has "
                  << layout_ds_layout_def->GetTotalDescriptorCount() << " total descriptors, but "
                  << report_data->FormatHandle(bound_dsl_handle) << ", which is bound, has " << bound_total_count
                  << " total descriptors.";
        error_msg = error_str.str();
        return false;  // trivial fail case
    }

    // Descriptor counts match so need to go through bindings one-by-one
    //  and verify that type and stageFlags match
    for (const auto &layout_binding : layout_ds_layout_def->GetBindings()) {
        const auto bound_binding = bound_ds_layout_def->GetBindingInfoFromBinding(layout_binding.binding);
        if (layout_binding.descriptorCount != bound_binding->descriptorCount) {
            std::stringstream error_str;
            error_str << "Binding " << layout_binding.binding << " for " << report_data->FormatHandle(layout_dsl_handle)
                      << " from pipeline layout has a descriptorCount of " << layout_binding.descriptorCount << " but binding "
                      << layout_binding.binding << " for " << report_data->FormatHandle(bound_dsl_handle)
                      << ", which is bound, has a descriptorCount of " << bound_binding->descriptorCount;
            error_msg = error_str.str();
            return false;
        } else if (layout_binding.descriptorType != bound_binding->descriptorType) {
            std::stringstream error_str;
            error_str << "Binding " << layout_binding.binding << " for " << report_data->FormatHandle(layout_dsl_handle)
                      << " from pipeline layout is type '" << string_VkDescriptorType(layout_binding.descriptorType)
                      << "' but binding " << layout_binding.binding << " for " << report_data->FormatHandle(bound_dsl_handle)
                      << ", which is bound, is type '" << string_VkDescriptorType(bound_binding->descriptorType) << "'";
            error_msg = error_str.str();
            return false;
        } else if (layout_binding.stageFlags != bound_binding->stageFlags) {
            std::stringstream error_str;
            error_str << "Binding " << layout_binding.binding << " for " << report_data->FormatHandle(layout_dsl_handle)
                      << " from pipeline layout has stageFlags " << string_VkShaderStageFlags(layout_binding.stageFlags)
                      << " but binding " << layout_binding.binding << " for " << report_data->FormatHandle(bound_dsl_handle)
                      << ", which is bound, has stageFlags " << string_VkShaderStageFlags(bound_binding->stageFlags);
            error_msg = error_str.str();
            return false;
        } else if (!ImmutableSamplersAreEqual(layout_binding, *bound_binding)) {
            error_msg = "Immutable samplers from binding " + std::to_string(layout_binding.binding) + " in pipeline layout " +
                        report_data->FormatHandle(layout_dsl_handle) +
                        " do not match the immutable samplers in the layout currently bound (" +
                        report_data->FormatHandle(bound_dsl_handle) + ")";
            return false;
        }
    }

    const auto &ds_layout_flags = layout_ds_layout_def->GetBindingFlags();
    const auto &bound_layout_flags = bound_ds_layout_def->GetBindingFlags();
    if (bound_layout_flags != ds_layout_flags) {
        std::stringstream error_str;
        assert(ds_layout_flags.size() == bound_layout_flags.size());
        size_t i;
        for (i = 0; i < ds_layout_flags.size(); i++) {
            if (ds_layout_flags[i] != bound_layout_flags[i]) break;
        }
        error_str << report_data->FormatHandle(layout_dsl_handle)
                  << " from pipeline layout does not have the same binding flags at binding " << i << " ( "
                  << string_VkDescriptorBindingFlags(ds_layout_flags[i]) << " ) as " << report_data->FormatHandle(bound_dsl_handle)
                  << " ( " << string_VkDescriptorBindingFlags(bound_layout_flags[i]) << " ), which is bound";
        error_msg = error_str.str();
        return false;
    }

    // No detailed check should succeed if the trivial check failed -- or the dictionary has failed somehow.
    bool compatible = true;
    assert(!compatible);
    return compatible;
}

// For given cvdescriptorset::DescriptorSet, verify that its Set is compatible w/ the setLayout corresponding to
// pipelineLayout[layoutIndex]
bool CoreChecks::VerifySetLayoutCompatibility(const cvdescriptorset::DescriptorSet &descriptor_set,
                                              const PIPELINE_LAYOUT_STATE &pipeline_layout, const uint32_t layoutIndex,
                                              std::string &errorMsg) const {
    auto num_sets = pipeline_layout.set_layouts.size();
    if (layoutIndex >= num_sets) {
        std::stringstream error_str;
        error_str << report_data->FormatHandle(pipeline_layout.layout()) << ") only contains " << num_sets
                  << " setLayouts corresponding to sets 0-" << num_sets - 1 << ", but you're attempting to bind set to index "
                  << layoutIndex;
        errorMsg = error_str.str();
        return false;
    }
    if (descriptor_set.IsPushDescriptor()) return true;
    const auto *layout_node = pipeline_layout.set_layouts[layoutIndex].get();
    if (layout_node) {
        return VerifySetLayoutCompatibility(*layout_node, *descriptor_set.GetLayout(), errorMsg);
    } else {
        // It's possible the DSL is null when creating a graphics pipeline library, in which case we can't verify compatibility
        // here.
        return true;
    }
}

bool CoreChecks::VerifySetLayoutCompatibility(const PIPELINE_LAYOUT_STATE &layout_a, const PIPELINE_LAYOUT_STATE &layout_b,
                                              std::string &error_msg) const {
    const uint32_t num_sets = static_cast<uint32_t>(std::min(layout_a.set_layouts.size(), layout_b.set_layouts.size()));
    for (uint32_t i = 0; i < num_sets; ++i) {
        const auto ds_a = layout_a.set_layouts[i];
        const auto ds_b = layout_b.set_layouts[i];
        if (ds_a && ds_b) {
            if (!VerifySetLayoutCompatibility(*ds_a, *ds_b, error_msg)) {
                return false;
            }
        }
    }
    return true;
}

bool CoreChecks::PreCallValidateCmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
                                                      VkPipelineLayout layout, uint32_t firstSet, uint32_t setCount,
                                                      const VkDescriptorSet *pDescriptorSets, uint32_t dynamicOffsetCount,
                                                      const uint32_t *pDynamicOffsets) const {
    auto cb_state = GetRead<CMD_BUFFER_STATE>(commandBuffer);
    assert(cb_state);
    bool skip = false;
    skip |= ValidateCmd(*cb_state, CMD_BINDDESCRIPTORSETS);
    // Track total count of dynamic descriptor types to make sure we have an offset for each one
    uint32_t total_dynamic_descriptors = 0;
    std::string error_string = "";

    auto pipeline_layout = Get<PIPELINE_LAYOUT_STATE>(layout);
    for (uint32_t set_idx = 0; set_idx < setCount; set_idx++) {
        auto descriptor_set = Get<cvdescriptorset::DescriptorSet>(pDescriptorSets[set_idx]);
        if (descriptor_set) {
            // Verify that set being bound is compatible with overlapping setLayout of pipelineLayout
            if (!VerifySetLayoutCompatibility(*descriptor_set, *pipeline_layout, set_idx + firstSet, error_string)) {
                skip |= LogError(pDescriptorSets[set_idx], "VUID-vkCmdBindDescriptorSets-pDescriptorSets-00358",
                                 "vkCmdBindDescriptorSets(): descriptorSet #%u being bound is not compatible with overlapping "
                                 "descriptorSetLayout at index %u of "
                                 "%s due to: %s.",
                                 set_idx, set_idx + firstSet, report_data->FormatHandle(layout).c_str(), error_string.c_str());
            }

            auto set_dynamic_descriptor_count = descriptor_set->GetDynamicDescriptorCount();
            if (set_dynamic_descriptor_count) {
                // First make sure we won't overstep bounds of pDynamicOffsets array
                if ((total_dynamic_descriptors + set_dynamic_descriptor_count) > dynamicOffsetCount) {
                    // Test/report this here, such that we don't run past the end of pDynamicOffsets in the else clause
                    skip |=
                        LogError(pDescriptorSets[set_idx], "VUID-vkCmdBindDescriptorSets-dynamicOffsetCount-00359",
                                 "vkCmdBindDescriptorSets(): descriptorSet #%u (%s) requires %u dynamicOffsets, but only %u "
                                 "dynamicOffsets are left in "
                                 "pDynamicOffsets array. There must be one dynamic offset for each dynamic descriptor being bound.",
                                 set_idx, report_data->FormatHandle(pDescriptorSets[set_idx]).c_str(),
                                 descriptor_set->GetDynamicDescriptorCount(), (dynamicOffsetCount - total_dynamic_descriptors));
                    // Set the number found to the maximum to prevent duplicate messages, or subsquent descriptor sets from
                    // testing against the "short tail" we're skipping below.
                    total_dynamic_descriptors = dynamicOffsetCount;
                } else {  // Validate dynamic offsets and Dynamic Offset Minimums
                    // offset for all sets (pDynamicOffsets)
                    uint32_t cur_dyn_offset = total_dynamic_descriptors;
                    // offset into this descriptor set
                    uint32_t set_dyn_offset = 0;
                    const auto &dsl = descriptor_set->GetLayout();
                    const auto binding_count = dsl->GetBindingCount();
                    const auto &limits = phys_dev_props.limits;
                    for (uint32_t i = 0; i < binding_count; i++) {
                        const auto *binding = dsl->GetDescriptorSetLayoutBindingPtrFromIndex(i);
                        // skip checking binding if not needed
                        if (cvdescriptorset::IsDynamicDescriptor(binding->descriptorType) == false) {
                            continue;
                        }

                        // If a descriptor set has only binding 0 and 2 the binding_index will be 0 and 2
                        const uint32_t binding_index = binding->binding;
                        const uint32_t descriptorCount = binding->descriptorCount;

                        // Need to loop through each descriptor count inside the binding
                        // if descriptorCount is zero the binding with a dynamic descriptor type does not count
                        for (uint32_t j = 0; j < descriptorCount; j++) {
                            const uint32_t offset = pDynamicOffsets[cur_dyn_offset];
                            if (offset == 0) {
                                // offset of zero is equivalent of not having the dynamic offset
                                cur_dyn_offset++;
                                set_dyn_offset++;
                                continue;
                            }

                            // Validate alignment with limit
                            if ((binding->descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) &&
                                (SafeModulo(offset, limits.minUniformBufferOffsetAlignment) != 0)) {
                                skip |= LogError(commandBuffer, "VUID-vkCmdBindDescriptorSets-pDynamicOffsets-01971",
                                                 "vkCmdBindDescriptorSets(): pDynamicOffsets[%u] is %u, but must be a multiple of "
                                                 "device limit minUniformBufferOffsetAlignment 0x%" PRIxLEAST64 ".",
                                                 cur_dyn_offset, offset, limits.minUniformBufferOffsetAlignment);
                            }
                            if ((binding->descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) &&
                                (SafeModulo(offset, limits.minStorageBufferOffsetAlignment) != 0)) {
                                skip |= LogError(commandBuffer, "VUID-vkCmdBindDescriptorSets-pDynamicOffsets-01972",
                                                 "vkCmdBindDescriptorSets(): pDynamicOffsets[%u] is %u, but must be a multiple of "
                                                 "device limit minStorageBufferOffsetAlignment 0x%" PRIxLEAST64 ".",
                                                 cur_dyn_offset, offset, limits.minStorageBufferOffsetAlignment);
                            }

                            auto *descriptor = descriptor_set->GetDescriptorFromDynamicOffsetIndex(set_dyn_offset);
                            assert(descriptor != nullptr);
                            // Currently only GeneralBuffer are dynamic and need to be checked
                            if (descriptor->GetClass() == cvdescriptorset::DescriptorClass::GeneralBuffer) {
                                const auto *buffer_descriptor = static_cast<const cvdescriptorset::BufferDescriptor *>(descriptor);
                                const VkDeviceSize bound_range = buffer_descriptor->GetRange();
                                const VkDeviceSize bound_offset = buffer_descriptor->GetOffset();
                                // NOTE: null / invalid buffers may show up here, errors are raised elsewhere for this.
                                auto buffer_state = buffer_descriptor->GetBufferState();

                                // Validate offset didn't go over buffer
                                if ((bound_range == VK_WHOLE_SIZE) && (offset > 0)) {
                                    const LogObjectList objlist(commandBuffer, pDescriptorSets[set_idx],
                                                                buffer_descriptor->GetBuffer());
                                    skip |=
                                        LogError(objlist, "VUID-vkCmdBindDescriptorSets-pDescriptorSets-06715",
                                                 "vkCmdBindDescriptorSets(): pDynamicOffsets[%u] is 0x%x, but must be zero since "
                                                 "the buffer descriptor's range is VK_WHOLE_SIZE in descriptorSet #%u binding #%u "
                                                 "descriptor[%u].",
                                                 cur_dyn_offset, offset, set_idx, binding_index, j);

                                } else if (buffer_state && (bound_range != VK_WHOLE_SIZE) &&
                                           ((offset + bound_range + bound_offset) > buffer_state->createInfo.size)) {
                                    const LogObjectList objlist(commandBuffer, pDescriptorSets[set_idx],
                                                                buffer_descriptor->GetBuffer());
                                    skip |=
                                        LogError(objlist, "VUID-vkCmdBindDescriptorSets-pDescriptorSets-01979",
                                                 "vkCmdBindDescriptorSets(): pDynamicOffsets[%u] is 0x%x which when added to the "
                                                 "buffer descriptor's range (0x%" PRIxLEAST64
                                                 ") is greater than the size of the buffer (0x%" PRIxLEAST64
                                                 ") in descriptorSet #%u binding #%u descriptor[%u].",
                                                 cur_dyn_offset, offset, bound_range, buffer_state->createInfo.size, set_idx,
                                                 binding_index, j);
                                }
                            }
                            cur_dyn_offset++;
                            set_dyn_offset++;
                        }  // descriptorCount loop
                    }      // bindingCount loop
                    // Keep running total of dynamic descriptor count to verify at the end
                    total_dynamic_descriptors += set_dynamic_descriptor_count;
                }
            }
            if (descriptor_set->GetPoolState()->createInfo.flags & VK_DESCRIPTOR_POOL_CREATE_HOST_ONLY_BIT_EXT) {
                const LogObjectList objlist(pDescriptorSets[set_idx], descriptor_set->GetPoolState()->Handle());
                skip |= LogError(objlist, "VUID-vkCmdBindDescriptorSets-pDescriptorSets-04616",
                                 "vkCmdBindDescriptorSets(): pDescriptorSets[%" PRIu32
                                 "] was allocated from a pool that was created with VK_DESCRIPTOR_POOL_CREATE_HOST_ONLY_BIT_EXT.",
                                 set_idx);
            }
            if (descriptor_set->GetLayout()->GetCreateFlags() & VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT) {
                const LogObjectList objlist(pDescriptorSets[set_idx], descriptor_set->GetLayout()->Handle());
                skip |= LogError(pDescriptorSets[set_idx], "VUID-vkCmdBindDescriptorSets-pDescriptorSets-08010",
                                 "vkCmdBindDescriptorSets(): pDescriptorSets[%" PRIu32
                                 "] was allocated with a VkDescriptorSetLayout created with the flag "
                                 "VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT set.",
                                 set_idx);
            }
        } else if (!enabled_features.graphics_pipeline_library_features.graphicsPipelineLibrary) {
            skip |= LogError(pDescriptorSets[set_idx], "VUID-vkCmdBindDescriptorSets-graphicsPipelineLibrary-06754",
                             "vkCmdBindDescriptorSets(): Attempt to bind pDescriptorSets[%" PRIu32
                             "] (%s) that does not exist, and the layout was not created "
                             "VK_PIPELINE_LAYOUT_CREATE_INDEPENDENT_SETS_BIT_EXT.",
                             set_idx, report_data->FormatHandle(pDescriptorSets[set_idx]).c_str());
        }
    }
    //  dynamicOffsetCount must equal the total number of dynamic descriptors in the sets being bound
    if (total_dynamic_descriptors != dynamicOffsetCount) {
        skip |= LogError(cb_state->commandBuffer(), "VUID-vkCmdBindDescriptorSets-dynamicOffsetCount-00359",
                         "vkCmdBindDescriptorSets(): Attempting to bind %u descriptorSets with %u dynamic descriptors, but "
                         "dynamicOffsetCount is %u. It should "
                         "exactly match the number of dynamic descriptors.",
                         setCount, total_dynamic_descriptors, dynamicOffsetCount);
    }
    // firstSet and descriptorSetCount sum must be less than setLayoutCount
    if ((firstSet + setCount) > static_cast<uint32_t>(pipeline_layout->set_layouts.size())) {
        skip |= LogError(cb_state->commandBuffer(), "VUID-vkCmdBindDescriptorSets-firstSet-00360",
                         "vkCmdBindDescriptorSets(): Sum of firstSet (%u) and descriptorSetCount (%u) is greater than "
                         "VkPipelineLayoutCreateInfo::setLayoutCount "
                         "(%zu) when pipeline layout was created",
                         firstSet, setCount, pipeline_layout->set_layouts.size());
    }

    static const std::map<VkPipelineBindPoint, std::string> bindpoint_errors = {
        std::make_pair(VK_PIPELINE_BIND_POINT_GRAPHICS, "VUID-vkCmdBindDescriptorSets-pipelineBindPoint-00361"),
        std::make_pair(VK_PIPELINE_BIND_POINT_COMPUTE, "VUID-vkCmdBindDescriptorSets-pipelineBindPoint-00361"),
        std::make_pair(VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, "VUID-vkCmdBindDescriptorSets-pipelineBindPoint-00361")};
    skip |= ValidatePipelineBindPoint(cb_state.get(), pipelineBindPoint, "vkCmdBindPipeline()", bindpoint_errors);

    return skip;
}

bool CoreChecks::PreCallValidateCreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
                                                          const VkAllocationCallbacks *pAllocator,
                                                          VkDescriptorSetLayout *pSetLayout) const {
    bool skip = false;
    vvl::unordered_set<uint32_t> bindings;
    uint64_t total_descriptors = 0;

    const auto *flags_pCreateInfo = LvlFindInChain<VkDescriptorSetLayoutBindingFlagsCreateInfo>(pCreateInfo->pNext);
    const bool push_descriptor_set = (pCreateInfo->flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR) != 0;

    uint32_t max_binding = 0;

    uint32_t update_after_bind = pCreateInfo->bindingCount;
    uint32_t uniform_buffer_dynamic = pCreateInfo->bindingCount;
    uint32_t storage_buffer_dynamic = pCreateInfo->bindingCount;

    for (uint32_t i = 0; i < pCreateInfo->bindingCount; ++i) {
        const auto &binding_info = pCreateInfo->pBindings[i];
        max_binding = std::max(max_binding, binding_info.binding);

        if (!bindings.insert(binding_info.binding).second) {
            skip |= LogError(device, "VUID-VkDescriptorSetLayoutCreateInfo-binding-00279",
                             "vkCreateDescriptorSetLayout(): pBindings[%u] has duplicated binding number (%u).", i,
                             binding_info.binding);
        }

        if (binding_info.descriptorType == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT) {
            if (!enabled_features.core13.inlineUniformBlock) {
                skip |= LogError(device, "VUID-VkDescriptorSetLayoutBinding-descriptorType-04604",
                                 "vkCreateDescriptorSetLayout(): pBindings[%u] is creating VkDescriptorSetLayout with "
                                 "descriptor type VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT "
                                 "but the inlineUniformBlock feature is not enabled",
                                 i);
            } else if (push_descriptor_set) {
                skip |= LogError(device, "VUID-VkDescriptorSetLayoutCreateInfo-flags-02208",
                                 "vkCreateDescriptorSetLayout(): pBindings[%u] is creating VkDescriptorSetLayout with descriptor "
                                 "type VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT but "
                                 "VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR flag is set",
                                 i);
            } else {
                if ((binding_info.descriptorCount % 4) != 0) {
                    skip |= LogError(device, "VUID-VkDescriptorSetLayoutBinding-descriptorType-02209",
                                     "vkCreateDescriptorSetLayout(): pBindings[%u] has descriptorCount =(%" PRIu32
                                     ") but must be a multiple of 4",
                                     i, binding_info.descriptorCount);
                }
                if ((binding_info.descriptorCount > phys_dev_ext_props.inline_uniform_block_props.maxInlineUniformBlockSize) &&
                    !(pCreateInfo->flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT)) {
                    skip |= LogError(device, "VUID-VkDescriptorSetLayoutBinding-descriptorType-08004",
                                     "vkCreateDescriptorSetLayout(): pBindings[%u] has descriptorCount =(%" PRIu32
                                     ") but must be less than or equal to maxInlineUniformBlockSize (%u) if the "
                                     "VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT flag is not set",
                                     i, binding_info.descriptorCount,
                                     phys_dev_ext_props.inline_uniform_block_props.maxInlineUniformBlockSize);
                }
            }
        } else if (binding_info.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
            uniform_buffer_dynamic = i;
            if (push_descriptor_set) {
                skip |= LogError(
                    device, "VUID-VkDescriptorSetLayoutCreateInfo-flags-00280",
                    "vkCreateDescriptorSetLayout(): pBindings[%u] is creating VkDescriptorSetLayout with descriptor type "
                    "VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT but VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC flag is set",
                    i);
            }
        } else if (binding_info.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
            storage_buffer_dynamic = i;
            if (push_descriptor_set) {
                skip |= LogError(
                    device, "VUID-VkDescriptorSetLayoutCreateInfo-flags-00280",
                    "vkCreateDescriptorSetLayout(): pBindings[%u] is creating VkDescriptorSetLayout with descriptor type "
                    "VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT but VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC flag is set",
                    i);
            }
        }

        if ((binding_info.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER ||
             binding_info.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) &&
            binding_info.pImmutableSamplers) {
            for (uint32_t j = 0; j < binding_info.descriptorCount; j++) {
                auto sampler_state = Get<SAMPLER_STATE>(binding_info.pImmutableSamplers[j]);
                if (sampler_state && (sampler_state->createInfo.borderColor == VK_BORDER_COLOR_INT_CUSTOM_EXT ||
                                      sampler_state->createInfo.borderColor == VK_BORDER_COLOR_FLOAT_CUSTOM_EXT)) {
                    skip |= LogError(device, "VUID-VkDescriptorSetLayoutBinding-pImmutableSamplers-04009",
                                     "vkCreateDescriptorSetLayout(): pBindings[%u].pImmutableSamplers[%u] has VkSampler %s"
                                     " presented as immutable has a custom border color",
                                     i, j, report_data->FormatHandle(binding_info.pImmutableSamplers[j]).c_str());
                }
            }
        }

        if (binding_info.descriptorType == VK_DESCRIPTOR_TYPE_MUTABLE_EXT && binding_info.pImmutableSamplers != nullptr) {
            skip |= LogError(device, "VUID-VkDescriptorSetLayoutBinding-descriptorType-04605",
                             "vkCreateDescriptorSetLayout(): pBindings[%u] has descriptorType "
                             "VK_DESCRIPTOR_TYPE_MUTABLE_EXT but pImmutableSamplers is not NULL.",
                             i);
        }

        if (pCreateInfo->flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_EMBEDDED_IMMUTABLE_SAMPLERS_BIT_EXT) {
            if (binding_info.descriptorType != VK_DESCRIPTOR_TYPE_SAMPLER) {
                skip |= LogError(device, "VUID-VkDescriptorSetLayoutBinding-flags-08005",
                                 "vkCreateDescriptorSetLayout(): pBindings[%u] has descriptorType "
                                 "not equal to %s but the "
                                 "VK_DESCRIPTOR_SET_LAYOUT_CREATE_EMBEDDED_IMMUTABLE_SAMPLERS_BIT_EXT flag is set.",
                                 i, string_VkDescriptorType(binding_info.descriptorType));
            }

            if (binding_info.descriptorCount > 1) {
                skip |= LogError(device, "VUID-VkDescriptorSetLayoutBinding-flags-08006",
                                 "vkCreateDescriptorSetLayout(): pBindings[%u] descriptorCount is %" PRIu32
                                 " but the "
                                 "VK_DESCRIPTOR_SET_LAYOUT_CREATE_EMBEDDED_IMMUTABLE_SAMPLERS_BIT_EXT flag is set.",
                                 i, binding_info.descriptorCount);
            }

            if ((binding_info.descriptorCount == 1) && (binding_info.pImmutableSamplers == nullptr)) {
                skip |= LogError(device, "VUID-VkDescriptorSetLayoutBinding-flags-08007",
                                 "vkCreateDescriptorSetLayout(): pBindings[%u] has a descriptorCount "
                                 "of 1 and the VK_DESCRIPTOR_SET_LAYOUT_CREATE_EMBEDDED_IMMUTABLE_SAMPLERS_BIT_EXT flag set, but "
                                 "pImmutableSamplers is NULL",
                                 i);
            }
        }

        total_descriptors += binding_info.descriptorCount;
    }

    if (flags_pCreateInfo) {
        if (flags_pCreateInfo->bindingCount != 0 && flags_pCreateInfo->bindingCount != pCreateInfo->bindingCount) {
            skip |= LogError(device, "VUID-VkDescriptorSetLayoutBindingFlagsCreateInfo-bindingCount-03002",
                             "vkCreateDescriptorSetLayout(): VkDescriptorSetLayoutCreateInfo::bindingCount (%d) != "
                             "VkDescriptorSetLayoutBindingFlagsCreateInfo::bindingCount (%d)",
                             pCreateInfo->bindingCount, flags_pCreateInfo->bindingCount);
        }

        if (flags_pCreateInfo->bindingCount == pCreateInfo->bindingCount) {
            for (uint32_t i = 0; i < pCreateInfo->bindingCount; ++i) {
                const auto &binding_info = pCreateInfo->pBindings[i];

                if (flags_pCreateInfo->pBindingFlags[i] & VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT) {
                    update_after_bind = i;
                    if ((pCreateInfo->flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT) == 0) {
                        skip |= LogError(
                            device, "VUID-VkDescriptorSetLayoutCreateInfo-flags-03000",
                            "vkCreateDescriptorSetLayout(): pBindings[%u] has VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT but the "
                            "set layout does not have the VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT flag set.",
                            i);
                    }

                    if (binding_info.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER &&
                        !enabled_features.core12.descriptorBindingUniformBufferUpdateAfterBind) {
                        skip |= LogError(
                            device,
                            "VUID-VkDescriptorSetLayoutBindingFlagsCreateInfo-"
                            "descriptorBindingUniformBufferUpdateAfterBind-03005",
                            "vkCreateDescriptorSetLayout(): pBindings[%u] can't have VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT "
                            "for %s since descriptorBindingUniformBufferUpdateAfterBind is not enabled.",
                            i, string_VkDescriptorType(binding_info.descriptorType));
                    }
                    if ((binding_info.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER ||
                         binding_info.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
                         binding_info.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) &&
                        !enabled_features.core12.descriptorBindingSampledImageUpdateAfterBind) {
                        skip |= LogError(
                            device,
                            "VUID-VkDescriptorSetLayoutBindingFlagsCreateInfo-"
                            "descriptorBindingSampledImageUpdateAfterBind-03006",
                            "vkCreateDescriptorSetLayout(): pBindings[%u] can't have VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT "
                            "for %s since descriptorBindingSampledImageUpdateAfterBind is not enabled.",
                            i, string_VkDescriptorType(binding_info.descriptorType));
                    }
                    if (binding_info.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE &&
                        !enabled_features.core12.descriptorBindingStorageImageUpdateAfterBind) {
                        skip |= LogError(
                            device,
                            "VUID-VkDescriptorSetLayoutBindingFlagsCreateInfo-"
                            "descriptorBindingStorageImageUpdateAfterBind-03007",
                            "vkCreateDescriptorSetLayout(): pBindings[%u] can't have VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT "
                            "for %s since descriptorBindingStorageImageUpdateAfterBind is not enabled.",
                            i, string_VkDescriptorType(binding_info.descriptorType));
                    }
                    if (binding_info.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER &&
                        !enabled_features.core12.descriptorBindingStorageBufferUpdateAfterBind) {
                        skip |= LogError(
                            device,
                            "VUID-VkDescriptorSetLayoutBindingFlagsCreateInfo-"
                            "descriptorBindingStorageBufferUpdateAfterBind-03008",
                            "vkCreateDescriptorSetLayout(): pBindings[%u] can't have VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT "
                            "for %s since descriptorBindingStorageBufferUpdateAfterBind is not enabled.",
                            i, string_VkDescriptorType(binding_info.descriptorType));
                    }
                    if (binding_info.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER &&
                        !enabled_features.core12.descriptorBindingUniformTexelBufferUpdateAfterBind) {
                        skip |= LogError(
                            device,
                            "VUID-VkDescriptorSetLayoutBindingFlagsCreateInfo-"
                            "descriptorBindingUniformTexelBufferUpdateAfterBind-03009",
                            "vkCreateDescriptorSetLayout(): pBindings[%u] can't have VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT "
                            "for %s since descriptorBindingUniformTexelBufferUpdateAfterBind is not enabled.",
                            i, string_VkDescriptorType(binding_info.descriptorType));
                    }
                    if (binding_info.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER &&
                        !enabled_features.core12.descriptorBindingStorageTexelBufferUpdateAfterBind) {
                        skip |= LogError(
                            device,
                            "VUID-VkDescriptorSetLayoutBindingFlagsCreateInfo-"
                            "descriptorBindingStorageTexelBufferUpdateAfterBind-03010",
                            "vkCreateDescriptorSetLayout(): pBindings[%u] can't have VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT "
                            "for %s since descriptorBindingStorageTexelBufferUpdateAfterBind is not enabled.",
                            i, string_VkDescriptorType(binding_info.descriptorType));
                    }
                    if ((binding_info.descriptorType == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT ||
                         binding_info.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
                         binding_info.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)) {
                        skip |= LogError(device, "VUID-VkDescriptorSetLayoutBindingFlagsCreateInfo-None-03011",
                                         "vkCreateDescriptorSetLayout(): pBindings[%u] can't have "
                                         "VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT for %s.",
                                         i, string_VkDescriptorType(binding_info.descriptorType));
                    }

                    if (binding_info.descriptorType == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT &&
                        !enabled_features.core13.descriptorBindingInlineUniformBlockUpdateAfterBind) {
                        skip |= LogError(
                            device,
                            "VUID-VkDescriptorSetLayoutBindingFlagsCreateInfo-"
                            "descriptorBindingInlineUniformBlockUpdateAfterBind-02211",
                            "vkCreateDescriptorSetLayout(): pBindings[%u] can't have VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT "
                            "for %s since descriptorBindingInlineUniformBlockUpdateAfterBind is not enabled.",
                            i, string_VkDescriptorType(binding_info.descriptorType));
                    }
                    if ((binding_info.descriptorType == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR ||
                         binding_info.descriptorType == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV) &&
                        !enabled_features.ray_tracing_acceleration_structure_features
                             .descriptorBindingAccelerationStructureUpdateAfterBind) {
                        skip |= LogError(device,
                                         "VUID-VkDescriptorSetLayoutBindingFlagsCreateInfo-"
                                         "descriptorBindingAccelerationStructureUpdateAfterBind-03570",
                                         "vkCreateDescriptorSetLayout(): pBindings[%" PRIu32
                                         "] can't have VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT "
                                         "for %s if "
                                         "VkPhysicalDeviceAccelerationStructureFeaturesKHR::"
                                         "descriptorBindingAccelerationStructureUpdateAfterBind is not enabled.",
                                         i, string_VkDescriptorType(binding_info.descriptorType));
                    }
                }

                if (flags_pCreateInfo->pBindingFlags[i] & VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT) {
                    if (!enabled_features.core12.descriptorBindingUpdateUnusedWhilePending) {
                        skip |= LogError(
                            device,
                            "VUID-VkDescriptorSetLayoutBindingFlagsCreateInfo-descriptorBindingUpdateUnusedWhilePending-03012",
                            "vkCreateDescriptorSetLayout(): pBindings[%u] can't have "
                            "VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT for %s since "
                            "descriptorBindingUpdateUnusedWhilePending is not enabled.",
                            i, string_VkDescriptorType(binding_info.descriptorType));
                    }
                }

                if (flags_pCreateInfo->pBindingFlags[i] & VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT) {
                    if (!enabled_features.core12.descriptorBindingPartiallyBound) {
                        skip |= LogError(
                            device, "VUID-VkDescriptorSetLayoutBindingFlagsCreateInfo-descriptorBindingPartiallyBound-03013",
                            "vkCreateDescriptorSetLayout(): pBindings[%u] can't have VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT for "
                            "%s since descriptorBindingPartiallyBound is not enabled.",
                            i, string_VkDescriptorType(binding_info.descriptorType));
                    }
                }

                if (flags_pCreateInfo->pBindingFlags[i] & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT) {
                    if (binding_info.binding != max_binding) {
                        skip |= LogError(
                            device, "VUID-VkDescriptorSetLayoutBindingFlagsCreateInfo-pBindingFlags-03004",
                            "vkCreateDescriptorSetLayout(): pBindings[%u] has VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT "
                            "but %u is the largest value of all the bindings.",
                            i, binding_info.binding);
                    }

                    if (!enabled_features.core12.descriptorBindingVariableDescriptorCount) {
                        skip |= LogError(
                            device,
                            "VUID-VkDescriptorSetLayoutBindingFlagsCreateInfo-descriptorBindingVariableDescriptorCount-03014",
                            "vkCreateDescriptorSetLayout(): pBindings[%u] can't have "
                            "VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT for %s since "
                            "descriptorBindingVariableDescriptorCount is not enabled.",
                            i, string_VkDescriptorType(binding_info.descriptorType));
                    }
                    if ((binding_info.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) ||
                        (binding_info.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)) {
                        skip |= LogError(device, "VUID-VkDescriptorSetLayoutBindingFlagsCreateInfo-pBindingFlags-03015",
                                         "vkCreateDescriptorSetLayout(): pBindings[%u] can't have "
                                         "VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT for %s.",
                                         i, string_VkDescriptorType(binding_info.descriptorType));
                    }
                }

                if (push_descriptor_set &&
                    (flags_pCreateInfo->pBindingFlags[i] &
                     (VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT |
                      VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT))) {
                    skip |= LogError(
                        device, "VUID-VkDescriptorSetLayoutBindingFlagsCreateInfo-flags-03003",
                        "vkCreateDescriptorSetLayout(): pBindings[%u] can't have VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT, "
                        "VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT, or "
                        "VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT for with "
                        "VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR.",
                        i);
                }
            }
        }
    }

    if (update_after_bind < pCreateInfo->bindingCount) {
        if (uniform_buffer_dynamic < pCreateInfo->bindingCount) {
            skip |= LogError(device, "VUID-VkDescriptorSetLayoutCreateInfo-descriptorType-03001",
                             "vkCreateDescriptorSetLayout(): binding (%" PRIi32
                             ") has VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT "
                             "flag, but binding (%" PRIi32 ") has descriptor type VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC.",
                             update_after_bind, uniform_buffer_dynamic);
        }
        if (storage_buffer_dynamic < pCreateInfo->bindingCount) {
            skip |= LogError(device, "VUID-VkDescriptorSetLayoutCreateInfo-descriptorType-03001",
                             "vkCreateDescriptorSetLayout(): binding (%" PRIi32
                             ") has VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT "
                             "flag, but binding (%" PRIi32 ") has descriptor type VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC.",
                             update_after_bind, storage_buffer_dynamic);
        }
    }

    if ((push_descriptor_set) && (total_descriptors > phys_dev_ext_props.push_descriptor_props.maxPushDescriptors)) {
        skip |=
            LogError(device, "VUID-VkDescriptorSetLayoutCreateInfo-flags-00281",
                     "vkCreateDescriptorSetLayout(): for push descriptor, total descriptor count in layout (%" PRIu64
                     ") must not be greater than VkPhysicalDevicePushDescriptorPropertiesKHR::maxPushDescriptors (%" PRIu32 ").",
                     total_descriptors, phys_dev_ext_props.push_descriptor_props.maxPushDescriptors);
    }

    return skip;
}

// Validate that the state of this set is appropriate for the given bindings and dynamic_offsets at Draw time
//  This includes validating that all descriptors in the given bindings are updated,
//  that any update buffers are valid, and that any dynamic offsets are within the bounds of their buffers.
// Return true if state is acceptable, or false and write an error message into error string
bool CoreChecks::ValidateDrawState(const DescriptorSet &descriptor_set, const BindingVariableMap &bindings,
                                   const std::vector<uint32_t> &dynamic_offsets, const CMD_BUFFER_STATE &cb_state,
                                   const char *caller, const DrawDispatchVuid &vuids) const {
    std::optional<vvl::unordered_map<VkImageView, VkImageLayout>> checked_layouts;
    if (descriptor_set.GetTotalDescriptorCount() > cvdescriptorset::PrefilterBindRequestMap::kManyDescriptors_) {
        checked_layouts.emplace();
    }
    bool result = false;
    VkFramebuffer framebuffer = cb_state.activeFramebuffer ? cb_state.activeFramebuffer->framebuffer() : VK_NULL_HANDLE;
    DescriptorContext context{caller, vuids, cb_state, descriptor_set, framebuffer, true, checked_layouts};

    for (const auto &binding_pair : bindings) {
        const auto *binding = descriptor_set.GetBinding(binding_pair.first);
        if (!binding) {  //  End at construction is the condition for an invalid binding.
            auto set = descriptor_set.GetSet();
            result |= LogError(set, vuids.descriptor_buffer_bit_set_08114,
                               "%s encountered the following validation error at %s time: Attempting to "
                               "validate DrawState for binding #%u  which is an invalid binding for this descriptor set.",
                               report_data->FormatHandle(set).c_str(), caller, binding_pair.first);
            return result;
        }

        if (binding->IsBindless()) {
            // Can't validate the descriptor because it may not have been updated,
            // or the view could have been destroyed
            continue;
        }
        result |= ValidateDescriptorSetBindingData(context, binding_pair, *binding);
    }
    return result;
}

template <typename T>
bool CoreChecks::ValidateDescriptors(const DescriptorContext &context, const DescriptorBindingInfo &binding_info,
                                     const T &binding) const {
    bool skip = false;
    for (uint32_t index = 0; !skip && index < binding.count; index++) {
        const auto &descriptor = binding.descriptors[index];

        if (!binding.updated[index]) {
            auto set = context.descriptor_set.GetSet();
            return LogError(set, context.vuids.descriptor_buffer_bit_set_08114,
                            "Descriptor set %s encountered the following validation error at %s time: Descriptor in binding "
                            "#%" PRIu32 " index %" PRIu32
                            " is being used in draw but has never been updated via vkUpdateDescriptorSets() or a similar call.",
                            report_data->FormatHandle(set).c_str(), context.caller, binding_info.first, index);
        }
        skip = ValidateDescriptor(context, binding_info, index, binding.type, descriptor);
    }
    return skip;
}

bool CoreChecks::ValidateDescriptorSetBindingData(const DescriptorContext &context, const DescriptorBindingInfo &binding_info,
                                                  const cvdescriptorset::DescriptorBinding &binding) const {
    using DescriptorClass = cvdescriptorset::DescriptorClass;
    bool skip = false;
    switch (binding.descriptor_class) {
        case DescriptorClass::InlineUniform:
            // Can't validate the descriptor because it may not have been updated.
            break;
        case DescriptorClass::GeneralBuffer:
            skip = ValidateDescriptors(context, binding_info, static_cast<const cvdescriptorset::BufferBinding &>(binding));
            break;
        case DescriptorClass::ImageSampler:
            skip = ValidateDescriptors(context, binding_info, static_cast<const cvdescriptorset::ImageSamplerBinding &>(binding));
            break;
        case DescriptorClass::Image:
            skip = ValidateDescriptors(context, binding_info, static_cast<const cvdescriptorset::ImageBinding &>(binding));
            break;
        case DescriptorClass::PlainSampler:
            skip = ValidateDescriptors(context, binding_info, static_cast<const cvdescriptorset::SamplerBinding &>(binding));
            break;
        case DescriptorClass::TexelBuffer:
            skip = ValidateDescriptors(context, binding_info, static_cast<const cvdescriptorset::TexelBinding &>(binding));
            break;
        case DescriptorClass::AccelerationStructure:
            skip = ValidateDescriptors(context, binding_info,
                                       static_cast<const cvdescriptorset::AccelerationStructureBinding &>(binding));
            break;
        default:
            break;
    }
    return skip;
}

bool CoreChecks::ValidateDescriptor(const DescriptorContext &context, const DescriptorBindingInfo &binding_info, uint32_t index,
                                    VkDescriptorType descriptor_type, const cvdescriptorset::BufferDescriptor &descriptor) const {
    // Verify that buffers are valid
    auto buffer = descriptor.GetBuffer();
    auto buffer_node = descriptor.GetBufferState();
    if ((!buffer_node && !enabled_features.robustness2_features.nullDescriptor) || (buffer_node && buffer_node->Destroyed())) {
        auto set = context.descriptor_set.GetSet();
        return LogError(set, context.vuids.descriptor_buffer_bit_set_08114,
                        "Descriptor set %s encountered the following validation error at %s time: Descriptor in "
                        "binding #%" PRIu32 " index %" PRIu32 " is using buffer %s that is invalid or has been destroyed.",
                        report_data->FormatHandle(set).c_str(), context.caller, binding_info.first, index,
                        report_data->FormatHandle(buffer).c_str());
    }
    if (buffer) {
        if (buffer_node /* && !buffer_node->sparse*/) {
            for (const auto &binding : buffer_node->GetInvalidMemory()) {
                auto set = context.descriptor_set.GetSet();
                return LogError(set, context.vuids.descriptor_buffer_bit_set_08114,
                                "Descriptor set %s encountered the following validation error at %s time: Descriptor in "
                                "binding #%" PRIu32 " index %" PRIu32 " is uses buffer %s that references invalid memory %s.",
                                report_data->FormatHandle(set).c_str(), context.caller, binding_info.first, index,
                                report_data->FormatHandle(buffer).c_str(), report_data->FormatHandle(binding->mem()).c_str());
            }
        }
        if (enabled_features.core11.protectedMemory == VK_TRUE) {
            if (ValidateProtectedBuffer(context.cb_state, *buffer_node, context.caller,
                                        context.vuids.unprotected_command_buffer_02707, "Buffer is in a descriptorSet")) {
                return true;
            }
            if (binding_info.second.variable->is_written_to &&
                ValidateUnprotectedBuffer(context.cb_state, *buffer_node, context.caller,
                                          context.vuids.protected_command_buffer_02712, "Buffer is in a descriptorSet")) {
                return true;
            }
        }
    }
    return false;
}

bool CoreChecks::ValidateDescriptor(const DescriptorContext &context, const DescriptorBindingInfo &binding_info, uint32_t index,
                                    VkDescriptorType descriptor_type,
                                    const cvdescriptorset::ImageDescriptor &image_descriptor) const {
    std::vector<const SAMPLER_STATE *> sampler_states;
    VkImageView image_view = image_descriptor.GetImageView();
    const IMAGE_VIEW_STATE *image_view_state = image_descriptor.GetImageViewState();
    const auto binding = binding_info.first;

    if (image_descriptor.GetClass() == cvdescriptorset::DescriptorClass::ImageSampler) {
        sampler_states.emplace_back(
            static_cast<const cvdescriptorset::ImageSamplerDescriptor &>(image_descriptor).GetSamplerState());
    } else {
        if (binding_info.second.variable->samplers_used_by_image.size() > index) {
            for (const auto &desc_index : binding_info.second.variable->samplers_used_by_image[index]) {
                const auto *desc =
                    context.descriptor_set.GetDescriptorFromBinding(desc_index.sampler_slot.binding, desc_index.sampler_index);
                // TODO: This check _shouldn't_ be necessary due to the checks made in ResourceInterfaceVariable() in
                //       shader_validation.cpp. However, without this check some traces still crash.
                if (desc && (desc->GetClass() == cvdescriptorset::DescriptorClass::PlainSampler)) {
                    const auto *sampler_state = static_cast<const cvdescriptorset::SamplerDescriptor *>(desc)->GetSamplerState();
                    if (sampler_state) sampler_states.emplace_back(sampler_state);
                }
            }
        }
    }

    if ((!image_view_state && !enabled_features.robustness2_features.nullDescriptor) ||
        (image_view_state && image_view_state->Destroyed())) {
        // Image view must have been destroyed since initial update. Could potentially flag the descriptor
        //  as "invalid" (updated = false) at DestroyImageView() time and detect this error at bind time

        auto set = context.descriptor_set.GetSet();
        return LogError(set, context.vuids.descriptor_buffer_bit_set_08114,
                        "%s: Descriptor set %s in binding #%" PRIu32 " index %" PRIu32
                        " is using imageView %s that is invalid or has been destroyed.",
                        context.caller, report_data->FormatHandle(set).c_str(), binding, index,
                        report_data->FormatHandle(image_view).c_str());
    }
    if (image_view) {
        const auto &variable = *binding_info.second.variable;
        const auto &image_view_ci = image_view_state->create_info;

        // if combined sampler, this variable might not be a OpTypeImage
        // SubpassData gets validated elsewhere
        if (variable.IsImage() && variable.image_dim != spv::DimSubpassData) {
            bool valid_dim = true;
            // From vkspec.html#textures-operation-validation
            switch (image_view_ci.viewType) {
                case VK_IMAGE_VIEW_TYPE_1D:
                    valid_dim = (variable.image_dim == spv::Dim1D) && !variable.is_image_array;
                    break;
                case VK_IMAGE_VIEW_TYPE_2D:
                    valid_dim = (variable.image_dim == spv::Dim2D) && !variable.is_image_array;
                    break;
                case VK_IMAGE_VIEW_TYPE_3D:
                    valid_dim = (variable.image_dim == spv::Dim3D) && !variable.is_image_array;
                    break;
                case VK_IMAGE_VIEW_TYPE_CUBE:
                    valid_dim = (variable.image_dim == spv::DimCube) && !variable.is_image_array;
                    break;
                case VK_IMAGE_VIEW_TYPE_1D_ARRAY:
                    valid_dim = (variable.image_dim == spv::Dim1D) && variable.is_image_array;
                    break;
                case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
                    valid_dim = (variable.image_dim == spv::Dim2D) && variable.is_image_array;
                    break;
                case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:
                    valid_dim = (variable.image_dim == spv::DimCube) && variable.is_image_array;
                    break;
                default:
                    break;  // incase a new VkImageViewType is added, let it be valid by default
            }
            if (!valid_dim) {
                auto set = context.descriptor_set.GetSet();
                const LogObjectList objlist(set, image_view);
                return LogError(objlist, context.vuids.image_view_dim_07752,
                                "%s: Descriptor set %s in binding #%" PRIu32 " index %" PRIu32
                                " ImageView type is %s but the OpTypeImage has (Dim = %s) and (Arrrayed = %d).",
                                context.caller, report_data->FormatHandle(set).c_str(), binding, index,
                                string_VkImageViewType(image_view_ci.viewType), string_SpvDim(variable.image_dim),
                                variable.is_image_array);
            }

            if (!(variable.image_format_type & image_view_state->descriptor_format_bits)) {
                // bad component type
                auto set = context.descriptor_set.GetSet();
                const LogObjectList objlist(set, image_view);
                return LogError(objlist, context.vuids.image_view_numeric_format_07753,
                                "%s: Descriptor set %s in binding #%" PRIu32 " index %" PRIu32
                                " requires %s component type, but bound descriptor format is %s.",
                                context.caller, report_data->FormatHandle(set).c_str(), binding, index,
                                string_NumericType(variable.image_format_type), string_VkFormat(image_view_ci.format));
            }

            const bool image_format_width_64 = FormatHasComponentSize(image_view_ci.format, 64);
            if (image_format_width_64) {
                if (binding_info.second.variable->image_sampled_type_width != 64) {
                    auto set = context.descriptor_set.GetSet();
                    const LogObjectList objlist(set, image_view);
                    return LogError(
                        objlist, context.vuids.image_view_access_64_04470,
                        "%s: Descriptor set %s in binding #%" PRIu32 " index %" PRIu32
                        " has a 64-bit component ImageView format (%s) but the OpTypeImage's Sampled Type has a width of %" PRIu32
                        ".",
                        context.caller, report_data->FormatHandle(set).c_str(), binding, index,
                        string_VkFormat(image_view_ci.format), binding_info.second.variable->image_sampled_type_width);
                } else if (!enabled_features.shader_image_atomic_int64_features.sparseImageInt64Atomics &&
                           image_view_state->image_state->sparse_residency) {
                    auto set = context.descriptor_set.GetSet();
                    const LogObjectList objlist(set, image_view, image_view_state->image_state->image());
                    return LogError(objlist, context.vuids.image_view_sparse_64_04474,
                                    "%s: Descriptor set %s in binding #%" PRIu32 " index %" PRIu32
                                    " a OpTypeImage's Sampled Type has a width of 64 backed by a sparse Image, but "
                                    "sparseImageInt64Atomics is not enabled.",
                                    context.caller, report_data->FormatHandle(set).c_str(), binding, index);
                }
            } else if (!image_format_width_64 && binding_info.second.variable->image_sampled_type_width != 32) {
                auto set = context.descriptor_set.GetSet();
                const LogObjectList objlist(set, image_view);
                return LogError(
                    objlist, context.vuids.image_view_access_32_04471,
                    "%s: Descriptor set %s in binding #%" PRIu32 " index %" PRIu32
                    " has a 32-bit component ImageView format (%s) but the OpTypeImage's Sampled Type has a width of %" PRIu32 ".",
                    context.caller, report_data->FormatHandle(set).c_str(), binding, index, string_VkFormat(image_view_ci.format),
                    binding_info.second.variable->image_sampled_type_width);
            }
        }

        // NOTE: Submit time validation of UPDATE_AFTER_BIND image layout is not possible with the
        // image layout tracking as currently implemented, so only record_time_validation is done
        if (!disabled[image_layout_validation] && context.record_time_validate) {
            VkImageLayout image_layout = image_descriptor.GetImageLayout();
            // Verify Image Layout
            // No "invalid layout" VUID required for this call, since the optimal_layout parameter is UNDEFINED.
            // The caller provides a checked_layouts map when there are a large number of layouts to check,
            // making it worthwhile to keep track of verified layouts and not recheck them.
            bool already_validated = false;
            if (context.checked_layouts) {
                auto search = context.checked_layouts->find(image_view);
                if (search != context.checked_layouts->end() && search->second == image_layout) {
                    already_validated = true;
                }
            }
            if (!already_validated) {
                bool hit_error = false;
                VerifyImageLayout(context.cb_state, *image_view_state, image_layout, context.caller,
                                  "VUID-VkDescriptorImageInfo-imageLayout-00344", &hit_error);
                if (hit_error) {
                    auto set = context.descriptor_set.GetSet();
                    return LogError(
                        set, context.vuids.descriptor_buffer_bit_set_08114,
                        "%s: Descriptor set %s Image layout specified at vkCmdBindDescriptorSets time doesn't match actual image "
                        "layout at time descriptor is used. See previous error callback for specific details.",
                        context.caller, report_data->FormatHandle(set).c_str());
                }
                if (context.checked_layouts) {
                    context.checked_layouts->emplace(image_view, image_layout);
                }
            }
        }

        // Verify Sample counts
        if (variable.IsImage() && !variable.is_multisampled && image_view_state->samples != VK_SAMPLE_COUNT_1_BIT) {
            auto set = context.descriptor_set.GetSet();
            return LogError(set, " VUID-RuntimeSpirv-samples-08725",
                            "%s: Descriptor set %s in binding #%" PRIu32 " index %" PRIu32
                            " requires bound image to have VK_SAMPLE_COUNT_1_BIT but got %s.",
                            context.caller, report_data->FormatHandle(set).c_str(), binding, index,
                            string_VkSampleCountFlagBits(image_view_state->samples));
        }
        if (variable.IsImage() && variable.is_multisampled && image_view_state->samples == VK_SAMPLE_COUNT_1_BIT) {
            auto set = context.descriptor_set.GetSet();
            return LogError(set, "VUID-RuntimeSpirv-samples-08726",
                            "%s: Descriptor set %s in binding #%" PRIu32 " index %" PRIu32
                            " requires bound image to have multiple samples, but got VK_SAMPLE_COUNT_1_BIT.",
                            context.caller, report_data->FormatHandle(set).c_str(), binding, index);
        }

        // Verify VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT
        if (variable.is_atomic_operation && (descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) &&
            !(image_view_state->format_features & VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT)) {
            auto set = context.descriptor_set.GetSet();
            const LogObjectList objlist(set, image_view);
            return LogError(objlist, context.vuids.imageview_atomic_02691,
                            "%s: Descriptor set %s in binding #%" PRIu32 " index %" PRIu32
                            ", %s, format %s, doesn't contain VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT.",
                            context.caller, report_data->FormatHandle(set).c_str(), binding, index,
                            report_data->FormatHandle(image_view).c_str(), string_VkFormat(image_view_ci.format));
        }

        // When KHR_format_feature_flags2 is supported, the read/write without
        // format support is reported per format rather than a single physical
        // device feature.
        if (has_format_feature2) {
            const VkFormatFeatureFlags2 format_features = image_view_state->format_features;

            if (descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
                if ((variable.is_read_without_format) && !(format_features & VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT)) {
                    auto set = context.descriptor_set.GetSet();
                    const LogObjectList objlist(set, image_view);
                    return LogError(objlist, context.vuids.storage_image_read_without_format_07028,
                                    "%s: Descriptor set %s in binding #%" PRIu32 " index %" PRIu32
                                    ", %s, image view format %s feature flags (%s) doesn't "
                                    "contain VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT",
                                    context.caller, report_data->FormatHandle(set).c_str(), binding, index,
                                    report_data->FormatHandle(image_view).c_str(), string_VkFormat(image_view_ci.format),
                                    string_VkFormatFeatureFlags2(format_features).c_str());
                }

                if ((variable.is_write_without_format) &&
                    !(format_features & VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT)) {
                    auto set = context.descriptor_set.GetSet();
                    const LogObjectList objlist(set, image_view);
                    return LogError(objlist, context.vuids.storage_image_write_without_format_07027,
                                    "%s: Descriptor set %s in binding #%" PRIu32 " index %" PRIu32
                                    ", %s, image view format %s feature flags (%s) doesn't "
                                    "contain VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT",
                                    context.caller, report_data->FormatHandle(set).c_str(), binding, index,
                                    report_data->FormatHandle(image_view).c_str(), string_VkFormat(image_view_ci.format),
                                    string_VkFormatFeatureFlags2(format_features).c_str());
                }
            }

            if ((variable.is_dref) && !(format_features & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT)) {
                auto set = context.descriptor_set.GetSet();
                const LogObjectList objlist(set, image_view);
                return LogError(objlist, context.vuids.depth_compare_sample_06479,
                                "%s: Descriptor set %s in binding #%" PRIu32 " index %" PRIu32
                                ", %s, image view format %s feature flags (%s) doesn't "
                                "contain VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT",
                                context.caller, report_data->FormatHandle(set).c_str(), binding, index,
                                report_data->FormatHandle(image_view).c_str(), string_VkFormat(image_view_ci.format),
                                string_VkFormatFeatureFlags2(format_features).c_str());
            }
        }

        // Verify if attachments are used in DescriptorSet
        const std::vector<IMAGE_VIEW_STATE *> *attachments = context.cb_state.active_attachments.get();
        const std::vector<SUBPASS_INFO> *subpasses = context.cb_state.active_subpasses.get();
        if (attachments && attachments->size() > 0 && subpasses && (descriptor_type != VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)) {
            for (uint32_t att_index = 0; att_index < attachments->size(); ++att_index) {
                const auto &view_state = (*attachments)[att_index];
                const SUBPASS_INFO &subpass = (*subpasses)[att_index];
                if (!view_state || view_state->Destroyed()) {
                    continue;
                }
                const bool same_view = view_state->image_view() == image_view;
                const bool overlapping_view = image_view_state->OverlapSubresource(*view_state);
                if (!same_view && !overlapping_view) {
                    continue;
                }

                bool descriptor_read_from = false;
                bool descriptor_written_to = false;
                uint32_t set_index = std::numeric_limits<uint32_t>::max();
                for (uint32_t i = 0; i < context.cb_state.lastBound[VK_PIPELINE_BIND_POINT_GRAPHICS].per_set.size(); ++i) {
                    const auto &set = context.cb_state.lastBound[VK_PIPELINE_BIND_POINT_GRAPHICS].per_set[i];
                    if (set.bound_descriptor_set.get() == &(context.descriptor_set)) {
                        set_index = i;
                        break;
                    }
                }
                assert(set_index != std::numeric_limits<uint32_t>::max());
                const auto pipeline = context.cb_state.GetCurrentPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS);
                for (const auto &stage : pipeline->stage_states) {
                    if (!stage.entrypoint) {
                        continue;
                    }
                    for (const auto &inteface_variable : stage.entrypoint->resource_interface_variables) {
                        if (inteface_variable.decorations.set == set_index && inteface_variable.decorations.binding == binding) {
                            descriptor_written_to |= inteface_variable.is_written_to;
                            descriptor_read_from |=
                                inteface_variable.is_read_from | inteface_variable.is_sampler_implicitLod_dref_proj;
                            break;
                        }
                    }
                }

                const bool layout_read_only = IsImageLayoutReadOnly(subpass.layout);
                bool write_attachment =
                    (subpass.usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) > 0 &&
                    !layout_read_only;
                if (write_attachment && descriptor_read_from) {
                    if (same_view) {
                        auto set = context.descriptor_set.GetSet();
                        const LogObjectList objlist(set, image_view, context.framebuffer);
                        return LogError(objlist, context.vuids.image_subresources_subpass_read_08753,
                                        "%s: Descriptor set %s Image View %s is being read from in Descriptor in binding #%" PRIu32
                                        " index %" PRIu32 " and will be written to as %s attachment # %" PRIu32 ".",
                                        context.caller, report_data->FormatHandle(set).c_str(),
                                        report_data->FormatHandle(image_view).c_str(), binding, index,
                                        report_data->FormatHandle(context.framebuffer).c_str(), att_index);
                    } else if (overlapping_view) {
                        auto set = context.descriptor_set.GetSet();
                        const LogObjectList objlist(set, image_view, context.framebuffer, view_state->image_view());
                        return LogError(
                            objlist, context.vuids.image_subresources_subpass_read_08753,
                            "%s: Descriptor set %s Image subresources of %s is being read from in Descriptor in binding #%" PRIu32
                            " index %" PRIu32 " and will be written to as %s in %s attachment # %" PRIu32 " overlap.",
                            context.caller, report_data->FormatHandle(set).c_str(), report_data->FormatHandle(image_view).c_str(),
                            binding, index, report_data->FormatHandle(view_state->image_view()).c_str(),
                            report_data->FormatHandle(context.framebuffer).c_str(), att_index);
                    }
                }
                const bool read_attachment = (subpass.usage & (VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) > 0;
                if (read_attachment && descriptor_written_to) {
                    if (same_view) {
                        auto set = context.descriptor_set.GetSet();
                        const LogObjectList objlist(set, image_view, context.framebuffer);
                        return LogError(
                            objlist, context.vuids.image_subresources_subpass_write_06539,
                            "%s: Descriptor set %s Image View  %s is being written to in Descriptor in binding #%" PRIu32
                            " index %" PRIu32 " and read from as %s attachment # %" PRIu32 ".",
                            context.caller, report_data->FormatHandle(set).c_str(), report_data->FormatHandle(image_view).c_str(),
                            binding, index, report_data->FormatHandle(context.framebuffer).c_str(), att_index);
                    } else if (overlapping_view) {
                        auto set = context.descriptor_set.GetSet();
                        const LogObjectList objlist(set, image_view, context.framebuffer, view_state->image_view());
                        return LogError(
                            objlist, context.vuids.image_subresources_subpass_write_06539,
                            "%s: Descriptor set %s Image subresources of %s is being written to in Descriptor in binding #%" PRIu32
                            " index %" PRIu32 " and will be read from as %s in %s attachment # %" PRIu32 " overlap.",
                            context.caller, report_data->FormatHandle(set).c_str(), report_data->FormatHandle(image_view).c_str(),
                            binding, index, report_data->FormatHandle(view_state->image_view()).c_str(),
                            report_data->FormatHandle(context.framebuffer).c_str(), att_index);
                    }
                }

                if (descriptor_written_to && !layout_read_only) {
                    if (same_view) {
                        auto set = context.descriptor_set.GetSet();
                        const LogObjectList objlist(set, image_view, context.framebuffer);
                        return LogError(objlist, context.vuids.image_subresources_render_pass_write_06537,
                                        "%s: Descriptor set %s Image View %s is used in Descriptor in binding #%" PRIu32
                                        " index %" PRIu32 " as writable and %s attachment # %" PRIu32 ".",
                                        context.caller, report_data->FormatHandle(set).c_str(),
                                        report_data->FormatHandle(image_view).c_str(), binding, index,
                                        report_data->FormatHandle(context.framebuffer).c_str(), att_index);
                    } else if (overlapping_view) {
                        auto set = context.descriptor_set.GetSet();
                        const LogObjectList objlist(set, image_view, context.framebuffer, view_state->image_view());
                        return LogError(objlist, context.vuids.image_subresources_render_pass_write_06537,
                                        "%s: Descriptor set %s Image subresources of %s in writable Descriptor in binding #%" PRIu32
                                        " index %" PRIu32 " and %s in %s attachment # %" PRIu32 " overlap.",
                                        context.caller, report_data->FormatHandle(set).c_str(),
                                        report_data->FormatHandle(image_view).c_str(), binding, index,
                                        report_data->FormatHandle(view_state->image_view()).c_str(),
                                        report_data->FormatHandle(context.framebuffer).c_str(), att_index);
                    }
                }
            }
            if (enabled_features.core11.protectedMemory == VK_TRUE) {
                if (ValidateProtectedImage(context.cb_state, *image_view_state->image_state, context.caller,
                                           context.vuids.unprotected_command_buffer_02707, "Image is in a descriptorSet")) {
                    return true;
                }
                if (binding_info.second.variable->is_written_to &&
                    ValidateUnprotectedImage(context.cb_state, *image_view_state->image_state, context.caller,
                                             context.vuids.protected_command_buffer_02712, "Image is in a descriptorSet")) {
                    return true;
                }
            }
        }

        const VkFormat image_view_format = image_view_state->create_info.format;
        for (const auto *sampler_state : sampler_states) {
            if (!sampler_state || sampler_state->Destroyed()) {
                continue;
            }

            // TODO: Validate 04015 for DescriptorClass::PlainSampler
            if ((sampler_state->createInfo.borderColor == VK_BORDER_COLOR_INT_CUSTOM_EXT ||
                 sampler_state->createInfo.borderColor == VK_BORDER_COLOR_FLOAT_CUSTOM_EXT) &&
                (sampler_state->customCreateInfo.format == VK_FORMAT_UNDEFINED)) {
                if (image_view_format == VK_FORMAT_B4G4R4A4_UNORM_PACK16 || image_view_format == VK_FORMAT_B5G6R5_UNORM_PACK16 ||
                    image_view_format == VK_FORMAT_B5G5R5A1_UNORM_PACK16) {
                    auto set = context.descriptor_set.GetSet();
                    const LogObjectList objlist(set, sampler_state->sampler(), image_view_state->image_view());
                    return LogError(objlist, "VUID-VkSamplerCustomBorderColorCreateInfoEXT-format-04015",
                                    "%s: Descriptor set %s Sampler %s in binding #%" PRIu32 " index %" PRIu32
                                    " has a custom border color with format = VK_FORMAT_UNDEFINED and is used to sample an image "
                                    "view %s with format %s",
                                    context.caller, report_data->FormatHandle(set).c_str(),
                                    report_data->FormatHandle(sampler_state->sampler()).c_str(), binding, index,
                                    report_data->FormatHandle(image_view_state->image_view()).c_str(),
                                    string_VkFormat(image_view_format));
                }
            }
            const VkFilter sampler_mag_filter = sampler_state->createInfo.magFilter;
            const VkFilter sampler_min_filter = sampler_state->createInfo.minFilter;
            const VkBool32 sampler_compare_enable = sampler_state->createInfo.compareEnable;
            if ((sampler_compare_enable == VK_FALSE) &&
                !(image_view_state->format_features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
                if (sampler_mag_filter == VK_FILTER_LINEAR || sampler_min_filter == VK_FILTER_LINEAR) {
                    auto set = context.descriptor_set.GetSet();
                    const LogObjectList objlist(set, sampler_state->sampler(), image_view_state->image_view());
                    return LogError(objlist, context.vuids.linear_filter_sampler_04553,
                                    "%s: Descriptor set %s Sampler (%s) is set to use VK_FILTER_LINEAR with compareEnable is set "
                                    "to VK_FALSE, but image view's (%s) format (%s) does not contain "
                                    "VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT in its format features.",
                                    context.caller, report_data->FormatHandle(set).c_str(),
                                    report_data->FormatHandle(sampler_state->sampler()).c_str(),
                                    report_data->FormatHandle(image_view_state->image_view()).c_str(),
                                    string_VkFormat(image_view_format));
                }
                if (sampler_state->createInfo.mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR) {
                    auto set = context.descriptor_set.GetSet();
                    const LogObjectList objlist(set, sampler_state->sampler(), image_view_state->image_view());
                    return LogError(objlist, context.vuids.linear_mipmap_sampler_04770,
                                    "%s: Descriptor set %s Sampler (%s) is set to use VK_SAMPLER_MIPMAP_MODE_LINEAR with "
                                    "compareEnable is set to VK_FALSE, but image view's (%s) format (%s) does not contain "
                                    "VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT in its format features.",
                                    context.caller, report_data->FormatHandle(set).c_str(),
                                    report_data->FormatHandle(sampler_state->sampler()).c_str(),
                                    report_data->FormatHandle(image_view_state->image_view()).c_str(),
                                    string_VkFormat(image_view_format));
                }
            }

            if (sampler_mag_filter == VK_FILTER_CUBIC_EXT || sampler_min_filter == VK_FILTER_CUBIC_EXT) {
                if (!(image_view_state->format_features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_CUBIC_BIT_EXT)) {
                    auto set = context.descriptor_set.GetSet();
                    const LogObjectList objlist(set, sampler_state->sampler(), image_view_state->image_view());
                    return LogError(
                        objlist, context.vuids.cubic_sampler_02692,
                        "%s: Descriptor set %s Sampler (%s) is set to use VK_FILTER_CUBIC_EXT, then image view's (%s) format (%s) "
                        "MUST contain VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_CUBIC_BIT_EXT in its format features.",
                        context.caller, report_data->FormatHandle(set).c_str(),
                        report_data->FormatHandle(sampler_state->sampler()).c_str(),
                        report_data->FormatHandle(image_view_state->image_view()).c_str(),
                        string_VkFormat(image_view_state->create_info.format));
                }

                if (IsExtEnabled(device_extensions.vk_ext_filter_cubic)) {
                    const auto reduction_mode_info =
                        LvlFindInChain<VkSamplerReductionModeCreateInfo>(sampler_state->createInfo.pNext);
                    if (reduction_mode_info &&
                        (reduction_mode_info->reductionMode == VK_SAMPLER_REDUCTION_MODE_MIN ||
                         reduction_mode_info->reductionMode == VK_SAMPLER_REDUCTION_MODE_MAX) &&
                        !image_view_state->filter_cubic_props.filterCubicMinmax) {
                        auto set = context.descriptor_set.GetSet();
                        const LogObjectList objlist(set, sampler_state->sampler(), image_view_state->image_view());
                        return LogError(objlist, context.vuids.filter_cubic_min_max_02695,
                                        "%s: Descriptor set %s Sampler (%s) is set to use VK_FILTER_CUBIC_EXT & %s, but image view "
                                        "(%s) doesn't support filterCubicMinmax.",
                                        context.caller, report_data->FormatHandle(set).c_str(),
                                        report_data->FormatHandle(sampler_state->sampler()).c_str(),
                                        string_VkSamplerReductionMode(reduction_mode_info->reductionMode),
                                        report_data->FormatHandle(image_view_state->image_view()).c_str());
                    }

                    if (!image_view_state->filter_cubic_props.filterCubic) {
                        auto set = context.descriptor_set.GetSet();
                        const LogObjectList objlist(set, sampler_state->sampler(), image_view_state->image_view());
                        return LogError(objlist, context.vuids.filter_cubic_02694,
                                        "%s: Descriptor set %s Sampler (%s) is set to use VK_FILTER_CUBIC_EXT, but image view (%s) "
                                        "doesn't support filterCubic.",
                                        context.caller, report_data->FormatHandle(set).c_str(),
                                        report_data->FormatHandle(sampler_state->sampler()).c_str(),
                                        report_data->FormatHandle(image_view_state->image_view()).c_str());
                    }
                }

                if (IsExtEnabled(device_extensions.vk_img_filter_cubic)) {
                    if (image_view_state->create_info.viewType == VK_IMAGE_VIEW_TYPE_3D ||
                        image_view_state->create_info.viewType == VK_IMAGE_VIEW_TYPE_CUBE ||
                        image_view_state->create_info.viewType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) {
                        auto set = context.descriptor_set.GetSet();
                        const LogObjectList objlist(set, sampler_state->sampler(), image_view_state->image_view());
                        return LogError(
                            objlist, context.vuids.img_filter_cubic_02693,
                            "%s: Descriptor set %s Sampler(%s)is set to use VK_FILTER_CUBIC_EXT while the VK_IMG_filter_cubic "
                            "extension is enabled, but image view (%s) has an invalid imageViewType (%s).",
                            context.caller, report_data->FormatHandle(set).c_str(),
                            report_data->FormatHandle(sampler_state->sampler()).c_str(),
                            report_data->FormatHandle(image_view_state->image_view()).c_str(),
                            string_VkImageViewType(image_view_state->create_info.viewType));
                    }
                }
            }
            const auto image_state = image_view_state->image_state.get();
            if ((image_state->createInfo.flags & VK_IMAGE_CREATE_CORNER_SAMPLED_BIT_NV) &&
                (sampler_state->createInfo.addressModeU != VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE ||
                 sampler_state->createInfo.addressModeV != VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE ||
                 sampler_state->createInfo.addressModeW != VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)) {
                std::string address_mode_letter =
                    (sampler_state->createInfo.addressModeU != VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)   ? "U"
                    : (sampler_state->createInfo.addressModeV != VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE) ? "V"
                                                                                                        : "W";
                VkSamplerAddressMode address_mode =
                    (sampler_state->createInfo.addressModeU != VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
                        ? sampler_state->createInfo.addressModeU
                    : (sampler_state->createInfo.addressModeV != VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
                        ? sampler_state->createInfo.addressModeV
                        : sampler_state->createInfo.addressModeW;
                auto set = context.descriptor_set.GetSet();
                const LogObjectList objlist(set, sampler_state->sampler(), image_state->image(), image_view_state->image_view());
                return LogError(objlist, context.vuids.corner_sampled_address_mode_02696,
                                "%s: Descriptor set %s Image (%s) in image view (%s) is created with flag "
                                "VK_IMAGE_CREATE_CORNER_SAMPLED_BIT_NV and can only be sampled using "
                                "VK_SAMPLER_ADDRESS_MODE_CLAMP_EDGE, but sampler (%s) has "
                                "createInfo.addressMode%s set to %s.",
                                context.caller, report_data->FormatHandle(set).c_str(),
                                report_data->FormatHandle(image_state->image()).c_str(),
                                report_data->FormatHandle(image_view_state->image_view()).c_str(),
                                report_data->FormatHandle(sampler_state->sampler()).c_str(), address_mode_letter.c_str(),
                                string_VkSamplerAddressMode(address_mode));
            }

            // UnnormalizedCoordinates sampler validations
            // only check if sampled as could have a texelFetch on a combined image sampler
            if (sampler_state->createInfo.unnormalizedCoordinates && variable.is_sampler_sampled) {
                // If ImageView is used by a unnormalizedCoordinates sampler, it needs to check ImageView type
                if (image_view_ci.viewType == VK_IMAGE_VIEW_TYPE_3D || image_view_ci.viewType == VK_IMAGE_VIEW_TYPE_CUBE ||
                    image_view_ci.viewType == VK_IMAGE_VIEW_TYPE_1D_ARRAY ||
                    image_view_ci.viewType == VK_IMAGE_VIEW_TYPE_2D_ARRAY ||
                    image_view_ci.viewType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) {
                    auto set = context.descriptor_set.GetSet();
                    const LogObjectList objlist(set, image_view, sampler_state->sampler());
                    return LogError(objlist, context.vuids.sampler_imageview_type_08609,
                                    "%s: Descriptor set %s Image View %s, type: %s in Descriptor in binding #%" PRIu32
                                    " index %" PRIu32 "is used by %s.",
                                    context.caller, report_data->FormatHandle(set).c_str(),
                                    report_data->FormatHandle(image_view).c_str(), string_VkImageViewType(image_view_ci.viewType),
                                    binding, index, report_data->FormatHandle(sampler_state->sampler()).c_str());
                }

                // sampler must not be used with any of the SPIR-V OpImageSample* or OpImageSparseSample*
                // instructions with ImplicitLod, Dref or Proj in their name
                if (variable.is_sampler_implicitLod_dref_proj) {
                    auto set = context.descriptor_set.GetSet();
                    const LogObjectList objlist(set, image_view, sampler_state->sampler());
                    return LogError(objlist, context.vuids.sampler_implicitLod_dref_proj_08610,
                                    "%s: Descriptor set %s Image View %s in Descriptor in binding #%" PRIu32 " index %" PRIu32
                                    " is used by %s that uses invalid operator.",
                                    context.caller, report_data->FormatHandle(set).c_str(),
                                    report_data->FormatHandle(image_view).c_str(), binding, index,
                                    report_data->FormatHandle(sampler_state->sampler()).c_str());
                }

                // sampler must not be used with any of the SPIR-V OpImageSample* or OpImageSparseSample*
                // instructions that includes a LOD bias or any offset values
                if (variable.is_sampler_bias_offset) {
                    auto set = context.descriptor_set.GetSet();
                    const LogObjectList objlist(set, image_view, sampler_state->sampler());
                    return LogError(objlist, context.vuids.sampler_bias_offset_08611,
                                    "%s: Descriptor set %s Image View %s in Descriptor in binding #%" PRIu32 " index %" PRIu32
                                    " is used by %s that uses invalid bias or offset operator.",
                                    context.caller, report_data->FormatHandle(set).c_str(),
                                    report_data->FormatHandle(image_view).c_str(), binding, index,
                                    report_data->FormatHandle(sampler_state->sampler()).c_str());
                }
            }
        }

        for (const uint32_t texel_component_count : binding_info.second.variable->write_without_formats_component_count_list) {
            const uint32_t format_component_count = FormatComponentCount(image_view_format);
            if (texel_component_count < format_component_count) {
                auto set = context.descriptor_set.GetSet();
                const LogObjectList objlist(set, image_view);
                return LogError(device, context.vuids.storage_image_write_texel_count_04115,
                                "%s: OpImageWrite Texel operand only contains %" PRIu32
                                " components, but the VkImageView is mapped to a OpImage format of %s has %" PRIu32
                                " components.\n",
                                context.caller, texel_component_count, string_VkFormat(image_view_format), format_component_count);
            }
        }
    }
    return false;
}

bool CoreChecks::ValidateDescriptor(const DescriptorContext &context, const DescriptorBindingInfo &binding_info, uint32_t index,
                                    VkDescriptorType descriptor_type,
                                    const cvdescriptorset::ImageSamplerDescriptor &descriptor) const {
    bool skip = ValidateDescriptor(context, binding_info, index, descriptor_type,
                                   static_cast<const cvdescriptorset::ImageDescriptor &>(descriptor));
    if (!skip) {
        skip = ValidateSamplerDescriptor(context, context.descriptor_set, binding_info, index, descriptor.GetSampler(),
                                         descriptor.IsImmutableSampler(), descriptor.GetSamplerState());
    }
    return skip;
}

bool CoreChecks::ValidateDescriptor(const DescriptorContext &context, const DescriptorBindingInfo &binding_info, uint32_t index,
                                    VkDescriptorType descriptor_type,
                                    const cvdescriptorset::TexelDescriptor &texel_descriptor) const {
    auto buffer_view = texel_descriptor.GetBufferView();
    auto buffer_view_state = texel_descriptor.GetBufferViewState();
    const auto binding = binding_info.first;
    const auto &variable = *binding_info.second.variable;
    if ((!buffer_view_state && !enabled_features.robustness2_features.nullDescriptor) ||
        (buffer_view_state && buffer_view_state->Destroyed())) {
        auto set = context.descriptor_set.GetSet();
        return LogError(set, context.vuids.descriptor_buffer_bit_set_08114,
                        "Descriptor set %s encountered the following validation error at %s time: Descriptor in "
                        "binding #%" PRIu32 " index %" PRIu32 " is using bufferView %s that is invalid or has been destroyed.",
                        report_data->FormatHandle(set).c_str(), context.caller, binding, index,
                        report_data->FormatHandle(buffer_view).c_str());
    }
    if (buffer_view && buffer_view_state) {
        auto buffer = buffer_view_state->create_info.buffer;
        const auto *buffer_state = buffer_view_state->buffer_state.get();
        const VkFormat buffer_view_format = buffer_view_state->create_info.format;
        if (buffer_state->Destroyed()) {
            auto set = context.descriptor_set.GetSet();
            return LogError(set, context.vuids.descriptor_buffer_bit_set_08114,
                            "Descriptor set %s encountered the following validation error at %s time: Descriptor in "
                            "binding #%" PRIu32 " index %" PRIu32 " is using buffer %s that has been destroyed.",
                            report_data->FormatHandle(set).c_str(), context.caller, binding, index,
                            report_data->FormatHandle(buffer).c_str());
        }
        const auto format_bits = GetFormatType(buffer_view_format);

        if (!(variable.image_format_type & format_bits)) {
            // bad component type
            auto set = context.descriptor_set.GetSet();
            return LogError(set, context.vuids.descriptor_buffer_bit_set_08114,
                            "Descriptor set %s encountered the following validation error at %s time: Descriptor in "
                            "binding #%" PRIu32 " index %" PRIu32 " requires %s component type, but bound descriptor format is %s.",
                            report_data->FormatHandle(set).c_str(), context.caller, binding, index,
                            string_NumericType(variable.image_format_type), string_VkFormat(buffer_view_format));
        }

        const bool buffer_format_width_64 = FormatHasComponentSize(buffer_view_format, 64);
        if (buffer_format_width_64 && binding_info.second.variable->image_sampled_type_width != 64) {
            auto set = context.descriptor_set.GetSet();
            const LogObjectList objlist(set, buffer_view);
            return LogError(
                objlist, context.vuids.buffer_view_access_64_04472,
                "%s: Descriptor set %s in binding #%" PRIu32 " index %" PRIu32
                " has a 64-bit component BufferView format (%s) but the OpTypeImage's Sampled Type has a width of %" PRIu32 ".",
                context.caller, report_data->FormatHandle(set).c_str(), binding, index, string_VkFormat(buffer_view_format),
                binding_info.second.variable->image_sampled_type_width);
        } else if (!buffer_format_width_64 && binding_info.second.variable->image_sampled_type_width != 32) {
            auto set = context.descriptor_set.GetSet();
            const LogObjectList objlist(set, buffer_view);
            return LogError(
                objlist, context.vuids.buffer_view_access_32_04473,
                "%s: Descriptor set %s in binding #%" PRIu32 " index %" PRIu32
                " has a 32-bit component BufferView format (%s) but the OpTypeImage's Sampled Type has a width of %" PRIu32 ".",
                context.caller, report_data->FormatHandle(set).c_str(), binding, index, string_VkFormat(buffer_view_format),
                binding_info.second.variable->image_sampled_type_width);
        }

        const VkFormatFeatureFlags2KHR buf_format_features = buffer_view_state->buf_format_features;
        const VkDescriptorType descriptor_type = context.descriptor_set.GetBinding(binding)->type;

        // Verify VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_ATOMIC_BIT
        if ((variable.is_atomic_operation) && (descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER) &&
            !(buf_format_features & VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_ATOMIC_BIT)) {
            auto set = context.descriptor_set.GetSet();
            const LogObjectList objlist(set, buffer_view);
            return LogError(objlist, context.vuids.bufferview_atomic_07888,
                            "Descriptor set %s encountered the following validation error at %s time: Descriptor "
                            "in binding #%" PRIu32 " index %" PRIu32
                            ", %s, format %s, doesn't "
                            "contain VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT.",
                            report_data->FormatHandle(set).c_str(), context.caller, binding, index,
                            report_data->FormatHandle(buffer_view).c_str(), string_VkFormat(buffer_view_format));
        }

        // When KHR_format_feature_flags2 is supported, the read/write without
        // format support is reported per format rather than a single physical
        // device feature.
        if (has_format_feature2) {
            if (descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER) {
                if ((variable.is_read_without_format) &&
                    !(buf_format_features & VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT_KHR)) {
                    auto set = context.descriptor_set.GetSet();

                    const LogObjectList objlist(set, buffer_view);
                    return LogError(objlist, context.vuids.storage_texel_buffer_read_without_format_07030,
                                    "Descriptor set %s encountered the following validation error at %s time: Descriptor "
                                    "in binding #%" PRIu32 " index %" PRIu32
                                    ", %s, buffer view format %s feature flags (%s) doesn't "
                                    "contain VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT_KHR",
                                    report_data->FormatHandle(set).c_str(), context.caller, binding, index,
                                    report_data->FormatHandle(buffer_view).c_str(), string_VkFormat(buffer_view_format),
                                    string_VkFormatFeatureFlags2(buf_format_features).c_str());
                }

                if ((variable.is_write_without_format) &&
                    !(buf_format_features & VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT_KHR)) {
                    auto set = context.descriptor_set.GetSet();
                    const LogObjectList objlist(set, buffer_view);
                    return LogError(objlist, context.vuids.storage_texel_buffer_write_without_format_07029,
                                    "Descriptor set %s encountered the following validation error at %s time: Descriptor "
                                    "in binding #%" PRIu32 " index %" PRIu32
                                    ", %s, buffer view format %s feature flags (%s) doesn't "
                                    "contain VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT_KHR",
                                    report_data->FormatHandle(set).c_str(), context.caller, binding, index,
                                    report_data->FormatHandle(buffer_view).c_str(), string_VkFormat(buffer_view_format),
                                    string_VkFormatFeatureFlags2(buf_format_features).c_str());
                }
            }
        }

        if (enabled_features.core11.protectedMemory == VK_TRUE) {
            if (ValidateProtectedBuffer(context.cb_state, *buffer_view_state->buffer_state, context.caller,
                                        context.vuids.unprotected_command_buffer_02707, "Buffer is in a descriptorSet")) {
                return true;
            }
            if (binding_info.second.variable->is_written_to &&
                ValidateUnprotectedBuffer(context.cb_state, *buffer_view_state->buffer_state, context.caller,
                                          context.vuids.protected_command_buffer_02712, "Buffer is in a descriptorSet")) {
                return true;
            }
        }

        for (const uint32_t texel_component_count : binding_info.second.variable->write_without_formats_component_count_list) {
            const uint32_t format_component_count = FormatComponentCount(buffer_view_format);
            if (texel_component_count < format_component_count) {
                auto set = context.descriptor_set.GetSet();

                const LogObjectList objlist(set, buffer_view);
                return LogError(device, context.vuids.storage_texel_buffer_write_texel_count_04469,
                                "%s: OpImageWrite Texel operand only contains %" PRIu32
                                " components, but the VkImageView is mapped to a OpImage format of %s has %" PRIu32
                                " components.\n",
                                context.caller, texel_component_count, string_VkFormat(buffer_view_format), format_component_count);
            }
        }
    }
    return false;
}

bool CoreChecks::ValidateDescriptor(const DescriptorContext &context, const DescriptorBindingInfo &binding_info, uint32_t index,
                                    VkDescriptorType descriptor_type,
                                    const cvdescriptorset::AccelerationStructureDescriptor &descriptor) const {
    // Verify that acceleration structures are valid
    const auto binding = binding_info.first;
    if (descriptor.is_khr()) {
        auto acc = descriptor.GetAccelerationStructure();
        auto acc_node = descriptor.GetAccelerationStructureStateKHR();
        if (!acc_node || acc_node->Destroyed()) {
            if (acc != VK_NULL_HANDLE || !enabled_features.robustness2_features.nullDescriptor) {
                auto set = context.descriptor_set.GetSet();
                return LogError(set, context.vuids.descriptor_buffer_bit_set_08114,
                                "Descriptor set %s encountered the following validation error at %s time: "
                                "Descriptor in binding #%" PRIu32 " index %" PRIu32
                                " is using acceleration structure %s that is invalid or has been destroyed.",
                                report_data->FormatHandle(set).c_str(), context.caller, binding, index,
                                report_data->FormatHandle(acc).c_str());
            }
        } else {
            for (const auto &mem_binding : acc_node->buffer_state->GetInvalidMemory()) {
                auto set = context.descriptor_set.GetSet();
                return LogError(set, context.vuids.descriptor_buffer_bit_set_08114,
                                "Descriptor set %s encountered the following validation error at %s time: Descriptor in "
                                "binding #%" PRIu32 " index %" PRIu32
                                " is using acceleration structure %s that references invalid memory %s.",
                                report_data->FormatHandle(set).c_str(), context.caller, binding, index,
                                report_data->FormatHandle(acc).c_str(), report_data->FormatHandle(mem_binding->mem()).c_str());
            }
        }
    } else {
        auto acc = descriptor.GetAccelerationStructureNV();
        auto acc_node = descriptor.GetAccelerationStructureStateNV();
        if (!acc_node || acc_node->Destroyed()) {
            if (acc != VK_NULL_HANDLE || !enabled_features.robustness2_features.nullDescriptor) {
                auto set = context.descriptor_set.GetSet();
                return LogError(set, context.vuids.descriptor_buffer_bit_set_08114,
                                "Descriptor set %s encountered the following validation error at %s time: "
                                "Descriptor in binding #%" PRIu32 " index %" PRIu32
                                " is using acceleration structure %s that is invalid or has been destroyed.",
                                report_data->FormatHandle(set).c_str(), context.caller, binding, index,
                                report_data->FormatHandle(acc).c_str());
            }
        } else {
            for (const auto &mem_binding : acc_node->GetInvalidMemory()) {
                auto set = context.descriptor_set.GetSet();
                return LogError(set, context.vuids.descriptor_buffer_bit_set_08114,
                                "Descriptor set %s encountered the following validation error at %s time: Descriptor in "
                                "binding #%" PRIu32 " index %" PRIu32
                                " is using acceleration structure %s that references invalid memory %s.",
                                report_data->FormatHandle(set).c_str(), context.caller, binding, index,
                                report_data->FormatHandle(acc).c_str(), report_data->FormatHandle(mem_binding->mem()).c_str());
            }
        }
    }
    return false;
}

// If the validation is related to both of image and sampler,
// please leave it in (descriptor_class == DescriptorClass::ImageSampler || descriptor_class ==
// DescriptorClass::Image) Here is to validate for only sampler.
bool CoreChecks::ValidateSamplerDescriptor(const DescriptorContext &context, const cvdescriptorset::DescriptorSet &descriptor_set,
                                           const DescriptorBindingInfo &binding_info, uint32_t index, VkSampler sampler,
                                           bool is_immutable, const SAMPLER_STATE *sampler_state) const {
    // Verify Sampler still valid
    if (!sampler_state || sampler_state->Destroyed()) {
        auto set = descriptor_set.GetSet();
        return LogError(set, context.vuids.descriptor_buffer_bit_set_08114,
                        "Descriptor set %s encountered the following validation error at %s time: Descriptor in "
                        "binding #%" PRIu32 " index %" PRIu32 " is using sampler %s that is invalid or has been destroyed.",
                        report_data->FormatHandle(set).c_str(), context.caller, binding_info.first, index,
                        report_data->FormatHandle(sampler).c_str());
    } else {
        if (sampler_state->samplerConversion && !is_immutable) {
            auto set = descriptor_set.GetSet();
            return LogError(set, context.vuids.descriptor_buffer_bit_set_08114,
                            "Descriptor set %s encountered the following validation error at %s time: sampler (%s) "
                            "in the descriptor set (%s) contains a YCBCR conversion (%s), then the sampler MUST "
                            "also exist as an immutable sampler.",
                            report_data->FormatHandle(set).c_str(), context.caller, report_data->FormatHandle(sampler).c_str(),
                            report_data->FormatHandle(descriptor_set.GetSet()).c_str(),
                            report_data->FormatHandle(sampler_state->samplerConversion).c_str());
        }
    }
    return false;
}

bool CoreChecks::ValidateDescriptor(const DescriptorContext &context, const DescriptorBindingInfo &binding_info, uint32_t index,
                                    VkDescriptorType descriptor_type, const cvdescriptorset::SamplerDescriptor &descriptor) const {
    return ValidateSamplerDescriptor(context, context.descriptor_set, binding_info, index, descriptor.GetSampler(),
                                     descriptor.IsImmutableSampler(), descriptor.GetSamplerState());
}

// Starting at offset descriptor of given binding, parse over update_count
//  descriptor updates and verify that for any binding boundaries that are crossed, the next binding(s) are all consistent
//  Consistency means that their type, stage flags, and whether or not they use immutable samplers matches
//  If so, return true. If not, fill in error_msg and return false
static bool VerifyUpdateConsistency(debug_report_data *report_data, const DescriptorSet &set, uint32_t binding, uint32_t offset,
                                    uint32_t update_count, const char *type, std::string *error_msg) {
    auto current_iter = set.FindBinding(binding);
    bool pass = true;
    // Verify consecutive bindings match (if needed)
    auto &orig_binding = **current_iter;
    while (pass && update_count) {
        // First, it's legal to offset beyond your own binding so handle that case
        if (offset > 0) {
            // index_range.start + offset is which descriptor is needed to update. If it > index_range.end, it means the descriptor
            // isn't in this binding, maybe in next binding.
            if (offset > (*current_iter)->count) {
                // Advance to next binding, decrement offset by binding size
                offset -= (*current_iter)->count;
                ++current_iter;
                // Verify next consecutive binding matches type, stage flags & immutable sampler use and if AtEnd
                if (current_iter == set.end() || !orig_binding.IsConsistent(**current_iter)) {
                    pass = false;
                }
                continue;
            }
        }

        update_count -= std::min(update_count, (*current_iter)->count - offset);
        if (update_count) {
            // Starting offset is beyond the current binding. Check consistency, update counters and advance to the next binding,
            // looking for the start point. All bindings (even those skipped) must be consistent with the update and with the
            // original binding.
            offset = 0;
            ++current_iter;
            // Verify next consecutive binding matches type, stage flags & immutable sampler use and if AtEnd
            if (current_iter == set.end() || !orig_binding.IsConsistent(**current_iter)) {
                pass = false;
            }
        }
    }

    if (!pass) {
        std::stringstream error_str;
        error_str << "Attempting " << type;
        if (set.IsPushDescriptor()) {
            error_str << " push descriptors";
        } else {
            error_str << " descriptor set " << report_data->FormatHandle(set.Handle());
        }
        error_str << " binding #" << orig_binding.binding << " with #" << update_count
                  << " descriptors being updated but this update oversteps the bounds of this binding and the next binding is "
                     "not consistent with current binding";
        if (current_iter == set.end()) {
            error_str << " (update past the end of the descriptor set)";
        } else {
            auto current_binding = current_iter->get();
            // Get what was not consistent in IsConsistent() as a more detailed error message
            if (current_binding->type != orig_binding.type) {
                error_str << " (" << string_VkDescriptorType(current_binding->type)
                          << " != " << string_VkDescriptorType(orig_binding.type) << ")";
            } else if (current_binding->stage_flags != orig_binding.stage_flags) {
                error_str << " (" << string_VkShaderStageFlags(current_binding->stage_flags)
                          << " != " << string_VkShaderStageFlags(orig_binding.stage_flags) << ")";
            } else if (current_binding->has_immutable_samplers != orig_binding.has_immutable_samplers) {
                error_str << " (pImmutableSamplers don't match)";
            } else if (current_binding->binding_flags != orig_binding.binding_flags) {
                error_str << " (" << string_VkDescriptorBindingFlags(current_binding->binding_flags)
                          << " != " << string_VkDescriptorBindingFlags(orig_binding.binding_flags) << ")";
            }
        }

        error_str << " so this update is invalid";
        *error_msg = error_str.str();
    }
    return pass;
}

// Validate Copy update
bool CoreChecks::ValidateCopyUpdate(const VkCopyDescriptorSet *update, const DescriptorSet *dst_set, const DescriptorSet *src_set,
                                    const char *func_name, std::string *error_code, std::string *error_msg) const {
    const auto *dst_layout = dst_set->GetLayout().get();
    const auto *src_layout = src_set->GetLayout().get();

    // Verify dst layout still valid
    if (dst_layout->Destroyed()) {
        *error_code = "VUID-VkCopyDescriptorSet-dstSet-parameter";
        std::ostringstream str;
        str << "Cannot call " << func_name << " to perform copy update on dstSet " << report_data->FormatHandle(dst_set->GetSet())
            << " created with destroyed " << report_data->FormatHandle(dst_layout->GetDescriptorSetLayout()) << ".";
        *error_msg = str.str();
        return false;
    }

    // Verify src layout still valid
    if (src_layout->Destroyed()) {
        *error_code = "VUID-VkCopyDescriptorSet-srcSet-parameter";
        std::ostringstream str;
        str << "Cannot call " << func_name << " to perform copy update on dstSet " << report_data->FormatHandle(dst_set->GetSet())
            << " from srcSet " << report_data->FormatHandle(src_set->GetSet()) << " created with destroyed "
            << report_data->FormatHandle(src_layout->GetDescriptorSetLayout()) << ".";
        *error_msg = str.str();
        return false;
    }

    if (!dst_layout->HasBinding(update->dstBinding)) {
        *error_code = "VUID-VkCopyDescriptorSet-dstBinding-00347";
        std::stringstream error_str;
        error_str << "DescriptorSet " << report_data->FormatHandle(dst_set->GetSet())
                  << " does not have copy update dest binding of " << update->dstBinding;
        *error_msg = error_str.str();
        return false;
    }
    if (!src_set->HasBinding(update->srcBinding)) {
        *error_code = "VUID-VkCopyDescriptorSet-srcBinding-00345";
        std::stringstream error_str;
        error_str << "DescriptorSet " << report_data->FormatHandle(src_set->GetSet())
                  << " does not have copy update src binding of " << update->srcBinding;
        *error_msg = error_str.str();
        return false;
    }
    // Verify idle ds
    if (dst_set->InUse() &&
        !(dst_layout->GetDescriptorBindingFlagsFromBinding(update->dstBinding) &
          (VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT))) {
        *error_code = "VUID-vkUpdateDescriptorSets-None-03047";
        std::stringstream error_str;
        error_str << "Cannot call " << func_name << " to perform copy update on descriptor set "
                  << report_data->FormatHandle(dst_set->GetSet()) << " that is in use by a command buffer";
        *error_msg = error_str.str();
        return false;
    }
    // src & dst set bindings are valid
    // Check bounds of src & dst
    auto src_start_idx = src_set->GetGlobalIndexRangeFromBinding(update->srcBinding).start + update->srcArrayElement;
    if ((src_start_idx + update->descriptorCount) > src_set->GetTotalDescriptorCount()) {
        // SRC update out of bounds
        *error_code = "VUID-VkCopyDescriptorSet-srcArrayElement-00346";
        std::stringstream error_str;
        error_str << "Attempting copy update from descriptorSet " << report_data->FormatHandle(update->srcSet) << " binding#"
                  << update->srcBinding << " with offset index of "
                  << src_set->GetGlobalIndexRangeFromBinding(update->srcBinding).start << " plus update array offset of "
                  << update->srcArrayElement << " and update of " << update->descriptorCount
                  << " descriptors oversteps total number of descriptors in set: " << src_set->GetTotalDescriptorCount();
        *error_msg = error_str.str();
        return false;
    }
    auto dst_start_idx = dst_layout->GetGlobalIndexRangeFromBinding(update->dstBinding).start + update->dstArrayElement;
    if ((dst_start_idx + update->descriptorCount) > dst_layout->GetTotalDescriptorCount()) {
        // DST update out of bounds
        *error_code = "VUID-VkCopyDescriptorSet-dstArrayElement-00348";
        std::stringstream error_str;
        error_str << "Attempting copy update to descriptorSet " << report_data->FormatHandle(dst_set->GetSet()) << " binding#"
                  << update->dstBinding << " with offset index of "
                  << dst_layout->GetGlobalIndexRangeFromBinding(update->dstBinding).start << " plus update array offset of "
                  << update->dstArrayElement << " and update of " << update->descriptorCount
                  << " descriptors oversteps total number of descriptors in set: " << dst_layout->GetTotalDescriptorCount();
        *error_msg = error_str.str();
        return false;
    }
    // Check that types match
    // TODO : Base default error case going from here is "VUID-VkAcquireNextImageInfoKHR-semaphore-parameter" 2ba which covers all
    // consistency issues, need more fine-grained error codes
    *error_code = "VUID-VkCopyDescriptorSet-srcSet-00349";
    auto src_type = src_layout->GetTypeFromBinding(update->srcBinding);
    auto dst_type = dst_layout->GetTypeFromBinding(update->dstBinding);
    if (src_type != VK_DESCRIPTOR_TYPE_MUTABLE_EXT && dst_type != VK_DESCRIPTOR_TYPE_MUTABLE_EXT && src_type != dst_type) {
        *error_code = "VUID-VkCopyDescriptorSet-dstBinding-02632";
        std::stringstream error_str;
        error_str << "Attempting copy update to descriptorSet " << report_data->FormatHandle(dst_set->GetSet()) << " binding #"
                  << update->dstBinding << " with type " << string_VkDescriptorType(dst_type) << " from descriptorSet "
                  << report_data->FormatHandle(src_set->GetSet()) << " binding #" << update->srcBinding << " with type "
                  << string_VkDescriptorType(src_type) << ". Types do not match";
        *error_msg = error_str.str();
        return false;
    }
    // Verify consistency of src & dst bindings if update crosses binding boundaries
    if ((!VerifyUpdateConsistency(report_data, *src_set, update->srcBinding, update->srcArrayElement, update->descriptorCount,
                                  "copy update from", error_msg)) ||
        (!VerifyUpdateConsistency(report_data, *dst_set, update->dstBinding, update->dstArrayElement, update->descriptorCount,
                                  "copy update to", error_msg))) {
        return false;
    }

    if ((src_layout->GetCreateFlags() & VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT) &&
        !(dst_layout->GetCreateFlags() & VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT)) {
        *error_code = "VUID-VkCopyDescriptorSet-srcSet-01918";
        std::stringstream error_str;
        error_str << "If pname:srcSet's (" << report_data->FormatHandle(update->srcSet)
                  << ") layout was created with the "
                     "ename:VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT flag "
                     "set, then pname:dstSet's ("
                  << report_data->FormatHandle(update->dstSet)
                  << ") layout must: also have been created with the "
                     "ename:VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT flag set";
        *error_msg = error_str.str();
        return false;
    }

    if (!(src_layout->GetCreateFlags() &
          (VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT | VK_DESCRIPTOR_SET_LAYOUT_CREATE_HOST_ONLY_POOL_BIT_EXT)) &&
        (dst_layout->GetCreateFlags() & VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT)) {
        *error_code = "VUID-VkCopyDescriptorSet-srcSet-04885";
        std::stringstream error_str;
        error_str << "If pname:srcSet's (" << report_data->FormatHandle(update->srcSet)
                  << ") layout was created with neither ename:VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT nor "
                     "ename:VK_DESCRIPTOR_SET_LAYOUT_CREATE_HOST_ONLY_POOL_BIT_EXT flags set, then pname:dstSet's ("
                  << report_data->FormatHandle(update->dstSet)
                  << ") layout must: have been created without the "
                     "ename:VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT flag set";
        *error_msg = error_str.str();
        return false;
    }

    if ((src_set->GetPoolState()->createInfo.flags & VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT) &&
        !(dst_set->GetPoolState()->createInfo.flags & VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT)) {
        *error_code = "VUID-VkCopyDescriptorSet-srcSet-01920";
        std::stringstream error_str;
        error_str << "If the descriptor pool from which pname:srcSet (" << report_data->FormatHandle(update->srcSet)
                  << ") was allocated was created "
                     "with the ename:VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT flag "
                     "set, then the descriptor pool from which pname:dstSet ("
                  << report_data->FormatHandle(update->dstSet)
                  << ") was allocated must: "
                     "also have been created with the ename:VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT flag set";
        *error_msg = error_str.str();
        return false;
    }

    if (!(src_set->GetPoolState()->createInfo.flags &
          (VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_POOL_CREATE_HOST_ONLY_BIT_EXT)) &&
        (dst_set->GetPoolState()->createInfo.flags & VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT)) {
        *error_code = "VUID-VkCopyDescriptorSet-srcSet-04887";
        std::stringstream error_str;
        error_str << "If the descriptor pool from which pname:srcSet (" << report_data->FormatHandle(update->srcSet)
                  << ") was allocated was created with neither ename:VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT nor "
                     "ename:VK_DESCRIPTOR_POOL_CREATE_HOST_ONLY_BIT_EXT flags set, then the descriptor pool from which "
                     "pname:dstSet ("
                  << report_data->FormatHandle(update->dstSet)
                  << ") was allocated must: have been created without the "
                     "ename:VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT flag set";
        *error_msg = error_str.str();
        return false;
    }

    if (src_type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT) {
        if ((update->srcArrayElement % 4) != 0) {
            *error_code = "VUID-VkCopyDescriptorSet-srcBinding-02223";
            std::stringstream error_str;
            error_str << "Attempting copy update to VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT binding with "
                      << "srcArrayElement " << update->srcArrayElement << " not a multiple of 4";
            *error_msg = error_str.str();
            return false;
        }
        if ((update->dstArrayElement % 4) != 0) {
            *error_code = "VUID-VkCopyDescriptorSet-dstBinding-02224";
            std::stringstream error_str;
            error_str << "Attempting copy update to VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT binding with "
                      << "dstArrayElement " << update->dstArrayElement << " not a multiple of 4";
            *error_msg = error_str.str();
            return false;
        }
        if ((update->descriptorCount % 4) != 0) {
            *error_code = "VUID-VkCopyDescriptorSet-srcBinding-02225";
            std::stringstream error_str;
            error_str << "Attempting copy update to VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT binding with "
                      << "descriptorCount " << update->descriptorCount << " not a multiple of 4";
            *error_msg = error_str.str();
            return false;
        }
    }

    if (dst_type == VK_DESCRIPTOR_TYPE_MUTABLE_EXT) {
        if (src_type != VK_DESCRIPTOR_TYPE_MUTABLE_EXT) {
            if (!dst_layout->IsTypeMutable(src_type, update->dstBinding)) {
                *error_code = "VUID-VkCopyDescriptorSet-dstSet-04612";
                std::stringstream error_str;
                error_str << "Attempting copy update with dstBinding descriptor type VK_DESCRIPTOR_TYPE_MUTABLE_EXT, but the new "
                             "active descriptor type "
                          << string_VkDescriptorType(src_type) << " is not in the corresponding pMutableDescriptorTypeLists list.";
                *error_msg = error_str.str();
                return false;
            }
        }
    } else if (src_type == VK_DESCRIPTOR_TYPE_MUTABLE_EXT) {
        auto src_iter = src_set->FindDescriptor(update->srcBinding, update->srcArrayElement);
        for (uint32_t i = 0; i < update->descriptorCount; i++, ++src_iter) {
            const auto &mutable_src = static_cast<const cvdescriptorset::MutableDescriptor &>(*src_iter);
            if (mutable_src.ActiveType() != dst_type) {
                *error_code = "VUID-VkCopyDescriptorSet-srcSet-04613";
                std::stringstream error_str;
                error_str << "Attempting copy update with srcBinding descriptor type VK_DESCRIPTOR_TYPE_MUTABLE_EXT, but the "
                             "active descriptor type ("
                          << string_VkDescriptorType(mutable_src.ActiveType()) << ") does not match the dstBinding descriptor type "
                          << string_VkDescriptorType(dst_type) << ".";
                *error_msg = error_str.str();
                return false;
            }
        }
    }

    if (dst_type == VK_DESCRIPTOR_TYPE_MUTABLE_EXT) {
        if (src_type == VK_DESCRIPTOR_TYPE_MUTABLE_EXT) {
            const auto &mutable_src_types = src_layout->GetMutableTypes(update->srcBinding);
            const auto &mutable_dst_types = dst_layout->GetMutableTypes(update->dstBinding);
            bool complete_match = mutable_src_types.size() == mutable_dst_types.size();
            if (complete_match) {
                for (const auto mutable_src_type : mutable_src_types) {
                    if (std::find(mutable_dst_types.begin(), mutable_dst_types.end(), mutable_src_type) ==
                        mutable_dst_types.end()) {
                        complete_match = false;
                        break;
                    }
                }
            }
            if (!complete_match) {
                *error_code = "VUID-VkCopyDescriptorSet-dstSet-04614";
                std::stringstream error_str;
                error_str << "Attempting copy update with dstBinding and new active descriptor type being "
                             "VK_DESCRIPTOR_TYPE_MUTABLE_EXT, but their corresponding pMutableDescriptorTypeLists do not match.";
                *error_msg = error_str.str();
                return false;
            }
        }
    }

    // Update mutable types
    if (src_type == VK_DESCRIPTOR_TYPE_MUTABLE_EXT) {
        src_type = static_cast<const cvdescriptorset::MutableDescriptor *>(
                       src_set->GetDescriptorFromBinding(update->srcBinding, update->srcArrayElement))
                       ->ActiveType();
    }
    if (dst_type == VK_DESCRIPTOR_TYPE_MUTABLE_EXT) {
        dst_type = static_cast<const cvdescriptorset::MutableDescriptor *>(
                       dst_set->GetDescriptorFromBinding(update->dstBinding, update->dstArrayElement))
                       ->ActiveType();
    }

    // Update parameters all look good and descriptor updated so verify update contents
    if (!VerifyCopyUpdateContents(update, src_set, src_type, src_start_idx, dst_set, dst_type, dst_start_idx, func_name, error_code,
                                  error_msg)) {
        return false;
    }

    // All checks passed so update is good
    return true;
}

// Validate given sampler. Currently this only checks to make sure it exists in the samplerMap
bool CoreChecks::ValidateSampler(const VkSampler sampler) const { return Get<SAMPLER_STATE>(sampler).get() != nullptr; }

bool CoreChecks::ValidateImageUpdate(VkImageView image_view, VkImageLayout image_layout, VkDescriptorType type,
                                     const char *func_name, std::string *error_code, std::string *error_msg) const {
    auto iv_state = Get<IMAGE_VIEW_STATE>(image_view);
    assert(iv_state);

    // Note that when an imageview is created, we validated that memory is bound so no need to re-check here
    // Validate that imageLayout is compatible with aspect_mask and image format
    //  and validate that image usage bits are correct for given usage
    VkImageAspectFlags aspect_mask = iv_state->normalized_subresource_range.aspectMask;
    VkImage image = iv_state->create_info.image;
    VkFormat format = VK_FORMAT_MAX_ENUM;
    VkImageUsageFlags usage = 0;
    auto *image_node = iv_state->image_state.get();
    assert(image_node);

    format = image_node->createInfo.format;
    const auto image_view_usage_info = LvlFindInChain<VkImageViewUsageCreateInfo>(iv_state->create_info.pNext);
    const auto stencil_usage_info = LvlFindInChain<VkImageStencilUsageCreateInfo>(image_node->createInfo.pNext);
    if (image_view_usage_info) {
        usage = image_view_usage_info->usage;
    } else {
        usage = image_node->createInfo.usage;
    }
    if (stencil_usage_info) {
        const bool stencil_aspect = (aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT) > 0;
        const bool depth_aspect = (aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT) > 0;
        if (stencil_aspect && !depth_aspect) {
            usage = stencil_usage_info->stencilUsage;
        } else if (stencil_aspect && depth_aspect) {
            usage &= stencil_usage_info->stencilUsage;
        }
    }

    // Validate that memory is bound to image
    if (ValidateMemoryIsBoundToImage(device, *image_node, func_name, kVUID_Core_Bound_Resource_FreedMemoryAccess)) {
        *error_code = kVUID_Core_Bound_Resource_FreedMemoryAccess;
        *error_msg = "No memory bound to image.";
        return false;
    }

    // KHR_maintenance1 allows rendering into 2D or 2DArray views which slice a 3D image,
    // but not binding them to descriptor sets.
    if (iv_state->IsDepthSliced() && image_node->createInfo.imageType == VK_IMAGE_TYPE_3D) {
        if (iv_state->create_info.viewType == VK_IMAGE_VIEW_TYPE_2D_ARRAY) {
            *error_code = "VUID-VkDescriptorImageInfo-imageView-06712";
            *error_msg = "ImageView must not be a 2DArray view of a 3D image";
            return false;
        }

        // vk_ext_image_2d_view_of_3d allows use of VIEW_TYPE_2D in descriptor
        if (IsExtEnabled(device_extensions.vk_ext_image_2d_view_of_3d)) {
            if ((type != VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) && (type != VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) &&
                (type != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)) {
                *error_code = "VUID-VkDescriptorImageInfo-imageView-07795";
                std::stringstream error_str;
                error_str << "ImageView (" << report_data->FormatHandle(image_view)
                          << ") , is a 2D image view created from 3D image (" << report_data->FormatHandle(image)
                          << ") , written to a descriptor of type " << string_VkDescriptorType(type)
                          << " but needs to be a descriptor type of VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, "
                             "VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, or VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER";
                *error_msg = error_str.str();
                return false;
            }

            if (type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE && !enabled_features.image_2d_view_of_3d_features.image2DViewOf3D) {
                *error_code = "VUID-VkDescriptorImageInfo-descriptorType-06713";
                std::stringstream error_str;
                error_str << "ImageView (" << report_data->FormatHandle(image_view)
                          << ") , is a 2D image view created from 3D image (" << report_data->FormatHandle(image)
                          << ") , written to a descriptor of type VK_DESCRIPTOR_TYPE_STORAGE_IMAGE"
                          << " and the image2DViewOf3D feature is not enabled";
                *error_msg = error_str.str();
                return false;
            }

            if ((type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) &&
                !enabled_features.image_2d_view_of_3d_features.sampler2DViewOf3D) {
                *error_code = "VUID-VkDescriptorImageInfo-descriptorType-06714";
                std::stringstream error_str;
                error_str << "ImageView (" << report_data->FormatHandle(image_view)
                          << ") , is a 2D image view created from 3D image (" << report_data->FormatHandle(image)
                          << ") , written to a descriptor of type " << string_VkDescriptorType(type)
                          << " and the image2DViewOf3D feature is not enabled";
                *error_msg = error_str.str();
                return false;
            }

            if (!(image_node->createInfo.flags & VK_IMAGE_CREATE_2D_VIEW_COMPATIBLE_BIT_EXT)) {
                *error_code = "VUID-VkDescriptorImageInfo-imageView-07796";
                std::stringstream error_str;
                error_str << "ImageView (" << report_data->FormatHandle(image_view)
                          << ") , is a 2D image view created from 3D image (" << report_data->FormatHandle(image)
                          << ") , written to a descriptor of type " << string_VkDescriptorType(type)
                          << " but the image used to create the image view was not created with "
                             "VK_IMAGE_CREATE_2D_VIEW_COMPATIBLE_BIT_EXT set";
                *error_msg = error_str.str();
                return false;
            }
        } else if (iv_state->create_info.viewType == VK_IMAGE_VIEW_TYPE_2D) {
            *error_code = "VUID-VkDescriptorImageInfo-imageView-06711";
            *error_msg = "ImageView must not be a 2D view of a 3D image";
            return false;
        }
    }

    // TODO : The various image aspect and format checks here are based on general spec language in 11.5 Image Views section under
    // vkCreateImageView(). What's the best way to create unique id for these cases?
    *error_code = kVUID_Core_DrawState_InvalidImageView;
    const bool ds = FormatIsDepthOrStencil(format);
    switch (image_layout) {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            // Only Color bit must be set
            if ((aspect_mask & VK_IMAGE_ASPECT_COLOR_BIT) != VK_IMAGE_ASPECT_COLOR_BIT) {
                std::stringstream error_str;
                error_str
                    << "ImageView (" << report_data->FormatHandle(image_view)
                    << ") uses layout VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL but does not have VK_IMAGE_ASPECT_COLOR_BIT set.";
                *error_msg = error_str.str();
                return false;
            }
            // format must NOT be DS
            if (ds) {
                std::stringstream error_str;
                error_str << "ImageView (" << report_data->FormatHandle(image_view)
                          << ") uses layout VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL but the image format is "
                          << string_VkFormat(format) << " which is not a color format.";
                *error_msg = error_str.str();
                return false;
            }
            break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            // Depth or stencil bit must be set, but both must NOT be set
            if (aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT) {
                if (aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT) {
                    // both  must NOT be set
                    std::stringstream error_str;
                    error_str << "ImageView (" << report_data->FormatHandle(image_view)
                              << ") has both STENCIL and DEPTH aspects set";
                    *error_msg = error_str.str();
                    return false;
                }
            } else if (!(aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT)) {
                // Neither were set
                std::stringstream error_str;
                error_str << "ImageView (" << report_data->FormatHandle(image_view) << ") has layout "
                          << string_VkImageLayout(image_layout) << " but does not have STENCIL or DEPTH aspects set";
                *error_msg = error_str.str();
                return false;
            }
            // format must be DS
            if (!ds) {
                std::stringstream error_str;
                error_str << "ImageView (" << report_data->FormatHandle(image_view) << ") has layout "
                          << string_VkImageLayout(image_layout) << " but the image format is " << string_VkFormat(format)
                          << " which is not a depth/stencil format.";
                *error_msg = error_str.str();
                return false;
            }
            break;
        default:
            // For other layouts if the source is depth/stencil image, both aspect bits must not be set
            if (ds) {
                if (aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT) {
                    if (aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT) {
                        // both  must NOT be set
                        std::stringstream error_str;
                        error_str << "ImageView (" << report_data->FormatHandle(image_view) << ") has layout "
                                  << string_VkImageLayout(image_layout) << " and is using depth/stencil image of format "
                                  << string_VkFormat(format)
                                  << " but it has both STENCIL and DEPTH aspects set, which is illegal. When using a depth/stencil "
                                     "image in a descriptor set, please only set either VK_IMAGE_ASPECT_DEPTH_BIT or "
                                     "VK_IMAGE_ASPECT_STENCIL_BIT depending on whether it will be used for depth reads or stencil "
                                     "reads respectively.";
                        *error_code = "VUID-VkDescriptorImageInfo-imageView-01976";
                        *error_msg = error_str.str();
                        return false;
                    }
                }
            }
            break;
    }
    // Now validate that usage flags are correctly set for given type of update
    //  As we're switching per-type, if any type has specific layout requirements, check those here as well
    // TODO : The various image usage bit requirements are in general spec language for VkImageUsageFlags bit block in 11.3 Images
    // under vkCreateImage()
    const char *error_usage_bit = nullptr;
    switch (type) {
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            if (iv_state->samplerConversion != VK_NULL_HANDLE) {
                *error_code = "VUID-VkWriteDescriptorSet-descriptorType-01946";
                std::stringstream error_str;
                error_str << "ImageView (" << report_data->FormatHandle(image_view) << ")"
                          << "used as a VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE can't be created with VkSamplerYcbcrConversion";
                *error_msg = error_str.str();
                return false;
            }
            [[fallthrough]];
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
            if (!(usage & VK_IMAGE_USAGE_SAMPLED_BIT)) {
                error_usage_bit = "VK_IMAGE_USAGE_SAMPLED_BIT";
                *error_code = "VUID-VkWriteDescriptorSet-descriptorType-00337";
            }
            break;
        }
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: {
            if (!(usage & VK_IMAGE_USAGE_STORAGE_BIT)) {
                error_usage_bit = "VK_IMAGE_USAGE_STORAGE_BIT";
                *error_code = "VUID-VkWriteDescriptorSet-descriptorType-00339";
            } else if ((VK_IMAGE_LAYOUT_GENERAL != image_layout) &&
                       (!IsExtEnabled(device_extensions.vk_khr_shared_presentable_image) ||
                        (VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR != image_layout))) {
                *error_code = "VUID-VkWriteDescriptorSet-descriptorType-04152";
                std::stringstream error_str;
                error_str << "Descriptor update with descriptorType VK_DESCRIPTOR_TYPE_STORAGE_IMAGE"
                          << " is being updated with invalid imageLayout " << string_VkImageLayout(image_layout) << " for image "
                          << report_data->FormatHandle(image) << " in imageView " << report_data->FormatHandle(image_view)
                          << ". Allowed layouts are: VK_IMAGE_LAYOUT_GENERAL";
                if (IsExtEnabled(device_extensions.vk_khr_shared_presentable_image)) {
                    error_str << " or VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR";
                }
                *error_msg = error_str.str();
                return false;
            }
            break;
        }
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: {
            if (!(usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) {
                error_usage_bit = "VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT";
                *error_code = "VUID-VkWriteDescriptorSet-descriptorType-00338";
            }
            break;
        }
        case VK_DESCRIPTOR_TYPE_SAMPLE_WEIGHT_IMAGE_QCOM: {
            if (!(usage & VK_IMAGE_USAGE_SAMPLE_WEIGHT_BIT_QCOM)) {
                error_usage_bit = "VK_IMAGE_USAGE_SAMPLE_WEIGHT_BIT_QCOM";
                *error_code = "VUID-VkWriteDescriptorSet-descriptorType-06942";
            }
            break;
        }
        case VK_DESCRIPTOR_TYPE_BLOCK_MATCH_IMAGE_QCOM: {
            if (!(usage & VK_IMAGE_USAGE_SAMPLE_BLOCK_MATCH_BIT_QCOM)) {
                error_usage_bit = "VK_IMAGE_USAGE_SAMPLE_BLOCK_MATCH_BIT_QCOM";
                *error_code = "VUID-VkWriteDescriptorSet-descriptorType-06943";
            }
            break;
        }
        default:
            break;
    }
    if (error_usage_bit) {
        std::stringstream error_str;
        error_str << "ImageView (" << report_data->FormatHandle(image_view) << ") with usage mask " << std::hex << std::showbase
                  << usage << " being used for a descriptor update of type " << string_VkDescriptorType(type) << " does not have "
                  << error_usage_bit << " set.";
        *error_msg = error_str.str();
        return false;
    }

    // All the following types share the same image layouts
    // checkf or Storage Images above
    if ((type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) || (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) ||
        (type == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)) {
        // Test that the layout is compatible with the descriptorType for the two sampled image types
        const static std::array<VkImageLayout, 3> valid_layouts = {
            {VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL}};

        struct ExtensionLayout {
            VkImageLayout layout;
            ExtEnabled DeviceExtensions::*extension;
        };
        const static std::array<ExtensionLayout, 8> extended_layouts{{
            //  Note double brace req'd for aggregate initialization
            {VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR, &DeviceExtensions::vk_khr_shared_presentable_image},
            {VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL, &DeviceExtensions::vk_khr_maintenance2},
            {VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL, &DeviceExtensions::vk_khr_maintenance2},
            {VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR, &DeviceExtensions::vk_khr_synchronization2},
            {VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR, &DeviceExtensions::vk_khr_synchronization2},
            {VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL, &DeviceExtensions::vk_khr_separate_depth_stencil_layouts},
            {VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL, &DeviceExtensions::vk_khr_separate_depth_stencil_layouts},
            {VK_IMAGE_LAYOUT_ATTACHMENT_FEEDBACK_LOOP_OPTIMAL_EXT, &DeviceExtensions::vk_ext_attachment_feedback_loop_layout},
        }};
        auto is_layout = [image_layout, this](const ExtensionLayout &ext_layout) {
            return IsExtEnabled(device_extensions.*(ext_layout.extension)) && (ext_layout.layout == image_layout);
        };

        const bool valid_layout = (std::find(valid_layouts.cbegin(), valid_layouts.cend(), image_layout) != valid_layouts.cend()) ||
                                  std::any_of(extended_layouts.cbegin(), extended_layouts.cend(), is_layout);

        if (!valid_layout) {
            // The following works as currently all 3 descriptor types share the same set of valid layouts
            switch (type) {
                case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                    *error_code = "VUID-VkWriteDescriptorSet-descriptorType-04149";
                    break;
                case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                    *error_code = "VUID-VkWriteDescriptorSet-descriptorType-04150";
                    break;
                case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
                    *error_code = "VUID-VkWriteDescriptorSet-descriptorType-04151";
                    break;
                default:
                    break;
            }
            std::stringstream error_str;
            error_str << "Descriptor update with descriptorType " << string_VkDescriptorType(type)
                      << " is being updated with invalid imageLayout " << string_VkImageLayout(image_layout) << " for image "
                      << report_data->FormatHandle(image) << " in imageView " << report_data->FormatHandle(image_view)
                      << ". Allowed layouts are: VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, "
                      << "VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL";
            for (auto &ext_layout : extended_layouts) {
                if (IsExtEnabled(device_extensions.*(ext_layout.extension))) {
                    error_str << ", " << string_VkImageLayout(ext_layout.layout);
                }
            }
            *error_msg = error_str.str();
            return false;
        }
    }

    if ((type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) || (type == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)) {
        const VkComponentMapping components = iv_state->create_info.components;
        if (IsIdentitySwizzle(components) == false) {
            *error_code = "VUID-VkWriteDescriptorSet-descriptorType-00336";
            std::stringstream error_str;
            error_str << "ImageView (" << report_data->FormatHandle(image_view) << ") has a non-identiy swizzle component, "
                      << " r swizzle = " << string_VkComponentSwizzle(components.r) << ","
                      << " g swizzle = " << string_VkComponentSwizzle(components.g) << ","
                      << " b swizzle = " << string_VkComponentSwizzle(components.b) << ","
                      << " a swizzle = " << string_VkComponentSwizzle(components.a) << ".";
            *error_msg = error_str.str();
            return false;
        }
    }

    if ((type == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT) && (iv_state->min_lod != 0.0f)) {
        *error_code = "VUID-VkWriteDescriptorSet-descriptorType-06450";
        std::stringstream error_str;
        error_str << "ImageView (" << report_data->FormatHandle(image_view)
                  << ") , written to a descriptor of type VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT with a minLod (" << iv_state->min_lod
                  << ") that is not 0.0";
        *error_msg = error_str.str();
        return false;
    }

    return true;
}

// This is a helper function that iterates over a set of Write and Copy updates, pulls the DescriptorSet* for updated
//  sets, and then calls their respective Validate[Write|Copy]Update functions.
// If the update hits an issue for which the callback returns "true", meaning that the call down the chain should
//  be skipped, then true is returned.
// If there is no issue with the update, then false is returned.
bool CoreChecks::ValidateUpdateDescriptorSets(uint32_t write_count, const VkWriteDescriptorSet *p_wds, uint32_t copy_count,
                                              const VkCopyDescriptorSet *p_cds, const char *func_name) const {
    bool skip = false;
    // Validate Write updates
    for (uint32_t i = 0; i < write_count; i++) {
        auto dest_set = p_wds[i].dstSet;
        auto set_node = Get<cvdescriptorset::DescriptorSet>(dest_set);
        std::string error_code;
        std::string error_str;
        if (!ValidateWriteUpdate(set_node.get(), &p_wds[i], func_name, &error_code, &error_str, false)) {
            skip |= LogError(dest_set, error_code, "%s pDescriptorWrites[%u] failed write update validation for %s with error: %s.",
                             func_name, i, report_data->FormatHandle(dest_set).c_str(), error_str.c_str());
        }

        if (p_wds[i].pNext) {
            const auto *pnext_struct = LvlFindInChain<VkWriteDescriptorSetAccelerationStructureKHR>(p_wds[i].pNext);
            if (pnext_struct) {
                for (uint32_t j = 0; j < pnext_struct->accelerationStructureCount; ++j) {
                    auto as_state = Get<ACCELERATION_STRUCTURE_STATE_KHR>(pnext_struct->pAccelerationStructures[j]);
                    if (as_state && (as_state->create_infoKHR.sType == VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR &&
                                     (as_state->create_infoKHR.type != VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR &&
                                      as_state->create_infoKHR.type != VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR))) {
                        skip |=
                            LogError(dest_set, "VUID-VkWriteDescriptorSetAccelerationStructureKHR-pAccelerationStructures-03579",
                                     "%s: For pDescriptorWrites[%u] acceleration structure in pAccelerationStructures[%u] must "
                                     "have been created with "
                                     "VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR or VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR.",
                                     func_name, i, j);
                    }
                }
            }
            const auto *pnext_struct_nv = LvlFindInChain<VkWriteDescriptorSetAccelerationStructureNV>(p_wds[i].pNext);
            if (pnext_struct_nv) {
                for (uint32_t j = 0; j < pnext_struct_nv->accelerationStructureCount; ++j) {
                    auto as_state = Get<ACCELERATION_STRUCTURE_STATE>(pnext_struct_nv->pAccelerationStructures[j]);
                    if (as_state && (as_state->create_infoNV.sType == VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV &&
                                     as_state->create_infoNV.info.type != VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV)) {
                        skip |= LogError(dest_set, "VUID-VkWriteDescriptorSetAccelerationStructureNV-pAccelerationStructures-03748",
                                         "%s: For pDescriptorWrites[%u] acceleration structure in pAccelerationStructures[%u] must "
                                         "have been created with"
                                         " VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV.",
                                         func_name, i, j);
                    }
                }
            }
        }
    }
    // Now validate copy updates
    for (uint32_t i = 0; i < copy_count; ++i) {
        auto dst_set = p_cds[i].dstSet;
        auto src_set = p_cds[i].srcSet;
        auto src_node = Get<cvdescriptorset::DescriptorSet>(src_set);
        auto dst_node = Get<cvdescriptorset::DescriptorSet>(dst_set);
        // Object_tracker verifies that src & dest descriptor set are valid
        assert(src_node);
        assert(dst_node);
        std::string error_code;
        std::string error_str;
        if (!ValidateCopyUpdate(&p_cds[i], dst_node.get(), src_node.get(), func_name, &error_code, &error_str)) {
            const LogObjectList objlist(dst_set, src_set);
            skip |= LogError(objlist, error_code, "%s pDescriptorCopies[%u] failed copy update from %s to %s with error: %s.",
                             func_name, i, report_data->FormatHandle(src_set).c_str(), report_data->FormatHandle(dst_set).c_str(),
                             error_str.c_str());
        }
    }
    return skip;
}

cvdescriptorset::DecodedTemplateUpdate::DecodedTemplateUpdate(const ValidationStateTracker *device_data,
                                                              VkDescriptorSet descriptorSet,
                                                              const UPDATE_TEMPLATE_STATE *template_state, const void *pData,
                                                              VkDescriptorSetLayout push_layout) {
    auto const &create_info = template_state->create_info;
    inline_infos.resize(create_info.descriptorUpdateEntryCount);  // Make sure we have one if we need it
    inline_infos_khr.resize(create_info.descriptorUpdateEntryCount);
    inline_infos_nv.resize(create_info.descriptorUpdateEntryCount);
    desc_writes.reserve(create_info.descriptorUpdateEntryCount);  // emplaced, so reserved without initialization
    VkDescriptorSetLayout effective_dsl = create_info.templateType == VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET
                                              ? create_info.descriptorSetLayout
                                              : push_layout;
    auto layout_obj = device_data->Get<cvdescriptorset::DescriptorSetLayout>(effective_dsl);

    // Create a WriteDescriptorSet struct for each template update entry
    for (uint32_t i = 0; i < create_info.descriptorUpdateEntryCount; i++) {
        auto binding_count = layout_obj->GetDescriptorCountFromBinding(create_info.pDescriptorUpdateEntries[i].dstBinding);
        auto binding_being_updated = create_info.pDescriptorUpdateEntries[i].dstBinding;
        auto dst_array_element = create_info.pDescriptorUpdateEntries[i].dstArrayElement;

        desc_writes.reserve(desc_writes.size() + create_info.pDescriptorUpdateEntries[i].descriptorCount);
        for (uint32_t j = 0; j < create_info.pDescriptorUpdateEntries[i].descriptorCount; j++) {
            desc_writes.emplace_back();
            auto &write_entry = desc_writes.back();

            size_t offset = create_info.pDescriptorUpdateEntries[i].offset + j * create_info.pDescriptorUpdateEntries[i].stride;
            char *update_entry = (char *)(pData) + offset;

            if (dst_array_element >= binding_count) {
                dst_array_element = 0;
                binding_being_updated = layout_obj->GetNextValidBinding(binding_being_updated);
            }

            write_entry.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write_entry.pNext = NULL;
            write_entry.dstSet = descriptorSet;
            write_entry.dstBinding = binding_being_updated;
            write_entry.dstArrayElement = dst_array_element;
            write_entry.descriptorCount = 1;
            write_entry.descriptorType = create_info.pDescriptorUpdateEntries[i].descriptorType;

            switch (create_info.pDescriptorUpdateEntries[i].descriptorType) {
                case VK_DESCRIPTOR_TYPE_SAMPLER:
                case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
                    write_entry.pImageInfo = reinterpret_cast<VkDescriptorImageInfo *>(update_entry);
                    break;

                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
                    write_entry.pBufferInfo = reinterpret_cast<VkDescriptorBufferInfo *>(update_entry);
                    break;

                case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
                case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
                    write_entry.pTexelBufferView = reinterpret_cast<VkBufferView *>(update_entry);
                    break;
                case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT: {
                    VkWriteDescriptorSetInlineUniformBlockEXT *inline_info = &inline_infos[i];
                    inline_info->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK_EXT;
                    inline_info->pNext = nullptr;
                    inline_info->dataSize = create_info.pDescriptorUpdateEntries[i].descriptorCount;
                    inline_info->pData = update_entry;
                    write_entry.pNext = inline_info;
                    // descriptorCount must match the dataSize member of the VkWriteDescriptorSetInlineUniformBlockEXT structure
                    write_entry.descriptorCount = inline_info->dataSize;
                    // skip the rest of the array, they just represent bytes in the update
                    j = create_info.pDescriptorUpdateEntries[i].descriptorCount;
                    break;
                }
                case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR: {
                    VkWriteDescriptorSetAccelerationStructureKHR *inline_info_khr = &inline_infos_khr[i];
                    inline_info_khr->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
                    inline_info_khr->pNext = nullptr;
                    inline_info_khr->accelerationStructureCount = create_info.pDescriptorUpdateEntries[i].descriptorCount;
                    inline_info_khr->pAccelerationStructures = reinterpret_cast<VkAccelerationStructureKHR *>(update_entry);
                    write_entry.pNext = inline_info_khr;
                    break;
                }
                case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV: {
                    VkWriteDescriptorSetAccelerationStructureNV *inline_info_nv = &inline_infos_nv[i];
                    inline_info_nv->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_NV;
                    inline_info_nv->pNext = nullptr;
                    inline_info_nv->accelerationStructureCount = create_info.pDescriptorUpdateEntries[i].descriptorCount;
                    inline_info_nv->pAccelerationStructures = reinterpret_cast<VkAccelerationStructureNV *>(update_entry);
                    write_entry.pNext = inline_info_nv;
                    break;
                }
                default:
                    assert(0);
                    break;
            }
            dst_array_element++;
        }
    }
}
// These helper functions carry out the validate and record descriptor updates peformed via update templates. They decode
// the templatized data and leverage the non-template UpdateDescriptor helper functions.
bool CoreChecks::ValidateUpdateDescriptorSetsWithTemplateKHR(VkDescriptorSet descriptorSet,
                                                             const UPDATE_TEMPLATE_STATE *template_state, const void *pData) const {
    // Translate the templated update into a normal update for validation...
    cvdescriptorset::DecodedTemplateUpdate decoded_update(this, descriptorSet, template_state, pData);
    return ValidateUpdateDescriptorSets(static_cast<uint32_t>(decoded_update.desc_writes.size()), decoded_update.desc_writes.data(),
                                        0, NULL, "vkUpdateDescriptorSetWithTemplate()");
}

std::string cvdescriptorset::DescriptorSet::StringifySetAndLayout() const {
    std::string out;
    auto layout_handle = layout_->GetDescriptorSetLayout();
    if (IsPushDescriptor()) {
        std::ostringstream str;
        str << "Push Descriptors defined with " << state_data_->report_data->FormatHandle(layout_handle);
        out = str.str();
    } else {
        std::ostringstream str;
        str << state_data_->report_data->FormatHandle(GetSet()) << " allocated with "
            << state_data_->report_data->FormatHandle(layout_handle);
        out = str.str();
    }
    return out;
}

// Loop through the write updates to validate for a push descriptor set, ignoring dstSet
bool CoreChecks::ValidatePushDescriptorsUpdate(const DescriptorSet *push_set, uint32_t write_count,
                                               const VkWriteDescriptorSet *p_wds, const char *func_name) const {
    assert(push_set->IsPushDescriptor());
    bool skip = false;
    for (uint32_t i = 0; i < write_count; i++) {
        std::string error_code;
        std::string error_str;
        if (!ValidateWriteUpdate(push_set, &p_wds[i], func_name, &error_code, &error_str, true)) {
            skip |= LogError(push_set->GetDescriptorSetLayout(), error_code,
                             "%s VkWriteDescriptorSet[%u] failed update validation: %s.", func_name, i, error_str.c_str());
        }
    }
    return skip;
}

// For the given buffer, verify that its creation parameters are appropriate for the given type
//  If there's an error, update the error_msg string with details and return false, else return true
static bool ValidateBufferUsage(debug_report_data *report_data, BUFFER_STATE const *buffer_node, VkDescriptorType type,
                                std::string *error_code, std::string *error_msg) {
    // Verify that usage bits set correctly for given type
    auto usage = buffer_node->createInfo.usage;
    const char *error_usage_bit = nullptr;
    switch (type) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            if (!(usage & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT)) {
                *error_code = "VUID-VkWriteDescriptorSet-descriptorType-00334";
                error_usage_bit = "VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT";
            }
            break;
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            if (!(usage & VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT)) {
                *error_code = "VUID-VkWriteDescriptorSet-descriptorType-00335";
                error_usage_bit = "VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT";
            }
            break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            if (!(usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)) {
                *error_code = "VUID-VkWriteDescriptorSet-descriptorType-00330";
                error_usage_bit = "VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT";
            }
            break;
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            if (!(usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
                *error_code = "VUID-VkWriteDescriptorSet-descriptorType-00331";
                error_usage_bit = "VK_BUFFER_USAGE_STORAGE_BUFFER_BIT";
            }
            break;
        default:
            break;
    }
    if (error_usage_bit) {
        std::stringstream error_str;
        error_str << "Buffer (" << report_data->FormatHandle(buffer_node->buffer()) << ") with usage mask " << std::hex
                  << std::showbase << usage << " being used for a descriptor update of type " << string_VkDescriptorType(type)
                  << " does not have " << error_usage_bit << " set.";
        *error_msg = error_str.str();
        return false;
    }
    return true;
}

// For buffer descriptor updates, verify the buffer usage and VkDescriptorBufferInfo struct which includes:
//  1. buffer is valid
//  2. buffer was created with correct usage flags
//  3. offset is less than buffer size
//  4. range is either VK_WHOLE_SIZE or falls in (0, (buffer size - offset)]
//  5. range and offset are within the device's limits
// If there's an error, update the error_msg string with details and return false, else return true
bool CoreChecks::ValidateBufferUpdate(VkDescriptorBufferInfo const *buffer_info, VkDescriptorType type, const char *func_name,
                                      std::string *error_code, std::string *error_msg) const {
    // First make sure that buffer is valid
    auto buffer_node = Get<BUFFER_STATE>(buffer_info->buffer);
    // Any invalid buffer should already be caught by object_tracker
    assert(buffer_node);
    if (ValidateMemoryIsBoundToBuffer(device, *buffer_node, func_name, "VUID-VkWriteDescriptorSet-descriptorType-00329")) {
        *error_code = "VUID-VkWriteDescriptorSet-descriptorType-00329";
        *error_msg = "No memory bound to buffer.";
        return false;
    }
    // Verify usage bits
    if (!ValidateBufferUsage(report_data, buffer_node.get(), type, error_code, error_msg)) {
        // error_msg will have been updated by ValidateBufferUsage()
        return false;
    }
    // offset must be less than buffer size
    if (buffer_info->offset >= buffer_node->createInfo.size) {
        *error_code = "VUID-VkDescriptorBufferInfo-offset-00340";
        std::stringstream error_str;
        error_str << "VkDescriptorBufferInfo offset of " << buffer_info->offset << " is greater than or equal to buffer "
                  << report_data->FormatHandle(buffer_node->buffer()) << " size of " << buffer_node->createInfo.size;
        *error_msg = error_str.str();
        return false;
    }
    if (buffer_info->range != VK_WHOLE_SIZE) {
        // Range must be VK_WHOLE_SIZE or > 0
        if (!buffer_info->range) {
            *error_code = "VUID-VkDescriptorBufferInfo-range-00341";
            std::stringstream error_str;
            error_str << "For buffer " << report_data->FormatHandle(buffer_node->buffer())
                      << " VkDescriptorBufferInfo range is not VK_WHOLE_SIZE and is zero, which is not allowed.";
            *error_msg = error_str.str();
            return false;
        }
        // Range must be VK_WHOLE_SIZE or <= (buffer size - offset)
        if (buffer_info->range > (buffer_node->createInfo.size - buffer_info->offset)) {
            *error_code = "VUID-VkDescriptorBufferInfo-range-00342";
            std::stringstream error_str;
            error_str << "For buffer " << report_data->FormatHandle(buffer_node->buffer()) << " VkDescriptorBufferInfo range is "
                      << buffer_info->range << " which is greater than buffer size (" << buffer_node->createInfo.size
                      << ") minus requested offset of " << buffer_info->offset;
            *error_msg = error_str.str();
            return false;
        }
    }
    // Check buffer update sizes against device limits
    const auto &limits = phys_dev_props.limits;
    if (VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER == type || VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC == type) {
        auto max_ub_range = limits.maxUniformBufferRange;
        if (buffer_info->range != VK_WHOLE_SIZE && buffer_info->range > max_ub_range) {
            *error_code = "VUID-VkWriteDescriptorSet-descriptorType-00332";
            std::stringstream error_str;
            error_str << "For buffer " << report_data->FormatHandle(buffer_node->buffer()) << " VkDescriptorBufferInfo range is "
                      << buffer_info->range << " which is greater than this device's maxUniformBufferRange (" << max_ub_range
                      << ")";
            *error_msg = error_str.str();
            return false;
        } else if (buffer_info->range == VK_WHOLE_SIZE && (buffer_node->createInfo.size - buffer_info->offset) > max_ub_range) {
            *error_code = "VUID-VkWriteDescriptorSet-descriptorType-00332";
            std::stringstream error_str;
            error_str << "For buffer " << report_data->FormatHandle(buffer_node->buffer())
                      << " VkDescriptorBufferInfo range is VK_WHOLE_SIZE but effective range "
                      << "(" << (buffer_node->createInfo.size - buffer_info->offset) << ") is greater than this device's "
                      << "maxUniformBufferRange (" << max_ub_range << ")";
            *error_msg = error_str.str();
            return false;
        }
    } else if (VK_DESCRIPTOR_TYPE_STORAGE_BUFFER == type || VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC == type) {
        auto max_sb_range = limits.maxStorageBufferRange;
        if (buffer_info->range != VK_WHOLE_SIZE && buffer_info->range > max_sb_range) {
            *error_code = "VUID-VkWriteDescriptorSet-descriptorType-00333";
            std::stringstream error_str;
            error_str << "For buffer " << report_data->FormatHandle(buffer_node->buffer()) << " VkDescriptorBufferInfo range is "
                      << buffer_info->range << " which is greater than this device's maxStorageBufferRange (" << max_sb_range
                      << ")";
            *error_msg = error_str.str();
            return false;
        } else if (buffer_info->range == VK_WHOLE_SIZE && (buffer_node->createInfo.size - buffer_info->offset) > max_sb_range) {
            *error_code = "VUID-VkWriteDescriptorSet-descriptorType-00333";
            std::stringstream error_str;
            error_str << "For buffer " << report_data->FormatHandle(buffer_node->buffer())
                      << " VkDescriptorBufferInfo range is VK_WHOLE_SIZE but effective range "
                      << "(" << (buffer_node->createInfo.size - buffer_info->offset) << ") is greater than this device's "
                      << "maxStorageBufferRange (" << max_sb_range << ")";
            *error_msg = error_str.str();
            return false;
        }
    }
    return true;
}

template <typename T>
bool CoreChecks::ValidateAccelerationStructureUpdate(T acc_node, const char *func_name, std::string *error_code,
                                                     std::string *error_msg) const {
    // nullDescriptor feature allows this to be VK_NULL_HANDLE
    if (acc_node) {
        if (ValidateMemoryIsBoundToAccelerationStructure(device, *acc_node, func_name, kVUIDUndefined)) {
            *error_code = kVUIDUndefined;
            *error_msg = "No memory bound to acceleration structure.";
            return false;
        }
    }
    return true;
}

// Verify that the contents of the update are ok, but don't perform actual update
bool CoreChecks::VerifyCopyUpdateContents(const VkCopyDescriptorSet *update, const DescriptorSet *src_set,
                                          VkDescriptorType src_type, uint32_t src_index, const DescriptorSet *dst_set,
                                          VkDescriptorType dst_type, uint32_t dst_index, const char *func_name,
                                          std::string *error_code, std::string *error_msg) const {
    // Note : Repurposing some Write update error codes here as specific details aren't called out for copy updates like they are
    // for write updates
    using DescriptorClass = cvdescriptorset::DescriptorClass;
    using BufferDescriptor = cvdescriptorset::BufferDescriptor;
    using ImageDescriptor = cvdescriptorset::ImageDescriptor;
    using ImageSamplerDescriptor = cvdescriptorset::ImageSamplerDescriptor;
    using SamplerDescriptor = cvdescriptorset::SamplerDescriptor;
    using TexelDescriptor = cvdescriptorset::TexelDescriptor;

    auto device_data = this;

    if (dst_type == VK_DESCRIPTOR_TYPE_SAMPLER) {
        auto dst_iter = dst_set->FindDescriptor(update->dstBinding, update->dstArrayElement);
        for (uint32_t di = 0; di < update->descriptorCount; ++di, ++dst_iter) {
            if (dst_iter.updated() && dst_iter->IsImmutableSampler()) {
                *error_code = "VUID-VkCopyDescriptorSet-dstBinding-02753";
                std::stringstream error_str;
                error_str << "Attempted copy update to an immutable sampler descriptor.";
                *error_msg = error_str.str();
                return false;
            }
        }
    }

    switch (src_set->GetBinding(update->srcBinding)->descriptor_class) {
        case DescriptorClass::PlainSampler: {
            auto src_iter = src_set->FindDescriptor(update->srcBinding, update->srcArrayElement);
            for (uint32_t di = 0; di < update->descriptorCount; ++di) {
                if (src_iter.updated()) {
                    if (!src_iter->IsImmutableSampler()) {
                        auto update_sampler = static_cast<const SamplerDescriptor &>(*src_iter).GetSampler();
                        if (!ValidateSampler(update_sampler)) {
                            *error_code = "VUID-VkWriteDescriptorSet-descriptorType-00325";
                            std::stringstream error_str;
                            error_str << "Attempted copy update to sampler descriptor with invalid sampler: "
                                      << report_data->FormatHandle(update_sampler) << ".";
                            *error_msg = error_str.str();
                            return false;
                        }
                    } else {
                        // TODO : Warn here
                    }
                }
            }
            break;
        }
        case DescriptorClass::ImageSampler: {
            auto src_iter = src_set->FindDescriptor(update->srcBinding, update->srcArrayElement);
            for (uint32_t di = 0; di < update->descriptorCount; ++di, ++src_iter) {
                if (!src_iter.updated()) continue;
                auto img_samp_desc = static_cast<const ImageSamplerDescriptor &>(*src_iter);
                // First validate sampler
                if (!img_samp_desc.IsImmutableSampler()) {
                    auto update_sampler = img_samp_desc.GetSampler();
                    if (!ValidateSampler(update_sampler)) {
                        *error_code = "VUID-VkWriteDescriptorSet-descriptorType-00325";
                        std::stringstream error_str;
                        error_str << "Attempted copy update to sampler descriptor with invalid sampler: "
                                  << report_data->FormatHandle(update_sampler) << ".";
                        *error_msg = error_str.str();
                        return false;
                    }
                } else {
                    // TODO : Warn here
                }
                // Validate image
                auto image_view = img_samp_desc.GetImageView();
                auto image_layout = img_samp_desc.GetImageLayout();
                if (image_view) {
                    if (!ValidateImageUpdate(image_view, image_layout, src_type, func_name, error_code, error_msg)) {
                        std::stringstream error_str;
                        error_str << "Attempted copy update to combined image sampler descriptor failed due to: "
                                  << error_msg->c_str();
                        *error_msg = error_str.str();
                        return false;
                    }
                }
            }
            break;
        }
        case DescriptorClass::Image: {
            auto src_iter = src_set->FindDescriptor(update->srcBinding, update->srcArrayElement);
            for (uint32_t di = 0; di < update->descriptorCount; ++di, ++src_iter) {
                if (!src_iter.updated()) continue;
                auto img_desc = static_cast<const ImageDescriptor &>(*src_iter);
                auto image_view = img_desc.GetImageView();
                auto image_layout = img_desc.GetImageLayout();
                if (image_view) {
                    if (!ValidateImageUpdate(image_view, image_layout, src_type, func_name, error_code, error_msg)) {
                        std::stringstream error_str;
                        error_str << "Attempted copy update to image descriptor failed due to: " << error_msg->c_str();
                        *error_msg = error_str.str();
                        return false;
                    }
                }
            }
            break;
        }
        case DescriptorClass::TexelBuffer: {
            auto src_iter = src_set->FindDescriptor(update->srcBinding, update->srcArrayElement);
            for (uint32_t di = 0; di < update->descriptorCount; ++di, ++src_iter) {
                if (!src_iter.updated()) continue;
                auto buffer_view = static_cast<const TexelDescriptor &>(*src_iter).GetBufferView();
                if (buffer_view) {
                    auto bv_state = device_data->Get<BUFFER_VIEW_STATE>(buffer_view);
                    if (!bv_state) {
                        *error_code = "VUID-VkWriteDescriptorSet-descriptorType-02994";
                        std::stringstream error_str;
                        error_str << "Attempted copy update to texel buffer descriptor with invalid buffer view: "
                                  << report_data->FormatHandle(buffer_view);
                        *error_msg = error_str.str();
                        return false;
                    }
                    auto buffer = bv_state->create_info.buffer;
                    auto buffer_state = Get<BUFFER_STATE>(buffer);
                    if (!ValidateBufferUsage(report_data, buffer_state.get(), src_type, error_code, error_msg)) {
                        std::stringstream error_str;
                        error_str << "Attempted copy update to texel buffer descriptor failed due to: " << error_msg->c_str();
                        *error_msg = error_str.str();
                        return false;
                    }
                }
            }
            break;
        }
        case DescriptorClass::GeneralBuffer: {
            auto src_iter = src_set->FindDescriptor(update->srcBinding, update->srcArrayElement);
            for (uint32_t di = 0; di < update->descriptorCount; ++di, ++src_iter) {
                if (!src_iter.updated()) continue;
                auto buffer_state = static_cast<const BufferDescriptor &>(*src_iter).GetBufferState();
                if (buffer_state) {
                    if (!ValidateBufferUsage(report_data, buffer_state, src_type, error_code, error_msg)) {
                        std::stringstream error_str;
                        error_str << "Attempted copy update to buffer descriptor failed due to: " << error_msg->c_str();
                        *error_msg = error_str.str();
                        return false;
                    }
                }
            }
            break;
        }
        case DescriptorClass::InlineUniform:
        case DescriptorClass::AccelerationStructure:
        case DescriptorClass::Mutable:
            break;
        default:
            assert(0);  // We've already verified update type so should never get here
            break;
    }
    // All checks passed so update contents are good
    return true;
}

// Verify that the state at allocate time is correct, but don't actually allocate the sets yet
bool CoreChecks::ValidateAllocateDescriptorSets(const VkDescriptorSetAllocateInfo *p_alloc_info,
                                                const cvdescriptorset::AllocateDescriptorSetsData *ds_data) const {
    bool skip = false;
    auto pool_state = Get<DESCRIPTOR_POOL_STATE>(p_alloc_info->descriptorPool);

    for (uint32_t i = 0; i < p_alloc_info->descriptorSetCount; i++) {
        auto layout = Get<cvdescriptorset::DescriptorSetLayout>(p_alloc_info->pSetLayouts[i]);
        if (layout) {  // nullptr layout indicates no valid layout handle for this device, validated/logged in object_tracker
            if (layout->IsPushDescriptor()) {
                skip |= LogError(p_alloc_info->pSetLayouts[i], "VUID-VkDescriptorSetAllocateInfo-pSetLayouts-00308",
                                 "%s specified at pSetLayouts[%" PRIu32
                                 "] in vkAllocateDescriptorSets() was created with invalid flag %s set.",
                                 report_data->FormatHandle(p_alloc_info->pSetLayouts[i]).c_str(), i,
                                 "VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR");
            }
            if (layout->GetCreateFlags() & VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT) {
                skip |= LogError(p_alloc_info->pSetLayouts[i], "VUID-VkDescriptorSetAllocateInfo-pSetLayouts-08009",
                                 "%s specified at pSetLayouts[%" PRIu32
                                 "] in vkAllocateDescriptorSets() was created with invalid flag %s set.",
                                 report_data->FormatHandle(p_alloc_info->pSetLayouts[i]).c_str(), i,
                                 "VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT");
            }
            if (layout->GetCreateFlags() & VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT &&
                !(pool_state->createInfo.flags & VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT)) {
                skip |= LogError(
                    device, "VUID-VkDescriptorSetAllocateInfo-pSetLayouts-03044",
                    "vkAllocateDescriptorSets(): Descriptor set layout create flags and pool create flags mismatch for index (%d)",
                    i);
            }
            if (layout->GetCreateFlags() & VK_DESCRIPTOR_SET_LAYOUT_CREATE_HOST_ONLY_POOL_BIT_EXT &&
                !(pool_state->createInfo.flags & VK_DESCRIPTOR_POOL_CREATE_HOST_ONLY_BIT_EXT)) {
                skip |= LogError(device, "VUID-VkDescriptorSetAllocateInfo-pSetLayouts-04610",
                                 "vkAllocateDescriptorSets(): pSetLayouts[%d].flags contain "
                                 "VK_DESCRIPTOR_SET_LAYOUT_CREATE_HOST_ONLY_POOL_BIT_EXT bit, but the pool was not created "
                                 "with the VK_DESCRIPTOR_POOL_CREATE_HOST_ONLY_BIT_EXT bit.",
                                 i);
            }
        }
    }
    if (!IsExtEnabled(device_extensions.vk_khr_maintenance1)) {
        // Track number of descriptorSets allowable in this pool
        if (pool_state->GetAvailableSets() < p_alloc_info->descriptorSetCount) {
            skip |= LogError(pool_state->Handle(), "VUID-VkDescriptorSetAllocateInfo-apiVersion-07895",
                             "vkAllocateDescriptorSets(): Unable to allocate %u descriptorSets from %s"
                             ". This pool only has %d descriptorSets remaining.",
                             p_alloc_info->descriptorSetCount, report_data->FormatHandle(pool_state->Handle()).c_str(),
                             pool_state->GetAvailableSets());
        }
        // Determine whether descriptor counts are satisfiable
        for (auto it = ds_data->required_descriptors_by_type.begin(); it != ds_data->required_descriptors_by_type.end(); ++it) {
            auto available_count = pool_state->GetAvailableCount(it->first);

            if (ds_data->required_descriptors_by_type.at(it->first) > available_count) {
                skip |= LogError(pool_state->Handle(), "VUID-VkDescriptorSetAllocateInfo-apiVersion-07896",
                                 "vkAllocateDescriptorSets(): Unable to allocate %u descriptors of type %s from %s"
                                 ". This pool only has %d descriptors of this type remaining.",
                                 ds_data->required_descriptors_by_type.at(it->first),
                                 string_VkDescriptorType(VkDescriptorType(it->first)),
                                 report_data->FormatHandle(pool_state->Handle()).c_str(), available_count);
            }
        }
    }

    const auto *count_allocate_info = LvlFindInChain<VkDescriptorSetVariableDescriptorCountAllocateInfo>(p_alloc_info->pNext);

    if (count_allocate_info) {
        if (count_allocate_info->descriptorSetCount != 0 &&
            count_allocate_info->descriptorSetCount != p_alloc_info->descriptorSetCount) {
            skip |= LogError(device, "VUID-VkDescriptorSetVariableDescriptorCountAllocateInfo-descriptorSetCount-03045",
                             "vkAllocateDescriptorSets(): VkDescriptorSetAllocateInfo::descriptorSetCount (%d) != "
                             "VkDescriptorSetVariableDescriptorCountAllocateInfo::descriptorSetCount (%d)",
                             p_alloc_info->descriptorSetCount, count_allocate_info->descriptorSetCount);
        }
        if (count_allocate_info->descriptorSetCount == p_alloc_info->descriptorSetCount) {
            for (uint32_t i = 0; i < p_alloc_info->descriptorSetCount; i++) {
                auto layout = Get<cvdescriptorset::DescriptorSetLayout>(p_alloc_info->pSetLayouts[i]);
                if (count_allocate_info->pDescriptorCounts[i] > layout->GetDescriptorCountFromBinding(layout->GetMaxBinding())) {
                    skip |= LogError(device, "VUID-VkDescriptorSetVariableDescriptorCountAllocateInfo-pSetLayouts-03046",
                                     "vkAllocateDescriptorSets(): pDescriptorCounts[%d] = (%d), binding's descriptorCount = (%d)",
                                     i, count_allocate_info->pDescriptorCounts[i],
                                     layout->GetDescriptorCountFromBinding(layout->GetMaxBinding()));
                }
            }
        }
    }

    return skip;
}

// Validate the state for a given write update but don't actually perform the update
//  If an error would occur for this update, return false and fill in details in error_msg string
bool CoreChecks::ValidateWriteUpdate(const DescriptorSet *dest_set, const VkWriteDescriptorSet *update, const char *func_name,
                                     std::string *error_code, std::string *error_msg, bool push) const {
    const auto *dest_layout = dest_set->GetLayout().get();

    // Verify dst layout still valid
    // ObjectLifetimes only checks if null, we check if valid dstSet here
    if (dest_layout->Destroyed()) {
        *error_code = "VUID-VkWriteDescriptorSet-dstSet-00320";
        std::ostringstream str;
        str << "Cannot call " << func_name << " to perform write update on " << dest_set->StringifySetAndLayout()
            << " which has been destroyed";
        *error_msg = str.str();
        return false;
    }
    // Verify dst binding exists
    if (!dest_layout->HasBinding(update->dstBinding)) {
        *error_code = "VUID-VkWriteDescriptorSet-dstBinding-00315";
        std::stringstream error_str;
        error_str << dest_set->StringifySetAndLayout() << " does not have binding " << update->dstBinding;
        *error_msg = error_str.str();
        return false;
    }

    auto dest = dest_set->GetBinding(update->dstBinding);
    // Make sure binding isn't empty
    if (0 == dest->count) {
        *error_code = "VUID-VkWriteDescriptorSet-dstBinding-00316";
        std::stringstream error_str;
        error_str << dest_set->StringifySetAndLayout() << " cannot updated binding " << update->dstBinding
                  << " that has 0 descriptors";
        *error_msg = error_str.str();
        return false;
    }

    // Verify idle ds
    if (dest_set->InUse() && !(dest->IsBindless())) {
        *error_code = "VUID-vkUpdateDescriptorSets-None-03047";
        std::stringstream error_str;
        error_str << "Cannot call " << func_name << " to perform write update on " << dest_set->StringifySetAndLayout()
                  << " that is in use by a command buffer";
        *error_msg = error_str.str();
        return false;
    }
    // We know that binding is valid, verify update and do update on each descriptor
    if ((dest->type != VK_DESCRIPTOR_TYPE_MUTABLE_EXT) && (dest->type != update->descriptorType)) {
        *error_code = "VUID-VkWriteDescriptorSet-descriptorType-00319";
        std::stringstream error_str;
        error_str << "Attempting write update to " << dest_set->StringifySetAndLayout() << " binding #" << update->dstBinding
                  << " with type " << string_VkDescriptorType(dest->type) << " but update type is "
                  << string_VkDescriptorType(update->descriptorType);
        *error_msg = error_str.str();
        return false;
    }
    if (dest->type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT) {
        if ((update->dstArrayElement % 4) != 0) {
            *error_code = "VUID-VkWriteDescriptorSet-descriptorType-02219";
            std::stringstream error_str;
            error_str << "Attempting write update to " << dest_set->StringifySetAndLayout() << " binding #" << update->dstBinding
                      << " with "
                      << "dstArrayElement " << update->dstArrayElement << " not a multiple of 4";
            *error_msg = error_str.str();
            return false;
        }
        if ((update->descriptorCount % 4) != 0) {
            *error_code = "VUID-VkWriteDescriptorSet-descriptorType-02220";
            std::stringstream error_str;
            error_str << "Attempting write update to " << dest_set->StringifySetAndLayout() << " binding #" << update->dstBinding
                      << " with "
                      << "descriptorCount  " << update->descriptorCount << " not a multiple of 4";
            *error_msg = error_str.str();
            return false;
        }
        const auto *write_inline_info = LvlFindInChain<VkWriteDescriptorSetInlineUniformBlockEXT>(update->pNext);
        if (!write_inline_info || write_inline_info->dataSize != update->descriptorCount) {
            *error_code = "VUID-VkWriteDescriptorSet-descriptorType-02221";
            std::stringstream error_str;
            if (!write_inline_info) {
                error_str << "Attempting write update to " << dest_set->StringifySetAndLayout() << " binding #"
                          << update->dstBinding << " with "
                          << "VkWriteDescriptorSetInlineUniformBlock missing";
            } else {
                error_str << "Attempting write update to " << dest_set->StringifySetAndLayout() << " binding #"
                          << update->dstBinding << " with "
                          << "VkWriteDescriptorSetInlineUniformBlock dataSize " << write_inline_info->dataSize << " not equal to "
                          << "VkWriteDescriptorSet descriptorCount " << update->descriptorCount;
            }
            *error_msg = error_str.str();
            return false;
        }
        // This error is probably unreachable due to the previous two errors
        if (write_inline_info && (write_inline_info->dataSize % 4) != 0) {
            *error_code = "VUID-VkWriteDescriptorSetInlineUniformBlock-dataSize-02222";
            std::stringstream error_str;
            error_str << "Attempting write update to " << dest_set->StringifySetAndLayout() << " binding #" << update->dstBinding
                      << " with "
                      << "VkWriteDescriptorSetInlineUniformBlock dataSize " << write_inline_info->dataSize
                      << " not a multiple of 4";
            *error_msg = error_str.str();
            return false;
        }
    }
    // Verify all bindings update share identical properties across all items
    if (update->descriptorCount > 0) {
        // Save first binding information and error if something different is found
        auto current_iter = dest_set->FindBinding(update->dstBinding);
        VkShaderStageFlags stage_flags = (*current_iter)->stage_flags;
        VkDescriptorType descriptor_type = (*current_iter)->type;
        const bool immutable_samplers = (*current_iter)->has_immutable_samplers;
        uint32_t dst_array_element = update->dstArrayElement;

        for (uint32_t i = 0; i < update->descriptorCount;) {
            if (current_iter == dest_set->end()) {
                break;  // prevents setting error here if bindings don't exist
            }
            auto current_binding = current_iter->get();

            // All consecutive bindings updated, except those with a descriptorCount of zero, must have identical descType and
            // stageFlags
            if (current_binding->count > 0) {
                // Check for consistent stageFlags and descriptorType
                if ((current_binding->stage_flags != stage_flags) || (current_binding->type != descriptor_type)) {
                    *error_code = "VUID-VkWriteDescriptorSet-descriptorCount-00317";
                    std::stringstream error_str;
                    error_str
                        << "Attempting write update to " << dest_set->StringifySetAndLayout() << " binding #"
                        << current_binding->binding << " (" << i << " from dstBinding offset)"
                        << " with a different stageFlag and/or descriptorType from previous bindings."
                        << " All bindings must have consecutive stageFlag and/or descriptorType across a VkWriteDescriptorSet";
                    *error_msg = error_str.str();
                    return false;
                }
                // Check if all immutableSamplers or not
                if (current_binding->has_immutable_samplers != immutable_samplers) {
                    *error_code = "VUID-VkWriteDescriptorSet-descriptorCount-00318";
                    std::stringstream error_str;
                    error_str << "Attempting write update to " << dest_set->StringifySetAndLayout() << " binding index #"
                              << current_binding->binding << " (" << i << " from dstBinding offset)"
                              << " with a different usage of immutable samplers from previous bindings."
                              << " All bindings must have all or none usage of immutable samplers across a VkWriteDescriptorSet";
                    *error_msg = error_str.str();
                    return false;
                }
            }

            // Skip the remaining descriptors for this binding, and move to the next binding
            i += (current_binding->count - dst_array_element);
            dst_array_element = 0;
            ++current_iter;
        }
    }

    // Verify consecutive bindings match (if needed)
    if (!VerifyUpdateConsistency(report_data, *dest_set, update->dstBinding, update->dstArrayElement, update->descriptorCount,
                                 "write update to", error_msg)) {
        *error_code = "VUID-VkWriteDescriptorSet-dstArrayElement-00321";
        return false;
    }
    const auto orig_binding = dest_set->GetBinding(update->dstBinding);
    // Verify write to variable descriptor
    if (orig_binding && orig_binding->IsVariableCount()) {
        if ((update->dstArrayElement + update->descriptorCount) > dest_set->GetVariableDescriptorCount()) {
            std::stringstream error_str;
            *error_code = "VUID-VkWriteDescriptorSet-dstArrayElement-00321";
            error_str << "Attempting write update to " << dest_set->StringifySetAndLayout() << " binding index #"
                      << update->dstBinding << " array element " << update->dstArrayElement << " with " << update->descriptorCount
                      << " writes but variable descriptor size is " << dest_set->GetVariableDescriptorCount();
            *error_msg = error_str.str();
            return false;
        }
    }
    auto start_idx = dest_set->GetGlobalIndexRangeFromBinding(update->dstBinding).start + update->dstArrayElement;
    // Update is within bounds and consistent so last step is to validate update contents
    if (!VerifyWriteUpdateContents(dest_set, update, start_idx, func_name, error_code, error_msg, push)) {
        std::stringstream error_str;
        error_str << "Write update to " << dest_set->StringifySetAndLayout() << " binding #" << update->dstBinding
                  << " failed with error message: " << error_msg->c_str();
        *error_msg = error_str.str();
        return false;
    }
    if (orig_binding != nullptr && orig_binding->type == VK_DESCRIPTOR_TYPE_MUTABLE_EXT) {
        // Check if the new descriptor descriptor type is in the list of allowed mutable types for this binding
        if (!dest_set->Layout().IsTypeMutable(update->descriptorType, update->dstBinding)) {
            *error_code = "VUID-VkWriteDescriptorSet-dstSet-04611";
            std::stringstream error_str;
            error_str << "Write update type is " << string_VkDescriptorType(update->descriptorType)
                      << ", but descriptor set layout binding was created with type VK_DESCRIPTOR_TYPE_MUTABLE_EXT and used type "
                         "is not in VkMutableDescriptorTypeListEXT::pDescriptorTypes for this binding.";
            *error_msg = error_str.str();
            return false;
        }
    }
    // All checks passed, update is clean
    return true;
}

// Verify that the contents of the update are ok, but don't perform actual update
bool CoreChecks::VerifyWriteUpdateContents(const DescriptorSet *dest_set, const VkWriteDescriptorSet *update, const uint32_t index,
                                           const char *func_name, std::string *error_code, std::string *error_msg,
                                           bool push) const {
    using ImageSamplerDescriptor = cvdescriptorset::ImageSamplerDescriptor;

    switch (update->descriptorType) {
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
            auto iter = dest_set->FindDescriptor(update->dstBinding, update->dstArrayElement);
            for (uint32_t di = 0; di < update->descriptorCount && !iter.AtEnd(); ++di, ++iter) {
                // Validate image
                auto image_view = update->pImageInfo[di].imageView;
                auto image_layout = update->pImageInfo[di].imageLayout;
                auto sampler = update->pImageInfo[di].sampler;
                auto iv_state = Get<IMAGE_VIEW_STATE>(image_view);
                const ImageSamplerDescriptor &desc = (const ImageSamplerDescriptor &)*iter;
                if (image_view) {
                    const auto *image_state = iv_state->image_state.get();
                    if (!ValidateImageUpdate(image_view, image_layout, update->descriptorType, func_name, error_code, error_msg)) {
                        std::stringstream error_str;
                        error_str << "Attempted write update to combined image sampler descriptor failed due to: "
                                  << error_msg->c_str();
                        *error_msg = error_str.str();
                        return false;
                    }
                    if (IsExtEnabled(device_extensions.vk_khr_sampler_ycbcr_conversion)) {
                        if (desc.IsImmutableSampler()) {
                            auto sampler_state = Get<SAMPLER_STATE>(desc.GetSampler());
                            if (iv_state && sampler_state) {
                                if (iv_state->samplerConversion != sampler_state->samplerConversion) {
                                    *error_code = "VUID-VkWriteDescriptorSet-descriptorType-01948";
                                    std::stringstream error_str;
                                    error_str
                                        << "Attempted write update to combined image sampler and image view and sampler ycbcr "
                                           "conversions are not identical, sampler: "
                                        << report_data->FormatHandle(desc.GetSampler())
                                        << " image view: " << report_data->FormatHandle(iv_state->image_view()) << ".";
                                    *error_msg = error_str.str();
                                    return false;
                                }
                            }
                        } else {
                            if (iv_state && (iv_state->samplerConversion != VK_NULL_HANDLE)) {
                                *error_code = "VUID-VkWriteDescriptorSet-descriptorType-02738";
                                std::stringstream error_str;
                                error_str << "Because dstSet (" << report_data->FormatHandle(update->dstSet)
                                          << ") is bound to image view (" << report_data->FormatHandle(iv_state->image_view())
                                          << ") that includes a YCBCR conversion, it must have been allocated with a layout that "
                                             "includes an immutable sampler.";
                                *error_msg = error_str.str();
                                return false;
                            }
                        }
                    }
                    // If there is an immutable sampler then |sampler| isn't used, so the following VU does not apply.
                    if (sampler && !desc.IsImmutableSampler() && FormatIsMultiplane(image_state->createInfo.format)) {
                        // multiplane formats must be created with mutable format bit
                        if (0 == (image_state->createInfo.flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT)) {
                            *error_code = "VUID-VkDescriptorImageInfo-sampler-01564";
                            std::stringstream error_str;
                            error_str << "image " << report_data->FormatHandle(image_state->image())
                                      << " combined image sampler is a multi-planar "
                                      << "format and was not was not created with the VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT";
                            *error_msg = error_str.str();
                            return false;
                        }
                        const VkFormat image_format = image_state->createInfo.format;
                        const VkImageAspectFlags image_aspect = iv_state->create_info.subresourceRange.aspectMask;
                        if (!IsValidPlaneAspect(image_format, image_aspect)) {
                            *error_code = "VUID-VkDescriptorImageInfo-sampler-01564";
                            std::stringstream error_str;
                            error_str << "image " << report_data->FormatHandle(image_state->image())
                                      << " combined image sampler is a multi-planar "
                                      << "format " << string_VkFormat(image_format) << " and "
                                      << report_data->FormatHandle(iv_state->image_view()) << " aspectMask is invalid";
                            *error_msg = error_str.str();
                            return false;
                        }
                    }

                    // Verify portability
                    auto sampler_state = Get<SAMPLER_STATE>(sampler);
                    if (sampler_state) {
                        if (IsExtEnabled(device_extensions.vk_khr_portability_subset)) {
                            if ((VK_FALSE == enabled_features.portability_subset_features.mutableComparisonSamplers) &&
                                (VK_FALSE != sampler_state->createInfo.compareEnable)) {
                                LogError(device, "VUID-VkDescriptorImageInfo-mutableComparisonSamplers-04450",
                                         "%s (portability error): sampler comparison not available.", func_name);
                            }
                        }
                    }
                }
            }
        }
            [[fallthrough]];
        case VK_DESCRIPTOR_TYPE_SAMPLER: {
            auto iter = dest_set->FindDescriptor(update->dstBinding, update->dstArrayElement);
            for (uint32_t di = 0; di < update->descriptorCount && !iter.AtEnd(); ++di, ++iter) {
                const auto &desc = *iter;
                if (!desc.IsImmutableSampler()) {
                    if (!ValidateSampler(update->pImageInfo[di].sampler)) {
                        *error_code = "VUID-VkWriteDescriptorSet-descriptorType-00325";
                        std::stringstream error_str;
                        error_str << "Attempted write update to sampler descriptor with invalid sampler: "
                                  << report_data->FormatHandle(update->pImageInfo[di].sampler) << ".";
                        *error_msg = error_str.str();
                        return false;
                    }
                } else if (update->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER && !push) {
                    *error_code = "VUID-VkWriteDescriptorSet-descriptorType-02752";
                    std::stringstream error_str;
                    error_str << "Attempted write update to an immutable sampler descriptor.";
                    *error_msg = error_str.str();
                    return false;
                }
            }
            break;
        }
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case VK_DESCRIPTOR_TYPE_SAMPLE_WEIGHT_IMAGE_QCOM:
        case VK_DESCRIPTOR_TYPE_BLOCK_MATCH_IMAGE_QCOM: {
            for (uint32_t di = 0; di < update->descriptorCount; ++di) {
                auto image_view = update->pImageInfo[di].imageView;
                auto image_layout = update->pImageInfo[di].imageLayout;
                if (image_view) {
                    if (!ValidateImageUpdate(image_view, image_layout, update->descriptorType, func_name, error_code, error_msg)) {
                        std::stringstream error_str;
                        error_str << "Attempted write update to image descriptor failed due to: " << error_msg->c_str();
                        *error_msg = error_str.str();
                        return false;
                    }
                }
            }
            break;
        }
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: {
            for (uint32_t di = 0; di < update->descriptorCount; ++di) {
                auto buffer_view = update->pTexelBufferView[di];
                if (buffer_view) {
                    auto bv_state = Get<BUFFER_VIEW_STATE>(buffer_view);
                    if (!bv_state) {
                        *error_code = "VUID-VkWriteDescriptorSet-descriptorType-02994";
                        std::stringstream error_str;
                        error_str << "Attempted write update to texel buffer descriptor with invalid buffer view: "
                                  << report_data->FormatHandle(buffer_view);
                        *error_msg = error_str.str();
                        return false;
                    }
                    auto buffer = bv_state->create_info.buffer;
                    auto buffer_state = Get<BUFFER_STATE>(buffer);
                    // Verify that buffer underlying the view hasn't been destroyed prematurely
                    if (!buffer_state) {
                        *error_code = "VUID-VkWriteDescriptorSet-descriptorType-02994";
                        std::stringstream error_str;
                        error_str << "Attempted write update to texel buffer descriptor failed because underlying buffer ("
                                  << report_data->FormatHandle(buffer) << ") has been destroyed: " << error_msg->c_str();
                        *error_msg = error_str.str();
                        return false;
                    } else if (!ValidateBufferUsage(report_data, buffer_state.get(), update->descriptorType, error_code,
                                                    error_msg)) {
                        std::stringstream error_str;
                        error_str << "Attempted write update to texel buffer descriptor failed due to: " << error_msg->c_str();
                        *error_msg = error_str.str();
                        return false;
                    }
                }
            }
            break;
        }
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
            for (uint32_t di = 0; di < update->descriptorCount; ++di) {
                if (update->pBufferInfo[di].buffer) {
                    if (!ValidateBufferUpdate(update->pBufferInfo + di, update->descriptorType, func_name, error_code, error_msg)) {
                        std::stringstream error_str;
                        error_str << "Attempted write update to buffer descriptor failed due to: " << error_msg->c_str();
                        *error_msg = error_str.str();
                        return false;
                    }
                }
            }
            break;
        }
        case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT:
            break;
        case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV: {
            const auto *acc_info = LvlFindInChain<VkWriteDescriptorSetAccelerationStructureNV>(update->pNext);
            for (uint32_t di = 0; di < update->descriptorCount; ++di) {
                auto as_state = Get<ACCELERATION_STRUCTURE_STATE>(acc_info->pAccelerationStructures[di]);
                if (!ValidateAccelerationStructureUpdate(as_state.get(), func_name, error_code, error_msg)) {
                    std::stringstream error_str;
                    error_str << "Attempted write update to acceleration structure descriptor failed due to: "
                              << error_msg->c_str();
                    *error_msg = error_str.str();
                    return false;
                }
            }

        } break;
        // KHR acceleration structures don't require memory to be bound manually to them.
        case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
            break;
        default:
            assert(0);  // We've already verified update type so should never get here
            break;
    }
    // All checks passed so update contents are good
    return true;
}

bool CoreChecks::PreCallValidateCmdSetDescriptorBufferOffsetsEXT(VkCommandBuffer commandBuffer,
                                                                 VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout,
                                                                 uint32_t firstSet, uint32_t setCount,
                                                                 const uint32_t *pBufferIndices,
                                                                 const VkDeviceSize *pOffsets) const {
    auto cb_state = GetRead<CMD_BUFFER_STATE>(commandBuffer);
    auto pipeline_layout = Get<PIPELINE_LAYOUT_STATE>(layout);
    assert(cb_state);
    assert(pipeline_layout);

    bool skip = false;

    static const std::map<VkPipelineBindPoint, std::string> bindpoint_errors = {
        std::make_pair(VK_PIPELINE_BIND_POINT_GRAPHICS, "VUID-vkCmdSetDescriptorBufferOffsetsEXT-pipelineBindPoint-08067"),
        std::make_pair(VK_PIPELINE_BIND_POINT_COMPUTE, "VUID-vkCmdSetDescriptorBufferOffsetsEXT-pipelineBindPoint-08067"),
        std::make_pair(VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, "VUID-vkCmdSetDescriptorBufferOffsetsEXT-pipelineBindPoint-08067")};
    skip |= ValidatePipelineBindPoint(cb_state.get(), pipelineBindPoint, "vkCmdSetDescriptorBufferOffsetsEXT()", bindpoint_errors);

    if (!enabled_features.descriptor_buffer_features.descriptorBuffer) {
        skip |= LogError(device, "VUID-vkCmdSetDescriptorBufferOffsetsEXT-None-08060",
                         "vkCmdSetDescriptorBufferOffsetsEXT(): The descriptorBuffer feature "
                         "must be enabled.");
    }

    if ((firstSet + setCount) > pipeline_layout->set_layouts.size()) {
        skip |= LogError(device, "VUID-vkCmdSetDescriptorBufferOffsetsEXT-firstSet-08066",
                         "vkCmdSetDescriptorBufferOffsetsEXT(): The sum of firstSet (%" PRIu32 ") and setCount (%" PRIu32
                         ") is greater than VkPipelineLayoutCreateInfo::setLayoutCount (%" PRIuLEAST64 ") when layout was created",
                         firstSet, setCount, (uint64_t)pipeline_layout->set_layouts.size());

        // Clamp so that we don't attempt to access invalid stuff
        setCount = std::min(setCount, static_cast<uint32_t>(pipeline_layout->set_layouts.size()));
    }

    for (uint32_t i = 0; i < setCount; i++) {
        const uint32_t bufferIndex = pBufferIndices[i];
        const VkDeviceAddress offset = pOffsets[i];
        bool valid_buffer = false;
        bool valid_binding = false;

        const auto set_layout = pipeline_layout->set_layouts[firstSet + i];
        if ((set_layout->GetCreateFlags() & VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT) == 0) {
            const LogObjectList objlist(cb_state->commandBuffer(), set_layout->GetDescriptorSetLayout(), pipeline_layout->layout());
            skip |= LogError(objlist, "VUID-vkCmdSetDescriptorBufferOffsetsEXT-firstSet-09006",
                             "vkCmdSetDescriptorBufferOffsetsEXT(): Descriptor set layout (%s) for set %" PRIu32
                             " was created without VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT flag set",
                             report_data->FormatHandle(set_layout->GetDescriptorSetLayout()).c_str(), firstSet + i);
        }

        if (bufferIndex < cb_state->descriptor_buffer_binding_info.size()) {
            const VkDeviceAddress start = cb_state->descriptor_buffer_binding_info[bufferIndex].address;
            const auto buffer_states = GetBuffersByAddress(start);

            if (!buffer_states.empty()) {
                const auto buffer_state_starts = GetBuffersByAddress(start + offset);

                if (!buffer_state_starts.empty()) {
                    const auto bindings = set_layout->GetBindings();
                    const auto pSetLayoutSize = set_layout->GetLayoutSizeInBytes();
                    VkDeviceSize setLayoutSize = 0;

                    if (pSetLayoutSize == nullptr) {
                        const auto pool = cb_state->command_pool;
                        DispatchGetDescriptorSetLayoutSizeEXT(pool->dev_data->device, set_layout->GetDescriptorSetLayout(),
                                                              &setLayoutSize);
                    } else {
                        setLayoutSize = *pSetLayoutSize;
                    }

                    if (setLayoutSize > 0) {
                        // It looks like enough to check last binding in set
                        for (uint32_t j = 0; j < set_layout->GetBindingCount(); j++) {
                            const VkDescriptorBindingFlags flags = set_layout->GetDescriptorBindingFlagsFromIndex(j);
                            const bool vdc = (flags & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT) != 0;

                            if (vdc) {
                                // If a binding is VARIABLE_DESCRIPTOR_COUNT, the effective setLayoutSize we
                                // must validate is just the offset of the last binding.
                                const auto pool = cb_state->command_pool;
                                uint32_t binding = set_layout->GetDescriptorSetLayoutBindingPtrFromIndex(j)->binding;
                                DispatchGetDescriptorSetLayoutBindingOffsetEXT(
                                    pool->dev_data->device, set_layout->GetDescriptorSetLayout(), binding, &setLayoutSize);

                                // If the descriptor set only consists of VARIABLE_DESCRIPTOR_COUNT bindings, the
                                // offset may be 0. In this case, treat the descriptor set layout as size 1,
                                // so we validate that the offset is sensible.
                                if (set_layout->GetBindingCount() == 1) {
                                    setLayoutSize = 1;
                                }

                                // There can only be one binding with VARIABLE_COUNT.
                                break;
                            }
                        }
                    }

                    if (setLayoutSize > 0) {
                        const auto buffer_state_ends = GetBuffersByAddress(start + offset + setLayoutSize - 1);
                        if (!buffer_state_ends.empty()) {
                            valid_binding = true;
                        }
                    }
                }

                valid_buffer = true;
            }

            if (!valid_binding) {
                skip |= LogError(device, "VUID-vkCmdSetDescriptorBufferOffsetsEXT-pOffsets-08063",
                                 "vkCmdSetDescriptorBufferOffsetsEXT(): pOffsets[%" PRIu32
                                 "]: The offsets in pOffsets must be small enough such that any descriptor binding"
                                 " referenced by layout without the VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT"
                                 " flag computes a valid address inside the underlying VkBuffer",
                                 i);
            }
        }

        if (!valid_buffer) {
            skip |= LogError(device, "VUID-vkCmdSetDescriptorBufferOffsetsEXT-pBufferIndices-08065",
                             "vkCmdSetDescriptorBufferOffsetsEXT(): pBufferIndices[%" PRIu32
                             "]: Each element of pBufferIndices must reference a valid descriptor buffer binding "
                             "set by a previous call to vkCmdBindDescriptorBuffersEXT in commandBuffer",
                             i);
        }

        if (pBufferIndices[i] >= phys_dev_ext_props.descriptor_buffer_props.maxDescriptorBufferBindings) {
            skip |= LogError(device, "VUID-vkCmdSetDescriptorBufferOffsetsEXT-pBufferIndices-08064",
                             "vkCmdSetDescriptorBufferOffsetsEXT(): pBufferIndices[%" PRIu32 "] (%" PRIu32
                             ") "
                             "is greater than VkPhysicalDeviceDescriptorBufferPropertiesEXT::maxDescriptorBufferBindings (%" PRIu32
                             ") ",
                             i, pBufferIndices[i], phys_dev_ext_props.descriptor_buffer_props.maxDescriptorBufferBindings);
        }

        if (SafeModulo(pOffsets[i], phys_dev_ext_props.descriptor_buffer_props.descriptorBufferOffsetAlignment) != 0) {
            skip |= LogError(device, "VUID-vkCmdSetDescriptorBufferOffsetsEXT-pOffsets-08061",
                             "vkCmdSetDescriptorBufferOffsetsEXT(): pOffsets[%" PRIu32 "] (%" PRIuLEAST64
                             ") is not aligned to VkPhysicalDeviceDescriptorBufferPropertiesEXT::descriptorBufferOffsetAlignment"
                             " (%" PRIuLEAST64 ")",
                             i, pOffsets[i], phys_dev_ext_props.descriptor_buffer_props.descriptorBufferOffsetAlignment);
        }
    }

    return skip;
}

bool CoreChecks::PreCallValidateCmdBindDescriptorBufferEmbeddedSamplersEXT(VkCommandBuffer commandBuffer,
                                                                           VkPipelineBindPoint pipelineBindPoint,
                                                                           VkPipelineLayout layout, uint32_t set) const {
    auto cb_state = GetRead<CMD_BUFFER_STATE>(commandBuffer);
    assert(cb_state);

    bool skip = false;

    if (!enabled_features.descriptor_buffer_features.descriptorBuffer) {
        skip |= LogError(device, "VUID-vkCmdBindDescriptorBufferEmbeddedSamplersEXT-None-08068",
                         "vkCmdBindDescriptorBufferEmbeddedSamplersEXT(): The descriptorBuffer feature "
                         "must be enabled.");
    }

    static const std::map<VkPipelineBindPoint, std::string> bindpoint_errors = {
        std::make_pair(VK_PIPELINE_BIND_POINT_GRAPHICS,
                       "VUID-vkCmdBindDescriptorBufferEmbeddedSamplersEXT-pipelineBindPoint-08069"),
        std::make_pair(VK_PIPELINE_BIND_POINT_COMPUTE, "VUID-vkCmdBindDescriptorBufferEmbeddedSamplersEXT-pipelineBindPoint-08069"),
        std::make_pair(VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                       "VUID-vkCmdBindDescriptorBufferEmbeddedSamplersEXT-pipelineBindPoint-08069")};
    skip |= ValidatePipelineBindPoint(cb_state.get(), pipelineBindPoint, "vkCmdBindDescriptorBufferEmbeddedSamplersEXT()",
                                      bindpoint_errors);

    auto pipeline_layout = Get<PIPELINE_LAYOUT_STATE>(layout);
    if (set >= pipeline_layout->set_layouts.size()) {
        skip |= LogError(device, "VUID-vkCmdBindDescriptorBufferEmbeddedSamplersEXT-set-08071",
                         "vkCmdBindDescriptorBufferEmbeddedSamplersEXT(): set (%" PRIu32
                         ") is greater than "
                         "VkPipelineLayoutCreateInfo::setLayoutCount (%" PRIuLEAST64 ") when layout was created.",
                         set, (uint64_t)pipeline_layout->set_layouts.size());
    } else {
        auto set_layout = pipeline_layout->set_layouts[set];
        if (!(set_layout->GetCreateFlags() & VK_DESCRIPTOR_SET_LAYOUT_CREATE_EMBEDDED_IMMUTABLE_SAMPLERS_BIT_EXT)) {
            skip |= LogError(device, "VUID-vkCmdBindDescriptorBufferEmbeddedSamplersEXT-set-08070",
                             "vkCmdBindDescriptorBufferEmbeddedSamplersEXT(): layout must have been created with the "
                             "VK_DESCRIPTOR_SET_LAYOUT_CREATE_EMBEDDED_IMMUTABLE_SAMPLERS_BIT_EXT flag set.");
        }
    }

    return skip;
}

bool CoreChecks::PreCallValidateCmdBindDescriptorBuffersEXT(VkCommandBuffer commandBuffer, uint32_t bufferCount,
                                                            const VkDescriptorBufferBindingInfoEXT *pBindingInfos) const {
    auto cb_state = GetRead<CMD_BUFFER_STATE>(commandBuffer);
    assert(cb_state);

    bool skip = false;

    if (!enabled_features.descriptor_buffer_features.descriptorBuffer) {
        skip |= LogError(device, "VUID-vkCmdBindDescriptorBuffersEXT-None-08047",
                         "vkCmdBindDescriptorBuffersEXT(): The descriptorBuffer feature must be enabled.");
    }

    uint32_t num_sampler_buffers = 0;
    uint32_t num_resource_buffers = 0;
    uint32_t num_push_descriptor_buffers = 0;

    for (uint32_t i = 0; i < bufferCount; i++) {
        const VkDescriptorBufferBindingInfoEXT &bindingInfo = pBindingInfos[i];
        const auto buffer_states = GetBuffersByAddress(bindingInfo.address);
        // Try to find a valid buffer in buffer_states.
        // If none if found, output each violated VUIDs, with the list of buffers that violate it.
        {
            using BUFFER_STATE_PTR = ValidationStateTracker::BUFFER_STATE_PTR;
            BufferAddressValidation<5> buffer_address_validator = {{{
                {"VUID-vkCmdBindDescriptorBuffersEXT-pBindingInfos-08052", LogObjectList(device),
                 [this, commandBuffer](const BUFFER_STATE_PTR &buffer_state, std::string *out_error_msg) {
                     if (!out_error_msg) {
                         return !buffer_state->sparse && buffer_state->IsMemoryBound();
                     } else {
                         return ValidateMemoryIsBoundToBuffer(commandBuffer, *buffer_state, "vkCmdBindDescriptorBuffersEXT()",
                                                              "VUID-vkCmdBindDescriptorBuffersEXT-pBindingInfos-08052");
                     }
                 }},

                {"VUID-vkCmdBindDescriptorBuffersEXT-pBindingInfos-08055", LogObjectList(device),
                 [binding_usage = bindingInfo.usage](const BUFFER_STATE_PTR &buffer_state, std::string *out_error_msg) {
                     if ((buffer_state->createInfo.usage &
                          (VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
                           VK_BUFFER_USAGE_PUSH_DESCRIPTORS_DESCRIPTOR_BUFFER_BIT_EXT)) !=
                         (binding_usage &
                          (VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
                           VK_BUFFER_USAGE_PUSH_DESCRIPTORS_DESCRIPTOR_BUFFER_BIT_EXT))) {
                         if (out_error_msg) {
                             *out_error_msg += "buffer has usage " + string_VkBufferUsageFlags(buffer_state->createInfo.usage);
                         }
                         return false;
                     }
                     return true;
                 },
                 [binding_usage = bindingInfo.usage, i]() {
                     return "The following buffers have a usage that does not match pBindingInfos[" + std::to_string(i) +
                            "].usage (" + string_VkBufferUsageFlags(binding_usage) + "):\n";
                 }},

                {"VUID-VkDescriptorBufferBindingInfoEXT-usage-08122", LogObjectList(device),
                 [binding_usage = bindingInfo.usage, &num_sampler_buffers](const BUFFER_STATE_PTR &buffer_state,
                                                                           std::string *out_error_msg) {
                     if (binding_usage & VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT) {
                         ++num_sampler_buffers;
                         if (!(buffer_state->createInfo.usage & VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT)) {
                             if (out_error_msg) {
                                 *out_error_msg += "has usage " + string_VkBufferUsageFlags(buffer_state->createInfo.usage);
                             }
                             return false;
                         }
                     }
                     return true;
                 },
                 []() {
                     return "The following buffers were not created with VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT:\n";
                 }},

                {"VUID-VkDescriptorBufferBindingInfoEXT-usage-08123", LogObjectList(device),
                 [binding_usage = bindingInfo.usage, &num_resource_buffers](const BUFFER_STATE_PTR &buffer_state,
                                                                            std::string *out_error_msg) {
                     if (binding_usage & VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT) {
                         ++num_resource_buffers;
                         if (!(buffer_state->createInfo.usage & VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT)) {
                             if (out_error_msg) {
                                 *out_error_msg += "buffer has usage " + string_VkBufferUsageFlags(buffer_state->createInfo.usage);
                             }
                             return false;
                         }
                     }
                     return true;
                 },
                 []() {
                     return "The following buffers were not created with VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT:\n";
                 }},

                {"VUID-VkDescriptorBufferBindingInfoEXT-usage-08124", LogObjectList(device),
                 [binding_usage = bindingInfo.usage, &num_push_descriptor_buffers](const BUFFER_STATE_PTR &buffer_state,
                                                                                   std::string *out_error_msg) {
                     if (binding_usage & VK_BUFFER_USAGE_PUSH_DESCRIPTORS_DESCRIPTOR_BUFFER_BIT_EXT) {
                         ++num_push_descriptor_buffers;
                         if (!(buffer_state->createInfo.usage & VK_BUFFER_USAGE_PUSH_DESCRIPTORS_DESCRIPTOR_BUFFER_BIT_EXT)) {
                             if (out_error_msg) {
                                 *out_error_msg += "buffer has usage " + string_VkBufferUsageFlags(buffer_state->createInfo.usage);
                             }
                             return false;
                         }
                     }
                     return true;
                 },
                 []() {
                     return "The following buffers were not created with "
                            "VK_BUFFER_USAGE_PUSH_DESCRIPTORS_DESCRIPTOR_BUFFER_BIT_EXT:\n";
                 }},
            }}};

            const std::string address_name = "pBindingInfos[" + std::to_string(i) + "].address";
            skip |= buffer_address_validator.LogErrorsIfNoValidBuffer(*this, buffer_states, "vkCmdBindDescriptorBuffersEXT()",
                                                                      address_name, bindingInfo.address);
        }

        const auto *buffer_handle = LvlFindInChain<VkDescriptorBufferBindingPushDescriptorBufferHandleEXT>(pBindingInfos[i].pNext);
        if (!phys_dev_ext_props.descriptor_buffer_props.bufferlessPushDescriptors &&
            (pBindingInfos[i].usage & VK_BUFFER_USAGE_PUSH_DESCRIPTORS_DESCRIPTOR_BUFFER_BIT_EXT) && !buffer_handle) {
            skip |= LogError(device, "VUID-VkDescriptorBufferBindingInfoEXT-bufferlessPushDescriptors-08056",
                             "vkCmdBindDescriptorBuffersEXT(): pBindingInfos[%" PRIu32
                             "].pNext does not contain a VkDescriptorBufferBindingPushDescriptorBufferHandleEXT structure, but "
                             "VkPhysicalDeviceDescriptorBufferPropertiesEXT::bufferlessPushDescriptors is VK_FALSE and usage "
                             "contains VK_BUFFER_USAGE_PUSH_DESCRIPTORS_DESCRIPTOR_BUFFER_BIT_EXT",
                             i);
        }

        if (SafeModulo(pBindingInfos[i].address, phys_dev_ext_props.descriptor_buffer_props.descriptorBufferOffsetAlignment) != 0) {
            skip |=
                LogError(device, "VUID-VkDescriptorBufferBindingInfoEXT-address-08057",
                         "vkCmdBindDescriptorBuffersEXT(): pBindingInfos[%" PRIu32 "].address (%" PRIuLEAST64
                         ") is not aligned "
                         "to VkPhysicalDeviceDescriptorBufferPropertiesEXT::descriptorBufferOffsetAlignment (%" PRIuLEAST64 ")",
                         i, pBindingInfos[i].address, phys_dev_ext_props.descriptor_buffer_props.descriptorBufferOffsetAlignment);
        }

        if (buffer_handle && phys_dev_ext_props.descriptor_buffer_props.bufferlessPushDescriptors) {
            skip |= LogError(device, "VUID-VkDescriptorBufferBindingPushDescriptorBufferHandleEXT-bufferlessPushDescriptors-08059",
                             "vkCmdBindDescriptorBuffersEXT(): pBindingInfos[%" PRIu32
                             "].pNext contains a VkDescriptorBufferBindingPushDescriptorBufferHandleEXT structure, "
                             "but VkPhysicalDeviceDescriptorBufferPropertiesEXT::bufferlessPushDescriptors is VK_TRUE",
                             i);
        }
    }

    if (num_sampler_buffers > phys_dev_ext_props.descriptor_buffer_props.maxSamplerDescriptorBufferBindings) {
        skip |= LogError(device, "VUID-vkCmdBindDescriptorBuffersEXT-maxSamplerDescriptorBufferBindings-08048",
                         "vkCmdBindDescriptorBuffersEXT(): Number of sampler buffers is %" PRIu32
                         ". There must be no more than "
                         "VkPhysicalDeviceDescriptorBufferPropertiesEXT::maxSamplerDescriptorBufferBindings (%" PRIu32
                         ") descriptor buffers containing sampler descriptor data bound. ",
                         num_sampler_buffers, phys_dev_ext_props.descriptor_buffer_props.maxSamplerDescriptorBufferBindings);
    }

    if (num_resource_buffers > phys_dev_ext_props.descriptor_buffer_props.maxResourceDescriptorBufferBindings) {
        skip |= LogError(device, "VUID-vkCmdBindDescriptorBuffersEXT-maxResourceDescriptorBufferBindings-08049",
                         "vkCmdBindDescriptorBuffersEXT(): Number of resource buffers is %" PRIu32
                         ". There must be no more than "
                         "VkPhysicalDeviceDescriptorBufferPropertiesEXT::maxResourceDescriptorBufferBindings (%" PRIu32
                         ") descriptor buffers containing resource descriptor data bound.",
                         num_resource_buffers, phys_dev_ext_props.descriptor_buffer_props.maxResourceDescriptorBufferBindings);
    }

    if (num_push_descriptor_buffers > 1) {
        skip |= LogError(device, "VUID-vkCmdBindDescriptorBuffersEXT-None-08050",
                         "vkCmdBindDescriptorBuffersEXT(): Number of descriptor buffers is %" PRIu32
                         ". "
                         "There must be no more than 1 descriptor buffer bound that was created "
                         "with the VK_BUFFER_USAGE_PUSH_DESCRIPTORS_DESCRIPTOR_BUFFER_BIT_EXT bit set.",
                         num_push_descriptor_buffers);
    }

    if (bufferCount > phys_dev_ext_props.descriptor_buffer_props.maxDescriptorBufferBindings) {
        skip |= LogError(device, "VUID-vkCmdBindDescriptorBuffersEXT-bufferCount-08051",
                         "vkCmdBindDescriptorBuffersEXT(): bufferCount (%" PRIu32
                         ") must be less than or equal to "
                         "VkPhysicalDeviceDescriptorBufferPropertiesEXT::maxDescriptorBufferBindings (%" PRIu32 ").",
                         bufferCount, phys_dev_ext_props.descriptor_buffer_props.maxDescriptorBufferBindings);
    }

    return skip;
}

bool CoreChecks::PreCallValidateGetDescriptorSetLayoutSizeEXT(VkDevice device, VkDescriptorSetLayout layout,
                                                              VkDeviceSize *pLayoutSizeInBytes) const {
    bool skip = false;

    if (!enabled_features.descriptor_buffer_features.descriptorBuffer) {
        skip |= LogError(device, "VUID-vkGetDescriptorSetLayoutSizeEXT-None-08011",
                         "vkGetDescriptorSetLayoutSizeEXT(): The descriptorBuffer feature must be enabled.");
    }

    auto setlayout = Get<cvdescriptorset::DescriptorSetLayout>(layout);

    if (!(setlayout->GetCreateFlags() & VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT)) {
        skip |= LogError(device, "VUID-vkGetDescriptorSetLayoutSizeEXT-layout-08012",
                         "vkGetDescriptorSetLayoutSizeEXT(): layout must have been created with the "
                         "VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT flag set.");
    }

    return skip;
}

bool CoreChecks::PreCallValidateGetDescriptorSetLayoutBindingOffsetEXT(VkDevice device, VkDescriptorSetLayout layout,
                                                                       uint32_t binding, VkDeviceSize *pOffset) const {
    bool skip = false;

    if (!enabled_features.descriptor_buffer_features.descriptorBuffer) {
        skip |= LogError(device, "VUID-vkGetDescriptorSetLayoutBindingOffsetEXT-None-08013",
                         "vkGetDescriptorSetLayoutBindingOffsetEXT(): The descriptorBuffer feature must be enabled.");
    }

    auto setlayout = Get<cvdescriptorset::DescriptorSetLayout>(layout);

    if (!(setlayout->GetCreateFlags() & VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT)) {
        skip |= LogError(device, "VUID-vkGetDescriptorSetLayoutBindingOffsetEXT-layout-08014",
                         "vkGetDescriptorSetLayoutBindingOffsetEXT(): layout must have been created with the "
                         "VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT flag set.");
    }

    return skip;
}

bool CoreChecks::PreCallValidateGetBufferOpaqueCaptureDescriptorDataEXT(VkDevice device,
                                                                        const VkBufferCaptureDescriptorDataInfoEXT *pInfo,
                                                                        void *pData) const {
    bool skip = false;

    if (!enabled_features.descriptor_buffer_features.descriptorBufferCaptureReplay) {
        skip |= LogError(pInfo->buffer, "VUID-vkGetBufferOpaqueCaptureDescriptorDataEXT-None-08072",
                         "vkGetBufferOpaqueCaptureDescriptorDataEXT(): The descriptorBufferCaptureReplay feature must be enabled.");
    }

    if (physical_device_count > 1 && !enabled_features.core12.bufferDeviceAddressMultiDevice &&
        !enabled_features.buffer_device_address_ext_features.bufferDeviceAddressMultiDevice) {
        skip |=
            LogError(pInfo->buffer, "VUID-vkGetBufferOpaqueCaptureDescriptorDataEXT-device-08074",
                     "vkGetBufferOpaqueCaptureDescriptorDataEXT(): If device was created with multiple physical devices, then the "
                     "bufferDeviceAddressMultiDevice feature must be enabled.");
    }

    auto buffer_state = Get<BUFFER_STATE>(pInfo->buffer);

    if (buffer_state) {
        if (!(buffer_state->createInfo.flags & VK_BUFFER_CREATE_DESCRIPTOR_BUFFER_CAPTURE_REPLAY_BIT_EXT)) {
            skip |= LogError(pInfo->buffer, "VUID-VkBufferCaptureDescriptorDataInfoEXT-buffer-08075",
                             "VkBufferCaptureDescriptorDataInfoEXT: pInfo->buffer must have been created with the "
                             "VK_BUFFER_CREATE_DESCRIPTOR_BUFFER_CAPTURE_REPLAY_BIT_EXT flag set.");
        }
    }

    return skip;
}

bool CoreChecks::PreCallValidateGetImageOpaqueCaptureDescriptorDataEXT(VkDevice device,
                                                                       const VkImageCaptureDescriptorDataInfoEXT *pInfo,
                                                                       void *pData) const {
    bool skip = false;

    if (!enabled_features.descriptor_buffer_features.descriptorBufferCaptureReplay) {
        skip |= LogError(pInfo->image, "VUID-vkGetImageOpaqueCaptureDescriptorDataEXT-None-08076",
                         "vkGetImageOpaqueCaptureDescriptorDataEXT(): The descriptorBufferCaptureReplay feature must be enabled.");
    }

    if (physical_device_count > 1 && !enabled_features.core12.bufferDeviceAddressMultiDevice &&
        !enabled_features.buffer_device_address_ext_features.bufferDeviceAddressMultiDevice) {
        skip |=
            LogError(pInfo->image, "VUID-vkGetImageOpaqueCaptureDescriptorDataEXT-device-08078",
                     "vkGetImageOpaqueCaptureDescriptorDataEXT(): If device was created with multiple physical devices, then the "
                     "bufferDeviceAddressMultiDevice feature must be enabled.");
    }

    auto image_state = Get<IMAGE_STATE>(pInfo->image);

    if (image_state) {
        if (!(image_state->createInfo.flags & VK_IMAGE_CREATE_DESCRIPTOR_BUFFER_CAPTURE_REPLAY_BIT_EXT)) {
            skip |= LogError(pInfo->image, "VUID-VkImageCaptureDescriptorDataInfoEXT-image-08079",
                             "VkImageCaptureDescriptorDataInfoEXT: pInfo->image must have been created with the "
                             "VK_IMAGE_CREATE_DESCRIPTOR_BUFFER_CAPTURE_REPLAY_BIT_EXT flag set.");
        }
    }

    return skip;
}

bool CoreChecks::PreCallValidateGetImageViewOpaqueCaptureDescriptorDataEXT(VkDevice device,
                                                                           const VkImageViewCaptureDescriptorDataInfoEXT *pInfo,
                                                                           void *pData) const {
    bool skip = false;

    if (!enabled_features.descriptor_buffer_features.descriptorBufferCaptureReplay) {
        skip |=
            LogError(pInfo->imageView, "VUID-vkGetImageViewOpaqueCaptureDescriptorDataEXT-None-08080",
                     "vkGetImageViewOpaqueCaptureDescriptorDataEXT(): The descriptorBufferCaptureReplay feature must be enabled.");
    }

    if (physical_device_count > 1 && !enabled_features.core12.bufferDeviceAddressMultiDevice &&
        !enabled_features.buffer_device_address_ext_features.bufferDeviceAddressMultiDevice) {
        skip |= LogError(
            pInfo->imageView, "VUID-vkGetImageViewOpaqueCaptureDescriptorDataEXT-device-08082",
            "vkGetImageViewOpaqueCaptureDescriptorDataEXT(): If device was created with multiple physical devices, then the "
            "bufferDeviceAddressMultiDevice feature must be enabled.");
    }

    auto image_view_state = Get<IMAGE_VIEW_STATE>(pInfo->imageView);

    if (image_view_state) {
        if (!(image_view_state->create_info.flags & VK_IMAGE_VIEW_CREATE_DESCRIPTOR_BUFFER_CAPTURE_REPLAY_BIT_EXT)) {
            skip |= LogError(pInfo->imageView, "VUID-VkImageViewCaptureDescriptorDataInfoEXT-imageView-08083",
                             "VkImageCaptureDescriptorDataInfoEXT: pInfo->imageView must have been created with the "
                             "VK_IMAGE_VIEW_CREATE_DESCRIPTOR_BUFFER_CAPTURE_REPLAY_BIT_EXT flag set.");
        }
    }

    return skip;
}

bool CoreChecks::PreCallValidateGetSamplerOpaqueCaptureDescriptorDataEXT(VkDevice device,
                                                                         const VkSamplerCaptureDescriptorDataInfoEXT *pInfo,
                                                                         void *pData) const {
    bool skip = false;

    if (!enabled_features.descriptor_buffer_features.descriptorBufferCaptureReplay) {
        skip |=
            LogError(pInfo->sampler, "VUID-vkGetSamplerOpaqueCaptureDescriptorDataEXT-None-08084",
                     "vkGetSamplerOpaqueCaptureDescriptorDataEXT(): The descriptorBufferCaptureReplay feature must be enabled.");
    }

    if (physical_device_count > 1 && !enabled_features.core12.bufferDeviceAddressMultiDevice &&
        !enabled_features.buffer_device_address_ext_features.bufferDeviceAddressMultiDevice) {
        skip |=
            LogError(pInfo->sampler, "VUID-vkGetSamplerOpaqueCaptureDescriptorDataEXT-device-08086",
                     "vkGetSamplerOpaqueCaptureDescriptorDataEXT(): If device was created with multiple physical devices, then the "
                     "bufferDeviceAddressMultiDevice feature must be enabled.");
    }

    auto sampler_state = Get<SAMPLER_STATE>(pInfo->sampler);

    if (sampler_state) {
        if (!(sampler_state->createInfo.flags & VK_SAMPLER_CREATE_DESCRIPTOR_BUFFER_CAPTURE_REPLAY_BIT_EXT)) {
            skip |= LogError(pInfo->sampler, "VUID-VkSamplerCaptureDescriptorDataInfoEXT-sampler-08087",
                             "VkSamplerCaptureDescriptorDataInfoEXT: pInfo->sampler must have been created with the "
                             "VK_SAMPLER_CREATE_DESCRIPTOR_BUFFER_CAPTURE_REPLAY_BIT_EXT flag set.");
        }
    }

    return skip;
}

bool CoreChecks::PreCallValidateGetAccelerationStructureOpaqueCaptureDescriptorDataEXT(
    VkDevice device, const VkAccelerationStructureCaptureDescriptorDataInfoEXT *pInfo, void *pData) const {
    bool skip = false;

    if (!enabled_features.descriptor_buffer_features.descriptorBufferCaptureReplay) {
        skip |= LogError(device, "VUID-vkGetAccelerationStructureOpaqueCaptureDescriptorDataEXT-None-08088",
                         "vkGetAccelerationStructureOpaqueCaptureDescriptorDataEXT(): The descriptorBufferCaptureReplay feature "
                         "must be enabled.");
    }

    if (physical_device_count > 1 && !enabled_features.core12.bufferDeviceAddressMultiDevice &&
        !enabled_features.buffer_device_address_ext_features.bufferDeviceAddressMultiDevice) {
        skip |= LogError(device, "VUID-vkGetAccelerationStructureOpaqueCaptureDescriptorDataEXT-device-08090",
                         "vkGetAccelerationStructureOpaqueCaptureDescriptorDataEXT(): If device was created with multiple physical "
                         "devices (%" PRIu32 "), then the bufferDeviceAddressMultiDevice feature must be enabled.",
                         physical_device_count);
    }

    if (pInfo->accelerationStructure != VK_NULL_HANDLE) {
        auto acceleration_structure_state = Get<ACCELERATION_STRUCTURE_STATE_KHR>(pInfo->accelerationStructure);

        if (acceleration_structure_state) {
            if (!(acceleration_structure_state->create_infoKHR.createFlags &
                  VK_ACCELERATION_STRUCTURE_CREATE_DESCRIPTOR_BUFFER_CAPTURE_REPLAY_BIT_EXT)) {
                skip |= LogError(pInfo->accelerationStructure,
                                 "VUID-VkAccelerationStructureCaptureDescriptorDataInfoEXT-accelerationStructure-08091",
                                 "VkAccelerationStructureCaptureDescriptorDataInfoEXT: pInfo->accelerationStructure must have been "
                                 "created with the "
                                 "VK_ACCELERATION_STRUCTURE_CREATE_DESCRIPTOR_BUFFER_CAPTURE_REPLAY_BIT_EXT flag set.");
            }
        }

        if (pInfo->accelerationStructureNV != VK_NULL_HANDLE) {
            LogError(device, "VUID-VkAccelerationStructureCaptureDescriptorDataInfoEXT-accelerationStructure-08093",
                     "VkAccelerationStructureCaptureDescriptorDataInfoEXT(): If accelerationStructure is not VK_NULL_HANDLE, "
                     "accelerationStructureNV must be VK_NULL_HANDLE. ");
        }
    }

    if (pInfo->accelerationStructureNV != VK_NULL_HANDLE) {
        auto acceleration_structure_state = Get<ACCELERATION_STRUCTURE_STATE>(pInfo->accelerationStructureNV);

        if (acceleration_structure_state) {
            if (!(acceleration_structure_state->create_infoNV.info.flags &
                  VK_ACCELERATION_STRUCTURE_CREATE_DESCRIPTOR_BUFFER_CAPTURE_REPLAY_BIT_EXT)) {
                skip |= LogError(pInfo->accelerationStructureNV,
                                 "VUID-VkAccelerationStructureCaptureDescriptorDataInfoEXT-accelerationStructureNV-08092",
                                 "VkAccelerationStructureCaptureDescriptorDataInfoEXT: pInfo->accelerationStructure must have been "
                                 "created with the "
                                 "VK_ACCELERATION_STRUCTURE_CREATE_DESCRIPTOR_BUFFER_CAPTURE_REPLAY_BIT_EXT flag set.");
            }
        }

        if (pInfo->accelerationStructure != VK_NULL_HANDLE) {
            LogError(device, "VUID-VkAccelerationStructureCaptureDescriptorDataInfoEXT-accelerationStructureNV-08094",
                     "VkAccelerationStructureCaptureDescriptorDataInfoEXT(): If accelerationStructureNV is not VK_NULL_HANDLE, "
                     "accelerationStructure must be VK_NULL_HANDLE. ");
        }
    }

    return skip;
}

bool CoreChecks::ValidateDescriptorAddressInfoEXT(VkDevice device, const VkDescriptorAddressInfoEXT *address_info) const {
    bool skip = false;

    if ((address_info->address == 0) && !enabled_features.robustness2_features.nullDescriptor) {
        skip |= LogError(device, "VUID-VkDescriptorAddressInfoEXT-address-08043",
                         "VkDescriptorAddressInfoEXT: address is 0, but the nullDescriptor feature is not enabled.");
    }

    const auto buffer_states = GetBuffersByAddress(address_info->address);
    if ((address_info->address != 0) && buffer_states.empty()) {
        skip |= LogError(device, "VUID-VkDescriptorAddressInfoEXT-None-08044",
                         "VkDescriptorAddressInfoEXT: address not 0 or a valid buffer address.");
    } else {
        using BUFFER_STATE_PTR = ValidationStateTracker::BUFFER_STATE_PTR;
        BufferAddressValidation<1> buffer_address_validator = {
            {{{"VUID-VkDescriptorAddressInfoEXT-range-08045", LogObjectList(device),
               [&address_info](const BUFFER_STATE_PTR &buffer_state, std::string *out_error_msg) {
                   if (address_info->range >
                       buffer_state->createInfo.size - (address_info->address - buffer_state->deviceAddress)) {
                       if (out_error_msg) {
                           *out_error_msg += "range goes past buffer end";
                       }
                       return false;
                   }
                   return true;
               }}}}};

        skip |= buffer_address_validator.LogErrorsIfNoValidBuffer(*this, buffer_states, "vkCmdBindDescriptorBuffersEXT", "address",
                                                                  address_info->address);
    }

    if (address_info->range == VK_WHOLE_SIZE) {
        skip |= LogError(device, "VUID-VkDescriptorAddressInfoEXT-range-08046",
                         "VkDescriptorAddressInfoEXT: range must not be VK_WHOLE_SIZE.");
    }

    return skip;
}

bool CoreChecks::PreCallValidateGetDescriptorEXT(VkDevice device, const VkDescriptorGetInfoEXT *pDescriptorInfo, size_t dataSize,
                                                 void *pDescriptor) const {
    bool skip = false;

    if (!enabled_features.descriptor_buffer_features.descriptorBuffer) {
        skip |= LogError(device, "VUID-vkGetDescriptorEXT-None-08015",
                         "vkGetDescriptorEXT(): The descriptorBuffer feature must be enabled.");
    }

    switch (pDescriptorInfo->type) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
        case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK:
            skip |= LogError(device, "VUID-VkDescriptorGetInfoEXT-type-08018",
                             "vkGetDescriptorEXT(): type must not be VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC or "
                             "VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC or VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK.");
            break;
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            if (Get<SAMPLER_STATE>(pDescriptorInfo->data.pCombinedImageSampler->sampler).get() == nullptr) {
                skip |= LogError(device, "VUID-VkDescriptorGetInfoEXT-type-08019",
                                 "vkGetDescriptorEXT(): type is VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, but "
                                 "pCombinedImageSampler->sampler is not a valid sampler.");
            }
            if ((pDescriptorInfo->data.pCombinedImageSampler->imageView != VK_NULL_HANDLE) &&
                (Get<IMAGE_VIEW_STATE>(pDescriptorInfo->data.pCombinedImageSampler->imageView).get() == nullptr)) {
                skip |= LogError(device, "VUID-VkDescriptorGetInfoEXT-type-08020",
                                 "vkGetDescriptorEXT(): type is VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, but "
                                 "pCombinedImageSampler->imageView is not VK_NULL_HANDLE or a valid image view.");
            }
            break;
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            if (Get<IMAGE_VIEW_STATE>(pDescriptorInfo->data.pInputAttachmentImage->imageView).get() == nullptr) {
                skip |= LogError(device, "VUID-VkDescriptorGetInfoEXT-type-08021",
                                 "vkGetDescriptorEXT(): type is VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, but "
                                 "pInputAttachmentImage->imageView is not valid image view.");
            }
            break;
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            if (pDescriptorInfo->data.pSampledImage && (pDescriptorInfo->data.pSampledImage->imageView != VK_NULL_HANDLE) &&
                (Get<IMAGE_VIEW_STATE>(pDescriptorInfo->data.pSampledImage->imageView).get() == nullptr)) {
                skip |= LogError(device, "VUID-VkDescriptorGetInfoEXT-type-08022",
                                 "vkGetDescriptorEXT(): type is VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, but "
                                 "pSampledImage->imageView is not VK_NULL_HANDLE or a valid image view.");
            }
            break;
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            if (pDescriptorInfo->data.pStorageImage && (pDescriptorInfo->data.pStorageImage->imageView != VK_NULL_HANDLE) &&
                (Get<IMAGE_VIEW_STATE>(pDescriptorInfo->data.pStorageImage->imageView).get() == nullptr)) {
                skip |= LogError(device, "VUID-VkDescriptorGetInfoEXT-type-08023",
                                 "vkGetDescriptorEXT(): type is VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, but "
                                 "pStorageImage->imageView is not VK_NULL_HANDLE or a valid image view.");
            }
            break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            if (pDescriptorInfo->data.pUniformTexelBuffer && (pDescriptorInfo->data.pUniformTexelBuffer->address != 0) &&
                (GetBuffersByAddress(pDescriptorInfo->data.pUniformTexelBuffer->address).empty())) {
                skip |= LogError(device, "VUID-VkDescriptorGetInfoEXT-type-08024",
                                 "vkGetDescriptorEXT(): type is VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, but "
                                 "pUniformTexelBuffer is not NULL and pUniformTexelBuffer->address is not zero or "
                                 "an address within a buffer");
            }
            break;
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            if (pDescriptorInfo->data.pStorageTexelBuffer && (pDescriptorInfo->data.pStorageTexelBuffer->address != 0) &&
                (GetBuffersByAddress(pDescriptorInfo->data.pStorageTexelBuffer->address).empty())) {
                skip |= LogError(device, "VUID-VkDescriptorGetInfoEXT-type-08025",
                                 "vkGetDescriptorEXT(): type is VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, but "
                                 "pStorageTexelBuffer is not NULL and pStorageTexelBuffer->address is not zero or "
                                 "an address within a buffer");
            }
            break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            if (pDescriptorInfo->data.pUniformBuffer && (pDescriptorInfo->data.pUniformBuffer->address != 0) &&
                (GetBuffersByAddress(pDescriptorInfo->data.pStorageTexelBuffer->address).empty())) {
                skip |= LogError(device, "VUID-VkDescriptorGetInfoEXT-type-08026",
                                 "vkGetDescriptorEXT(): type is VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, but "
                                 "pUniformBuffer is not NULL and pUniformBuffer->address is not zero or "
                                 "an address within a buffer");
            }
            break;
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            if (pDescriptorInfo->data.pStorageBuffer && (pDescriptorInfo->data.pStorageBuffer->address != 0) &&
                (GetBuffersByAddress(pDescriptorInfo->data.pStorageBuffer->address).empty())) {
                skip |= LogError(device, "VUID-VkDescriptorGetInfoEXT-type-08027",
                                 "vkGetDescriptorEXT(): type is VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, but "
                                 "pStorageBuffer is not NULL and pStorageBuffer->address is not zero or "
                                 "an address within a buffer");
            }
            break;

        case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV:
            if (pDescriptorInfo->data.accelerationStructure) {
                const VkAccelerationStructureNV as = (VkAccelerationStructureNV)pDescriptorInfo->data.accelerationStructure;
                auto as_state = Get<ACCELERATION_STRUCTURE_STATE>(as);

                if (!as_state) {
                    skip |= LogError(device, "VUID-VkDescriptorGetInfoEXT-type-08029",
                                     "If type is VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV and accelerationStructure is not 0, "
                                     "accelerationStructure must contain the handle of a VkAccelerationStructureNV created on "
                                     "device, returned by vkGetAccelerationStructureHandleNV");
                }
            }
            break;

        default:
            break;
    }

    std::string_view vuid_memory_bound = "";
    using BUFFER_STATE_PTR = ValidationStateTracker::BUFFER_STATE_PTR;
    BufferAddressValidation<1> buffer_address_validator = {
        {{{"VUID-VkDescriptorDataEXT-type", LogObjectList(device),
           [this, device, &vuid_memory_bound](const BUFFER_STATE_PTR &buffer_state, std::string *out_error_msg) {
               if (!out_error_msg) {
                   return !buffer_state->sparse && buffer_state->IsMemoryBound();
               } else {
                   return ValidateMemoryIsBoundToBuffer(device, *buffer_state, "vkGetDescriptorEXT()", vuid_memory_bound.data());
               }
           }}}}};

    switch (pDescriptorInfo->type) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            if (pDescriptorInfo->data.pUniformBuffer) {
                const auto buffer_states = GetBuffersByAddress(pDescriptorInfo->data.pUniformBuffer->address);
                if (!buffer_states.empty()) {
                    vuid_memory_bound = "VUID-VkDescriptorDataEXT-type-08030";
                    skip |= buffer_address_validator.LogErrorsIfNoValidBuffer(*this, buffer_states, "vkGetDescriptorEXT()",
                                                                              "pDescriptorInfo->data.pUniformBuffer->address",
                                                                              pDescriptorInfo->data.pUniformBuffer->address);
                }
            } else if (!enabled_features.robustness2_features.nullDescriptor) {
                skip |= LogError(device, "VUID-VkDescriptorDataEXT-type-08039",
                                 "vkGetDescriptorEXT(): type is VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, but "
                                 "pUniformBuffer is NULL and the nullDescriptor feature is not enabled.");
            }
            break;
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            if (pDescriptorInfo->data.pStorageBuffer) {
                const auto buffer_states = GetBuffersByAddress(pDescriptorInfo->data.pUniformBuffer->address);
                if (!buffer_states.empty()) {
                    vuid_memory_bound = "VUID-VkDescriptorDataEXT-type-08031";
                    skip |= buffer_address_validator.LogErrorsIfNoValidBuffer(*this, buffer_states, "vkGetDescriptorEXT()",
                                                                              "pDescriptorInfo->data.pUniformBuffer->address",
                                                                              pDescriptorInfo->data.pUniformBuffer->address);
                }
            } else if (!enabled_features.robustness2_features.nullDescriptor) {
                skip |= LogError(device, "VUID-VkDescriptorDataEXT-type-08040",
                                 "vkGetDescriptorEXT(): type is VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, but "
                                 "pStorageBuffer is NULL and the nullDescriptor feature is not enabled.");
            }
            break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            if (pDescriptorInfo->data.pUniformTexelBuffer) {
                const auto buffer_states = GetBuffersByAddress(pDescriptorInfo->data.pUniformBuffer->address);
                if (!buffer_states.empty()) {
                    vuid_memory_bound = "VUID-VkDescriptorDataEXT-type-08032";
                    skip |= buffer_address_validator.LogErrorsIfNoValidBuffer(*this, buffer_states, "vkGetDescriptorEXT()",
                                                                              "pDescriptorInfo->data.pUniformBuffer->address",
                                                                              pDescriptorInfo->data.pUniformBuffer->address);
                }
            } else if (!enabled_features.robustness2_features.nullDescriptor) {
                skip |= LogError(device, "VUID-VkDescriptorDataEXT-type-08037",
                                 "vkGetDescriptorEXT(): type is VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, but "
                                 "pUniformTexelBuffer is NULL and the nullDescriptor feature is not enabled.");
            }
            break;
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            if (pDescriptorInfo->data.pStorageTexelBuffer) {
                const auto buffer_states = GetBuffersByAddress(pDescriptorInfo->data.pUniformBuffer->address);
                if (!buffer_states.empty()) {
                    vuid_memory_bound = "VUID-VkDescriptorDataEXT-type-08033";
                    skip |= buffer_address_validator.LogErrorsIfNoValidBuffer(*this, buffer_states, "vkGetDescriptorEXT()",
                                                                              "pDescriptorInfo->data.pUniformBuffer->address",
                                                                              pDescriptorInfo->data.pUniformBuffer->address);
                }
            } else if (!enabled_features.robustness2_features.nullDescriptor) {
                skip |= LogError(device, "VUID-VkDescriptorDataEXT-type-08038",
                                 "vkGetDescriptorEXT(): type is VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, but "
                                 "pStorageTexelBuffer is NULL and the nullDescriptor feature is not enabled.");
            }
            break;
        case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
            if ((pDescriptorInfo->data.accelerationStructure == 0) && !enabled_features.robustness2_features.nullDescriptor) {
                skip |= LogError(device, "VUID-VkDescriptorDataEXT-type-08041",
                                 "vkGetDescriptorEXT(): type is VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, but "
                                 "accelerationStructure is 0 and the nullDescriptor feature is not enabled.");
            }
            break;
        case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV:
            if ((pDescriptorInfo->data.accelerationStructure == 0) && !enabled_features.robustness2_features.nullDescriptor) {
                skip |= LogError(device, "VUID-VkDescriptorDataEXT-type-08042",
                                 "vkGetDescriptorEXT(): type is VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, but "
                                 "accelerationStructure is 0 and the nullDescriptor feature is not enabled.");
            }
            break;
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            if ((pDescriptorInfo->data.pCombinedImageSampler->imageView == VK_NULL_HANDLE) &&
                !enabled_features.robustness2_features.nullDescriptor) {
                skip |=
                    LogError(device, "VUID-VkDescriptorDataEXT-type-08034",
                             "vkGetDescriptorEXT(): type is VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, but "
                             "pCombinedImageSampler->imageView is VK_NULL_HANDLE and the nullDescriptor feature is not enabled.");
            }
            break;
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            if (!enabled_features.robustness2_features.nullDescriptor &&
                (!pDescriptorInfo->data.pSampledImage || (pDescriptorInfo->data.pSampledImage->imageView == VK_NULL_HANDLE))) {
                skip |= LogError(device, "VUID-VkDescriptorDataEXT-type-08035",
                                 "vkGetDescriptorEXT(): type is VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, but "
                                 "pSampledImage is NULL, or pSampledImage->imageView is VK_NULL_HANDLE, and the nullDescriptor "
                                 "feature is not enabled.");
            }
            break;
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            if (!enabled_features.robustness2_features.nullDescriptor &&
                (!pDescriptorInfo->data.pStorageImage || (pDescriptorInfo->data.pStorageImage->imageView == VK_NULL_HANDLE))) {
                skip |= LogError(device, "VUID-VkDescriptorDataEXT-type-08036",
                                 "vkGetDescriptorEXT(): type is VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, but "
                                 "pStorageImage is NULL, or pStorageImage->imageView is VK_NULL_HANDLE, and the nullDescriptor "
                                 "feature is not enabled.");
            }
            break;
        default:
            break;
    }

    switch (pDescriptorInfo->type) {
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            if (pDescriptorInfo->data.pUniformTexelBuffer) {
                skip |= ValidateDescriptorAddressInfoEXT(device, pDescriptorInfo->data.pUniformTexelBuffer);
            }
            break;
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            if (pDescriptorInfo->data.pStorageTexelBuffer) {
                skip |= ValidateDescriptorAddressInfoEXT(device, pDescriptorInfo->data.pStorageTexelBuffer);
            }
            break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            if (pDescriptorInfo->data.pUniformBuffer) {
                skip |= ValidateDescriptorAddressInfoEXT(device, pDescriptorInfo->data.pUniformBuffer);
            }
            break;
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            if (pDescriptorInfo->data.pStorageBuffer) {
                skip |= ValidateDescriptorAddressInfoEXT(device, pDescriptorInfo->data.pStorageBuffer);
            }
            break;
        default:
            break;
    }

    bool checkDataSize = false;
    std::size_t size = 0u;

    switch (pDescriptorInfo->type) {
        case VK_DESCRIPTOR_TYPE_SAMPLER:
            checkDataSize = true;
            size = phys_dev_ext_props.descriptor_buffer_props.samplerDescriptorSize;
            break;

        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            checkDataSize = true;
            size = phys_dev_ext_props.descriptor_buffer_props.combinedImageSamplerDescriptorSize;
            break;

        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            checkDataSize = true;
            size = phys_dev_ext_props.descriptor_buffer_props.sampledImageDescriptorSize;
            break;

        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            checkDataSize = true;
            size = phys_dev_ext_props.descriptor_buffer_props.storageImageDescriptorSize;
            break;

        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            checkDataSize = true;
            size = enabled_features.core.robustBufferAccess
                       ? phys_dev_ext_props.descriptor_buffer_props.robustUniformTexelBufferDescriptorSize
                       : phys_dev_ext_props.descriptor_buffer_props.uniformTexelBufferDescriptorSize;
            break;

        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            checkDataSize = true;
            size = enabled_features.core.robustBufferAccess
                       ? phys_dev_ext_props.descriptor_buffer_props.robustStorageTexelBufferDescriptorSize
                       : phys_dev_ext_props.descriptor_buffer_props.storageTexelBufferDescriptorSize;
            break;

        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            checkDataSize = true;
            size = enabled_features.core.robustBufferAccess
                       ? phys_dev_ext_props.descriptor_buffer_props.robustUniformBufferDescriptorSize
                       : phys_dev_ext_props.descriptor_buffer_props.uniformBufferDescriptorSize;
            break;

        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            checkDataSize = true;
            size = enabled_features.core.robustBufferAccess
                       ? phys_dev_ext_props.descriptor_buffer_props.robustStorageBufferDescriptorSize
                       : phys_dev_ext_props.descriptor_buffer_props.storageBufferDescriptorSize;
            break;

        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            checkDataSize = true;
            size = phys_dev_ext_props.descriptor_buffer_props.inputAttachmentDescriptorSize;
            break;

        case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
            checkDataSize = true;
            size = phys_dev_ext_props.descriptor_buffer_props.accelerationStructureDescriptorSize;
            break;
        default:
            break;
    }

    if (pDescriptorInfo->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && pDescriptorInfo->data.pSampler != nullptr) {
        const auto sampler_state = Get<SAMPLER_STATE>(*pDescriptorInfo->data.pSampler);

        if (sampler_state && (0 != (sampler_state->createInfo.flags & VK_SAMPLER_CREATE_SUBSAMPLED_BIT_EXT))) {
            dataSize = phys_dev_ext_props.descriptor_buffer_density_props.combinedImageSamplerDensityMapDescriptorSize;
            checkDataSize = true;
        }
    }

    if (checkDataSize && size != dataSize) {
        skip |= LogError(device, "VUID-vkGetDescriptorEXT-dataSize-08125",
                         "vkGetDescriptorEXT(): dataSize (%zu) must equal the size of a descriptor (%zu) of type "
                         "VkDescriptorGetInfoEXT::type "
                         "determined by the value in VkPhysicalDeviceDescriptorBufferPropertiesEXT, or "
                         "VkPhysicalDeviceDescriptorBufferDensityMapPropertiesEXT::combinedImageSamplerDensityMapDescriptorSize if "
                         "pDescriptorInfo specifies a VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER whose VkSampler was created with "
                         "VK_SAMPLER_CREATE_SUBSAMPLED_BIT_EXT set",
                         dataSize, size);
    }

    return skip;
}

bool CoreChecks::PreCallValidateResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool,
                                                    VkDescriptorPoolResetFlags flags) const {
    // Make sure sets being destroyed are not currently in-use
    if (disabled[object_in_use]) return false;
    bool skip = false;
    auto pool = Get<DESCRIPTOR_POOL_STATE>(descriptorPool);
    if (pool && pool->InUse()) {
        skip |= LogError(descriptorPool, "VUID-vkResetDescriptorPool-descriptorPool-00313",
                         "It is invalid to call vkResetDescriptorPool() with descriptor sets in use by a command buffer.");
    }
    return skip;
}

bool CoreChecks::PreCallValidateDestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool,
                                                      const VkAllocationCallbacks *pAllocator) const {
    auto desc_pool_state = Get<DESCRIPTOR_POOL_STATE>(descriptorPool);
    bool skip = false;
    if (desc_pool_state) {
        skip |= ValidateObjectNotInUse(desc_pool_state.get(), "vkDestroyDescriptorPool",
                                       "VUID-vkDestroyDescriptorPool-descriptorPool-00303");
    }
    return skip;
}

// Ensure the pool contains enough descriptors and descriptor sets to satisfy
// an allocation request. Fills common_data with the total number of descriptors of each type required,
// as well as DescriptorSetLayout ptrs used for later update.
bool CoreChecks::PreCallValidateAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo *pAllocateInfo,
                                                       VkDescriptorSet *pDescriptorSets, void *ads_state_data) const {
    StateTracker::PreCallValidateAllocateDescriptorSets(device, pAllocateInfo, pDescriptorSets, ads_state_data);

    cvdescriptorset::AllocateDescriptorSetsData *ads_state =
        reinterpret_cast<cvdescriptorset::AllocateDescriptorSetsData *>(ads_state_data);
    // All state checks for AllocateDescriptorSets is done in single function
    return ValidateAllocateDescriptorSets(pAllocateInfo, ads_state);
}

// Validate that given set is valid and that it's not being used by an in-flight CmdBuffer
// func_str is the name of the calling function
// Return false if no errors occur
// Return true if validation error occurs and callback returns true (to skip upcoming API call down the chain)
bool CoreChecks::ValidateIdleDescriptorSet(VkDescriptorSet set, const char *func_str) const {
    if (disabled[object_in_use]) return false;
    bool skip = false;
    auto set_node = Get<cvdescriptorset::DescriptorSet>(set);
    if (set_node) {
        // TODO : This covers various error cases so should pass error enum into this function and use passed in enum here
        if (set_node->InUse()) {
            skip |= LogError(set, "VUID-vkFreeDescriptorSets-pDescriptorSets-00309",
                             "Cannot call %s() on %s that is in use by a command buffer.", func_str,
                             report_data->FormatHandle(set).c_str());
        }
    }
    return skip;
}

bool CoreChecks::PreCallValidateFreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t count,
                                                   const VkDescriptorSet *pDescriptorSets) const {
    // Make sure that no sets being destroyed are in-flight
    bool skip = false;
    // First make sure sets being destroyed are not currently in-use
    for (uint32_t i = 0; i < count; ++i) {
        if (pDescriptorSets[i] != VK_NULL_HANDLE) {
            skip |= ValidateIdleDescriptorSet(pDescriptorSets[i], "vkFreeDescriptorSets");
        }
    }
    auto pool_state = Get<DESCRIPTOR_POOL_STATE>(descriptorPool);
    if (pool_state && !(VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT & pool_state->createInfo.flags)) {
        // Can't Free from a NON_FREE pool
        skip |= LogError(descriptorPool, "VUID-vkFreeDescriptorSets-descriptorPool-00312",
                         "It is invalid to call vkFreeDescriptorSets() with a pool created without setting "
                         "VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT.");
    }
    return skip;
}

bool CoreChecks::PreCallValidateUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount,
                                                     const VkWriteDescriptorSet *pDescriptorWrites, uint32_t descriptorCopyCount,
                                                     const VkCopyDescriptorSet *pDescriptorCopies) const {
    // First thing to do is perform map look-ups.
    // NOTE : UpdateDescriptorSets is somewhat unique in that it's operating on a number of DescriptorSets
    //  so we can't just do a single map look-up up-front, but do them individually in functions below

    // Now make call(s) that validate state, but don't perform state updates in this function
    // Note, here DescriptorSets is unique in that we don't yet have an instance. Using a helper function in the
    //  namespace which will parse params and make calls into specific class instances
    return ValidateUpdateDescriptorSets(descriptorWriteCount, pDescriptorWrites, descriptorCopyCount, pDescriptorCopies,
                                        "vkUpdateDescriptorSets()");
}

bool CoreChecks::PreCallValidateCmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
                                                        VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount,
                                                        const VkWriteDescriptorSet *pDescriptorWrites) const {
    auto cb_state = GetRead<CMD_BUFFER_STATE>(commandBuffer);
    assert(cb_state);
    bool skip = false;
    skip |= ValidateCmd(*cb_state, CMD_PUSHDESCRIPTORSETKHR);

    static const std::map<VkPipelineBindPoint, std::string> bind_errors = {
        std::make_pair(VK_PIPELINE_BIND_POINT_GRAPHICS, "VUID-vkCmdPushDescriptorSetKHR-pipelineBindPoint-00363"),
        std::make_pair(VK_PIPELINE_BIND_POINT_COMPUTE, "VUID-vkCmdPushDescriptorSetKHR-pipelineBindPoint-00363"),
        std::make_pair(VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, "VUID-vkCmdPushDescriptorSetKHR-pipelineBindPoint-00363")};

    skip |= ValidatePipelineBindPoint(cb_state.get(), pipelineBindPoint, "vkCmdPushDescriptorSetKHR()", bind_errors);
    auto layout_data = Get<PIPELINE_LAYOUT_STATE>(layout);

    // Validate the set index points to a push descriptor set and is in range
    if (layout_data) {
        const auto &set_layouts = layout_data->set_layouts;
        if (set < set_layouts.size()) {
            const auto &dsl = set_layouts[set];
            if (dsl) {
                if (!dsl->IsPushDescriptor()) {
                    skip = LogError(layout, "VUID-vkCmdPushDescriptorSetKHR-set-00365",
                                    "vkCmdPushDescriptorSetKHR(): Set index %" PRIu32
                                    " does not match push descriptor set layout index for %s.",
                                    set, report_data->FormatHandle(layout).c_str());
                } else {
                    // Create an empty proxy in order to use the existing descriptor set update validation
                    // TODO move the validation (like this) that doesn't need descriptor set state to the DSL object so we
                    // don't have to do this. Note we need to const_cast<>(this) because GPU-AV needs a non-const version of
                    // the state tracker. The proxy here could get away with const.
                    cvdescriptorset::DescriptorSet proxy_ds(VK_NULL_HANDLE, nullptr, dsl, 0, const_cast<CoreChecks *>(this));
                    skip |= ValidatePushDescriptorsUpdate(&proxy_ds, descriptorWriteCount, pDescriptorWrites,
                                                          "vkCmdPushDescriptorSetKHR()");
                }
            }
        } else {
            skip = LogError(layout, "VUID-vkCmdPushDescriptorSetKHR-set-00364",
                            "vkCmdPushDescriptorSetKHR(): Set index %" PRIu32 " is outside of range for %s (set < %" PRIu32 ").",
                            set, report_data->FormatHandle(layout).c_str(), static_cast<uint32_t>(set_layouts.size()));
        }
    }

    return skip;
}

bool CoreChecks::ValidateDescriptorUpdateTemplate(const char *func_name,
                                                  const VkDescriptorUpdateTemplateCreateInfo *pCreateInfo) const {
    bool skip = false;
    auto layout = Get<cvdescriptorset::DescriptorSetLayout>(pCreateInfo->descriptorSetLayout);
    if (VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET == pCreateInfo->templateType && !layout) {
        skip |= LogError(pCreateInfo->descriptorSetLayout, "VUID-VkDescriptorUpdateTemplateCreateInfo-templateType-00350",
                         "%s: Invalid pCreateInfo->descriptorSetLayout (%s)", func_name,
                         report_data->FormatHandle(pCreateInfo->descriptorSetLayout).c_str());
    } else if (VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR == pCreateInfo->templateType) {
        auto bind_point = pCreateInfo->pipelineBindPoint;
        const bool valid_bp = (bind_point == VK_PIPELINE_BIND_POINT_GRAPHICS) || (bind_point == VK_PIPELINE_BIND_POINT_COMPUTE) ||
                              (bind_point == VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR);
        if (!valid_bp) {
            skip |=
                LogError(device, "VUID-VkDescriptorUpdateTemplateCreateInfo-templateType-00351",
                         "%s: Invalid pCreateInfo->pipelineBindPoint (%" PRIu32 ").", func_name, static_cast<uint32_t>(bind_point));
        }
        auto pipeline_layout = Get<PIPELINE_LAYOUT_STATE>(pCreateInfo->pipelineLayout);
        if (!pipeline_layout) {
            skip |= LogError(pCreateInfo->pipelineLayout, "VUID-VkDescriptorUpdateTemplateCreateInfo-templateType-00352",
                             "%s: Invalid pCreateInfo->pipelineLayout (%s)", func_name,
                             report_data->FormatHandle(pCreateInfo->pipelineLayout).c_str());
        } else {
            const uint32_t pd_set = pCreateInfo->set;
            if ((pd_set >= pipeline_layout->set_layouts.size()) || !pipeline_layout->set_layouts[pd_set] ||
                !pipeline_layout->set_layouts[pd_set]->IsPushDescriptor()) {
                skip |= LogError(pCreateInfo->pipelineLayout, "VUID-VkDescriptorUpdateTemplateCreateInfo-templateType-00353",
                                 "%s: pCreateInfo->set (%" PRIu32
                                 ") does not refer to the push descriptor set layout for pCreateInfo->pipelineLayout (%s).",
                                 func_name, pd_set, report_data->FormatHandle(pCreateInfo->pipelineLayout).c_str());
            }
        }
    } else if (VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET == pCreateInfo->templateType) {
        for (const auto &binding : layout->GetBindings()) {
            if (binding.descriptorType == VK_DESCRIPTOR_TYPE_MUTABLE_EXT) {
                skip |= LogError(
                    device, "VUID-VkDescriptorUpdateTemplateCreateInfo-templateType-04615",
                    "%s: pCreateInfo->templateType is VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET, but "
                    "pCreateInfo->descriptorSetLayout contains a binding with descriptor type VK_DESCRIPTOR_TYPE_MUTABLE_EXT.",
                    func_name);
            }
        }
    }
    for (uint32_t i = 0; i < pCreateInfo->descriptorUpdateEntryCount; ++i) {
        const auto &descriptor_update = pCreateInfo->pDescriptorUpdateEntries[i];
        if (descriptor_update.descriptorType == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT) {
            if (descriptor_update.dstArrayElement & 3) {
                skip |= LogError(pCreateInfo->pipelineLayout, "VUID-VkDescriptorUpdateTemplateEntry-descriptor-02226",
                                 "%s: pCreateInfo->pDescriptorUpdateEntries[%" PRIu32
                                 "] has descriptorType VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT, but dstArrayElement (%" PRIu32
                                 ") is not a "
                                 "multiple of 4).",
                                 func_name, i, descriptor_update.dstArrayElement);
            }
            if (descriptor_update.descriptorCount & 3) {
                skip |= LogError(pCreateInfo->pipelineLayout, "VUID-VkDescriptorUpdateTemplateEntry-descriptor-02227",
                                 "%s: pCreateInfo->pDescriptorUpdateEntries[%" PRIu32
                                 "] has descriptorType VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT, but descriptorCount (%" PRIu32
                                 ")is not a "
                                 "multiple of 4).",
                                 func_name, i, descriptor_update.descriptorCount);
            }
        }
    }
    return skip;
}

bool CoreChecks::PreCallValidateCreateDescriptorUpdateTemplate(VkDevice device,
                                                               const VkDescriptorUpdateTemplateCreateInfo *pCreateInfo,
                                                               const VkAllocationCallbacks *pAllocator,
                                                               VkDescriptorUpdateTemplate *pDescriptorUpdateTemplate) const {
    bool skip = ValidateDescriptorUpdateTemplate("vkCreateDescriptorUpdateTemplate()", pCreateInfo);
    return skip;
}

bool CoreChecks::PreCallValidateCreateDescriptorUpdateTemplateKHR(VkDevice device,
                                                                  const VkDescriptorUpdateTemplateCreateInfo *pCreateInfo,
                                                                  const VkAllocationCallbacks *pAllocator,
                                                                  VkDescriptorUpdateTemplate *pDescriptorUpdateTemplate) const {
    bool skip = ValidateDescriptorUpdateTemplate("vkCreateDescriptorUpdateTemplateKHR()", pCreateInfo);
    return skip;
}

bool CoreChecks::ValidateUpdateDescriptorSetWithTemplate(VkDescriptorSet descriptorSet,
                                                         VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                                         const void *pData) const {
    bool skip = false;
    auto template_state = Get<UPDATE_TEMPLATE_STATE>(descriptorUpdateTemplate);
    // Object tracker will report errors for invalid descriptorUpdateTemplate values, avoiding a crash in release builds
    // but retaining the assert as template support is new enough to want to investigate these in debug builds.
    assert(template_state);
    // TODO: Validate template push descriptor updates
    if (template_state->create_info.templateType == VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET) {
        skip = ValidateUpdateDescriptorSetsWithTemplateKHR(descriptorSet, template_state.get(), pData);
    }
    return skip;
}

bool CoreChecks::PreCallValidateUpdateDescriptorSetWithTemplate(VkDevice device, VkDescriptorSet descriptorSet,
                                                                VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                                                const void *pData) const {
    return ValidateUpdateDescriptorSetWithTemplate(descriptorSet, descriptorUpdateTemplate, pData);
}

bool CoreChecks::PreCallValidateUpdateDescriptorSetWithTemplateKHR(VkDevice device, VkDescriptorSet descriptorSet,
                                                                   VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                                                   const void *pData) const {
    return ValidateUpdateDescriptorSetWithTemplate(descriptorSet, descriptorUpdateTemplate, pData);
}

bool CoreChecks::PreCallValidateCmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer,
                                                                    VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                                                    VkPipelineLayout layout, uint32_t set,
                                                                    const void *pData) const {
    auto cb_state = GetRead<CMD_BUFFER_STATE>(commandBuffer);
    assert(cb_state);
    bool skip = false;
    skip |= ValidateCmd(*cb_state, CMD_PUSHDESCRIPTORSETWITHTEMPLATEKHR);

    auto layout_data = Get<PIPELINE_LAYOUT_STATE>(layout);
    const auto dsl = layout_data ? layout_data->GetDsl(set) : nullptr;
    // Validate the set index points to a push descriptor set and is in range
    if (dsl) {
        if (!dsl->IsPushDescriptor()) {
            skip = LogError(layout, "VUID-vkCmdPushDescriptorSetWithTemplateKHR-set-07305",
                            "vkCmdPushDescriptorSetWithTemplateKHR(): Set index %" PRIu32
                            " does not match push descriptor set layout index for %s.",
                            set, report_data->FormatHandle(layout).c_str());
        }
    } else if (layout_data && (set >= layout_data->set_layouts.size())) {
        skip = LogError(layout, "VUID-vkCmdPushDescriptorSetWithTemplateKHR-set-07304",
                        "vkCmdPushDescriptorSetWithTemplateKHR(): Set index %" PRIu32 " is outside of range for %s (set < %" PRIu32
                        ").",
                        set, report_data->FormatHandle(layout).c_str(), static_cast<uint32_t>(layout_data->set_layouts.size()));
    }

    auto template_state = Get<UPDATE_TEMPLATE_STATE>(descriptorUpdateTemplate);
    if (template_state) {
        const auto &template_ci = template_state->create_info;
        static const std::map<VkPipelineBindPoint, std::string> bind_errors = {
            std::make_pair(VK_PIPELINE_BIND_POINT_GRAPHICS, "VUID-vkCmdPushDescriptorSetWithTemplateKHR-commandBuffer-00366"),
            std::make_pair(VK_PIPELINE_BIND_POINT_COMPUTE, "VUID-vkCmdPushDescriptorSetWithTemplateKHR-commandBuffer-00366"),
            std::make_pair(VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                           "VUID-vkCmdPushDescriptorSetWithTemplateKHR-commandBuffer-00366")};
        skip |= ValidatePipelineBindPoint(cb_state.get(), template_ci.pipelineBindPoint, "vkCmdPushDescriptorSetWithTemplateKHR()",
                                          bind_errors);

        if (template_ci.templateType != VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR) {
            skip |= LogError(cb_state->commandBuffer(), "VUID-vkCmdPushDescriptorSetWithTemplateKHR-descriptorUpdateTemplate-07994",
                             "vkCmdPushDescriptorSetWithTemplateKHR(): descriptorUpdateTemplate %s was not created with flag "
                             "VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR.",
                             report_data->FormatHandle(descriptorUpdateTemplate).c_str());
        }
        if (template_ci.set != set) {
            skip |= LogError(cb_state->commandBuffer(), "VUID-vkCmdPushDescriptorSetWithTemplateKHR-set-07995",
                             "vkCmdPushDescriptorSetWithTemplateKHR(): descriptorUpdateTemplate %s created with set %" PRIu32
                             " does not match command parameter set %" PRIu32 ".",
                             report_data->FormatHandle(descriptorUpdateTemplate).c_str(), template_ci.set, set);
        }
        auto template_layout = Get<PIPELINE_LAYOUT_STATE>(template_ci.pipelineLayout);
        if (!IsPipelineLayoutSetCompat(set, layout_data.get(), template_layout.get())) {
            const LogObjectList objlist(cb_state->commandBuffer(), descriptorUpdateTemplate, template_ci.pipelineLayout, layout);
            skip |= LogError(objlist, "VUID-vkCmdPushDescriptorSetWithTemplateKHR-layout-07993",
                             "vkCmdPushDescriptorSetWithTemplateKHR(): descriptorUpdateTemplate %s created with %s is incompatible "
                             "with command parameter "
                             "%s for set %" PRIu32,
                             report_data->FormatHandle(descriptorUpdateTemplate).c_str(),
                             report_data->FormatHandle(template_ci.pipelineLayout).c_str(),
                             report_data->FormatHandle(layout).c_str(), set);
        }
    }

    if (dsl && template_state) {
        if (!Get<cvdescriptorset::DescriptorSetLayout>(dsl->GetDescriptorSetLayout())) {
            const LogObjectList objlist(cb_state->commandBuffer(), descriptorUpdateTemplate, layout);
            skip |= LogError(objlist, "VUID-vkCmdPushDescriptorSetWithTemplateKHR-pData-01686",
                             "vkCmdPushDescriptorSetWithTemplateKHR(): pData does not point to a valid layout, it possible the "
                             "VkDescriptorUpdateTemplateCreateInfo::descriptorSetLayout was accidentally destroy.");
        } else {
            // Create an empty proxy in order to use the existing descriptor set update validation
            cvdescriptorset::DescriptorSet proxy_ds(VK_NULL_HANDLE, nullptr, dsl, 0, const_cast<CoreChecks *>(this));
            // Decode the template into a set of write updates
            cvdescriptorset::DecodedTemplateUpdate decoded_template(this, VK_NULL_HANDLE, template_state.get(), pData,
                                                                    dsl->GetDescriptorSetLayout());
            // Validate the decoded update against the proxy_ds
            skip |= ValidatePushDescriptorsUpdate(&proxy_ds, static_cast<uint32_t>(decoded_template.desc_writes.size()),
                                                  decoded_template.desc_writes.data(), "vkCmdPushDescriptorSetWithTemplateKHR()");
        }
    }

    return skip;
}

enum DSL_DESCRIPTOR_GROUPS {
    DSL_TYPE_SAMPLERS = 0,
    DSL_TYPE_UNIFORM_BUFFERS,
    DSL_TYPE_STORAGE_BUFFERS,
    DSL_TYPE_SAMPLED_IMAGES,
    DSL_TYPE_STORAGE_IMAGES,
    DSL_TYPE_INPUT_ATTACHMENTS,
    DSL_TYPE_INLINE_UNIFORM_BLOCK,
    DSL_TYPE_ACCELERATION_STRUCTURE,
    DSL_TYPE_ACCELERATION_STRUCTURE_NV,
    DSL_NUM_DESCRIPTOR_GROUPS
};

// Used by PreCallValidateCreatePipelineLayout.
// Returns an array of size DSL_NUM_DESCRIPTOR_GROUPS of the maximum number of descriptors used in any single pipeline stage
std::valarray<uint32_t> GetDescriptorCountMaxPerStage(
    const DeviceFeatures *enabled_features,
    const std::vector<std::shared_ptr<cvdescriptorset::DescriptorSetLayout const>> &set_layouts, bool skip_update_after_bind) {
    // Identify active pipeline stages
    std::vector<VkShaderStageFlags> stage_flags = {VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT,
                                                   VK_SHADER_STAGE_COMPUTE_BIT};
    if (enabled_features->core.geometryShader) {
        stage_flags.push_back(VK_SHADER_STAGE_GEOMETRY_BIT);
    }
    if (enabled_features->core.tessellationShader) {
        stage_flags.push_back(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
        stage_flags.push_back(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
    }
    if (enabled_features->ray_tracing_pipeline_features.rayTracingPipeline) {
        stage_flags.push_back(VK_SHADER_STAGE_RAYGEN_BIT_KHR);
        stage_flags.push_back(VK_SHADER_STAGE_ANY_HIT_BIT_KHR);
        stage_flags.push_back(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
        stage_flags.push_back(VK_SHADER_STAGE_MISS_BIT_KHR);
        stage_flags.push_back(VK_SHADER_STAGE_INTERSECTION_BIT_KHR);
        stage_flags.push_back(VK_SHADER_STAGE_CALLABLE_BIT_KHR);
    }

    // Allow iteration over enum values
    std::vector<DSL_DESCRIPTOR_GROUPS> dsl_groups = {
        DSL_TYPE_SAMPLERS,
        DSL_TYPE_UNIFORM_BUFFERS,
        DSL_TYPE_STORAGE_BUFFERS,
        DSL_TYPE_SAMPLED_IMAGES,
        DSL_TYPE_STORAGE_IMAGES,
        DSL_TYPE_INPUT_ATTACHMENTS,
        DSL_TYPE_INLINE_UNIFORM_BLOCK,
        DSL_TYPE_ACCELERATION_STRUCTURE,
        DSL_TYPE_ACCELERATION_STRUCTURE_NV,
    };

    // Sum by layouts per stage, then pick max of stages per type
    std::valarray<uint32_t> max_sum(0U, DSL_NUM_DESCRIPTOR_GROUPS);  // max descriptor sum among all pipeline stages
    for (auto stage : stage_flags) {
        std::valarray<uint32_t> stage_sum(0U, DSL_NUM_DESCRIPTOR_GROUPS);  // per-stage sums
        for (const auto &dsl : set_layouts) {
            if (!dsl) {
                continue;
            }
            if (skip_update_after_bind && (dsl->GetCreateFlags() & VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT)) {
                continue;
            }

            for (uint32_t binding_idx = 0; binding_idx < dsl->GetBindingCount(); binding_idx++) {
                const VkDescriptorSetLayoutBinding *binding = dsl->GetDescriptorSetLayoutBindingPtrFromIndex(binding_idx);
                // Bindings with a descriptorCount of 0 are "reserved" and should be skipped
                if (0 != (stage & binding->stageFlags) && binding->descriptorCount > 0) {
                    switch (binding->descriptorType) {
                        case VK_DESCRIPTOR_TYPE_SAMPLER:
                            stage_sum[DSL_TYPE_SAMPLERS] += binding->descriptorCount;
                            break;
                        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
                            stage_sum[DSL_TYPE_UNIFORM_BUFFERS] += binding->descriptorCount;
                            break;
                        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
                            stage_sum[DSL_TYPE_STORAGE_BUFFERS] += binding->descriptorCount;
                            break;
                        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
                        case VK_DESCRIPTOR_TYPE_SAMPLE_WEIGHT_IMAGE_QCOM:
                        case VK_DESCRIPTOR_TYPE_BLOCK_MATCH_IMAGE_QCOM:
                            stage_sum[DSL_TYPE_SAMPLED_IMAGES] += binding->descriptorCount;
                            break;
                        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
                            stage_sum[DSL_TYPE_STORAGE_IMAGES] += binding->descriptorCount;
                            break;
                        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                            stage_sum[DSL_TYPE_SAMPLED_IMAGES] += binding->descriptorCount;
                            stage_sum[DSL_TYPE_SAMPLERS] += binding->descriptorCount;
                            break;
                        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
                            stage_sum[DSL_TYPE_INPUT_ATTACHMENTS] += binding->descriptorCount;
                            break;
                        case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT:
                            // count one block per binding. descriptorCount is number of bytes
                            stage_sum[DSL_TYPE_INLINE_UNIFORM_BLOCK]++;
                            break;
                        case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
                            stage_sum[DSL_TYPE_ACCELERATION_STRUCTURE] += binding->descriptorCount;
                            break;
                        case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV:
                            stage_sum[DSL_TYPE_ACCELERATION_STRUCTURE_NV] += binding->descriptorCount;
                            break;
                        default:
                            break;
                    }
                }
            }
        }
        for (auto type : dsl_groups) {
            max_sum[type] = std::max(stage_sum[type], max_sum[type]);
        }
    }
    return max_sum;
}

// Used by PreCallValidateCreatePipelineLayout.
// Returns a map indexed by VK_DESCRIPTOR_TYPE_* enum of the summed descriptors by type.
// Note: descriptors only count against the limit once even if used by multiple stages.
std::map<uint32_t, uint32_t> GetDescriptorSum(
    const std::vector<std::shared_ptr<cvdescriptorset::DescriptorSetLayout const>> &set_layouts, bool skip_update_after_bind) {
    std::map<uint32_t, uint32_t> sum_by_type;
    for (const auto &dsl : set_layouts) {
        if (!dsl) {
            continue;
        }
        if (skip_update_after_bind && (dsl->GetCreateFlags() & VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT)) {
            continue;
        }

        for (uint32_t binding_idx = 0; binding_idx < dsl->GetBindingCount(); binding_idx++) {
            const VkDescriptorSetLayoutBinding *binding = dsl->GetDescriptorSetLayoutBindingPtrFromIndex(binding_idx);
            // Bindings with a descriptorCount of 0 are "reserved" and should be skipped
            if (binding->descriptorCount > 0) {
                if (binding->descriptorType == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT) {
                    // count one block per binding. descriptorCount is number of bytes
                    sum_by_type[binding->descriptorType]++;
                } else {
                    sum_by_type[binding->descriptorType] += binding->descriptorCount;
                }
            }
        }
    }
    return sum_by_type;
}

bool CoreChecks::PreCallValidateCreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo *pCreateInfo,
                                                     const VkAllocationCallbacks *pAllocator,
                                                     VkPipelineLayout *pPipelineLayout) const {
    bool skip = false;

    std::vector<std::shared_ptr<cvdescriptorset::DescriptorSetLayout const>> set_layouts(pCreateInfo->setLayoutCount, nullptr);
    unsigned int push_descriptor_set_count = 0;
    unsigned int descriptor_buffer_set_count = 0;
    unsigned int valid_set_count = 0;
    {
        for (uint32_t i = 0; i < pCreateInfo->setLayoutCount; ++i) {
            set_layouts[i] = Get<cvdescriptorset::DescriptorSetLayout>(pCreateInfo->pSetLayouts[i]);
            if (set_layouts[i]) {
                if (set_layouts[i]->IsPushDescriptor()) ++push_descriptor_set_count;
                if (set_layouts[i]->GetCreateFlags() & VK_DESCRIPTOR_SET_LAYOUT_CREATE_HOST_ONLY_POOL_BIT_EXT) {
                    skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-pSetLayouts-04606",
                                     "vkCreatePipelineLayout(): pCreateInfo->pSetLayouts[%" PRIu32
                                     "] was created with VK_DESCRIPTOR_SET_LAYOUT_CREATE_HOST_ONLY_POOL_BIT_EXT bit.",
                                     i);
                }
                ++valid_set_count;
                if (set_layouts[i]->GetCreateFlags() & VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT) {
                    ++descriptor_buffer_set_count;
                }
            }
        }
    }

    if ((descriptor_buffer_set_count != 0) && (valid_set_count != descriptor_buffer_set_count)) {
        skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-pSetLayouts-08008",
                         "vkCreatePipelineLayout() All sets must be created with "
                         "VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT or none of them.");
    }

    if (push_descriptor_set_count > 1) {
        skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-pSetLayouts-00293",
                         "vkCreatePipelineLayout() Multiple push descriptor sets found.");
    }

    // Max descriptors by type, within a single pipeline stage
    std::valarray<uint32_t> max_descriptors_per_stage = GetDescriptorCountMaxPerStage(&enabled_features, set_layouts, true);
    // Samplers
    if (max_descriptors_per_stage[DSL_TYPE_SAMPLERS] > phys_dev_props.limits.maxPerStageDescriptorSamplers) {
        skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-03016",
                         "vkCreatePipelineLayout(): max per-stage sampler bindings count (%d) exceeds device "
                         "maxPerStageDescriptorSamplers limit (%d).",
                         max_descriptors_per_stage[DSL_TYPE_SAMPLERS], phys_dev_props.limits.maxPerStageDescriptorSamplers);
    }

    // Uniform buffers
    if (max_descriptors_per_stage[DSL_TYPE_UNIFORM_BUFFERS] > phys_dev_props.limits.maxPerStageDescriptorUniformBuffers) {
        skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-03017",
                         "vkCreatePipelineLayout(): max per-stage uniform buffer bindings count (%d) exceeds device "
                         "maxPerStageDescriptorUniformBuffers limit (%d).",
                         max_descriptors_per_stage[DSL_TYPE_UNIFORM_BUFFERS],
                         phys_dev_props.limits.maxPerStageDescriptorUniformBuffers);
    }

    // Storage buffers
    if (max_descriptors_per_stage[DSL_TYPE_STORAGE_BUFFERS] > phys_dev_props.limits.maxPerStageDescriptorStorageBuffers) {
        skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-03018",
                         "vkCreatePipelineLayout(): max per-stage storage buffer bindings count (%d) exceeds device "
                         "maxPerStageDescriptorStorageBuffers limit (%d).",
                         max_descriptors_per_stage[DSL_TYPE_STORAGE_BUFFERS],
                         phys_dev_props.limits.maxPerStageDescriptorStorageBuffers);
    }

    // Sampled images
    if (max_descriptors_per_stage[DSL_TYPE_SAMPLED_IMAGES] > phys_dev_props.limits.maxPerStageDescriptorSampledImages) {
        skip |=
            LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-06939",
                     "vkCreatePipelineLayout(): max per-stage sampled image bindings count (%d) exceeds device "
                     "maxPerStageDescriptorSampledImages limit (%d).",
                     max_descriptors_per_stage[DSL_TYPE_SAMPLED_IMAGES], phys_dev_props.limits.maxPerStageDescriptorSampledImages);
    }

    // Storage images
    if (max_descriptors_per_stage[DSL_TYPE_STORAGE_IMAGES] > phys_dev_props.limits.maxPerStageDescriptorStorageImages) {
        skip |=
            LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-03020",
                     "vkCreatePipelineLayout(): max per-stage storage image bindings count (%d) exceeds device "
                     "maxPerStageDescriptorStorageImages limit (%d).",
                     max_descriptors_per_stage[DSL_TYPE_STORAGE_IMAGES], phys_dev_props.limits.maxPerStageDescriptorStorageImages);
    }

    // Input attachments
    if (max_descriptors_per_stage[DSL_TYPE_INPUT_ATTACHMENTS] > phys_dev_props.limits.maxPerStageDescriptorInputAttachments) {
        skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-03021",
                         "vkCreatePipelineLayout(): max per-stage input attachment bindings count (%d) exceeds device "
                         "maxPerStageDescriptorInputAttachments limit (%d).",
                         max_descriptors_per_stage[DSL_TYPE_INPUT_ATTACHMENTS],
                         phys_dev_props.limits.maxPerStageDescriptorInputAttachments);
    }

    // Inline uniform blocks
    if (max_descriptors_per_stage[DSL_TYPE_INLINE_UNIFORM_BLOCK] >
        phys_dev_ext_props.inline_uniform_block_props.maxPerStageDescriptorInlineUniformBlocks) {
        skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-02214",
                         "vkCreatePipelineLayout(): max per-stage inline uniform block bindings count (%d) exceeds device "
                         "maxPerStageDescriptorInlineUniformBlocks limit (%d).",
                         max_descriptors_per_stage[DSL_TYPE_INLINE_UNIFORM_BLOCK],
                         phys_dev_ext_props.inline_uniform_block_props.maxPerStageDescriptorInlineUniformBlocks);
    }

    // Acceleration structures
    if (max_descriptors_per_stage[DSL_TYPE_ACCELERATION_STRUCTURE] >
        phys_dev_ext_props.acc_structure_props.maxPerStageDescriptorAccelerationStructures) {
        skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-03571",
                         "vkCreatePipelineLayout(): max per-stage acceleration structure bindings count (%" PRIu32
                         ") exceeds device "
                         "maxPerStageDescriptorAccelerationStructures limit (%" PRIu32 ").",
                         max_descriptors_per_stage[DSL_TYPE_ACCELERATION_STRUCTURE],
                         phys_dev_ext_props.acc_structure_props.maxPerStageDescriptorAccelerationStructures);
    }

    // Total descriptors by type
    //
    std::map<uint32_t, uint32_t> sum_all_stages = GetDescriptorSum(set_layouts, true);
    // Samplers
    uint32_t sum = sum_all_stages[VK_DESCRIPTOR_TYPE_SAMPLER] + sum_all_stages[VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER];
    if (sum > phys_dev_props.limits.maxDescriptorSetSamplers) {
        skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-03028",
                         "vkCreatePipelineLayout(): sum of sampler bindings among all stages (%d) exceeds device "
                         "maxDescriptorSetSamplers limit (%d).",
                         sum, phys_dev_props.limits.maxDescriptorSetSamplers);
    }

    // Uniform buffers
    if (sum_all_stages[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER] > phys_dev_props.limits.maxDescriptorSetUniformBuffers) {
        skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-03029",
                         "vkCreatePipelineLayout(): sum of uniform buffer bindings among all stages (%d) exceeds device "
                         "maxDescriptorSetUniformBuffers limit (%d).",
                         sum_all_stages[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER], phys_dev_props.limits.maxDescriptorSetUniformBuffers);
    }

    // Dynamic uniform buffers
    if (sum_all_stages[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC] > phys_dev_props.limits.maxDescriptorSetUniformBuffersDynamic) {
        skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-03030",
                         "vkCreatePipelineLayout(): sum of dynamic uniform buffer bindings among all stages (%d) exceeds device "
                         "maxDescriptorSetUniformBuffersDynamic limit (%d).",
                         sum_all_stages[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC],
                         phys_dev_props.limits.maxDescriptorSetUniformBuffersDynamic);
    }

    // Storage buffers
    if (sum_all_stages[VK_DESCRIPTOR_TYPE_STORAGE_BUFFER] > phys_dev_props.limits.maxDescriptorSetStorageBuffers) {
        skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-03031",
                         "vkCreatePipelineLayout(): sum of storage buffer bindings among all stages (%d) exceeds device "
                         "maxDescriptorSetStorageBuffers limit (%d).",
                         sum_all_stages[VK_DESCRIPTOR_TYPE_STORAGE_BUFFER], phys_dev_props.limits.maxDescriptorSetStorageBuffers);
    }

    // Dynamic storage buffers
    if (sum_all_stages[VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC] > phys_dev_props.limits.maxDescriptorSetStorageBuffersDynamic) {
        skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-03032",
                         "vkCreatePipelineLayout(): sum of dynamic storage buffer bindings among all stages (%d) exceeds device "
                         "maxDescriptorSetStorageBuffersDynamic limit (%d).",
                         sum_all_stages[VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC],
                         phys_dev_props.limits.maxDescriptorSetStorageBuffersDynamic);
    }

    //  Sampled images
    sum = sum_all_stages[VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE] + sum_all_stages[VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER] +
          sum_all_stages[VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER];
    if (sum > phys_dev_props.limits.maxDescriptorSetSampledImages) {
        skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-03033",
                         "vkCreatePipelineLayout(): sum of sampled image bindings among all stages (%d) exceeds device "
                         "maxDescriptorSetSampledImages limit (%d).",
                         sum, phys_dev_props.limits.maxDescriptorSetSampledImages);
    }

    //  Storage images
    sum = sum_all_stages[VK_DESCRIPTOR_TYPE_STORAGE_IMAGE] + sum_all_stages[VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER];
    if (sum > phys_dev_props.limits.maxDescriptorSetStorageImages) {
        skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-03034",
                         "vkCreatePipelineLayout(): sum of storage image bindings among all stages (%d) exceeds device "
                         "maxDescriptorSetStorageImages limit (%d).",
                         sum, phys_dev_props.limits.maxDescriptorSetStorageImages);
    }

    // Input attachments
    if (sum_all_stages[VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT] > phys_dev_props.limits.maxDescriptorSetInputAttachments) {
        skip |=
            LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-03035",
                     "vkCreatePipelineLayout(): sum of input attachment bindings among all stages (%d) exceeds device "
                     "maxDescriptorSetInputAttachments limit (%d).",
                     sum_all_stages[VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT], phys_dev_props.limits.maxDescriptorSetInputAttachments);
    }

    // Inline uniform blocks
    if (sum_all_stages[VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT] >
        phys_dev_ext_props.inline_uniform_block_props.maxDescriptorSetInlineUniformBlocks) {
        skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-02216",
                         "vkCreatePipelineLayout(): sum of inline uniform block bindings among all stages (%d) exceeds device "
                         "maxDescriptorSetInlineUniformBlocks limit (%d).",
                         sum_all_stages[VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT],
                         phys_dev_ext_props.inline_uniform_block_props.maxDescriptorSetInlineUniformBlocks);
    }

    // Acceleration structures
    if (sum_all_stages[VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR] >
        phys_dev_ext_props.acc_structure_props.maxDescriptorSetAccelerationStructures) {
        skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-03573",
                         "vkCreatePipelineLayout(): sum of acceleration structures bindings among all stages (%" PRIu32
                         ") exceeds device "
                         "maxDescriptorSetAccelerationStructures limit (%" PRIu32 ").",
                         sum_all_stages[VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR],
                         phys_dev_ext_props.acc_structure_props.maxDescriptorSetAccelerationStructures);
    }

    // Acceleration structures NV
    if (sum_all_stages[VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV] >
        phys_dev_ext_props.ray_tracing_props_nv.maxDescriptorSetAccelerationStructures) {
        skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-02381",
                         "vkCreatePipelineLayout(): sum of acceleration structures NV bindings among all stages (%" PRIu32
                         ") exceeds device "
                         "VkPhysicalDeviceRayTracingPropertiesNV::maxDescriptorSetAccelerationStructures limit (%" PRIu32 ").",
                         sum_all_stages[VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV],
                         phys_dev_ext_props.ray_tracing_props_nv.maxDescriptorSetAccelerationStructures);
    }

    // Extension exposes new properties limits
    if (IsExtEnabled(device_extensions.vk_ext_descriptor_indexing)) {
        // XXX TODO: replace with correct VU messages

        // Max descriptors by type, within a single pipeline stage
        std::valarray<uint32_t> max_descriptors_per_stage_update_after_bind =
            GetDescriptorCountMaxPerStage(&enabled_features, set_layouts, false);
        // Samplers
        if (max_descriptors_per_stage_update_after_bind[DSL_TYPE_SAMPLERS] >
            phys_dev_props_core12.maxPerStageDescriptorUpdateAfterBindSamplers) {
            skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-03022",
                             "vkCreatePipelineLayout(): max per-stage sampler bindings count (%d) exceeds device "
                             "maxPerStageDescriptorUpdateAfterBindSamplers limit (%d).",
                             max_descriptors_per_stage_update_after_bind[DSL_TYPE_SAMPLERS],
                             phys_dev_props_core12.maxPerStageDescriptorUpdateAfterBindSamplers);
        }

        // Uniform buffers
        if (max_descriptors_per_stage_update_after_bind[DSL_TYPE_UNIFORM_BUFFERS] >
            phys_dev_props_core12.maxPerStageDescriptorUpdateAfterBindUniformBuffers) {
            skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-03023",
                             "vkCreatePipelineLayout(): max per-stage uniform buffer bindings count (%d) exceeds device "
                             "maxPerStageDescriptorUpdateAfterBindUniformBuffers limit (%d).",
                             max_descriptors_per_stage_update_after_bind[DSL_TYPE_UNIFORM_BUFFERS],
                             phys_dev_props_core12.maxPerStageDescriptorUpdateAfterBindUniformBuffers);
        }

        // Storage buffers
        if (max_descriptors_per_stage_update_after_bind[DSL_TYPE_STORAGE_BUFFERS] >
            phys_dev_props_core12.maxPerStageDescriptorUpdateAfterBindStorageBuffers) {
            skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-03024",
                             "vkCreatePipelineLayout(): max per-stage storage buffer bindings count (%d) exceeds device "
                             "maxPerStageDescriptorUpdateAfterBindStorageBuffers limit (%d).",
                             max_descriptors_per_stage_update_after_bind[DSL_TYPE_STORAGE_BUFFERS],
                             phys_dev_props_core12.maxPerStageDescriptorUpdateAfterBindStorageBuffers);
        }

        // Sampled images
        if (max_descriptors_per_stage_update_after_bind[DSL_TYPE_SAMPLED_IMAGES] >
            phys_dev_props_core12.maxPerStageDescriptorUpdateAfterBindSampledImages) {
            skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-03025",
                             "vkCreatePipelineLayout(): max per-stage sampled image bindings count (%d) exceeds device "
                             "maxPerStageDescriptorUpdateAfterBindSampledImages limit (%d).",
                             max_descriptors_per_stage_update_after_bind[DSL_TYPE_SAMPLED_IMAGES],
                             phys_dev_props_core12.maxPerStageDescriptorUpdateAfterBindSampledImages);
        }

        // Storage images
        if (max_descriptors_per_stage_update_after_bind[DSL_TYPE_STORAGE_IMAGES] >
            phys_dev_props_core12.maxPerStageDescriptorUpdateAfterBindStorageImages) {
            skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-03026",
                             "vkCreatePipelineLayout(): max per-stage storage image bindings count (%d) exceeds device "
                             "maxPerStageDescriptorUpdateAfterBindStorageImages limit (%d).",
                             max_descriptors_per_stage_update_after_bind[DSL_TYPE_STORAGE_IMAGES],
                             phys_dev_props_core12.maxPerStageDescriptorUpdateAfterBindStorageImages);
        }

        // Input attachments
        if (max_descriptors_per_stage_update_after_bind[DSL_TYPE_INPUT_ATTACHMENTS] >
            phys_dev_props_core12.maxPerStageDescriptorUpdateAfterBindInputAttachments) {
            skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-03027",
                             "vkCreatePipelineLayout(): max per-stage input attachment bindings count (%d) exceeds device "
                             "maxPerStageDescriptorUpdateAfterBindInputAttachments limit (%d).",
                             max_descriptors_per_stage_update_after_bind[DSL_TYPE_INPUT_ATTACHMENTS],
                             phys_dev_props_core12.maxPerStageDescriptorUpdateAfterBindInputAttachments);
        }

        // Inline uniform blocks
        if (max_descriptors_per_stage_update_after_bind[DSL_TYPE_INLINE_UNIFORM_BLOCK] >
            phys_dev_ext_props.inline_uniform_block_props.maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks) {
            skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-02215",
                             "vkCreatePipelineLayout(): max per-stage inline uniform block bindings count (%d) exceeds device "
                             "maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks limit (%d).",
                             max_descriptors_per_stage_update_after_bind[DSL_TYPE_INLINE_UNIFORM_BLOCK],
                             phys_dev_ext_props.inline_uniform_block_props.maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks);
        }

        // Acceleration structures
        if (max_descriptors_per_stage_update_after_bind[DSL_TYPE_ACCELERATION_STRUCTURE] >
            phys_dev_ext_props.acc_structure_props.maxPerStageDescriptorUpdateAfterBindAccelerationStructures) {
            skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-03572",
                             "vkCreatePipelineLayout(): max per-stage acceleration structure bindings count (%d) exceeds device "
                             "maxPerStageDescriptorUpdateAfterBindAccelerationStructures limit (%d).",
                             max_descriptors_per_stage_update_after_bind[DSL_TYPE_ACCELERATION_STRUCTURE],
                             phys_dev_ext_props.acc_structure_props.maxPerStageDescriptorUpdateAfterBindAccelerationStructures);
        }

        // Total descriptors by type, summed across all pipeline stages
        //
        std::map<uint32_t, uint32_t> sum_all_stages_update_after_bind = GetDescriptorSum(set_layouts, false);
        // Samplers
        sum = sum_all_stages_update_after_bind[VK_DESCRIPTOR_TYPE_SAMPLER] +
              sum_all_stages_update_after_bind[VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER];
        if (sum > phys_dev_props_core12.maxDescriptorSetUpdateAfterBindSamplers) {
            skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-pSetLayouts-03036",
                             "vkCreatePipelineLayout(): sum of sampler bindings among all stages (%d) exceeds device "
                             "maxDescriptorSetUpdateAfterBindSamplers limit (%d).",
                             sum, phys_dev_props_core12.maxDescriptorSetUpdateAfterBindSamplers);
        }

        // Uniform buffers
        if (sum_all_stages_update_after_bind[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER] >
            phys_dev_props_core12.maxDescriptorSetUpdateAfterBindUniformBuffers) {
            skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-pSetLayouts-03037",
                             "vkCreatePipelineLayout(): sum of uniform buffer bindings among all stages (%d) exceeds device "
                             "maxDescriptorSetUpdateAfterBindUniformBuffers limit (%d).",
                             sum_all_stages_update_after_bind[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER],
                             phys_dev_props_core12.maxDescriptorSetUpdateAfterBindUniformBuffers);
        }

        // Dynamic uniform buffers
        if (sum_all_stages_update_after_bind[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC] >
            phys_dev_props_core12.maxDescriptorSetUpdateAfterBindUniformBuffersDynamic) {
            skip |=
                LogError(device, "VUID-VkPipelineLayoutCreateInfo-pSetLayouts-03038",
                         "vkCreatePipelineLayout(): sum of dynamic uniform buffer bindings among all stages (%d) exceeds device "
                         "maxDescriptorSetUpdateAfterBindUniformBuffersDynamic limit (%d).",
                         sum_all_stages_update_after_bind[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC],
                         phys_dev_props_core12.maxDescriptorSetUpdateAfterBindUniformBuffersDynamic);
        }

        // Storage buffers
        if (sum_all_stages_update_after_bind[VK_DESCRIPTOR_TYPE_STORAGE_BUFFER] >
            phys_dev_props_core12.maxDescriptorSetUpdateAfterBindStorageBuffers) {
            skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-pSetLayouts-03039",
                             "vkCreatePipelineLayout(): sum of storage buffer bindings among all stages (%d) exceeds device "
                             "maxDescriptorSetUpdateAfterBindStorageBuffers limit (%d).",
                             sum_all_stages_update_after_bind[VK_DESCRIPTOR_TYPE_STORAGE_BUFFER],
                             phys_dev_props_core12.maxDescriptorSetUpdateAfterBindStorageBuffers);
        }

        // Dynamic storage buffers
        if (sum_all_stages_update_after_bind[VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC] >
            phys_dev_props_core12.maxDescriptorSetUpdateAfterBindStorageBuffersDynamic) {
            skip |=
                LogError(device, "VUID-VkPipelineLayoutCreateInfo-pSetLayouts-03040",
                         "vkCreatePipelineLayout(): sum of dynamic storage buffer bindings among all stages (%d) exceeds device "
                         "maxDescriptorSetUpdateAfterBindStorageBuffersDynamic limit (%d).",
                         sum_all_stages_update_after_bind[VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC],
                         phys_dev_props_core12.maxDescriptorSetUpdateAfterBindStorageBuffersDynamic);
        }

        //  Sampled images
        sum = sum_all_stages_update_after_bind[VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE] +
              sum_all_stages_update_after_bind[VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER] +
              sum_all_stages_update_after_bind[VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER];
        if (sum > phys_dev_props_core12.maxDescriptorSetUpdateAfterBindSampledImages) {
            skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-pSetLayouts-03041",
                             "vkCreatePipelineLayout(): sum of sampled image bindings among all stages (%d) exceeds device "
                             "maxDescriptorSetUpdateAfterBindSampledImages limit (%d).",
                             sum, phys_dev_props_core12.maxDescriptorSetUpdateAfterBindSampledImages);
        }

        //  Storage images
        sum = sum_all_stages_update_after_bind[VK_DESCRIPTOR_TYPE_STORAGE_IMAGE] +
              sum_all_stages_update_after_bind[VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER];
        if (sum > phys_dev_props_core12.maxDescriptorSetUpdateAfterBindStorageImages) {
            skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-pSetLayouts-03042",
                             "vkCreatePipelineLayout(): sum of storage image bindings among all stages (%d) exceeds device "
                             "maxDescriptorSetUpdateAfterBindStorageImages limit (%d).",
                             sum, phys_dev_props_core12.maxDescriptorSetUpdateAfterBindStorageImages);
        }

        // Input attachments
        if (sum_all_stages_update_after_bind[VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT] >
            phys_dev_props_core12.maxDescriptorSetUpdateAfterBindInputAttachments) {
            skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-pSetLayouts-03043",
                             "vkCreatePipelineLayout(): sum of input attachment bindings among all stages (%d) exceeds device "
                             "maxDescriptorSetUpdateAfterBindInputAttachments limit (%d).",
                             sum_all_stages_update_after_bind[VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT],
                             phys_dev_props_core12.maxDescriptorSetUpdateAfterBindInputAttachments);
        }

        // Inline uniform blocks
        if (sum_all_stages_update_after_bind[VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT] >
            phys_dev_ext_props.inline_uniform_block_props.maxDescriptorSetUpdateAfterBindInlineUniformBlocks) {
            skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-02217",
                             "vkCreatePipelineLayout(): sum of inline uniform block bindings among all stages (%d) exceeds device "
                             "maxDescriptorSetUpdateAfterBindInlineUniformBlocks limit (%d).",
                             sum_all_stages_update_after_bind[VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT],
                             phys_dev_ext_props.inline_uniform_block_props.maxDescriptorSetUpdateAfterBindInlineUniformBlocks);
        }

        // Acceleration structures
        if (sum_all_stages_update_after_bind[VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR] >
            phys_dev_ext_props.acc_structure_props.maxDescriptorSetUpdateAfterBindAccelerationStructures) {
            skip |=
                LogError(device, "VUID-VkPipelineLayoutCreateInfo-descriptorType-03574",
                         "vkCreatePipelineLayout(): sum of acceleration structures bindings among all stages (%d) exceeds device "
                         "maxDescriptorSetUpdateAfterBindAccelerationStructures limit (%d).",
                         sum_all_stages_update_after_bind[VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR],
                         phys_dev_ext_props.acc_structure_props.maxDescriptorSetUpdateAfterBindAccelerationStructures);
        }
    }

    // Extension exposes new properties limits
    if (IsExtEnabled(device_extensions.vk_ext_fragment_density_map2)) {
        uint32_t sum_subsampled_samplers = 0;
        for (const auto &dsl : set_layouts) {
            // find the number of subsampled samplers across all stages
            // NOTE: this does not use the GetDescriptorSum patter because it needs the Get<SAMPLER_STATE> method
            if ((dsl->GetCreateFlags() & VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT)) {
                continue;
            }
            for (uint32_t binding_idx = 0; binding_idx < dsl->GetBindingCount(); binding_idx++) {
                const VkDescriptorSetLayoutBinding *binding = dsl->GetDescriptorSetLayoutBindingPtrFromIndex(binding_idx);

                // Bindings with a descriptorCount of 0 are "reserved" and should be skipped
                if (binding->descriptorCount > 0) {
                    if (((binding->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) ||
                         (binding->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER)) &&
                        (binding->pImmutableSamplers != nullptr)) {
                        for (uint32_t sampler_idx = 0; sampler_idx < binding->descriptorCount; sampler_idx++) {
                            auto state = Get<SAMPLER_STATE>(binding->pImmutableSamplers[sampler_idx]);
                            if (state && (state->createInfo.flags & (VK_SAMPLER_CREATE_SUBSAMPLED_BIT_EXT |
                                                                     VK_SAMPLER_CREATE_SUBSAMPLED_COARSE_RECONSTRUCTION_BIT_EXT))) {
                                sum_subsampled_samplers++;
                            }
                        }
                    }
                }
            }
        }
        if (sum_subsampled_samplers > phys_dev_ext_props.fragment_density_map2_props.maxDescriptorSetSubsampledSamplers) {
            skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-pImmutableSamplers-03566",
                             "vkCreatePipelineLayout(): sum of sampler bindings with flags containing "
                             "VK_SAMPLER_CREATE_SUBSAMPLED_BIT_EXT or "
                             "VK_SAMPLER_CREATE_SUBSAMPLED_COARSE_RECONSTRUCTION_BIT_EXT among all stages(% d) "
                             "exceeds device maxDescriptorSetSubsampledSamplers limit (%d).",
                             sum_subsampled_samplers,
                             phys_dev_ext_props.fragment_density_map2_props.maxDescriptorSetSubsampledSamplers);
        }
    }

    if (!enabled_features.graphics_pipeline_library_features.graphicsPipelineLibrary) {
        for (uint32_t i = 0; i < pCreateInfo->setLayoutCount; ++i) {
            if (!pCreateInfo->pSetLayouts[i]) {
                skip |= LogError(device, "VUID-VkPipelineLayoutCreateInfo-graphicsPipelineLibrary-06753",
                                 "vkCreatePipelineLayout(): pSetLayouts[%" PRIu32
                                 "] is VK_NULL_HANDLE, but graphicsPipelineLibrary is not enabled.",
                                 i);
            }
        }
    }
    return skip;
}

bool CoreChecks::PreCallValidateCmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout,
                                                 VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size,
                                                 const void *pValues) const {
    bool skip = false;
    auto cb_state = GetRead<CMD_BUFFER_STATE>(commandBuffer);
    assert(cb_state);
    skip |= ValidateCmd(*cb_state, CMD_PUSHCONSTANTS);

    // Check if pipeline_layout VkPushConstantRange(s) overlapping offset, size have stageFlags set for each stage in the command
    // stageFlags argument, *and* that the command stageFlags argument has bits set for the stageFlags in each overlapping range.
    if (!skip) {
        auto layout_state = Get<PIPELINE_LAYOUT_STATE>(layout);
        const auto &ranges = *layout_state->push_constant_ranges;
        VkShaderStageFlags found_stages = 0;
        for (const auto &range : ranges) {
            if ((offset >= range.offset) && (offset + size <= range.offset + range.size)) {
                VkShaderStageFlags matching_stages = range.stageFlags & stageFlags;
                if (matching_stages != range.stageFlags) {
                    skip |=
                        LogError(commandBuffer, "VUID-vkCmdPushConstants-offset-01796",
                                 "vkCmdPushConstants(): stageFlags (%s, offset (%" PRIu32 "), and size (%" PRIu32
                                 "),  must contain all stages in overlapping VkPushConstantRange stageFlags (%s), offset (%" PRIu32
                                 "), and size (%" PRIu32 ") in %s.",
                                 string_VkShaderStageFlags(stageFlags).c_str(), offset, size,
                                 string_VkShaderStageFlags(range.stageFlags).c_str(), range.offset, range.size,
                                 report_data->FormatHandle(layout).c_str());
                }

                // Accumulate all stages we've found
                found_stages = matching_stages | found_stages;
            }
        }
        if (found_stages != stageFlags) {
            uint32_t missing_stages = ~found_stages & stageFlags;
            skip |= LogError(
                commandBuffer, "VUID-vkCmdPushConstants-offset-01795",
                "vkCmdPushConstants(): %s, VkPushConstantRange in %s overlapping offset = %d and size = %d, do not contain %s.",
                string_VkShaderStageFlags(stageFlags).c_str(), report_data->FormatHandle(layout).c_str(), offset, size,
                string_VkShaderStageFlags(missing_stages).c_str());
        }
    }
    return skip;
}

bool CoreChecks::PreCallValidateCreateSampler(VkDevice device, const VkSamplerCreateInfo *pCreateInfo,
                                              const VkAllocationCallbacks *pAllocator, VkSampler *pSampler) const {
    bool skip = false;

    auto num_samplers = Count<SAMPLER_STATE>();
    if (num_samplers >= phys_dev_props.limits.maxSamplerAllocationCount) {
        skip |= LogError(
            device, "VUID-vkCreateSampler-maxSamplerAllocationCount-04110",
            "vkCreateSampler(): Number of currently valid sampler objects (%zu) is not less than the maximum allowed (%u).",
            num_samplers, phys_dev_props.limits.maxSamplerAllocationCount);
    }

    const auto sampler_reduction = LvlFindInChain<VkSamplerReductionModeCreateInfo>(pCreateInfo->pNext);
    if (sampler_reduction && sampler_reduction->reductionMode != VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE) {
        if ((api_version >= VK_API_VERSION_1_2) && !enabled_features.core12.samplerFilterMinmax) {
            skip |= LogError(
                device, "VUID-VkSamplerCreateInfo-pNext-06726",
                "vkCreateSampler(): VkSamplerReductionModeCreateInfo is included in the pNext chain, samplerFilterMinmax is not "
                "enabled, and VkSamplerReductionModeCreateInfo::reductionMode (%s) != VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE.",
                string_VkSamplerReductionMode(sampler_reduction->reductionMode));
        } else if ((api_version < VK_API_VERSION_1_2) && !IsExtEnabled(device_extensions.vk_ext_sampler_filter_minmax)) {
            // NOTE: technically this VUID is only if the corresponding _feature_ is not enabled, and only if on api_version
            // >= 1.2, but there doesn't appear to be a similar VUID for when api_version < 1.2
            skip |= LogError(device, "VUID-VkSamplerCreateInfo-pNext-06726",
                             "vkCreateSampler(): sampler reduction mode is %s, but extension %s is not enabled.",
                             string_VkSamplerReductionMode(sampler_reduction->reductionMode),
                             VK_EXT_SAMPLER_FILTER_MINMAX_EXTENSION_NAME);
        }
    }
    if (enabled_features.core11.samplerYcbcrConversion == VK_TRUE) {
        const VkSamplerYcbcrConversionInfo *conversion_info = LvlFindInChain<VkSamplerYcbcrConversionInfo>(pCreateInfo->pNext);
        if (conversion_info != nullptr) {
            const VkSamplerYcbcrConversion sampler_ycbcr_conversion = conversion_info->conversion;
            auto ycbcr_state = Get<SAMPLER_YCBCR_CONVERSION_STATE>(sampler_ycbcr_conversion);
            if ((ycbcr_state->format_features &
                 VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_YCBCR_CONVERSION_SEPARATE_RECONSTRUCTION_FILTER_BIT_KHR) == 0) {
                const VkFilter chroma_filter = ycbcr_state->chromaFilter;
                if (pCreateInfo->minFilter != chroma_filter) {
                    skip |= LogError(
                        device, "VUID-VkSamplerCreateInfo-minFilter-01645",
                        "VkCreateSampler: VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_SEPARATE_RECONSTRUCTION_FILTER_BIT is "
                        "not supported for SamplerYcbcrConversion's (%s) format %s so minFilter (%s) needs to be equal to "
                        "chromaFilter (%s)",
                        report_data->FormatHandle(sampler_ycbcr_conversion).c_str(), string_VkFormat(ycbcr_state->format),
                        string_VkFilter(pCreateInfo->minFilter), string_VkFilter(chroma_filter));
                }
                if (pCreateInfo->magFilter != chroma_filter) {
                    skip |= LogError(
                        device, "VUID-VkSamplerCreateInfo-minFilter-01645",
                        "VkCreateSampler: VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_SEPARATE_RECONSTRUCTION_FILTER_BIT is "
                        "not supported for SamplerYcbcrConversion's (%s) format %s so minFilter (%s) needs to be equal to "
                        "chromaFilter (%s)",
                        report_data->FormatHandle(sampler_ycbcr_conversion).c_str(), string_VkFormat(ycbcr_state->format),
                        string_VkFilter(pCreateInfo->minFilter), string_VkFilter(chroma_filter));
                }
            }
            // At this point there is a known sampler YCbCr conversion enabled
            if (sampler_reduction) {
                if (sampler_reduction->reductionMode != VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE) {
                    skip |= LogError(device, "VUID-VkSamplerCreateInfo-None-01647",
                                     "A sampler YCbCr Conversion is being used creating this sampler so the sampler reduction mode "
                                     "must be VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE.");
                }
            }
        }
    }

    if (pCreateInfo->borderColor == VK_BORDER_COLOR_INT_CUSTOM_EXT ||
        pCreateInfo->borderColor == VK_BORDER_COLOR_FLOAT_CUSTOM_EXT) {
        if (!enabled_features.custom_border_color_features.customBorderColors) {
            skip |=
                LogError(device, "VUID-VkSamplerCreateInfo-customBorderColors-04085",
                         "vkCreateSampler(): A custom border color was specified without enabling the custom border color feature");
        }
        auto custom_create_info = LvlFindInChain<VkSamplerCustomBorderColorCreateInfoEXT>(pCreateInfo->pNext);
        if (custom_create_info) {
            if (custom_create_info->format == VK_FORMAT_UNDEFINED &&
                !enabled_features.custom_border_color_features.customBorderColorWithoutFormat) {
                skip |= LogError(device, "VUID-VkSamplerCustomBorderColorCreateInfoEXT-format-04014",
                                 "vkCreateSampler(): A custom border color was specified as VK_FORMAT_UNDEFINED without the "
                                 "customBorderColorWithoutFormat feature being enabled");
            }
        }
        if (custom_border_color_sampler_count >= phys_dev_ext_props.custom_border_color_props.maxCustomBorderColorSamplers) {
            skip |= LogError(device, "VUID-VkSamplerCreateInfo-None-04012",
                             "vkCreateSampler(): Creating a sampler with a custom border color will exceed the "
                             "maxCustomBorderColorSamplers limit of %d",
                             phys_dev_ext_props.custom_border_color_props.maxCustomBorderColorSamplers);
        }
    }

    if (IsExtEnabled(device_extensions.vk_khr_portability_subset)) {
        if ((VK_FALSE == enabled_features.portability_subset_features.samplerMipLodBias) && pCreateInfo->mipLodBias != 0) {
            skip |= LogError(device, "VUID-VkSamplerCreateInfo-samplerMipLodBias-04467",
                             "vkCreateSampler (portability error): mip LOD bias not supported.");
        }
    }

    // If any of addressModeU, addressModeV or addressModeW are VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE, the
    // VK_KHR_sampler_mirror_clamp_to_edge extension or promoted feature must be enabled
    if ((device_extensions.vk_khr_sampler_mirror_clamp_to_edge != kEnabledByCreateinfo) &&
        (enabled_features.core12.samplerMirrorClampToEdge == VK_FALSE)) {
        // Use 'else' because getting 3 large error messages is redundant and assume developer, if set all 3, will notice and fix
        // all at once
        if (pCreateInfo->addressModeU == VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE) {
            skip |=
                LogError(device, "VUID-VkSamplerCreateInfo-addressModeU-01079",
                         "vkCreateSampler(): addressModeU is set to VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE but the "
                         "VK_KHR_sampler_mirror_clamp_to_edge extension or samplerMirrorClampToEdge feature has not been enabled.");
        } else if (pCreateInfo->addressModeV == VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE) {
            skip |=
                LogError(device, "VUID-VkSamplerCreateInfo-addressModeU-01079",
                         "vkCreateSampler(): addressModeV is set to VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE but the "
                         "VK_KHR_sampler_mirror_clamp_to_edge extension or samplerMirrorClampToEdge feature has not been enabled.");
        } else if (pCreateInfo->addressModeW == VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE) {
            skip |=
                LogError(device, "VUID-VkSamplerCreateInfo-addressModeU-01079",
                         "vkCreateSampler(): addressModeW is set to VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE but the "
                         "VK_KHR_sampler_mirror_clamp_to_edge extension or samplerMirrorClampToEdge feature has not been enabled.");
        }
    }

    if ((pCreateInfo->flags & VK_SAMPLER_CREATE_NON_SEAMLESS_CUBE_MAP_BIT_EXT) &&
        (!enabled_features.non_seamless_cube_map_features.nonSeamlessCubeMap)) {
        skip |= LogError(device, "VUID-VkSamplerCreateInfo-nonSeamlessCubeMap-06788",
                         "vkCreateSampler(): flags contains VK_SAMPLER_CREATE_NON_SEAMLESS_CUBE_MAP_BIT_EXT but the "
                         "VK_EXT_non_seamless_cube_map feature has not been enabled.");
    }

    if ((pCreateInfo->flags & VK_SAMPLER_CREATE_DESCRIPTOR_BUFFER_CAPTURE_REPLAY_BIT_EXT) &&
        !enabled_features.descriptor_buffer_features.descriptorBufferCaptureReplay) {
        skip |= LogError(
            device, "VUID-VkSamplerCreateInfo-flags-08110",
            "vkCreateSampler(): the descriptorBufferCaptureReplay device feature is disabled: Samplers cannot be created with "
            "the VK_SAMPLER_CREATE_DESCRIPTOR_BUFFER_CAPTURE_REPLAY_BIT_EXT.");
    }

    auto opaque_capture_descriptor_buffer = LvlFindInChain<VkOpaqueCaptureDescriptorDataCreateInfoEXT>(pCreateInfo->pNext);
    if (opaque_capture_descriptor_buffer && !(pCreateInfo->flags & VK_SAMPLER_CREATE_DESCRIPTOR_BUFFER_CAPTURE_REPLAY_BIT_EXT)) {
        skip |= LogError(device, "VUID-VkSamplerCreateInfo-pNext-08111",
                         "vkCreateSampler(): VkOpaqueCaptureDescriptorDataCreateInfoEXT is in pNext chain, but "
                         "VK_SAMPLER_CREATE_DESCRIPTOR_BUFFER_CAPTURE_REPLAY_BIT_EXT is not set.");
    }

    return skip;
}
