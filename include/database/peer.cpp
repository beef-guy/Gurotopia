#include "pch.hpp"
#include <algorithm>
#include "items.hpp"
#include "world.hpp"
#include "onVariant/SetClothing.hpp"
#include "onVariant/CountryState.hpp"
#include "database/titles.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "commands/punch.hpp"
#include "tools/string.hpp"
#include "tools/auth.hpp"
#include "tools/random.hpp"
#include "action/carnival.hpp"

#include "roles.hpp"
#include "peer.hpp"

bool peer::exists(const std::string &growid)
{
    ::hStmt hStmt{ "SELECT 1 FROM peer WHERE growid = ? LIMIT 1" };

    MYSQL_BIND param = make_bind_in(growid); // WHERE
    hStmt.bind_param(&param);
    hStmt.execute();

    return (!mysql_stmt_store_result(hStmt.pStmt) && mysql_stmt_num_rows(hStmt.pStmt) > 0);
}

::blob slot::to_blob() const
{
    ::blob blob;
    blob.i16(this->id);
    blob.i16(this->count);

    return blob;
}

template<typename T>
void peer::mysql_insert(const std::string &column, const T &value)
{
    ::hStmt hStmt{ std::format("INSERT INTO peer ({}) VALUES (?)", column).c_str() };

    MYSQL_BIND param = make_bind_in(value); // VALUES
    hStmt.bind_param(&param);
    hStmt.execute();
}
template void peer::mysql_insert<signed>(const std::string&, const signed&);
template void peer::mysql_insert<unsigned>(const std::string&, const unsigned&);
template void peer::mysql_insert<float>(const std::string&, const float&);
template void peer::mysql_insert<std::string>(const std::string&, const std::string&);

template<typename T>
void peer::mysql_update(const std::string &column, const T &value)
{
    ::hStmt hStmt{ std::format("UPDATE peer SET {} = ? WHERE growid = ?", column).c_str() };

    MYSQL_BIND params[2] = {
        make_bind_in(value),       // SET
        make_bind_in(this->growid) // WHERE
    };
    hStmt.bind_param(params);
    hStmt.execute();
}
template void peer::mysql_update<signed>(const std::string&, const signed&);
template void peer::mysql_update<unsigned>(const std::string&, const unsigned&);
template void peer::mysql_update<long long>(const std::string&, const long long&);
template void peer::mysql_update<float>(const std::string&, const float&);
template void peer::mysql_update<std::string>(const std::string&, const std::string&);

template<typename T>
T peer::mysql_select(const std::string &column, const std::string &arg, bool by_user_id)
{
    T value{};
    ::hStmt hStmt{ std::format("SELECT {}({}) FROM peer WHERE {} = ? LIMIT 1", arg, column, by_user_id ? "uid" : "growid").c_str() };

    MYSQL_BIND param = by_user_id ? make_bind_in(this->user_id) : make_bind_in(this->growid); // WHERE
    hStmt.bind_param(&param);

    u_long length = 0;
    MYSQL_BIND result = make_bind_out(value);
    result.length = &length;
    mysql_stmt_bind_result(hStmt.pStmt, &result);

    hStmt.execute();
    hStmt.fetch();
    if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, ::blob>)
        value.resize(length);

    return value;
}
/* since we will only select during mysql_select_all */ // @note add templates here if use select outside of this file.
template std::string peer::mysql_select<std::string>(const std::string&, const std::string&, bool); // @note tile_change.cpp (World Lock owner name)
template signed peer::mysql_select<signed>(const std::string&, const std::string&, bool); // @note moderation.cpp (offline players)
template long long peer::mysql_select<long long>(const std::string&, const std::string&, bool);

