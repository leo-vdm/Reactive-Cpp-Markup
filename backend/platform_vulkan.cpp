#if defined(_WIN32) || defined(WIN32) || defined(WIN64) || defined(__CYGWIN__)
#define VK_USE_PLATFORM_WIN32_KHR 1
#endif
#if defined(__linux__) && !defined(_WIN32)
#define VK_USE_PLATFORM_XLIB_KHR 1 
#endif

#define STB_IMAGE_IMPLEMENTATION 1
#include "stb_image.h"

#include <chrono>

#include <vulkan/vulkan.h>
#include "platform.h"
#include <stdio.h>
#include <cassert>
#include "file_system.h"
#include "graphics_types.h"

const char* required_vk_device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

#ifdef NDEBUG
#else
    #define VK_USE_VALIDATION 1
    const char* validation_layers[] = {"VK_LAYER_KHRONOS_validation"};
#endif

struct vk_shader_pair 
{
    void* vert_shader_bin;
    int vert_shader_length;
    void* frag_shader_bin;
    int frag_shader_length;
};

struct vk_optional_features
{
    bool SAMPLER_ANISOTROPY;
    bool RENDERER_MSAA;
    VkSampleCountFlagBits MSAA_SAMPLES;
    bool RENDERER_SAMPLE_SHADING;
};


struct LoadedImageHandle
{
    VkImage vk_image_texture;
    VkImageView vk_image_texture_view;
    int vk_image_memory_offset;
    int image_size;
};


struct VulkanRenderPlatform 
{
    VkInstance vk_instance;
    VkPhysicalDevice vk_physical_device;
    bool vk_logical_device_initialized;
    
    VkDevice vk_device;
    VkQueue vk_graphics_queue;
    VkQueue vk_present_queue;
    
    union
    {
        struct
        {
            uint32_t vk_graphics_queue_family;
            uint32_t vk_present_queue_family;
        };
        uint32_t vk_queue_indeces[2];
    };
    
    VkSwapchainCreateInfoKHR vk_swapchain_settings;
    
    vk_shader_pair vk_opaque_shader;
    vk_shader_pair vk_transparent_shader;
    
    vk_optional_features vk_supported_optionals;
    
    bool vk_graphics_pipeline_initialized;
    VkRenderPass vk_main_render_pass;
    
    VkPipeline vk_opaque_graphics_pipeline;
    VkPipelineLayout vk_opaque_graphics_pipeline_layout;
    
    VkPipeline vk_transparent_graphics_pipeline;
    VkPipelineLayout vk_transparent_graphics_pipeline_layout;
    
    VkCommandPool vk_main_command_pool;
    VkCommandPool vk_transient_command_pool;
    VkDescriptorPool vk_main_descriptor_pool;
    VkDescriptorSetLayout vk_uniform_descriptor_layout;
    VkSampler vk_main_image_sampler;
    
    bool vk_image_buffer_initialized;
    int vk_image_buffer_size;
    int vk_next_image_offset;
    VkBuffer vk_image_buffer;
    VkDeviceMemory vk_image_memory;
    VkDescriptorSet vk_image_descriptors;
    VkDescriptorSetLayout vk_image_descriptor_layout;
    int vk_next_image_index;
        
    Arena* vk_master_arena;
    Arena* vk_pointer_arrays;
    Arena* vk_swapchain_image_views;
    Arena* vk_framebuffers;
    Arena* vk_binary_data;
    Arena* vk_image_handles;
};

VulkanRenderPlatform rendering_platform;


bool vk_extensions_supported(const VulkanSupportedExtensions supported_extensions, const VulkanSupportedExtensions required_extensions)
{
    bool* supported_extensions_bools = (bool*)(&supported_extensions);
    bool* required_extensions_bools = (bool*)(&required_extensions);

    for(int i = 0; i < (sizeof(VulkanSupportedExtensions) / sizeof(bool)); i += sizeof(bool))
    {
        if(required_extensions_bools[i] && !supported_extensions_bools[i])
        {
            return false;
        }
    }
    return true;
}


bool vk_present_supported(VkPhysicalDevice* target_device, VkSurfaceKHR surface, int avoided_family_index, int* present_family_index = NULL)
{
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(*target_device, &queue_family_count, nullptr);
    
    void* allocated_space = AllocScratch((queue_family_count + 1)*sizeof(VkQueueFamilyProperties));
    VkQueueFamilyProperties* supported_families = (VkQueueFamilyProperties*)align_mem(allocated_space, VkQueueFamilyProperties);
    
    vkGetPhysicalDeviceQueueFamilyProperties(*target_device, &queue_family_count, supported_families);
    
    bool found_avoided_family = false;
    
    for(int i = 0; i < queue_family_count; i++)
    {
        VkBool32 present_supported = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(*target_device, i, surface, &present_supported);
    
        if(present_supported)
        {
            // This family is compatible but non-ideal, continue searching
            if(i == avoided_family_index)
            {
                found_avoided_family = true;
                continue;
            }
        
            if(present_family_index)
            {
                *present_family_index = i;
            }            
            
            DeAllocScratch(allocated_space);        
            return true;                              
        }
    }
    
    DeAllocScratch(allocated_space);
    
    // We found a compatible family but it was the non-ideal one
    if(found_avoided_family)
    {
        if(present_family_index)
        {
            *present_family_index = avoided_family_index;
        }
        return true;
    }
    
    return false;
}


bool vk_device_queues_supported(VkPhysicalDevice* target_device, int avoided_family_index, int* graphics_family_index = NULL)
{
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(*target_device, &queue_family_count, nullptr);
    
    void* allocated_space = AllocScratch((queue_family_count + 1)*sizeof(VkQueueFamilyProperties));
    VkQueueFamilyProperties* supported_families = (VkQueueFamilyProperties*)align_mem(allocated_space, VkQueueFamilyProperties);
    
    vkGetPhysicalDeviceQueueFamilyProperties(*target_device, &queue_family_count, supported_families);
    
    bool found_avoided_family = false;
    
    for(int i = 0; i < queue_family_count; i++)
    {
        if((supported_families + i)->queueFlags & VK_QUEUE_GRAPHICS_BIT)
        {
            // This family is compatible but non-ideal, continue searching
            if(i == avoided_family_index)
            {
                found_avoided_family = true;
                continue;
            }
            
            if(graphics_family_index)
            {
                *graphics_family_index = i;
            }
            
            DeAllocScratch(allocated_space);        
            return true;                   
        }
        
    }
    
    DeAllocScratch(allocated_space);
    
    // We found a compatible family but it was the non-ideal one
    if(found_avoided_family)
    {
        if(graphics_family_index)
        {
            *graphics_family_index = avoided_family_index;
        }
        return true;
    }
    
    return false;
}

VkPhysicalDevice* vk_pick_physical_device(VkPhysicalDevice* physical_devices, int device_count)
{
    VkPhysicalDevice* curr_best = NULL;
    int curr_best_score = 0;
    
    for(int i = 0; i < device_count; i++)
    {
        int score = 0;
        VkPhysicalDeviceProperties device_properties;
        VkPhysicalDeviceFeatures device_features;
        vkGetPhysicalDeviceProperties(*(physical_devices + i), &device_properties);
        vkGetPhysicalDeviceFeatures(*(physical_devices + i), &device_features);
        
        printf("Evaluating physical device with name \"%s\"\n", device_properties.deviceName);
        
        if(device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            score += 500;
        }
        
        // More texture resolution = more better
        score += device_properties.limits.maxImageDimension2D;
        
        // Check if device is incompatible
        if(!device_features.geometryShader || !vk_device_queues_supported(physical_devices + i, -1))
        {
            score = 0;
        }
        
        if(score > curr_best_score)
        {
            curr_best_score = score;
            curr_best = (physical_devices + i);
        }
    }
    
    return curr_best;
}

bool vk_check_swapchain_viability(VkSurfaceKHR surface)
{
    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(rendering_platform.vk_physical_device, surface, &format_count, 0);
    
    if(!format_count)
    {
        return false;
    }
    
    uint32_t present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(rendering_platform.vk_physical_device, surface, &present_mode_count, 0);
    
    if(!present_mode_count)
    {
        return false;
    }
    
    return true;
}

VkSurfaceFormatKHR vk_pick_surface_format(VkSurfaceKHR surface)
{
    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(rendering_platform.vk_physical_device, surface, &format_count, 0);
    
    // Should have already verified that formats are non zero before calling this func
    assert(format_count != 0);
    
    void* allocated_space = AllocScratch((format_count + 1)*sizeof(VkSurfaceFormatKHR));
    VkSurfaceFormatKHR* supported_formats = (VkSurfaceFormatKHR*)align_mem(allocated_space, VkSurfaceFormatKHR);
    
    vkGetPhysicalDeviceSurfaceFormatsKHR(rendering_platform.vk_physical_device, surface, &format_count, supported_formats);
    
    for(int i = 0; i < format_count; i++)
    {
        VkSurfaceFormatKHR* curr_format = supported_formats + i;
        // Note(Leo): AMD drivers tend to mess up gamma (darkens the image) when doing device side MSAA in SRGB format, 
        // UNORM seems to fix it without causing gamma problems (lightening the image) in drivers that dont have issues. 
        // Note(Leo): Images also have to be loaded in UNORM because of it being used here!
        // Ideal format
        if(curr_format->format == VK_FORMAT_B8G8R8A8_UNORM && curr_format->colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            DeAllocScratch(allocated_space);
            return *curr_format;
        }
    }
    
    // Just pick a format since we didnt find our ideal
    DeAllocScratch(allocated_space);
    return supported_formats[0];
}

VkPresentModeKHR vk_pick_surface_presentation_mode(VkSurfaceKHR surface)
{
    uint32_t mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(rendering_platform.vk_physical_device, surface, &mode_count, 0);
    
    // Should have already verified that formats are non zero before calling this func
    assert(mode_count != 0);
    
    void* allocated_space = AllocScratch((mode_count + 1)*sizeof(VkPresentModeKHR));
    VkPresentModeKHR* supported_modes = (VkPresentModeKHR*)align_mem(allocated_space, VkPresentModeKHR);
    
    vkGetPhysicalDeviceSurfacePresentModesKHR(rendering_platform.vk_physical_device, surface, &mode_count, supported_modes);
    
    for(int i = 0; i < mode_count; i++)
    {
        VkPresentModeKHR* curr_mode = supported_modes + i;
        // Ideal mode
        if(*curr_mode == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            DeAllocScratch(allocated_space);
            return *curr_mode;
        }
    }
    
    // FIFO is garuanteed to be present but non-ideal
    DeAllocScratch(allocated_space);
    return VK_PRESENT_MODE_FIFO_KHR;
}

bool vk_pick_swapchain_settings(VkSurfaceKHR surface)
{
    if(!vk_check_swapchain_viability(surface))
    {
        return false;
    }
    VkSurfaceFormatKHR chosen_format = vk_pick_surface_format(surface);
    VkPresentModeKHR chosen_mode = vk_pick_surface_presentation_mode(surface);
    
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(rendering_platform.vk_physical_device, surface, &capabilities);
    
    int image_count = capabilities.minImageCount + 1;
    
    if(capabilities.maxImageCount > 0 && capabilities.maxImageCount < image_count)
    {
        image_count = capabilities.maxImageCount;
    }
    
    VkSwapchainCreateInfoKHR settings = {};
    settings.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    settings.imageFormat = chosen_format.format;
    settings.imageColorSpace = chosen_format.colorSpace;
    settings.minImageCount = image_count;
    settings.imageArrayLayers = 1;
    settings.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    settings.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    settings.presentMode = chosen_mode;
    settings.clipped = VK_TRUE;
    settings.oldSwapchain = VK_NULL_HANDLE;
    settings.preTransform = capabilities.currentTransform;
    
    // Note(Leo): share images between graphics and presentation queue if they are seperate queues
    if(rendering_platform.vk_queue_indeces[0] != rendering_platform.vk_queue_indeces[1])
    {
        settings.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        settings.queueFamilyIndexCount = 2;
        settings.pQueueFamilyIndices = rendering_platform.vk_queue_indeces;
    }
    else
    {
        settings.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        settings.queueFamilyIndexCount = 0;
        settings.pQueueFamilyIndices = 0;
    }
    
    rendering_platform.vk_swapchain_settings = settings;
    return true;
}

