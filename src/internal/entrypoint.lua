-- Presentation only. No package.loadlib, FFI, native callbacks,
-- engine pointers or lighting payload. Native settings own persistence/application.
if ShadowEngineDiagnostics and ShadowEngineDiagnostics.loaded then return end
-- Only the development fixture/tooling explicitly supplies this argument.
-- NexusTools' normal entrypoint uses the public single-report menu.
local tool = { development = ... == "development", loaded = false, pending = nil, captureMessage = "No capture requested.",
    populationMessage = "No shadow onset recording requested." }
local pollPopulation
ShadowEngineDiagnostics = tool

local function stage(message)
    if type(SH_LOG) == "function" then
        pcall(SH_LOG, "[Shadow Engine Tools v2.0.94] {}", message)
    end
end
stage("entry purpose=nexusCompatibilityWarning")

local function notify(message)
    if type(SH_Notifications_PushNotification) == "function" then
        local ok, err = pcall(SH_Notifications_PushNotification, message)
        if not ok then stage("notification unavailable: " .. tostring(err)) end
    end
end

local function captureMessage(message)
    if tool.captureMessage ~= message then
        tool.captureMessage = message
        stage(message)
        if tool.captureRoot then
            local ok, err = pcall(function() tool.captureRoot:Rerender() end)
            if not ok then stage("capture display refresh failed: " .. tostring(err)) end
        end
    end
    notify(message)
end

local function readStatus()
    local paths = { "ShadowEngineDiagnostics.status", "bin/ShadowEngineDiagnostics.status",
                    "../bin/ShadowEngineDiagnostics.status" }
    if type(io) ~= "table" or type(io.open) ~= "function" then
        error("NexusTools Lua file API unavailable")
    end
    for _, path in ipairs(paths) do
        local file = io.open(path, "rb")
        if file then
            local text = file:read(384) or ""
            file:close()
            local status = tool.settingsApi.parse(text, path)
            if status then return status end
        end
    end
end

local function pollExtraSlots(status)
    local pending = tool.pending
    if not status or not pending or pending.extraSlots == nil then return end
    local suffix
    if pending.pid ~= status.pid or pending.session ~= status.session then
        suffix = " Previous test request interrupted by a new game session."
    elseif status.ack < pending.sequence then
        return
    elseif status.ack > pending.sequence then
        suffix = " Request superseded; showing the current engine value."
    elseif status.extraSlots ~= pending.extraSlots then
        suffix = " Requested change was not applied."
    end
    pending.extraSlots = nil
    local message = "Extra slots: " .. string.format("%.0f", status.extraSlots) ..
        " (temporary; resets to 2 on game restart)." .. (suffix or "")
    stage(message)
    notify(message)
end

local function pollCompatibility(status)
    if not status or not status.hostCompatibility or status.hostCompatibility < 1 then return end
    local key = status.pid .. ":" .. status.session
    local state = status.hostCompatibility
    if tool.compatibilitySession ~= key then
        tool.compatibilitySession, tool.compatibilityNotice, tool.compatibilityMessage = key, nil, nil
    end
    if state == 1 then tool.compatibilityMessage = nil; return end
    local message = "NexusTools compatibility could not be fully verified. Attempting shadow startup; the optional Lua repair is disabled."
    message = message .. " Last runtime-tested NexusTools: 1.1.13. If issues occur, revert to that complete version."
    tool.compatibilityMessage = message
    if tool.compatibilityNotice ~= state then
        tool.compatibilityNotice = state
        stage(message)
        notify(message)
    end
end

local function requestTarget(status, quiet, settings)
    status = status or readStatus()
    if not status or (not settings and status.downstream ~= 2) then
        if not quiet then notify("Shadow Engine diagnostics unavailable: engine startup is not ready.") end
        return nil
    end
    -- Observe an older test result before another request replaces the mailbox owner.
    pollExtraSlots(status)
    if tool.populationWatch then pollPopulation(false, status) end
    -- Every next sequence remains exact even with a single-precision Lua number.
    if status.ack >= 16777215 then
        if not quiet then notify("Request sequence exhausted. Restart the game before changing settings.") end
        return nil
    end
    if tool.pending and tool.pending.session == status.session and
       tool.pending.pid == status.pid and
       (status.ack < tool.pending.sequence or
        ((tool.pending.settings or tool.pending.population) and status.processedSeq < tool.pending.sequence)) then
        if not quiet then notify("A request is still pending. Wait a moment and retry.") end
        return nil
    end
    local sequence = status.ack + 1
    local request = status.path:gsub("%.status$", ".request")
    return status, sequence, request, request .. ".tmp"
