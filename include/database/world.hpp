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

/* @brief Storage Box Xtreme contents */
struct storage
{
    ::pos pos{};
    std::vector<::slot> items{};
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
}
