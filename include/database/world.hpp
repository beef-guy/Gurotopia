#pragma once

enum wstate3 : u_char
{
    S_RIGHT   = 0x00,
    S_LOCKED  = 0x10,
    S_LEFT    = 0x20,
    S_SPLICED = 0x1d, // @todo understand the percise flags.
    S_TOGGLE  = 0x40,
    S_PUBLIC  = 0x80,

    S_EXTRA   = 0x01, // @note tile has extra data after its flags (doors, signs, locks...)
    S_PARENT  = 0x02  // @note tile belongs to an area lock; state[0..1] holds the lock's tile index
};

/* locks that only occupy a number of tiles, and not the whole world. */
#define is_tile_lock(id) (id == 202/*small*/ || id == 204/*big*/ || id == 206/*huge*/ || id == 4994/*builder*/)

/* @return how many tiles an area lock covers. source: https://growtopia.fandom.com/wiki/Locks */
inline int tile_lock_size(int id)
{
    switch (id)
    {
        case 202: return 10;   // @note Small Lock
        case 204: return 48;   // @note Big Lock
        case 206: return 200;  // @note Huge Lock
        case 4994: return 200; // @note Builder's Lock
    }
    return 0;
}

enum wstate4 : u_char
{
    S_WATER =    0x04,
    S_GLUE =     0x08,
    S_SILENCED = 0x02, // @note tile flag 0x200: a silenced dice / music block
    S_FIRE =     0x10,
    /* paint buckets */
    S_RED =      0x20,
    S_GREEN =    0x40,
    S_YELLOW =   S_RED | S_GREEN,
    S_BLUE =     0x80,
    S_AQUA =     S_GREEN | S_BLUE,
    S_PURPLE =   S_RED | S_BLUE,
    S_CHARCOAL = S_RED | S_GREEN | S_BLUE,
    S_VANISH =   S_RED | S_YELLOW | S_GREEN | S_AQUA | S_BLUE | S_PURPLE | S_CHARCOAL
};

enum lock_state : u_char
{
    DISABLE_MUSIC = 0x10,
    DISABLE_MUSIC_RENDER = 0x20,
    RAINBOWS = 0x80
};

char get_type(const ::item &item);

#include "database.hpp"

struct block 
{
    block(short _fg = 0, short _bg = 0) : 
        fg(_fg), bg(_bg), state(0, 0, 0, 0) {}
    
    short fg{0}, bg{0};
    u_char state[4];

    u_char hits[2] = {0, 0}; // @note fg, bg

    void reset();

    ::blob to_blob() const;
};
#define cord(x,y) ((y) * 100 + (x))

struct door 
{
    door(std::string _label, std::string _dest, std::string _id, ::pos _pos) : 
        label(_label), dest(_dest), id(_id), pos(_pos) {}

    std::string label{};
    std::string dest{};
    std::string id{};
    bool is_public{true}; // @note false: only people who can build here may walk through

    ::pos pos{};

    ::blob to_blob() const;
};

struct sign 
{
    sign(std::string _label, ::pos _pos) : 
        label(_label), pos(_pos) {}

    std::string label{};
    u_int idk{0xffffffff}; // @note unsigned bool. 0xffffffff = false and 1 = true.

    ::pos pos{};

    ::blob to_blob() const;
};

struct tree
{
    tree(u_int _tick, u_char _fruit, ::pos _pos) : 
        tick(_tick), fruit(_fruit), pos(_pos) {}

    /* @note MAP data */
    u_int tick{}; // @note last modified in seconds
    u_char fruit{};

    ::pos pos{};

    ::blob to_blob(bool seconds = false) const;
};

struct display
{
    display(u_int _id, ::pos _pos) : 
        id(_id), pos(_pos) {}

    u_int id{};
    ::pos pos{};
};

struct random_block
{
    random_block(u_char _value, ::pos _pos) : 
        value(_value), pos(_pos) {}

    u_char value{};
    ::pos pos{};
};

struct object 
{
    object(u_short _id = 0, u_short _count = 0, ::pos _pos = {0, 0}, u_int _uid = 0) : 
        id(_id), count(_count), pos(_pos), uid(_uid) {}
    u_short id{};
    u_short count{};
    ::pos pos{};

    u_int uid{};

    ::blob to_blob() const;
};

