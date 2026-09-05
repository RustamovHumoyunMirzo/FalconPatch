local DebugInfo = Java.use("com.example.DebugInfo")

local hook = fpatch.hookMethod(
    "com.example.DebugInfo",
    "buildLabel",
    "()Ljava/lang/String;",
    function(_, original)
        return tostring(original) .. " [FalconPatch]"
    end)

local value, err = DebugInfo:callStatic("buildLabel", "()Ljava/lang/String;")
if value then
    fpatch.log(4, value)
else
    fpatch.log(6, err)
end

local stats = hook:stats()
fpatch.log(4, "hook calls=" .. tostring(stats and stats.calls or 0))
hook:remove()
