#pragma once

#include <string>
#include <unordered_map>
#include <utility>

extern u_char get_punch_id(u_int item_id);

/* @note the website's punch effect per item (item manager). they win over the table, 0 = no effect */
extern void set_punch_overrides(std::unordered_map<u_short, u_char> overrides);

extern void punch(ENetEvent& event, const std::string_view text);
