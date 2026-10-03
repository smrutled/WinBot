-- data/tools/calculator.lua
-- Comprehensive Windows Calculator automation tool suite for WinBot.

local ButtonMap = {
    ['0'] = "Zero",
    ['1'] = "One",
    ['2'] = "Two",
    ['3'] = "Three",
    ['4'] = "Four",
    ['5'] = "Five",
    ['6'] = "Six",
    ['7'] = "Seven",
    ['8'] = "Eight",
    ['9'] = "Nine",
    ['.'] = "Decimal separator",
    ['+'] = "Plus",
    ['-'] = "Minus",
    ['*'] = "Multiply by",
    ['/'] = "Divide by",
    ['='] = "Equals",
    ['%'] = "Percent",
    ['c'] = "Clear",
    ['C'] = "Clear"
}

local function getOrLaunchCalculator(timeoutMs)
    timeoutMs = timeoutMs or 4000
    local win = winbot.waitForWindow("Calculator", 1000)
    if not win then
        pcall(function() winbot.shell("start calc.exe") end)
        win = winbot.waitForWindow("Calculator", timeoutMs)
    end
    return win
end

local function parseDisplayValue(rawText)
    if not rawText then return "" end
    -- Windows Calculator formats as "Display is 42"
    local clean = rawText:gsub("^Display is%s*", "")
    return clean
end

local tools = {}

-- 1. calc_calculate: Evaluates a full math expression
tools[1] = {
    name = "calc_calculate",
    description = "Evaluates an arithmetic expression on Windows Calculator and returns the result (e.g. '12.5 + 7 * 2').",
    parameters = {
        type = "object",
        properties = {
            expression = {
                type = "string",
                description = "Arithmetic expression to calculate, e.g. '12 + 34' or '100 / 4'"
            },
            clear_first = {
                type = "boolean",
                description = "Whether to clear the calculator before entering the expression (default: true)"
            }
        },
        required = {"expression"}
    },
    execute = function(args)
        local win = getOrLaunchCalculator(4000)
        if not win then return nil, "Failed to connect to Windows Calculator" end

        if args.clear_first ~= false then
            pcall(function() win:select("Clear", 500):click() end)
        end

        local expr = tostring(args.expression or "")
        for i = 1, #expr do
            local ch = expr:sub(i, i)
            if ch ~= " " then
                local btnName = ButtonMap[ch] or ch
                local btn = win:select(btnName, 1000)
                if btn then
                    btn:click()
                else
                    win:type(ch)
                end
            end
        end

        -- Ensure Equals is clicked if expression didn't end with '='
        if not expr:match("=%s*$") then
            local eqBtn = win:select("Equals", 1000)
            if eqBtn then eqBtn:click() end
        end

        local resultsEl = win:select("CalculatorResults", 2000)
        if resultsEl then
            resultsEl:refresh()
            local raw = resultsEl:name()
            local clean = parseDisplayValue(raw)
            return {
                expression = expr,
                result = clean,
                raw_display = raw
            }
        end

        return { expression = expr, status = "executed" }
    end
}

-- 2. calc_press: Clicks a specific calculator button
tools[2] = {
    name = "calc_press",
    description = "Presses a specific button on Windows Calculator (e.g. 'One', 'Plus', 'Two', 'Equals', 'Clear', 'Square root').",
    parameters = {
        type = "object",
        properties = {
            button = {
                type = "string",
                description = "Name of the button to click (e.g. 'One', 'Plus', 'Equals', 'Clear', 'Five', 'Multiply by')"
            }
        },
        required = {"button"}
    },
    execute = function(args)
        local win = getOrLaunchCalculator(4000)
        if not win then return nil, "Failed to connect to Windows Calculator" end

        local target = tostring(args.button or "")
        local btnName = ButtonMap[target] or target
        local btn = win:select(btnName, 2000)
        if not btn then
            return nil, "Button '" .. target .. "' not found in Calculator"
        end

        btn:click()

        local resultsEl = win:select("CalculatorResults", 1000)
        local display = ""
        if resultsEl then
            resultsEl:refresh()
            display = parseDisplayValue(resultsEl:name())
        end

        return {
            button_pressed = target,
            current_display = display
        }
    end
}

-- 3. calc_get_display: Reads the current display
tools[3] = {
    name = "calc_get_display",
    description = "Reads and returns the current value shown on the Windows Calculator display.",
    parameters = {
        type = "object",
        properties = {}
    },
    execute = function(args)
        local win = getOrLaunchCalculator(3000)
        if not win then return nil, "Calculator window not open" end

        local resultsEl = win:select("CalculatorResults", 2000)
        if not resultsEl then
            return nil, "Could not locate CalculatorResults element"
        end

        resultsEl:refresh()
        local raw = resultsEl:name()
        return {
            display = parseDisplayValue(raw),
            raw = raw
        }
    end
}

-- 4. calc_clear: Clears the calculator
tools[4] = {
    name = "calc_clear",
    description = "Clears the Windows Calculator display and calculation state (equivalent to pressing 'C').",
    parameters = {
        type = "object",
        properties = {}
    },
    execute = function(args)
        local win = getOrLaunchCalculator(3000)
        if not win then return nil, "Calculator window not open" end

        local clearBtn = win:select("Clear", 1500)
        if clearBtn then
            clearBtn:click()
        else
            win:key("Escape")
        end

        return { status = "cleared", display = "0" }
    end
}

return tools
