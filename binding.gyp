{
  "targets": [
    {
      "target_name": "discord_social",
      "sources": ["src/addon.cpp"],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "lib/discord_social_sdk/include"
      ],
      "libraries": [
        "../lib/discord_social_sdk/lib/release/discord_partner_sdk.lib"
      ],
      "defines": ["NAPI_DISABLE_CPP_EXCEPTIONS"]
    }
  ]
}