/* @brief Small, Big, Huge and Builder's Lock. the tiles it covers carry S_PARENT and the lock's tile index */
struct area_lock
{
    ::pos pos{};
    int owner{};
    std::vector<int> access{}; // @note user ids
    bool is_public{};
    bool ignore_air{}; // @note only lock tiles that have something on them
    u_char builder_mode{}; // @note Builder's Lock: 0 build & break, 1 only building, 2 only breaking (for people with access)
};

/* @brief a Vending Machine's stock. price > 0 is World Locks per item, price < 0 is items per World Lock */
struct vending
{
    ::pos pos{};
    short id{};
    int count{};
    int price{};
    int earned{}; // @note World Locks waiting for the owner
};

/* @brief when a provider (Cow, Chicken, ATM, Science Station...) was last collected */
struct provider
{
    ::pos pos{};
    u_int tick{}; // @note ticks() when placed or last collected
};

/* @brief a letter in a Mailbox, or a line on a Bulletin Board */
struct letter
{
    u_int id{}; // @note stays the same while the letter exists, so a delete button can't hit the wrong letter
    int uid{};
    std::string name{};
    std::string text{};
};

/* @brief Mailbox and Bulletin Board contents. is_public and hide_names are Bulletin Board settings */
struct mailbox
{
    ::pos pos{};
    std::vector<::letter> letters{};
    u_int next_id{1};
    bool is_public{};
    bool hide_names{};
};

/* @brief one gift in a Donation Box */
struct gift
{
    u_int id{};
    short item{};
    short count{};
    int uid{};
    std::string name{};
    std::string note{};
};

struct donation_box
{
    ::pos pos{};
    std::vector<::gift> gifts{};
    u_int next_id{1};
};

/* @brief the four items on a Display Shelf (0 = empty spot) */
struct shelf
{
    ::pos pos{};
    std::array<u_int, 4> items{};
};

/* @brief a Heart Monitor beats while the player who placed it is online */
struct heart_monitor
{
    ::pos pos{};
    int uid{};
    std::string name{};
};

/* @brief a Mannequin's sign text and clothes, in clothing:: order (hair .. charm) */
struct mannequin
{
    ::pos pos{};
    std::string label{};
    std::array<short, 9> clothes{};
};

/* @brief Weather Machine - Background (item = the background) and - Stuff (item that falls, gravity) */
struct weather_setting
{
    ::pos pos{};
    u_int item{};
    int gravity{50};
};

/* @brief MAGPLANT 5000, Gaia's Beacon and Unstable Tesseract: the item it stores, how many, collecting drops, building mode */
struct magplant
{
    ::pos pos{};
    short item{};
    int count{};
    bool collect{};
    bool remote{};
};
/* @return how many items this machine holds at most */
extern int magplant_limit(short machine);

/* @brief Fish Tank Port: the fish swimming in its tank (item id, pounds) */
struct fish_tank
{
    ::pos pos{};
    std::vector<std::pair<short, int>> fish{};
};
/* @brief Fish Wall Mount: the mounted fish, for good */
struct fish_mount
{
    ::pos pos{};
    short fish{};
    int lbs{};
    std::string label{};
};
/* @brief Lobster Trap: the bait inside and when it was last checked */
struct lobster_trap
{
    ::pos pos{};
    short bait{};
    int count{};
    u_int checked{}; // @note ticks()
};

/* @brief Tesseract Manipulator, Heart of Gaia, Techno-Organic Engine: gem-fuelled machines that break or harvest a target */
struct auto_machine
{
    ::pos pos{};
    int gems{};
    short target{}; // @note a block (Tesseract) or a seed (Heart of Gaia); the Techno-Organic Engine takes either
    bool on{};
    short item{};   // @note Techno-Organic Engine: the drop it keeps
    int count{};
    long long next{}; // @note not saved: when it acts next (ms)
    ::pos working{-1, -1}; // @note not saved: the block it is punching right now
};

/* @brief Fossil Prep Station and Geiger Charger: one item inside that is ready at a set time (unix seconds) */
struct timed_machine
{
    ::pos pos{};
    short item{};
    long long start{};
    long long ready{};
};

/* @brief Storage Box Xtreme contents */
struct storage
{
    ::pos pos{};
    std::vector<::slot> items{};
};

