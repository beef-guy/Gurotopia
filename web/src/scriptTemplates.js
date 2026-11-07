'use strict';
/*
 * Starter scripts for the "New script" form. Each is plain Lua that uses the API in include/scripting/lua.cpp,
 * so owners can read one, change a number and have a feature running in a minute.
 */
const TEMPLATES = [
  {
    key: 'blank',
    label: 'Empty script',
    text: 'Start from nothing.',
    source: `-- Write your Lua here. The panel on the right lists everything you can use.

on("join", function(player)
  player:message("\`2Welcome to " .. server.name .. ", " .. player.name .. "!")
end)
`,
  },
  {
    key: 'welcome',
    label: 'Welcome message',
    text: 'Greets players and counts how many times each one joined.',
    source: `-- Greets every player and remembers how many times they joined.

on("join", function(player)
  local visits = player:get("visits", 0) + 1
  player:set("visits", visits)

  if visits == 1 then
    player:overlay("\`2Welcome to " .. server.name .. "!")
    server.broadcast("\`9" .. player.name .. " joined for the first time. Say hi!")
  else
    player:message("\`2Welcome back, " .. player.name .. "! This is visit number " .. visits .. ".")
  end
end)
`,
  },
  {
    key: 'daily',
    label: '/daily reward',
    text: 'A command that gives gems once every 24 hours.',
    source: `-- /daily gives gems once a day.

local REWARD = 500          -- gems
local WAIT = 24 * 60 * 60   -- seconds between rewards

command("/daily", function(player)
  local last = player:get("daily_last", 0)
  local now = os.time()

  if now - last < WAIT then
    local left = WAIT - (now - last)
    player:message("\`4Come back in " .. math.floor(left / 3600) .. " hours and " .. math.floor(left % 3600 / 60) .. " minutes.")
    return
  end

  player:set("daily_last", now)
  player:addGems(REWARD)
  player:overlay("\`2You got " .. REWARD .. " gems!")
  player:sound("audio/success.wav")
end, "Get free gems once a day")
`,
  },
  {
    key: 'shop',
    label: 'Gem shop dialog',
    text: '/shop opens a menu where players buy items with gems.',
    source: `-- /shop opens a menu. Each button buys an item for gems.

local SHOP = {
  { id = 2,   count = 50, price = 100 },  -- 50 Dirt
  { id = 242, count = 1,  price = 2000 }, -- 1 World Lock
}

local function openShop(player)
  local d = Dialog("Gem shop", 112)
  d:text("You have \`2" .. player.gems .. "\`\` gems.")
  for i, offer in ipairs(SHOP) do
    d:button("buy" .. i, offer.count .. " " .. (item.name(offer.id) or "?") .. " for " .. offer.price .. " gems")
  end
  d:show(player, function(player, button)
    local offer = SHOP[tonumber(button:match("^buy(%d+)$") or "")]
    if not offer then return end
    if player.gems < offer.price then
      player:message("\`4You need " .. offer.price .. " gems for that.")
    elseif player:give(offer.id, offer.count) == 0 then
      player:message("\`4Your backpack is full.")
    else
      player:addGems(-offer.price)
      player:message("\`2Bought " .. offer.count .. " " .. item.name(offer.id) .. "!")
    end
    openShop(player)
  end)
end

command("/shop", openShop, "Buy items with gems")
`,
  },
  {
    key: 'announce',
    label: 'Automatic tips',
    text: 'Sends a tip to everyone online every few minutes.',
    source: `-- Every 10 minutes everyone online gets the next tip.

local TIPS = {
  "Type /daily for free gems every day!",
  "Type /shop to spend your gems.",
  "Be nice to each other!",
}
local next = 1

every(10 * 60, function()
  server.broadcast("\`5[TIP]\`\` " .. TIPS[next])
  next = next % #TIPS + 1
end)
`,
  },
  {
    key: 'lobby',
    label: 'Protected lobby world',
    text: 'Nobody but staff can break or build in START.',
    source: `-- Only staff can break or build in the START world.

local LOBBY = "START"

local function protected(player)
  return player.worldName == LOBBY and not player:isStaff()
end

on("punch", function(player, x, y, block)
  if protected(player) then
    player:bubble("\`4You can't break blocks in the lobby.")
    return true -- true stops the punch
  end
end)

on("place", function(player, x, y, itemId)
  if protected(player) then return true end
end)

on("enterWorld", function(player, world)
  if world.name == LOBBY then player:overlay("\`2Welcome to the lobby!") end
end)
`,
  },
  {
    key: 'miner',
    label: 'Lucky mining',
    text: 'Breaking blocks sometimes drops bonus gems.',
    source: `-- Each block broken has a 1 in 20 chance to give bonus gems.

on("break", function(player, x, y, block)
  if math.random(1, 20) == 1 then
    local gems = math.random(5, 25)
    player:addGems(gems)
    player:bubble("\`9Lucky! +" .. gems .. " gems")
  end
end)
`,
  },
];

const BY_KEY = new Map(TEMPLATES.map((t) => [t.key, t]));

module.exports = { TEMPLATES, get: (key) => BY_KEY.get(key) || BY_KEY.get('blank') };