void peer::mysql_select_all(bool by_user_id)
{
    this->user_id    = this->mysql_select<signed>("uid", "", by_user_id);
    this->growid     = this->mysql_select<std::string>("growid", "", by_user_id);
    this->password   = this->mysql_select<std::string>("password", "", by_user_id);
    this->created_at = this->mysql_select<std::time_t>("created_at", "UNIX_TIMESTAMP", by_user_id);
    this->gems       = this->mysql_select<long long>("gems", "", by_user_id);
    this->wl_storage = this->mysql_select<signed>("wl_storage", "", by_user_id);
    this->level[0]   = this->mysql_select<signed>("level", "", by_user_id);
    this->level[1]   = this->mysql_select<signed>("xp", "", by_user_id);
    this->role_id    = this->mysql_select<signed>("role", "", by_user_id);
    this->role       = roles::powers(this->role_id);
    this->banned_until = this->mysql_select<long long>("banned_until", "", by_user_id);
    this->ban_reason   = this->mysql_select<std::string>("ban_reason", "", by_user_id);
    this->muted_until  = this->mysql_select<long long>("muted_until", "", by_user_id);
    this->playtime     = this->mysql_select<long long>("playtime", "", by_user_id);
    this->daily_quest  = this->mysql_select<signed>("daily_quest", "", by_user_id);
    this->piggy_bank   = this->mysql_select<long long>("piggy_bank", "", by_user_id);
    this->wardrobe       = this->mysql_select<std::string>("wardrobe", "", by_user_id);
    this->wardrobe_slots = std::clamp(this->mysql_select<signed>("wardrobe_slots", "", by_user_id), 2, 6);
    this->notebook       = this->mysql_select<std::string>("notebook", "", by_user_id);
    this->daily_bonus    = this->mysql_select<signed>("daily_bonus", "", by_user_id);
    this->bonus_streak   = this->mysql_select<signed>("bonus_streak", "", by_user_id);
    this->online_status  = std::clamp(this->mysql_select<signed>("online_status", "", by_user_id), 0, 2);
    this->growmojis         = this->mysql_select<std::string>("growmojis", "", by_user_id);
    this->pass_season       = this->mysql_select<signed>("pass_season", "", by_user_id);
    this->pass_points       = std::max(this->mysql_select<signed>("pass_points", "", by_user_id), 0);
    this->pass_free         = this->mysql_select<signed>("pass_free", "", by_user_id);
    this->pass_royal_claims = this->mysql_select<signed>("pass_royal_claims", "", by_user_id);
    this->pass_royal        = this->mysql_select<signed>("pass_royal", "", by_user_id);
    this->geiger_charge    = std::max(this->mysql_select<signed>("geiger_charge", "", by_user_id), 0);
    this->irradiated_until = this->mysql_select<long long>("irradiated_until", "", by_user_id);
    this->cursed_until = this->mysql_select<long long>("cursed_until", "", by_user_id);
    carnival_load(*this, this->mysql_select<std::string>("carnival", "", by_user_id));
    this->login_at     = std::time(nullptr);
    this->skin_color   = static_cast<u_int>(this->mysql_select<long long>("skin_color", "", by_user_id));
    const std::string clothing = this->mysql_select<std::string>("clothing", "", by_user_id);
    auto inventory   = this->mysql_select<::blob>("inventory", "", by_user_id);

    if (inventory.size() >= sizeof(int)/*slot_size*/ + sizeof(short)/*size*/)
    {
        int pos{};
        inventory.read_i32(this->slot_size, pos);

        short size{};
        inventory.read_i16(size, pos);
        this->slots.resize(std::max<short>(size, 0)); // @note a damaged save can't be negative
        for (::slot &slot : this->slots)
        {
            if (!inventory.read_i16(slot.id, pos)) break;
            if (!inventory.read_i16(slot.count, pos)) break;
        }
    }
    else
    {
        this->slot_size = 16;
        this->slots.resize(3ull); // @note default starter items
        this->slots[0ull] = ::slot{18, 1};   // @note fist
        this->slots[1ull] = ::slot{32, 1};   // @note wrench
        this->slots[2ull] = ::slot{9640, 1}; // @note my first world lock
        this->mysql_update_all();
    }
    this->load_clothing(clothing); // @note after the inventory, so only items peer still has are worn
}

std::string peer::serialize_clothing() const
{
    std::string text{};
    for (std::size_t i = 0; i < this->clothing.size(); ++i)
    {
        if (i != 0) text += ',';
        text += std::to_string(static_cast<int>(this->clothing[i]));
    }
    return text;
}

void peer::load_clothing(const std::string &text)
{
    this->clothing.fill(0.0f);
    std::size_t i = 0;
    for (const std::string &part : readch(text, ','))
    {
        if (i >= this->clothing.size()) break;
        const int id = atoi(part.c_str());
        if (id > 0 && static_cast<std::size_t>(id) < items.size() && this->have_slot(::slot(id, 0)) > 0 &&
            id_to_item(id).cloth_type == i)
            this->clothing[i] = static_cast<float>(id);
        ++i;
    }
    this->update_effects();
}

::blob peer::serialize_inventory() const
{
    ::blob blob{};
    blob.i32(this->slot_size);
    blob.i16(this->slots.size());
    for (const ::slot &slot : this->slots)
    {
        blob.push_back(slot.to_blob());
    }
    return blob;
}