/* @brief Kranken's Galactic Block: the tint picked in its wrench menu (packed like Proton's MAKE_RGBA, 0xffffffff = no tint) */
struct kranken
{
    ::pos pos{};
    u_int color{0xffffffff};
};

/* @brief Safe Vault: a password-protected store of items (the password is kept hashed) */
struct safe_vault
{
    ::pos pos{};
    std::string password{}; // @note auth::hash_password(); empty = no password set yet
    std::vector<::slot> items{};
};

/* @brief Staging Platform, Game Block, Game Flag, Game Grave, Game Goal: which team the block belongs to (4 = no team) */
struct game_tile
{
    ::pos pos{};
    u_char team{4};
};

/* @brief a Game Generator's rules */
struct game_rules
{
    ::pos pos{};
    u_char mode{0};    // @note 0 Capture the Flag, 1 Reach the Goal
    u_char teams{2};   // @note 2-4
    u_char minutes{5}; // @note game length
    u_char goal{3};    // @note points a team needs to win
};

/* @brief a Challenge Timer: how long players get from the Start Flag to the End Flag, and the world's best time */
struct challenge_timer
{
    ::pos pos{};
    int seconds{60};
    int best_ms{};
    std::string best_name{};
};

/* @brief a cooking oven: the heat picked for the first ingredient and what is cooking */
struct oven
{
    ::pos pos{};
    u_char heat{}; // @note heat units per second: 3 Low, 6 Medium, 9 High (the game's own values; 0 = empty)
    long long since{}; // @note unix milliseconds when `parts` was last brought up to date
    std::vector<std::pair<short, int>> parts{}; // @note ingredient, heat it has taken so far
};
/* @return each ingredient with the heat it has taken up to now (action/cooking.cpp) */
extern std::vector<std::pair<short, int>> oven_parts_now(const ::oven &oven);

/* @brief a ghost floating around the world (an NPC the game draws itself). positions are in pixels, the middle of the ghost */
struct ghost
{
    u_char id{};    // @note the NPC id the game knows it by (1-255, shared with Ghost Jars)
    u_char kind{1}; // @note NPC type: 1 Ghost, 12 Mind Ghost
    float x{}, y{};   // @note where it was when it last changed course
    float dx{}, dy{}; // @note where it is floating to
    float speed{40.0f}; // @note pixels a second
    long long since{};  // @note ms (steady clock) when it left (x, y)
    long long next{};   // @note not before this (ms): pick somewhere new to float to
    long long held_until{}; // @note a Neutron Gun beam has it until then (ms)
    u_char into_jar{};      // @note a jar is pulling it in (that jar's id): it flies into the jar, then goes in
    long long in_jar_at{};  // @note ms (steady clock) when it reaches the jar
};

/* @brief a Ghost Jar or Ghost Trap that was set down. it pulls in a ghost above it for a while */
struct ghost_jar
{
    u_char id{};
    short item{}; // @note 3720 Ghost Jar, 6082 Ghost Trap
    float x{}, y{};
    long long placed{}; // @note ms (steady clock)
    bool done{};
    bool pulling{}; // @note a ghost is flying into it right now
};

/* @brief a puff of steam travelling through Steam Blocks. the game moves its own copy the same way, this one is for what steam does here (the Spirit Storage Unit, Steam Doors...) */
struct steam_puff
{
    short x{}, y{};  // @note the tile it is on
    u_char dir{};    // @note where it goes next: 0 right, 1 down, 2 left, 3 up
    u_char steps{};  // @note tiles travelled, it runs out after a while
};

/* @brief a Steam Engine's heat. Nuclear Fuel adds 100 degrees, above 500 it makes steam */
struct steam_engine
{
    ::pos pos{};
    int heat{};          // @note degrees
    long long cooled{};  // @note unix time the heat was last worked out
    long long next{};    // @note ms (steady clock): next puff
};

/* @brief a Steam Collector holding steam until it has enough for every way out */
struct steam_collector
{
    ::pos pos{};
    int held{};
    u_char from{};       // @note the side the last steam came in from (no steam goes back out there)
    long long until{};   // @note ms (steady clock): it lets go of the steam after 20 seconds
};

/* @brief a Spirit Storage Unit and how many ghosts are stored in it (each one is 5%) */
struct spirit_unit
{
    ::pos pos{};
    int ghosts{};
};