end

local function queueRequest(status, sequence, request, temporary, text, label, settings)
    local file = io.open(temporary, "wb")
    if not file then notify("Cannot write the diagnostic request."); return end
    local ok = file:write(text)
    local closed = file:close()
    if not ok or not closed then notify("Diagnostic request write failed."); return end
    if not os.rename(temporary, request) then
        notify("Diagnostic mailbox busy. Resume gameplay and retry."); return
    end
    tool.pending = { pid = status.pid, session = status.session, sequence = sequence,
        settings = settings, text = text, request = request, temporary = temporary }
    stage((label or "Diagnostic") .. " request queued sequence=" .. tostring(sequence))
    -- Workload capture owns its single, capture-specific queued notification.
    if label then notify(label .. " request queued.") end
    return true
end


local function send(action)
    local status, sequence, request, temporary = requestTarget()
    if not status then return end
    if action == 11 and status.captureState >= 1 and status.captureState <= 3 then
        notify("A capture is already running. Wait for the finished notification.")
        return
    end
    local queued = queueRequest(status, sequence, request, temporary,
        string.format("SE1 %s %s %d %.0f\n",
            status.pid, status.session, sequence, action), action ~= 11 and "Diagnostic")
    if action == 11 and queued then
        -- Queued feedback is already announced here. A matching worker serial
        -- still gates later state changes; lead-in polling must not announce it twice.
        tool.captureWatch = { pid = status.pid, session = status.session,
            serial = status.captureSerial + 1, ticks = 0, state = 1 }
        captureMessage("Capture queued. Recording starts after 3 seconds; the menu may stay open.")
    end
end

local function toggleExtraSlots()
    local status, sequence, request, temporary = requestTarget()
    if not status then return end
    local desired = status.extraSlots == 2 and 0 or 2
    local action = desired == 0 and 12 or 13
    if queueRequest(status, sequence, request, temporary,
        string.format("SE1 %s %s %d %d\n", status.pid, status.session, sequence, action),
        "Extra-slot test") then
        -- Only a subsequent native status with matching session/ACK confirms this.
        tool.pending.extraSlots = desired
    end
end

-- Capture observation remains scoped to an explicitly requested capture.
local function pollCapture(manual, observed)
    local watch = tool.captureWatch
    if not tool.loaded then return end
    if not watch and not manual then return end
    local status = observed or readStatus()
    if not status then
        if manual or not tool.captureReadFailed then
            captureMessage("Capture status temporarily unavailable. Waiting for the engine report.")
        end
        tool.captureReadFailed = true
        return
    end
    local recovered = tool.captureReadFailed
    tool.captureReadFailed = false
    if not watch then
        if manual then
            local labels = { [0] = "No capture in this session.",
                [1] = "Capture queued. Recording starts after 3 seconds.",
                [2] = "Shadow capture is recording (5-second window).",
                [3] = "Recording finished. Saving the report...",
                [4] = "Shadow capture saved. Ready for the next test.",
                [5] = "Shadow capture failed. See ShadowEnginePatch.log for the start or saving error." }
            captureMessage(labels[status.captureState] or "Capture status unavailable.")
        end
        return
    end
    if status.pid ~= watch.pid or status.session ~= watch.session then
        tool.captureWatch = nil
        captureMessage("Capture interrupted: the game session changed.")
        return
    end
    if status.captureSerial ~= watch.serial then
        if manual then captureMessage("Capture request is waiting for engine acknowledgment.") end
        return
    end
    if status.captureState == watch.state and not manual and not recovered then return end
    watch.state = status.captureState
    if watch.state == 1 then
        captureMessage("Capture queued. Recording starts after 3 seconds; the menu may stay open.")
    elseif watch.state == 2 then
        captureMessage("Shadow capture is recording (5-second window).")
    elseif watch.state == 3 then
        captureMessage("Recording finished. Saving the report...")
    elseif watch.state == 4 or watch.state == 5 then
        tool.captureWatch = nil
        captureMessage(watch.state == 4 and "Shadow capture saved. Ready for the next test." or
            "Shadow capture failed. See ShadowEnginePatch.log for the start or saving error.")
    end
