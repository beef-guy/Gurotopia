#pragma once

/* @brief posting to a Discord channel through a webhook (Channel settings > Integrations > Webhooks > Copy Webhook URL) */
namespace discord
{
    /* @note https://discord.com/api/webhooks/... read from the --discord-webhook file. empty = don't post */
    extern std::string webhook;

    /* @brief read the webhook URL from a file (first line). a missing file just means no Discord posting */
    extern void load_webhook(const std::string &file);

    /*
    * @brief post a message with an image. blocks until Discord answers, so call it on another thread
    * @return empty on success, otherwise what went wrong
    */
    extern std::string post_image(const std::string &url, const std::string &message, const std::string &filename, const std::vector<u_char> &png);
}
