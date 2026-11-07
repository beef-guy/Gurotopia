'use strict';
/*
 * The reference beside the Lua editor: everything a script can use (include/scripting/lua.cpp).
 * Each entry: what to type, what it does, and a snippet that the Insert button puts in the editor.
 */
const SECTIONS = [
  {
    title: 'Events',
    text: 'on(event, function) runs your function every time it happens. Return true from chat, punch, place or wrench to stop it.',
    entries: [
      ['on("join", function(player) end)', 'A player logged in.', 'on("join", function(player)\n  player:message("Welcome, " .. player.name .. "!")\nend)\n'],
      ['on("leave", function(player) end)', 'A player is logging out.', 'on("leave", function(player)\n  server.broadcast(player.name .. " left.")\nend)\n'],
      ['on("enterWorld", function(player, world) end)', 'A player entered a world.', 'on("enterWorld", function(player, world)\n  player:overlay("Welcome to " .. world.name)\nend)\n'],
      ['on("leaveWorld", function(player, world) end)', 'A player is leaving a world.', 'on("leaveWorld", function(player, world)\n\nend)\n'],
      ['on("chat", function(player, text) end)', 'A player said something. Return true to hide it.', 'on("chat", function(player, text)\n  if text:lower():find("badword") then\n    player:message("`4Please be nice.")\n    return true\n  end\nend)\n'],
      ['on("punch", function(player, x, y, block) end)', 'A player punched a block. Return true to stop it.', 'on("punch", function(player, x, y, block)\n\nend)\n'],
      ['on("break", function(player, x, y, block) end)', 'A block was broken.', 'on("break", function(player, x, y, block)\n\nend)\n'],
      ['on("place", function(player, x, y, itemId) end)', 'A player placed a block or used an item on a tile. Return true to stop it.', 'on("place", function(player, x, y, itemId)\n\nend)\n'],
      ['on("wrench", function(player, x, y, block) end)', 'A player wrenched a tile. Return true to show your own menu instead.', 'on("wrench", function(player, x, y, block)\n\nend)\n'],
      ['on("start", function() end)', 'The scripts were (re)loaded.', 'on("start", function()\n  print("scripts loaded")\nend)\n'],
    ],
  },
  {
    title: 'Commands',
    text: 'Your own /commands. text is everything after the command, words is it split by spaces.',
    entries: [
      ['command("/name", function(player, text, words) end, "help text")', 'Anyone can use it. It shows in /help.', 'command("/hello", function(player, text, words)\n  player:message("Hello, " .. player.name .. "!")\nend, "Say hello")\n'],
      ['command("/name", fn, "help", "moderator")', 'Only moderators and up ("developer" for developers only).', 'command("/heal", function(player, text, words)\n\nend, "Staff only", "moderator")\n'],
    ],
  },
  {
    title: 'Player',
    text: 'player.name, player.id, player.world, player.worldName, player.x, player.y, player.level, player.gems, player.role, player.staff, player.online',
    entries: [
      ['player:message(text)', 'A line in their chat box. `2 makes it green, `4 red, `9 yellow.', 'player:message("`2Hello!")\n'],
      ['player:overlay(text)', 'Big text in the middle of their screen.', 'player:overlay("`2You won!")\n'],
      ['player:bubble(text)', 'A speech bubble over their head that only they see.', 'player:bubble("Hi!")\n'],
      ['player:say(text)', 'Makes them say it, like typing in chat.', 'player:say("I love this server!")\n'],
      ['player:give(itemId, count)', 'Puts items in their backpack. Returns how many fit (0 when full).', 'player:give(2, 10)\n'],
      ['player:take(itemId, count)', 'Takes items away. Returns how many were taken.', 'player:take(2, 10)\n'],
      ['player:has(itemId, count)', 'true if they have at least that many.', 'if player:has(242, 1) then\n\nend\n'],
      ['player:count(itemId)', 'How many of the item they have.', 'local locks = player:count(242)\n'],
      ['player:inventory()', 'A list of { id =, count = } for everything they carry.', 'for _, slot in ipairs(player:inventory()) do\n  print(item.name(slot.id), slot.count)\nend\n'],
      ['player:addGems(amount)', 'Gives gems (a negative amount takes them). Returns their new total.', 'player:addGems(100)\n'],
      ['player:setGems(amount)', 'Sets their gems.', 'player:setGems(0)\n'],
      ['player:addXp(amount)', 'Gives XP, levelling them up as needed.', 'player:addXp(50)\n'],
      ['player:warp(world)', 'Sends them to a world ("EXIT" for the world menu).', 'player:warp("START")\n'],
      ['player:teleport(x, y)', 'Moves them to a tile in their world.', 'player:teleport(50, 20)\n'],
      ['player:kick(reason)', 'Disconnects them.', 'player:kick("Bye!")\n'],
      ['player:sound(file)', 'Plays a game sound for them.', 'player:sound("audio/success.wav")\n'],
      ['player:isStaff() / player:isDeveloper()', 'Their staff powers.', 'if player:isStaff() then\n\nend\n'],
    ],
  },
  {
    title: 'Saving data',
    text: 'Saved values stay after restarts. Text, numbers and true/false can be saved.',
    entries: [
      ['player:get(name, default)', 'A value saved for this player, or default when there is none.', 'local coins = player:get("coins", 0)\n'],
      ['player:set(name, value)', 'Saves a value for this player. nil deletes it.', 'player:set("coins", player:get("coins", 0) + 1)\n'],
      ['server.get(name, default) / server.set(name, value)', 'The same, for the whole server.', 'server.set("visits", server.get("visits", 0) + 1)\n'],
    ],
  },
  {
    title: 'Worlds',
    text: 'world.name, world.width, world.height, world.ownerId, world.playerCount. A world is only there while someone is in it.',
    entries: [
      ['server.world(name)', 'An open world, or nil.', 'local w = server.world("START")\n'],
      ['world:getBlock(x, y) / world:getBackground(x, y)', 'The item id there (0 = nothing).', 'local id = world:getBlock(x, y)\n'],
      ['world:setBlock(x, y, itemId)', 'Puts a plain block there (0 removes it). Locks, doors and machines can\'t be placed or replaced.', 'world:setBlock(x, y, 2)\n'],
      ['world:setBackground(x, y, itemId)', 'Changes the background.', 'world:setBackground(x, y, 14)\n'],
      ['world:players()', 'Everyone in the world.', 'for _, p in ipairs(world:players()) do\n  p:message("Hi")\nend\n'],
      ['world:message(text) / world:overlay(text) / world:sound(file)', 'To everyone in the world.', 'world:message("`9Event starting!")\n'],
      ['world:drop(x, y, itemId, count)', 'Drops items on a tile.', 'world:drop(x, y, 112, 10)\n'],
    ],
  },
  {
    title: 'Server and items',
    entries: [
      ['server.players()', 'Everyone online.', 'for _, p in ipairs(server.players()) do\n  p:message("Hi everyone")\nend\n'],
      ['server.player(name)', 'An online player by name, or nil.', 'local p = server.player("Psy7")\n'],
      ['server.broadcast(text)', 'A message to everyone online.', 'server.broadcast("`5Double gems this hour!")\n'],
      ['server.online() / server.name', 'How many are online, and this server\'s name.', 'print(server.name, server.online())\n'],
      ['item.name(id) / item.find(name) / item.rarity(id)', 'Look items up. item.find("dirt") is 2.', 'local id = item.find("World Lock")\n'],
    ],
  },
  {
    title: 'Timers',
    entries: [
      ['after(seconds, function)', 'Runs once, later. Returns an id.', 'after(5, function()\n  player:message("5 seconds later")\nend)\n'],
      ['every(seconds, function)', 'Runs again and again. Returns an id.', 'every(60, function()\n  server.broadcast("Another minute!")\nend)\n'],
      ['cancel(id)', 'Stops a timer.', 'cancel(timer)\n'],
      ['os.time() / os.clock() / os.date(format)', 'Seconds since 1970, a precise clock, and a date (UTC).', 'print(os.date("%Y-%m-%d"))\n'],
    ],
  },
  {
    title: 'Dialogs',
    text: 'Menus with buttons and boxes. The function gets the button pressed and what was typed in the boxes.',
    entries: [
      ['Dialog(title, iconItemId)', 'Starts a menu. Then add lines and :show() it.', 'local d = Dialog("My menu", 242)\nd:text("Pick one:")\nd:button("yes", "Yes please")\nd:button("no", "No thanks")\nd:show(player, function(player, button, values)\n  player:message("You picked " .. button)\nend)\n'],
      [':text(t) :small(t) :item(id, t) :spacer()', 'Text lines, a line with an item\'s picture, a gap.', 'd:item(242, "A World Lock")\n'],
      [':button(name, label)', 'A button. Its name is what your function gets.', 'd:button("buy", "Buy")\n'],
      [':input(name, label, default, maxLength)', 'A text box. Read it with values.name.', 'd:input("amount", "How many?", "1", 5)\n'],
      [':checkbox(name, label, checked)', 'A tick box: values.name is "1" or "0".', 'd:checkbox("agree", "I agree", false)\n'],
      [':show(player, function, closeText, okText)', 'Opens it for the player.', 'd:show(player, function(player, button, values)\n\nend)\n'],
    ],
  },
  {
    title: 'Other',
    entries: [
      ['print(...)', 'Writes to the script log below.', 'print("hello", 123)\n'],
      ['split(text)', 'A list of the words in text.', 'local words = split(text)\n'],
      ['math.random(a, b)', 'A random whole number from a to b.', 'if math.random(1, 10) == 1 then\n\nend\n'],
    ],
  },
];

const LIMITS = [
  'Each event, command and timer may run for 100 ms. A script that takes longer is stopped and the error shows in the log.',
  'All scripts together may use 64 MB of memory.',
  'Files, os.execute, load and the debug library aren\'t there: scripts can only change the game.',
];

module.exports = { SECTIONS, LIMITS };
