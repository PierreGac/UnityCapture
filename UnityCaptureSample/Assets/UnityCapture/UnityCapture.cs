/*
  Unity Capture
  Copyright (c) 2018 Bernhard Schelling

  Feature contributors:
    Brandon J Matthews (low-level interface for custom texture capture)

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

using System.Collections;
using System.IO;
using UnityEngine;

[RequireComponent(typeof(Camera))]
public class UnityCapture : MonoBehaviour
{
    public enum ECaptureDevice { CaptureDevice1 = 0, CaptureDevice2 = 1, CaptureDevice3 = 2, CaptureDevice4 = 3, CaptureDevice5 = 4, CaptureDevice6 = 5, CaptureDevice7 = 6, CaptureDevice8 = 7, CaptureDevice9 = 8, CaptureDevice10 = 9 }
    public enum EResizeMode { Disabled = 0, LinearResize = 1 }
    public enum EMirrorMode { Disabled = 0, MirrorHorizontally = 1 }
    public enum ECaptureSendResult
    {
        SUCCESS = 0,
        WARNING_FRAMESKIP = 1,
        WARNING_CAPTUREINACTIVE = 2,
        ERROR_UNSUPPORTEDGRAPHICSDEVICE = 100,
        ERROR_PARAMETER = 101,
        ERROR_TOOLARGERESOLUTION = 102,
        ERROR_TEXTUREFORMAT = 103,
        ERROR_READTEXTURE = 104,
        ERROR_READTEXTUREDATA = 105,
        ERROR_TEXTUREHANDLE = 106,
        ERROR_INVALIDCAPTUREINSTANCEPTR = 200
    };

    [Tooltip("Capture device index")] public ECaptureDevice CaptureDevice = ECaptureDevice.CaptureDevice1;
    [Tooltip("Scale image if Unity and capture resolution don't match (can introduce frame dropping, not recommended)")] public EResizeMode ResizeMode = EResizeMode.Disabled;
    [Tooltip("How many milliseconds to wait for a new frame until sending is considered to be stopped")] public int Timeout = 1000;
    [Tooltip("Mirror captured output image")] public EMirrorMode MirrorMode = EMirrorMode.Disabled;
    [Tooltip("Introduce a frame of latency in favor of frame rate")] public bool DoubleBuffering = false;
    [Tooltip("Check to enable VSync during capturing")] public bool EnableVSync = false;
    [Tooltip("Set the desired render target frame rate")] public int TargetFrameRate = 60;
    [Tooltip("Check to disable output of warnings")] public bool HideWarnings = false;

    Interface CaptureInterface;

    [SerializeField]
    private bool _runOnStart = true;

    private bool _requestScreenshot = false;
    private RenderTexture _sourceTexture = null;
    private string _requestedScreenshotFileName = "test.png";

    private bool _active = false;
    public bool active
    {
        get
        {
            return _active;
        }
        set
        {
            if (_active != value)
            {
                if (CaptureInterface != null)
                {
                    CaptureInterface.active = value;
                }
                if (_active)
                {
                    _active = false;
                    if (CaptureInterface != null)
                    {
                        StopCoroutine(CaptureInterface.CallPluginAtEndOfFrames());
                    }
                }
                else
                {
                    _active = true;
                    if (CaptureInterface != null)
                    {
                        StartCoroutine(CaptureInterface.CallPluginAtEndOfFrames());
                    }
                }
                _active = value;
            }
        }
    }

    void Awake()
    {
        QualitySettings.vSyncCount = (EnableVSync ? 1 : 0);
        Application.targetFrameRate = TargetFrameRate;

        if (Application.runInBackground == false)
        {
            Debug.LogWarning("Application.runInBackground switched to enabled for capture streaming");
            Application.runInBackground = true;
        }
    }


    void Start()
    {
        CaptureInterface = new Interface(CaptureDevice);
        if (_runOnStart)
        {
            active = true;
        }
    }

#if UNITY_EDITOR
    private void Update()
    {
        if (Input.GetKeyDown(KeyCode.Space))
        {
            active = !active;
        }
        if (Input.GetKeyDown(KeyCode.T))
        {
            _requestScreenshot = true;
        }
    }
#endif

    public void TakeScreenshot(string fileName)
    {
        DirectoryInfo dInfo = new DirectoryInfo(fileName);
        if (Directory.Exists(dInfo.FullName))
        {
            _requestedScreenshotFileName = fileName;
            _requestScreenshot = true;
        }
    }

    void OnDestroy()
    {
        CaptureInterface.Close();
    }

    void OnRenderImage(RenderTexture source, RenderTexture destination)
    {
        Graphics.Blit(source, destination);

        if (_sourceTexture == null)
        {
            _sourceTexture = source; // Assuming that the source texture reference will be the same...
        }

        if(_requestScreenshot)
        {
            CaptureInterface.SetTexture(source, Timeout, DoubleBuffering, ResizeMode, MirrorMode, _requestedScreenshotFileName);
            _requestScreenshot = false;
            StartCoroutine(CaptureInterface.TakeScreenshot());
        }

        if (_active)
        {
            // This method is always called, in case of an unespected error, it may help to fall back in a working situation
            // Another idea should be to start the recording process manually and call this method only one (when the result is RET_SUCCESS) and never call it again
            CaptureInterface.SetTexture(source, Timeout, DoubleBuffering, ResizeMode, MirrorMode);
            ECaptureSendResult result = CaptureInterface.LastResult(); // Retreiving back the result
            switch (result)
            {
                case ECaptureSendResult.SUCCESS: break;
                case ECaptureSendResult.WARNING_FRAMESKIP: if (!HideWarnings) Debug.LogWarning("[UnityCapture] Capture device did skip a frame read, capture frame rate will not match render frame rate."); break;
                case ECaptureSendResult.WARNING_CAPTUREINACTIVE: if (!HideWarnings) Debug.LogWarning("[UnityCapture] Capture device is inactive"); break;
                case ECaptureSendResult.ERROR_UNSUPPORTEDGRAPHICSDEVICE: Debug.LogError("[UnityCapture] Unsupported graphics device (only D3D11/GL/GLCORE/GLES supported)"); break;
                case ECaptureSendResult.ERROR_PARAMETER: Debug.LogError("[UnityCapture] Input parameter error"); break;
                case ECaptureSendResult.ERROR_TOOLARGERESOLUTION: Debug.LogError("[UnityCapture] Render resolution is too large to send to capture device"); break;
                case ECaptureSendResult.ERROR_TEXTUREFORMAT: Debug.LogError("[UnityCapture] Render texture format is unsupported (only basic non-HDR (ARGB32) and HDR (FP16/ARGB Half) formats are supported)"); break;
                case ECaptureSendResult.ERROR_READTEXTURE: Debug.LogError("[UnityCapture] Error while reading texture image data"); break;
                case ECaptureSendResult.ERROR_READTEXTUREDATA: Debug.LogError("[UnityCapture] Error while reading texture buffer data"); break;
                case ECaptureSendResult.ERROR_TEXTUREHANDLE: Debug.LogError("[UnityCapture] Texture handle error"); break;
                case ECaptureSendResult.ERROR_INVALIDCAPTUREINSTANCEPTR: Debug.LogError("[UnityCapture] Invalid Capture Instance Pointer"); break;
                default: Debug.LogErrorFormat("[UnityCapture] Another error occured: {0} (0x{1})", result, result.ToString("X")); break;
            }
        }
    }

    public class Interface
    {
        [System.Runtime.InteropServices.DllImport("UnityCapturePlugin")] extern static System.IntPtr CaptureCreateInstance(int CapNum);
        [System.Runtime.InteropServices.DllImport("UnityCapturePlugin")] extern static ECaptureSendResult GetLastResult();
        [System.Runtime.InteropServices.DllImport("UnityCapturePlugin")] extern static void CaptureDeleteInstance(System.IntPtr instance);
        [System.Runtime.InteropServices.DllImport("UnityCapturePlugin")] extern static void SetTextureFromUnity(System.IntPtr instance, System.IntPtr nativetexture, int Timeout, bool UseDoubleBuffering, EResizeMode ResizeMode, EMirrorMode MirrorMode, bool IsLinearColorSpace, int width, int height);
        [System.Runtime.InteropServices.DllImport("UnityCapturePlugin")] extern static void PrepareScreenshot(System.IntPtr instance, System.IntPtr nativetexture, int Timeout, bool UseDoubleBuffering, EResizeMode ResizeMode, EMirrorMode MirrorMode, bool IsLinearColorSpace, int width, int height, byte[] fileName);
        [System.Runtime.InteropServices.DllImport("UnityCapturePlugin")] extern static System.IntPtr GetTakeScreenshotEventFunc();
        [System.Runtime.InteropServices.DllImport("UnityCapturePlugin")] extern static System.IntPtr GetRenderEventFunc();
        System.IntPtr CaptureInstance;

        System.IntPtr _cachedTexturePtr;

        public bool active = false;

        public Interface(ECaptureDevice CaptureDevice)
        {
            CaptureInstance = CaptureCreateInstance((int)CaptureDevice);
        }

        ~Interface()
        {
            Close();
        }

        public IEnumerator CallPluginAtEndOfFrames()
        {
            while (active)
            {
                yield return new WaitForEndOfFrame();
                GL.IssuePluginEvent(GetRenderEventFunc(), 1);
            }
        }

        public IEnumerator TakeScreenshot()
        {
            yield return new WaitForEndOfFrame();
            GL.IssuePluginEvent(GetTakeScreenshotEventFunc(), 1);
        }

        public void Close()
        {
            if (CaptureInstance != System.IntPtr.Zero)
            {
                CaptureDeleteInstance(CaptureInstance);
            }
            CaptureInstance = System.IntPtr.Zero;
        }

        /// <summary>
        /// Prepare the CatpureInstance to the texture sending process
        /// </summary>
        /// <param name="Source"></param>
        /// <param name="Timeout"></param>
        /// <param name="DoubleBuffering"></param>
        /// <param name="ResizeMode"></param>
        /// <param name="MirrorMode"></param>
        public void SetTexture(Texture Source, int Timeout = 1000, bool DoubleBuffering = false, EResizeMode ResizeMode = EResizeMode.Disabled, EMirrorMode MirrorMode = EMirrorMode.Disabled, string fileName = null)
        {
            if (CaptureInstance != System.IntPtr.Zero)
            {
                if (_cachedTexturePtr == System.IntPtr.Zero)
                {
                    _cachedTexturePtr = Source.GetNativeTexturePtr(); // On some configs, GetNativeTexPtr is slow (on a low end pc => 8.59ms/call !)
                }
                if (fileName == null)
                {
                    SetTextureFromUnity(CaptureInstance, _cachedTexturePtr, Timeout, DoubleBuffering, ResizeMode, MirrorMode, QualitySettings.activeColorSpace == ColorSpace.Linear, Source.width, Source.height);
                }
                else
                {
                    byte[] bytes = System.Text.Encoding.Unicode.GetBytes(fileName);
                    PrepareScreenshot(CaptureInstance, _cachedTexturePtr, Timeout, DoubleBuffering, ResizeMode, MirrorMode, QualitySettings.activeColorSpace == ColorSpace.Linear, Source.width, Source.height, bytes);
                }
            }
        }

        /// <summary>
        /// Returns the last result of the last IssuePluginEvent call
        /// </summary>
        /// <returns></returns>
        public ECaptureSendResult LastResult()
        {
            return GetLastResult();
        }
    }
}
