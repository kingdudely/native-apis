{
  "targets": [
    {
      "target_name": "native-apis",
      "sources": ["src/addon.cpp"],
      "conditions": [
        ["OS=='win'", {
          "sources": [
            "src/windows/src/mouse.cpp",
            "src/windows/src/keyboard.cpp",
            "src/windows/src/virtual_screen.cpp"
          ],
          "libraries": [
            "setupapi.lib",
            "cfgmgr32.lib",
            "advapi32.lib",
            "user32.lib"
          ]
        }],
        ["OS=='linux'", {
          "sources": [
            "src/linux/src/mouse.cpp",
            "src/linux/src/keyboard.cpp",
            "src/linux/src/virtual_screen.cpp"
          ],
          "libraries": [
            "-lX11",
            "-lXrandr",
            "-lXtst"
          ]
        }],
        ["OS=='mac'", {
          "sources": [
            "src/macos/src/mouse.cpp",
            "src/macos/src/keyboard.cpp",
            "src/macos/src/virtual_screen.mm"
          ],
          "link_settings": {
            "libraries": [
              "-framework Carbon",
              "-framework Foundation",
              "-framework CoreGraphics"
            ]
          },
          "xcode_settings": {
            "OTHER_CFLAGS": ["-ObjC++", "-fobjc-arc"],
            "OTHER_LDFLAGS": []
          }
        }]
      ],
      "include_dirs": [
        "src",
        "<!@(node -p \"require('node-addon-api').include\")"
      ],
      "dependencies": [
        "<!(node -p \"require('node-addon-api').gyp\")"
      ],
      "defines": ["NAPI_DISABLE_CPP_EXCEPTIONS"]
    }
  ]
}
