-- @description PadTrigger - Record audio
local r = reaper
local sourceKey = "P_EXT:PadTriggerSource"
local record = 1013
local padClass = "ABCDEF019182FAEB4D79767350647472"

local function sourceGuid(track)
    local _, guid = r.GetSetMediaTrackInfo_String(track, sourceKey, "", false)
    return guid
end

local function linkedTrack(project, guid)
    for i = 0, r.CountTracks(project) - 1 do
        local track = r.GetTrack(project, i)
        if r.GetTrackGUID(track) == guid then return track end
    end
end

local function checkPadTrigger(track)
    local midiMode
    for fx = 0, r.TrackFX_GetCount(track) - 1 do
        local _, ident = r.TrackFX_GetNamedConfigParm(track, fx, "fx_ident")
        local _, original = r.TrackFX_GetNamedConfigParm(track, fx, "original_name")
        if midiMode == true then
            local _, instrument = r.TrackFX_GetNamedConfigParm(track, fx, "is_instrument")
            if instrument == "1" and r.TrackFX_GetEnabled(track, fx) and not r.TrackFX_GetOffline(track, fx) then
                return
            end
        elseif ((ident or ""):upper():find(padClass, 1, true) or original == "Padomizer" or original == "Pad Trigger"
            or original == "VST3: Padomizer (MyVST)" or original == "VST3: Pad Trigger (MyVST)")
            and r.TrackFX_GetEnabled(track, fx) and not r.TrackFX_GetOffline(track, fx) then
            local parameter = r.TrackFX_GetParamFromIdent(track, fx, "soundMode")
            if parameter < 0 then
                for candidate = 0, r.TrackFX_GetNumParams(track, fx) - 1 do
                    local _, label = r.TrackFX_GetParamName(track, fx, candidate, "")
                    if label == "Sound mode" then
                        parameter = candidate
                        break
                    end
                end
            end
            if parameter >= 0 then
                if r.TrackFX_GetParamNormalized(track, fx, parameter) < 0.5 then return end
                midiMode = true
            end
        end
    end
    if midiMode then error("Place an enabled instrument after PadTrigger in the track FX chain.", 0) end
    error("Select a track with PadTrigger enabled in its regular FX chain, or its linked print track.", 0)
end

local function printMode(track)
    return r.GetMediaTrackInfo_Value(track, "I_RECINPUT") < 0
        and r.GetMediaTrackInfo_Value(track, "I_RECMODE") == 3
        and (math.floor(r.GetMediaTrackInfo_Value(track, "I_RECMODE_FLAGS")) & 3) == 2
end

local function printSend(track, category, index)
    local get = r.GetTrackSendInfo_Value
    return get(track, category, index, "I_SENDMODE") == 3
        and get(track, category, index, "I_SRCCHAN") == 0
        and get(track, category, index, "I_DSTCHAN") == 0
        and (math.floor(get(track, category, index, "I_MIDIFLAGS")) & 31) == 31
end