end

local function populationMessage(message, quiet)
    if tool.populationMessage ~= message then
        tool.populationMessage = message
        if not quiet then stage(message) end
        if tool.populationRoot then
            local ok, err = pcall(function() tool.populationRoot:Rerender() end)
            if not ok then stage("shadow onset display refresh failed: " .. tostring(err)) end
        end
    end
    if not quiet then notify(message) end
end

function tool.populationLabel(status)
    local state = status.populationState
    if state == 0 then return "No shadow onset recording in this session." end
    if state == 1 or state == 2 or state == 6 then
        local remaining = math.ceil((status.populationWindowMs - status.populationElapsedMs) / 1000)
        if remaining == 0 then return "Shadow onset recording window complete. Waiting for automatic saving." end
        return "Shadow onset recording is active: " .. string.format("%.0f", remaining) ..
            " seconds remaining. The report saves automatically."
    end
    if state == 3 then return "Shadow onset recording finished. Saving the report; wait before starting another." end
    if state == 5 then return "Shadow onset report saving failed. Check ShadowEnginePatch.log." end
    local message = status.populationPartial == 1 and
        "Shadow onset report saved with partial coverage or output; limits are listed in the log." or
        "Shadow onset report saved."
    return message .. " Ready for another recording."
end

-- Decimal identifiers remain exact on the game's single-precision Lua VM.
function tool.nextPopulationSerial(value)
    if value == "4294967295" then return end
    local digits, carry = {}, 1
    for i = #value, 1, -1 do
        local digit = tonumber(value:sub(i, i)) + carry
        carry = digit >= 10 and 1 or 0
        digits[i] = tostring(digit % 10)
    end
    return (carry == 1 and "1" or "") .. table.concat(digits)
end

pollPopulation = function(manual, observed)
    if not tool.loaded then return end
    local watch = tool.populationWatch
    if not watch and not manual then return end
    local status = observed
    if not status and manual then status = readStatus() end
    if not status then
        if manual or not tool.populationReadFailed then
            populationMessage("Shadow onset status unavailable. Use Check shadow onset progress to retry.")
        end
        tool.populationReadFailed = true
        return
    end
    local recovered = tool.populationReadFailed
    tool.populationReadFailed = false
    if not status.populationAvailable then
        tool.populationWatch = nil
        populationMessage("Whole-pass shadow onset recording requires Shadow Engine 2.0.87 or newer. Existing settings and flicker controls remain available.")
        return
    end
    if watch and (watch.pid ~= status.pid or watch.session ~= status.session) then
        tool.populationWatch = nil
        populationMessage("Shadow onset recording interrupted by a new game session.")
        return
    end
    if watch and watch.request then
        local request = watch.request
        if status.ack < request.sequence or status.processedSeq < request.sequence then
            if manual then populationMessage("Shadow onset request is waiting for engine confirmation.") end
            return
        end
        if status.processedSeq ~= request.sequence then
            tool.populationWatch = nil
            populationMessage("Shadow onset request was superseded. Check shadow onset progress for the current engine state.")
            return
        end
        watch.request = nil
        if status.result ~= 1 then
            tool.populationWatch = nil
            populationMessage("Shadow onset request was rejected: the engine may be busy saving or recording may be unavailable. " .. tool.populationLabel(status))
            return
        end
    end
    if watch and status.populationSerial ~= watch.serial then
        -- A changed later serial belongs to another request. Never attribute
        -- that request's saved state to the recording we were watching.
        tool.populationWatch = nil
        populationMessage("Shadow onset recording identity changed. Check shadow onset progress for the current recording.")
        return
    end
    if not watch then
        watch = { pid = status.pid, session = status.session, serial = status.populationSerial }
    end
    local key = tostring(status.populationState) .. ":" .. tostring(status.populationPartial)
    if not manual and not recovered and watch.key == key then
        -- Native elapsed time updates the menu countdown, without a popup
        -- every second or a locally invented completion deadline.
        populationMessage(tool.populationLabel(status), true)
        return
    end
    watch.key = key
    if status.populationState == 1 or status.populationState == 2 or
       status.populationState == 3 or status.populationState == 6 then
        tool.populationWatch = watch
    else tool.populationWatch = nil end
    populationMessage(tool.populationLabel(status))