void peer::mysql_update_all()
{
    if (this->growid.empty()) return;
    this->mysql_update("carnival", carnival_serialize(*this)); // @note action/carnival.cpp

    ::hStmt hStmt{ "UPDATE peer SET inventory = ?, gems = ?, level = ?, xp = ?, wl_storage = ?, clothing = ?, skin_color = ?, playtime = ?, daily_quest = ?, geiger_charge = ?, irradiated_until = ? WHERE growid = ?" };
    const ::blob inventory = this->serialize_inventory();
    const int level_value = this->level[0], xp_value = this->level[1];
    const std::string clothing = this->serialize_clothing();
    const long long skin = this->skin_color;
    const long long played = this->total_playtime();

    MYSQL_BIND params[12] = {
        make_bind_in(inventory),
        make_bind_in(this->gems),
        make_bind_in(level_value),
        make_bind_in(xp_value),
        make_bind_in(this->wl_storage),
        make_bind_in(clothing),
        make_bind_in(skin),
        make_bind_in(played),
        make_bind_in(this->daily_quest),
        make_bind_in(this->geiger_charge),
        make_bind_in(this->irradiated_until),
        make_bind_in(this->growid)
    };
    hStmt.bind_param(params);
    hStmt.execute();
}

bool peer::load(const std::string &growid, const std::string password)
{
    this->growid = growid;
    if (!this->exists(growid))
    {
        return false;
    }
    this->mysql_select_all(false);
    if (!auth::verify_password(password, this->password))
    {
        return false;
    }
    if (!auth::is_hashed(this->password)) // @note upgrade a plain-text password the first time its owner logs in
    {
        this->password = auth::hash_password(password);
        this->mysql_update("password", this->password);
    }
    this->authenticated = true;
    return true;
}

bool peer::load_by_uid(int uid)
{
    this->user_id = uid;
    this->mysql_select_all(true);
    this->authenticated = !this->growid.empty();
    return this->authenticated;
}

bool peer::create_account(const std::string &growid, const std::string &password)
{
    if (growid.empty() || growid.length() > 18 || !isalnum(growid)) return false; // @note fits peer.growid VARCHAR(18)
    if (password.empty() || password.length() > 18) return false; // @note fits peer.password VARCHAR(18)

    this->growid = growid;
    if (this->exists(growid))
    {
        return false;
    }
    this->mysql_insert("growid", growid);
    this->mysql_update("password", auth::hash_password(password));

    this->slots.resize(3ull); // @note default starter items
    this->slots[0ull] = ::slot{18, 1};   // @note fist
    this->slots[1ull] = ::slot{32, 1};   // @note wrench
    this->slots[2ull] = ::slot{9640, 1}; // @note my first world lock

    this->mysql_update_all();
    this->mysql_select_all(false);
    this->authenticated = true;
    return true;
}

peer::~peer()
{
    if (this->authenticated) this->mysql_update_all(); // @note a failed login must never overwrite the account it tried to use
}

u_short peer::emplace(::slot slot) 
{
    auto it = std::ranges::find(this->slots, slot.id, &::slot::id);

    if (it != this->slots.end()) 
    {
        const u_short excess = std::max(0, (it->count + slot.count) - 200);
        it->count = std::clamp(it->count + slot.count, 0, 200);
        if (it->count == 0)
        {
            const ::item &item = id_to_item(it->id);
            if (item.cloth_type < this->clothing.size()) this->clothing[item.cloth_type] = 0;
        }
        return excess;
    }
    if (slot.count <= 0) return 0; // @note can't take away an item peer doesn't have
    this->slots.emplace_back(std::move(slot)); // @note no such item in inventory, so we create a new entry.
    return 0;
}

u_short peer::have_slot(::slot slot)
{
    auto it = std::ranges::find(this->slots, slot.id, &::slot::id);

    if (it != this->slots.end())
        return it->count;
    return 0;
}

int peer::free_slots() const
{
    const int used = static_cast<int>(std::ranges::count_if(this->slots, [](const ::slot &slot) { return slot.count > 0; }));
    return this->slot_size - used;
}

bool peer::can_hold(short id, int count)
{
    const int have = this->have_slot(::slot(id, 0));
    if (have + count > 200) return false;
    return have > 0 || this->free_slots() > 0;
}

