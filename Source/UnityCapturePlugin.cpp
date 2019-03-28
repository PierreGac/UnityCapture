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

#include "shared.inl"
#include <chrono>
#include <string>
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
#include <gl/gl.h>

static int g_GraphicsDeviceType = -1;
static ID3D11Device* g_D3D11GraphicsDevice = 0;
//static ID3D12Device* g_D3D12GraphicsDevice = 0;


struct UnityCaptureInstance
{
	SharedImageMemory* Sender;
	void* TextureHandle;
	int Width, Height;
	SharedImageMemory::EFormat EFormat;

	// DirectX11 stuff
	ID3D11Texture2D* d3dtex;
	ID3D11DeviceContext* ctx;
	ID3D11Texture2D* Textures[2];

	// DirectX12 stuff
	// TODO

	bool UseDoubleBuffering, AlternativeBuffer, IsLinearColorSpace;
	SharedImageMemory::EResizeMode ResizeMode;
	SharedImageMemory::EMirrorMode MirrorMode;
	int Timeout;

	// Open GL stuff
	unsigned long gl_dataSize;
	unsigned char* gl_dataPtr;

	int lastResult = RET_SUCCESS;
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
	delete c->Sender;
	if (c->Textures[0]) c->Textures[0]->Release();
	if (c->Textures[1]) c->Textures[1]->Release();
	if (c->gl_dataPtr) delete c->gl_dataPtr;
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
		if (g_GraphicsDeviceType == kUnityGfxRendererOpenGLCore || g_GraphicsDeviceType == kUnityGfxRendererOpenGL || g_GraphicsDeviceType == kUnityGfxRendererOpenGLES30)
		{
			c->gl_dataSize = width * height * 4;
			c->gl_dataPtr = new unsigned char[c->gl_dataSize];
			c->EFormat = SharedImageMemory::FORMAT_UINT8;
		}
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

extern "C" __declspec(dllexport) int GetLastResult()
{
	return g_captureInstance->lastResult;
}

static bool isOpenGL()
{
	return g_GraphicsDeviceType == kUnityGfxRendererOpenGL || g_GraphicsDeviceType == kUnityGfxRendererOpenGLCore;
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
	if (result && isOpenGL() && !g_captureInstance->gl_dataPtr)
	{
		g_captureInstance->lastResult = RET_ERROR_READTEXTUREDATA;
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
	GLuint gltex = (GLuint)(size_t)(g_captureInstance->TextureHandle);
	int error = GL_NO_ERROR;
	// Bind the texture to the GL_TEXTURE_2D
	glBindTexture(GL_TEXTURE_2D, gltex);
	error = glGetError();
	if (error != GL_NO_ERROR)
	{
		g_captureInstance->lastResult = RET_ERROR_READTEXTURE;
		return;
	}

	// TODO : Check HDR, Add double buffering, Check Linear space, Check for the resize mode
	/* HDR:
		- HDR or not, it seems that GL_RGBA will works in any cases
	   COLOR SPACE:
		- Gamma or Liear => RGBA still works
	*/

	// Gets the texture buffer for OpenGL ES, use glReadPixels
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_captureInstance->gl_dataPtr); // If the format is RGBA8, then RGBA still works
	error = glGetError();
	if (error != GL_NO_ERROR)
	{
		g_captureInstance->lastResult = RET_ERROR_READTEXTUREDATA;
		return;
	}

	// Send the texture to the DirectShow device
	SharedImageMemory::ESendResult res = g_captureInstance->Sender->Send(g_captureInstance->Width, g_captureInstance->Height,
		g_captureInstance->Width, g_captureInstance->gl_dataSize, g_captureInstance->EFormat, g_captureInstance->ResizeMode,
		g_captureInstance->MirrorMode, g_captureInstance->Timeout, (const unsigned char*)g_captureInstance->gl_dataPtr);

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
