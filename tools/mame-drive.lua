-- Lode Runner for Tiki-100 -- MAME automation driver
--
-- Loaded with MAME's -autoboot_script. It replays a list of steps against the
-- emulated machine using MAME's own facilities, so there is no synthetic
-- Windows input involved: MAME maps host keys by scancode and ignores
-- injected keystrokes, which is why keybd_event-style driving does not work
-- on the MAME window.
--
-- Steps arrive in the TIKI_MAME_STEPS environment variable, separated by
-- semicolons, and run in order:
--
--   wait:<frames>   let the machine run for N frames (tiki100 is ~50 Hz)
--   post:<text>     type text via the natural keyboard; "\n" is RETURN
--   hold:P|p  P|40  hold an ioport field down for N frames, then release it
--   ports           list every ioport tag/field, to find hold targets
--   bind:<tag>|<f>  report the host key bound to a matrix key
--   clk:<tag>       report a device's clock in Hz
--   snap            write a PNG into the snapshot directory
--   quit            exit MAME
--
-- A hold step returns immediately, so a following wait runs while the key is
-- still down. That matters because the game scans the keyboard matrix itself:
-- a natural-keyboard post taps a key for a frame or two, which the ~12 fps
-- game loop usually misses entirely.
--
-- A post step is not considered finished until the keyboard queue has
-- drained, so a following wait measures from the last character actually
-- typed rather than from the moment the text was queued.

local steps = {}
for item in (os.getenv("TIKI_MAME_STEPS") or ""):gmatch("[^;]+") do
    steps[#steps + 1] = item
end

local index = 1
local countdown = 0
local draining = false
local holds = {}

local function find_field(tag, name)
    local port = manager.machine.ioport.ports[":" .. tag]
    if not port then
        return nil, "no port :" .. tag
    end
    local field = port.fields[name]
    if not field then
        return nil, "no field " .. name .. " on :" .. tag
    end
    return field
end

local function unescape(text)
    return (text:gsub("\\n", "\n"):gsub("\\t", "\t"))
end

local function log(message)
    print("[tiki] " .. message)
    -- Start-Process does not capture MAME's stdout, so mirror the trace into a
    -- file that the PowerShell driver can read back.
    local path = os.getenv("TIKI_MAME_LOG")
    if path then
        local f = io.open(path, "a")
        if f then
            f:write(message .. "\n")
            f:close()
        end
    end
end

local function run_step(step)
    local verb, argument = step:match("^(%a+):?(.*)$")

    if verb == "wait" then
        countdown = tonumber(argument) or 0
    elseif verb == "post" then
        local text = unescape(argument)
        log("post " .. string.format("%q", text))
        manager.machine.natkeyboard:post(text)
        draining = true
    elseif verb == "snap" then
        log("snapshot")
        manager.machine.video:snapshot()
    elseif verb == "hold" then
        -- hold:<tag>|<field>|<frames> -- press a matrix key and keep it down.
        local tag, name, frames = argument:match("^([^|]+)|([^|]+)|(%d+)$")
        if tag then
            local field, err = find_field(tag, name)
            if field then
                field:set_value(1)
                holds[#holds + 1] = { field = field, frames = tonumber(frames) }
                log(string.format("hold %s/%s for %s", tag, name, frames))
            else
                log("hold failed: " .. tostring(err))
            end
        else
            log("bad hold step: " .. step)
        end
    elseif verb == "pc" then
        local ok, result = pcall(function ()
            return string.format("%04X", manager.machine.devices[":z80"].state["PC"].value)
        end)
        log("pc = " .. tostring(result))
    elseif verb == "ports" then
        local ok, result = pcall(function ()
            local out = {}
            for tag, port in pairs(manager.machine.ioport.ports) do
                for name in pairs(port.fields) do
                    out[#out + 1] = tag .. "/" .. name
                end
            end
            table.sort(out)
            return table.concat(out, " | ")
        end)
        log("ports: " .. tostring(result))
    elseif verb == "bind" then
        -- bind:<tag>|<field> -- report the host key currently bound to a
        -- matrix key, so controls can be documented without guessing.
        local tag, name = argument:match("^([^|]+)|(.+)$")
        local field, err = find_field(tag, name)
        if not field then
            log("bind: " .. tostring(err))
        else
            local ok, result = pcall(function ()
                local input = manager.machine.input
                return input:seq_name(field:input_seq("standard"))
            end)
            log("bind: " .. tag .. "/" .. name .. " = " .. tostring(result)
                .. " mask=" .. string.format("0x%02X", field.mask))
        end
    elseif verb == "clk" then
        -- clk:<tag> -- report a device's clock, e.g. to derive AY tone periods.
        local ok, result = pcall(function ()
            local dev = manager.machine.devices[argument]
            if not dev then return "no device " .. argument end
            return tostring(dev.clock)
        end)
        log("clk: " .. argument .. " = " .. tostring(result))
    elseif verb == "devs" then
        local ok, result = pcall(function ()
            local out = {}
            for tag in pairs(manager.machine.devices) do
                out[#out + 1] = tag
            end
            table.sort(out)
            return table.concat(out, " ")
        end)
        log("devs: " .. tostring(result))
    elseif verb == "peek" then
        -- peek:<hexaddr>:<count> -- dump Z80 memory, for inspecting game state
        -- without having to read digits back out of a screenshot.
        local addr, count = argument:match("^(%x+):(%d+)$")
        if addr then
            local base = tonumber(addr, 16)
            local ok, result = pcall(function ()
                local space = manager.machine.devices[":z80"].spaces["program"]
                local out = {}
                for i = 0, tonumber(count) - 1 do
                    out[#out + 1] = string.format("%02X", space:read_u8(base + i))
                end
                return table.concat(out, " ")
            end)
            if ok then
                log(string.format("peek %04X = %s", base, result))
            else
                log("peek failed: " .. tostring(result))
            end
        else
            log("bad peek step: " .. step)
        end
    elseif verb == "quit" then
        log("exit")
        manager.machine:exit()
    else
        log("unknown step: " .. step)
    end
end

-- The notifier subscription must stay referenced: MAME cancels the callback
-- when the returned token is garbage-collected, so this global is load-bearing
-- rather than decorative.
tiki_subscription = emu.add_machine_frame_notifier(function ()
    -- Release expired holds first, so a key held for N frames is down for
    -- exactly the N frames that follow the hold step.
    for i = #holds, 1, -1 do
        local entry = holds[i]
        entry.frames = entry.frames - 1
        if entry.frames <= 0 then
            entry.field:set_value(0)
            table.remove(holds, i)
        end
    end

    if index > #steps then
        return
    end

    -- Hold here until the natural keyboard has typed everything queued by the
    -- previous post step.
    if draining then
        if manager.machine.natkeyboard.is_posting then
            return
        end
        draining = false
    end

    if countdown > 0 then
        countdown = countdown - 1
        return
    end

    local step = steps[index]
    index = index + 1
    run_step(step)
end)

log(#steps .. " steps queued")
