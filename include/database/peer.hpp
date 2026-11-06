#pragma once

#include <cmath> // @note std::floor
#include <deque> // @note std::deque

enum bgra : u_int
{
    BLUE  = 0xff000000,
    GREEN = 0x00ff0000,
    RED   = 0x0000ff00,
    ALPHA = 0x000000ff
};

#include "database.hpp"

/* id, count */
struct slot {
    slot(short _id = 0, short _count = 0) : id(_id), count(_count) {}
    short id{};
    short count{}; // @note total amount of that item

    ::blob to_blob() const;
};

/* x, y */
struct pos {
    pos() = default;
    pos(float _x, float _y) : x(_x), y(_y) {}
    pos(int _x, int _y)     : x(_x), y(_y) {}
    pos(u_int _x, u_int _y) : x(_x), y(_y) {} // @todo can this even be unsigned?

    float x, y;

    /* @param pixel 
    *   true to divide by 32 pixels for true tile. 
    *   false to multiply by 32 for percise position. 
    */
    pos by_32(bool pixel = false) const { 
        return (pixel) ? 
            pos{std::floor/*<auto>*/(x/32.0f), std::floor/*<auto>*/(y/32.0f)} : 
            pos{x*32.0f, y*32.0f}; 
    }

    int x_int() const { return std::floor<int>(x); }
    int y_int() const { return std::floor<int>(y); }

    u_int x_u_int() const { return std::floor<u_int>(x); }
    u_int y_u_int() const { return std::floor<u_int>(y); }

    auto operator<=>(const pos&) const = default;
};

struct Billboard {
    short id{0}; // @note the item they're selling
    bool show{};
    bool isBuying{};
    int price{1};
    bool perItem{}; // @note true if world locks per item, false if items per world lock
};

enum role : u_char {
    PLAYER, 
    MODERATOR, 
    DEVELOPER
};

enum pstate : int
{
    S_GHOST       = 0x00000001,
    S_DOUBLE_JUMP = 0x00000002,
    S_DEVIL_HORNS = 0x00000040, // @note shown while Cursed
    S_FROZEN      = 0x00000800, // @note Freeze Wand
    S_DUCT_TAPE   = 0x00002000,
    S_IRRADIATED  = 0x00080000  // @note the green glow of the Irradiated mod
};

class peer {
public:
    ~peer();

    bool load(const std::string &growid, const std::string password);
    bool load_by_uid(int uid);
    bool create_account(const std::string &growid, const std::string &password);

    bool exists(const std::string &growid);

    template<typename T>
    void mysql_insert(const std::string &column, const T &value);

    template<typename T>
    void mysql_update(const std::string &column, const T &value);

    template<typename T>
    T    mysql_select(const std::string &column, const std::string &arg = "", bool by_user_id = false); // @note by_user_id: WHERE uid instead of WHERE growid
    void mysql_select_all(bool by_user_id = false);
    void mysql_update_all();

    /* @note clothing as "id,id,..." so it can be saved in one column */
    std::string serialize_clothing() const;
    void load_clothing(const std::string &text);

    bool authenticated{false}; // @note true once logged in. nothing is saved to the database before that.