end

local function sendPopulation(action)
    local status, sequence, request, temporary = requestTarget()
    if not status then return end
    if not status.populationAvailable then
        populationMessage("Whole-pass shadow onset recording requires Shadow Engine 2.0.87 or newer. Existing settings and flicker controls remain available.")
        return
    end
    local state = status.populationState
    local active = state == 1 or state == 2 or state == 6
    if (action == 14 and (active or state == 3)) or (action ~= 14 and not active) then
        populationMessage((action == 14 and "Cannot start another shadow onset recording. " or
            "Start a shadow onset recording before stopping. ") .. tool.populationLabel(status))
        return
    end
    local serial = status.populationSerial
    if action == 14 then serial = tool.nextPopulationSerial(serial) end
    if not serial then populationMessage("Shadow onset recording sequence exhausted. Restart the game."); return end
    if queueRequest(status, sequence, request, temporary,
        string.format("SE1 %s %s %d %d\n", status.pid, status.session, sequence, action), nil) then
        tool.pending.population = true
        tool.populationWatch = { pid = status.pid, session = status.session, serial = serial,
            request = { sequence = sequence, action = action } }
        populationMessage(action == 14 and "Shadow onset start requested. Waiting for engine confirmation." or
            "Shadow onset stop requested. Waiting for engine confirmation.")
    end
end

local function submitSettings(status, mask, values)
    local target, sequence, request, temporary = requestTarget(status, true, true)
    if not target or target.pendingSeq ~= 0 then return end
    local text = string.format("SES2 %s %s %d %d %d %d %d\n",
        target.pid, target.session, sequence, mask, values[1] or 0, values[2] or 0, values[4] or 0)
    if queueRequest(target, sequence, request, temporary, text, nil, true) then return sequence end
    return nil, true
end

local function retrySettings(status, sequence)
    local pending = tool.pending
    if not pending or not pending.settings or pending.sequence ~= sequence or
        pending.pid ~= status.pid or pending.session ~= status.session or status.pendingSeq == sequence then return end
    queueRequest(status, sequence, pending.request, pending.temporary, pending.text, nil, true)
end

local function guarded(callback)
    return function()
        if not tool.loaded then return end
        local ok, err = pcall(callback)
        if not ok then
            stage("action failed: " .. tostring(err))
            pcall(notify, "Shadow Engine tool failed: " .. tostring(err))
        end
    end
end

local function readAndPollStatus(force, readOnly, elapsedTicks)
    if not tool.loaded or tool.reading or tool.settings.redrawing then return end
    -- Poll on a changed wall-clock second, with a bounded update fallback
    -- when the host clock is absent/frozen. Neither determines capture time.
    tool.statusWatch = tool.statusWatch or { ticks = 0 }
    local watch = tool.statusWatch
    watch.ticks = watch.ticks + (elapsedTicks or 1)
    local now
    if not watch.clockUnavailable then
        if type(os) == "table" and type(os.time) == "function" then
            local ok, value = pcall(os.time)
            if ok and type(value) == "number" then now = value
            else watch.clockUnavailable = true end
        else watch.clockUnavailable = true end
    end
    if not force and watch.ticks < 60 and
       (not now or now == watch.lastClock) then return end
    watch.ticks = 0
    watch.lastClock = now
    tool.reading = true
    local ok, status = pcall(readStatus)
    local populationStatus = ok and status or nil
    if ok then pollCompatibility(status) end
    if tool.support then tool.support.poll(ok and status or nil) end
    tool.settings.observe(ok and status or nil)
    if ok then pollExtraSlots(status) end
    if ok and not readOnly then tool.settings.flush() end
    local err
    if tool.captureWatch then ok, err = pcall(pollCapture, false, ok and status or nil) end
    if tool.populationWatch then
        local populationOk, populationErr = pcall(pollPopulation, false, populationStatus)
        if not populationOk then stage("shadow onset status read failed: " .. tostring(populationErr)) end
    end
    if not ok then
        if not tool.captureReadFailed then
            stage("engine status read failed: " .. tostring(err or status))
            if tool.captureWatch then captureMessage("Capture status temporarily unavailable. Use Check test progress to check it.") end
        end
        tool.captureReadFailed = true
    end
    tool.reading = false
