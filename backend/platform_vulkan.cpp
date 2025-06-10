#if defined(_WIN32) || defined(WIN32) || defined(WIN64) || defined(__CYGWIN__)
#define VK_USE_PLATFORM_WIN32_KHR 1
#endif
#if defined(__linux__) && !defined(_WIN32)  && !defined(__ANDROID__)
#define VK_USE_PLATFORM_XLIB_KHR 1 
#endif
#if defined(__ANDROID__)
#define VK_USE_PLATFORM_ANDROID_KHR 1
#endif

#define VK_NO_PROTOTYPES 1
#define STB_IMAGE_IMPLEMENTATION 1
#include "third_party/stbi/stb_image.h"

#include <chrono>

#include <vulkan/vulkan.h>
#include "platform.h"
#include <stdio.h>
#include <cassert>
#include "file_system.h"
#include "graphics_types.h"

const char* required_vk_device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

#define IMAGE_TILE_SIZE 250 // Size of the tiles that images get split into
#define IMAGE_ATLAS_SIZE 1500 // # of tiles in the image atlas

#define MAX_RENDER_TILE_SIZE 64 

#define WINDOW_STAGING_SIZE Megabytes(10) // Size of each window's staging buffer
#define WINDOW_INPUT_SIZE Megabytes(10)

#ifdef NDEBUG
// Note(Leo): validation layers have/appear to have a memory leak (at least in task manager) so turn them off when investigating leak issues
#else
    #define VK_USE_VALIDATION 0
    const char* validation_layers[] = {"VK_LAYER_KHRONOS_validation"};
#endif

struct vk_shader
{
    void* shader_bin;
    int shader_length;
};

struct vk_atlas_texture 
{
    VkDescriptorSetLayout descriptor_layout;
    VkDescriptorSet descriptor_set;
    VkImage image;
    VkImageView image_view;
    VkDeviceMemory memory; 
    uvec3 dimensions;
};

enum class ScreenOrientation
{
    ZERO,
    NINETY,
    TWO_SEVENTY
};

struct VulkanRenderPlatform 
{
    VkLibrary vk_library;
    VkInstance vk_instance;
    VkPhysicalDevice vk_physical_device;
    bool vk_logical_device_initialized;
    
    VkDevice vk_device;
    VkQueue vk_compute_queue;
    VkQueue vk_present_queue;
    
    union
    {
        struct
        {
            uint32_t vk_compute_queue_family;
            uint32_t vk_present_queue_family;
        };
        uint32_t vk_queue_indeces[2];
    };
    
    VkSwapchainCreateInfoKHR vk_swapchain_settings;
    
    vk_shader vk_combined_shader;

    bool vk_graphics_pipeline_initialized;
    
    VkPipeline vk_combined_pipeline;
    VkPipelineLayout vk_combined_pipeline_layout;
    VkDescriptorSetLayout vk_combined_descriptor_layout;
    int32_t render_tile_size;
    
    VkCommandPool vk_main_command_pool;
    VkCommandPool vk_transient_command_pool;
    VkDescriptorPool vk_main_descriptor_pool;
    
    vk_atlas_texture vk_glyph_atlas;
    vk_atlas_texture vk_image_atlas;
    uint32_t image_tile_capacity;
    
    Arena* vk_master_arena;
    Arena* vk_swapchain_image_views;
    Arena* image_atlas_tiles;
    Arena* image_handles;
    Arena* vk_binary_data;
    
    #if PLATFORM_ANDROID
    ScreenOrientation orientation;
    #endif
};

VulkanRenderPlatform rendering_platform;

// Exported fn's
PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;

// Global fn's
PFN_vkCreateInstance vkCreateInstance;

PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties;

// Instance fn's
PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices;

PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties; 

PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties;

PFN_vkGetPhysicalDeviceFeatures vkGetPhysicalDeviceFeatures;

PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR;

PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR;

PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR;

PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR;

PFN_vkCreateDevice vkCreateDevice;

PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr;

PFN_vkGetPhysicalDeviceImageFormatProperties vkGetPhysicalDeviceImageFormatProperties;

PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties;

#if PLATFORM_LINUX
PFN_vkCreateXlibSurfaceKHR vkCreateXlibSurfaceKHR;
#elif PLATFORM_WINDOWS
PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR;
#elif PLATFORM_ANDROID
PFN_vkCreateAndroidSurfaceKHR vkCreateAndroidSurfaceKHR;
#endif

// Device fn's

PFN_vkGetDeviceQueue vkGetDeviceQueue;

PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR;

PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR;

PFN_vkCreateImageView vkCreateImageView;

PFN_vkCreateImage vkCreateImage;

PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout;

PFN_vkCreateCommandPool vkCreateCommandPool;

PFN_vkCreateDescriptorPool vkCreateDescriptorPool;

PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements;

PFN_vkAllocateMemory vkAllocateMemory;

PFN_vkBindImageMemory vkBindImageMemory;

PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers;

PFN_vkBeginCommandBuffer vkBeginCommandBuffer;

PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier;

PFN_vkEndCommandBuffer vkEndCommandBuffer;

PFN_vkQueueSubmit vkQueueSubmit;

PFN_vkQueueWaitIdle vkQueueWaitIdle;

PFN_vkFreeCommandBuffers vkFreeCommandBuffers;

PFN_vkAllocateDescriptorSets vkAllocateDescriptorSets;

PFN_vkUpdateDescriptorSets vkUpdateDescriptorSets;

PFN_vkCreateShaderModule vkCreateShaderModule;

PFN_vkCreatePipelineLayout vkCreatePipelineLayout;

PFN_vkCreateComputePipelines vkCreateComputePipelines;

PFN_vkCreateSemaphore vkCreateSemaphore;

PFN_vkCreateFence vkCreateFence;

PFN_vkCreateBuffer vkCreateBuffer;

PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements;

PFN_vkBindBufferMemory vkBindBufferMemory;

PFN_vkMapMemory vkMapMemory;

PFN_vkWaitForFences vkWaitForFences;

PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR;

PFN_vkResetFences vkResetFences;

PFN_vkResetCommandBuffer vkResetCommandBuffer;

PFN_vkCmdCopyBuffer vkCmdCopyBuffer;

PFN_vkCmdBindPipeline vkCmdBindPipeline;

PFN_vkCmdPushConstants vkCmdPushConstants;

PFN_vkCmdBindDescriptorSets vkCmdBindDescriptorSets;

PFN_vkCmdDispatch vkCmdDispatch;

PFN_vkQueuePresentKHR vkQueuePresentKHR;

PFN_vkUnmapMemory vkUnmapMemory;

PFN_vkDestroyBuffer vkDestroyBuffer;

PFN_vkFreeMemory vkFreeMemory;

PFN_vkCmdCopyBufferToImage vkCmdCopyBufferToImage;

PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR;

PFN_vkDestroyImageView vkDestroyImageView;

PFN_vkCreateSampler vkCreateSampler;

PFN_vkGetFenceStatus vkGetFenceStatus;

PFN_vkDeviceWaitIdle vkDeviceWaitIdle;

PFN_vkDestroySemaphore vkDestroySemaphore;

PFN_vkDestroyFence vkDestroyFence;

PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR;

bool vk_get_hook_address()
{
    #if PLATFORM_LINUX
        rendering_platform.vk_library = dlopen("libvulkan.so", RTLD_NOW);
        if(!rendering_platform.vk_library)
        {
            return false;
        }
    #elif PLATFORM_ANDROID
        rendering_platform.vk_library = dlopen("libvulkan.so", RTLD_NOW);
        if(!rendering_platform.vk_library)
        {
            return false;
        }
    #elif PLATFORM_WINDOWS
        rendering_platform.vk_library = (HMODULE)LoadLibrary(TEXT("vulkan-1.dll"));
        if(!rendering_platform.vk_library)
        {
            return false;
        }
    #endif

    vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)PlatformGetProcAddress(rendering_platform.vk_library, "vkGetInstanceProcAddr");
    if(!vkGetInstanceProcAddr)
    {
        return false;
    }
    
    return true;
}