    int user_id{}; // @note unqiue user id.
    std::string growid{""}, password{""};
    std::time_t created_at{}; // @note when inserted in SQL (account age)
    long long playtime{}; // @note seconds played before this session
    std::time_t login_at{}; // @note when this session started
    /* @return seconds played, including this session */
    long long total_playtime() const { return this->playtime + ((this->login_at > 0) ? std::time(nullptr) - this->login_at : 0); }
    u_char role{};
    /* @note /playermode: staff and owner powers are switched off (not saved) so the server's owner can test like a normal player */
    bool player_mode{};
    u_char real_role{};
    /* @return true if `uid` is this peer and they're not in /playermode */
    bool owns(int uid) const { return !this->player_mode && uid != 0 && uid == this->user_id; }
    std::array<float, 10ull> clothing{}; // @note peer's clothing {id, clothing::}
    int daily_quest{}; // @note day (yyyymmdd, UTC) Crazy Jim's daily quest was last done
    /* @note self wrench menu (action/profile.cpp) */
    std::string wardrobe{};   // @note saved outfits: 10 item ids each, "id,id,...;id,id,..."
    int wardrobe_slots{2};    // @note outfits unlocked
    std::string notebook{};   // @note the player's own notes
    int daily_bonus{};        // @note day (yyyymmdd, UTC) the daily bonus was last claimed
    int bonus_streak{};       // @note days in a row the daily bonus was claimed
    int online_status{};      // @note 0 online, 1 busy, 2 away
    std::string growmojis{};  // @note unlocked Growmojis: ",wl,grow," (action/profile.cpp)
    /* @note Grow Pass (action/growpass.cpp) */
    int pass_season{};        // @note yyyymm the points below belong to
    int pass_points{};
    int pass_free{};          // @note bit per level: free reward claimed
    int pass_royal_claims{};  // @note bit per level: Royal reward claimed
    int pass_royal{};         // @note yyyymm of the season they have the Royal Grow Pass for
    int pass_breaks{};        // @note blocks broken towards the next point (not saved)
    int pass_harvests{};      // @note trees harvested towards the next point (not saved)
    /* @note Geiger Counter and Great Chameleon (action/radiation.cpp) */
    int geiger_charge{};          // @note seconds a Dead Geiger Counter has charged in hand (saved)
    long long irradiated_until{}; // @note unix seconds; the Irradiated mod (saved)
    /* @note Freeze, Curse and Ban Wands, Challenge Flags (action/special_items.cpp) */
    long long frozen_until{};     // @note ms (steady clock); 0 = not frozen
    long long cursed_until{};     // @note unix seconds; stuck in HELL until then (saved)
    struct
    {
        bool active{};
        std::string world{};
        long long start{};    // @note ms (steady clock)
        long long deadline{}; // @note ms; 0 = no time limit
    } challenge{};
    long long still_since{};      // @note ms: when the player last moved
    ::pos last_pos{-1, -1};
    long long geiger_found_since{}; // @note ms: standing on the radiation source since
    long long geiger_pulse{};     // @note ms: last ping
    long long geiger_charge_tick{}; // @note ms
    bool disguised{};             // @note the Great Chameleon blended them into a block
    long long disguise_ended{};   // @note ms: when the disguise last ended
    u_short transformed{};        // @note action/transform.cpp: the worn item showing its transformed look (0 = none)
    int flag_test{-1};            // @note /transformflag, /transformscan: forced OnFlagMay2019 bit (-1 = off)
    long long flag_scan_next{};   // @note ms: when /transformscan tries the next bit (0 = not scanning)
    /* @note the Carnival (action/carnival.cpp): Golden Ticket Booth credit, games won and the Ringmaster's quest (saved as one column) */
    struct carnival_data
    {
        int credit{};        // @note Rarity given to a Golden Ticket Booth that didn't add up to a ticket yet
        int wins{};          // @note Carnival games won (50 = Funtime Block)
        int step{};          // @note the Ringmaster's quest: 0 = none, 1..10 = the task being done
        int task{};          // @note ring_task::
        int target{};        // @note the item to bring, for RT_DELIVER_ITEM
        int goal{1};
        long long progress{};
        bool told{};         // @note "step done, go back to the Ringmaster" was said (not saved)
    } carnival{};
    ::pos magplant{-1, -1}; // @note the MAGPLANT 5000 this player's remote builds from (not saved)
    std::string magplant_world{};
    struct
    {
        bool active{};
        std::string world{};
        ::pos tile{};       // @note the water tile the line is in
        ::pos start{};      // @note where the player stood when casting (pixels)
        short bait{};
        long long next_bite{}; // @note steady clock ms
        long long bite{};      // @note when the last bite happened, 0 = none yet
    } fishing{}; // @note not saved
    std::vector<int> friends{}; // @note user ids of friends (loaded on log in)
    std::vector<int> ignored{}; // @note user ids this player ignores (Ignore Player): their chat and messages are hidden. until log off
    std::vector<int> friend_requests{}; // @note user ids who asked to be friends this session
    u_char punch_range{}, build_range{}; // @note extra reach in tiles from clothing (set in on::SetClothing)
    u_short mods{}; // @note clothing powers (mod:: in playmods.hpp), set in on::SetClothing
    u_char punch_effect{}; // @note last equipped clothing that has a effect. supporting 0-255 effects.

    int netid{}; // @note peer's netid is world identity. this will be useful for many packet sending
    u_char icon{}; // @note typing or busy icon
    std::string display_growid{}; // @note use this for displaying growid, never use ::growid
    std::string country{};
    float game_version{0.0f};

    u_int skin_color{ 2527912447 };
    u_int hair_color = bgra::GREEN | bgra::GREEN | bgra::RED | bgra::ALPHA; // @note value declines for specfic hair colors

    int state{}; // @note using pstate::

    ::Billboard billboard{};

    ::pos pos{}; // @note position 1D {x, y}
    ::pos rest_pos{}; // @note respawn position {x, y}
    std::string door_target{}; // @note door ID to appear at in the next world (doors set to WORLD:ID)
    bool facing_left{}; // @note peer is directed towards the left direction
    short pain_hp{ 10 };

