/*
  Unity Capture
  Copyright (c) 2018 Bernhard Schelling

  Based on UnityCam
  https://github.com/mrayy/UnityCam
  Copyright (c) 2016 MHD Yamen Saraiji

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
	 claim that you wrote the original software. If you use this software
	 in a product, an acknowledgment in the product documentation would be
	 appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
	 misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#include "png.h"
#include "shared.inl"
#include <chrono>
#include <string>
#include <algorithm>
#include "IUnityGraphics.h"

enum
{
	RET_SUCCESS = 0,
	RET_WARNING_FRAMESKIP = 1,
	RET_WARNING_CAPTUREINACTIVE = 2,
	RET_ERROR_UNSUPPORTEDGRAPHICSDEVICE = 100,
	RET_ERROR_PARAMETER = 101,
	RET_ERROR_TOOLARGERESOLUTION = 102,
	RET_ERROR_TEXTUREFORMAT = 103,
	RET_ERROR_READTEXTURE = 104,
	RET_ERROR_READTEXTUREDATA = 105,
	RET_ERROR_TEXTUREHANDLE = 106,
};

#include <d3d11.h>
//#include <d3d12.h>
#include <glew.h>

#pragma comment(lib, "glew32.lib")

static int g_GraphicsDeviceType = -1;
static ID3D11Device* g_D3D11GraphicsDevice = 0;
//static ID3D12Device* g_D3D12GraphicsDevice = 0;


struct UnityCaptureInstance
{
	SharedImageMemory* Sender;
	void* TextureHandle;
	int Width, Height;
	SharedImageMemory::EFormat EFormat;

	void* cachedData_DIRECTSHOW = NULL;
	void* cachedData_SCREENSHOT = NULL;

	// DirectX11 stuff
	DXGI_FORMAT d3Format;
	ID3D11Texture2D* d3dtex;
	ID3D11DeviceContext* ctx;
	ID3D11Texture2D* Textures[2];

	// DirectX12 stuff
	// TODO

	bool UseDoubleBuffering, AlternativeBuffer, IsLinearColorSpace;
	SharedImageMemory::EResizeMode ResizeMode;
	SharedImageMemory::EMirrorMode MirrorMode;
	int Timeout;

	// Screenshot stuff
	const wchar_t* ss_fileName = NULL;

	int lastResult = RET_SUCCESS;

	~UnityCaptureInstance()
	{
		if (Sender)
		{
			delete Sender;
			Sender = NULL;
		}
		if (Textures[0])
		{
			Textures[0]->Release();
			Textures[0] = NULL;
		}
		if (Textures[1])
		{
			Textures[1]->Release();
			Textures[1] = NULL;
		}
		if (cachedData_DIRECTSHOW)
		{
			free(cachedData_DIRECTSHOW); /*alloc 1*/
			cachedData_DIRECTSHOW = NULL; /*alloc 1*/
		}
		if (cachedData_SCREENSHOT)
		{
			free(cachedData_SCREENSHOT); /*alloc 2*/
			cachedData_SCREENSHOT = NULL; /*alloc 2*/
		}
	}
};
static UnityCaptureInstance* g_captureInstance = NULL;

extern "C" __declspec(dllexport) UnityCaptureInstance* CaptureCreateInstance(int CapNum)
{
	UnityCaptureInstance* c = new UnityCaptureInstance();
	memset(c, 0, sizeof(UnityCaptureInstance));
	c->Sender = new SharedImageMemory(CapNum);
	return c;
}

extern "C" __declspec(dllexport) void CaptureDeleteInstance(UnityCaptureInstance* c)
{
	if (!c) return;
	delete c;
}