/* @brief someone hit with a Ban Wand can't come back until `until` (unix seconds) */
struct world_ban
{
    int uid{};
    long long until{};
};

/* @brief a running Game Generator game (not saved: a game ends when the world empties) */
struct battle
{
    bool active{};
    ::pos generator{};
    u_char mode{}, teams{2};
    int goal{3};
    long long ends{}; // @note unix seconds
    std::array<int, 4> scores{};
    std::vector<std::pair<int, u_char>> members{}; // @note user id, team
    struct carry { int uid{}; ::pos flag{}; u_char team{}; };
    std::vector<carry> carried{}; // @note flags being carried, and the tile each came from
    std::vector<std::pair<::pos, short>> changed{}; // @note busted Game Blocks and taken flags: the tile and what was there, rebuilt when the game ends
};

class world 
{
public:
    world(const std::string &name = "");/*DEFAULT*/
    ~world();

    static bool exists(const std::string& name);
    void save(); // @note write this world to the database

    template<typename T>
    void mysql_insert(const std::string& column, const T& value);

    template<typename T>
    void mysql_update(const std::string& column, const T& value);

    template<typename T>
    T    mysql_select(const std::string &column, const std::string &arg = "");
    void mysql_select_all();

    std::string name{};

    int owner{ 00 }; // @note owner of world using peer's user id.
    std::array<int, 20> access{}; // @note {user_id} @credit https://www.growtopiagame.com/forums/member/440629-yeldyt
    bool is_public{}; // @note checks if world is public to break/place
    u_char lock_state{0x00}; // @note uses lock_state::
    u_char minimum_entry_level{1}; // @note minimal level required to enter a world

    u_char visitors{}; // @note the current number of peers in a world, excluding invisable peers
    u_char netid_counter{}; // @note a number that only increases, this value resets during ~world()

    std::vector<::block> blocks; // @note all blocks, size of 1D meaning (6000) instead of 2D (100, 60)
    u_int last_object_uid{0};
    std::vector<::object> objects{};
    std::vector<::door> doors{};
    std::vector<::random_block> random_blocks{};
    std::vector<::sign> signs{};
    std::vector<::tree> trees{};
    std::vector<::area_lock> area_locks{};
    std::vector<::vending> vendings{};
    std::vector<::storage> storages{};
    std::vector<::display> displays{};
    std::vector<::provider> providers{};
    std::vector<::mailbox> mailboxes{}; // @note Mailboxes and Bulletin Boards
    std::vector<::donation_box> donation_boxes{};
    std::vector<::shelf> shelves{};
    std::vector<::heart_monitor> heart_monitors{};
    std::vector<::mannequin> mannequins{};
    std::vector<::weather_setting> weather_settings{};
    std::vector<::magplant> magplants{};
    std::vector<::fish_tank> fish_tanks{};
    std::vector<::fish_mount> fish_mounts{};
    std::vector<::lobster_trap> lobster_traps{};
    std::vector<::auto_machine> auto_machines{};
    std::vector<::timed_machine> timed_machines{}; // @note Fossil Prep Stations and Geiger Chargers
    std::vector<::kranken> krankens{};
    std::vector<::safe_vault> vaults{};
    std::vector<::game_tile> game_tiles{};
    std::vector<::game_rules> game_generators{};
    std::vector<::challenge_timer> challenge_timers{};
    std::vector<::world_ban> world_bans{};
    std::vector<::oven> ovens{};
    ::battle battle{}; // @note not saved
    std::vector<::spirit_unit> spirit_units{};
    std::vector<::steam_puff> steam_puffs{};           // @note not saved: steam only lasts a moment
    std::vector<::steam_engine> steam_engines{};       // @note saved (EX_STEAM_ENGINE)
    std::vector<::steam_collector> steam_collectors{}; // @note not saved
    std::vector<::ghost> ghosts{}; // @note saved (kind and place only), so a haunted world stays haunted
    std::vector<::ghost_jar> ghost_jars{}; // @note not saved: a jar only works for a few seconds
    std::vector<std::pair<long long, std::array<u_char, 2>>> npc_removals{}; // @note not saved: NPCs to take away after a moment (ms, {id, id})
    long long ghosts_checked{}; // @note unix seconds the world was last open, for ghosts moving in while nobody was here
    ::pos geiger{-1, -1}; // @note not saved: the hidden radiation source Geiger Counters look for
    std::vector<std::pair<::pos, int>> fossil_life{}; // @note not saved: Rock Hammer taps a Fossil Rock still needs
    std::vector<u_int> fresh_fossils{}; // @note not saved: dropped Fossils nobody picked up yet (a Fossil Brush can polish them)
    u_int xenonite{}; // @note the Xenonite Crystal's settings (xenonite:: in playmods.hpp). only one crystal per world