    int slot_size{16}; // @note amount of slots this peer has | were talking total slots not itemed slots, to get itemed slots do slot.size()
    std::vector<slot> slots{}; // @note an array of each slot. storing {id, count}
    /*
    * @brief set slot::count to nagative value if you want to remove an amount. 
    * @return the remaining amount if exeeds 200. e.g. emplace(slot{0, 201}) returns 1.
    */
    u_short emplace(::slot slot);
    /*
    * @brief check if item exists in inventory (::slots)
    * @return slot count (0 if not exist)
    */
    u_short have_slot(::slot slot);
    /* @return empty backpack slots */
    int free_slots() const;
    /* @return true if `count` of `id` fits in the backpack */
    bool can_hold(short id, int count);
    ::blob serialize_inventory() const;

    std::vector<short> fav{};

    long long gems{0};
    long long piggy_bank{0}; // @note gems saved in the Piggy Bank (the button on the left of the world screen)
    std::string piggy_label{}; // @note what the Piggy Bank button shows now, so it's only resent when it changes
    bool mail_page_sent{}; // @note the mailbox got its page this session
    int mail_page{};       // @note the page index it asked for then
    int wl_storage{0};
    std::array<u_int, 2ull> level{ 1, 0 }; // {level, xp} XP formula credits: https://www.growtopiagame.com/forums/member/553046-kasete
    /*
    * @brief add XP safely, this function also handles level up.
    */
    void add_xp(ENetEvent &event, u_short value);

    /*
    * @brief recalculate transient effects based on clothing.
    */
    void update_effects();

    std::array<std::string, 6ull> recent_worlds{}; // @note recent worlds, a list of 6 worlds, once it reaches 7 it'll be replaced by the oldest
    std::array<std::string, 200ull> my_worlds{}; // @note first 200 relevant worlds locked by peer.
    
    std::deque<u_int> messages; // @note last 5 que messages sent time, this is used to check for spamming

    u_short fires_removed{};
    u_short gbc_pity{}; // @note GBC pity; for each 100 will receive super GBC

    /* @todo move outside */
    std::vector<::slot> trade_offers{};
    bool trade_accepted = false;
    bool trade_confirmed = false; // @note pressed "Do The Deal!" on the final dialog
    int trade_with{};    // @note user_id of the peer this peer is trading with. 0 = not trading
    int trade_request{}; // @note user_id this peer asked to trade with, until they answer

    long long banned_until{}; // @note unix time. 0 = not banned
    std::string ban_reason{};
    long long muted_until{};  // @note unix time. 0 = not muted
    std::time_t duct_tape_until{}; // @note unix time duct tape falls off
    u_int last_sb{};          // @note ticks() of the last /sb, for the cooldown
};

extern ENetHost* host;

/* @brief search specific peer(s), used in peers() */
enum class PeerSearch : u_char
{
    WORLD = 0,
    EVERYONE = 1
};
template <typename Func>
void peers(const std::string &world, PeerSearch search, Func &&fun)
{
    for (ENetPeer *peer = host->peers, *end = peer + host->peerCount; peer != end; ++peer)
    {
        if (peer->state != ENET_PEER_STATE_CONNECTED) continue;

        if (search == PeerSearch::WORLD)
        {
            ::peer *pPeer = static_cast<::peer*>(peer->data);
            if (pPeer->netid == 0 || (pPeer->recent_worlds.back() != world)) continue;
        }
        std::forward<Func>(fun)(peer);
    }
}

extern void safe_disconnect_peers(int signal);

class gameMessage {
    public:
        int packet_create{ 03 }; // @note NET_MESSAGE_GAME_MESSAGE

        std::string message{};
        u_char terminator{ '\0' };
};

extern ::blob compressMessage(const gameMessage &gameMessage);

enum class gamePacketState : int
{
    UPDATE               = 0x00000004,
    EXTENDED             = 0x00000008,
    MOVE_LEFT            = 0x00000010,
    MOVE_RIGHT           = 0x00000020,
    HIT_BY_LAVA          = 0x00000040,
    JUMP                 = 0x00000080,
    ITEM_ACTIVATE_OBJECT = 0x00004000
};
class gamePacket {
public:
    int packet_create{ 04 }; // @note NET_MESSAGE_GAME_PACKET

    int type{};
    union
    {
        int netid{},
            particle_effect;
    };
    int uid{}; // @todo understand this better @note so far I think this holds uid value
    int state{}; // @note enum gamePacketState
    union
    {
        float count{},
              waterspeed;
    };
    union
    {
        int id{},
            delay;
    };
    ::pos pos{}; // @note position 1D {x, y}
    ::pos speed{}; // @note player movement (velocity(x), gravity(y)), higher gravity = smaller jumps
    float idk{};
    ::pos punch{}; // @note punching/placing position 2D {x, y}
    u_int size{};
};

extern gamePacket make_gamePacket(const enet_uint8 *data);

/* put it back into it's original form */
extern ::blob compress_state(const gamePacket &gamePacket);

extern void send_inventory_state(ENetEvent &event);
