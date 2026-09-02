#ifndef TELEGRAM_OTA_H
#define TELEGRAM_OTA_H

// Handles a firmware document (*.bin) sent by an allowlisted chat:
// resolves the Telegram file URL, applies the OTA and restarts on success.
// Sends progress/result messages to the owner's chat.
void telegram_ota_handle_document(const char *bot_token,
                                  const char *file_id,
                                  const char *file_name);

#endif // TELEGRAM_OTA_H
