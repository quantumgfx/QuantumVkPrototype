#include "texture_format.hpp"
#include "format.hpp"
#include <algorithm>

using namespace std;

namespace Vulkan
{
	uint32_t TextureFormatLayout::NumMiplevels(uint32_t width, uint32_t height, uint32_t depth)
	{
		uint32_t size = unsigned(max(max(width, height), depth));
		uint32_t levels = 0;
		while (size)
		{
			levels++;
			size >>= 1;
		}
		return levels;
	}

	void TextureFormatLayout::FormatBlockDim(VkFormat format, uint32_t& width, uint32_t& height)
	{
#define fmt(x, w, h)     \
    case VK_FORMAT_##x: \
        width = w; \
        height = h; \
        break

		switch (format)
		{
			fmt(ETC2_R8G8B8A8_UNORM_BLOCK, 4, 4);
			fmt(ETC2_R8G8B8A8_SRGB_BLOCK, 4, 4);
			fmt(ETC2_R8G8B8A1_UNORM_BLOCK, 4, 4);
			fmt(ETC2_R8G8B8A1_SRGB_BLOCK, 4, 4);
			fmt(ETC2_R8G8B8_UNORM_BLOCK, 4, 4);
			fmt(ETC2_R8G8B8_SRGB_BLOCK, 4, 4);
			fmt(EAC_R11_UNORM_BLOCK, 4, 4);
			fmt(EAC_R11_SNORM_BLOCK, 4, 4);
			fmt(EAC_R11G11_UNORM_BLOCK, 4, 4);
			fmt(EAC_R11G11_SNORM_BLOCK, 4, 4);

			fmt(BC1_RGB_UNORM_BLOCK, 4, 4);
			fmt(BC1_RGB_SRGB_BLOCK, 4, 4);
			fmt(BC1_RGBA_UNORM_BLOCK, 4, 4);
			fmt(BC1_RGBA_SRGB_BLOCK, 4, 4);
			fmt(BC2_UNORM_BLOCK, 4, 4);
			fmt(BC2_SRGB_BLOCK, 4, 4);
			fmt(BC3_UNORM_BLOCK, 4, 4);
			fmt(BC3_SRGB_BLOCK, 4, 4);
			fmt(BC4_UNORM_BLOCK, 4, 4);
			fmt(BC4_SNORM_BLOCK, 4, 4);
			fmt(BC5_UNORM_BLOCK, 4, 4);
			fmt(BC5_SNORM_BLOCK, 4, 4);
			fmt(BC6H_UFLOAT_BLOCK, 4, 4);
			fmt(BC6H_SFLOAT_BLOCK, 4, 4);
			fmt(BC7_SRGB_BLOCK, 4, 4);
			fmt(BC7_UNORM_BLOCK, 4, 4);

			fmt(ASTC_4x4_SRGB_BLOCK, 4, 4);
			fmt(ASTC_5x4_SRGB_BLOCK, 5, 4);
			fmt(ASTC_5x5_SRGB_BLOCK, 5, 5);
			fmt(ASTC_6x5_SRGB_BLOCK, 6, 5);
			fmt(ASTC_6x6_SRGB_BLOCK, 6, 6);
			fmt(ASTC_8x5_SRGB_BLOCK, 8, 5);
			fmt(ASTC_8x6_SRGB_BLOCK, 8, 6);
			fmt(ASTC_8x8_SRGB_BLOCK, 8, 8);
			fmt(ASTC_10x5_SRGB_BLOCK, 10, 5);
			fmt(ASTC_10x6_SRGB_BLOCK, 10, 6);
			fmt(ASTC_10x8_SRGB_BLOCK, 10, 8);
			fmt(ASTC_10x10_SRGB_BLOCK, 10, 10);
			fmt(ASTC_12x10_SRGB_BLOCK, 12, 10);
			fmt(ASTC_12x12_SRGB_BLOCK, 12, 12);
			fmt(ASTC_4x4_UNORM_BLOCK, 4, 4);
			fmt(ASTC_5x4_UNORM_BLOCK, 5, 4);
			fmt(ASTC_5x5_UNORM_BLOCK, 5, 5);
			fmt(ASTC_6x5_UNORM_BLOCK, 6, 5);
			fmt(ASTC_6x6_UNORM_BLOCK, 6, 6);
			fmt(ASTC_8x5_UNORM_BLOCK, 8, 5);
			fmt(ASTC_8x6_UNORM_BLOCK, 8, 6);
			fmt(ASTC_8x8_UNORM_BLOCK, 8, 8);
			fmt(ASTC_10x5_UNORM_BLOCK, 10, 5);
			fmt(ASTC_10x6_UNORM_BLOCK, 10, 6);
			fmt(ASTC_10x8_UNORM_BLOCK, 10, 8);
			fmt(ASTC_10x10_UNORM_BLOCK, 10, 10);
			fmt(ASTC_12x10_UNORM_BLOCK, 12, 10);
			fmt(ASTC_12x12_UNORM_BLOCK, 12, 12);

		default:
			width = 1;
			height = 1;
			break;
		}

#undef fmt
	}

