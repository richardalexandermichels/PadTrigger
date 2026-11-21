-- @description PadTrigger - Set up stereo print
local path = debug.getinfo(1, "S").source:sub(2):match("^(.*[/\\])")
local run, reason = loadfile(path .. "PadTrigger - Record audio.lua")
if run then
    run("setup-only")
else
    reaper.MB(tostring(reason), "PadTrigger - Set up stereo print", 0)
end