    /* @return the area lock that covers this tile, or nullptr */
    ::area_lock *lock_at(const ::pos &pos);
    /* @brief (re)claim tiles around an area lock, up to its size */
    void apply_area_lock(::area_lock &lock, u_short lock_id);
    /* @brief release every tile an area lock holds */
    void release_area_lock(const ::pos &lock_pos);
    /* @return true if peer may build (or break) on this tile */
    bool can_edit(const ::peer &peer, const ::pos &pos, bool breaking);
    /* @return true if a Signal Jammer is switched on here */
    bool jammed() const;

    ::pos spawn{}; // @note position of main door
    ::pos weather{};
    u_short base_weather{}; // @note the weather a blast world has when no weather machine runs (client WEATHER_TYPE id)

    ::blob serialize();
    /* @brief one tile as the client reads it: block, area lock parent, extra data */
    ::blob tile_blob(int index);
    ::blob extras_blob() const;
    void load_extras(::blob &blob);
};
extern std::vector<world> worlds;

/* @return true if a tile position is inside this world */
inline bool valid_tile(const ::world &world, const ::pos &pos)
{
    return pos.x >= 0 && pos.x < 100 && pos.y >= 0 && cord(pos.x, pos.y) < world.blocks.size();
}

extern void send_gameMessage(ENetPeer *peer, ::gameMessage gameMessage) ;

extern void send_data(ENetPeer &peer, const ::blob &blob);

extern void send_gamePacket(ENetPeer &peer, gamePacket &gamePacket);

extern void tile_apply_damage(ENetEvent &event, gamePacket gamePacket, block &block, u_int value);

/*
* @brief set slot::count to nagative value if you want to remove an amount. 
* @return the remaining amount if exeeds 200. e.g. emplace(slot{0, 201}) returns 1.
*/
extern u_short modify_item_inventory(ENetEvent &event, ::slot slot);

extern void item_change_object(ENetEvent& event, ::gamePacket gamePacket);

extern void merge_object(ENetEvent& event, ::slot slot, const ::pos &pos, ::world &world);
extern void remove_object(ENetEvent& event, signed uid);
extern int  add_object(ENetEvent& event, ::slot slot, const ::pos &pos, ::world &world);

extern void add_drop(ENetEvent &event, ::slot im, ::pos pos, ::world &world);
/* @brief a drop from breaking a block or harvesting a tree: a magplant collecting this item takes it, else it lands on the ground */
extern void add_break_drop(ENetEvent &event, ::slot im, ::pos pos, ::world &world);

extern void send_tile_update(ENetEvent &event, gamePacket s, ::block &b, ::world &world);

/* @brief send one tile to everyone in that world (for changes that don't come from someone in it) */
extern void world_tile_update(::world &world, const ::pos &pos);

/*
* @param speed actually just the particle color & visual, not the speed.
* @param id seems to be a 0xc8 multiplier for multiple particles. unsure.
*/
extern void send_particle_effect(ENetEvent &event, const ::pos &pos, ::pos speed, int id = 0xc8*0, float offset = 0.0f);

extern void remove_fire(ENetEvent &event, gamePacket gamePacket, ::block &block, ::world& world);

extern void fireworks(ENetEvent &event, const ::pos &pos);

extern void generate_world(::world &world);

extern bool door_mover(::world &world, const ::pos &pos);

namespace blast
{
    extern void thermonuclear(::world &world);
    /* @brief Beach, Desert, Harvest Moon, Mars, Undersea, Cave, Jungle, Monochrome, Treasure, SurgWorld, Bountiful, Candyland: reshape a new world. @return false if this item isn't one of them */
    extern bool themed(::world &world, int blast);
    /* @return true if this item makes a world */
    extern bool is_world_blast(int id);
}