bool vk_initialize_logical_device(VkSurfaceKHR surface, const char** device_extension_names, int device_extension_count)
{
    // Find the queue family index for our device
    int graphics_family_index;
    if(!vk_device_queues_supported(&(rendering_platform.vk_physical_device), -1, &graphics_family_index))
    {
        printf("Failed to initialize graphics queue!\n");
    }
    
    int present_family_index;
    if(!vk_present_supported(&(rendering_platform.vk_physical_device), surface, graphics_family_index, &present_family_index))
    {
        printf("Failed to initialize presentation queue!\n");
    }
    
    // Queue arguments
    float queue_priority = 1.0f;
    
    VkDeviceQueueCreateInfo graphics_queue_create_info = {};
    graphics_queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    graphics_queue_create_info.queueFamilyIndex = (uint32_t)graphics_family_index;
    graphics_queue_create_info.queueCount = 1;
    graphics_queue_create_info.pQueuePriorities = &queue_priority;
    rendering_platform.vk_queue_indeces[0] = (uint32_t)graphics_family_index;
    
    VkDeviceQueueCreateInfo present_queue_create_info = {};
    present_queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    present_queue_create_info.queueFamilyIndex = (uint32_t)present_family_index;
    present_queue_create_info.queueCount = 1;
    present_queue_create_info.pQueuePriorities = &queue_priority;
    rendering_platform.vk_queue_indeces[1] = (uint32_t)present_family_index;
    
    VkDeviceQueueCreateInfo queue_create_infos[2] = { graphics_queue_create_info, present_queue_create_info };
    
    // Device arguments
    VkPhysicalDeviceFeatures device_features = {};
    device_features.geometryShader = VK_TRUE;
    
    if(rendering_platform.vk_supported_optionals.SAMPLER_ANISOTROPY)
    {
        device_features.samplerAnisotropy = VK_TRUE;
    }
    if(rendering_platform.vk_supported_optionals.RENDERER_SAMPLE_SHADING)
    {
        device_features.sampleRateShading = VK_TRUE;
    }
    
    VkDeviceCreateInfo logical_create_info = {};
    logical_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    logical_create_info.pQueueCreateInfos = queue_create_infos;
    logical_create_info.queueCreateInfoCount = 2;
    logical_create_info.pEnabledFeatures = &device_features;
    
    #if VK_USE_VALIDATION
    logical_create_info.enabledLayerCount = sizeof(validation_layers)/sizeof(char**);
    logical_create_info.ppEnabledLayerNames = validation_layers;
    #else
    logical_create_info.enabledLayerCount = 0;
    #endif
    
    logical_create_info.enabledExtensionCount = device_extension_count;
    logical_create_info.ppEnabledExtensionNames = device_extension_names;
    
    
    if(vkCreateDevice(rendering_platform.vk_physical_device, &logical_create_info, 0, &(rendering_platform.vk_device)) != VK_SUCCESS)
    {
        printf("Failed to create logical device in vulkan!\n");
        return false;
    }
    
    vkGetDeviceQueue(rendering_platform.vk_device, graphics_family_index, 0, &(rendering_platform.vk_graphics_queue));
    vkGetDeviceQueue(rendering_platform.vk_device, present_family_index, 0, &(rendering_platform.vk_present_queue));
    
    if(!vk_pick_swapchain_settings(surface))
    {
        printf("Swapchain could not find a compatible configuration!\n");
    }
    
    
    printf("Succesfully initialized logical device and queues!\n");
    
    rendering_platform.vk_logical_device_initialized = true;
    return true;
}

VkExtent2D vk_pick_swapchain_extent(VkSurfaceKHR surface, int window_width, int window_height)
{
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(rendering_platform.vk_physical_device, surface, &capabilities);
    
    VkExtent2D picked_extent;
    picked_extent.width = window_width;
    picked_extent.height = window_height;
    
    if(picked_extent.width > capabilities.maxImageExtent.width)
    {
        picked_extent.width = capabilities.maxImageExtent.width;
    }
    if(picked_extent.width < capabilities.minImageExtent.width)
    {
        picked_extent.width = capabilities.minImageExtent.width;
    }

    if(picked_extent.height > capabilities.maxImageExtent.height)
    {
        picked_extent.height = capabilities.maxImageExtent.height;
    }
    if(picked_extent.height < capabilities.minImageExtent.height)
    {
        picked_extent.height = capabilities.minImageExtent.height;
    }
    
    return picked_extent;
} 

bool vk_create_swapchain(VkSurfaceKHR surface, int window_width, int widow_height, VkSwapchainKHR* swapchain)
{
    assert(swapchain);
    VkSwapchainCreateInfoKHR create_info = rendering_platform.vk_swapchain_settings;
    
    create_info.surface = surface;
    
    create_info.imageExtent = vk_pick_swapchain_extent(surface, window_width, widow_height);
    
    if(vkCreateSwapchainKHR(rendering_platform.vk_device, &create_info, 0, swapchain) != VK_SUCCESS)
    {
        return false;
    }    
    
    return true;
}

bool vk_create_image_view(VkImage image, VkFormat format, VkImageAspectFlags aspect_flags, VkImageView* target_view)
{
    VkImageViewCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    create_info.image = image;
    create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    create_info.format = format;
    create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.subresourceRange.aspectMask = aspect_flags;
    create_info.subresourceRange.baseMipLevel = 0;
    create_info.subresourceRange.levelCount = 1;
    create_info.subresourceRange.baseArrayLayer = 0;
    create_info.subresourceRange.layerCount = 1;
    
    if(vkCreateImageView(rendering_platform.vk_device, &create_info, 0, target_view) != VK_SUCCESS)
    {
        return false;
    }
    return true;
}

LinkedPointer* vk_create_swapchain_image_views(VkSwapchainKHR swapchain)
{
    uint32_t image_count;
    vkGetSwapchainImagesKHR(rendering_platform.vk_device, swapchain, &image_count, nullptr);
    
    void* allocated_space = AllocScratch((image_count + 1)*sizeof(VkImage));
    VkImage* swapchain_images = (VkImage*)align_mem(allocated_space, VkImage);
    
    vkGetSwapchainImagesKHR(rendering_platform.vk_device, swapchain, &image_count, swapchain_images);
    
    LinkedPointer* prev = {};
    LinkedPointer* first = {};
    
    for(int i = 0; i < image_count; i++)
    {
        VkImageView* created = (VkImageView*)Alloc(rendering_platform.vk_swapchain_image_views, sizeof(VkImageView));
        LinkedPointer* curr = (LinkedPointer*)Alloc(rendering_platform.vk_pointer_arrays, sizeof(LinkedPointer), zero());
        
        curr->data = created;
        
        if(!first)
        {
            first = curr;
        }
        
        if(prev)
        {
            prev->next = curr;
        }
        prev = curr;
        
        if(!vk_create_image_view(swapchain_images[i], rendering_platform.vk_swapchain_settings.imageFormat, VK_IMAGE_ASPECT_COLOR_BIT, created))
        {
            printf("Failed to create image view!\n");
        }
        /*
        VkImageViewCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        create_info.image = swapchain_images[i];
        create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        create_info.format = rendering_platform.vk_swapchain_settings.imageFormat;
        create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        create_info.subresourceRange.baseMipLevel = 0;
        create_info.subresourceRange.levelCount = 1;
        create_info.subresourceRange.baseArrayLayer = 0;
        create_info.subresourceRange.layerCount = 1;
        
        if(vkCreateImageView(rendering_platform.vk_device, &create_info, 0, created) != VK_SUCCESS)
        {
            printf("Failed to create image view!\n");
        }
        */
        
    }
    
    DeAllocScratch(allocated_space);
    
    return first;
}

//LinkedPointer* vk_create_frame_buffers(LinkedPointer* first_swapchain_image_view, int window_width, int window_height, VkImageView window_depth_view)
LinkedPointer* vk_create_frame_buffers(PlatformWindow* window)
{
    LinkedPointer* curr_image_view = window->vk_first_image_view;
    
    
    LinkedPointer* prev_added = {};
    LinkedPointer* first_added = {};
    while(curr_image_view)
    {
        VkFramebuffer* created = (VkFramebuffer*)Alloc(rendering_platform.vk_framebuffers, sizeof(VkFramebuffer));
        LinkedPointer* curr_added = (LinkedPointer*)Alloc(rendering_platform.vk_pointer_arrays, sizeof(LinkedPointer));
        
        curr_added->data = created;
        
        if(!first_added)
        {
            first_added = curr_added;
        }
        
        if(prev_added)
        {
            prev_added->next = curr_added;
        }
        prev_added = curr_added;
        
        VkImageView attachments[3];
        if(rendering_platform.vk_supported_optionals.RENDERER_MSAA)
        {
            attachments[0] = window->vk_msaa_image_view;
            attachments[1] = window->vk_depth_image_view;
            attachments[2] = *((VkImageView*)curr_image_view->data);     
        }
        else
        {
            attachments[0] = *((VkImageView*)curr_image_view->data); 
            attachments[1] = window->vk_depth_image_view; 
        }
    
        VkFramebufferCreateInfo framebuffer_info = {};
        
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = rendering_platform.vk_main_render_pass;
        framebuffer_info.attachmentCount = sizeof(attachments)/sizeof(VkImageView);
        
        // Note(Leo): excludes MSAA view if it isnt supported
        if(!rendering_platform.vk_supported_optionals.RENDERER_MSAA)
        {
            framebuffer_info.attachmentCount -= 1;
        }
        
        framebuffer_info.pAttachments = attachments;
        framebuffer_info.width = window->width;
        framebuffer_info.height = window->height;
        framebuffer_info.layers = 1;
        
        if(vkCreateFramebuffer(rendering_platform.vk_device, &framebuffer_info, 0, created) != VK_SUCCESS)
        {
            printf("Failed to create framebuffer!\n");
        }
        curr_image_view = curr_image_view->next;
    }
    
    return first_added;
}

