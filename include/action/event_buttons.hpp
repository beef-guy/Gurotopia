#pragma once

/*
* @note the buttons down the left of the world screen (the client calls them event buttons):
*   Daily Challenge, Piggy Bank and Mailbox (Mailbox lives in database/mail.hpp and action/mailbox.hpp).
*   the client draws them from JSON the server sends (OnEventButtonDataSet), and a tap on one sends
*   action|<buttonAction> back here unless the client has its own screen for it.
*/

constexpr long long PIGGY_MAX   = 1'500'000; // @note the most a Piggy Bank holds
constexpr long long PIGGY_SMASH = 50'000;    // @note it can be smashed once it has this many
constexpr short PIGGY_PRICE_DL  = 1;         // @note smashing costs 1 Diamond Lock (or 100 World Locks). real Growtopia charges real money here

/* @return every event button as the eventButtons={...} JSON of the login packet */
extern std::string event_buttons_json(const ::peer &peer);

/* @note send every left-side button again (on entering a world) */
extern void event_buttons_refresh(ENetPeer *peer);

/* @note gems the player just collected also go into their Piggy Bank */
extern void piggy_bank_add(ENetEvent &event, long long gems);

/* @note one tree of today's challenge item was harvested */
extern void daily_challenge_harvest(ENetEvent &event, short block_id);

/* @note mail the prizes for every finished day that hasn't been paid yet */
extern void daily_challenge_settle();

extern void daily_challenge_create_tables();

namespace action
{
    extern void openPiggyBank(ENetEvent& event, const std::string& header);
    extern void dailychallengemenu(ENetEvent& event, const std::string& header);
}

/* @note dialog_return for "piggy_bank" */
extern void piggy_bank(ENetEvent& event, const ::hPipe &hPipe);
