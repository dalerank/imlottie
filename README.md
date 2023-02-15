# imlottie
lottie renderer for imgui based on rlottie library

![lottie](https://user-images.githubusercontent.com/918081/218279099-f1b0df48-79a0-45a5-8668-3bce568692fb.gif)

```
    ImLottie::init();
    while (!done) {
        ImLottie::LottieAnimation(_("speaker.json").c_str(), ImVec2(48, 48), true, 0);

        ImLottie::sync(g_pd3dDevice, g_pd3dDeviceContext);
   }
```
