-- HelloWorld addon — test the WoWee addon system
print("|cff00ff00[HelloWorld]|r Addon loaded! Lua 5.1 is working.")
print("|cff00ff00[HelloWorld]|r GetTime() = " .. string.format("%.2f", GetTime()) .. " seconds")

-- Query player info (will show real data when called after world entry)
local name = UnitName("player")
local level = UnitLevel("player")
local health = UnitHealth("player")
local maxHealth = UnitHealthMax("player")
local gold = math.floor(GetMoney() / 10000)

print("|cff00ff00[HelloWorld]|r Player: " .. name .. " (Level " .. level .. ")")
if maxHealth > 0 then
    print("|cff00ff00[HelloWorld]|r Health: " .. health .. "/" .. maxHealth)
end
if gold > 0 then
    print("|cff00ff00[HelloWorld]|r Gold: " .. gold .. "g")
end