uint32_t vk_find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memory_properties;
    vkGetPhysicalDeviceMemoryProperties(rendering_platform.vk_physical_device, &memory_properties);
    
    for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++)
    {
        if ((type_filter & (1 << i)) && (memory_properties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    
    assert(0);
    printf("Unable to find sutaible memory type with desired properties!\n");
    return 0;
}

bool vk_format_has_stencil(VkFormat format)
{
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

bool vk_pick_depth_format(VkFormat* target)
{
    const VkFormat candidates[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};
    int candidate_count = sizeof(candidates)/sizeof(VkFormat);
    
    for(int i = 0; i < candidate_count; i++)
    {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(rendering_platform.vk_physical_device, candidates[i], &properties);
        
        if(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            *target = candidates[i];
            return true;
        }
    }
    
    return false;
}

bool vk_create_depth_image(PlatformWindow* window)
{
    VkFormat picked_format;
    if(!vk_pick_depth_format(&picked_format))
    {
        printf("No supported format found for depth buffer!\n");
        return false;
    }
    
    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = window->width;
    image_info.extent.height = window->height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = picked_format;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if(rendering_platform.vk_supported_optionals.RENDERER_MSAA)
    {
        image_info.samples = rendering_platform.vk_supported_optionals.MSAA_SAMPLES;
    }
    else
    {
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    }
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if(vkCreateImage(rendering_platform.vk_device, &image_info, 0, &(window->vk_window_depth_image)) != VK_SUCCESS)
    {
        printf("Couldnt allocate depth buffer image memory!\n");
        return false;
    }

    VkMemoryRequirements memory_requirements;
    vkGetImageMemoryRequirements(rendering_platform.vk_device, window->vk_window_depth_image, &memory_requirements);
    
    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = memory_requirements.size;
    alloc_info.memoryTypeIndex = vk_find_memory_type(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if(vkAllocateMemory(rendering_platform.vk_device, &alloc_info, 0, &(window->vk_depth_image_memory)) != VK_SUCCESS)
    {
        return false;
    }

    vkBindImageMemory(rendering_platform.vk_device, window->vk_window_depth_image, window->vk_depth_image_memory, 0);
    
    if(!vk_create_image_view(window->vk_window_depth_image, picked_format, VK_IMAGE_ASPECT_DEPTH_BIT, &(window->vk_depth_image_view)))
    {
        printf("Failed to create image view for depth buffer!\n");
        return false;
    }
    
    return true;
}

bool vk_create_msaa_image(PlatformWindow* window)
{
    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = window->width;
    image_info.extent.height = window->height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = rendering_platform.vk_swapchain_settings.imageFormat;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    image_info.samples = rendering_platform.vk_supported_optionals.MSAA_SAMPLES;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if(vkCreateImage(rendering_platform.vk_device, &image_info, 0, &(window->vk_window_msaa_image)) != VK_SUCCESS)
    {
        printf("Couldnt allocate msaa image memory!\n");
        return false;
    }
    
    VkMemoryRequirements memory_requirements;
    vkGetImageMemoryRequirements(rendering_platform.vk_device, window->vk_window_msaa_image, &memory_requirements);
    
    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = memory_requirements.size;
    alloc_info.memoryTypeIndex = vk_find_memory_type(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if(vkAllocateMemory(rendering_platform.vk_device, &alloc_info, 0, &(window->vk_msaa_image_memory)) != VK_SUCCESS)
    {
        return false;
    }

    vkBindImageMemory(rendering_platform.vk_device, window->vk_window_msaa_image, window->vk_msaa_image_memory, 0);
    
    if(!vk_create_image_view(window->vk_window_msaa_image, rendering_platform.vk_swapchain_settings.imageFormat, VK_IMAGE_ASPECT_COLOR_BIT, &(window->vk_msaa_image_view)))
    {
        printf("Failed to create image view for depth buffer!\n");
        return false;
    }
    
    return true;
}

bool vk_create_shader_module(void* binary, int binary_length, VkShaderModule* created)
{
    VkShaderModuleCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = binary_length;
    create_info.pCode = (const uint32_t*)binary;
    
    if (vkCreateShaderModule(rendering_platform.vk_device, &create_info, 0, created) != VK_SUCCESS)
    {
        return false;
    }
    
    return true;
}

bool vk_create_render_pass(VkRenderPass* render_pass)
{
    VkAttachmentDescription color_attachment = {};
    color_attachment.format = rendering_platform.vk_swapchain_settings.imageFormat;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    
    if(rendering_platform.vk_supported_optionals.RENDERER_MSAA)
    {
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_attachment.samples = rendering_platform.vk_supported_optionals.MSAA_SAMPLES;
        color_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    else
    {
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
        
    VkAttachmentReference color_attachment_ref = {};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription color_resolve_attachment = {};
    if(rendering_platform.vk_supported_optionals.RENDERER_MSAA)
    {
        color_resolve_attachment.format = rendering_platform.vk_swapchain_settings.imageFormat;
        color_resolve_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        color_resolve_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_resolve_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_resolve_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_resolve_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_resolve_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color_resolve_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
    
    VkAttachmentReference color_resolve_ref = {};
    color_resolve_ref.attachment = 2;
    color_resolve_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    VkFormat depth_format;
    if(!vk_pick_depth_format(&depth_format))
    {
        return false;
    }
    
    VkAttachmentDescription depth_attatchment = {};
    depth_attatchment.format = depth_format;
    
    if(rendering_platform.vk_supported_optionals.RENDERER_MSAA)
    {
        depth_attatchment.samples = rendering_platform.vk_supported_optionals.MSAA_SAMPLES;
    }
    else
    {
        depth_attatchment.samples = VK_SAMPLE_COUNT_1_BIT;
    }
    
    depth_attatchment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attatchment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attatchment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attatchment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attatchment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attatchment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_attatchment_ref = {};
    depth_attatchment_ref.attachment = 1;
    depth_attatchment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;
    subpass.pDepthStencilAttachment = &depth_attatchment_ref;
    if(rendering_platform.vk_supported_optionals.RENDERER_MSAA)
    {
        subpass.pResolveAttachments = &color_resolve_ref;
    }
    
    VkAttachmentDescription attatchments[] = {color_attachment,  depth_attatchment, color_resolve_attachment};
    VkRenderPassCreateInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = sizeof(attatchments)/sizeof(VkAttachmentDescription);
    
    // Note(Leo): Keeps the array the same but excludes the MSAA attatchment
    if(!rendering_platform.vk_supported_optionals.RENDERER_MSAA)
    {
        render_pass_info.attachmentCount -= 1;
    }

    render_pass_info.pAttachments = attatchments;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;
    
    if(vkCreateRenderPass(rendering_platform.vk_device, &render_pass_info, nullptr, render_pass) != VK_SUCCESS)
    {
        return false;   
    }
    
    return true;
}

bool vk_create_transparent_graphics_pipeline(void* vert_shader_bin, int vert_bin_length, void* frag_shader_bin, int frag_bin_length)
{
    VkShaderModule vert_shader_module;
    VkShaderModule frag_shader_module;
    
    if(!vk_create_shader_module(vert_shader_bin, vert_bin_length, &vert_shader_module))
    {
        printf("Failed to create vertex shader module!\n");
        return false;
    }
    if(!vk_create_shader_module(frag_shader_bin, frag_bin_length, &frag_shader_module))
    {
        printf("Failed to create fragment shader module!\n");
        return false;
    }
    
    VkPipelineShaderStageCreateInfo vert_shader_stage_info = {};
    vert_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_shader_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_shader_stage_info.module = vert_shader_module;
    vert_shader_stage_info.pName = "main";
    
    VkPipelineShaderStageCreateInfo frag_shader_stage_info = {};
    frag_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_shader_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_shader_stage_info.module = frag_shader_module;
    frag_shader_stage_info.pName = "main";
    
    VkPipelineShaderStageCreateInfo shader_stages[] = {vert_shader_stage_info, frag_shader_stage_info};
    
    VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic_state = {};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = (uint32_t)sizeof(dynamic_states)/sizeof(VkDynamicState);
    dynamic_state.pDynamicStates = dynamic_states;
    
    VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    
    VkVertexInputBindingDescription input_binding_description = vk_get_binding_description(vertex());
    VkVertexInputBindingDescription instance_binding_description = vk_get_binding_description(transparent_instance());
    
    VkVertexInputBindingDescription input_binding_descriptions[] = { input_binding_description, instance_binding_description };
    
    vertex_input_info.pVertexBindingDescriptions = input_binding_descriptions;
    vertex_input_info.vertexBindingDescriptionCount = sizeof(input_binding_descriptions)/sizeof(VkVertexInputBindingDescription);
    
    int attribute_description_count = 0;
    vertex_input_info.pVertexAttributeDescriptions = vk_get_attribute_descriptions(transparent_instance(), &attribute_description_count);
    vertex_input_info.vertexAttributeDescriptionCount = (uint32_t)attribute_description_count;
    
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;
    
    VkPipelineViewportStateCreateInfo viewport_state = {};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;
    
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    if(rendering_platform.vk_supported_optionals.RENDERER_MSAA)
    {
        multisampling.rasterizationSamples = rendering_platform.vk_supported_optionals.MSAA_SAMPLES;
    }
    else
    {
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    }
    if(rendering_platform.vk_supported_optionals.RENDERER_SAMPLE_SHADING)
    {
        multisampling.sampleShadingEnable = VK_TRUE;
        multisampling.minSampleShading = 0.2f;
    }
    else
    {
        multisampling.sampleShadingEnable = VK_FALSE;
    }
    
    VkPipelineColorBlendAttachmentState color_blend_attachment = {};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_TRUE;
    color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    
    VkPipelineColorBlendStateCreateInfo color_blending = {};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.logicOp = VK_LOGIC_OP_COPY; 
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;
    color_blending.blendConstants[0] = 0.0f;
    color_blending.blendConstants[1] = 0.0f;
    color_blending.blendConstants[2] = 0.0f;
    color_blending.blendConstants[3] = 0.0f;
    
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_FALSE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depth_stencil.depthBoundsTestEnable = VK_FALSE;
    depth_stencil.maxDepthBounds = 1.0f; 
    depth_stencil.minDepthBounds = 0.0f; 
    depth_stencil.stencilTestEnable = VK_FALSE;
    depth_stencil.front = {}; 
    depth_stencil.back = {};
    
    // Note(Leo): This should be initialized by the opaque pipeline!
    /*
    VkDescriptorSetLayoutBinding ubo_layout_binding = {};
    ubo_layout_binding.binding = 0;
    ubo_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo_layout_binding.descriptorCount = 1;
    ubo_layout_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    ubo_layout_binding.pImmutableSamplers = 0;

    VkDescriptorSetLayoutBinding ubo_bindings[] = { ubo_layout_binding };

    VkDescriptorSetLayoutCreateInfo ubo_descriptor_set_create_info = {};
    ubo_descriptor_set_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ubo_descriptor_set_create_info.bindingCount = (uint32_t)sizeof(ubo_bindings)/sizeof(VkDescriptorSetLayoutBinding);
    ubo_descriptor_set_create_info.pBindings = ubo_bindings;
    
    if(vkCreateDescriptorSetLayout(rendering_platform.vk_device, &ubo_descriptor_set_create_info, 0, &(rendering_platform.vk_uniform_descriptor_layout)) != VK_SUCCESS)
    {
        return false;
    }
    */
    
    VkDescriptorSetLayoutBinding sampler_layout_binding = {};
    sampler_layout_binding.binding = 0;
    sampler_layout_binding.descriptorCount = 1;
    sampler_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    sampler_layout_binding.pImmutableSamplers = 0;
    sampler_layout_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding images_layout_binding = {};
    images_layout_binding.binding = 1;
    images_layout_binding.descriptorCount = MAX_TEXTURE_COUNT;
    images_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    images_layout_binding.pImmutableSamplers = 0;
    images_layout_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutBinding image_bindings[] = { sampler_layout_binding, images_layout_binding };
    
    VkDescriptorSetLayoutCreateInfo images_descriptor_set_create_info = {};
    images_descriptor_set_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    images_descriptor_set_create_info.bindingCount = (uint32_t)sizeof(image_bindings)/sizeof(VkDescriptorSetLayoutBinding);
    images_descriptor_set_create_info.pBindings = image_bindings;
    
    if(vkCreateDescriptorSetLayout(rendering_platform.vk_device, &images_descriptor_set_create_info, 0, &(rendering_platform.vk_image_descriptor_layout)) != VK_SUCCESS)
    {
        return false;
    }
    
    VkDescriptorSetLayout set_layouts[] = { rendering_platform.vk_uniform_descriptor_layout, rendering_platform.vk_image_descriptor_layout };
    
    VkPipelineLayoutCreateInfo pipeline_layout_info = {};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = sizeof(set_layouts)/sizeof(VkDescriptorSetLayout); 
    pipeline_layout_info.pSetLayouts = set_layouts; 
    pipeline_layout_info.pushConstantRangeCount = 0;
    pipeline_layout_info.pPushConstantRanges = 0;

    if(vkCreatePipelineLayout(rendering_platform.vk_device, &pipeline_layout_info, 0, &(rendering_platform.vk_transparent_graphics_pipeline_layout)) != VK_SUCCESS)
    {
        printf("Failed to create pipeline layout!\n");
        return false;
    }
    
    VkGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = rendering_platform.vk_transparent_graphics_pipeline_layout;
    pipeline_info.renderPass = rendering_platform.vk_main_render_pass;
    pipeline_info.subpass = 0;
    
    if(vkCreateGraphicsPipelines(rendering_platform.vk_device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &rendering_platform.vk_transparent_graphics_pipeline) != VK_SUCCESS)
    {
        printf("Failed on creating pipeline!\n");
        return false;
    }
    
    return true;
}

bool vk_create_opaque_graphics_pipeline(void* vert_shader_bin, int vert_bin_length, void* frag_shader_bin, int frag_bin_length)
{
    VkShaderModule vert_shader_module;
    VkShaderModule frag_shader_module;
    
    if(!vk_create_shader_module(vert_shader_bin, vert_bin_length, &vert_shader_module))
    {
        printf("Failed to create vertex shader module!\n");
        return false;
    }
    if(!vk_create_shader_module(frag_shader_bin, frag_bin_length, &frag_shader_module))
    {
        printf("Failed to create fragment shader module!\n");
        return false;
    }
    
    VkPipelineShaderStageCreateInfo vert_shader_stage_info = {};
    vert_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_shader_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_shader_stage_info.module = vert_shader_module;
    vert_shader_stage_info.pName = "main";
    
    VkPipelineShaderStageCreateInfo frag_shader_stage_info = {};
    frag_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_shader_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_shader_stage_info.module = frag_shader_module;
    frag_shader_stage_info.pName = "main";
    
    VkPipelineShaderStageCreateInfo shader_stages[] = {vert_shader_stage_info, frag_shader_stage_info};
    
    VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic_state = {};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = (uint32_t)sizeof(dynamic_states)/sizeof(VkDynamicState);
    dynamic_state.pDynamicStates = dynamic_states;
    
    VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    
    VkVertexInputBindingDescription vertex_binding_description = vk_get_binding_description(vertex());
    VkVertexInputBindingDescription instance_binding_description = vk_get_binding_description(opaque_instance());
    
    VkVertexInputBindingDescription input_binding_descriptions[] = { vertex_binding_description, instance_binding_description };
    
    vertex_input_info.pVertexBindingDescriptions = input_binding_descriptions;
    vertex_input_info.vertexBindingDescriptionCount = sizeof(input_binding_descriptions)/sizeof(VkVertexInputBindingDescription);
    
    int attribute_description_count = 0;
    vertex_input_info.pVertexAttributeDescriptions = vk_get_attribute_descriptions(opaque_instance(), &attribute_description_count);
    vertex_input_info.vertexAttributeDescriptionCount = (uint32_t)attribute_description_count;
    
    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;
    
    VkPipelineViewportStateCreateInfo viewport_state = {};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;
    
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    if(rendering_platform.vk_supported_optionals.RENDERER_MSAA)
    {
        multisampling.rasterizationSamples = rendering_platform.vk_supported_optionals.MSAA_SAMPLES;
    }
    else
    {
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    }
    if(rendering_platform.vk_supported_optionals.RENDERER_SAMPLE_SHADING)
    {
        multisampling.sampleShadingEnable = VK_TRUE;
        multisampling.minSampleShading = 0.2f;
    }
    else
    {
        multisampling.sampleShadingEnable = VK_FALSE;
    }
    
    VkPipelineColorBlendAttachmentState color_blend_attachment = {};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_TRUE;
    color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    
    VkPipelineColorBlendStateCreateInfo color_blending = {};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.logicOp = VK_LOGIC_OP_COPY; 
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;
    color_blending.blendConstants[0] = 0.0f;
    color_blending.blendConstants[1] = 0.0f;
    color_blending.blendConstants[2] = 0.0f;
    color_blending.blendConstants[3] = 0.0f;
    
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = VK_TRUE;
    depth_stencil.depthWriteEnable = VK_TRUE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depth_stencil.depthBoundsTestEnable = VK_FALSE;
    depth_stencil.maxDepthBounds = 1.0f; 
    depth_stencil.minDepthBounds = 0.0f; 
    depth_stencil.stencilTestEnable = VK_FALSE;
    depth_stencil.front = {}; 
    depth_stencil.back = {};
    
    VkDescriptorSetLayoutBinding ubo_layout_binding = {};
    ubo_layout_binding.binding = 0;
    ubo_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo_layout_binding.descriptorCount = 1;
    ubo_layout_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    ubo_layout_binding.pImmutableSamplers = 0;

    VkDescriptorSetLayoutBinding ubo_bindings[] = { ubo_layout_binding };

    VkDescriptorSetLayoutCreateInfo ubo_descriptor_set_create_info = {};
    ubo_descriptor_set_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ubo_descriptor_set_create_info.bindingCount = (uint32_t)sizeof(ubo_bindings)/sizeof(VkDescriptorSetLayoutBinding);
    ubo_descriptor_set_create_info.pBindings = ubo_bindings;
    
    if(vkCreateDescriptorSetLayout(rendering_platform.vk_device, &ubo_descriptor_set_create_info, 0, &(rendering_platform.vk_uniform_descriptor_layout)) != VK_SUCCESS)
    {
        return false;
    }
        
    VkDescriptorSetLayout set_layouts[] = { rendering_platform.vk_uniform_descriptor_layout };
    
    VkPipelineLayoutCreateInfo pipeline_layout_info = {};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = sizeof(set_layouts)/sizeof(VkDescriptorSetLayout); 
    pipeline_layout_info.pSetLayouts = set_layouts; 
    pipeline_layout_info.pushConstantRangeCount = 0;
    pipeline_layout_info.pPushConstantRanges = 0;

    if(vkCreatePipelineLayout(rendering_platform.vk_device, &pipeline_layout_info, 0, &(rendering_platform.vk_opaque_graphics_pipeline_layout)) != VK_SUCCESS)
    {
        printf("Failed to create pipeline layout!\n");
        return false;
    }
            
    VkGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = rendering_platform.vk_opaque_graphics_pipeline_layout;
    pipeline_info.renderPass = rendering_platform.vk_main_render_pass;
    pipeline_info.subpass = 0;
    
    if(vkCreateGraphicsPipelines(rendering_platform.vk_device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &rendering_platform.vk_opaque_graphics_pipeline) != VK_SUCCESS)
    {
        printf("Failed on creating pipeline!\n");
        return false;
    }
    
    return true;
}

bool vk_create_command_pools()
{
    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    //pool_info.queueFamilyIndex = rendering_platform.vk_present_queue_family;
    pool_info.queueFamilyIndex = rendering_platform.vk_graphics_queue_family;
    
    if(vkCreateCommandPool(rendering_platform.vk_device, &pool_info, 0, &rendering_platform.vk_main_command_pool) != VK_SUCCESS)
    {
        return false;
    }
    
    pool_info = {};
    
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = rendering_platform.vk_graphics_queue_family;
    
    if(vkCreateCommandPool(rendering_platform.vk_device, &pool_info, 0, &rendering_platform.vk_transient_command_pool) != VK_SUCCESS)
    {
        return false;
    }
    
    return true;
}

bool vk_create_descriptor_pool()
{
    VkDescriptorPoolSize ubo_pool_size = {};
    ubo_pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo_pool_size.descriptorCount = MAX_WINDOW_COUNT;
    
    VkDescriptorPoolSize sampler_pool_size = {};
    sampler_pool_size.type = VK_DESCRIPTOR_TYPE_SAMPLER;
    sampler_pool_size.descriptorCount = 2; // Note(Leo): + 1 to stop vulkan complaining that theres 0 space left.
    
    VkDescriptorPoolSize images_pool_size = {};
    images_pool_size.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    images_pool_size.descriptorCount = MAX_TEXTURE_COUNT;
    
    VkDescriptorPoolSize pool_sizes[] = { ubo_pool_size, sampler_pool_size, images_pool_size};
    
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = sizeof(pool_sizes)/sizeof(VkDescriptorPoolSize);
    pool_info.pPoolSizes = pool_sizes;
    pool_info.maxSets = MAX_WINDOW_COUNT + MAX_TEXTURE_COUNT + 1; // Note(Leo): +1 for sampler set?
    
    if(vkCreateDescriptorPool(rendering_platform.vk_device, &pool_info, 0, &(rendering_platform.vk_main_descriptor_pool)) != VK_SUCCESS)
    {
        return false;
    }
    
    return true;
}

bool vk_initialize_image_descriptor()
{
    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = rendering_platform.vk_main_descriptor_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &(rendering_platform.vk_image_descriptor_layout);
    
    if(vkAllocateDescriptorSets(rendering_platform.vk_device, &alloc_info, &(rendering_platform.vk_image_descriptors)) != VK_SUCCESS)
    {
        return false;
    }
    
    VkDescriptorImageInfo sampler_info = {};
    sampler_info.sampler = rendering_platform.vk_main_image_sampler;
    
    VkWriteDescriptorSet sampler_descriptor_write = {};
    sampler_descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    sampler_descriptor_write.dstSet = rendering_platform.vk_image_descriptors;
    sampler_descriptor_write.dstBinding = 0;
    sampler_descriptor_write.dstArrayElement = 0;
    sampler_descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    sampler_descriptor_write.descriptorCount = 1;
    sampler_descriptor_write.pImageInfo = &sampler_info;
    
    vkUpdateDescriptorSets(rendering_platform.vk_device, 1, &sampler_descriptor_write, 0, 0);
    
    return true;
}   

bool vk_create_image_descriptor(VkDescriptorSet target, VkImageView target_view, int image_index)
{
    VkDescriptorImageInfo image_info = {};
    image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_info.imageView = target_view;
    image_info.sampler = 0;
    
    VkWriteDescriptorSet image_descriptor_write = {};
    image_descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    image_descriptor_write.dstSet = target;
    image_descriptor_write.dstBinding = 1;
    image_descriptor_write.dstArrayElement = image_index;
    image_descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    image_descriptor_write.descriptorCount = 1;
    image_descriptor_write.pImageInfo = &image_info;
    
    vkUpdateDescriptorSets(rendering_platform.vk_device, 1, &image_descriptor_write, 0, 0);
    
    return true;
}

bool vk_create_uniform_descriptor(VkDescriptorSet* target, VkBuffer uniform_buffer)
{
    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = rendering_platform.vk_main_descriptor_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &(rendering_platform.vk_uniform_descriptor_layout);
    
    if(vkAllocateDescriptorSets(rendering_platform.vk_device, &alloc_info, target) != VK_SUCCESS)
    {
        return false;
    }
    
    VkDescriptorBufferInfo buffer_info = {};
    buffer_info.buffer = uniform_buffer;
    buffer_info.offset = 0;
    buffer_info.range = sizeof(UniformBufferObject);
    
    VkWriteDescriptorSet ubo_descriptor_write = {};
    ubo_descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ubo_descriptor_write.dstSet = *target;
    ubo_descriptor_write.dstBinding = 0;
    ubo_descriptor_write.dstArrayElement = 0;
    ubo_descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo_descriptor_write.descriptorCount = 1;    
    ubo_descriptor_write.pBufferInfo = &buffer_info;
    ubo_descriptor_write.pImageInfo = 0;
    ubo_descriptor_write.pTexelBufferView = 0;
    
    vkUpdateDescriptorSets(rendering_platform.vk_device, 1, &ubo_descriptor_write, 0, 0);
    
    return true;
}

bool vk_create_command_buffer(VkCommandBuffer* target)
{
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = rendering_platform.vk_main_command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;
    
    if (vkAllocateCommandBuffers(rendering_platform.vk_device, &alloc_info, target) != VK_SUCCESS)
    {
        return false;    
    }
    
    return true;
}

bool vk_create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer* target_buffer, VkDeviceMemory* target_physical_memory)
{
    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = size;
    buffer_info.usage = usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if(vkCreateBuffer(rendering_platform.vk_device, &buffer_info, nullptr, target_buffer) != VK_SUCCESS)
    {
        return false;
    }
    
    VkMemoryRequirements memory_requirements;
    vkGetBufferMemoryRequirements(rendering_platform.vk_device, *target_buffer, &memory_requirements);
    
    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = memory_requirements.size;
    alloc_info.memoryTypeIndex = vk_find_memory_type(memory_requirements.memoryTypeBits, properties);
    
    if(vkAllocateMemory(rendering_platform.vk_device, &alloc_info, nullptr, target_physical_memory) != VK_SUCCESS)
    {
        return false;
    }
    
    if(vkBindBufferMemory(rendering_platform.vk_device, *target_buffer, *target_physical_memory, 0) != VK_SUCCESS)
    {
        return false;
    }
    
    return true;
}

bool vk_record_command_buffer(VkCommandBuffer buffer, PlatformWindow* window, int image_index, int index_count, int opaque_instance_count, int opaque_instance_offset, int transparent_instance_count, int transparent_instance_offset)
{
    VkCommandBufferBeginInfo begin_recording_info = {};
    begin_recording_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_recording_info.flags = 0; 
    begin_recording_info.pInheritanceInfo = 0;
    
    if (vkBeginCommandBuffer(buffer, &begin_recording_info) != VK_SUCCESS)
    {
        return false;
    }
    
    VkFramebuffer used_frame_buffer = {};
    LinkedPointer* curr = window->vk_first_framebuffer;
    for(int i = 0; i < image_index; i++)
    {
        curr = curr->next;
        assert(curr);
    }
    used_frame_buffer = *((VkFramebuffer*)curr->data);
    
    VkRenderPassBeginInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = rendering_platform.vk_main_render_pass;
    render_pass_info.framebuffer = used_frame_buffer;
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = {(uint32_t)window->width, (uint32_t)window->height};
    
    VkClearValue clear_color = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    VkClearValue depth_clear = {};
    depth_clear.depthStencil = {1.0f, 0};
    
    VkClearValue clear_values[] = {clear_color, depth_clear};
    
    render_pass_info.clearValueCount = sizeof(clear_values)/sizeof(depth_clear);
    render_pass_info.pClearValues = clear_values;
    
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)window->width;
    viewport.height = (float)window->height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    
    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = {(uint32_t)window->width, (uint32_t)window->height};
    
    vkCmdBeginRenderPass(buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
    
    // Note(Leo): Opaque pass
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rendering_platform.vk_opaque_graphics_pipeline);
    
    vkCmdSetViewport(buffer, 0, 1, &viewport);
    vkCmdSetScissor(buffer, 0, 1, &scissor);

    VkBuffer vertex_buffers[] = { window->vk_window_vertex_buffer };
    VkDeviceSize opaque_offsets[] = { 0 };
    // Vertices
    // Note(Leo): Both pipelines share vertices since they draw the same shape
    vkCmdBindVertexBuffers(buffer, 0, 1, vertex_buffers, opaque_offsets);
    
    VkDeviceSize opaque_instance_offsets[] = { (uint64_t)opaque_instance_offset };
    // Instances
    vkCmdBindVertexBuffers(buffer, 1, 1, vertex_buffers, opaque_instance_offsets);
    
    // Note(Leo): Both pipelines share indices since they draw the same shape 
    vkCmdBindIndexBuffer(buffer, window->vk_window_index_buffer, 0, VK_INDEX_TYPE_UINT16);
    
    VkDescriptorSet opaque_descriptor_sets[1] = { window->vk_uniform_descriptor };
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rendering_platform.vk_opaque_graphics_pipeline_layout, 0, 1, opaque_descriptor_sets, 0, 0);
    vkCmdDrawIndexed(buffer, index_count, opaque_instance_count, 0, 0, 0);

    // Note(Leo): Transparent pass    
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rendering_platform.vk_transparent_graphics_pipeline);
    
    vkCmdSetViewport(buffer, 0, 1, &viewport);
    vkCmdSetScissor(buffer, 0, 1, &scissor);

    VkDeviceSize transparent_offsets[] = { 0 };
    // Vertices
    vkCmdBindVertexBuffers(buffer, 0, 1, vertex_buffers, transparent_offsets);
    
    VkDeviceSize transparent_instance_offsets[] = { (uint64_t)transparent_instance_offset };
    // Instances
    vkCmdBindVertexBuffers(buffer, 1, 1, vertex_buffers, transparent_instance_offsets);
    
    vkCmdBindIndexBuffer(buffer, window->vk_window_index_buffer, 0, VK_INDEX_TYPE_UINT16);
    
    VkDescriptorSet transparent_descriptor_sets[2] = { window->vk_uniform_descriptor, rendering_platform.vk_image_descriptors };
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rendering_platform.vk_transparent_graphics_pipeline_layout, 0, 2, transparent_descriptor_sets, 0, 0);
    vkCmdDrawIndexed(buffer, index_count, transparent_instance_count, 0, 0, 0);
    
    vkCmdEndRenderPass(buffer);
    
    if(vkEndCommandBuffer(buffer) != VK_SUCCESS)
    {
        return false;
    }
    
    return true;
}

void* vk_read_shader_bin(FILE* bin, int* len)
{
    int file_length;
    fseek(bin, 0, SEEK_END);    
    file_length = ftell(bin);
    rewind(bin);
    
    printf("Found file length of %d", file_length);
    
    void* allocated_space = Alloc(rendering_platform.vk_binary_data, file_length, no_zero());
    fread(allocated_space, file_length, 1, bin);
    *len = file_length;
    return allocated_space;
}

void vk_destroy_swapchain(VkSwapchainKHR swapchain)
{
    vkDestroySwapchainKHR(rendering_platform.vk_device, swapchain, nullptr);
}

void vk_destroy_swapchain_image_views(LinkedPointer* first_image_view)
{
    LinkedPointer* last = {};
    LinkedPointer* curr = first_image_view;
    
    curr = first_image_view;
    while(curr)
    {
        vkDestroyImageView(rendering_platform.vk_device, *((VkImageView*)curr->data), nullptr);
        DeAlloc(rendering_platform.vk_swapchain_image_views, curr->data);
        
        last = curr;
        curr = curr->next;
    }
    
    // Note(Leo): This relies on next being the first element of linked pointer and similiarly in freeblock!
    ((FreeBlock*)last)->next_free = rendering_platform.vk_pointer_arrays->first_free.next_free;
    rendering_platform.vk_pointer_arrays->first_free.next_free = (FreeBlock*)first_image_view;
}

void vk_destroy_swapchain_frame_buffers(LinkedPointer* first_frame_buffer)
{
    LinkedPointer* last = {};
    LinkedPointer* curr = first_frame_buffer;
    
    curr = first_frame_buffer;
    while(curr)
    {
        vkDestroyFramebuffer(rendering_platform.vk_device, *((VkFramebuffer*)curr->data), nullptr);
        DeAlloc(rendering_platform.vk_framebuffers, curr->data);
        
        last = curr;
        curr = curr->next;
    }
    
    // Note(Leo): This relies on next being the first element of linked pointer and similiarly in freeblock!
    ((FreeBlock*)last)->next_free = rendering_platform.vk_pointer_arrays->first_free.next_free;
    rendering_platform.vk_pointer_arrays->first_free.next_free = (FreeBlock*)first_frame_buffer;
}

bool vk_check_msaa_support(VkPhysicalDevice checked_device, VkSampleCountFlagBits* msaa_level_target)
{
    VkPhysicalDeviceProperties support_properties;
    vkGetPhysicalDeviceProperties(checked_device, &support_properties);
    VkSampleCountFlags supported_sample_count = support_properties.limits.framebufferColorSampleCounts & support_properties.limits.framebufferDepthSampleCounts;
    
    // MSAA Supported
    if(supported_sample_count & VK_SAMPLE_COUNT_64_BIT) { *msaa_level_target = VK_SAMPLE_COUNT_64_BIT; return true; }
    if(supported_sample_count & VK_SAMPLE_COUNT_32_BIT) { *msaa_level_target = VK_SAMPLE_COUNT_32_BIT; return true; }
    if(supported_sample_count & VK_SAMPLE_COUNT_16_BIT) { *msaa_level_target = VK_SAMPLE_COUNT_16_BIT; return true; }
    if(supported_sample_count & VK_SAMPLE_COUNT_8_BIT) { *msaa_level_target = VK_SAMPLE_COUNT_8_BIT; return true; }
    if(supported_sample_count & VK_SAMPLE_COUNT_4_BIT) { *msaa_level_target = VK_SAMPLE_COUNT_4_BIT; return true; }
    if(supported_sample_count & VK_SAMPLE_COUNT_2_BIT) { *msaa_level_target = VK_SAMPLE_COUNT_2_BIT; return true; }

    // MSAA not supported
    *msaa_level_target = VK_SAMPLE_COUNT_1_BIT;
    return false;
}

int InitializeVulkan(Arena* master_arena, const char** required_extension_names, int required_extension_count, FILE* opaque_vert_shader, FILE* opaque_frag_shader, FILE* transparent_vert_shader, FILE* transparent_frag_shader, int image_buffer_size)
{
    uint32_t extension_count;
    vkEnumerateInstanceExtensionProperties(NULL, &extension_count, NULL);
    
    printf("Found %d vulkan extensions\n", extension_count);
    
    // Note(Leo) + 1 since allignment can move the array over an entire element in worst case
    void* allocated_space = AllocScratch((extension_count + 1)*sizeof(VkExtensionProperties));
    VkExtensionProperties* supported_extensions = (VkExtensionProperties*)align_mem(allocated_space, VkExtensionProperties);
    
    vkEnumerateInstanceExtensionProperties(NULL, &extension_count, supported_extensions);
    
    VulkanSupportedExtensions extension_support = {};
    for(int i = 0; i < extension_count; i++)
    {
        printf("Supported extension: %s\n", supported_extensions[i].extensionName);
        // Note(Leo): This is really really ugly BUT glfw does it so idc
        if(strcmp(supported_extensions[i].extensionName, VK_E_KHR_SURFACE_NAME) == 0)
            extension_support.VK_E_KHR_SURFACE = true;
        else if(strcmp(supported_extensions[i].extensionName, VK_E_KHR_WIN32_SURFACE_NAME) == 0)
            extension_support.VK_E_KHR_WIN32_SURFACE = true;
        else if(strcmp(supported_extensions[i].extensionName, VK_E_KHR_MACOS_SURFACE_NAME) == 0)
            extension_support.VK_E_KHR_MACOS_SURFACE = true;
        else if(strcmp(supported_extensions[i].extensionName, VK_E_KHR_METAL_SURFACE_NAME) == 0)
            extension_support.VK_E_KHR_METAL_SURFACE = true;
        else if(strcmp(supported_extensions[i].extensionName, VK_E_KHR_XLIB_SURFACE_NAME) == 0)
            extension_support.VK_E_KHR_XLIB_SURFACE = true;
        else if(strcmp(supported_extensions[i].extensionName, VK_E_KHR_WAYLAND_SURFACE_NAME) == 0)
            extension_support.VK_E_KHR_WAYLAND_SURFACE = true;
    }
    DeAllocScratch(allocated_space);
    
    
    // Note(Leo): We just send the given list of extensions to VulkanCreateInstance since we cant run the program if they arent supported anyway
    /* 
    // See if we can support the platform
    if(!vk_extensions_supported(extension_support, required_extensions))
    {
        return 1;
    }
    */
    
    rendering_platform = {};
    rendering_platform.vk_master_arena = (Arena*)Alloc(master_arena, sizeof(Arena), zero());
    *(rendering_platform.vk_master_arena) = CreateArena(100*sizeof(Arena), sizeof(Arena));
    
    
    VkApplicationInfo app_info = {};
    
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = VULKAN_APPLICATION_NAME;
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "No Engine";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;
    
    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    
    create_info.enabledExtensionCount = required_extension_count;
    create_info.ppEnabledExtensionNames = required_extension_names;
    #if VK_USE_VALIDATION 
        create_info.enabledLayerCount = sizeof(validation_layers)/sizeof(char**);
        create_info.ppEnabledLayerNames = validation_layers;
    #else
        create_info.enabledLayerCount = 0;
    #endif
    
    
    VkResult result = vkCreateInstance(&create_info, 0, &rendering_platform.vk_instance);
    
    if(result != VK_SUCCESS)
    {
        printf("Failed to initialize vulkan!\n");
        return 1;
    }    
    
    printf("Succesfully initialized vulkan!\n");
    
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(rendering_platform.vk_instance, &device_count, 0);
    
    if(!device_count)
    {
        printf("No Vulkan compatible gpu's present!\n");
        return 1;
    }
    
    // Note(Leo): +1 to fit alignment
    allocated_space = AllocScratch((device_count + 1)*sizeof(VkPhysicalDevice));
    VkPhysicalDevice* physical_devices = align_mem(allocated_space, VkPhysicalDevice);
    
    vkEnumeratePhysicalDevices(rendering_platform.vk_instance, &device_count, physical_devices);    
    
    VkPhysicalDevice* chosen_physical_device = vk_pick_physical_device(physical_devices, device_count);
    if(!chosen_physical_device)
    {
        printf("No vulkan compatible gpus met the application's requirements!\n");
        return 1;
    }
    
    rendering_platform.vk_physical_device = *chosen_physical_device;
    
    VkPhysicalDeviceFeatures supported_features;
    vkGetPhysicalDeviceFeatures(rendering_platform.vk_physical_device, &supported_features);
    if(supported_features.samplerAnisotropy)
    {
        rendering_platform.vk_supported_optionals.SAMPLER_ANISOTROPY = true;
    }

    if(supported_features.sampleRateShading)
    {
        rendering_platform.vk_supported_optionals.RENDERER_SAMPLE_SHADING = true;
    }
    
    VkSampleCountFlagBits supported_msaa_level;
    if(vk_check_msaa_support(rendering_platform.vk_physical_device, &supported_msaa_level))
    {
        rendering_platform.vk_supported_optionals.RENDERER_MSAA = true;
        rendering_platform.vk_supported_optionals.MSAA_SAMPLES = supported_msaa_level;
    }

    
    DeAllocScratch(allocated_space);
    
    rendering_platform.vk_pointer_arrays = (Arena*)Alloc(rendering_platform.vk_master_arena, sizeof(Arena), zero());
    *(rendering_platform.vk_pointer_arrays) = CreateArena(10000*sizeof(LinkedPointer), sizeof(LinkedPointer));
    
    rendering_platform.vk_swapchain_image_views = (Arena*)Alloc(rendering_platform.vk_master_arena, sizeof(Arena), zero());
    *(rendering_platform.vk_swapchain_image_views) = CreateArena(10000*sizeof(VkImageView), sizeof(VkImageView));
    
    rendering_platform.vk_binary_data = (Arena*)Alloc(rendering_platform.vk_master_arena, sizeof(Arena), zero());
    *(rendering_platform.vk_binary_data) = CreateArena(10000000*sizeof(char), sizeof(char));
    
    rendering_platform.vk_framebuffers = (Arena*)Alloc(rendering_platform.vk_master_arena, sizeof(Arena), zero());
    *(rendering_platform.vk_framebuffers) = CreateArena(1000*sizeof(VkFramebuffer), sizeof(VkFramebuffer));
    
    rendering_platform.vk_image_handles = (Arena*)Alloc(rendering_platform.vk_master_arena, sizeof(Arena), zero());
    *(rendering_platform.vk_image_handles) = CreateArena(1000*sizeof(LoadedImageHandle), sizeof(LoadedImageHandle));
    
    
    rendering_platform.vk_opaque_shader.vert_shader_bin = vk_read_shader_bin(opaque_vert_shader, &rendering_platform.vk_opaque_shader.vert_shader_length);
    rendering_platform.vk_opaque_shader.frag_shader_bin = vk_read_shader_bin(opaque_frag_shader, &rendering_platform.vk_opaque_shader.frag_shader_length);
    
    rendering_platform.vk_transparent_shader.vert_shader_bin = vk_read_shader_bin(transparent_vert_shader, &rendering_platform.vk_transparent_shader.vert_shader_length);
    rendering_platform.vk_transparent_shader.frag_shader_bin = vk_read_shader_bin(transparent_frag_shader, &rendering_platform.vk_transparent_shader.frag_shader_length);
    
    rendering_platform.vk_image_buffer_size = image_buffer_size;
    
    return 0;   
}

bool vk_create_sync_objects(PlatformWindow* window)
{
    VkSemaphoreCreateInfo semaphore_info = {};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    
    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    // Note(Leo): Create the fence signalled so renderWindow doesnt get stuck waiting. 
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    
    if(vkCreateSemaphore(rendering_platform.vk_device, &semaphore_info, 0, &(window->vk_image_available_semaphore)) != VK_SUCCESS)
    {
        return false;
    }
    if(vkCreateSemaphore(rendering_platform.vk_device, &semaphore_info, 0, &(window->vk_render_finished_semaphore)) != VK_SUCCESS)
    {
        return false;
    }
    if(vkCreateFence(rendering_platform.vk_device, &fence_info, 0, &(window->vk_in_flight_fence)) != VK_SUCCESS)
    {
        return false;
    }
    
    return true;
}

bool vk_copy_buffer(VkBuffer src_buffer, VkBuffer dst_buffer, VkDeviceSize size, int src_offset = 0, int dst_offset = 0)
{
    VkCommandBufferAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = rendering_platform.vk_transient_command_pool;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer temp_command_buffer;
    if(vkAllocateCommandBuffers(rendering_platform.vk_device, &alloc_info, &temp_command_buffer) != VK_SUCCESS)
    {
        return false;
    }
    
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    if(vkBeginCommandBuffer(temp_command_buffer, &begin_info) != VK_SUCCESS)
    {
        return false;
    }
    
    VkBufferCopy copy_region = {};
    copy_region.srcOffset = src_offset; 
    copy_region.dstOffset = dst_offset; 
    copy_region.size = size;
    vkCmdCopyBuffer(temp_command_buffer, src_buffer, dst_buffer, 1, &copy_region);
    
    vkEndCommandBuffer(temp_command_buffer);
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &temp_command_buffer;
    
    if(vkQueueSubmit(rendering_platform.vk_graphics_queue, 1, &submit_info, VK_NULL_HANDLE) != VK_SUCCESS)
    {
        return false;
    }
    
    vkQueueWaitIdle(rendering_platform.vk_graphics_queue);
    
    vkFreeCommandBuffers(rendering_platform.vk_device, rendering_platform.vk_transient_command_pool, 1, &temp_command_buffer);
    return true;
}

bool vk_create_staging_buffer(int staging_size, PlatformWindow* window)
{
    if(!vk_create_buffer(staging_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &(window->vk_window_staging_buffer), &(window->vk_staging_memory)))
    {
        return false;
    }
    
    if(vkMapMemory(rendering_platform.vk_device, window->vk_staging_memory, 0, staging_size, 0, &(window->vk_staging_mapped_address)) != VK_SUCCESS)
    {
        return false;
    }
    
    return true;
}

bool vk_create_vertex_buffer_with_index(int size_vertices, int size_indexes, PlatformWindow* window)
{
    // Note(Leo): +2 to leave space for alignment
    VkDeviceSize vertex_buffer_size = sizeof(vertex) * (size_vertices + 2);
    VkDeviceSize index_buffer_size = sizeof(uint16_t) * (size_indexes + 1);
    
    window->vk_vertex_staging_buffer_size = vertex_buffer_size;
    window->vk_index_staging_buffer_size = index_buffer_size;
    
    if(!vk_create_staging_buffer(vertex_buffer_size + index_buffer_size, window))
    {
        return false;
    }
    
    if(!vk_create_buffer(vertex_buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &(window->vk_window_vertex_buffer), &(window->vk_vertex_memory)))
    {
        return false;
    }

    if(!vk_create_buffer(index_buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &(window->vk_window_index_buffer), &(window->vk_index_memory)))
    {
        return false;
    }

    return true;
}

bool vk_create_image_sampler(VkSampler* image_sampler)
{
    VkSamplerCreateInfo sampler_info = {};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    
    if(rendering_platform.vk_supported_optionals.SAMPLER_ANISOTROPY)
    {
        sampler_info.anisotropyEnable = VK_TRUE;
        
        VkPhysicalDeviceProperties properties = {};
        vkGetPhysicalDeviceProperties(rendering_platform.vk_physical_device, &properties);
        
        sampler_info.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
    }
    else
    {
        sampler_info.anisotropyEnable = VK_FALSE;
        sampler_info.maxAnisotropy = 1.0f;
    }
    
    sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    sampler_info.compareEnable = VK_FALSE;
    sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.mipLodBias = 0.0f;
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = 0.0f;
    
    if(vkCreateSampler(rendering_platform.vk_device, &sampler_info, 0, image_sampler) != VK_SUCCESS)
    {
        return false;
    }
    
    return true;
}

bool vk_create_uniform_buffer(PlatformWindow* window)
{
    VkDeviceSize buffer_size = sizeof(UniformBufferObject);
    
    if(!vk_create_buffer(buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &(window->vk_window_uniform_buffer), &(window->vk_uniform_memory)))
    {
        return false;
    }
    
    if(vkMapMemory(rendering_platform.vk_device, window->vk_uniform_memory, 0, buffer_size, 0, &(window->vk_uniform_mapped_address)) != VK_SUCCESS)
    {
        return false;
    }
    
    return true;
}


// Note(Leo): This currently is getting called when we have an image to use for getmemrequirements so the image buffer cannot 
// be used until the first  image is created.
bool vk_create_image_buffer(VkImage target_image)
{
    VkMemoryRequirements memory_requirements;
    vkGetImageMemoryRequirements(rendering_platform.vk_device, target_image, &memory_requirements);
    
    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = rendering_platform.vk_image_buffer_size;
    alloc_info.memoryTypeIndex = vk_find_memory_type(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if(vkAllocateMemory(rendering_platform.vk_device, &alloc_info, 0, &(rendering_platform.vk_image_memory)) != VK_SUCCESS)
    {
        printf("Failed to create image buffer!\n");
        return false;
    }

    return true;
}

bool vk_transition_image_layout(VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout)
{
    VkCommandBufferAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = rendering_platform.vk_transient_command_pool;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer temp_command_buffer;
    if(vkAllocateCommandBuffers(rendering_platform.vk_device, &alloc_info, &temp_command_buffer) != VK_SUCCESS)
    {
        return false;
    }
    
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    if(vkBeginCommandBuffer(temp_command_buffer, &begin_info) != VK_SUCCESS)
    {
        return false;
    }   

    VkImageMemoryBarrier image_barrier = {};
    image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_barrier.oldLayout = old_layout;
    image_barrier.newLayout = new_layout;
    image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    image_barrier.image = image;
    image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_barrier.subresourceRange.baseMipLevel = 0;
    image_barrier.subresourceRange.levelCount = 1;
    image_barrier.subresourceRange.baseArrayLayer = 0;
    image_barrier.subresourceRange.layerCount = 1;
    
    VkPipelineStageFlags source_flags;
    VkPipelineStageFlags destination_flags;
    
    if(old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        image_barrier.srcAccessMask = 0;
        image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
        source_flags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destination_flags = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if(old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        image_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        image_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
        source_flags = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destination_flags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else
    {
        printf("ERROR: Unsuported image transition!\n");
        return false;
    }
    
    vkCmdPipelineBarrier(temp_command_buffer, source_flags, destination_flags, 0, 0, 0, 0, 0, 1, &image_barrier);
    
    vkEndCommandBuffer(temp_command_buffer);
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &temp_command_buffer;
    
    if(vkQueueSubmit(rendering_platform.vk_graphics_queue, 1, &submit_info, VK_NULL_HANDLE) != VK_SUCCESS)
    {
        return false;
    }
    
    vkQueueWaitIdle(rendering_platform.vk_graphics_queue);
    
    vkFreeCommandBuffers(rendering_platform.vk_device, rendering_platform.vk_transient_command_pool, 1, &temp_command_buffer);
    
    return true;
}

bool vk_copy_buffer_to_image(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
{
    VkCommandBufferAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandPool = rendering_platform.vk_transient_command_pool;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer temp_command_buffer;
    if(vkAllocateCommandBuffers(rendering_platform.vk_device, &alloc_info, &temp_command_buffer) != VK_SUCCESS)
    {
        return false;
    }
    
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    if(vkBeginCommandBuffer(temp_command_buffer, &begin_info) != VK_SUCCESS)
    {
        return false;
    }
        
    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { width, height, 1 };
    
    vkCmdCopyBufferToImage(temp_command_buffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    vkEndCommandBuffer(temp_command_buffer);
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &temp_command_buffer;
    
    if(vkQueueSubmit(rendering_platform.vk_graphics_queue, 1, &submit_info, VK_NULL_HANDLE) != VK_SUCCESS)
    {
        return false;
    }
    
    vkQueueWaitIdle(rendering_platform.vk_graphics_queue);
    
    vkFreeCommandBuffers(rendering_platform.vk_device, rendering_platform.vk_transient_command_pool, 1, &temp_command_buffer);
    return true;
}

std::map<std::string, LoadedImageHandle*> loaded_image_map = {};

LoadedImageHandle* RenderplatformGetImage(const char* name)
{
    auto result = loaded_image_map.find(name);
    
    if(result != loaded_image_map.end())
    {
        return result->second;
    }
    
    return NULL;
}

void vk_init_empty_image()
{
    LoadedImageHandle* created_handle = (LoadedImageHandle*)Alloc(rendering_platform.vk_image_handles, sizeof(LoadedImageHandle));
    loaded_image_map.insert({ "vk_empty_image", created_handle });
    
    
    //Note(Leo): 4 bytes per pixel for RGBA, empty image is 1 pixel
    int image_size = 4;
    created_handle->image_size = image_size;
    
    VkImage created_image;
    
    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = (uint32_t)1;
    image_info.extent.height = (uint32_t)1;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    //image_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.flags = 0;
    
    if(vkCreateImage(rendering_platform.vk_device, &image_info, 0, &created_image) != VK_SUCCESS)
    {
        assert(0);
        return;
    }
    
    if(!rendering_platform.vk_image_buffer_initialized)
    {
        if(!vk_create_image_buffer(created_image))
        {
            return;
        }
    }
    
    VkBuffer temp_stage;
    VkDeviceMemory temp_stage_memory;
    
    if(!vk_create_buffer(image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &temp_stage, &temp_stage_memory))
    {
        return;
    }
    
    void* staging_data;
    vkMapMemory(rendering_platform.vk_device, temp_stage_memory, 0, image_size, 0, &staging_data);
    memset(staging_data, 0, image_size);
    vkUnmapMemory(rendering_platform.vk_device, temp_stage_memory);
    
    VkMemoryRequirements created_requirements = {};
    vkGetImageMemoryRequirements(rendering_platform.vk_device, created_image, &created_requirements);
    
    // Note(Leo): This is required in the windows version of the Vulkan spec, linux seems to work without it but it is better to leave it for both
    rendering_platform.vk_next_image_offset = align_offset_to(rendering_platform.vk_next_image_offset, created_requirements.alignment);
    
    vkBindImageMemory(rendering_platform.vk_device, created_image, rendering_platform.vk_image_memory, rendering_platform.vk_next_image_offset);
    created_handle->vk_image_memory_offset = rendering_platform.vk_next_image_offset; 
    
    rendering_platform.vk_next_image_offset += image_size;
    
    created_handle->vk_image_texture = created_image;
    
    //if(!vk_transition_image_layout(created_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
    if(!vk_transition_image_layout(created_image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
    {
        printf("Failed transitioning layout for image!\n");
        return;
    }
        
    if(!vk_copy_buffer_to_image(temp_stage, created_image, 1, 1))
    {
        return;
    }
    
    //if(!vk_transition_image_layout(created_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
    if(!vk_transition_image_layout(created_image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
    {
        printf("Failed transitioning layout for image!\n");
        return;
    }
    
    vkDestroyBuffer(rendering_platform.vk_device, temp_stage, 0);
    vkFreeMemory(rendering_platform.vk_device, temp_stage_memory, 0);
    
    VkImageViewCreateInfo image_view_info = {};
    image_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    image_view_info.image = created_handle->vk_image_texture;
    image_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    //image_view_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    image_view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_view_info.subresourceRange.baseMipLevel = 0;
    image_view_info.subresourceRange.levelCount = 1;
    image_view_info.subresourceRange.baseArrayLayer = 0;
    image_view_info.subresourceRange.layerCount = 1;

    if(vkCreateImageView(rendering_platform.vk_device, &image_view_info, 0, &(created_handle->vk_image_texture_view)) != VK_SUCCESS)
    {   
        printf("Failed to create imageview for image!\n");
        return;
    }
    
    if(rendering_platform.vk_next_image_index != 0)
    {
        printf("ERROR: Another image was initialized before the empty image!");
        assert(0);
    }
    /*
    if(!vk_create_image_descriptor(rendering_platform.vk_image_descriptors, created_handle->vk_image_texture_view, rendering_platform.vk_next_image_index))
    {
        printf("Failed to update image descriptor for image!\n");
        return;
    }
    */
    // Note(Leo): Initialize all image descriptors to point to the empty image (stops vulkan from complaining that they are uninitialized)
    for(int i = 0; i < MAX_TEXTURE_COUNT; i++)
    {
        if(!vk_create_image_descriptor(rendering_platform.vk_image_descriptors, created_handle->vk_image_texture_view, i))
        {
            printf("Failed to update image descriptor for image!\n");
            return;
        }
    }
    
    rendering_platform.vk_next_image_index++;
    
}

void RenderplatformLoadImage(FILE* image_file, const char* name)
{
    LoadedImageHandle* created_handle = (LoadedImageHandle*)Alloc(rendering_platform.vk_image_handles, sizeof(LoadedImageHandle));
    loaded_image_map.insert({ name, created_handle });
    
    assert(image_file);
    int image_width, image_height, image_channels;
    stbi_uc* image_pixels = stbi_load_from_file(image_file, &image_width, &image_height, &image_channels, STBI_rgb_alpha);
    
    if(!image_pixels)
    {
        printf("Image load failed with reason: %s\n\n\n", stbi_failure_reason());
        assert(image_pixels);
        return;
    }
    
    //Note(Leo): 4 bytes per pixel for RGBA
    int image_size = image_width * image_height * 4;
    
    created_handle->image_size = image_size;
    
    VkImage created_image;
    
    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = (uint32_t)image_width;
    image_info.extent.height = (uint32_t)image_height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    //image_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.flags = 0;
    
    if(vkCreateImage(rendering_platform.vk_device, &image_info, 0, &created_image) != VK_SUCCESS)
    {
        assert(0);
        return;
    }
    
    if(!rendering_platform.vk_image_buffer_initialized)
    {
        if(!vk_create_image_buffer(created_image))
        {
            return;
        }
    }
    
    VkBuffer temp_stage;
    VkDeviceMemory temp_stage_memory;
    
    if(!vk_create_buffer(image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &temp_stage, &temp_stage_memory))
    {
        return;
    }
    
    void* staging_data;
    vkMapMemory(rendering_platform.vk_device, temp_stage_memory, 0, image_size, 0, &staging_data);
    memcpy(staging_data, image_pixels, image_size);
    vkUnmapMemory(rendering_platform.vk_device, temp_stage_memory);
    
    stbi_image_free(image_pixels);
    
    VkMemoryRequirements created_requirements = {};
    vkGetImageMemoryRequirements(rendering_platform.vk_device, created_image, &created_requirements);
    
    // Note(Leo): This is required in the windows version of the Vulkan spec, linux seems to work without it but it is better to leave it for both
    rendering_platform.vk_next_image_offset = align_offset_to(rendering_platform.vk_next_image_offset, created_requirements.alignment);
    
    vkBindImageMemory(rendering_platform.vk_device, created_image, rendering_platform.vk_image_memory, rendering_platform.vk_next_image_offset);
    created_handle->vk_image_memory_offset = rendering_platform.vk_next_image_offset; 
    
    rendering_platform.vk_next_image_offset += image_size;
    
    created_handle->vk_image_texture = created_image;
    
    //if(!vk_transition_image_layout(created_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
    if(!vk_transition_image_layout(created_image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
    {
        printf("Failed transitioning layout for image!\n");
        return;
    }
        
    if(!vk_copy_buffer_to_image(temp_stage, created_image, (uint32_t)image_width, (uint32_t)image_height))
    {
        return;
    }
    
    //if(!vk_transition_image_layout(created_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
    if(!vk_transition_image_layout(created_image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
    {
        printf("Failed transitioning layout for image!\n");
        return;
    }
    
    vkDestroyBuffer(rendering_platform.vk_device, temp_stage, 0);
    vkFreeMemory(rendering_platform.vk_device, temp_stage_memory, 0);
    
    //if(!vk_create_image_view(created_handle->vk_image_texture, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, &(created_handle->vk_image_texture_view)))
    if(!vk_create_image_view(created_handle->vk_image_texture, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, &(created_handle->vk_image_texture_view)))
    {
        printf("Failed to create imageview for image!\n");
        return;
    }
    
    if(!vk_create_image_descriptor(rendering_platform.vk_image_descriptors, created_handle->vk_image_texture_view, rendering_platform.vk_next_image_index))
    {
        printf("Failed to update image descriptor for image!\n");
        return;
    }
    rendering_platform.vk_next_image_index++;
}

#define vk_copy_to_buffer_aligned(buffer_next, copy_src, alloc_size, align_type)  vk_copy_to_buffer_aligned_f(align_mem((void*)buffer_next, align_type), (void*)copy_src, alloc_size)

void* vk_copy_to_buffer_aligned_f(void* aligned_buffer_next, void* copy_src, int alloc_size)
{
    memcpy(aligned_buffer_next, copy_src, alloc_size);
    return aligned_buffer_next;
}

void RenderplatformDrawWindow(PlatformWindow* window)
{
    vkWaitForFences(rendering_platform.vk_device, 1, &(window->vk_in_flight_fence), VK_TRUE, UINT64_MAX);
    
    uint32_t image_index;
    // Note(Leo): != success indicates that the window has been resized (swapchains need to be recreated), return and wait for it to be caught by our main loop 
    VkResult surface_status = vkAcquireNextImageKHR(rendering_platform.vk_device, window->vk_window_swapchain, UINT64_MAX, window->vk_image_available_semaphore, VK_NULL_HANDLE, &image_index);
    if(surface_status != VK_SUCCESS)
    {
        //vk_window_resized(window);
        return;
    }
    
    vkResetFences(rendering_platform.vk_device, 1, &(window->vk_in_flight_fence));
    vkResetCommandBuffer(window->vk_command_buffer, 0);
    
    // Note(Leo): Quad
    const uint16_t shared_indices[] = { 0, 1, 2, 2, 3, 0 };
    
    const vertex shared_vertices[] = {
        {{0.0f, 0.0f}, {0.0f, 0.0f}},
        {{1.0f, 0.0f}, {1.0f, 0.0f}},
        {{1.0f, 1.0f},  {1.0f, 1.0f}},
        {{0.0f, 1.0f}, {0.0f, 1.0f}},
    };
    
    const transparent_instance temp_transparent_instances[] = {
        {{0.0f, -1.0f, 0.2f}, {0.2f, 0.2f, 0.5f, 0.5f}, {1.0f, 1.0f}, {0.1f, 0.0f, 0.0f, 0.0f}, { 1 } },
        {{-0.5f, -1.0f, 0.0f}, {0.2f, 0.2f, 0.5f, 0.5f}, {1.0f, 1.0f}, {0.1f, 0.0f, 0.0f, 0.0f}, { 0 } },
    };

    const opaque_instance temp_opaque_instances[] = {
        {{0.0f, 0.0f, 1.0f}, {0.2f, 0.2f, 0.5f}, {2.0f, 1.0f}, {0.1f, 0.0f, 0.0f, 0.0f}},  
        {{-1.0f, -1.0f, 1.0f}, {0.2f, 0.2f, 0.0f}, {1.0f, 2.0f}, {0.1f, 0.1f, 0.1f, 0.1f}},  
    };
    
    void* first_vertex_address = vk_copy_to_buffer_aligned(window->vk_staging_mapped_address, shared_vertices, sizeof(shared_vertices), vertex);
    int first_vertex_offset = offset_of(first_vertex_address , window->vk_staging_mapped_address);
    int shared_vertex_count = sizeof(shared_vertices)/sizeof(vertex);
    
    void* first_index_address = vk_copy_to_buffer_aligned(((vertex*)first_vertex_address + shared_vertex_count), shared_indices, sizeof(shared_indices), uint16_t);
    int first_index_offset = offset_of(first_index_address, window->vk_staging_mapped_address);
    int shared_index_count = sizeof(shared_indices)/sizeof(uint16_t);

    void* first_opaque_instance_address = vk_copy_to_buffer_aligned(((uint16_t*)first_index_address + shared_index_count), temp_opaque_instances, sizeof(temp_opaque_instances), opaque_instance);
    int first_opaque_instance_offset = offset_of(first_opaque_instance_address, window->vk_staging_mapped_address); 
    int temp_opaque_instance_count = sizeof(temp_opaque_instances)/sizeof(opaque_instance);
    
    void* first_transparent_instance_address = vk_copy_to_buffer_aligned(((opaque_instance*)first_opaque_instance_address + temp_opaque_instance_count), temp_transparent_instances, sizeof(temp_transparent_instances), transparent_instance);
    int first_transparent_instance_offset = offset_of(first_transparent_instance_address, window->vk_staging_mapped_address);
    int temp_transparent_instance_count = sizeof(temp_transparent_instances)/sizeof(transparent_instance);
    
    // Set uniform buffer transforms
    UniformBufferObject ubo = {};
    
    Identity(&(ubo.model));
    Identity(&(ubo.view));
    Identity(&(ubo.projection));
    
    // Note(Leo): Aspect ratio correction    
    //ubo.projection.m[0][0] = (float)window->height / (float)window->width;
    ubo.projection.m[1][1] = (float)window->width / (float)window->height;
    
    memcpy(window->vk_uniform_mapped_address, &ubo, sizeof(ubo));
    
    if(!vk_copy_buffer(window->vk_window_staging_buffer, window->vk_window_vertex_buffer, sizeof(shared_vertices), first_vertex_offset))
    {
        printf("ERROR: Failed while copying staging buffer to vertex buffer!");
    }
    
    int final_opaque_instance_offset = align_offset_to(sizeof(shared_vertices), alignof(opaque_instance));
    if(!vk_copy_buffer(window->vk_window_staging_buffer, window->vk_window_vertex_buffer, sizeof(temp_opaque_instances), first_opaque_instance_offset, final_opaque_instance_offset))
    {
        printf("ERROR: Failed while copying staging buffer to vertex buffer!");
    }
    
    int final_transparent_instance_offset = align_offset_to((final_opaque_instance_offset + sizeof(temp_opaque_instances)), alignof(transparent_instance));
    if(!vk_copy_buffer(window->vk_window_staging_buffer, window->vk_window_vertex_buffer, sizeof(temp_transparent_instances), first_transparent_instance_offset, final_transparent_instance_offset))
    {
        printf("ERROR: Failed while copying staging buffer to vertex buffer!");
    }
    
    if(!vk_copy_buffer(window->vk_window_staging_buffer, window->vk_window_index_buffer, sizeof(shared_indices), first_index_offset))
    {
        printf("ERROR: Failed while copying staging buffer to index buffer!");
    }
    
    if(!vk_record_command_buffer(window->vk_command_buffer, window, image_index, shared_index_count, temp_opaque_instance_count, final_opaque_instance_offset, temp_transparent_instance_count, final_transparent_instance_offset))
    {
        printf("ERROR: Couldnt record command buffer!\n");
    }
    
    VkSemaphore wait_semaphores[] = { window->vk_image_available_semaphore };
    VkSemaphore signal_semaphores[] = { window->vk_render_finished_semaphore };
    VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &(window->vk_command_buffer);
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores;
    
    if(vkQueueSubmit(rendering_platform.vk_graphics_queue, 1, &submit_info, window->vk_in_flight_fence) != VK_SUCCESS)
    {
        printf("ERROR: Couldnt submit draw command buffer!\n");
    }
    
    VkSwapchainKHR swapchains[] = { window->vk_window_swapchain };
    
    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_semaphores;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = swapchains;
    present_info.pImageIndices = &image_index;
    present_info.pResults = nullptr;
    
    VkResult que_status = vkQueuePresentKHR(rendering_platform.vk_present_queue, &present_info);
    if(que_status != VK_SUCCESS)
    {
        //vk_window_resized(window);
        return;
    }
}

bool RenderplatformSafeToDelete(PlatformWindow* window)
{
    if(vkGetFenceStatus(rendering_platform.vk_device, window->vk_in_flight_fence) != VK_SUCCESS)
    {
        return false;
    }
    
    return true;
}

void vk_window_resized(PlatformWindow* window)
{
    vkDeviceWaitIdle(rendering_platform.vk_device);
    
    vk_destroy_swapchain_frame_buffers(window->vk_first_framebuffer);
    vk_destroy_swapchain_image_views(window->vk_first_image_view);
    vk_destroy_swapchain(window->vk_window_swapchain);
    
    if(rendering_platform.vk_supported_optionals.RENDERER_MSAA)
    {
        vkDestroyImageView(rendering_platform.vk_device, window->vk_msaa_image_view, 0);
        vkDestroyImage(rendering_platform.vk_device, window->vk_window_msaa_image, 0);
        vkFreeMemory(rendering_platform.vk_device, window->vk_msaa_image_memory, 0);
    }  
    
    vkDestroyImageView(rendering_platform.vk_device, window->vk_depth_image_view, 0);
    vkDestroyImage(rendering_platform.vk_device, window->vk_window_depth_image, 0);
    vkFreeMemory(rendering_platform.vk_device, window->vk_depth_image_memory, 0);
    
    vkDestroySemaphore(rendering_platform.vk_device, window->vk_image_available_semaphore, nullptr);
    vkDestroySemaphore(rendering_platform.vk_device, window->vk_render_finished_semaphore, nullptr);
    vkDestroyFence(rendering_platform.vk_device, window->vk_in_flight_fence, nullptr);
    
    if(!vk_create_swapchain(window->vk_window_surface, window->width, window->height, &(window->vk_window_swapchain)))
    {
        printf("ERROR: Couldn't re-create swapchain!\n");
    }
    
    window->vk_first_image_view = vk_create_swapchain_image_views(window->vk_window_swapchain);
    
    if(rendering_platform.vk_supported_optionals.RENDERER_MSAA)
    {
        if(!vk_create_msaa_image(window))
        {
            printf("ERROR: Couldn't re-create window depth image!\n");
        } 
    }
    
    if(!vk_create_depth_image(window))
    {
        printf("ERROR: Couldn't re-create window depth image!\n");
    }
    
    //window->vk_first_framebuffer = vk_create_frame_buffers(window->vk_first_image_view, window->width, window->height, window->vk_depth_image_view);
    window->vk_first_framebuffer = vk_create_frame_buffers(window);
    
    vk_create_sync_objects(window);
}

void vk_destroy_window_surface(PlatformWindow* window)
{
    vkDeviceWaitIdle(rendering_platform.vk_device);
    
    vkDestroySemaphore(rendering_platform.vk_device, window->vk_image_available_semaphore, nullptr);
    vkDestroySemaphore(rendering_platform.vk_device, window->vk_render_finished_semaphore, nullptr);
    vkDestroyFence(rendering_platform.vk_device, window->vk_in_flight_fence, nullptr);

    vk_destroy_swapchain_frame_buffers(window->vk_first_framebuffer);
    vk_destroy_swapchain_image_views(window->vk_first_image_view);
    vk_destroy_swapchain(window->vk_window_swapchain);
    
    if(rendering_platform.vk_supported_optionals.RENDERER_MSAA)
    {
        vkDestroyImageView(rendering_platform.vk_device, window->vk_msaa_image_view, 0);
        vkDestroyImage(rendering_platform.vk_device, window->vk_window_msaa_image, 0);
        vkFreeMemory(rendering_platform.vk_device, window->vk_msaa_image_memory, 0);
    }  
    
    vkDestroyImageView(rendering_platform.vk_device, window->vk_depth_image_view, 0);
    vkDestroyImage(rendering_platform.vk_device, window->vk_window_depth_image, 0);
    vkFreeMemory(rendering_platform.vk_device, window->vk_depth_image_memory, 0);
    
    vkDestroySurfaceKHR(rendering_platform.vk_instance, window->vk_window_surface, 0);
}

#if defined(_WIN32) || defined(WIN32) || defined(WIN64) || defined(__CYGWIN__)
#include <windows.h>
void win32_vk_create_window_surface(PlatformWindow* window, HMODULE windows_module_handle)
{
    VkWin32SurfaceCreateInfoKHR surface_info = {};
    surface_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    
    surface_info.hwnd = window->window_handle;
    surface_info.hinstance = windows_module_handle;
    VkResult error;
    
    error = vkCreateWin32SurfaceKHR(rendering_platform.vk_instance, &surface_info, NULL, &(window->vk_window_surface));
    if(error)
    {
        window->vk_window_surface = {};
        printf("Error creating window surface!\n");
    }
    
    if(!rendering_platform.vk_logical_device_initialized)
    {
        if(!vk_initialize_logical_device(window->vk_window_surface, required_vk_device_extensions, sizeof(required_vk_device_extensions)/sizeof(char**)))
        {
            printf("Failed to init logical device!\n");
        }
    }
    
    if(!vk_create_swapchain(window->vk_window_surface, window->width, window->height, &window->vk_window_swapchain))
    {
        printf("Failed to create window swapchain\n");
    }
    
    window->vk_first_image_view = vk_create_swapchain_image_views(window->vk_window_swapchain);
    if(!window->vk_first_image_view)
    {
        printf("Failed to create image views for window swapchain!\n");
    }
    
    if(!rendering_platform.vk_graphics_pipeline_initialized)
    {
        if(!vk_create_render_pass(&rendering_platform.vk_main_render_pass))
        {
            printf("Failed to create renderpass!\n");
        }
        if(!vk_create_opaque_graphics_pipeline(rendering_platform.vk_opaque_shader.vert_shader_bin, rendering_platform.vk_opaque_shader.vert_shader_length, rendering_platform.vk_opaque_shader.frag_shader_bin, rendering_platform.vk_opaque_shader.frag_shader_length))
        {
            printf("Failed to initialize opaque graphics pipeline!");
        }
        if(!vk_create_transparent_graphics_pipeline(rendering_platform.vk_transparent_shader.vert_shader_bin, rendering_platform.vk_transparent_shader.vert_shader_length, rendering_platform.vk_transparent_shader.frag_shader_bin, rendering_platform.vk_transparent_shader.frag_shader_length))
        {
            printf("Failed to initialize transparent graphics pipeline!");
        }
        if(!vk_create_image_sampler(&(rendering_platform.vk_main_image_sampler)))
        {
            printf("Failed to initialize image sampler!\n");
        }
        if(!vk_create_command_pools())
        {
            printf("Failed to create command pool!\n");
        }
        if(!vk_create_command_buffer(&(window->vk_command_buffer)))
        {
            printf("Failed to create command buffer\n");
        }
        if(!vk_create_sync_objects(window))
        {
            printf("Failed to creat sync objects!\n");
        }
        if(!vk_create_descriptor_pool())
        {
            printf("Failed to create descriptor pool!\n");
        }
        if(!vk_initialize_image_descriptor())
        {
            printf("Failed to create image descriptor!\n");
        }
        vk_init_empty_image();
        
        rendering_platform.vk_graphics_pipeline_initialized = true;
    }
    
    if(rendering_platform.vk_supported_optionals.RENDERER_MSAA)
    {
        if(!vk_create_msaa_image(window))
        {
            printf("Failed to create color buffer for MSAA!");
        }
    }
    
    if(!vk_create_depth_image(window))
    {
        printf("Failed to create window depth buffer!\n");
    }
    
    //window->vk_first_framebuffer = vk_create_frame_buffers(window->vk_first_image_view, window->width, window->height, window->vk_depth_image_view);
    window->vk_first_framebuffer = vk_create_frame_buffers(window);
    
    if(!vk_create_vertex_buffer_with_index(100, 100, window))
    {
        printf("Failed to create vertex buffers!\n");
    }
    
    if(!vk_create_uniform_buffer(window))
    {
        printf("Failed to create uniform buffer!\n");
    }
    
    if(!vk_create_uniform_descriptor(&(window->vk_uniform_descriptor), window->vk_window_uniform_buffer))
    {
        printf("Failed to create a uniform descriptor!\n");
    }

}
#endif


#if defined(__linux__) && !defined(_WIN32)
#include <X11/Xlib.h>

void linux_vk_create_window_surface(PlatformWindow* window, Display* x_display)
{
    VkXlibSurfaceCreateInfoKHR surface_info = {};
    surface_info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    
    surface_info.dpy = x_display;
    surface_info.window = window->window_handle;
    VkResult error;
    
    error = vkCreateXlibSurfaceKHR(rendering_platform.vk_instance, &surface_info, NULL, &(window->vk_window_surface));
    
    if(error)
    {
        window->vk_window_surface = {};
        printf("Error creating window surface!\n");
    }
    
    if(!rendering_platform.vk_logical_device_initialized)
    {
        if(!vk_initialize_logical_device(window->vk_window_surface, required_vk_device_extensions, sizeof(required_vk_device_extensions)/sizeof(char**)))
        {
            printf("Failed to init logical device!\n");
        }
    }
    
    if(!vk_create_swapchain(window->vk_window_surface, window->width, window->height, &window->vk_window_swapchain))
    {
        printf("Failed to create window swapchain\n");
    }
    
    window->vk_first_image_view = vk_create_swapchain_image_views(window->vk_window_swapchain);
    if(!window->vk_first_image_view)
    {
        printf("Failed to create image views for window swapchain!\n");
    }
    
    if(!rendering_platform.vk_graphics_pipeline_initialized)
    {
        if(!vk_create_render_pass(&rendering_platform.vk_main_render_pass))
        {
            printf("Failed to create renderpass!\n");
        }
        if(!vk_create_opaque_graphics_pipeline(rendering_platform.vk_opaque_shader.vert_shader_bin, rendering_platform.vk_opaque_shader.vert_shader_length, rendering_platform.vk_opaque_shader.frag_shader_bin, rendering_platform.vk_opaque_shader.frag_shader_length))
        {
            printf("Failed to initialize opaque graphics pipeline!");
        }
        if(!vk_create_transparent_graphics_pipeline(rendering_platform.vk_transparent_shader.vert_shader_bin, rendering_platform.vk_transparent_shader.vert_shader_length, rendering_platform.vk_transparent_shader.frag_shader_bin, rendering_platform.vk_transparent_shader.frag_shader_length))
        {
            printf("Failed to initialize transparent graphics pipeline!");
        }
        else
        {
            printf("Succesfully initialized graphics pipeline!\n");
        }
        if(!vk_create_image_sampler(&(rendering_platform.vk_main_image_sampler)))
        {
            printf("Failed to initialize image sampler!\n");
        }
        if(!vk_create_command_pools())
        {
            printf("Failed to create command pool!\n");
        }
        if(!vk_create_command_buffer(&(window->vk_command_buffer)))
        {
            printf("Failed to create command buffer\n");
        }
        if(!vk_create_sync_objects(window))
        {
            printf("Failed to create sync objects!\n");
        }
        if(!vk_create_descriptor_pool())
        {
            printf("Failed to create descriptor pool!\n");
        }
        if(!vk_initialize_image_descriptor())
        {
            printf("Failed to create image descriptor!\n");
        }
        vk_init_empty_image();
        
        rendering_platform.vk_graphics_pipeline_initialized = true;
    }
    
    if(rendering_platform.vk_supported_optionals.RENDERER_MSAA)
    {
        if(!vk_create_msaa_image(window))
        {
            printf("Failed to create color buffer for MSAA!");
        }
    }
    
    if(!vk_create_depth_image(window))
    {
        printf("Failed to create window depth buffer!\n");
    }
    
    //window->vk_first_framebuffer = vk_create_frame_buffers(window->vk_first_image_view, window->width, window->height, window->vk_depth_image_view);
    window->vk_first_framebuffer = vk_create_frame_buffers(window);
    
    if(!vk_create_vertex_buffer_with_index(100, 100, window))
    {
        printf("Failed to create vertex buffers!\n");
    }
    
    if(!vk_create_uniform_buffer(window))
    {
        printf("Failed to create uniform buffer!\n");
    }
    
    if(!vk_create_uniform_descriptor(&(window->vk_uniform_descriptor), window->vk_window_uniform_buffer))
    {
        printf("Failed to create a uniform descriptor!\n");
    }
}
#endif
