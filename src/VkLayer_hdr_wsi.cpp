#define VK_USE_PLATFORM_WAYLAND_KHR
#include "vkroots.h"
#include "color-management-v1-client-protocol.h"
#include "color-representation-v1-client-protocol.h"

#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <optional>

using namespace std::literals;

namespace HdrLayer
{
  static bool contains_str(const std::vector<const char *> vec, std::string_view lookupValue)
  {
    return std::any_of(vec.begin(), vec.end(),
                        [=](const char *value)
                        { return value == lookupValue; });
  }
  static bool contains_u32(const std::vector<uint32_t> vec, uint32_t lookupValue)
  {
    return std::any_of(vec.begin(), vec.end(),
                        [=](uint32_t value)
                        { return value == lookupValue; });
  }

  struct ColorDescription
  {
    VkSurfaceFormat2KHR surface;
    int primaries_named;
    int tf_named;
    bool extended_volume;
  };

  static constexpr std::array<ColorDescription, 12> s_ExtraHDRSurfaceFormats = {
      ColorDescription{
          .surface = {.surfaceFormat = {
                          VK_FORMAT_A2B10G10R10_UNORM_PACK32,
                          VK_COLOR_SPACE_HDR10_ST2084_EXT,
                      }},
          .primaries_named = XX_COLOR_MANAGER_V4_PRIMARIES_BT2020,
          .tf_named = XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_ST2084_PQ,
          .extended_volume = false,
      },
      ColorDescription{
        .surface = {
            .surfaceFormat = {
                VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT,
            }
        },
        .primaries_named = XX_COLOR_MANAGER_V4_PRIMARIES_SRGB,
        .tf_named = XX_COLOR_MANAGER_V4_TRANSFER_FUNCTION_LINEAR, //FUNCTION_SCRGB_LINEAR,
        .extended_volume = true,
    },
/*
      ColorDescription{
          .surface = {.surfaceFormat = {
                          VK_FORMAT_A2R10G10B10_UNORM_PACK32,
                          VK_COLOR_SPACE_HDR10_ST2084_EXT,
                      }},
          .primaries_cicp = 9,
          .tf_cicp = 16,
          .extended_volume = false,
      },
      ColorDescription{
          .surface = {.surfaceFormat = {
                          VK_FORMAT_A2B10G10R10_UNORM_PACK32,
                          VK_COLOR_SPACE_HDR10_HLG_EXT,
                      }},
          .primaries_cicp = 9,
          .tf_cicp = 18,
          .extended_volume = false,
      },
      ColorDescription{
          .surface = {.surfaceFormat = {
                          VK_FORMAT_A2R10G10B10_UNORM_PACK32,
                          VK_COLOR_SPACE_HDR10_HLG_EXT,
                      }},
          .primaries_cicp = 9,
          .tf_cicp = 18,
          .extended_volume = false,
      },
      ColorDescription{
          .surface = {.surfaceFormat = {
                          VK_FORMAT_R16G16B16A16_SFLOAT,
                          VK_COLOR_SPACE_BT2020_LINEAR_EXT,
                      }},
          .primaries_cicp = 9,
          .tf_cicp = 8,
          .extended_volume = false,
      },
      ColorDescription{
          .surface = {.surfaceFormat = {
                          VK_FORMAT_A2B10G10R10_UNORM_PACK32,
                          VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT,
                      }},
          .primaries_cicp = 13,
          .tf_cicp = 18,
          .extended_volume = false,
      },
      ColorDescription{
          .surface = {.surfaceFormat = {
                          VK_FORMAT_A2R10G10B10_UNORM_PACK32,
                          VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT,
                      }},
          .primaries_cicp = 13,
          .tf_cicp = 18,
          .extended_volume = false,
      },
      ColorDescription{
          .surface = {.surfaceFormat = {
                          VK_FORMAT_R16G16B16A16_SFLOAT,
                          VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT,
                      }},
          .primaries_cicp = 13,
          .tf_cicp = 8,
          .extended_volume = false,
      },
      ColorDescription{
          .surface = {.surfaceFormat = {
                          VK_FORMAT_A2B10G10R10_UNORM_PACK32,
                          VK_COLOR_SPACE_BT709_NONLINEAR_EXT,
                      }},
          .primaries_cicp = 6,
          .tf_cicp = 6,
          .extended_volume = true,
      },
      ColorDescription{
          .surface = {.surfaceFormat = {
                          VK_FORMAT_A2R10G10B10_UNORM_PACK32,
                          VK_COLOR_SPACE_BT709_NONLINEAR_EXT,
                      }},
          .primaries_cicp = 6,
          .tf_cicp = 6,
          .extended_volume = true,
      },
      ColorDescription{
          .surface = {.surfaceFormat = {
                          VK_FORMAT_R16G16B16A16_SFLOAT,
                          VK_COLOR_SPACE_BT709_LINEAR_EXT,
                      }},
          .primaries_cicp = 6,
          .tf_cicp = 8,
          .extended_volume = true,
      },
      ColorDescription{
          .surface = {.surfaceFormat = {
                          VK_FORMAT_R16G16B16A16_SFLOAT,
                          VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT,
                      }},
          .primaries_cicp = 1,
          .tf_cicp = 8,
          .extended_volume = true,
      }*/
};