end

local function pollStatus(force, readOnly, elapsedTicks)
    if tool.reading then return end
    local ok, err = pcall(readAndPollStatus, force, readOnly, elapsedTicks)
    tool.reading = false
    if not ok then stage("menu status refresh failed: " .. tostring(err)) end
end

local function scheduleStatusPoll()
    if not tool.loaded then return end
    local ok, err = pcall(SH_NextTick, tool.statusTick, 30)
    tool.timerAvailable = ok
    if not ok then stage("menu status timer unavailable: " .. tostring(err)) end
end

function tool.statusTick()
    if not tool.loaded then return end
    pollStatus(false, false, 30)
    scheduleStatusPoll()
end

local function refreshStatus()
    pollStatus(true, true)
    local status = not tool.settings.unavailable and tool.settings.current
    if status then
        tool.settings.observe(status)
        local quality = status.disabled < 2
        notify("Limiter: " .. tool.settingsApi.limitLabel(status.selectedLimit) ..
            " | Extra slots: " .. string.format("%.0f", status.extraSlots) ..
            " | Vehicle resolution: " .. (quality and string.format("%.0f", status.resolution) or "Original") ..
            " | World lights: " .. (status.worldEnabled == 1 and
                string.format("%.0f", status.worldResolution) or "Original") ..
            " | Shadow processing: " .. (status.downstream == 2 and status.ready == 1 and "ready" or "not ready") ..
            " | Driven-car protection: " .. (status.driverReady == 1 and "available" or "unavailable") ..
            ((tool.settings.inflight or next(tool.settings.desired) or status.pendingSeq > 0)
                and " | Changes pending; showing last applied settings." or ""))
    else
        notify("Shadow Engine mailbox unavailable. No policy changes are made.")
    end
end

-- WD1 stores a prototype's upvalue count in four bits. Keep the request
-- interface on its existing owner so menu registration captures one owner,
-- rather than independently capturing every transport helper (16 previously).
tool.requests = { target = requestTarget, queue = queueRequest,
    submitSettings = submitSettings, retrySettings = retrySettings }