void peer::add_xp(ENetEvent &event, u_short value) 
{
    if (std::ranges::find(this->clothing, 1996.0f/*Ring of Wisdom*/) != this->clothing.end() && RandomRange(0, 10) == 0) value *= 2; // @note wiki: 10% chance of double XP
    ring_progress(event.peer, RT_XP, value);
    u_int &lvl = this->level.front();
    u_int &xp = this->level.back() += value; // @note factor the new xp amount
    const int old_level = static_cast<int>(lvl);

    for (; lvl < 125; )
    {
        const u_int xp_formula = 50 * (lvl * lvl + 2); // @author https://www.growtopiagame.com/forums/member/553046-kasete
        if (xp < xp_formula) break;

        xp -= xp_formula;
        lvl++;

        if (lvl == 50) 
        {
            modify_item_inventory(event, ::slot{1400, 1}); // @note Mini Growtopian
            /* @todo based on account age give peer other items... */
        }
        send_varlist(event.peer, { "OnPlayerLeveledUp", (unsigned)lvl });
        send_varlist(event.peer, { "OnParticleEffect", 46u, CL_Vec2f{this->pos.x + 16.0f, this->pos.y + 16.0f}, 0.0f, 0.0f });

        std::string message = std::format("{} is now level {}!", this->display_growid, lvl);
        send_varlist(event.peer, { "OnTalkBubble", this->netid, message, 0u });
        on::ConsoleMessage(event.peer, message);
    }
    if (static_cast<int>(lvl) != old_level) titles::leveled_up(event.peer, old_level, static_cast<int>(lvl)); // @note Max Level at 125, and the website's level titles
}

void peer::update_effects()
{
    this->punch_effect = 0;
    for (float cloth : this->clothing)
    {
        u_char punch_id = get_punch_id((u_int)cloth);
        if (punch_id != 0) // @note an actual change rather than no effect.
            this->punch_effect = punch_id;
    }
    /* @note the held item's effect wins (a gun or sword over a hat), like the real game */
    if (u_char hand = get_punch_id(static_cast<u_int>(this->clothing[clothing::HAND])); hand != 0) this->punch_effect = hand;
}

ENetHost *host;

void safe_disconnect_peers(int signal)
{
    peers("", PeerSearch::EVERYONE, [](auto *p) { enet_peer_disconnect(p, 0); });
    enet_host_flush(host);
}

::blob compressMessage(const gameMessage &gameMessage)
{
    ::blob blob{};

    blob.i32(gameMessage.packet_create);
    for (char c : gameMessage.message) blob.u8(c);
    blob.u8(gameMessage.terminator);

    return blob;
}

gamePacket make_gamePacket(const enet_uint8 *data) 
{
    const int   *i32 = reinterpret_cast<const int*>(data);
    const u_int *u32 = reinterpret_cast<const u_int*>(data);
    const float *f32 = reinterpret_cast<const float*>(data);

    return gamePacket{
        .type  = i32[1],
        .netid = i32[2],
        .uid   = i32[3],
        .state = i32[4],
        .count = f32[5],
        .id    = i32[6],
        .pos   = ::pos{f32[7], f32[8]},
        .speed = ::pos{f32[9], f32[10]},
        .idk   = f32[11],
        .punch = ::pos{i32[12], i32[13]},
        .size  = u32[14]
    };
}

::blob compress_state(const gamePacket &gamePacket) 
{
    ::blob blob{};
    
    blob.i32(gamePacket.packet_create);
    blob.i32(gamePacket.type);
    blob.i32(gamePacket.netid);
    blob.i32(gamePacket.uid);
    blob.i32(gamePacket.state);
    blob.f32(gamePacket.count);
    blob.i32(gamePacket.id);
    blob.f32(gamePacket.pos.x);
    blob.f32(gamePacket.pos.y);
    blob.f32(gamePacket.speed.x);
    blob.f32(gamePacket.speed.y);
    blob.f32(gamePacket.idk);
    blob.i32(gamePacket.punch.x);
    blob.i32(gamePacket.punch.y);
    blob.i32(gamePacket.size);

    return blob;
}

void send_inventory_state(ENetEvent &event)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    ::blob blob = compress_state(::gamePacket{
        .type = 0x09, // @note PACKET_SEND_INVENTORY_STATE
        .netid = pPeer->netid,
        .state = (int)gamePacketState::EXTENDED
    });
    blob.u8(0x01); // @note enable flag for big backpack

    blob.push_back(pPeer->serialize_inventory());

    ENetPacket *packet = enet_packet_create(blob.data().data(), blob.size(), ENET_PACKET_FLAG_RELIABLE);
	if (enet_peer_send(event.peer, 0, packet)) enet_packet_destroy(packet);
}