  struct HdrSurfaceData
  {
    VkInstance instance;

    wl_display *display;
    wl_event_queue *queue;
    xx_color_manager_v4 *colorManagement;

    std::vector<uint32_t> features;
    std::vector<uint32_t> tf_named;
    std::vector<uint32_t> primaries_named;

    wl_surface *surface;
    xx_color_management_surface_v4 *colorSurface;
  };
  VKROOTS_DEFINE_SYNCHRONIZED_MAP_TYPE(HdrSurface, VkSurfaceKHR);

  struct HdrSwapchainData
  {
    VkSurfaceKHR surface;
    int primaries;
    int tf;

    xx_image_description_v4 *colorDescription;
    bool desc_dirty;
  };
  VKROOTS_DEFINE_SYNCHRONIZED_MAP_TYPE(HdrSwapchain, VkSwapchainKHR);

  enum DescStatus
  {
    WAITING,
    READY,
    FAILED,
  };

  class VkInstanceOverrides
  {
  public:
    static VkResult CreateInstance(
        PFN_vkCreateInstance pfnCreateInstanceProc,
        const VkInstanceCreateInfo *pCreateInfo,
        const VkAllocationCallbacks *pAllocator,
        VkInstance *pInstance)
    {
      auto enabledExts = std::vector<const char *>(
          pCreateInfo->ppEnabledExtensionNames,
          pCreateInfo->ppEnabledExtensionNames + pCreateInfo->enabledExtensionCount);

      if (contains_str(enabledExts, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME))
        std::erase(enabledExts, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);

      VkInstanceCreateInfo createInfo = *pCreateInfo;
      createInfo.enabledExtensionCount = uint32_t(enabledExts.size());
      createInfo.ppEnabledExtensionNames = enabledExts.data();

      return pfnCreateInstanceProc(&createInfo, pAllocator, pInstance);
    }

    static VkResult CreateWaylandSurfaceKHR(
        const vkroots::VkInstanceDispatch *pDispatch,
        VkInstance instance,
        const VkWaylandSurfaceCreateInfoKHR *pCreateInfo,
        const VkAllocationCallbacks *pAllocator,
        VkSurfaceKHR *pSurface)
    {
      auto queue = wl_display_create_queue(pCreateInfo->display);
      wl_registry *registry = wl_display_get_registry(pCreateInfo->display);
      wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(registry), queue);

      VkResult res = pDispatch->CreateWaylandSurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
      if (res != VK_SUCCESS)
      {
        return res;
      }

      {
        auto hdrSurface = HdrSurface::create(*pSurface, HdrSurfaceData{
                                                            .instance = instance,
                                                            .display = pCreateInfo->display,
                                                            .queue = queue,
                                                            .colorManagement = nullptr,
                                                            .features = {},
                                                            .tf_named = {},
                                                            .primaries_named = {},
                                                            .surface = pCreateInfo->surface,
                                                            .colorSurface = nullptr,
                                                        });

        wl_registry_add_listener(registry, &s_registryListener, reinterpret_cast<void *>(hdrSurface.get()));
        wl_display_dispatch_queue(pCreateInfo->display, queue);
        wl_display_roundtrip_queue(pCreateInfo->display, queue); // get globals
        wl_display_roundtrip_queue(pCreateInfo->display, queue); // get features/supported tf, primaries/etc
        wl_registry_destroy(registry);
      }

