#ifndef UI_H
#define UI_H

#include "client.h"
#include <stddef.h>

/*
 * Palette "arena multiplayer" - toni vivaci oceano/corallo, diversi sia
 * dalla palette originale che da quella militare. I NOMI delle macro
 * restano invariati apposta: sono usati anche in client.c, quindi
 * cambiando solo i valori qui dentro il tema si propaga ovunque senza
 * toccare altro codice.
 */
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[38;5;203m"  /* corallo - colpi/errori */
#define COLOR_GREEN   "\033[38;5;49m"   /* smeraldo - navi/successo */
#define COLOR_YELLOW  "\033[38;5;220m"  /* oro - avvisi/mancati */
#define COLOR_BLUE    "\033[38;5;39m"   /* blu oceano - acqua */
#define COLOR_CYAN    "\033[38;5;51m"   /* acquamarina - titoli */
#define COLOR_WHITE   "\033[97m"        /* bianco - testo enfatizzato */
#define COLOR_GRAY    "\033[38;5;245m"  /* grigio chiaro - testo secondario */
#define COLOR_BOLD    "\033[1m"

void ui_clear_screen(void);
void ui_show_banner(void);
void ui_show_lobby_menu(void);
void ui_show_boards(const Client *client);
void ui_show_my_board(const Client *client);
void ui_prompt_login(char *username, size_t max_len);
void ui_show_status(const char *message);
void ui_show_error(const char *message);
void ui_show_success(const char *message);
void ui_read_command(char *buffer, size_t buf_size);
void ui_flush_input(void);
int ui_wait_input(int seconds);
void ui_show_placement_instructions(int ship_size, int ship_num);
void ui_show_fire_instructions(void);
void ui_show_fire_result(const char *result, int row, int col);
void ui_show_game_list(const char *list);
void ui_show_game_over(bool victory);
void ui_prompt_rematch(void);

#endif