local function registerMenu()
    stage("register menu")
    local menu = SH_Menu_RegisterMenu("Shadow Engine Tools")
    tool.menu = menu
    tool.pages = {
        shadows = SH_Menu_RegisterCategory("Shadows", menu),
        traffic = SH_Menu_RegisterCategory("Traffic Control", menu)
    }
    if tool.development then
        tool.pages.diagnostics = SH_Menu_RegisterCategory("Diagnostics", menu)
        tool.pages.population = SH_Menu_RegisterCategory("Shadow onset recording", menu)
    else
        tool.pages.support = SH_Menu_RegisterCategory("Support", menu)
        tool.support = dofile("shadow_engine_tools/support.lua").create({ notify=notify, log=stage, guard=guarded,
            submit=function()
                local status, sequence, request, temporary = tool.requests.target()
                if not status then return end
                if tool.requests.queue(status, sequence, request, temporary,
                    string.format("SE1 %s %s %d 17\n", status.pid, status.session, sequence), nil) then
                    return status, sequence
                end
            end })
    end
    tool.traffic = dofile("shadow_engine_tools/traffic.lua")(notify, stage)
    tool.settingsApi = dofile("shadow_engine_tools/settings.lua")
    tool.settings = tool.settingsApi.create({ notify = notify, log = stage,
        loaded = function() return tool.loaded end, submit = tool.requests.submitSettings, retry = tool.requests.retrySettings,
        refresh = function() pollStatus(true, true) end })
    local commandPrefix = "SE_4F91C2D7_"
    tool.commands = {
        traffic = commandPrefix .. "MovingTraffic", limit = commandPrefix .. "VehicleLimit"
    }
    tool.settings.slider = tool.commands.limit
    tool.settings.sliderObserved = 0
    SH_Commands_RegisterInt(tool.commands.limit, "Vehicle shadow limit",
        "1-9 counts traffic vehicles. 0 = limiter off; 10 = Unlimited. Changes save and apply automatically.", menu, 0, 0, 10,
        guarded(tool.settings.sliderChanged))
    SH_Commands_RegisterBool(tool.commands.traffic, "Spawn moving traffic",
        "OFF stops new ordinary traffic from spawning. Existing cars, parked cars and mission/event vehicles remain. Changes immediately.",
        menu, true, guarded(function()
            tool.traffic.setEnabled(SH_Commands_GetStateBool(tool.commands.traffic))
        end))
    tool.actions = {}
    for action = 1, 11 do
        local id = action
        tool.actions[id] = guarded(function() send(id) end)
    end
    for _, action in ipairs({14, 16}) do
        local id = action
        tool.actions[id] = guarded(function() sendPopulation(id) end)
    end
    tool.actions[5] = nil
    tool.refresh = guarded(refreshStatus)
    tool.toggleExtras = guarded(toggleExtraSlots)
    tool.refreshCapture = guarded(function() pollCapture(true) end)
    tool.refreshPopulation = guarded(function() pollPopulation(true) end)
    tool.script = Script("SE_4F91C2D7_TrafficLifetime")
    function tool.script:OnUpdate()
        if not tool.timerAvailable then pollStatus() end
    end
    function tool.script:OnUnload()
        tool.loaded = false
        tool.captureWatch = nil
        tool.captureRoot = nil
        tool.populationWatch, tool.populationRoot = nil, nil
        tool.settings.remove()
        if tool.support then tool.support.remove() end
        tool.traffic.restore()
    end
    function menu:OnRemove()
        tool.loaded = false
        tool.pending = nil
        tool.captureWatch = nil
        tool.captureRoot = nil
        tool.populationWatch, tool.populationRoot = nil, nil
        tool.settings.remove()
        if tool.support then tool.support.remove() end
        tool.traffic.restore()
        if tool.script then tool.script:Remove(); tool.script = nil end
        tool.menu = nil
        tool.pages = nil
    end
    -- Page opens refresh status read-only. Registration never sends defaults.
    tool.layouts = {}
    tool.layouts.shadows = function(root)
        pollStatus(true, true)
        if tool.compatibilityMessage then root:Text(tool.compatibilityMessage); root:Divider() end
        root:Button("Check current shadow settings", tool.refresh)
        root:Divider()
        root:Text("World shadow quality", { size = 18, bold = true })
        root:Text("Streetlights and other non-vehicle local lights. Sun and vehicle shadows are controlled separately.")
        root:Divider()
        tool.settings.render(root, 4)
        root:Divider()
        root:Text("Vehicle shadow limit", { size = 18, bold = true })
        root:Text("Controls how many vehicles may cast headlight shadows. World shadows are unaffected.")
        root:Divider()
        tool.settings.render(root, 1)
        root:Text("1-9 counts traffic vehicles, not lights. 0 = limiter off; 10 = Unlimited.")
        root:Text("When enabled, the extra 2 slots protect nearby shadows and brief departures first. Approaching cars use any remaining space.")
        root:Text("When driven-car protection is available, your driven car is excluded from this count. Selected vehicle quality still applies to it. Native shadow limits still apply.")
        root:Divider()
        root:Text("Vehicle shadow quality", { size = 18, bold = true })
        tool.settings.render(root, 2)
    end
    tool.layouts.traffic = function(root)
        root:Text("Traffic control", { size = 18, bold = true })
        root:Text("Choose whether new ordinary moving traffic can spawn.")
        root:Divider()
        root:CommandWidget(tool.commands.traffic)
        root:Text("This affects new ordinary moving traffic only. Parked, existing, mission, and event vehicles remain.")
    end
    tool.layouts.diagnostics = function(root)
        tool.captureRoot = root
        root:Text("Engine reports", { size = 18, bold = true })
        root:Text("Use these tools when capturing a visual problem for investigation.")
        root:Divider()
        root:Text("Flicker investigation", { size = 16, bold = true })
        root:Button("Start / stop flicker recording", tool.actions[7])
        root:Text("Start before approaching a problem area. Records detailed engine history; normal play keeps continuous safety counters.")
        root:Text("Recording stops after 60 seconds, or 1.5 seconds after a mark. Reports allow up to 5 seconds for outstanding diagnostic observations.")
        root:Button("Mark flicker now", tool.actions[8])
        root:Text("Marks this moment in an active recording.")
        root:Button("Save engine report / finish recording", tool.actions[9])
        root:Text("Always saves safety counters. Detailed history requires starting a recording first; missing or incomplete coverage is labelled in the log.")
        root:Divider()
        root:Text("Vehicle-light sample", { size = 16, bold = true })
        root:Button("Save vehicle shadow sample", tool.actions[10])
        root:Text("Captures vehicle lights and shadow sizes. Resume gameplay to complete the sample.")
        root:Text("Reports are saved in ShadowEnginePatch.log.")
        root:Divider()
        root:Text("Driver protection test", { size = 16, bold = true })
        root:Button("Toggle +2 extra slots", tool.toggleExtras)
        root:Text("Temporarily enables or disables the 2 extra traffic slots. Resets to 2 on game restart.")
        root:Divider()
        root:Text("Performance comparison", { size = 16, bold = true })
        root:Text("Tools v2.0.94 | Protocol SE10")
        root:Button("Measure shadow workload - 5 seconds", tool.actions[11])
        root:Text("Recording starts after 3 seconds and lasts 5 seconds. You may leave the menu open; use the same menu state when comparing runs.")
        root:Button("Check test progress", tool.refreshCapture)
        root:Text("Checks the last workload test's status without starting a new test or restarting its timer.")
        root:Text("Workload measurement does not start detailed flicker recording. Keep that recording stopped for normal-play cost comparisons.")
        root:Text("Captures patch CPU costs, shadow counts and presentation timing when Windows permits it. Keep applied settings unchanged between comparisons.")
    end
    tool.layouts.population = function(root)
        tool.populationRoot = root
        root:Text("Shadow onset recording", { size = 18, bold = true })
        root:Text("Use this when a nearby light or vehicle headlight takes too long to cast its shadow.")
        root:Text("Start before approaching the affected area, then drive or walk through it. Records the whole 60-second pass as samples every 50 ms and saves automatically.")
        root:Text("No reaction or mark is required. You can stop early after passing the area. Very brief changes between samples may be missed.")
        root:Button("Start shadow onset recording", tool.actions[14])
        root:Button("Stop shadow onset recording", tool.actions[16])
        root:Button("Check shadow onset progress", tool.refreshPopulation)
        root:Text(tool.populationMessage)
        root:Text("Wait for the saved result before starting again. Partial reports and saving failures are labelled. Reports are saved in ShadowEnginePatch.log.")
        root:Text("These controls require Shadow Engine 2.0.87. Flicker investigation and workload measurements have separate controls and progress.")
    end
    stage(tool.development and "register development layouts" or "register public shadow, traffic and support layouts")
    tool.pages.shadows:Layout(tool.layouts.shadows)
    tool.pages.traffic:Layout(tool.layouts.traffic)
    if tool.development then
        tool.pages.diagnostics:Layout(tool.layouts.diagnostics)
        tool.pages.population:Layout(tool.layouts.population)
    else
        tool.pages.support:Layout(tool.support.render)
    end
    tool.loaded = true
    pollStatus(true, true)
    -- NexusTools callback getters are placeholders, so never invoke OnUpdate
    -- ourselves. Its NextTick queue drains before the game-gated Script update.
    -- One retained callback owns the chain; an unloaded owner's callback stops.
    scheduleStatusPoll()
    stage("ready; native settings hydrated; no startup default or traffic request")
end

local ok, err = pcall(registerMenu)
if not ok then
    tool.loaded = false
    if tool.traffic then pcall(tool.traffic.restore) end
    if tool.script then pcall(function() tool.script:Remove() end) end
    stage("startup failed: " .. tostring(err))
end