extern "C" __declspec(dllexport) void SetTextureFromUnity(UnityCaptureInstance* c, void* textureHandle, int Timeout, bool UseDoubleBuffering, SharedImageMemory::EResizeMode ResizeMode, SharedImageMemory::EMirrorMode MirrorMode, bool IsLinearColorSpace, int width, int height)
{
	if (!g_captureInstance || c->Width != width || c->Height != height || c->UseDoubleBuffering != UseDoubleBuffering || c->TextureHandle != textureHandle)
	{
		c->Width = width;
		c->Height = height;
		c->TextureHandle = textureHandle;
		c->UseDoubleBuffering = UseDoubleBuffering;
		c->IsLinearColorSpace = IsLinearColorSpace;
		c->MirrorMode = MirrorMode;
		c->ResizeMode = ResizeMode;
		c->Timeout = Timeout;
		if (g_GraphicsDeviceType == kUnityGfxRendererD3D11)
		{
			c->ctx = NULL;
			g_D3D11GraphicsDevice->GetImmediateContext(&c->ctx);
			if (!c->ctx)
			{
				c->lastResult = RET_ERROR_UNSUPPORTEDGRAPHICSDEVICE;
				c->TextureHandle = NULL; // Safety
				return;
			}

			c->d3dtex = (ID3D11Texture2D*)textureHandle;
			D3D11_TEXTURE2D_DESC desc = { 0 };
			c->d3dtex->GetDesc(&desc);

			//Allocate a Texture2D resource which holds the texture with CPU memory access
			D3D11_TEXTURE2D_DESC textureDesc;
			ZeroMemory(&textureDesc, sizeof(textureDesc));
			textureDesc.Width = desc.Width;
			textureDesc.Height = desc.Height;
			textureDesc.MipLevels = desc.MipLevels;
			textureDesc.ArraySize = 1;
			textureDesc.Format = desc.Format;
			c->d3Format = desc.Format;
			if (desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || desc.Format == DXGI_FORMAT_R8G8B8A8_UINT || desc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS)
			{
				c->EFormat = SharedImageMemory::FORMAT_UINT8;
			}
			else if (desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT || desc.Format == DXGI_FORMAT_R16G16B16A16_TYPELESS)
			{
				c->EFormat = (IsLinearColorSpace ? SharedImageMemory::FORMAT_FP16_LINEAR : SharedImageMemory::FORMAT_FP16_GAMMA);
			}
			textureDesc.SampleDesc.Count = 1;
			textureDesc.SampleDesc.Quality = 0;
			textureDesc.Usage = D3D11_USAGE_STAGING;
			textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			textureDesc.MiscFlags = 0;
			if (c->Textures[0])
			{
				c->Textures[0]->Release();
			}
			g_D3D11GraphicsDevice->CreateTexture2D(&textureDesc, NULL, &c->Textures[0]);
			if (c->Textures[1])
			{
				c->Textures[1]->Release();
			}
			if (UseDoubleBuffering)
			{
				g_D3D11GraphicsDevice->CreateTexture2D(&textureDesc, NULL, &c->Textures[1]);
			}
			else
			{
				c->Textures[1] = NULL;
			}
		}
		g_captureInstance = c;
	}
}

extern "C" __declspec(dllexport) void PrepareScreenshot(UnityCaptureInstance* c, void* textureHandle, int Timeout, bool UseDoubleBuffering, SharedImageMemory::EResizeMode ResizeMode, SharedImageMemory::EMirrorMode MirrorMode, bool IsLinearColorSpace, int width, int height, const wchar_t* fileName)
{
	SetTextureFromUnity(c, textureHandle, Timeout, UseDoubleBuffering, ResizeMode, MirrorMode, IsLinearColorSpace, width, height);
	if (g_captureInstance)
	{
		g_captureInstance->ss_fileName = fileName;
	}
}

extern "C" __declspec(dllexport) int GetLastResult()
{
	return g_captureInstance->lastResult;
}

static bool isOpenGL()
{
	return g_GraphicsDeviceType == kUnityGfxRendererOpenGL || g_GraphicsDeviceType == kUnityGfxRendererOpenGLCore;
}

static void Convert16To8_OpenGL(unsigned short* inputPtr, int height, int width, unsigned char* destPtr, int dataRowPitch)
{
	int destRowPitch = width * 8;
	int index = height - 1;
	int subIndex = 0;
	for (int i = 0; i < height; i++)
	{
		unsigned short* row = (unsigned short*)inputPtr + i * dataRowPitch;
		subIndex = 0;
		for (int j = 0; j < dataRowPitch; j++)
		{
			destPtr[index * destRowPitch + subIndex] = (unsigned char)(row[j] >> 8 & 255);
			subIndex++;
			destPtr[index * destRowPitch + subIndex] = (unsigned char)(row[j] & 255);
			subIndex++;
		}
		index--;
	}
}