bool vk_get_global_procs()
{
    #define VK_GLOBAL_LEVEL_FUNCTION( fun )                                                 \
    if( !(fun = (PFN_##fun)vkGetInstanceProcAddr( nullptr, #fun )) ) {                      \
        return false;                                                                       \
    }
    
    VK_GLOBAL_LEVEL_FUNCTION(vkCreateInstance);
    VK_GLOBAL_LEVEL_FUNCTION(vkEnumerateInstanceExtensionProperties);
    
    return true;
}

bool vk_get_instance_procs()
{
    #define VK_INSTANCE_LEVEL_FUNCTION( fun )                                                   \
    if( !(fun = (PFN_##fun)vkGetInstanceProcAddr( rendering_platform.vk_instance, #fun )) ) {   \
        return false;                                                                           \
    }
    
    VK_INSTANCE_LEVEL_FUNCTION(vkEnumeratePhysicalDevices);
    VK_INSTANCE_LEVEL_FUNCTION(vkGetPhysicalDeviceQueueFamilyProperties);
    VK_INSTANCE_LEVEL_FUNCTION(vkGetPhysicalDeviceProperties);
    VK_INSTANCE_LEVEL_FUNCTION(vkGetPhysicalDeviceFeatures);
    VK_INSTANCE_LEVEL_FUNCTION(vkGetPhysicalDeviceSurfaceSupportKHR);
    VK_INSTANCE_LEVEL_FUNCTION(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    VK_INSTANCE_LEVEL_FUNCTION(vkGetPhysicalDeviceSurfaceFormatsKHR);
    VK_INSTANCE_LEVEL_FUNCTION(vkGetPhysicalDeviceSurfacePresentModesKHR);
    VK_INSTANCE_LEVEL_FUNCTION(vkCreateDevice);
    VK_INSTANCE_LEVEL_FUNCTION(vkGetDeviceProcAddr);
    VK_INSTANCE_LEVEL_FUNCTION(vkGetPhysicalDeviceImageFormatProperties);
    VK_INSTANCE_LEVEL_FUNCTION(vkGetPhysicalDeviceMemoryProperties);
    VK_INSTANCE_LEVEL_FUNCTION(vkDestroySurfaceKHR);
    
    #if PLATFORM_LINUX
        VK_INSTANCE_LEVEL_FUNCTION(vkCreateXlibSurfaceKHR);
    #elif PLATFORM_ANDROID
        VK_INSTANCE_LEVEL_FUNCTION(vkCreateAndroidSurfaceKHR);
    #elif PLATFORM_WINDOWS
        VK_INSTANCE_LEVEL_FUNCTION(vkCreateWin32SurfaceKHR);
    #endif
    
    return true;
}

bool vk_get_device_procs()
{
    #define VK_DEVICE_LEVEL_FUNCTION( fun )                                                 \
    if( !(fun = (PFN_##fun)vkGetDeviceProcAddr( rendering_platform.vk_device, #fun )) ) {   \
        assert(0);\
        return false;                                                                       \
    }
    
    VK_DEVICE_LEVEL_FUNCTION(vkGetDeviceQueue);
    VK_DEVICE_LEVEL_FUNCTION(vkCreateSwapchainKHR);
    VK_DEVICE_LEVEL_FUNCTION(vkGetSwapchainImagesKHR);
    VK_DEVICE_LEVEL_FUNCTION(vkCreateImageView);
    VK_DEVICE_LEVEL_FUNCTION(vkCreateImage);
    VK_DEVICE_LEVEL_FUNCTION(vkCreateDescriptorSetLayout);
    VK_DEVICE_LEVEL_FUNCTION(vkCreateCommandPool);
    VK_DEVICE_LEVEL_FUNCTION(vkCreateDescriptorPool);
    VK_DEVICE_LEVEL_FUNCTION(vkGetImageMemoryRequirements);
    VK_DEVICE_LEVEL_FUNCTION(vkAllocateMemory);
    VK_DEVICE_LEVEL_FUNCTION(vkBindImageMemory);
    VK_DEVICE_LEVEL_FUNCTION(vkAllocateCommandBuffers);
    VK_DEVICE_LEVEL_FUNCTION(vkBeginCommandBuffer);
    VK_DEVICE_LEVEL_FUNCTION(vkCmdPipelineBarrier);
    VK_DEVICE_LEVEL_FUNCTION(vkEndCommandBuffer);
    VK_DEVICE_LEVEL_FUNCTION(vkQueueSubmit);
    VK_DEVICE_LEVEL_FUNCTION(vkQueueWaitIdle);
    VK_DEVICE_LEVEL_FUNCTION(vkFreeCommandBuffers);
    VK_DEVICE_LEVEL_FUNCTION(vkAllocateDescriptorSets);
    VK_DEVICE_LEVEL_FUNCTION(vkUpdateDescriptorSets);
    VK_DEVICE_LEVEL_FUNCTION(vkCreateShaderModule);
    VK_DEVICE_LEVEL_FUNCTION(vkCreatePipelineLayout);
    VK_DEVICE_LEVEL_FUNCTION(vkCreateComputePipelines);
    VK_DEVICE_LEVEL_FUNCTION(vkCreateSemaphore);
    VK_DEVICE_LEVEL_FUNCTION(vkCreateFence);
    VK_DEVICE_LEVEL_FUNCTION(vkCreateBuffer);
    VK_DEVICE_LEVEL_FUNCTION(vkGetBufferMemoryRequirements);
    VK_DEVICE_LEVEL_FUNCTION(vkBindBufferMemory);
    VK_DEVICE_LEVEL_FUNCTION(vkMapMemory);
    VK_DEVICE_LEVEL_FUNCTION(vkWaitForFences);
    VK_DEVICE_LEVEL_FUNCTION(vkAcquireNextImageKHR);
    VK_DEVICE_LEVEL_FUNCTION(vkResetFences);
    VK_DEVICE_LEVEL_FUNCTION(vkResetCommandBuffer);
    VK_DEVICE_LEVEL_FUNCTION(vkCmdCopyBuffer);
    VK_DEVICE_LEVEL_FUNCTION(vkCmdBindPipeline);
    VK_DEVICE_LEVEL_FUNCTION(vkCmdPushConstants);
    VK_DEVICE_LEVEL_FUNCTION(vkCmdBindDescriptorSets);
    VK_DEVICE_LEVEL_FUNCTION(vkCmdDispatch);
    VK_DEVICE_LEVEL_FUNCTION(vkQueuePresentKHR);
    VK_DEVICE_LEVEL_FUNCTION(vkUnmapMemory);
    VK_DEVICE_LEVEL_FUNCTION(vkDestroyBuffer);
    VK_DEVICE_LEVEL_FUNCTION(vkFreeMemory);
    VK_DEVICE_LEVEL_FUNCTION(vkCmdCopyBufferToImage);
    VK_DEVICE_LEVEL_FUNCTION(vkDestroySwapchainKHR);
    VK_DEVICE_LEVEL_FUNCTION(vkDestroyImageView);
    VK_DEVICE_LEVEL_FUNCTION(vkCreateSampler);
    VK_DEVICE_LEVEL_FUNCTION(vkGetFenceStatus);
    VK_DEVICE_LEVEL_FUNCTION(vkDeviceWaitIdle);
    VK_DEVICE_LEVEL_FUNCTION(vkDestroySemaphore);
    VK_DEVICE_LEVEL_FUNCTION(vkDestroyFence);
    
    return true;
}

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
    
    void* allocated_space = AllocScratch((queue_family_count + 1)*sizeof(VkQueueFamilyProperties), no_zero());
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
    
    void* allocated_space = AllocScratch((queue_family_count + 1)*sizeof(VkQueueFamilyProperties), no_zero());
    VkQueueFamilyProperties* supported_families = (VkQueueFamilyProperties*)align_mem(allocated_space, VkQueueFamilyProperties);
    
    vkGetPhysicalDeviceQueueFamilyProperties(*target_device, &queue_family_count, supported_families);
    
    bool found_avoided_family = false;
    
    for(int i = 0; i < queue_family_count; i++)
    {
        if((supported_families + i)->queueFlags & VK_QUEUE_COMPUTE_BIT)
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
        
        //printf("Evaluating physical device with name \"%s\"\n", device_properties.deviceName);
        
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
    
    void* allocated_space = AllocScratch((format_count + 1)*sizeof(VkSurfaceFormatKHR), no_zero());
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
    
    // Should have already verified that formats are non zero before calling this function
    assert(mode_count != 0);
    
    void* allocated_space = AllocScratch((mode_count + 1)*sizeof(VkPresentModeKHR), no_zero());
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
    // Note(Leo): Need the storage bit since we bind and directly write to the swapchain images inside our compute pipeline
    settings.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    settings.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    settings.presentMode = chosen_mode;
    settings.clipped = VK_TRUE;
    settings.oldSwapchain = VK_NULL_HANDLE;
    settings.preTransform = capabilities.currentTransform;
    
    // Note(Leo): share images between compute and presentation queue if they are seperate queues
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
    int compute_family_index;
    if(!vk_device_queues_supported(&(rendering_platform.vk_physical_device), -1, &compute_family_index))
    {
        printf("Failed to initialize compute queue!\n");
    }
    
    int present_family_index;
    if(!vk_present_supported(&(rendering_platform.vk_physical_device), surface, compute_family_index, &present_family_index))
    {
        printf("Failed to initialize presentation queue!\n");
    }
    
    // Queue arguments
    float queue_priority = 1.0f;
    
    VkDeviceQueueCreateInfo compute_queue_create_info = {};
    compute_queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    compute_queue_create_info.queueFamilyIndex = (uint32_t)compute_family_index;
    compute_queue_create_info.queueCount = 1;
    compute_queue_create_info.pQueuePriorities = &queue_priority;
    rendering_platform.vk_queue_indeces[0] = (uint32_t)compute_family_index;
    
    VkDeviceQueueCreateInfo present_queue_create_info = {};
    present_queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    present_queue_create_info.queueFamilyIndex = (uint32_t)present_family_index;
    present_queue_create_info.queueCount = 1;
    present_queue_create_info.pQueuePriorities = &queue_priority;
    rendering_platform.vk_queue_indeces[1] = (uint32_t)present_family_index;
    
    VkDeviceQueueCreateInfo queue_create_infos[2] = { compute_queue_create_info, present_queue_create_info };
    
    // Device arguments
    // Todo(Leo): Do we still need this now that we are using a compute pipeline?
    VkPhysicalDeviceFeatures device_features = {};
    device_features.geometryShader = VK_TRUE;
    
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
    
    if(!vk_get_device_procs())
    {
        return false;
    }
    
    vkGetDeviceQueue(rendering_platform.vk_device, compute_family_index, 0, &(rendering_platform.vk_compute_queue));
    vkGetDeviceQueue(rendering_platform.vk_device, present_family_index, 0, &(rendering_platform.vk_present_queue));
    
    if(!vk_pick_swapchain_settings(surface))
    {
        printf("Swapchain could not find a compatible configuration!\n");
    }
    
    
//    printf("Succesfully initialized logical device and queues!\n");
    
    rendering_platform.vk_logical_device_initialized = true;
    return true;
}

VkExtent2D vk_pick_swapchain_extent(VkSurfaceKHR surface, int window_width, int window_height)
{
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(rendering_platform.vk_physical_device, surface, &capabilities);
    
    VkExtent2D picked_extent;
    // Note(Leo): Swap width and height when the device wants to be rotated
    if(capabilities.currentTransform & VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR ||
        capabilities.currentTransform & VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR)
    {
        picked_extent.width = window_height;
        picked_extent.height = window_width;
        #if PLATFORM_ANDROID
        rendering_platform.orientation = capabilities.currentTransform == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR ? ScreenOrientation::NINETY : ScreenOrientation::TWO_SEVENTY;
        #endif
    }
    else
    {
        picked_extent.width = window_width;
        picked_extent.height = window_height;
        #if PLATFORM_ANDROID
        rendering_platform.orientation = ScreenOrientation::ZERO;
        #endif
    }
    
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
    
    #if PLATFORM_ANDROID
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(rendering_platform.vk_physical_device, surface, &capabilities);
    create_info.preTransform = capabilities.currentTransform;
    #endif
    
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

vk_swapchain_image* vk_create_swapchain_image_views(VkSwapchainKHR swapchain)
{
    uint32_t image_count;
    vkGetSwapchainImagesKHR(rendering_platform.vk_device, swapchain, &image_count, nullptr);
    
    void* allocated_space = AllocScratch((image_count + 1)*sizeof(VkImage), no_zero());
    VkImage* swapchain_images = (VkImage*)align_mem(allocated_space, VkImage);
    
    vkGetSwapchainImagesKHR(rendering_platform.vk_device, swapchain, &image_count, swapchain_images);
    
    vk_swapchain_image* curr = {};
    vk_swapchain_image* first = {};
    vk_swapchain_image* prev = {};
    
    for(int i = 0; i < image_count; i++)
    {
        curr = (vk_swapchain_image*)Alloc(rendering_platform.vk_swapchain_image_views, sizeof(vk_swapchain_image), zero());
        
        curr->image = swapchain_images[i];
        if(!vk_create_image_view(swapchain_images[i], rendering_platform.vk_swapchain_settings.imageFormat, VK_IMAGE_ASPECT_COLOR_BIT, &curr->image_view))
        {
            printf("Failed to create image view!\n");
        }
        
        if(!first)
        {
            first = curr;
        }
        
        if(prev)
        {
            prev->next = curr;
        }
        prev = curr;
        
    }
    
    DeAllocScratch(allocated_space);
    
    return first;
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

bool vk_create_atlas_descriptor_layout(vk_atlas_texture* input, VkDescriptorType type)
{
    VkDescriptorSetLayoutBinding image_layout_binding = {};
    image_layout_binding.binding = 0;
    image_layout_binding.descriptorType = type;
    image_layout_binding.descriptorCount = 1;
    image_layout_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    image_layout_binding.pImmutableSamplers = 0;
    
    VkDescriptorSetLayoutCreateInfo descriptor_set_create_info = {};
    
    descriptor_set_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    
    descriptor_set_create_info.bindingCount = 1;
    descriptor_set_create_info.pBindings = &image_layout_binding;
    
    if(vkCreateDescriptorSetLayout(rendering_platform.vk_device, &descriptor_set_create_info, 0, &(input->descriptor_layout)) != VK_SUCCESS)
    {
        return false;
    }
    return true;
}

bool vk_create_descriptor_set_layouts()
{
    assert(rendering_platform.vk_device);
    // Already initialized!
    assert(!rendering_platform.vk_combined_descriptor_layout);
    
    
    // Note(Leo): We share atlasses between all the windows however we cant do that for the input and swapchain layouts.
    //            Seperating the layouts into different sets allows us to potentially draw multiple windows at once
    if(!vk_create_atlas_descriptor_layout(&rendering_platform.vk_glyph_atlas, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE))
    {
        assert(0);
        return false;
    }
    
    if(!vk_create_atlas_descriptor_layout(&rendering_platform.vk_image_atlas, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE))
    {
        assert(0);
        return false;
    }

    VkDescriptorSetLayoutBinding input_buffer_layout_binding = {};
    input_buffer_layout_binding.binding = 0;
    input_buffer_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    input_buffer_layout_binding.descriptorCount = 1;
    input_buffer_layout_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    input_buffer_layout_binding.pImmutableSamplers = 0;

    VkDescriptorSetLayoutBinding swapchain_layout_binding = {};
    swapchain_layout_binding.binding = 1;
    swapchain_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    swapchain_layout_binding.descriptorCount = 1;
    swapchain_layout_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    swapchain_layout_binding.pImmutableSamplers = 0;
    
    VkDescriptorSetLayoutBinding combined_bindings[] = { input_buffer_layout_binding, swapchain_layout_binding };
    
    VkDescriptorSetLayoutCreateInfo combined_descriptor_set_create_info = {};
    
    combined_descriptor_set_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    
    combined_descriptor_set_create_info.bindingCount = (uint32_t)sizeof(combined_bindings)/sizeof(VkDescriptorSetLayoutBinding);
    combined_descriptor_set_create_info.pBindings = combined_bindings;
    
    if(vkCreateDescriptorSetLayout(rendering_platform.vk_device, &combined_descriptor_set_create_info, 0, &(rendering_platform.vk_combined_descriptor_layout)) != VK_SUCCESS)
    {
        assert(0);
        return false;
    }
    
    return true;
}

uint32_t vk_pick_render_tile_size()
{
    VkPhysicalDeviceProperties properties = {};
    vkGetPhysicalDeviceProperties(rendering_platform.vk_physical_device, &properties);
    
    // Get the largest size we could use for both width and height
    uint32_t max_size = MIN(properties.limits.maxComputeWorkGroupSize[0], properties.limits.maxComputeWorkGroupSize[1]);
    
    // We dont want the groups to get too large since that will increase overdraw
    max_size = MIN(max_size, MAX_RENDER_TILE_SIZE);
    
    uint32_t max_invocation_count = properties.limits.maxComputeWorkGroupInvocations;
    
    assert(max_invocation_count);
    // Todo(Leo): This is really stupid so replace it.
    while(max_size * max_size > max_invocation_count)
    {
        max_size -= 1;
    }
    
    return max_size;
}

bool vk_create_combined_pipeline(void* compute_shader_bin, int compute_bin_length)
{
    VkShaderModule compute_shader_module;

    if(!vk_create_shader_module(compute_shader_bin, compute_bin_length, &compute_shader_module))
    {
        printf("Failed to create compute shader module!\n");
        return false;
    }
    
    rendering_platform.render_tile_size = (int32_t)vk_pick_render_tile_size();
    
    SpecializationData specialization = {};
    specialization.render_tile_size = rendering_platform.render_tile_size;
    
    VkSpecializationMapEntry render_tile_entry = {};
    render_tile_entry.constantID = 0;
    render_tile_entry.size = sizeof(SpecializationData);
    render_tile_entry.offset = 0;
    
    VkSpecializationInfo specialization_info = {};
    specialization_info.dataSize = sizeof(SpecializationData);
    specialization_info.mapEntryCount = 1;
    specialization_info.pMapEntries = &render_tile_entry;
    specialization_info.pData = &specialization;
    
    
    VkPipelineShaderStageCreateInfo compute_shader_stage_info = {};
    compute_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    compute_shader_stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    compute_shader_stage_info.module = compute_shader_module;
    compute_shader_stage_info.pName = "main";
    compute_shader_stage_info.pSpecializationInfo = &specialization_info;
    
    VkDescriptorSetLayout set_layouts[] = { rendering_platform.vk_combined_descriptor_layout, rendering_platform.vk_glyph_atlas.descriptor_layout, rendering_platform.vk_image_atlas.descriptor_layout }; 
    
    VkPushConstantRange constants = {};
    constants.offset = 0;
    constants.size = sizeof(PushConstants);
    constants.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    VkPipelineLayoutCreateInfo pipeline_layout_info = {};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = sizeof(set_layouts)/sizeof(VkDescriptorSetLayout);
    pipeline_layout_info.pSetLayouts = set_layouts;
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &constants;
    
    if(vkCreatePipelineLayout(rendering_platform.vk_device, &pipeline_layout_info, 0, &rendering_platform.vk_combined_pipeline_layout) != VK_SUCCESS)
    {
        printf("Failed to create combined pipeline layout!\n");
        return false;
    }
    
    VkComputePipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.layout = rendering_platform.vk_combined_pipeline_layout;
    pipeline_info.stage = compute_shader_stage_info;
    
    if (vkCreateComputePipelines(rendering_platform.vk_device, 0, 1, &pipeline_info, 0, &rendering_platform.vk_combined_pipeline) != VK_SUCCESS)
    {
        printf("Failed to create combined pipeline!\n");
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
    pool_info.queueFamilyIndex = rendering_platform.vk_compute_queue_family;
    
    if(vkCreateCommandPool(rendering_platform.vk_device, &pool_info, 0, &rendering_platform.vk_main_command_pool) != VK_SUCCESS)
    {
        return false;
    }
    
    pool_info = {};
    
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = rendering_platform.vk_compute_queue_family;
    
    if(vkCreateCommandPool(rendering_platform.vk_device, &pool_info, 0, &rendering_platform.vk_transient_command_pool) != VK_SUCCESS)
    {
        return false;
    }
    
    return true;
}

bool vk_create_descriptor_pool()
{
    VkDescriptorPoolSize images_pool_size = {};
    images_pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    images_pool_size.descriptorCount = 3 * MAX_WINDOW_COUNT; // Note(Leo): 1 for glyph atlas, 1 for combined images and 1 for output image
    
    VkDescriptorPoolSize buffer_pool_size = {};
    buffer_pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    buffer_pool_size.descriptorCount = MAX_WINDOW_COUNT; // Note(Leo): 1 input buffer for each window

    VkDescriptorPoolSize pool_sizes[] = { images_pool_size, buffer_pool_size };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = sizeof(pool_sizes)/sizeof(VkDescriptorPoolSize);
    pool_info.pPoolSizes = pool_sizes;
    pool_info.maxSets = MAX_WINDOW_COUNT * 4;
    
    if(vkCreateDescriptorPool(rendering_platform.vk_device, &pool_info, 0, &(rendering_platform.vk_main_descriptor_pool)) != VK_SUCCESS)
    {
        return false;
    }
    
    return true;
}

bool vk_create_atlas_descriptor(vk_atlas_texture* input, VkDescriptorType image_type)
{
    // Need these top already be setup
    assert(input->descriptor_layout);
    assert(input->image_view);
    
    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = rendering_platform.vk_main_descriptor_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &(input->descriptor_layout);
    
    if(vkAllocateDescriptorSets(rendering_platform.vk_device, &alloc_info, &(input->descriptor_set)) != VK_SUCCESS)
    {
        return false;
    }
    
    VkDescriptorImageInfo atlas_info = {};
    atlas_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    atlas_info.imageView = input->image_view;
    atlas_info.sampler = 0;
    
    VkWriteDescriptorSet atlas_descriptor_write = {};
    atlas_descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    atlas_descriptor_write.dstSet = input->descriptor_set;
    atlas_descriptor_write.dstBinding = 0;
    atlas_descriptor_write.dstArrayElement = 0;
    atlas_descriptor_write.descriptorType = image_type;
    atlas_descriptor_write.descriptorCount = 1;
    atlas_descriptor_write.pImageInfo = &atlas_info;
    
    vkUpdateDescriptorSets(rendering_platform.vk_device, 1, &atlas_descriptor_write, 0, 0);
    
    return true;
}

bool vk_create_combined_descriptor(PlatformWindow* window)
{
    assert(window);
    assert(rendering_platform.vk_combined_descriptor_layout);
    // Buffer should be created already.
    assert(window->vk_input_buffer);
    
    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = rendering_platform.vk_main_descriptor_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &(rendering_platform.vk_combined_descriptor_layout);
    
    if(vkAllocateDescriptorSets(rendering_platform.vk_device, &alloc_info, &(window->vk_combined_descriptor)) != VK_SUCCESS)
    {
        return false;
    }
    
    VkDescriptorBufferInfo buffer_info = {};
    buffer_info.buffer = window->vk_input_buffer;
    buffer_info.offset = 0;
    buffer_info.range = (uint64_t)window->vk_input_buffer_size;
    
    VkWriteDescriptorSet buffer_descriptor_write = {};
    buffer_descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    buffer_descriptor_write.dstSet = window->vk_combined_descriptor;
    buffer_descriptor_write.dstBinding = 0;
    buffer_descriptor_write.dstArrayElement = 0;
    buffer_descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    buffer_descriptor_write.descriptorCount = 1;
    buffer_descriptor_write.pBufferInfo = &buffer_info;
    
    vkUpdateDescriptorSets(rendering_platform.vk_device, 1, &buffer_descriptor_write, 0, 0);
    
    return true;
}

// Called every frame to change the swapchain image target of the pipeline.
void vk_update_combined_descriptor(PlatformWindow* window, VkImageView image_view)
{
    VkDescriptorImageInfo image_info = {};
    image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    image_info.imageView = image_view;
    image_info.sampler = 0;
    
    VkWriteDescriptorSet image_descriptor_write = {};
    image_descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    image_descriptor_write.dstSet = window->vk_combined_descriptor;
    image_descriptor_write.dstBinding = 1;
    image_descriptor_write.dstArrayElement = 0;
    image_descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    image_descriptor_write.descriptorCount = 1;
    image_descriptor_write.pImageInfo = &image_info;
    
    vkUpdateDescriptorSets(rendering_platform.vk_device, 1, &image_descriptor_write, 0, 0);
}

bool vk_create_command_buffer(VkCommandBuffer* target)
{
    VkCommandBufferAllocateInfo alloc_info = {};
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

bool vk_record_command_buffer(VkCommandBuffer buffer, PlatformWindow* window, VkImage target_image, int shape_count)
{
    VkCommandBufferBeginInfo begin_recording_info = {};
    begin_recording_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_recording_info.flags = 0; 
    begin_recording_info.pInheritanceInfo = 0;
    
    if(vkBeginCommandBuffer(buffer, &begin_recording_info) != VK_SUCCESS)
    {
        return false;
    }
    
    // Note(Leo): Swapchain image has a unique layout which we have to transfer between to use it.
    VkImageMemoryBarrier image_barrier = {};
    image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    image_barrier.srcQueueFamilyIndex = rendering_platform.vk_present_queue_family;
    image_barrier.dstQueueFamilyIndex = rendering_platform.vk_compute_queue_family;
    image_barrier.image = target_image;
    image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_barrier.subresourceRange.baseMipLevel = 0;
    image_barrier.subresourceRange.levelCount = 1;
    image_barrier.subresourceRange.baseArrayLayer = 0;
    image_barrier.subresourceRange.layerCount = 1;
    image_barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    image_barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
    
    vkCmdPipelineBarrier(buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, 0, 0, 0, 1, &image_barrier);
    
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_COMPUTE, rendering_platform.vk_combined_pipeline);
    
    PushConstants constants = {};
    constants.screen_size = { (float)window->width, (float)window->height };
    constants.shape_count = shape_count;
    
    #if PLATFORM_ANDROID
    constants.invert_horizontal_axis = rendering_platform.orientation == ScreenOrientation::NINETY;
    constants.invert_vertical_axis = rendering_platform.orientation == ScreenOrientation::TWO_SEVENTY;
    #endif

    vkCmdPushConstants(buffer, rendering_platform.vk_combined_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &constants);
    
    VkDescriptorSet descriptor_sets[3] = { window->vk_combined_descriptor, rendering_platform.vk_glyph_atlas.descriptor_set, rendering_platform.vk_image_atlas.descriptor_set };
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_COMPUTE, rendering_platform.vk_combined_pipeline_layout, 0, 3, descriptor_sets, 0, 0);

    // Note(Leo): The compute shader renders in "tiles" to help with overdraw. Each tile is its own workgroup and invokes the shader
    //            width*width times (one for each pixel). At the moment tiles default to 64*64 (4096 pixels) so that on a 1920x1080 
    //            screen there is approximately 500 work groups with some overdraw on the bottom of the screen.
    //            The actual number of work groups needs to be calculated using the screen width/height
    
    // Note(Leo): + Render tile - 1 so that we will always round up the result
    assert(window->width && window->height);
    #if PLATFORM_ANDROID
    int width = rendering_platform.orientation != ScreenOrientation::ZERO ? window->height : window->width;
    int height = rendering_platform.orientation != ScreenOrientation::ZERO ? window->width : window->height;
    #else
    int width = window->width;
    int height = window->height;
    #endif
    
    uint32_t render_tile_size = (uint32_t)rendering_platform.render_tile_size;
    uint32_t horizontal_tiles = (width + render_tile_size - 1) / render_tile_size;
    uint32_t vertical_tiles = (height + render_tile_size - 1) / render_tile_size;
    
    vkCmdDispatch(buffer, horizontal_tiles, vertical_tiles, 1);
    
    image_barrier = {};
    image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    image_barrier.srcQueueFamilyIndex = rendering_platform.vk_compute_queue_family;
    image_barrier.dstQueueFamilyIndex = rendering_platform.vk_present_queue_family;
    image_barrier.image = target_image;
    image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_barrier.subresourceRange.baseMipLevel = 0;
    image_barrier.subresourceRange.levelCount = 1;
    image_barrier.subresourceRange.baseArrayLayer = 0;
    image_barrier.subresourceRange.layerCount = 1;
    image_barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
    image_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    vkCmdPipelineBarrier(buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, 0, 0, 0, 1, &image_barrier);

    
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
    
    void* allocated_space = Alloc(rendering_platform.vk_binary_data, file_length, no_zero());
    fread(allocated_space, file_length, 1, bin);
    *len = file_length;
    return allocated_space;
}

void vk_destroy_swapchain(VkSwapchainKHR swapchain)
{
    vkDestroySwapchainKHR(rendering_platform.vk_device, swapchain, 0);
}

void vk_destroy_swapchain_image_views(vk_swapchain_image* first_image_view)
{
    vk_swapchain_image* last = {};
    vk_swapchain_image* curr = first_image_view;
    
    curr = first_image_view;
    while(curr)
    {
        vkDestroyImageView(rendering_platform.vk_device, curr->image_view, 0);

        last = curr;
        curr = curr->next;
    }
    
    // Note(Leo): This relies on next being the first element of vk_swapchain_image and similiarly in freeblock!
    ((FreeBlock*)last)->next_free = rendering_platform.vk_swapchain_image_views->first_free.next_free;
    rendering_platform.vk_swapchain_image_views->first_free.next_free = (FreeBlock*)first_image_view;
}

int InitializeVulkan(Arena* master_arena, const char** required_extension_names, int required_extension_count, FILE* combined_shader)
{
    if(!vk_get_hook_address())
    {
        return 1;
    }
    if(!vk_get_global_procs())
    {
        return 1;
    }
    
    uint32_t extension_count;
    vkEnumerateInstanceExtensionProperties(NULL, &extension_count, NULL);
    
    // Note(Leo) + 1 since allignment can move the array over an entire element in worst case
    void* allocated_space = AllocScratch((extension_count + 1)*sizeof(VkExtensionProperties), no_zero());
    VkExtensionProperties* supported_extensions = (VkExtensionProperties*)align_mem(allocated_space, VkExtensionProperties);
    
    vkEnumerateInstanceExtensionProperties(NULL, &extension_count, supported_extensions);
    
    VulkanSupportedExtensions extension_support = {};
    for(int i = 0; i < extension_count; i++)
    {
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
    
    if(!vk_get_instance_procs())
    {
        return 1;
    }
    
//    printf("Succesfully initialized vulkan!\n");
    
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(rendering_platform.vk_instance, &device_count, 0);
    
    if(!device_count)
    {
        printf("No Vulkan compatible gpu's present!\n");
        return 1;
    }
    
    // Note(Leo): +1 to fit alignment
    allocated_space = AllocScratch((device_count + 1)*sizeof(VkPhysicalDevice), no_zero());
    VkPhysicalDevice* physical_devices = align_mem(allocated_space, VkPhysicalDevice);
    
    vkEnumeratePhysicalDevices(rendering_platform.vk_instance, &device_count, physical_devices);    
    
    VkPhysicalDevice* chosen_physical_device = vk_pick_physical_device(physical_devices, device_count);
    if(!chosen_physical_device)
    {
        printf("No vulkan compatible gpus met the application's requirements!\n");
        return 1;
    }
    
    rendering_platform.vk_physical_device = *chosen_physical_device;
    
    DeAllocScratch(allocated_space);
    
    rendering_platform.vk_swapchain_image_views = (Arena*)Alloc(rendering_platform.vk_master_arena, sizeof(Arena), zero());
    *(rendering_platform.vk_swapchain_image_views) = CreateArena(10000*sizeof(VkImageView), sizeof(VkImageView));
    
    rendering_platform.vk_binary_data = (Arena*)Alloc(rendering_platform.vk_master_arena, sizeof(Arena), zero());
    *(rendering_platform.vk_binary_data) = CreateArena(10000000*sizeof(char), sizeof(char));
    
    rendering_platform.image_atlas_tiles = (Arena*)Alloc(rendering_platform.vk_master_arena, sizeof(Arena), zero());
    *(rendering_platform.image_atlas_tiles) = CreateArena(1000*sizeof(RenderPlatformImageTile), sizeof(RenderPlatformImageTile));
    
    rendering_platform.image_handles = (Arena*)Alloc(rendering_platform.vk_master_arena, sizeof(Arena), zero());
    *(rendering_platform.image_handles) = CreateArena(1000*sizeof(LoadedImageHandle), sizeof(LoadedImageHandle));
    
    rendering_platform.vk_combined_shader.shader_bin = vk_read_shader_bin(combined_shader, &rendering_platform.vk_combined_shader.shader_length);
    
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
    
    if(vkQueueSubmit(rendering_platform.vk_compute_queue, 1, &submit_info, VK_NULL_HANDLE) != VK_SUCCESS)
    {
        return false;
    }
    
    vkQueueWaitIdle(rendering_platform.vk_compute_queue);
    
    vkFreeCommandBuffers(rendering_platform.vk_device, rendering_platform.vk_transient_command_pool, 1, &temp_command_buffer);
    return true;
}

bool vk_create_staging_buffer(int staging_size, PlatformWindow* window)
{
    if(!vk_create_buffer(staging_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &(window->vk_staging_buffer), &(window->vk_staging_memory)))
    {
        return false;
    }
    
    if(vkMapMemory(rendering_platform.vk_device, window->vk_staging_memory, 0, staging_size, 0, &(window->vk_staging_mapped_address)) != VK_SUCCESS)
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
    sampler_info.anisotropyEnable = VK_FALSE;
    sampler_info.maxAnisotropy = 1.0f;
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
    
    if(old_layout == VK_IMAGE_LAYOUT_GENERAL && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        image_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        image_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    
        source_flags = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        destination_flags = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    }
    else if(old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_GENERAL)
    {
        image_barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
        source_flags = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        destination_flags = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    }
    else if(old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_GENERAL)
    {
        image_barrier.srcAccessMask = 0;
        image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
        source_flags = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        destination_flags = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    }
    else if(old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
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
    else if(old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        image_barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
        source_flags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        destination_flags = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if(old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        image_barrier.srcAccessMask = 0;
        image_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        
        source_flags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
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
    
    if(vkQueueSubmit(rendering_platform.vk_compute_queue, 1, &submit_info, VK_NULL_HANDLE) != VK_SUCCESS)
    {
        return false;
    }
    
    vkQueueWaitIdle(rendering_platform.vk_compute_queue);
    
    vkFreeCommandBuffers(rendering_platform.vk_device, rendering_platform.vk_transient_command_pool, 1, &temp_command_buffer);
    
    return true;
}


bool vk_copy_buffer_to_image(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, ivec3 offsets)
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
    
    region.imageOffset = { offsets.x, offsets.y, offsets.z };
    region.imageExtent = { width, height, 1 };
    
    vkCmdCopyBufferToImage(temp_command_buffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    vkEndCommandBuffer(temp_command_buffer);
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &temp_command_buffer;
    
    if(vkQueueSubmit(rendering_platform.vk_compute_queue, 1, &submit_info, VK_NULL_HANDLE) != VK_SUCCESS)
    {
        return false;
    }
    
    vkQueueWaitIdle(rendering_platform.vk_compute_queue);
    
    vkFreeCommandBuffers(rendering_platform.vk_device, rendering_platform.vk_transient_command_pool, 1, &temp_command_buffer);
    return true;
}

inline bool vk_copy_buffer_to_image(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
{
    return vk_copy_buffer_to_image(buffer, image, width, height, {0, 0, 0});
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


#define vk_copy_to_buffer_aligned(buffer_next, copy_src, alloc_size, align_type)  vk_copy_to_buffer_aligned_f(align_mem((void*)buffer_next, align_type), (void*)copy_src, alloc_size)

void* vk_copy_to_buffer_aligned_f(void* aligned_buffer_next, void* copy_src, int alloc_size)
{
    memcpy(aligned_buffer_next, copy_src, alloc_size);
    return aligned_buffer_next;
}

// Note(Leo): Tiles are assumed to be square
uvec3 vk_pick_atlas_dimensions(int total_tile_target, int tile_width, VkFormat image_format)
{
    VkImageFormatProperties supported_atlas_size = {};
    if(vkGetPhysicalDeviceImageFormatProperties(rendering_platform.vk_physical_device, image_format, VK_IMAGE_TYPE_3D, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 0, &supported_atlas_size) != VK_SUCCESS)
    {
        assert(0);
        return {0, 0, 0};
    }
    
    uint32_t max_width = supported_atlas_size.maxExtent.width;
    uint32_t max_height = supported_atlas_size.maxExtent.height;
    
    // Shrink max width/height to be a factor of the glyph width so we can fit an even number of glyphs
    uint32_t tile_aligned_width = max_width - (max_width % tile_width);
    uint32_t tile_aligned_height = max_height - (max_height % tile_width);
    
    uint32_t tiles_per_layer = (tile_aligned_width / tile_width) *  (tile_aligned_height / tile_width);
    
    // Walk back the dimensions until we achieve an apporiximately minumum sized image
    // Todo(Leo): This is garbage, replace this with something smarter
    if(tiles_per_layer > total_tile_target)
    {
        while(tiles_per_layer > total_tile_target)
        {
            if(tile_aligned_width == tile_width || tile_aligned_height == tile_width)
            {
                break;
            }
            // Break when shrinking in either dimension would make the atlas too small (this tests shrinking both at once which isnt the same)
            // but its less wastefull calculations
            if((((tile_aligned_width - tile_width) / tile_width) * ((tile_aligned_height - tile_width) / tile_width)) < total_tile_target)
            {
                break;
            }
            if(tile_aligned_width > tile_aligned_height)
            {
                tile_aligned_width -= tile_width;                      
            }
            else
            {
                tile_aligned_height -= tile_width;
            }
            tiles_per_layer = (tile_aligned_width / tile_width) *  (tile_aligned_height / tile_width);
        }
        tiles_per_layer = (tile_aligned_width / tile_width) *  (tile_aligned_height / tile_width);
    }
    
    // Round up desired tile count so that we can get an ineger when finding the amount of layers needed
    uint32_t rounded_desired_tiles = total_tile_target + ( tiles_per_layer - (total_tile_target % tiles_per_layer));
    
    uint32_t required_depth = rounded_desired_tiles / tiles_per_layer;
    
    return { tile_aligned_width, tile_aligned_height, required_depth };
}

uvec3 vk_get_tile_coordinate(vk_atlas_texture* atlas, uint32_t tile_size, int tile_index)
{
    if(tile_index == 0)
    {
        return {0, 0, 0};
    }
    
    uint32_t atlas_width = atlas->dimensions.x;
    uint32_t atlas_height = atlas->dimensions.y;
    
    uint32_t atlas_width_tiles = atlas_width / tile_size; 
    uint32_t atlas_height_tiles = atlas_height / tile_size;
    
    uint32_t depth = tile_index / (atlas_width_tiles * atlas_height_tiles);
    uint32_t depth_remainder = tile_index % (atlas_width_tiles * atlas_height_tiles);
    
    uint32_t x_tile = depth_remainder % atlas_width_tiles;
    uint32_t y_tile = depth_remainder / atlas_width_tiles;
    
    return {x_tile * tile_size, y_tile * tile_size, depth};
}

vec3 RenderPlatformGetGlyphPosition(int glyph_slot)
{
    uvec3 position = vk_get_tile_coordinate(&rendering_platform.vk_glyph_atlas, FontPlatformGetGlyphSize(), glyph_slot);
    return { (float)position.x, (float)position.y, (float)position.z };
}

#define ImageTileSlot(tile_ptr) (((uintptr_t)tile_ptr - rendering_platform.image_atlas_tiles->mapped_address) / sizeof(RenderPlatformImageTile)) 

void RenderplatformLoadImage(FILE* image_file, const char* name)
{
    LoadedImageHandle* created_handle = (LoadedImageHandle*)Alloc(rendering_platform.image_handles, sizeof(LoadedImageHandle));
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
        
    created_handle->image_width = (uint32_t)image_width;
    created_handle->image_height = (uint32_t)image_height;
    
    assert(image_width && image_height);
    
    // Add an extra tile if there are remaining pixels 
    created_handle->tiled_width = (image_width / IMAGE_TILE_SIZE) + (image_width % IMAGE_TILE_SIZE > 0 ? 1 : 0);
    created_handle->tiled_height = (image_height / IMAGE_TILE_SIZE) + (image_height % IMAGE_TILE_SIZE > 0 ? 1 : 0);
    
    // Note(Leo): The slot of an RenderPlatformImageTile in the gpu atlas is the index of the RenderPlatformImageTile in the tiles arena
    RenderPlatformImageTile* curr_tile = (RenderPlatformImageTile*)Alloc(rendering_platform.image_atlas_tiles, sizeof(RenderPlatformImageTile));
    created_handle->first_tile = curr_tile;
    
    //Note(Leo): 4 bytes per pixel for RGBA
    void* working_tile_mem = AllocScratch(IMAGE_TILE_SIZE * IMAGE_TILE_SIZE * 4);
    Arena working_tile = CreateArenaWith(working_tile_mem, IMAGE_TILE_SIZE * IMAGE_TILE_SIZE * 4, sizeof(char));
    
    uint32_t cursor_x = 0;
    uint32_t cursor_y = 0;
    
    for(int row = 0; row < created_handle->tiled_height; row++)
    {
        for(int column = 0; column < created_handle->tiled_width; column++)
        {
            uint32_t slot = ImageTileSlot(curr_tile);
            curr_tile->atlas_offsets = vk_get_tile_coordinate(&rendering_platform.vk_image_atlas, (uint32_t)IMAGE_TILE_SIZE, ImageTileSlot(curr_tile));
            curr_tile->image_offsets.x = cursor_x; 
            curr_tile->image_offsets.y = cursor_y;
            
            curr_tile->content_width = IMAGE_TILE_SIZE; 
            if(cursor_x + IMAGE_TILE_SIZE > created_handle->image_width) // This tile's content isnt the full width of the tile
            {
                curr_tile->content_width = created_handle->image_width - cursor_x;
            }
        
            curr_tile->content_height = IMAGE_TILE_SIZE; 
            if(cursor_y + IMAGE_TILE_SIZE > created_handle->image_height) // This tile's content isnt the full height of the tile
            {
                curr_tile->content_height = created_handle->image_height - cursor_y;
            }
        
            uintptr_t image_copy_offset = (uintptr_t)(cursor_x + (cursor_y * created_handle->image_width));
            
            // 4 bytes per pixel
            image_copy_offset *= 4;
            
            for(int copy_row = 0; copy_row < curr_tile->content_height; copy_row++)
            {
                void* target_row = Alloc(&working_tile, IMAGE_TILE_SIZE * 4, zero());
                memcpy(target_row, (void*)((uintptr_t)image_pixels + image_copy_offset), curr_tile->content_width * 4);
                image_copy_offset += (uintptr_t)(created_handle->image_width * 4);
            }
            
            if(!vk_transition_image_layout(rendering_platform.vk_image_atlas.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
            {
                printf("Failed transitioning layout for image atlas!\n");
                return;
            }

            VkBuffer temp_stage;
            VkDeviceMemory temp_stage_memory;
            
            if(!vk_create_buffer(IMAGE_TILE_SIZE * IMAGE_TILE_SIZE * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &temp_stage, &temp_stage_memory))
            {
                return;
            }
            
            void* staging_data;
            vkMapMemory(rendering_platform.vk_device, temp_stage_memory, 0, IMAGE_TILE_SIZE * IMAGE_TILE_SIZE * 4, 0, &staging_data);
            memcpy(staging_data, (void*)working_tile.mapped_address, IMAGE_TILE_SIZE * IMAGE_TILE_SIZE * 4);
            vkUnmapMemory(rendering_platform.vk_device, temp_stage_memory);
            
            ivec3 tile_offsets = {(int32_t)curr_tile->atlas_offsets.x, (int32_t)curr_tile->atlas_offsets.y, (int32_t)curr_tile->atlas_offsets.z};
                    
            if(!vk_copy_buffer_to_image(temp_stage, rendering_platform.vk_image_atlas.image, (uint32_t)IMAGE_TILE_SIZE, (uint32_t)IMAGE_TILE_SIZE, tile_offsets))
            {
                return;
            }
            
            if(!vk_transition_image_layout(rendering_platform.vk_image_atlas.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL))
            {
                printf("Failed transitioning layout for glyph image!\n");
                return;
            }
            
            vkDestroyBuffer(rendering_platform.vk_device, temp_stage, 0);
            vkFreeMemory(rendering_platform.vk_device, temp_stage_memory, 0);
                        
            ResetArena(&working_tile);
            // Note(Leo): This isnt neccesary but by not sanitizing this arena totally the renderdoc view of the atlas becomes 
            //            polluted and difficult to judge whether it is working correctly.
            memset((void*)working_tile.mapped_address, 0, IMAGE_TILE_SIZE * IMAGE_TILE_SIZE * 4);
                        
            RenderPlatformImageTile* last_tile = curr_tile;
            
            // Dont alloc on very last iteration
            if(!(row == created_handle->tiled_height - 1 && column == created_handle->tiled_width - 1))
            {
                curr_tile = (RenderPlatformImageTile*)Alloc(rendering_platform.image_atlas_tiles, sizeof(RenderPlatformImageTile));
                last_tile->next = curr_tile;
            }
            
            cursor_x += IMAGE_TILE_SIZE;
        }
        cursor_x = 0;
        cursor_y += IMAGE_TILE_SIZE;
    }
    
    DeAllocScratch(working_tile_mem);
}



bool vk_initialize_atlas(vk_atlas_texture* atlas, VkFormat image_format)
{
    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_3D;
    image_info.extent.width = atlas->dimensions.x;
    image_info.extent.height = atlas->dimensions.y;
    image_info.extent.depth = atlas->dimensions.z;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = image_format;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.flags = 0;
    
    
    if(vkCreateImage(rendering_platform.vk_device, &image_info, 0, &(atlas->image)) != VK_SUCCESS)
    {
        assert(0);
        return false;
    }
    
    VkMemoryRequirements memory_requirements = {};
    vkGetImageMemoryRequirements(rendering_platform.vk_device, atlas->image, &memory_requirements);
    
    // Note(Leo): AMD driver on the new card (RX 9070 XT) seems to be bugged and returns a much larger size than it should
    //  here (900Mb instead of 50mb). There is nothing we can do about that except wait for AMD to fix it :(. WSL virtual
    //  display adapter and older AMD card both reported the size correctly so the code seems fine.
    //printf("Font atlas picked size %dx%dx%d with memory size %ld\n", atlas->dimensions.x, atlas->dimensions.y, atlas->dimensions.z, memory_requirements.size);
    
    VkMemoryAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    
    alloc_info.allocationSize = memory_requirements.size;
    alloc_info.memoryTypeIndex = vk_find_memory_type(memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if(vkAllocateMemory(rendering_platform.vk_device, &alloc_info, 0, &(atlas->memory)) != VK_SUCCESS)
    {
        printf("Failed to create image buffer!\n");
        return false;
    }
    
    vkBindImageMemory(rendering_platform.vk_device, atlas->image, atlas->memory, 0);
    
    if(!vk_transition_image_layout(atlas->image, image_format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL))
    {
        printf("Failed transitioning layout for glyph image!\n");
        return false;
    }
    
    VkImageViewCreateInfo image_view_info = {};
    image_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    image_view_info.image = atlas->image;
    image_view_info.viewType = VK_IMAGE_VIEW_TYPE_3D;
    image_view_info.format = image_format;
    image_view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    image_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_view_info.subresourceRange.baseMipLevel = 0;
    image_view_info.subresourceRange.levelCount = 1;
    image_view_info.subresourceRange.baseArrayLayer = 0;
    image_view_info.subresourceRange.layerCount = 1;
    
    if(vkCreateImageView(rendering_platform.vk_device, &image_view_info, 0, &(atlas->image_view)) != VK_SUCCESS)
    {
        return false;
    }
    
    return true;
}

bool vk_initialize_image_atlas()
{
    rendering_platform.vk_image_atlas.dimensions = vk_pick_atlas_dimensions(IMAGE_ATLAS_SIZE, IMAGE_TILE_SIZE, VK_FORMAT_R8G8B8A8_UNORM);
    rendering_platform.image_tile_capacity = (rendering_platform.vk_image_atlas.dimensions.x / IMAGE_TILE_SIZE) * (rendering_platform.vk_image_atlas.dimensions.y / IMAGE_TILE_SIZE) * rendering_platform.vk_image_atlas.dimensions.z;

    if(!vk_initialize_atlas(&rendering_platform.vk_image_atlas, VK_FORMAT_R8G8B8A8_UNORM))
    {
        return false;
    }
    if(!vk_create_atlas_descriptor(&rendering_platform.vk_image_atlas, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE))
    {
        return false;
    }
    return true;
}   

bool vk_initialize_font_atlas()
{
    uint32_t glyph_size = (uint32_t)FontPlatformGetGlyphSize();
    rendering_platform.vk_glyph_atlas.dimensions = vk_pick_atlas_dimensions(GLYPH_ATLAS_COUNT, glyph_size, VK_FORMAT_R8_UINT);
    uint32_t actual_glyph_capacity = (rendering_platform.vk_glyph_atlas.dimensions.x / glyph_size) * (rendering_platform.vk_glyph_atlas.dimensions.y / glyph_size) * rendering_platform.vk_glyph_atlas.dimensions.z;
    
    FontPlatformUpdateCache(actual_glyph_capacity);
    
    if(!vk_initialize_atlas(&rendering_platform.vk_glyph_atlas, VK_FORMAT_R8_UINT))
    {
        return false;
    }
    if(!vk_create_atlas_descriptor(&rendering_platform.vk_glyph_atlas, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE))
    {
        return false;
    }
    return true;
}   

bool vk_create_input_buffer(PlatformWindow* window)
{
    window->vk_input_buffer_size = WINDOW_INPUT_SIZE;
    return vk_create_buffer(WINDOW_INPUT_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &window->vk_input_buffer, &window->vk_input_memory);
}

void RenderplatformUploadGlyph(void* glyph_data, int glyph_width, int glyph_height, int glyph_slot)
{
    // Note(Leo): This depends on glyph pixels being 1 byte 
    int glyph_size = glyph_width * glyph_height * sizeof(char);
    assert(glyph_size);
    
    if(!vk_transition_image_layout(rendering_platform.vk_glyph_atlas.image, VK_FORMAT_R8_UINT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
    {
        printf("Failed transitioning layout for glyph image!\n");
        return;
    }
    
    VkBuffer temp_stage;
    VkDeviceMemory temp_stage_memory;
    
    if(!vk_create_buffer(glyph_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &temp_stage, &temp_stage_memory))
    {
        return;
    }
    
    void* staging_data;
    vkMapMemory(rendering_platform.vk_device, temp_stage_memory, 0, glyph_size, 0, &staging_data);
    memcpy(staging_data, glyph_data, glyph_size);
    vkUnmapMemory(rendering_platform.vk_device, temp_stage_memory);
    uvec3 found_glyph_offsets = vk_get_tile_coordinate(&rendering_platform.vk_glyph_atlas, (uint32_t)FontPlatformGetGlyphSize(), glyph_slot);
    
    ivec3 glyph_offsets = {(int32_t)found_glyph_offsets.x, (int32_t)found_glyph_offsets.y, (int32_t)found_glyph_offsets.z};
    
    if(!vk_copy_buffer_to_image(temp_stage, rendering_platform.vk_glyph_atlas.image, (uint32_t)glyph_width, (uint32_t)glyph_height, glyph_offsets))
    {
        return;
    }
    
    if(!vk_transition_image_layout(rendering_platform.vk_glyph_atlas.image, VK_FORMAT_R8_UINT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL))
    {
        printf("Failed transitioning layout for glyph image!\n");
        return;
    }
    
    vkDestroyBuffer(rendering_platform.vk_device, temp_stage, 0);
    vkFreeMemory(rendering_platform.vk_device, temp_stage_memory, 0);
}

void RenderplatformDrawWindow(PlatformWindow* window, Arena* renderque)
{
    BEGIN_TIMED_BLOCK(WAIT_FENCE);
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
    
    END_TIMED_BLOCK(WAIT_FENCE);
    
    
    // Note(Leo): Temporary code, REMOVE!!
    static bool done = false;
    if(!done)
    {
        FontHandle test_font = FontPlatformGetFont("platform_default_font.ttf");
        Arena temp = CreateArena(sizeof(FontPlatformShapedGlyph)*1000, sizeof(FontPlatformShapedGlyph));
        //FontPlatformShape(&temp, "Deez Nuts", test_font, 40, 500, 500);
        done = true;
    }

    vk_swapchain_image* curr = window->vk_first_image;
    for(int i = 0; i < image_index; i++)
    {
        curr = curr->next;
        assert(curr);
    }
    VkImageView used_image_view = curr->image_view;
    
    vk_update_combined_descriptor(window, used_image_view);
    
    memcpy(window->vk_staging_mapped_address, (void*)renderque->mapped_address, renderque->next_address - renderque->mapped_address);
    vk_copy_buffer(window->vk_staging_buffer, window->vk_input_buffer, renderque->next_address - renderque->mapped_address, 0, 0);
    
    int shape_count = (renderque->next_address - renderque->mapped_address) / sizeof(combined_instance);
    
    if(!vk_record_command_buffer(window->vk_command_buffer, window, curr->image, shape_count))
    {
        printf("ERROR: Couldnt record command buffer!\n");
    }
    
    VkSemaphore wait_semaphores[] = { window->vk_image_available_semaphore };
    VkSemaphore signal_semaphores[] = { window->vk_render_finished_semaphore };
    VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT  };
    
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &(window->vk_command_buffer);
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores;
    
    BEGIN_TIMED_BLOCK(RENDER_SUBMIT);
    
    if(vkQueueSubmit(rendering_platform.vk_compute_queue, 1, &submit_info, window->vk_in_flight_fence) != VK_SUCCESS)
    {
        printf("ERROR: Couldnt submit draw command buffer!\n");
    }

    END_TIMED_BLOCK(RENDER_SUBMIT);
    
    BEGIN_TIMED_BLOCK(RENDER_PRESENT);
    
    VkSwapchainKHR swapchains[] = { window->vk_window_swapchain };
    
    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_semaphores;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = swapchains;
    present_info.pImageIndices = &image_index;
    present_info.pResults = 0;
    
    VkResult que_status = vkQueuePresentKHR(rendering_platform.vk_present_queue, &present_info);
    
    END_TIMED_BLOCK(RENDER_PRESENT);
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

    vk_destroy_swapchain_image_views(window->vk_first_image);
    vk_destroy_swapchain(window->vk_window_swapchain);
    
    vkDestroySemaphore(rendering_platform.vk_device, window->vk_image_available_semaphore, nullptr);
    vkDestroySemaphore(rendering_platform.vk_device, window->vk_render_finished_semaphore, nullptr);
    vkDestroyFence(rendering_platform.vk_device, window->vk_in_flight_fence, nullptr);
    
    if(!vk_create_swapchain(window->vk_window_surface, window->width, window->height, &(window->vk_window_swapchain)))
    {
        printf("ERROR: Couldn't re-create swapchain!\n");
    }
    
    window->vk_first_image = vk_create_swapchain_image_views(window->vk_window_swapchain);
    
    vk_create_sync_objects(window);
}

void vk_destroy_window_surface(PlatformWindow* window)
{
    vkDeviceWaitIdle(rendering_platform.vk_device);
    
    vkDestroySemaphore(rendering_platform.vk_device, window->vk_image_available_semaphore, nullptr);
    vkDestroySemaphore(rendering_platform.vk_device, window->vk_render_finished_semaphore, nullptr);
    vkDestroyFence(rendering_platform.vk_device, window->vk_in_flight_fence, nullptr);

    vk_destroy_swapchain_image_views(window->vk_first_image);
    vk_destroy_swapchain(window->vk_window_swapchain);
    
    vkDestroySurfaceKHR(rendering_platform.vk_instance, window->vk_window_surface, 0);
}

// Note(Leo): Stuff that can only happen once a window surface has been provided (logical device dependent stuff)
void vk_late_initialize()
{    
    if(!vk_create_descriptor_set_layouts())
    {
        printf("Failed to create descriptor sets!\n");
    }
    if(!vk_create_command_pools())
    {
        printf("Failed to create command pool!\n");
    }    
    if(!vk_create_descriptor_pool())
    {
        printf("Failed to create descriptor pool!\n");
    }
    if(!vk_initialize_image_atlas())
    {
        printf("Failed to initialize image atlas!\n");
    }
    if(!vk_initialize_font_atlas())
    {
        printf("Failed to initialize font glyph atlas!\n");
    }
    if(!vk_create_combined_pipeline(rendering_platform.vk_combined_shader.shader_bin, rendering_platform.vk_combined_shader.shader_length))
    {
        printf("Failed to initialize the compute pipeline!\n");
    }
    rendering_platform.vk_graphics_pipeline_initialized = true;
}

#if PLATFORM_WINDOWS
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
    
    window->vk_first_image = vk_create_swapchain_image_views(window->vk_window_swapchain);
    if(!window->vk_first_image)
    {
        printf("Failed to create image views for window swapchain!\n");
    }
    
    if(!rendering_platform.vk_graphics_pipeline_initialized)
    {
        vk_late_initialize();
    }
    
    if(!vk_create_command_buffer(&(window->vk_command_buffer)))
    {
        printf("Failed to create command buffer\n");
    }
    
    if(!vk_create_sync_objects(window))
    {
        printf("Failed to create sync objects!\n");
    }

    if(!vk_create_staging_buffer(WINDOW_STAGING_SIZE, window))
    {
        printf("Failed to create a staging buffer!\n");
    }
        
    if(!vk_create_input_buffer(window))
    {
        printf("Failed to create window input buffer!\n");
    }

    if(!vk_create_combined_descriptor(window))
    {
        printf("Failed to create window descriptors!\n");
    }
}

#endif

#if PLATFORM_LINUX
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
    
    window->vk_first_image = vk_create_swapchain_image_views(window->vk_window_swapchain);
    if(!window->vk_first_image)
    {
        printf("Failed to create image views for window swapchain!\n");
    }
    
    if(!rendering_platform.vk_graphics_pipeline_initialized)
    {
        vk_late_initialize();
    }
    
    if(!vk_create_command_buffer(&(window->vk_command_buffer)))
    {
        printf("Failed to create command buffer\n");
    }
    
    if(!vk_create_sync_objects(window))
    {
        printf("Failed to create sync objects!\n");
    }
        
    if(!vk_create_staging_buffer(WINDOW_STAGING_SIZE, window))
    {
        printf("Failed to create a staging buffer!\n");
    }
    
    if(!vk_create_input_buffer(window))
    {
        printf("Failed to create window input buffer!\n");
    }
    
    if(!vk_create_combined_descriptor(window))
    {
        printf("Failed to create window descriptors!\n");
    }
}
#endif

#if PLATFORM_ANDROID
//#include <android_native_app_glue.h>

void android_vk_create_window_surface(PlatformWindow* window)
{
    VkAndroidSurfaceCreateInfoKHR  surface_info = {};
    surface_info.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    
    surface_info.window = window->window_handle;
    VkResult error = vkCreateAndroidSurfaceKHR(rendering_platform.vk_instance, &surface_info, NULL, &(window->vk_window_surface));
    
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
    
    window->vk_first_image = vk_create_swapchain_image_views(window->vk_window_swapchain);
    if(!window->vk_first_image)
    {
        printf("Failed to create image views for window swapchain!\n");
    }
    
    if(!rendering_platform.vk_graphics_pipeline_initialized)
    {
        vk_late_initialize();
    }
    
    if(!vk_create_command_buffer(&(window->vk_command_buffer)))
    {
        printf("Failed to create command buffer\n");
    }
    
    if(!vk_create_sync_objects(window))
    {
        printf("Failed to create sync objects!\n");
    }
        
    if(!vk_create_staging_buffer(WINDOW_STAGING_SIZE, window))
    {
        printf("Failed to create a staging buffer!\n");
    }
    
    if(!vk_create_input_buffer(window))
    {
        printf("Failed to create window input buffer!\n");
    }
    
    if(!vk_create_combined_descriptor(window))
    {
        printf("Failed to create window descriptors!\n");
    }
}
#endif