	uint32_t TextureFormatLayout::FormatBlockSize(VkFormat format, VkImageAspectFlags aspect)
	{
#define fmt(x, bpp)     \
    case VK_FORMAT_##x: \
        return bpp

#define fmt2(x, bpp0, bpp1) \
	case VK_FORMAT_##x:     \
		return aspect == VK_IMAGE_ASPECT_PLANE_0_BIT ? bpp0 : bpp1

		switch (format)
		{
			fmt(R4G4_UNORM_PACK8, 1);
			fmt(R4G4B4A4_UNORM_PACK16, 2);
			fmt(B4G4R4A4_UNORM_PACK16, 2);
			fmt(R5G6B5_UNORM_PACK16, 2);
			fmt(B5G6R5_UNORM_PACK16, 2);
			fmt(R5G5B5A1_UNORM_PACK16, 2);
			fmt(B5G5R5A1_UNORM_PACK16, 2);
			fmt(A1R5G5B5_UNORM_PACK16, 2);
			fmt(R8_UNORM, 1);
			fmt(R8_SNORM, 1);
			fmt(R8_USCALED, 1);
			fmt(R8_SSCALED, 1);
			fmt(R8_UINT, 1);
			fmt(R8_SINT, 1);
			fmt(R8_SRGB, 1);
			fmt(R8G8_UNORM, 2);
			fmt(R8G8_SNORM, 2);
			fmt(R8G8_USCALED, 2);
			fmt(R8G8_SSCALED, 2);
			fmt(R8G8_UINT, 2);
			fmt(R8G8_SINT, 2);
			fmt(R8G8_SRGB, 2);
			fmt(R8G8B8_UNORM, 3);
			fmt(R8G8B8_SNORM, 3);
			fmt(R8G8B8_USCALED, 3);
			fmt(R8G8B8_SSCALED, 3);
			fmt(R8G8B8_UINT, 3);
			fmt(R8G8B8_SINT, 3);
			fmt(R8G8B8_SRGB, 3);
			fmt(R8G8B8A8_UNORM, 4);
			fmt(R8G8B8A8_SNORM, 4);
			fmt(R8G8B8A8_USCALED, 4);
			fmt(R8G8B8A8_SSCALED, 4);
			fmt(R8G8B8A8_UINT, 4);
			fmt(R8G8B8A8_SINT, 4);
			fmt(R8G8B8A8_SRGB, 4);
			fmt(B8G8R8A8_UNORM, 4);
			fmt(B8G8R8A8_SNORM, 4);
			fmt(B8G8R8A8_USCALED, 4);
			fmt(B8G8R8A8_SSCALED, 4);
			fmt(B8G8R8A8_UINT, 4);
			fmt(B8G8R8A8_SINT, 4);
			fmt(B8G8R8A8_SRGB, 4);
			fmt(A8B8G8R8_UNORM_PACK32, 4);
			fmt(A8B8G8R8_SNORM_PACK32, 4);
			fmt(A8B8G8R8_USCALED_PACK32, 4);
			fmt(A8B8G8R8_SSCALED_PACK32, 4);
			fmt(A8B8G8R8_UINT_PACK32, 4);
			fmt(A8B8G8R8_SINT_PACK32, 4);
			fmt(A8B8G8R8_SRGB_PACK32, 4);
			fmt(A2B10G10R10_UNORM_PACK32, 4);
			fmt(A2B10G10R10_SNORM_PACK32, 4);
			fmt(A2B10G10R10_USCALED_PACK32, 4);
			fmt(A2B10G10R10_SSCALED_PACK32, 4);
			fmt(A2B10G10R10_UINT_PACK32, 4);
			fmt(A2B10G10R10_SINT_PACK32, 4);
			fmt(A2R10G10B10_UNORM_PACK32, 4);
			fmt(A2R10G10B10_SNORM_PACK32, 4);
			fmt(A2R10G10B10_USCALED_PACK32, 4);
			fmt(A2R10G10B10_SSCALED_PACK32, 4);
			fmt(A2R10G10B10_UINT_PACK32, 4);
			fmt(A2R10G10B10_SINT_PACK32, 4);
			fmt(R16_UNORM, 2);
			fmt(R16_SNORM, 2);
			fmt(R16_USCALED, 2);
			fmt(R16_SSCALED, 2);
			fmt(R16_UINT, 2);
			fmt(R16_SINT, 2);
			fmt(R16_SFLOAT, 2);
			fmt(R16G16_UNORM, 4);
			fmt(R16G16_SNORM, 4);
			fmt(R16G16_USCALED, 4);
			fmt(R16G16_SSCALED, 4);
			fmt(R16G16_UINT, 4);
			fmt(R16G16_SINT, 4);
			fmt(R16G16_SFLOAT, 4);
			fmt(R16G16B16_UNORM, 6);
			fmt(R16G16B16_SNORM, 6);
			fmt(R16G16B16_USCALED, 6);
			fmt(R16G16B16_SSCALED, 6);
			fmt(R16G16B16_UINT, 6);
			fmt(R16G16B16_SINT, 6);
			fmt(R16G16B16_SFLOAT, 6);
			fmt(R16G16B16A16_UNORM, 8);
			fmt(R16G16B16A16_SNORM, 8);
			fmt(R16G16B16A16_USCALED, 8);
			fmt(R16G16B16A16_SSCALED, 8);
			fmt(R16G16B16A16_UINT, 8);
			fmt(R16G16B16A16_SINT, 8);
			fmt(R16G16B16A16_SFLOAT, 8);
			fmt(R32_UINT, 4);
			fmt(R32_SINT, 4);
			fmt(R32_SFLOAT, 4);
			fmt(R32G32_UINT, 8);
			fmt(R32G32_SINT, 8);
			fmt(R32G32_SFLOAT, 8);
			fmt(R32G32B32_UINT, 12);
			fmt(R32G32B32_SINT, 12);
			fmt(R32G32B32_SFLOAT, 12);
			fmt(R32G32B32A32_UINT, 16);
			fmt(R32G32B32A32_SINT, 16);
			fmt(R32G32B32A32_SFLOAT, 16);
			fmt(R64_UINT, 8);
			fmt(R64_SINT, 8);
			fmt(R64_SFLOAT, 8);
			fmt(R64G64_UINT, 16);
			fmt(R64G64_SINT, 16);
			fmt(R64G64_SFLOAT, 16);
			fmt(R64G64B64_UINT, 24);
			fmt(R64G64B64_SINT, 24);
			fmt(R64G64B64_SFLOAT, 24);
			fmt(R64G64B64A64_UINT, 32);
			fmt(R64G64B64A64_SINT, 32);
			fmt(R64G64B64A64_SFLOAT, 32);
			fmt(B10G11R11_UFLOAT_PACK32, 4);
			fmt(E5B9G9R9_UFLOAT_PACK32, 4);

			fmt(D16_UNORM, 2);
			fmt(X8_D24_UNORM_PACK32, 4);
			fmt(D32_SFLOAT, 4);
			fmt(S8_UINT, 1);

		case VK_FORMAT_D16_UNORM_S8_UINT:
			return aspect == VK_IMAGE_ASPECT_DEPTH_BIT ? 2 : 1;
		case VK_FORMAT_D24_UNORM_S8_UINT:
		case VK_FORMAT_D32_SFLOAT_S8_UINT:
			return aspect == VK_IMAGE_ASPECT_DEPTH_BIT ? 4 : 1;

			// ETC2
			fmt(ETC2_R8G8B8A8_UNORM_BLOCK, 16);
			fmt(ETC2_R8G8B8A8_SRGB_BLOCK, 16);
			fmt(ETC2_R8G8B8A1_UNORM_BLOCK, 8);
			fmt(ETC2_R8G8B8A1_SRGB_BLOCK, 8);
			fmt(ETC2_R8G8B8_UNORM_BLOCK, 8);
			fmt(ETC2_R8G8B8_SRGB_BLOCK, 8);
			fmt(EAC_R11_UNORM_BLOCK, 8);
			fmt(EAC_R11_SNORM_BLOCK, 8);
			fmt(EAC_R11G11_UNORM_BLOCK, 16);
			fmt(EAC_R11G11_SNORM_BLOCK, 16);

			// BC
			fmt(BC1_RGB_UNORM_BLOCK, 8);
			fmt(BC1_RGB_SRGB_BLOCK, 8);
			fmt(BC1_RGBA_UNORM_BLOCK, 8);
			fmt(BC1_RGBA_SRGB_BLOCK, 8);
			fmt(BC2_UNORM_BLOCK, 16);
			fmt(BC2_SRGB_BLOCK, 16);
			fmt(BC3_UNORM_BLOCK, 16);
			fmt(BC3_SRGB_BLOCK, 16);
			fmt(BC4_UNORM_BLOCK, 8);
			fmt(BC4_SNORM_BLOCK, 8);
			fmt(BC5_UNORM_BLOCK, 16);
			fmt(BC5_SNORM_BLOCK, 16);
			fmt(BC6H_UFLOAT_BLOCK, 16);
			fmt(BC6H_SFLOAT_BLOCK, 16);
			fmt(BC7_SRGB_BLOCK, 16);
			fmt(BC7_UNORM_BLOCK, 16);

			// ASTC
			fmt(ASTC_4x4_SRGB_BLOCK, 16);
			fmt(ASTC_5x4_SRGB_BLOCK, 16);
			fmt(ASTC_5x5_SRGB_BLOCK, 16);
			fmt(ASTC_6x5_SRGB_BLOCK, 16);
			fmt(ASTC_6x6_SRGB_BLOCK, 16);
			fmt(ASTC_8x5_SRGB_BLOCK, 16);
			fmt(ASTC_8x6_SRGB_BLOCK, 16);
			fmt(ASTC_8x8_SRGB_BLOCK, 16);
			fmt(ASTC_10x5_SRGB_BLOCK, 16);
			fmt(ASTC_10x6_SRGB_BLOCK, 16);
			fmt(ASTC_10x8_SRGB_BLOCK, 16);
			fmt(ASTC_10x10_SRGB_BLOCK, 16);
			fmt(ASTC_12x10_SRGB_BLOCK, 16);
			fmt(ASTC_12x12_SRGB_BLOCK, 16);
			fmt(ASTC_4x4_UNORM_BLOCK, 16);
			fmt(ASTC_5x4_UNORM_BLOCK, 16);
			fmt(ASTC_5x5_UNORM_BLOCK, 16);
			fmt(ASTC_6x5_UNORM_BLOCK, 16);
			fmt(ASTC_6x6_UNORM_BLOCK, 16);
			fmt(ASTC_8x5_UNORM_BLOCK, 16);
			fmt(ASTC_8x6_UNORM_BLOCK, 16);
			fmt(ASTC_8x8_UNORM_BLOCK, 16);
			fmt(ASTC_10x5_UNORM_BLOCK, 16);
			fmt(ASTC_10x6_UNORM_BLOCK, 16);
			fmt(ASTC_10x8_UNORM_BLOCK, 16);
			fmt(ASTC_10x10_UNORM_BLOCK, 16);
			fmt(ASTC_12x10_UNORM_BLOCK, 16);
			fmt(ASTC_12x12_UNORM_BLOCK, 16);

			fmt(G8B8G8R8_422_UNORM, 4);
			fmt(B8G8R8G8_422_UNORM, 4);

			fmt(G8_B8_R8_3PLANE_420_UNORM, 1);
			fmt2(G8_B8R8_2PLANE_420_UNORM, 1, 2);
			fmt(G8_B8_R8_3PLANE_422_UNORM, 1);
			fmt2(G8_B8R8_2PLANE_422_UNORM, 1, 2);
			fmt(G8_B8_R8_3PLANE_444_UNORM, 1);

			fmt(R10X6_UNORM_PACK16, 2);
			fmt(R10X6G10X6_UNORM_2PACK16, 4);
			fmt(R10X6G10X6B10X6A10X6_UNORM_4PACK16, 8);
			fmt(G10X6B10X6G10X6R10X6_422_UNORM_4PACK16, 8);
			fmt(B10X6G10X6R10X6G10X6_422_UNORM_4PACK16, 8);
			fmt(G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16, 2);
			fmt(G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16, 2);
			fmt(G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16, 2);
			fmt2(G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16, 2, 4);
			fmt2(G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16, 2, 4);

			fmt(R12X4_UNORM_PACK16, 2);
			fmt(R12X4G12X4_UNORM_2PACK16, 4);
			fmt(R12X4G12X4B12X4A12X4_UNORM_4PACK16, 8);
			fmt(G12X4B12X4G12X4R12X4_422_UNORM_4PACK16, 8);
			fmt(B12X4G12X4R12X4G12X4_422_UNORM_4PACK16, 8);
			fmt(G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16, 2);
			fmt(G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16, 2);
			fmt(G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16, 2);
			fmt2(G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16, 2, 4);
			fmt2(G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16, 2, 4);

			fmt(G16B16G16R16_422_UNORM, 8);
			fmt(B16G16R16G16_422_UNORM, 8);
			fmt(G16_B16_R16_3PLANE_420_UNORM, 2);
			fmt(G16_B16_R16_3PLANE_422_UNORM, 2);
			fmt(G16_B16_R16_3PLANE_444_UNORM, 2);
			fmt2(G16_B16R16_2PLANE_420_UNORM, 2, 4);
			fmt2(G16_B16R16_2PLANE_422_UNORM, 2, 4);

		default:
			assert(0 && "Unknown format.");
			return 0;
		}
#undef fmt
#undef fmt2
	}
}