// This method will setup the sender
static bool PreRenderEvent()
{
	bool result = true;

	if (!g_captureInstance)
	{
		g_captureInstance->lastResult = RET_WARNING_CAPTUREINACTIVE;
		result = false;;
	}
	if (result && g_GraphicsDeviceType == -1)
	{
		g_captureInstance->lastResult = RET_ERROR_UNSUPPORTEDGRAPHICSDEVICE;
		result = false;
	}
	if (result && !g_captureInstance->TextureHandle)
	{
		g_captureInstance->lastResult = RET_ERROR_TEXTUREHANDLE;
		result = false;
	}
	if (result && g_GraphicsDeviceType == kUnityGfxRendererD3D11)
	{
		if (!g_captureInstance->ctx || !g_captureInstance->d3dtex)
		{
			g_captureInstance->lastResult = RET_ERROR_PARAMETER;
			result = false;
		}
	}
	if (result && !g_captureInstance->Sender->SendIsReady())
	{
		g_captureInstance->lastResult = RET_WARNING_CAPTUREINACTIVE;
		result = false;
	}

	return result;
}

// Used for DirectX11 Rendering
static void UNITY_INTERFACE_API OnRenderEvent_D3D11(int eventID)
{
	if (!PreRenderEvent())
	{
		return;
	}

	D3D11_TEXTURE2D_DESC desc = { 0 };
	g_captureInstance->d3dtex->GetDesc(&desc);
	if (!desc.Width || !desc.Height)
	{
		g_captureInstance->lastResult = RET_ERROR_READTEXTURE;
		return;
	}

	//Handle double buffer
	if (g_captureInstance->UseDoubleBuffering)
	{
		g_captureInstance->AlternativeBuffer ^= 1;
	}
	ID3D11Texture2D* WriteTexture = g_captureInstance->Textures[g_captureInstance->UseDoubleBuffering &&  g_captureInstance->AlternativeBuffer ? 1 : 0];
	ID3D11Texture2D* ReadTexture = g_captureInstance->Textures[g_captureInstance->UseDoubleBuffering && !g_captureInstance->AlternativeBuffer ? 1 : 0];

	//Copy render texture to texture with CPU access and map the image data to RAM
	g_captureInstance->ctx->CopyResource(WriteTexture, g_captureInstance->d3dtex);
	D3D11_MAPPED_SUBRESOURCE mapResource;
	if (FAILED(g_captureInstance->ctx->Map(ReadTexture, 0, D3D11_MAP_READ, 0, &mapResource)))
	{
		g_captureInstance->lastResult = RET_ERROR_READTEXTURE;
		return;
	}

	//memcpy(m_pSharedBuf->data, buffer, DataSize);
	//Push the captured data to the direct show filter
	SharedImageMemory::ESendResult res = g_captureInstance->Sender->Send(desc.Width, desc.Height, mapResource.RowPitch / (g_captureInstance->EFormat == SharedImageMemory::FORMAT_UINT8 ? 4 : 8), mapResource.RowPitch * desc.Height, g_captureInstance->EFormat, g_captureInstance->ResizeMode, g_captureInstance->MirrorMode, g_captureInstance->Timeout, (const unsigned char*)mapResource.pData);

	g_captureInstance->ctx->Unmap(ReadTexture, 0);

	switch (res)
	{
	case SharedImageMemory::SENDRES_TOOLARGE:
		g_captureInstance->lastResult = RET_ERROR_TOOLARGERESOLUTION;
	case SharedImageMemory::SENDRES_WARN_FRAMESKIP:
		g_captureInstance->lastResult = RET_WARNING_FRAMESKIP;
	default:
		g_captureInstance->lastResult = RET_SUCCESS;
	}
}

// TODO: I am unable to test on my PC, unity is crashing when switching to D3D12... Need to test on another PC/Upgrade Untiy
static void UNITY_INTERFACE_API OnRenderEvent_D3D12(int eventID)
{

}

