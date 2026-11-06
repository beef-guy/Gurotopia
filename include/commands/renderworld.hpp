#pragma once

/* @brief /renderworld: draw the world you're in to a PNG (renders/WORLD.png) and post it to Discord if a webhook is set */
extern void renderworld(ENetEvent &event, const std::string_view text);

/* @brief tell players when their render is done. call from the main loop */
extern void render_tick();