      if (!HdrSurface::get(*pSurface)->colorManagement)
      {
        fprintf(stderr, "[HDR Layer] wayland compositor lacking color management protocol..\n");

        HdrSurface::remove(*pSurface);
        return VK_SUCCESS;
      }
      if (!contains_u32(HdrSurface::get(*pSurface)->features, XX_COLOR_MANAGER_V4_FEATURE_PARAMETRIC))
      {
        fprintf(stderr, "[HDR Layer] color management implementation doesn't support parametric image descriptions..\n");
        HdrSurface::remove(*pSurface);
        return VK_SUCCESS;
      }

      auto hdrSurface = HdrSurface::get(*pSurface);

      xx_color_management_surface_v4 *colorSurface = xx_color_manager_v4_get_surface(hdrSurface->colorManagement, pCreateInfo->surface);

      hdrSurface->colorSurface = colorSurface;

      fprintf(stderr, "[HDR Layer] Created HDR surface\n");
      return VK_SUCCESS;
    }

    static VkResult GetPhysicalDeviceSurfaceFormatsKHR(
        const vkroots::VkInstanceDispatch *pDispatch,
        VkPhysicalDevice physicalDevice,
        VkSurfaceKHR surface,
        uint32_t *pSurfaceFormatCount,
        VkSurfaceFormatKHR *pSurfaceFormats)
    {
      auto hdrSurface = HdrSurface::get(surface);
      if (!hdrSurface)
        return pDispatch->GetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, pSurfaceFormatCount, pSurfaceFormats);