// Used for OpenGL rendering
static void UNITY_INTERFACE_API OnRenderEvent_OpenGL(int eventID)
{
	if (!PreRenderEvent()) // Setup sender and other checkups
	{
		return;
	}

	// Gets the GLtex from the texture handle
	GLuint gltex = (GLuint)g_captureInstance->TextureHandle;
	int error = GL_NO_ERROR;
	// Bind the texture to the GL_TEXTURE_2D
	glBindTexture(GL_TEXTURE_2D, gltex);
	error = glGetError();
	if (error != GL_NO_ERROR)
	{
		g_captureInstance->lastResult = RET_ERROR_READTEXTURE;
		return;
	}

	GLint format;
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
	error = glGetError();
	if (error != GL_NO_ERROR)
	{
		g_captureInstance->lastResult = RET_ERROR_TEXTUREFORMAT;
		return;
	}

	int rowPitch = g_captureInstance->Width * 4;
	g_captureInstance->EFormat = SharedImageMemory::FORMAT_UINT8;

	// I do not know how to handle Linear/gamma 16bit images

	// TODO : Check HDR, Add double buffering, Check Linear space, Check for the resize mode
	/* HDR:
		- HDR or not, it seems that GL_RGBA will works in any cases
	   COLOR SPACE:
		- Gamma or Linear => RGBA still works

		I am unable to setup directshow to accept 16bit openGL...
	*/

	// Gets the texture buffer for OpenGL ES, use glReadPixels
	if (!g_captureInstance->cachedData_DIRECTSHOW)
	{
		g_captureInstance->cachedData_DIRECTSHOW = malloc(sizeof(unsigned char) * g_captureInstance->Height * rowPitch); /*alloc 1*/
	}
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_captureInstance->cachedData_DIRECTSHOW);
	error = glGetError();
	if (error != GL_NO_ERROR)
	{
		g_captureInstance->lastResult = RET_ERROR_READTEXTUREDATA;
		return;
	}

	// Send the texture to the DirectShow device
	SharedImageMemory::ESendResult res = g_captureInstance->Sender->Send(g_captureInstance->Width, g_captureInstance->Height,
		g_captureInstance->Width,
		rowPitch * g_captureInstance->Height, g_captureInstance->EFormat, g_captureInstance->ResizeMode,
		g_captureInstance->MirrorMode, g_captureInstance->Timeout, (const unsigned char*)g_captureInstance->cachedData_DIRECTSHOW);

	switch (res)
	{
	case SharedImageMemory::SENDRES_TOOLARGE:
		g_captureInstance->lastResult = RET_ERROR_TOOLARGERESOLUTION;
	case SharedImageMemory::SENDRES_WARN_FRAMESKIP:
		g_captureInstance->lastResult = RET_WARNING_FRAMESKIP;
	default:
		g_captureInstance->lastResult = RET_SUCCESS;
	}
}

// Void rendering
static void UNITY_INTERFACE_API OnRenderEvent_Void(int eventID)
{
	g_captureInstance->lastResult = RET_ERROR_UNSUPPORTEDGRAPHICSDEVICE;
}

extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetRenderEventFunc()
{
	if (g_GraphicsDeviceType == kUnityGfxRendererD3D11)
	{
		return OnRenderEvent_D3D11;
	}
	else if (g_GraphicsDeviceType == kUnityGfxRendererD3D12)
	{
		return OnRenderEvent_D3D12;
	}
	else if (g_GraphicsDeviceType == kUnityGfxRendererOpenGL || g_GraphicsDeviceType == kUnityGfxRendererOpenGLCore)
	{
		return OnRenderEvent_OpenGL;
	}
	return OnRenderEvent_Void;
}


// SCREENSHOT SECTION


