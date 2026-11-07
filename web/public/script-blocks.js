'use strict';
// The block editor: PSNode's own Blockly blocks, the Lua they make, saving, and turning a block script into code.
(function () {
  const host = document.getElementById('blockly');
  const Blockly = window.Blockly;
  if (!host || !Blockly || !window.lua || !window.psScripts) return;
  const ps = window.psScripts;
  const { luaGenerator: gen, Order } = window.lua;

  /* ---------- the blocks ---------- */
  const SOUNDS = [
    ['success', 'audio/success.wav'], ['fanfare', 'audio/fanfare.wav'], ['level up', 'audio/levelup2.wav'],
    ['cash register', 'audio/cash_register.wav'], ['secret', 'audio/secret.wav'], ['teleport', 'audio/teleport.wav'],
    ['door', 'audio/door_open.wav'], ['beep', 'audio/beep.wav'], ['fail', 'audio/bleep_fail.wav'],
    ['piano', 'audio/piano_nice.wav'], ['page turn', 'audio/page_turn.wav'], ['evil laugh', 'audio/boo_evil_laugh.wav'],
    ['splash', 'audio/splash.wav'], ['race start', 'audio/race_start.wav'],
  ];
  const ROLES = [['everyone', 'player'], ['moderators and up', 'moderator'], ['developers only', 'developer']];
  const PROPS = [
    ['name', 'name'], ['gems', 'gems'], ['level', 'level'], ['world name', 'worldName'],
    ['x position', 'x'], ['y position', 'y'], ['role', 'role'], ['user id', 'id'],
  ];

  const ev = (type, message, tooltip, extra = {}) => ({
    type, message0: message, message1: 'do %1', args1: [{ type: 'input_statement', name: 'DO' }],
    style: 'ps_event', tooltip, ...extra,
  });
  const playerIn = (name = 'PLAYER') => ({ type: 'input_value', name, check: null });
  const stmt = (type, message, args, style, tooltip) => ({
    type, message0: message, args0: args, previousStatement: null, nextStatement: null, inputsInline: true, style, tooltip,
  });
  const val = (type, message, args, output, style, tooltip) => ({
    type, message0: message, args0: args, output, inputsInline: true, style, tooltip,
  });

  Blockly.common.defineBlocksWithJsonArray([
    /* @note events */
    ev('ps_on_join', 'when a player joins the server', 'Runs when someone logs in.'),
    ev('ps_on_leave', 'when a player leaves the server', 'Runs when someone logs out.'),
    ev('ps_on_enter', 'when a player enters a world', 'Runs when someone walks into any world.'),
    ev('ps_on_exit', 'when a player leaves a world', 'Runs when someone leaves a world.'),
    {
      type: 'ps_on_command', style: 'ps_event',
      message0: 'when a player types /%1', args0: [{ type: 'field_input', name: 'NAME', text: 'daily' }],
      message1: 'help text %1', args1: [{ type: 'field_input', name: 'DESC', text: 'Get a reward' }],
      message2: 'who can use it %1', args2: [{ type: 'field_dropdown', name: 'ROLE', options: ROLES }],
      message3: 'do %1', args3: [{ type: 'input_statement', name: 'DO' }],
      tooltip: 'Your own command. It shows in /help.',
    },
    ev('ps_on_chat', 'when a player says something', 'Runs for every chat message. Use "stop it" to hide the message.'),
    ev('ps_on_punch', 'when a player punches a block', 'Runs on every punch. Use "stop it" to stop the punch.'),
    ev('ps_on_break', 'when a player breaks a block', 'Runs after a block is broken.'),
    ev('ps_on_place', 'when a player places a block', 'Also runs when they use an item on a tile. Use "stop it" to stop it.'),
    ev('ps_on_wrench', 'when a player wrenches a block', 'Use "stop it" to show your own menu instead of the game\'s.'),
    {
      type: 'ps_every', style: 'ps_event',
      message0: 'every %1 seconds', args0: [{ type: 'field_number', name: 'SECONDS', value: 60, min: 0.1, max: 604800 }],
      message1: 'do %1', args1: [{ type: 'input_statement', name: 'DO' }],
      tooltip: 'Runs again and again. There is no "the player" here: use "for each player online".',
    },
    ev('ps_on_start', 'when the scripts start', 'Runs once each time the scripts are loaded (server start, or after saving a script).'),

    /* @note what the event is about */
    val('ps_player', 'the player', [], null, 'ps_value', 'The player this is about.'),
    val('ps_message', 'the chat message', [], 'String', 'ps_value', 'What they said (in "when a player says something").'),
    val('ps_command_text', 'the text after the command', [], 'String', 'ps_value', 'For /give Psy7 10 it is "Psy7 10".'),
    val('ps_command_word', 'word %1 after the command', [{ type: 'field_number', name: 'N', value: 1, min: 1, max: 20, precision: 1 }], 'String', 'ps_value', 'For /give Psy7 10, word 1 is "Psy7" and word 2 is "10".'),
    val('ps_tile_x', 'x of the block', [], 'Number', 'ps_value', 'Where the block is (0 is the left edge).'),
    val('ps_tile_y', 'y of the block', [], 'Number', 'ps_value', 'Where the block is (0 is the top).'),
    val('ps_tile_block', 'the block\'s item', [], 'Number', 'ps_value', 'The item id of the block.'),
    stmt('ps_stop', 'stop it', [], 'ps_value', 'Stops what the player did: hides the chat message, or stops the punch, placing or wrench.'),

    /* @note doing things to a player */
    stmt('ps_msg', 'send message %1 to %2', [{ type: 'input_value', name: 'TEXT' }, playerIn()], 'ps_player', 'A line in their chat box. `2 is green, `4 red, `9 yellow.'),
    stmt('ps_overlay', 'show big text %1 to %2', [{ type: 'input_value', name: 'TEXT' }, playerIn()], 'ps_player', 'Text in the middle of their screen.'),
    stmt('ps_bubble', 'show bubble %1 over %2', [{ type: 'input_value', name: 'TEXT' }, playerIn()], 'ps_player', 'A speech bubble only they see.'),
    stmt('ps_say', 'make %1 say %2', [playerIn(), { type: 'input_value', name: 'TEXT' }], 'ps_player', 'Like they typed it in chat.'),
    stmt('ps_give', 'give %1 %2 of %3', [playerIn(), { type: 'input_value', name: 'COUNT', check: 'Number' }, { type: 'input_value', name: 'ITEM', check: 'Number' }], 'ps_player', 'Puts items in their backpack (up to 200).'),
    stmt('ps_take', 'take %1 of %2 from %3', [{ type: 'input_value', name: 'COUNT', check: 'Number' }, { type: 'input_value', name: 'ITEM', check: 'Number' }, playerIn()], 'ps_player', 'Takes items from their backpack, as many as they have.'),
    stmt('ps_gems_add', 'give %1 %2 gems', [playerIn(), { type: 'input_value', name: 'AMOUNT', check: 'Number' }], 'ps_player', 'Adds gems.'),
    stmt('ps_gems_take', 'take %1 gems from %2', [{ type: 'input_value', name: 'AMOUNT', check: 'Number' }, playerIn()], 'ps_player', 'Takes gems (never below 0).'),
    stmt('ps_xp', 'give %1 %2 XP', [playerIn(), { type: 'input_value', name: 'AMOUNT', check: 'Number' }], 'ps_player', 'XP levels them up.'),
    stmt('ps_warp', 'send %1 to world %2', [playerIn(), { type: 'input_value', name: 'WORLD', check: 'String' }], 'ps_player', 'Warps them to a world.'),
    stmt('ps_teleport', 'move %1 to x %2 y %3', [playerIn(), { type: 'input_value', name: 'X', check: 'Number' }, { type: 'input_value', name: 'Y', check: 'Number' }], 'ps_player', 'Moves them to a tile in the world they are in.'),
    stmt('ps_kick', 'kick %1 because %2', [playerIn(), { type: 'input_value', name: 'TEXT' }], 'ps_player', 'Disconnects them.'),
    stmt('ps_sound', 'play sound %1 for %2', [{ type: 'field_dropdown', name: 'SOUND', options: SOUNDS.map(([a, b]) => [a, b]) }, playerIn()], 'ps_player', 'A game sound only they hear.'),

    /* @note about a player */
    val('ps_player_prop', '%1 of %2', [{ type: 'field_dropdown', name: 'PROP', options: PROPS }, playerIn()], null, 'ps_player', 'Something about the player.'),
    val('ps_player_has', '%1 has %2 of %3', [playerIn(), { type: 'input_value', name: 'COUNT', check: 'Number' }, { type: 'input_value', name: 'ITEM', check: 'Number' }], 'Boolean', 'ps_player', 'true if they have at least that many.'),
    val('ps_player_count', 'how many %1 %2 has', [{ type: 'input_value', name: 'ITEM', check: 'Number' }, playerIn()], 'Number', 'ps_player', 'How many of the item are in their backpack.'),
    val('ps_player_staff', '%1 is staff', [playerIn()], 'Boolean', 'ps_player', 'true for moderators and developers.'),
    val('ps_all_players', 'everyone online', [], 'Array', 'ps_world', 'A list of all players. Use it with "for each item in list".'),
    val('ps_world_players', 'everyone in the world of %1', [playerIn()], 'Array', 'ps_world', 'A list of the players in the same world.'),
    val('ps_online_count', 'number of players online', [], 'Number', 'ps_world', ''),
    val('ps_find_player', 'online player named %1', [{ type: 'input_value', name: 'NAME', check: 'String' }], null, 'ps_world', 'The player, or nothing if they are not online.'),

    /* @note items */
    val('ps_item', 'item %1', [{ type: 'field_number', name: 'ID', value: 2, min: 0, max: 32767, precision: 1 }], 'Number', 'ps_item', 'An item by its id. Find ids in the Item manager. 2 is Dirt, 242 is World Lock, 112 is Gems.'),
    val('ps_item_named', 'item named %1', [{ type: 'field_input', name: 'NAME', text: 'World Lock' }], 'Number', 'ps_item', 'An item by its exact name.'),
    val('ps_item_name', 'name of %1', [{ type: 'input_value', name: 'ITEM', check: 'Number' }], 'String', 'ps_item', 'The item\'s name.'),

    /* @note the world */
    stmt('ps_world_msg', 'tell everyone in the world of %1 %2', [playerIn(), { type: 'input_value', name: 'TEXT' }], 'ps_world', 'A chat line for everyone in that world.'),
    stmt('ps_broadcast', 'tell everyone online %1', [{ type: 'input_value', name: 'TEXT' }], 'ps_world', 'A chat line for the whole server.'),
    stmt('ps_set_block', 'put %1 at x %2 y %3 in the world of %4', [{ type: 'input_value', name: 'ITEM', check: 'Number' }, { type: 'input_value', name: 'X', check: 'Number' }, { type: 'input_value', name: 'Y', check: 'Number' }, playerIn()], 'ps_world', 'A plain block (0 removes it). Locks, doors and machines can\'t be placed or replaced.'),
    val('ps_get_block', 'block at x %1 y %2 in the world of %3', [{ type: 'input_value', name: 'X', check: 'Number' }, { type: 'input_value', name: 'Y', check: 'Number' }, playerIn()], 'Number', 'ps_world', 'The item id of the block there (0 = empty).'),
    stmt('ps_drop', 'drop %1 of %2 at x %3 y %4 in the world of %5', [{ type: 'input_value', name: 'COUNT', check: 'Number' }, { type: 'input_value', name: 'ITEM', check: 'Number' }, { type: 'input_value', name: 'X', check: 'Number' }, { type: 'input_value', name: 'Y', check: 'Number' }, playerIn()], 'ps_world', 'Drops items on the ground.'),

    /* @note saved values */
    stmt('ps_player_set', 'save %1 as %2 for %3', [{ type: 'input_value', name: 'VALUE' }, { type: 'field_input', name: 'KEY', text: 'coins' }, playerIn()], 'ps_data', 'Saved values stay after the server restarts.'),
    stmt('ps_player_add', 'change saved %1 of %2 by %3', [{ type: 'field_input', name: 'KEY', text: 'coins' }, playerIn(), { type: 'input_value', name: 'N', check: 'Number' }], 'ps_data', 'Adds to a saved number (it starts at 0).'),
    val('ps_player_get', 'saved %1 of %2', [{ type: 'field_input', name: 'KEY', text: 'coins' }, playerIn()], null, 'ps_data', 'A value saved for the player (0 if nothing was saved).'),
    stmt('ps_server_set', 'save %1 as %2 for the server', [{ type: 'input_value', name: 'VALUE' }, { type: 'field_input', name: 'KEY', text: 'record' }], 'ps_data', 'One value for the whole server.'),
    stmt('ps_server_add', 'change saved %1 of the server by %2', [{ type: 'field_input', name: 'KEY', text: 'record' }, { type: 'input_value', name: 'N', check: 'Number' }], 'ps_data', ''),
    val('ps_server_get', 'saved %1 of the server', [{ type: 'field_input', name: 'KEY', text: 'record' }], null, 'ps_data', '0 if nothing was saved.'),
    val('ps_now', 'seconds since 1970', [], 'Number', 'ps_data', 'The time now. Save it to make cooldowns: now minus the saved time is how long ago it was.'),

    /* @note timers */
    {
      type: 'ps_after', style: 'ps_timer', previousStatement: null, nextStatement: null,
      message0: 'after %1 seconds', args0: [{ type: 'input_value', name: 'SECONDS', check: 'Number' }],
      message1: 'do %1', args1: [{ type: 'input_statement', name: 'DO' }],
      inputsInline: true, tooltip: 'Waits, then runs the blocks inside. Everything else goes on meanwhile.',
    },
    stmt('ps_log', 'write %1 in the log', [{ type: 'input_value', name: 'TEXT' }], 'ps_timer', 'Shows in the script log under the editor. Handy to see what happens.'),

    /* @note menus */
    {
      type: 'ps_dialog', style: 'ps_dialog', previousStatement: null, nextStatement: null,
      message0: 'show a menu to %1 titled %2 with icon %3', args0: [playerIn(), { type: 'input_value', name: 'TITLE' }, { type: 'input_value', name: 'ICON', check: 'Number' }],
      message1: 'lines %1', args1: [{ type: 'input_statement', name: 'LINES', check: 'MenuLine' }],
      message2: 'when a button is pressed %1', args2: [{ type: 'input_statement', name: 'PRESS' }],
      inputsInline: true, tooltip: 'A menu with text, buttons and boxes. Put menu lines inside.',
    },
    { type: 'ps_menu_text', message0: 'text %1', args0: [{ type: 'input_value', name: 'TEXT' }], previousStatement: 'MenuLine', nextStatement: 'MenuLine', style: 'ps_dialog', inputsInline: true, tooltip: 'A line of text.' },
    { type: 'ps_menu_item', message0: 'picture of %1 with text %2', args0: [{ type: 'input_value', name: 'ITEM', check: 'Number' }, { type: 'input_value', name: 'TEXT' }], previousStatement: 'MenuLine', nextStatement: 'MenuLine', style: 'ps_dialog', inputsInline: true, tooltip: 'A line with an item\'s picture.' },
    { type: 'ps_menu_button', message0: 'button %1 that says %2', args0: [{ type: 'field_input', name: 'ID', text: 'buy' }, { type: 'input_value', name: 'TEXT' }], previousStatement: 'MenuLine', nextStatement: 'MenuLine', style: 'ps_dialog', inputsInline: true, tooltip: 'Give each button its own name (letters and digits) and check it with "button … was pressed".' },
    { type: 'ps_menu_input', message0: 'text box %1 asking %2', args0: [{ type: 'field_input', name: 'ID', text: 'amount' }, { type: 'input_value', name: 'TEXT' }], previousStatement: 'MenuLine', nextStatement: 'MenuLine', style: 'ps_dialog', inputsInline: true, tooltip: 'A box players type in. Read it with "what was typed in box".' },
    { type: 'ps_menu_spacer', message0: 'empty line', previousStatement: 'MenuLine', nextStatement: 'MenuLine', style: 'ps_dialog', tooltip: 'Some space.' },
    val('ps_menu_pressed', 'button %1 was pressed', [{ type: 'field_input', name: 'ID', text: 'buy' }], 'Boolean', 'ps_dialog', 'Use inside "when a button is pressed".'),
    val('ps_menu_value', 'what was typed in box %1', [{ type: 'field_input', name: 'ID', text: 'amount' }], 'String', 'ps_dialog', 'Use inside "when a button is pressed".'),
  ]);

  /* ---------- where blocks may be used ---------- */
  const PLAYER_EVENTS = ['ps_on_join', 'ps_on_leave', 'ps_on_enter', 'ps_on_exit', 'ps_on_command', 'ps_on_chat', 'ps_on_punch', 'ps_on_break', 'ps_on_place', 'ps_on_wrench'];
  const NEEDS = {
    ps_player: [PLAYER_EVENTS.concat('ps_dialog_press'), '"the player" only works inside an event about a player (not in "every … seconds" or "when the scripts start").'],
    ps_message: [['ps_on_chat'], 'Use this inside "when a player says something".'],
    ps_command_text: [['ps_on_command'], 'Use this inside "when a player types /…".'],
    ps_command_word: [['ps_on_command'], 'Use this inside "when a player types /…".'],
    ps_tile_x: [['ps_on_punch', 'ps_on_break', 'ps_on_place', 'ps_on_wrench'], 'Use this inside a block event (punch, break, place, wrench).'],
    ps_tile_y: [['ps_on_punch', 'ps_on_break', 'ps_on_place', 'ps_on_wrench'], 'Use this inside a block event (punch, break, place, wrench).'],
    ps_tile_block: [['ps_on_punch', 'ps_on_break', 'ps_on_place', 'ps_on_wrench'], 'Use this inside a block event (punch, break, place, wrench).'],
    ps_stop: [['ps_on_chat', 'ps_on_punch', 'ps_on_place', 'ps_on_wrench'], '"stop it" works in: says something, punches, places, wrenches.'],
    ps_menu_pressed: [['ps_dialog_press'], 'Use this inside "when a button is pressed" of a menu.'],
    ps_menu_value: [['ps_dialog_press'], 'Use this inside "when a button is pressed" of a menu.'],
  };
  /* @return the events (and 'ps_dialog_press' for a menu's button part) this block sits in */
  function contexts(block) {
    const out = new Set();
    let child = block;
    for (let parent = block.getSurroundParent(); parent; child = parent, parent = parent.getSurroundParent()) {
      if (parent.type === 'ps_dialog') {
        // @note is the child in the PRESS part?
        for (let b = parent.getInputTargetBlock('PRESS'); b; b = b.getNextBlock()) {
          if (b === child || b.getDescendants(false).includes(block)) out.add('ps_dialog_press');
        }
      }
      out.add(parent.type);
    }
    return out;
  }
  function checkPlace(block) {
    if (!block.workspace || block.isInFlyout) return;
    const rule = NEEDS[block.type];
    if (!rule) return;
    const where = contexts(block);
    const ok = rule[0].some((t) => where.has(t));
    block.setWarningText(ok || !block.getSurroundParent() ? null : rule[1]);
  }
  for (const type of Object.keys(NEEDS)) {
    const def = Blockly.Blocks[type];
    const init = def.init;
    def.init = function () { init.call(this); this.setOnChange(() => checkPlace(this)); };
  }
  const commandField = Blockly.Blocks.ps_on_command;
  const commandInit = commandField.init;
  commandField.init = function () {
    commandInit.call(this);
    this.getField('NAME').setValidator((v) => String(v).toLowerCase().replace(/^\/+/, '').replace(/[^a-z0-9_]/g, '').slice(0, 24) || null);
  };
  for (const type of ['ps_menu_button', 'ps_menu_input', 'ps_menu_pressed', 'ps_menu_value']) {
    const def = Blockly.Blocks[type];
    const init = def.init;
    def.init = function () { init.call(this); this.getField('ID').setValidator((v) => String(v).replace(/[^A-Za-z0-9_]/g, '').slice(0, 24) || null); };
  }

  /* ---------- the Lua they make ---------- */
  const q = (s) => gen.quote_(String(s));
  const v = (block, name, fallback) => gen.valueToCode(block, name, Order.NONE) || fallback;
  const body = (block, name = 'DO') => gen.statementToCode(block, name);
  const P = (block) => v(block, 'PLAYER', 'player');
  const f = gen.forBlock;

  const onEvent = (name, params) => (block) => `on(${q(name)}, function(${params})\n${body(block)}end)\n\n`;
  f.ps_on_join = onEvent('join', 'player');
  f.ps_on_leave = onEvent('leave', 'player');
  f.ps_on_enter = onEvent('enterWorld', 'player, world');
  f.ps_on_exit = onEvent('leaveWorld', 'player, world');
  f.ps_on_chat = onEvent('chat', 'player, message');
  f.ps_on_punch = onEvent('punch', 'player, x, y, block');
  f.ps_on_break = onEvent('break', 'player, x, y, block');
  f.ps_on_place = onEvent('place', 'player, x, y, block');
  f.ps_on_wrench = onEvent('wrench', 'player, x, y, block');
  f.ps_on_start = onEvent('start', '');
  f.ps_on_command = (block) => `command(${q('/' + block.getFieldValue('NAME'))}, function(player, text, words)\n${body(block)}end, ${q(block.getFieldValue('DESC'))}, ${q(block.getFieldValue('ROLE'))})\n\n`;
  f.ps_every = (block) => `every(${Number(block.getFieldValue('SECONDS')) || 60}, function()\n${body(block)}end)\n\n`;

  f.ps_player = () => ['player', Order.ATOMIC];
  f.ps_message = () => ['message', Order.ATOMIC];
  f.ps_command_text = () => ['text', Order.ATOMIC];
  f.ps_command_word = (block) => [`(words[${Number(block.getFieldValue('N')) || 1}] or "")`, Order.HIGH];
  f.ps_tile_x = () => ['x', Order.ATOMIC];
  f.ps_tile_y = () => ['y', Order.ATOMIC];
  f.ps_tile_block = () => ['block', Order.ATOMIC];
  f.ps_stop = () => 'do return true end\n';

  f.ps_msg = (b) => `${P(b)}:message(${v(b, 'TEXT', '""')})\n`;
  f.ps_overlay = (b) => `${P(b)}:overlay(${v(b, 'TEXT', '""')})\n`;
  f.ps_bubble = (b) => `${P(b)}:bubble(${v(b, 'TEXT', '""')})\n`;
  f.ps_say = (b) => `${P(b)}:say(${v(b, 'TEXT', '""')})\n`;
  f.ps_give = (b) => `${P(b)}:give(${v(b, 'ITEM', '2')}, ${v(b, 'COUNT', '1')})\n`;
  f.ps_take = (b) => `${P(b)}:take(${v(b, 'ITEM', '2')}, ${v(b, 'COUNT', '1')})\n`;
  f.ps_gems_add = (b) => `${P(b)}:addGems(${v(b, 'AMOUNT', '0')})\n`;
  f.ps_gems_take = (b) => `${P(b)}:addGems(-(${v(b, 'AMOUNT', '0')}))\n`;
  f.ps_xp = (b) => `${P(b)}:addXp(${v(b, 'AMOUNT', '1')})\n`;
  f.ps_warp = (b) => `${P(b)}:warp(${v(b, 'WORLD', '"START"')})\n`;
  f.ps_teleport = (b) => `${P(b)}:teleport(${v(b, 'X', '0')}, ${v(b, 'Y', '0')})\n`;
  f.ps_kick = (b) => `${P(b)}:kick(${v(b, 'TEXT', '""')})\n`;
  f.ps_sound = (b) => `${P(b)}:sound(${q(b.getFieldValue('SOUND'))})\n`;

  f.ps_player_prop = (b) => [`${v(b, 'PLAYER', 'player')}.${b.getFieldValue('PROP')}`, Order.HIGH];
  f.ps_player_has = (b) => [`${P(b)}:has(${v(b, 'ITEM', '2')}, ${v(b, 'COUNT', '1')})`, Order.HIGH];
  f.ps_player_count = (b) => [`${P(b)}:count(${v(b, 'ITEM', '2')})`, Order.HIGH];
  f.ps_player_staff = (b) => [`${P(b)}:isStaff()`, Order.HIGH];
  f.ps_all_players = () => ['server.players()', Order.HIGH];
  f.ps_world_players = (b) => [`(${P(b)}.world and ${P(b)}.world:players() or {})`, Order.HIGH];
  f.ps_online_count = () => ['server.online()', Order.HIGH];
  f.ps_find_player = (b) => [`server.player(${v(b, 'NAME', '""')})`, Order.HIGH];

  f.ps_item = (b) => [String(Number(b.getFieldValue('ID')) || 0), Order.ATOMIC];
  f.ps_item_named = (b) => [`(item.find(${q(b.getFieldValue('NAME'))}) or 0)`, Order.HIGH];
  f.ps_item_name = (b) => [`(item.name(${v(b, 'ITEM', '0')}) or "?")`, Order.HIGH];

  const inWorld = (b, call) => `if ${P(b)}.world then ${P(b)}.world:${call} end\n`;
  f.ps_world_msg = (b) => inWorld(b, `message(${v(b, 'TEXT', '""')})`);
  f.ps_broadcast = (b) => `server.broadcast(${v(b, 'TEXT', '""')})\n`;
  f.ps_set_block = (b) => inWorld(b, `setBlock(${v(b, 'X', '0')}, ${v(b, 'Y', '0')}, ${v(b, 'ITEM', '0')})`);
  f.ps_get_block = (b) => [`(${P(b)}.world and ${P(b)}.world:getBlock(${v(b, 'X', '0')}, ${v(b, 'Y', '0')}) or 0)`, Order.HIGH];
  f.ps_drop = (b) => inWorld(b, `drop(${v(b, 'X', '0')}, ${v(b, 'Y', '0')}, ${v(b, 'ITEM', '2')}, ${v(b, 'COUNT', '1')})`);

  f.ps_player_set = (b) => `${P(b)}:set(${q(b.getFieldValue('KEY'))}, ${v(b, 'VALUE', 'nil')})\n`;
  f.ps_player_add = (b) => { const p = P(b); const k = q(b.getFieldValue('KEY')); return `${p}:set(${k}, (tonumber(${p}:get(${k}, 0)) or 0) + ${v(b, 'N', '1')})\n`; };
  f.ps_player_get = (b) => [`${P(b)}:get(${q(b.getFieldValue('KEY'))}, 0)`, Order.HIGH];
  f.ps_server_set = (b) => `server.set(${q(b.getFieldValue('KEY'))}, ${v(b, 'VALUE', 'nil')})\n`;
  f.ps_server_add = (b) => { const k = q(b.getFieldValue('KEY')); return `server.set(${k}, (tonumber(server.get(${k}, 0)) or 0) + ${v(b, 'N', '1')})\n`; };
  f.ps_server_get = (b) => [`server.get(${q(b.getFieldValue('KEY'))}, 0)`, Order.HIGH];
  f.ps_now = () => ['os.time()', Order.HIGH];

  f.ps_after = (b) => `after(${v(b, 'SECONDS', '1')}, function()\n${body(b)}end)\n`;
  f.ps_log = (b) => `print(${v(b, 'TEXT', '""')})\n`;

  f.ps_dialog = (b) => {
    const lines = body(b, 'LINES');
    const press = body(b, 'PRESS');
    const indent = (code) => code.replace(/^(?=.)/gm, gen.INDENT);
    return `do\n${gen.INDENT}local menu = Dialog(${v(b, 'TITLE', '""')}, ${v(b, 'ICON', '0')})\n${lines}${gen.INDENT}menu:show(${P(b)}, function(player, button, values)\n${indent(press)}${gen.INDENT}end)\nend\n`;
  };
  f.ps_menu_text = (b) => `menu:text(${v(b, 'TEXT', '""')})\n`;
  f.ps_menu_item = (b) => `menu:item(${v(b, 'ITEM', '2')}, ${v(b, 'TEXT', '""')})\n`;
  f.ps_menu_button = (b) => `menu:button(${q(b.getFieldValue('ID'))}, ${v(b, 'TEXT', '""')})\n`;
  f.ps_menu_input = (b) => `menu:input(${q(b.getFieldValue('ID'))}, ${v(b, 'TEXT', '""')}, "", 30)\n`;
  f.ps_menu_spacer = () => 'menu:spacer()\n';
  f.ps_menu_pressed = (b) => [`(button == ${q(b.getFieldValue('ID'))})`, Order.HIGH];
  f.ps_menu_value = (b) => [`(values[${q(b.getFieldValue('ID'))}] or "")`, Order.HIGH];

  /* @note names the events use: a variable made in the editor gets another name instead of hiding one of these */
  gen.addReservedWords('player,world,message,text,words,x,y,block,button,values,menu,server,item,on,command,after,every,cancel,Dialog,split,os');

  /* ---------- the toolbox ---------- */
  const sh = (type, fields) => ({ shadow: { type, ...(fields ? { fields } : {}) } });
  const num = (n) => sh('math_number', { NUM: n });
  const txt = (t) => sh('text', { TEXT: t });
  const me = () => sh('ps_player');
  const item = (id) => sh('ps_item', { ID: id });
  const b = (type, inputs) => ({ kind: 'block', type, ...(inputs ? { inputs } : {}) });

  const toolbox = {
    kind: 'categoryToolbox',
    contents: [
      { kind: 'category', name: 'Events', categorystyle: 'ps_event', contents: [
        { kind: 'label', text: 'Start here: everything goes inside one of these' },
        b('ps_on_join'), b('ps_on_command'), b('ps_on_chat'), b('ps_on_break'), b('ps_on_punch'), b('ps_on_place'), b('ps_on_wrench'),
        b('ps_on_enter'), b('ps_on_exit'), b('ps_on_leave'), b('ps_every'), b('ps_on_start'),
        { kind: 'label', text: 'What the event is about' },
        b('ps_player'), b('ps_command_text'), b('ps_command_word'), b('ps_message'), b('ps_tile_block'), b('ps_tile_x'), b('ps_tile_y'), b('ps_stop'),
      ] },
      { kind: 'category', name: 'Player', categorystyle: 'ps_player', contents: [
        b('ps_msg', { TEXT: txt('Hello!'), PLAYER: me() }),
        b('ps_overlay', { TEXT: txt('You won!'), PLAYER: me() }),
        b('ps_bubble', { TEXT: txt('Hi!'), PLAYER: me() }),
        b('ps_say', { PLAYER: me(), TEXT: txt('I love this server!') }),
        b('ps_give', { PLAYER: me(), COUNT: num(10), ITEM: item(2) }),
        b('ps_take', { COUNT: num(1), ITEM: item(242), PLAYER: me() }),
        b('ps_gems_add', { PLAYER: me(), AMOUNT: num(100) }),
        b('ps_gems_take', { AMOUNT: num(100), PLAYER: me() }),
        b('ps_xp', { PLAYER: me(), AMOUNT: num(50) }),
        b('ps_sound', { PLAYER: me() }),
        b('ps_warp', { PLAYER: me(), WORLD: txt('START') }),
        b('ps_teleport', { PLAYER: me(), X: num(50), Y: num(20) }),
        b('ps_kick', { PLAYER: me(), TEXT: txt('Bye!') }),
        { kind: 'label', text: 'About a player' },
        b('ps_player_prop', { PLAYER: me() }),
        b('ps_player_has', { PLAYER: me(), COUNT: num(1), ITEM: item(242) }),
        b('ps_player_count', { ITEM: item(242), PLAYER: me() }),
        b('ps_player_staff', { PLAYER: me() }),
        b('ps_find_player', { NAME: txt('Psy7') }),
      ] },
      { kind: 'category', name: 'Items', categorystyle: 'ps_item', contents: [
        b('ps_item'), b('ps_item_named'), b('ps_item_name', { ITEM: item(242) }),
      ] },
      { kind: 'category', name: 'World', categorystyle: 'ps_world', contents: [
        b('ps_world_msg', { PLAYER: me(), TEXT: txt('Event starting!') }),
        b('ps_broadcast', { TEXT: txt('Welcome everyone!') }),
        b('ps_set_block', { ITEM: item(2), X: sh('ps_tile_x'), Y: sh('ps_tile_y'), PLAYER: me() }),
        b('ps_get_block', { X: num(50), Y: num(20), PLAYER: me() }),
        b('ps_drop', { COUNT: num(5), ITEM: item(112), X: sh('ps_tile_x'), Y: sh('ps_tile_y'), PLAYER: me() }),
        b('ps_all_players'), b('ps_world_players', { PLAYER: me() }), b('ps_online_count'),
      ] },
      { kind: 'category', name: 'Menus', categorystyle: 'ps_dialog', contents: [
        { kind: 'block', type: 'ps_dialog', inputs: {
          PLAYER: me(), TITLE: txt('Shop'), ICON: item(242),
          LINES: { block: { type: 'ps_menu_text', inputs: { TEXT: txt('What do you want?') }, next: { block: { type: 'ps_menu_button', fields: { ID: 'buy' }, inputs: { TEXT: txt('Buy 10 Dirt for 50 gems') } } } } },
          PRESS: { block: { type: 'controls_if', inputs: { IF0: { block: { type: 'ps_menu_pressed', fields: { ID: 'buy' } } } } } },
        } },
        b('ps_menu_text', { TEXT: txt('Some text') }),
        b('ps_menu_item', { ITEM: item(242), TEXT: txt('A World Lock') }),
        b('ps_menu_button', { TEXT: txt('Buy') }),
        b('ps_menu_input', { TEXT: txt('How many?') }),
        b('ps_menu_spacer'),
        b('ps_menu_pressed'), b('ps_menu_value'),
      ] },
      { kind: 'category', name: 'Saved data', categorystyle: 'ps_data', contents: [
        b('ps_player_add', { PLAYER: me(), N: num(1) }),
        b('ps_player_get', { PLAYER: me() }),
        b('ps_player_set', { VALUE: num(0), PLAYER: me() }),
        b('ps_server_add', { N: num(1) }),
        b('ps_server_get'),
        b('ps_server_set', { VALUE: num(0) }),
        b('ps_now'),
      ] },
      { kind: 'category', name: 'Timers', categorystyle: 'ps_timer', contents: [
        b('ps_after', { SECONDS: num(5) }), b('ps_log', { TEXT: txt('it works') }),
      ] },
      { kind: 'sep' },
      { kind: 'category', name: 'Logic', categorystyle: 'logic_category', contents: [
        b('controls_if'), { kind: 'block', type: 'controls_if', extraState: { hasElse: true } },
        b('logic_compare'), b('logic_operation'), b('logic_negate'), b('logic_boolean'), b('logic_null'),
      ] },
      { kind: 'category', name: 'Loops', categorystyle: 'loop_category', contents: [
        b('controls_repeat_ext', { TIMES: num(3) }),
        { kind: 'block', type: 'controls_forEach', inputs: { LIST: { block: { type: 'ps_all_players' } } } },
        b('controls_for', { FROM: num(1), TO: num(10), BY: num(1) }),
        b('controls_whileUntil'), b('controls_flow_statements'),
      ] },
      { kind: 'category', name: 'Math', categorystyle: 'math_category', contents: [
        b('math_number'), b('math_arithmetic', { A: num(1), B: num(1) }), b('math_random_int', { FROM: num(1), TO: num(10) }),
        b('math_round', { NUM: num(3.5) }), b('math_number_property', { NUMBER_TO_CHECK: num(0) }), b('math_modulo', { DIVIDEND: num(10), DIVISOR: num(3) }),
        b('math_constrain', { VALUE: num(50), LOW: num(1), HIGH: num(100) }),
      ] },
      { kind: 'category', name: 'Text', categorystyle: 'text_category', contents: [
        b('text'), b('text_join'), b('text_length', { VALUE: txt('abc') }), b('text_isEmpty', { VALUE: txt('') }),
        b('text_indexOf', { VALUE: { block: { type: 'variables_get' } }, FIND: txt('abc') }), b('text_changeCase', { TEXT: txt('abc') }),
      ] },
      { kind: 'category', name: 'Lists', categorystyle: 'list_category', contents: [
        b('lists_create_empty'), b('lists_create_with'), b('lists_length'), b('lists_isEmpty'), b('lists_getIndex'), b('lists_setIndex'),
      ] },
      { kind: 'category', name: 'Variables', categorystyle: 'variable_category', custom: 'VARIABLE' },
      { kind: 'category', name: 'Functions', categorystyle: 'procedure_category', custom: 'PROCEDURE' },
    ],
  };

  /* ---------- look ---------- */
  const style = (primary) => ({ colourPrimary: primary });
  const theme = Blockly.Theme.defineTheme('psnode', {
    name: 'psnode',
    base: Blockly.Themes.Classic,
    blockStyles: {
      ps_event: { ...style('#d4a017'), hat: 'cap' },
      ps_value: style('#c27c0e'),
      ps_player: style('#2a7de1'),
      ps_item: style('#7a5cd6'),
      ps_world: style('#1f9e6e'),
      ps_dialog: style('#16a2b8'),
      ps_data: style('#d0587e'),
      ps_timer: style('#e0703a'),
    },
    categoryStyles: {
      ps_event: { colour: '#d4a017' }, ps_player: { colour: '#2a7de1' }, ps_item: { colour: '#7a5cd6' }, ps_world: { colour: '#1f9e6e' },
      ps_dialog: { colour: '#16a2b8' }, ps_data: { colour: '#d0587e' }, ps_timer: { colour: '#e0703a' },
    },
    componentStyles: {
      workspaceBackgroundColour: '#0a1020',
      toolboxBackgroundColour: '#0f1729',
      toolboxForegroundColour: '#e8eefc',
      flyoutBackgroundColour: '#141f37',
      flyoutForegroundColour: '#e8eefc',
      flyoutOpacity: 0.97,
      scrollbarColour: '#2f4069',
      scrollbarOpacity: 0.8,
      insertionMarkerColour: '#ffffff',
      insertionMarkerOpacity: 0.3,
      cursorColour: '#3ddc84',
    },
    fontStyle: { family: '"Segoe UI", system-ui, sans-serif', size: 11 },
    startHats: true,
  });

  const workspace = Blockly.inject(host, {
    toolbox,
    theme,
    media: '/static/vendor/blockly/media/',
    renderer: 'zelos',
    trashcan: true,
    sounds: true,
    grid: { spacing: 24, length: 2, colour: '#1a2744', snap: true },
    zoom: { controls: true, wheel: true, startScale: 0.85, maxScale: 2, minScale: 0.4 },
    move: { scrollbars: true, drag: true, wheel: false },
  });

  /* ---------- load ---------- */
  const STARTER = { blocks: { languageVersion: 0, blocks: [
    { type: 'ps_on_join', x: 30, y: 30, inputs: { DO: { block: {
      type: 'ps_msg', inputs: { TEXT: { shadow: { type: 'text', fields: { TEXT: '`2Welcome to the server!' } } }, PLAYER: { shadow: { type: 'ps_player' } } },
    } } } },
    { type: 'ps_on_command', x: 30, y: 170, fields: { NAME: 'daily', DESC: 'Get 100 gems', ROLE: 'player' }, inputs: { DO: { block: {
      type: 'ps_gems_add', inputs: { PLAYER: { shadow: { type: 'ps_player' } }, AMOUNT: { shadow: { type: 'math_number', fields: { NUM: 100 } } } },
      next: { block: { type: 'ps_overlay', inputs: { TEXT: { shadow: { type: 'text', fields: { TEXT: '`2+100 gems!' } } }, PLAYER: { shadow: { type: 'ps_player' } } } } },
    } } } },
  ] } };
  let saved = '';
  try { saved = JSON.parse(document.getElementById('script-data').textContent).blocks || ''; } catch { saved = ''; }
  let loading = true;
  try {
    Blockly.serialization.workspaces.load(saved ? JSON.parse(saved) : STARTER, workspace);
  } catch (err) {
    ps.say(`Some blocks could not be loaded: ${err.message}`, 'bad-text');
  }

  /* ---------- the Lua preview ---------- */
  const preview = document.getElementById('lua-preview');
  const HEADER = '-- Made with the block editor. Change the blocks, not this code: saving the blocks makes it again.\n\n';
  function code() {
    return HEADER + gen.workspaceToCode(workspace).replace(/\n{3,}/g, '\n\n');
  }
  let previewTimer = 0;
  function refresh() {
    try { preview.textContent = code(); } catch (err) { preview.textContent = `-- the blocks could not be turned into Lua: ${err.message}`; }
  }
  workspace.addChangeListener((e) => {
    if (e.isUiEvent) return;
    if (!loading && e.type !== Blockly.Events.FINISHED_LOADING) ps.changed();
    clearTimeout(previewTimer);
    previewTimer = setTimeout(refresh, 250);
  });
  setTimeout(() => { loading = false; refresh(); }, 0);
  if (!saved) setTimeout(() => ps.changed(), 0); // @note the starter blocks aren't saved yet

  /* ---------- saving ---------- */
  async function saveNow() {
    const warnings = workspace.getAllBlocks(false).filter((blk) => blk.warning && blk.getSurroundParent());
    const json = JSON.stringify(Blockly.serialization.workspaces.save(workspace));
    let lua;
    try { lua = code(); } catch (err) { ps.say(`Not saved: ${err.message}`, 'bad-text'); return; }
    const ok = await ps.save({ blocks: json, source: lua });
    if (ok && warnings.length) ps.say(`Saved, but ${warnings.length} block${warnings.length === 1 ? ' is' : 's are'} in a place where ${warnings.length === 1 ? 'it doesn\'t' : 'they don\'t'} work. Look for the ⚠ sign.`, 'warn-text');
  }
  document.getElementById('save').addEventListener('click', saveNow);
  document.addEventListener('keydown', (e) => {
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') { e.preventDefault(); saveNow(); }
  });

  document.getElementById('to-code').addEventListener('click', async () => {
    if (!window.confirm('Turn this into a code script? You get the Lua below to edit, and the blocks are gone for good.')) return;
    const ok = await ps.save({ mode: 'code', source: code() });
    if (ok) window.location.reload();
  });

  /* @note Blockly needs to know when its box changes size */
  new ResizeObserver(() => Blockly.svgResize(workspace)).observe(host);
})();
