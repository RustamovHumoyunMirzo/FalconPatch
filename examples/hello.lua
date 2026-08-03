local jni = require("jni")
local gui = require("gui")

fpatch.log(4, "FalconPatch is running in " .. (jni.package_name() or "unknown"))
gui.toast("FalconPatch loaded")