static void WriteToPNG(void* data, SharedImageMemory::EFormat format, int dataRowPitch/*This is the row pitch gathered fro the original data type. This is mainly for D3D11*/)
{
	FILE *file;
	_wfopen_s(&file, g_captureInstance->ss_fileName, L"wb");
	if (!file)
	{
		return;
	}

	png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png_ptr)
	{
		return;
	}

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr)
	{
		return;
	}

	if (setjmp(png_jmpbuf(png_ptr)))
	{
		return;
	}

	png_init_io(png_ptr, file);
	if (setjmp(png_jmpbuf(png_ptr)))
	{
		return;
	}

	int bit_depth = format == SharedImageMemory::FORMAT_UINT8 ? 8 : 16;
	int transform = PNG_TRANSFORM_IDENTITY;



	png_set_IHDR(png_ptr, info_ptr, g_captureInstance->Width, g_captureInstance->Height,
		bit_depth, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
		PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

	if (setjmp(png_jmpbuf(png_ptr)))
	{
		return;
	}
	png_init_io(png_ptr, file);

	unsigned char** row_pointers = NULL;
	unsigned char* convertedData = NULL;
	int numRows = g_captureInstance->Height;

	if (g_GraphicsDeviceType == kUnityGfxRendererD3D11)
	{
		int bpp = 32;
		if (bit_depth == 16)
		{
			bpp = 64; // For 16bits textures, the PNG bpp is 64
		}
		int rowBytes = (uint64_t(g_captureInstance->Width) * bpp + 7u) / 8u; // bytes per row
		uint64_t numBytes = rowBytes * g_captureInstance->Height; // total byte count

		row_pointers = (unsigned char**)malloc(sizeof(unsigned char*) * numRows); /*alloc 3*/
		//unsigned char *sptr = (unsigned char*)data;
		int msize = std::min<int>(rowBytes, dataRowPitch);

		int index = numRows - 1;

		for (int i = 0; i < numRows; ++i)
		{
			row_pointers[index] = (unsigned char*)malloc(sizeof(unsigned char) * rowBytes); /*alloc 4*/
			memcpy_s(row_pointers[index], rowBytes, (unsigned char*)data + dataRowPitch * i, msize);
			index--;
			//sptr += dataRowPitch;
		}
	}
	else
	{
		row_pointers = (unsigned char**)malloc(sizeof(unsigned char*) * numRows); /*alloc 3*/

		if (bit_depth == 8)
		{
			int index = 0;
			for (int i = numRows - 1; i >= 0; --i)
			{
				row_pointers[index] = (unsigned char *)data + i * dataRowPitch;
				index++;
			}
		}
		else
		{
			// We need to convert the unsigned short data into a correct unsigned char array
			convertedData = (unsigned char*)malloc(sizeof(unsigned char) * g_captureInstance->Width * g_captureInstance->Height * 4 * 2); /*alloc 5*/
			Convert16To8_OpenGL((unsigned short*)data, g_captureInstance->Height, g_captureInstance->Width, convertedData, dataRowPitch);

			int chunkSize = g_captureInstance->Width * 8;

			for (int i = 0; i < g_captureInstance->Height; i++)
			{
				row_pointers[i] = convertedData + i * (g_captureInstance->Width * 8);
			}
		}
	}

	png_set_rows(png_ptr, info_ptr, row_pointers);
	png_write_png(png_ptr, info_ptr, transform, NULL);

	fclose(file);

	if (g_GraphicsDeviceType == kUnityGfxRendererD3D11)
	{
		for (int i = 0; i < 1; ++i)
		{
			free(row_pointers[i]); /*alloc 4*/
			row_pointers[i] = NULL; /*alloc 4*/
		}
	}

	if (convertedData)
	{
		free(convertedData); /*alloc 5*/
		convertedData = NULL; /*alloc 5*/
	}
	if (row_pointers)
	{
		free(row_pointers); /*alloc 3*/
		row_pointers = NULL; /*alloc 3*/
	}
	if (setjmp(png_jmpbuf(png_ptr)))
	{
		return;
	}

}

static void UNITY_INTERFACE_API OnTakeScreenshotEvent_D3D11(int eventID)
{
	D3D11_TEXTURE2D_DESC desc = { 0 };
	g_captureInstance->d3dtex->GetDesc(&desc);
	if (!desc.Width || !desc.Height)
	{
		g_captureInstance->lastResult = RET_ERROR_READTEXTURE;
		return;
	}

	ID3D11Texture2D* WriteTexture = g_captureInstance->Textures[0];
	ID3D11Texture2D* ReadTexture = g_captureInstance->Textures[0];

	//Copy render texture to texture with CPU access and map the image data to RAM
	g_captureInstance->ctx->CopyResource(WriteTexture, g_captureInstance->d3dtex);
	D3D11_MAPPED_SUBRESOURCE mapResource;
	if (FAILED(g_captureInstance->ctx->Map(ReadTexture, 0, D3D11_MAP_READ, 0, &mapResource)))
	{
		g_captureInstance->lastResult = RET_ERROR_READTEXTURE;
		return;
	}
	WriteToPNG(mapResource.pData, g_captureInstance->EFormat, mapResource.RowPitch);

	g_captureInstance->ctx->Unmap(ReadTexture, 0);

	g_captureInstance->lastResult = RET_SUCCESS;
}

