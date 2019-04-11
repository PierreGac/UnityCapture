using System.Collections;
using System.Collections.Generic;
using System.IO;
using UnityEngine;

[RequireComponent(typeof(Camera))]
public class Screenshot : MonoBehaviour
{
    public enum ScreenshotType
    {
        fromUpdate,
        fromOnRender,
    }

    private Camera _cam;
    private bool _keyDown = false;
    private Texture2D _tex;
    private RenderTexture _rt;
    private void Start()
    {
        _cam = GetComponent<Camera>();
        _tex = new Texture2D(Screen.width, Screen.height, TextureFormat.RGBA32, false);
        _rt = new RenderTexture(Screen.width, Screen.height, 32);
    }

    private void Update()
    {
        _keyDown = Input.GetKeyDown(KeyCode.O);
        if (Input.GetKeyDown(KeyCode.P))
        {
            StartCoroutine(TakeScreenshot());
        }
    }

    void OnRenderImage(RenderTexture source, RenderTexture destination)
    {
        Graphics.Blit(source, destination);
        if (_keyDown)
        {
            _tex.ReadPixels(new Rect(0, 0, Screen.width, Screen.height), 0, 0);
            File.WriteAllBytes("test.png", _tex.EncodeToPNG());
        }
    }


    private IEnumerator TakeScreenshot()
    {
        yield return new WaitForEndOfFrame();


        _cam.targetTexture = _rt;
        _cam.Render();
        RenderTexture.active = _rt;
        _tex.ReadPixels(new Rect(0, 0, Screen.width, Screen.height), 0, 0);
        _cam.targetTexture = null;
        RenderTexture.active = null;

        yield return 0;

        File.WriteAllBytes("test.png", _tex.EncodeToPNG());
    }
}