local function prepare(project, selected, movingExistingTake)
    assert(r.GetPlayStateEx(project) == 0, "Stop playback or recording before running this action.")
    assert(selected, "Select one PadTrigger track or its linked print track.")
    local linked = sourceGuid(selected)
    local source = linked == "" and selected or linkedTrack(project, linked)
    assert(source, "The selected print track's PadTrigger source is missing.")
    if linked == "" and printMode(selected) and r.GetTrackNumSends(selected, -1) > 0 then
        assert(r.GetTrackNumSends(selected, -1) == 1 and printSend(selected, -1, 0),
            "The selected print track has ambiguous routing. Select the Padomizer source or resolve its receives.")
        source = assert(r.GetTrackSendInfo_Value(selected, -1, 0, "P_SRCTRACK"))
    end
    assert(movingExistingTake or r.CountTrackMediaItems(source) == 0,
        "Move existing items off the PadTrigger mic track first so they will not trigger the instrument again.")
    assert(r.GetMediaTrackInfo_Value(source, "I_RECINPUT") >= 0
        and r.GetMediaTrackInfo_Value(source, "I_RECINPUT") < 1024,
        "Select a mono audio input on the PadTrigger track.")
    assert(r.GetMediaTrackInfo_Value(source, "I_FXEN") ~= 0, "Enable the PadTrigger track's FX.")
    checkPadTrigger(source)

    local guid = r.GetTrackGUID(source)
    local printTrack
    for i = 0, r.CountTracks(project) - 1 do
        local track = r.GetTrack(project, i)
        if sourceGuid(track) == guid then
            assert(not printTrack, "More than one print track is linked to this PadTrigger track.")
            printTrack = track
        end
    end

    -- A stock template receives from its imported source without a stored GUID.
    local send
    for i = 0, r.GetTrackNumSends(source, 0) - 1 do
        local target = r.GetTrackSendInfo_Value(source, 0, i, "P_DESTTRACK")
        local candidate = target and printMode(target) and printSend(source, 0, i)
        if target and (target == printTrack or candidate) then
            assert(not printTrack or printTrack == target, "More than one PadTrigger print destination matches.")
            assert(not send, "More than one send feeds the PadTrigger print track.")
            if candidate then
                local tag = sourceGuid(target)
                assert(tag == "" or tag == guid, "The print destination is linked to another source.")
            end
            assert(r.GetTrackNumSends(target, -1) == 1, "The print destination has multiple receives.")
            printTrack = target
            send = i
        end
    end
    if not printTrack then
        local position = r.CountTracks(project)
        r.InsertTrackAtIndex(position, false)
        printTrack = r.GetTrack(project, position)
        local _, name = r.GetTrackName(source)
        r.GetSetMediaTrackInfo_String(printTrack, "P_NAME", name .. " - stereo print", true)
        r.SetMediaTrackInfo_Value(printTrack, "I_HEIGHTOVERRIDE", 140)
    end
    local setSend = r.SetTrackSendInfo_Value
    if not send then
        send = r.CreateTrackSend(source, printTrack)
        assert(send >= 0, "REAPER could not create the post-FX audio send.")
        setSend(source, 0, send, "D_VOL", 1.0)
    end
    r.GetSetMediaTrackInfo_String(printTrack, sourceKey, guid, true)
    setSend(source, 0, send, "I_SENDMODE", 3) -- post-FX, pre-fader
    setSend(source, 0, send, "I_SRCCHAN", 0)
    setSend(source, 0, send, "I_DSTCHAN", 0)
    setSend(source, 0, send, "I_MIDIFLAGS", 31) -- audio only
    setSend(source, 0, send, "B_MUTE", 0)

    local set = r.SetMediaTrackInfo_Value
    set(source, "I_RECMODE", 2) -- monitor the mic without recording it
    set(source, "I_RECMON", 2) -- tape-style input monitoring
    set(source, "B_MAINSEND", 0) -- monitor via the print track
    set(source, "I_RECARM", 1)
    set(printTrack, "I_RECINPUT", -1)
    set(printTrack, "I_RECMODE", 3) -- latency-compensated stereo output
    set(printTrack, "I_RECMODE_FLAGS", 2) -- post-FX, pre-fader
    set(printTrack, "I_RECMONITEMS", 0)
    set(printTrack, "I_RECARM", 1)
    r.SetOnlyTrackSelected(printTrack)
    r.TrackList_AdjustWindows(false)
    r.UpdateArrange()
    return printTrack
end

-- A caller that has already printed an existing take can move it after preparing the destination.
local operation = (...)
if operation == "prepare" then return prepare end

local project = r.EnumProjects(-1, "")
if r.CountSelectedTracks(project) ~= 1 then
    r.MB("Select one PadTrigger track or its linked print track.", "PadTrigger - Record audio", 0)
    return
end
local selected = r.GetSelectedTrack(project, 0)
local name = "Set up PadTrigger stereo recording"
local previousChangeCount = r.GetProjectStateChangeCount(project)
r.Undo_BeginBlock2(project)
local ok, reason = pcall(prepare, project, selected, false)
r.Undo_EndBlock2(project, name, -1)
if not ok then
    if r.GetProjectStateChangeCount(project) ~= previousChangeCount and r.Undo_CanUndo2(project) == name then
        r.Undo_DoUndo2(project)
    end
    local message = tostring(reason)
    r.MB(message:match("^.-:%d+: (.*)$") or message, "PadTrigger - Record audio", 0)
    return
end
if operation ~= "setup-only" then r.Main_OnCommandEx(record, 0, project) end