/// Saves an OpenGL buffer to a png
/// Works well with 8bit depth [Gamma and Linear]
/// Have some issues with the 16bit depth with Linear mode (it's darker than it should be).. This needs investigations
static void UNITY_INTERFACE_API OnTakeScreenshotEvent_OpenGL(int eventID)
{
	GLuint gltex = (GLuint)(size_t)(g_captureInstance->TextureHandle);
	int error = GL_NO_ERROR;
	glBindTexture(GL_TEXTURE_2D, gltex);
	error = glGetError();
	if (error != GL_NO_ERROR)
	{
		g_captureInstance->lastResult = RET_ERROR_READTEXTURE;
		return;
	}

	GLint format;
	glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
	error = glGetError();
	if (error != GL_NO_ERROR)
	{
		g_captureInstance->lastResult = RET_ERROR_TEXTUREFORMAT;
		return;
	}

	int rowPitch = g_captureInstance->Width * 4;

	if (format == GL_RGBA16F_EXT || format == GL_RGBA16 || format == GL_RGBA16F)
	{
		g_captureInstance->EFormat = SharedImageMemory::FORMAT_FP16_GAMMA;
		if (!g_captureInstance->cachedData_SCREENSHOT)
		{
			g_captureInstance->cachedData_SCREENSHOT = malloc(sizeof(unsigned short) * rowPitch * g_captureInstance->Height * 2); /*alloc_2*/
		}
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_SHORT, g_captureInstance->cachedData_SCREENSHOT);
		error = glGetError();
		if (error != GL_NO_ERROR)
		{
			g_captureInstance->lastResult = RET_ERROR_READTEXTUREDATA;
			return;
		}

		WriteToPNG(g_captureInstance->cachedData_SCREENSHOT, g_captureInstance->EFormat, rowPitch);

		g_captureInstance->lastResult = RET_SUCCESS;
	}
	else
	{
		g_captureInstance->EFormat = SharedImageMemory::FORMAT_UINT8;
		if (!g_captureInstance->cachedData_SCREENSHOT)
		{
			g_captureInstance->cachedData_SCREENSHOT = malloc(sizeof(unsigned char) * g_captureInstance->Height * rowPitch); /*alloc 2*/
		}
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_captureInstance->cachedData_SCREENSHOT);
		error = glGetError();
		if (error != GL_NO_ERROR)
		{
			g_captureInstance->lastResult = RET_ERROR_READTEXTUREDATA;
			return;
		}

		// Save the file here
		WriteToPNG(g_captureInstance->cachedData_SCREENSHOT, g_captureInstance->EFormat, rowPitch);

		g_captureInstance->lastResult = RET_SUCCESS;
	}

}

static void UNITY_INTERFACE_API OnTakeScreenshotEvent_Void(int eventID)
{
	g_captureInstance->lastResult = RET_ERROR_UNSUPPORTEDGRAPHICSDEVICE;
}

extern "C" UnityRenderingEvent UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API GetTakeScreenshotEventFunc(int eventID)
{
	if (g_GraphicsDeviceType == kUnityGfxRendererD3D11)
	{
		return OnTakeScreenshotEvent_D3D11;
	}
	else if (g_GraphicsDeviceType == kUnityGfxRendererD3D12)
	{
		//return OnTakeScreenshotEvent_D3D12;
	}
	else if (g_GraphicsDeviceType == kUnityGfxRendererOpenGL || g_GraphicsDeviceType == kUnityGfxRendererOpenGLCore)
	{
		return OnTakeScreenshotEvent_OpenGL;
	}
	return OnTakeScreenshotEvent_Void;
}


// If exported by a plugin, this function will be called when graphics device is created, destroyed, and before and after it is reset (ie, resolution changed).
extern "C" void UNITY_INTERFACE_EXPORT UnitySetGraphicsDevice(void* device, int deviceType, int eventType)
{
	if (eventType == kUnityGfxDeviceEventInitialize || eventType == kUnityGfxDeviceEventAfterReset)
	{
		if (deviceType == kUnityGfxRendererD3D11)
		{
			g_D3D11GraphicsDevice = (ID3D11Device*)device;
			g_GraphicsDeviceType = deviceType;
		}
		else if (deviceType == kUnityGfxRendererOpenGLCore || deviceType == kUnityGfxRendererOpenGL)
		{
			g_GraphicsDeviceType = deviceType;
			if (GLEW_OK != glewInit())
			{
				// GLEW failed!
				//exit(1);
			}
		}
		else
		{
			g_GraphicsDeviceType = -1;
		}
	}
	else
	{
		g_GraphicsDeviceType = -1;
	}
}
