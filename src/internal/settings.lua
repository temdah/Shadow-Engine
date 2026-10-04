-- Presentation state only. The native owner validates, saves and applies changes.
local api = {}
-- Incremental construction stays below WD1's 31-register frame limit.
local fields = {}
for field in ("pid session ack disabled ready downstream profile vehicle intersection limit resolution worldEnabled worldResolution captureSerial captureState processedSeq result savedRevision appliedRevision loadReason saveError pendingSeq selectedLimit driverReady extraSlots populationSerial populationState populationPartial populationMarked populationWindowMs populationSampleMs populationElapsedMs hostCompatibility"):gmatch("%S+") do
    fields[#fields + 1] = field
end

function api.parse(text, path)
    if type(text) ~= "string" or #text >= 384 then return end
    local protocol, payload = text:match("^(SE%d+) ([0-9 %-]+)\n$")
    if protocol ~= "SE7" and protocol ~= "SE8" and protocol ~= "SE9" and protocol ~= "SE10" then return end
    local compatibility = protocol == "SE10"
    local population, wholePass = protocol ~= "SE7", protocol == "SE9" or compatibility
    local result, count = { path = path, populationAvailable = wholePass }, 0
    local pid, session, populationSerial
    for value in payload:gmatch("[^ \n]+") do
        count = count + 1
        if not fields[count] or not value:match("^%-?%d+$") then return end
        local negative = value:sub(1, 1) == "-"
        local digits = negative and value:sub(2) or value
        local ceiling = negative and "2147483648" or "4294967295"
        if #digits > 10 or (#digits == 10 and digits > ceiling) then return end
        local number = tonumber(value)
        if not number then return end
        result[fields[count]] = number
        if count == 1 then pid = value elseif count == 2 then session = value end
        if count == 26 then populationSerial = value end
    end
    if count ~= (compatibility and 33 or wholePass and 32 or population and 29 or 25) or result.pid <= 0 or result.session <= 0 or
        result.ack < 0 or result.processedSeq < 0 or result.pendingSeq < 0 or
        result.savedRevision < 0 or result.appliedRevision < 0 or result.saveError < 0 or
        result.disabled < 0 or result.disabled > 3 or result.ready < 0 or result.ready > 1 or
        result.downstream < -3 or result.downstream > 2 or result.limit < 1 or result.limit > 10 or
        result.worldEnabled < 0 or result.worldEnabled > 1 or
        result.result < -4 or result.result > 2 or result.loadReason < 0 or result.loadReason > 4 or
        result.captureSerial < 0 or result.captureState < 0 or result.captureState > 5 or
        result.selectedLimit < 0 or result.selectedLimit > 10 or
        result.driverReady < 0 or result.driverReady > 1 or
        (result.extraSlots ~= 0 and result.extraSlots ~= 2) then return end
    if population and (result.populationSerial < 0 or result.populationState < 0 or
        result.populationState > 6 or result.populationPartial < 0 or result.populationPartial > 1 or
        result.populationMarked < 0 or result.populationMarked > 1 or
        (result.populationState ~= 0 and result.populationSerial == 0)) then return end
    if wholePass and (result.populationWindowMs ~= 60000 or result.populationSampleMs ~= 50 or
        result.populationElapsedMs < 0 or result.populationElapsedMs > result.populationWindowMs) then return end
    if compatibility and (result.hostCompatibility < 0 or result.hostCompatibility == 2 or result.hostCompatibility > 5) then return end
    local bypass = result.selectedLimit == 0 or result.selectedLimit == 10
    if (result.disabled % 2 == 1) ~= bypass or
        (result.selectedLimit ~= 0 and result.limit ~= result.selectedLimit) then return end
    for _, key in ipairs({ "resolution", "worldResolution" }) do
        local value = result[key]
        if value ~= 512 and value ~= 1024 and value ~= 2048 and value ~= 4096 then return end
    end
    -- Preserve identifiers exactly even on hosts with single-precision Lua numbers.
    result.pid, result.session = pid, session
    if population then result.populationSerial = populationSerial end
    return result
end

local function nativeValue(status, key)
    if key == 1 then return status.selectedLimit end
    if key == 2 then
        if math.floor(status.disabled / 2) % 2 == 1 then return 0 end
        return status.resolution == 512 and 1 or status.resolution == 1024 and 2 or
            status.resolution == 2048 and 3 or status.resolution == 4096 and 4
    end
    if status.worldEnabled == 0 then return 0 end
    return status.worldResolution == 512 and 1 or status.worldResolution == 1024 and 2 or
        status.worldResolution == 2048 and 3 or status.worldResolution == 4096 and 4
end

local function choice(value, key)
    if value == nil or value == false then return "Unavailable" end
    if key == 1 then
        if value == 0 then return "0 (limiter off)" end
        if value == 10 then return "Unlimited (10)" end
        return string.format("%.0f", value)
    end
    return value == 0 and "Original" or string.format("%.0f", 256 * 2 ^ value)
end

function api.limitLabel(value) return choice(value, 1) end

function api.create(dependencies)
    local self = { desired = {}, roots = {}, callbacks = {}, message = "Open this page to check engine settings." }

    function self.syncSlider()
        if not self.slider or not self.current or self.unavailable then return end
        local readable, widget = pcall(SH_Commands_GetStateInt, self.slider)
        if not readable then
            self.sliderError = true
            dependencies.log("Limiter display read failed: " .. tostring(widget))
            return
        end
        -- A user drag may have updated the host widget while its callback is
        -- still queued. Preserve that newer intent before an older save result
        -- hydrates the widget. First hydration/session reset discard old state.
        if self.sliderHydrated and not self.resetSlider and widget ~= self.sliderObserved then
            self.sliderObserved = widget
            self.queueChoice(1, widget)
        end
        local value = self.value(1)
        if widget == value then
            self.sliderObserved = value
            self.sliderHydrated, self.resetSlider, self.sliderError = true, nil, nil
            return
        end
        -- The host setter returns no value and dispatches its callback even for
        -- unchanged values, sometimes later. Publish the echo guard first.
        self.sliderObserved = value
        local ok, err = pcall(SH_Commands_SetStateInt, self.slider, value)
        if not ok then
            self.sliderObserved = widget
            self.sliderError = true
            dependencies.log("Limiter display synchronization failed: " .. tostring(err))
        else
            self.sliderHydrated, self.resetSlider, self.sliderError = true, nil, nil
        end
    end

    function self.redraw()
        self.syncSlider()
        self.redrawing = true
        for _, root in pairs(self.roots) do pcall(function() root:Rerender() end) end
        self.redrawing = false
    end

    function self.feedback(message)
        self.message = message
        dependencies.log(message)
        dependencies.notify(message)
        self.redraw()
    end

    function self.value(key)
        if self.desired[key] ~= nil then return self.desired[key] end
        if self.inflight and self.inflight.values[key] ~= nil then return self.inflight.values[key] end
        return self.current and nativeValue(self.current, key)
    end

    function self.queueChoice(key, value)
        if not dependencies.loaded() then return end
        if key ~= 1 and key ~= 2 and key ~= 4 then return end
        if not self.current or self.unavailable then
            dependencies.notify("Engine status unavailable. Use Check current shadow settings, then try again.")
            return
        end
        if type(value) ~= "number" or value ~= math.floor(value) or value < 0 or
            value > (key == 1 and 10 or 4) then return end
        self.desired[key] = value
        self.quiet = 1
        if self.inflight and self.inflight.stalled then
            self.inflight.stalled = false
            self.inflight.polls, self.inflight.retries = 0, 0
        end
        self.message = "Change pending. Settings save and apply automatically."
        return true
    end

    function self.choose(key, value)
        if not dependencies.loaded() then return end
        dependencies.refresh()
        if self.queueChoice(key, value) then self.redraw() end
    end

    function self.sliderChanged()
        if not dependencies.loaded() then return end
        local value = SH_Commands_GetStateInt(self.slider)
        if value == self.sliderObserved then return end
        self.sliderObserved = value
        local previous = self.current
        dependencies.refresh()
        if previous and self.current and
            (previous.pid ~= self.current.pid or previous.session ~= self.current.session) then return end
        if self.queueChoice(1, value) then self.redraw() end
    end

    function self.observe(status)
        if not status then
            if not self.unavailable then
                self.unavailable = true
                self.message = "Engine status unavailable. Reopen this page or use Show applied engine settings to retry. Check ShadowEnginePatch.log if it stays unavailable."
                self.redraw()
            end
            return
        end
        local changed = not self.current or self.unavailable or
            self.current.appliedRevision ~= status.appliedRevision or
            self.current.disabled ~= status.disabled or self.current.limit ~= status.limit or
            self.current.selectedLimit ~= status.selectedLimit or self.current.driverReady ~= status.driverReady or
            self.current.resolution ~= status.resolution or self.current.worldResolution ~= status.worldResolution or
            self.current.worldEnabled ~= status.worldEnabled or
            self.current.ready ~= status.ready or self.current.downstream ~= status.downstream
        if self.unavailable and self.initialized then
            self.message = self.inflight and "Engine status available. Waiting for the settings result." or
                "Engine status available. Showing confirmed settings."
        end
        if self.current and (self.current.pid ~= status.pid or self.current.session ~= status.session) then
            self.desired, self.inflight = {}, nil
            self.resetSlider = true
            self.message = "New game session. Loaded the engine's saved settings."
            changed = true
        end
        self.unavailable = false
        self.current = status
        if not self.initialized then
            self.initialized = true
            self.message = status.loadReason == 0 and "Saved settings loaded by the engine." or
                status.loadReason == 1 and "Using defaults. Your first change will be saved." or
                "Saved settings could not be loaded. The engine is using defaults."
        end
        local pending = self.inflight
        if pending and status.processedSeq >= pending.sequence then
            self.inflight = nil
            local matches = true
            if status.processedSeq == pending.sequence and (status.result == 1 or status.result == 2) then
                for key, value in pairs(pending.values) do
                    if nativeValue(status, key) ~= value then
                        matches = false
                        if self.desired[key] == nil then self.desired[key] = value end
                    end
                end
            end
            if status.processedSeq ~= pending.sequence then
                self.feedback("Settings result was superseded. Showing confirmed engine values.")
            elseif not matches then
                self.feedback("Another request completed first. Your latest settings changes remain queued.")
            elseif status.result == 1 then self.feedback("Settings saved and applied to future shadow requests.")
            elseif status.result == 2 then
                self.feedback(status.savedRevision > 0 and "Settings already saved and applied." or
                    "Settings unchanged; the engine is using defaults.")
            elseif status.result == -3 then
                self.feedback("Settings could not be saved. Previous saved and active values were retained.")
            else self.feedback("Settings request was rejected. Showing confirmed engine values.") end
            changed = true
        elseif pending then
            pending.polls = pending.polls + 1
            if pending.polls >= 15 and not pending.stalled then
                pending.polls = 0
                pending.retries = pending.retries + 1
                if pending.retries <= 2 then dependencies.retry(status, pending.sequence)
                else
                    pending.stalled = true
                    self.feedback("Settings acknowledgment is delayed. Nothing is confirmed saved; another settings change retries safely.")
                end
            end
        end
        if changed then self.redraw() end
    end

    function self.flush()
        if self.inflight or self.unavailable or not self.current then return end
        if self.quiet and self.quiet > 0 then self.quiet = self.quiet - 1; return end
        local mask = 0
        for key in pairs(self.desired) do mask = mask + key end
        if mask == 0 then return end
        local sequence, failed = dependencies.submit(self.current, mask, self.desired)
        if sequence then
            self.inflight = { sequence = sequence, values = self.desired, polls = 0, retries = 0 }
            self.desired = {}
            self.message = "Saving settings. Active values remain shown until the engine confirms."
            self.redraw()
        elseif failed then
            self.desired = {}
            self.feedback("Settings request could not be written. Active values were retained; choose the setting again to retry.")
        end
    end

    function self.render(root, key)
        self.roots[key] = root
        if key == 1 then self.syncSlider() end
        if key == 1 then
            if self.current and not self.unavailable then root:CommandWidget(self.slider) end
        else
            local prefix = key == 2 and "Vehicle: " or "World: "
            for value = 0, 4 do root:Button(prefix .. choice(value, key), self.callbacks[key][value]) end
        end
    end

    for _, key in ipairs({ 2, 4 }) do
        self.callbacks[key] = {}
        for value = 0, 4 do
            local ownedKey, ownedValue = key, value
            self.callbacks[key][value] = function() self.choose(ownedKey, ownedValue) end
        end
    end
    function self.remove()
        self.roots, self.desired, self.inflight = {}, {}, nil
    end
    return self
end

return api
