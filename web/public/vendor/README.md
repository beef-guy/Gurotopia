Third-party files for the Lua script editor (web/src/views/server-script.ejs). Served as-is from /static/vendor/.

- blockly/ — Blockly 12.5.1 (https://github.com/google/blockly), Apache License 2.0.
  blockly_compressed.js, blocks_compressed.js, lua_compressed.js, msg/en.js (as en.js) and media/ from the npm package.
- codemirror/ — CodeMirror 5.65.19 (https://codemirror.net/5/), MIT License (LICENSE).
  lib/codemirror.js and .css, mode/lua/lua.js, addon/edit/matchbrackets.js and closebrackets.js.
- luaparse/ — luaparse 0.3.1 (https://github.com/fstirlitz/luaparse), MIT License (LICENSE). Checks Lua syntax in the browser.

To update one, take the same files from the newer npm package (npm pack <name>@<version>).