      uint32_t count = 0;
      auto result = pDispatch->GetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, nullptr);
      if (result != VK_SUCCESS)
      {
        return result;
      }
      std::vector<VkSurfaceFormatKHR> formats(count);
      result = pDispatch->GetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &count, formats.data());
      if (result != VK_SUCCESS)
      {
        return result;
      }

      std::vector<VkSurfaceFormatKHR> extraFormats = {};
      for (const auto &desc : s_ExtraHDRSurfaceFormats)
      {
        const bool hasFormat = std::ranges::any_of(formats, [&desc](const VkSurfaceFormatKHR fmt) {
          return desc.surface.surfaceFormat.format == fmt.format;
        });
        if (hasFormat)
        {
          fprintf(stderr, "[HDR Layer] Enabling format: %u colorspace: %u\n", desc.surface.surfaceFormat.format, desc.surface.surfaceFormat.colorSpace);
          extraFormats.push_back(desc.surface.surfaceFormat);
        }
      }
      /*
      // Can we use the preferred description for this?
      // We don't get output_enter events and even if, they are not enough
      // to figure out which wl_output color_description we want.
      extraFormats.push_back({
          VK_FORMAT_A2R10G10B10_UNORM_PACK32,
          VK_COLOR_SPACE_PASS_THROUGH_EXT,
      });
      extraFormats.push_back({
          VK_FORMAT_A2B10G10R10_UNORM_PACK32,
          VK_COLOR_SPACE_PASS_THROUGH_EXT,
      });
      extraFormats.push_back({
          VK_FORMAT_R16G16B16A16_SFLOAT,
          VK_COLOR_SPACE_PASS_THROUGH_EXT,
      });
      */

      return vkroots::helpers::append(
          pDispatch->GetPhysicalDeviceSurfaceFormatsKHR,
          extraFormats,
          pSurfaceFormatCount,
          pSurfaceFormats,
          physicalDevice,
          surface);
    }

    static VkResult GetPhysicalDeviceSurfaceFormats2KHR(
        const vkroots::VkInstanceDispatch *pDispatch,
        VkPhysicalDevice physicalDevice,
        const VkPhysicalDeviceSurfaceInfo2KHR *pSurfaceInfo,
        uint32_t *pSurfaceFormatCount,
        VkSurfaceFormat2KHR *pSurfaceFormats)
    {
      auto hdrSurface = HdrSurface::get(pSurfaceInfo->surface);
      if (!hdrSurface)
        return pDispatch->GetPhysicalDeviceSurfaceFormats2KHR(physicalDevice, pSurfaceInfo, pSurfaceFormatCount, pSurfaceFormats);

      uint32_t count = 0;
      auto result = pDispatch->GetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, pSurfaceInfo->surface, &count, nullptr);
      if (result != VK_SUCCESS)
      {
        return result;
      }
      std::vector<VkSurfaceFormatKHR> formats(count);
      result = pDispatch->GetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, pSurfaceInfo->surface, &count, formats.data());
      if (result != VK_SUCCESS)
      {
        return result;
      }

      std::vector<VkSurfaceFormat2KHR> extraFormats = {};
      for (const auto &desc : s_ExtraHDRSurfaceFormats)
      {
        const bool hasFormat = std::ranges::any_of(formats, [&desc](const VkSurfaceFormatKHR fmt) {
          return desc.surface.surfaceFormat.format == fmt.format;
        });
        if (hasFormat)
        {
          fprintf(stderr, "[HDR Layer] Enabling format: %u colorspace: %u\n", desc.surface.surfaceFormat.format, desc.surface.surfaceFormat.colorSpace);
          extraFormats.push_back(desc.surface);
        }
      }
      /*
      // Can we use the preferred description for this?
      // We don't get output_enter events and even if, they are not enough
      // to figure out which wl_output color_description we want.
      extraFormats.push_back({.surfaceFormat = {
                                  VK_FORMAT_A2R10G10B10_UNORM_PACK32,
                                  VK_COLOR_SPACE_PASS_THROUGH_EXT,
                              }});
      extraFormats.push_back({.surfaceFormat = {
                                  VK_FORMAT_A2B10G10R10_UNORM_PACK32,
                                  VK_COLOR_SPACE_PASS_THROUGH_EXT,
                              }});
      extraFormats.push_back({.surfaceFormat = {
                                  VK_FORMAT_R16G16B16A16_SFLOAT,
                                  VK_COLOR_SPACE_PASS_THROUGH_EXT,
                              }});
      */

      return vkroots::helpers::append(
          pDispatch->GetPhysicalDeviceSurfaceFormats2KHR,
          extraFormats,
          pSurfaceFormatCount,
          pSurfaceFormats,
          physicalDevice,
          pSurfaceInfo);
    }

    static void DestroySurfaceKHR(
        const vkroots::VkInstanceDispatch *pDispatch,
        VkInstance instance,
        VkSurfaceKHR surface,
        const VkAllocationCallbacks *pAllocator)
    {
      if (auto state = HdrSurface::get(surface))
      {
        xx_color_management_surface_v4_destroy(state->colorSurface);
        xx_color_manager_v4_destroy(state->colorManagement);
        wl_event_queue_destroy(state->queue);
      }
      HdrSurface::remove(surface);
      pDispatch->DestroySurfaceKHR(instance, surface, pAllocator);
    }

    static VkResult
    EnumerateDeviceExtensionProperties(
        const vkroots::VkInstanceDispatch *pDispatch,
        VkPhysicalDevice physicalDevice,
        const char *pLayerName,
        uint32_t *pPropertyCount,
        VkExtensionProperties *pProperties)
    {
      static constexpr std::array<VkExtensionProperties, 1> s_LayerExposedExts = {{
          {VK_EXT_HDR_METADATA_EXTENSION_NAME,
           VK_EXT_HDR_METADATA_SPEC_VERSION},
      }};

      if (pLayerName)
      {
        if (pLayerName == "VK_LAYER_hdr_wsi"sv)
        {
          return vkroots::helpers::array(s_LayerExposedExts, pPropertyCount, pProperties);
        }
        else
        {
          return pDispatch->EnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pPropertyCount, pProperties);
        }
      }

      return vkroots::helpers::append(
          pDispatch->EnumerateDeviceExtensionProperties,
          s_LayerExposedExts,
          pPropertyCount,
          pProperties,
          physicalDevice,
          pLayerName);
    }

  private:
    static constexpr struct xx_color_manager_v4_listener color_interface_listener
    {
      .supported_intent = [](void *data,
                             struct xx_color_manager_v4 *xx_color_manager_V4,
                             uint32_t render_intent) {},
      .supported_feature = [](void *data,
                              struct xx_color_manager_v4 *xx_color_manager_V4,
                              uint32_t feature)
      {
        auto surface = reinterpret_cast<HdrSurfaceData *>(data);
        surface->features.push_back(feature);
      },
      .supported_tf_named = [](void *data, struct xx_color_manager_v4 *xx_color_manager_V4, uint32_t tf_code)
      {
        auto surface = reinterpret_cast<HdrSurfaceData *>(data);
        surface->tf_named.push_back(tf_code);
      },
      .supported_primaries_named = [](void *data, struct xx_color_manager_v4 *xx_color_manager_V4, uint32_t primaries_code)
      {
        auto surface = reinterpret_cast<HdrSurfaceData *>(data);
        surface->primaries_named.push_back(primaries_code);
      }
    };

    static constexpr wl_registry_listener s_registryListener = {
        .global = [](void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
        {
          auto surface = reinterpret_cast<HdrSurfaceData *>(data);

          if (interface == "xx_color_manager_v4"sv)
          {
            surface->colorManagement = reinterpret_cast<xx_color_manager_v4 *>(
              wl_registry_bind(registry, name, &xx_color_manager_v4_interface, version));
            xx_color_manager_v4_add_listener(surface->colorManagement, &color_interface_listener, data);
          }
        },
        .global_remove = [](void *data, wl_registry *registry, uint32_t name) {},
    };
  };

  class VkDeviceOverrides
  {
  public:
    static void DestroySwapchainKHR(
        const vkroots::VkDeviceDispatch *pDispatch,
        VkDevice device,
        VkSwapchainKHR swapchain,
        const VkAllocationCallbacks *pAllocator)
    {
      HdrSwapchain::remove(swapchain);
      pDispatch->DestroySwapchainKHR(device, swapchain, pAllocator);
    }

    static VkResult CreateSwapchainKHR(
        const vkroots::VkDeviceDispatch *pDispatch,
        VkDevice device,
        const VkSwapchainCreateInfoKHR *pCreateInfo,
        const VkAllocationCallbacks *pAllocator,
        VkSwapchainKHR *pSwapchain)
    {
      auto hdrSurface = HdrSurface::get(pCreateInfo->surface);
      if (!hdrSurface)
        return pDispatch->CreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);

      VkSwapchainCreateInfoKHR swapchainInfo = *pCreateInfo;

      if (hdrSurface)
      {
        // If this is a custom surface
        // Force the colorspace to sRGB before sending to the driver.
        swapchainInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

        fprintf(stderr, "[HDR Layer] Creating swapchain for id: %u - format: %s - colorspace: %s\n",
                wl_proxy_get_id(reinterpret_cast<struct wl_proxy *>(hdrSurface->surface)),
                vkroots::helpers::enumString(pCreateInfo->imageFormat),
                vkroots::helpers::enumString(pCreateInfo->imageColorSpace));
      }

      // Check for VkFormat support and return VK_ERROR_INITIALIZATION_FAILED
      // if that VkFormat is unsupported for the underlying surface.
      {
        std::vector<VkSurfaceFormatKHR> supportedSurfaceFormats;
        vkroots::helpers::enumerate(
            pDispatch->pPhysicalDeviceDispatch->pInstanceDispatch->GetPhysicalDeviceSurfaceFormatsKHR,
            supportedSurfaceFormats,
            pDispatch->PhysicalDevice,
            swapchainInfo.surface);

        bool supportedSwapchainFormat = std::find_if(
                                            supportedSurfaceFormats.begin(),
                                            supportedSurfaceFormats.end(),
                                            [=](VkSurfaceFormatKHR value)
                                            { return value.format == swapchainInfo.imageFormat; }) != supportedSurfaceFormats.end();

        if (!supportedSwapchainFormat)
        {
          fprintf(stderr, "[HDR Layer] Refusing to make swapchain (unsupported VkFormat) for id: %u - format: %s - colorspace: %s\n",
                  wl_proxy_get_id(reinterpret_cast<struct wl_proxy *>(hdrSurface->surface)),
                  vkroots::helpers::enumString(pCreateInfo->imageFormat),
                  vkroots::helpers::enumString(pCreateInfo->imageColorSpace));

          return VK_ERROR_INITIALIZATION_FAILED;
        }
      }

      VkResult result = pDispatch->CreateSwapchainKHR(device, &swapchainInfo, pAllocator, pSwapchain);
      if (hdrSurface && result == VK_SUCCESS)
      {
        if (pCreateInfo->compositeAlpha == VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
        {
          // FIXME
          //wp_color_representation_v1_set_alpha_mode(hdrSurface->colorRepresentation, WP_COLOR_REPRESENTATION_V1_ALPHA_MODE_PREMULTIPLIED_ELECTRICAL);
        }
        else if (pCreateInfo->compositeAlpha == VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR)
        {
        // FIXME
          //wp_color_representation_v1_set_alpha_mode(hdrSurface->colorRepresentation, WP_COLOR_REPRESENTATION_V1_ALPHA_MODE_STRAIGHT);
        }

        auto primaries = 0;
        auto tf = 0;
        for (auto desc = s_ExtraHDRSurfaceFormats.begin(); desc != s_ExtraHDRSurfaceFormats.end(); ++desc)
        {
          if (desc->surface.surfaceFormat.colorSpace == pCreateInfo->imageColorSpace)
          {
            primaries = desc->primaries_named;
            tf = desc->tf_named;
            break;
          }
        }

        if (primaries == 0 && tf == 0 && pCreateInfo->imageColorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR && pCreateInfo->imageColorSpace != VK_COLOR_SPACE_PASS_THROUGH_EXT)
        {
          fprintf(stderr, "[HDR Layer] Unknown color space, assuming untagged");
        };

        /*
        if (pCreateInfo->imageColorSpace == VK_COLOR_SPACE_PASS_THROUGH_EXT) {
          // just use preferred? Ideally would like to use the most overlapping wl_outputs color description here
        }
        */

        xx_image_description_v4 *desc = nullptr;

        if (primaries != 0 && tf != 0)
        {
          auto status = DescStatus::WAITING;
          xx_image_description_creator_params_v4 *params = xx_color_manager_v4_new_parametric_creator(hdrSurface->colorManagement);
          xx_image_description_creator_params_v4_set_primaries_named(params, primaries);
          xx_image_description_creator_params_v4_set_tf_named(params, tf);
          desc = xx_image_description_creator_params_v4_create(params);
          xx_image_description_v4_add_listener(desc, &image_description_interface_listener, &status);
          while (status == DescStatus::WAITING)
          {
            wl_display_roundtrip_queue(hdrSurface->display, hdrSurface->queue);
          }
          if (status == DescStatus::FAILED)
          {
            fprintf(stderr, "[HDR Layer] Failed to create image description, failing swapchain creation");
            return VK_ERROR_INITIALIZATION_FAILED;
          }
        }
        else
        {
          wl_display_roundtrip_queue(hdrSurface->display, hdrSurface->queue); // send alpha mode
        }

        HdrSwapchain::create(*pSwapchain, HdrSwapchainData{
                                              .surface = pCreateInfo->surface,
                                              .primaries = primaries,
                                              .tf = tf,
                                              .colorDescription = desc,
                                              .desc_dirty = true,
                                          });
      }
      return result;
    }

    static void
    SetHdrMetadataEXT(
        const vkroots::VkDeviceDispatch *pDispatch,
        VkDevice device,
        uint32_t swapchainCount,
        const VkSwapchainKHR *pSwapchains,
        const VkHdrMetadataEXT *pMetadata)
    {
      //FIXME!
      return;

      for (uint32_t i = 0; i < swapchainCount; i++)
      {
        auto hdrSwapchain = HdrSwapchain::get(pSwapchains[i]);
        if (!hdrSwapchain)
        {
          fprintf(stderr, "[HDR Layer] SetHdrMetadataEXT: Swapchain %u does not support HDR.\n", i);
          continue;
        }

        auto hdrSurface = HdrSurface::get(hdrSwapchain->surface);
        if (!hdrSurface)
        {
          fprintf(stderr, "[HDR Layer] SetHdrMetadataEXT: Surface for swapchain %u was already destroyed. (App use after free).\n", i);
          abort();
          continue;
        }

        const VkHdrMetadataEXT &metadata = pMetadata[i];
        xx_image_description_creator_params_v4 *params = xx_color_manager_v4_new_parametric_creator(hdrSurface->colorManagement);
        xx_image_description_creator_params_v4_set_mastering_display_primaries(
            params,
            (uint32_t)round(metadata.displayPrimaryRed.x * 10000.0),
            (uint32_t)round(metadata.displayPrimaryRed.y * 10000.0),
            (uint32_t)round(metadata.displayPrimaryGreen.x * 10000.0),
            (uint32_t)round(metadata.displayPrimaryGreen.y * 10000.0),
            (uint32_t)round(metadata.displayPrimaryBlue.x * 10000.0),
            (uint32_t)round(metadata.displayPrimaryBlue.y * 10000.0),
            (uint32_t)round(metadata.whitePoint.x * 10000.0),
            (uint32_t)round(metadata.whitePoint.y * 10000.0));
        xx_image_description_creator_params_v4_set_mastering_luminance(
            params,
            (uint32_t)round(metadata.minLuminance * 10000.0),
            (uint32_t)round(metadata.maxLuminance));
        xx_image_description_creator_params_v4_set_primaries_named(params, hdrSwapchain->primaries);
        xx_image_description_creator_params_v4_set_tf_named(params, hdrSwapchain->tf);
        xx_image_description_creator_params_v4_set_max_cll(params, (uint32_t)round(metadata.maxContentLightLevel));
        xx_image_description_creator_params_v4_set_max_fall(params, (uint32_t)round(metadata.maxFrameAverageLightLevel));

        auto status = DescStatus::WAITING;
        xx_image_description_v4 *desc = xx_image_description_creator_params_v4_create(params);
        xx_image_description_v4_add_listener(desc, &image_description_interface_listener, &status);
        while (status == DescStatus::WAITING)
        {
          wl_display_roundtrip_queue(hdrSurface->display, hdrSurface->queue);
        }
        if (status == DescStatus::FAILED)
        {
          fprintf(stderr, "[HDR Layer] Failed to create new image description for new metadata!");
        }
        else
        {
          fprintf(stderr, "[HDR Layer] VkHdrMetadataEXT: mastering luminance min %f nits, max %f nits\n", metadata.minLuminance, metadata.maxLuminance);
          fprintf(stderr, "[HDR Layer] VkHdrMetadataEXT: maxContentLightLevel %f nits\n", metadata.maxContentLightLevel);
          fprintf(stderr, "[HDR Layer] VkHdrMetadataEXT: maxFrameAverageLightLevel %f nits\n", metadata.maxFrameAverageLightLevel);

          hdrSwapchain->colorDescription = desc;
          hdrSwapchain->desc_dirty = true;
        }
      }
    }

    static VkResult QueuePresentKHR(
        const vkroots::VkDeviceDispatch *pDispatch,
        VkQueue queue,
        const VkPresentInfoKHR *pPresentInfo)
    {
      for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++)
      {
        if (auto hdrSwapchain = HdrSwapchain::get(pPresentInfo->pSwapchains[i]))
        {
          if (hdrSwapchain->desc_dirty)
          {
            auto hdrSurface = HdrSurface::get(hdrSwapchain->surface);
            if (hdrSwapchain->colorDescription)
            {
              xx_color_management_surface_v4_set_image_description(hdrSurface->colorSurface, hdrSwapchain->colorDescription, XX_COLOR_MANAGER_V4_RENDER_INTENT_PERCEPTUAL);
            }
            else
            {
              xx_color_management_surface_v4_unset_image_description(hdrSurface->colorSurface);
            }
            hdrSwapchain->desc_dirty = false;
          }
        }
      }

      return pDispatch->QueuePresentKHR(queue, pPresentInfo);
    }

  private:
    static constexpr struct xx_image_description_v4_listener image_description_interface_listener
    {
      .failed = [](
                    void *data,
                    struct xx_image_description_v4 *xx_image_description_V4,
                    uint32_t cause,
                    const char *msg)
      {
        fprintf(stderr, "[HDR Layer] Image description failed: Cause %u, message: %s.\n", cause, msg);
        auto state = reinterpret_cast<enum DescStatus *>(data);
        *state = DescStatus::FAILED;
      },
      .ready = [](void *data, struct xx_image_description_v4 *xx_image_description_V4, uint32_t identity)
      {
        auto state = reinterpret_cast<enum DescStatus *>(data);
        *state = DescStatus::READY;
      }
      // we don't call get_information, so the rest should never be called
    };
  };
}

VKROOTS_DEFINE_LAYER_INTERFACES(HdrLayer::VkInstanceOverrides,
                                vkroots::NoOverrides,
                                HdrLayer::VkDeviceOverrides);

VKROOTS_IMPLEMENT_SYNCHRONIZED_MAP_TYPE(HdrLayer::HdrSurface);
VKROOTS_IMPLEMENT_SYNCHRONIZED_MAP_TYPE(HdrLayer::HdrSwapchain);
